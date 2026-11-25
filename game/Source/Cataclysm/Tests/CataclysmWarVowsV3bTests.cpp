// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' THIRD SLICE, SECOND HALF, V3b, 2026-11-25, on the coordinating session's rulings that day:
 * Colossus Eternal's increases to maximum health counted twice, its save above half health and its bonus from current
 * health; Thousand Deaths' chain. The two new stats are measured by their probes in `CataclysmStatExemptionTests.cpp`;
 * this file holds the mechanisms in the engine and that a War player is handed them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV3bTest
{
	/** A possessed level 100 War player and its ability system. */
	struct FWarVowsV3bPlayer
	{
		explicit FWarVowsV3bPlayer(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
			State->SetLevelAndExperience(100, 0);
			System = State->GetCataclysmAbilitySystemComponent();
			Equipment = Character->GetEquipment();
		}
		bool IsUsable() const { return State && Character && System && Equipment; }

		/** Hand over one point in this capstone with this option, and refresh. */
		void Take(const TCHAR* Node, int32 Option) const
		{
			FCataclysmPassiveAllocation Allocation;
			Allocation.Add(FName(Node), 1);
			Allocation.SetChosenOption(FName(Node), Option);
			State->SetPassiveAllocation(Allocation, TArray<FName>());
			Equipment->RefreshAttributes(System);
		}

		float Stat(const TCHAR* Name) const
		{
			return System->StatForSkill(FName(Name), FGameplayTagContainer(), 0.0f);
		}

		TArray<FCataclysmPoolAction> Chains() const
		{
			TArray<FCataclysmPoolAction> Found;
			for (const FCataclysmPoolAction& Action : System->GetPoolActions())
			{
				if (Action.bFrenzyChain)
				{
					Found.Add(Action);
				}
			}
			return Found;
		}

		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** A bare actor with an ability system, 100,000 maximum health and no defences, standing at the origin. */
	struct FWarVowsV3bWearer
	{
		explicit FWarVowsV3bWearer(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			System = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			System->RegisterComponent();
			System->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			System->InitAbilityActorInfo(Actor, Actor);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			SetHealth(100000.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		}
		void SetHealth(float Health) const
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
		float Health() const
		{
			return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}
		void Grant(const TCHAR* Stat, float Value) const
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Value;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			System->SetStatInputs(MoveTemp(Inputs));
		}
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature on the monsters' side `Metres` from the origin along X, with no defences and 100,000 health. */
	ACataclysmEnemyCharacter* WarVowsV3bCreatureAt(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}

	float WarVowsV3bHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

#define CATACLYSM_WAR_VOWS_V3B_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Colossus Eternal
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3B_TEST(FCataclysmWarVowsV3bSaveTest,
	"Cataclysm.WarVowsV3b.ColossusEternalSavesFromALethalHitAboveHalfHealthWithNoCooldown")
{
	using namespace CataclysmWarVowsV3bTest;
	// A WEARER WITH 100,000 MAXIMUM HEALTH HOLDING THE SAVE AT 50, struck with real blows of 200,000 by a creature.
	// At 60,000 it is left at 1. At 1 the condition no longer holds, and the next lethal blow kills it (THE FIRST CONTROL).
	// Set back to 60,000 at once, the next lethal blow is saved again: no cooldown. At exactly 50,000, half health and
	// not above it, a lethal blow kills (THE SECOND CONTROL).
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3bWearer Wearer(World);
	ACataclysmEnemyCharacter* Striker = WarVowsV3bCreatureAt(World, 2.0f);
	if (!TestNotNull(TEXT("set-up: a creature"), Striker))
	{
		return false;
	}
	Wearer.Grant(UCataclysmAbilitySystemComponent::LethalHitSurvivedAboveHealthStat, 50.0f);
	const auto Lethal = [&](float Health)
	{
		Wearer.SetHealth(Health);
		World->TimeSeconds += 0.1f;
		UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 200000.0f);
		return Wearer.Health();
	};

	TestEqual(TEXT("at 60% health a lethal blow leaves 1"), Lethal(60000.0f), 1.0f, 0.01f);
	World->TimeSeconds += 0.1f;
	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 200000.0f);
	TestEqual(TEXT("control: at 1 the condition no longer holds and the next lethal blow kills"), Wearer.Health(), 0.0f,
			  0.01f);
	TestEqual(TEXT("at 60% again, at once, it is saved again: no cooldown"), Lethal(60000.0f), 1.0f, 0.01f);
	TestEqual(TEXT("control: at exactly half health, not above it, a lethal blow kills"), Lethal(50000.0f), 0.0f, 0.01f);
	return true;
}

CATACLYSM_WAR_VOWS_V3B_TEST(FCataclysmWarVowsV3bCurrentHealthScaleTest,
	"Cataclysm.WarVowsV3b.ACurrentHealthScaleGrantsItsValueForEveryStepOfHealthHeld")
{
	// COLOSSUS ETERNAL'S BONUS IN THE PIPELINE: 2 flat for every 100 points of current health, above 75% health. At
	// 8,000 of 10,000 it is 160. THE CONTROLS: at 7,000 of 10,000 the condition fails and it is 0; with health unknown
	// it is 0.
	FCataclysmStatModifier Bonus;
	Bonus.Bucket = ECataclysmStatBucket::Flat;
	Bonus.Source = ECataclysmModifierSource::PassiveKeystone;
	Bonus.Value = 2.0f;
	Bonus.Scale = ECataclysmStatScale::PerPointOfCurrentHealth;
	Bonus.ScaleStep = 100.0f;
	Bonus.Condition = ECataclysmStatCondition::HealthAbovePercent;
	Bonus.ConditionValue = 75.0f;
	const TArray<FCataclysmStatModifier> Modifiers = {Bonus};
	const auto Flat = [&Modifiers](const FCataclysmStatConditions& State)
	{
		return UCataclysmStatPipeline::Evaluate(0.0f, Modifiers, FGameplayTagContainer(), State).Flat;
	};

	TestEqual(TEXT("at 8,000 of 10,000 health, 2 for every 100: 160"),
			  Flat(FCataclysmStatConditions::FromHealth(8000.0f, 10000.0f)), 160.0f, 0.001f);
	TestEqual(TEXT("control: at 7,000 of 10,000 the condition fails: 0"),
			  Flat(FCataclysmStatConditions::FromHealth(7000.0f, 10000.0f)), 0.0f, 0.001f);
	TestEqual(TEXT("control: with health unknown, 0"), Flat(FCataclysmStatConditions()), 0.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Thousand Deaths
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3B_TEST(FCataclysmWarVowsV3bChainTest,
	"Cataclysm.WarVowsV3b.ThousandDeathsChainsToOneEnemyMoreForEachKillInTheFrenzy")
{
	using namespace CataclysmWarVowsV3bTest;
	// A WEARER WITH ONE ACTION IN THOUSAND DEATHS' SHAPE: 60% of what the attack sent, within 5 metres of the enemy
	// struck. The struck creature stands 2 metres from the wearer; others at 1, 2.5, 4 and 10 metres from it.
	// THE CONTROL: an attack that sent nothing (a spell, as the player character announces it) chains to nobody. An
	// attack that sent 1,000 chains for 600 to the nearest one only. In Frenzy with two kills it chains to three, the
	// nearest first, and never to the one 10 metres away. Leaving Frenzy clears the kills: one again.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3bWearer Wearer(World);
	ACataclysmEnemyCharacter* Struck = WarVowsV3bCreatureAt(World, 2.0f);
	ACataclysmEnemyCharacter* First = WarVowsV3bCreatureAt(World, 3.0f);
	ACataclysmEnemyCharacter* Second = WarVowsV3bCreatureAt(World, 4.5f);
	ACataclysmEnemyCharacter* Third = WarVowsV3bCreatureAt(World, 6.0f);
	ACataclysmEnemyCharacter* Far = WarVowsV3bCreatureAt(World, 12.0f);
	if (!TestTrue(TEXT("set-up: five creatures"), Struck && First && Second && Third && Far))
	{
		return false;
	}
	FCataclysmPoolAction Chain;
	Chain.Event = FName(TEXT("hit_dealt"));
	Chain.Percent = 60.0f;
	Chain.EventValue = 5.0f;
	Chain.bFrenzyChain = true;
	Chain.TriggerKey = FName(TEXT("WarVowsV3b:chain"));
	Wearer.System->SetPoolActions({Chain});

	const TArray<ACataclysmEnemyCharacter*> Others = {Struck, First, Second, Third, Far};
	const auto Attack = [&](float Sent)
	{
		TArray<float> Before;
		for (const ACataclysmEnemyCharacter* Each : Others)
		{
			Before.Add(WarVowsV3bHealthOf(Each));
		}
		World->TimeSeconds += 1.0f;
		const float Was = Wearer.System->SetAttackBeingAnnouncedSent(Sent);
		Wearer.System->ActOnEvent(FName(TEXT("hit_dealt")), nullptr, /*EventAmount=*/Sent, /*bLanded=*/true, Struck);
		Wearer.System->SetAttackBeingAnnouncedSent(Was);
		TArray<float> Lost;
		for (int32 Index = 0; Index < Others.Num(); ++Index)
		{
			Lost.Add(Before[Index] - WarVowsV3bHealthOf(Others[Index]));
		}
		return Lost;
	};

	TArray<float> Lost = Attack(0.0f);
	TestEqual(TEXT("control: an attack that sent nothing chains to nobody"), Lost[1], 0.0f, 0.01f);

	Lost = Attack(1000.0f);
	TestEqual(TEXT("the struck enemy takes nothing from the chain"), Lost[0], 0.0f, 0.01f);
	TestEqual(TEXT("the nearest other takes 600"), Lost[1], 600.0f, 0.5f);
	TestEqual(TEXT("and with no kills nobody else"), Lost[2], 0.0f, 0.01f);

	Wearer.System->SetBerserkerState(ECataclysmBerserkerState::Frenzy);
	Wearer.System->NoteKillInFrenzy();
	Wearer.System->NoteKillInFrenzy();
	TestEqual(TEXT("set-up: two kills in this Frenzy"), Wearer.System->FrenzyKillsHeld(), 2);
	Lost = Attack(1000.0f);
	TestEqual(TEXT("with two kills, the nearest takes 600"), Lost[1], 600.0f, 0.5f);
	TestEqual(TEXT("and the second"), Lost[2], 600.0f, 0.5f);
	TestEqual(TEXT("and the third"), Lost[3], 600.0f, 0.5f);
	TestEqual(TEXT("and the one 10 metres from the struck enemy nothing"), Lost[4], 0.0f, 0.01f);

	Wearer.System->SetBerserkerState(ECataclysmBerserkerState::None);
	TestEqual(TEXT("leaving Frenzy clears the kills"), Wearer.System->FrenzyKillsHeld(), 0);
	Lost = Attack(1000.0f);
	TestEqual(TEXT("and the chain is one enemy again"), Lost[2], 0.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// The options, handed to a War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3B_TEST(FCataclysmWarVowsV3bHandedOverTest,
	"Cataclysm.WarVowsV3b.AWarPlayerTakingTheOptionsIsHandedTheirStatsAndChain")
{
	using namespace CataclysmWarVowsV3bTest;
	// LEVEL 100 WAR PLAYERS THROUGH `SetPassiveAllocation`. A Bulwark holding Colossus Eternal is handed the save at 50
	// and the flag; a Berserker holding Thousand Deaths is handed one chain (60% within 5 metres on a hit dealt, in
	// Frenzy, dual wielding). THE CONTROLS: Wrath Incarnate and Eternal Hunger hand over none of it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3bPlayer Bulwark(World);
	FWarVowsV3bPlayer Berserker(World);
	if (!TestTrue(TEXT("set-up: two possessed players"), Bulwark.IsUsable() && Berserker.IsUsable()))
	{
		return false;
	}

	Bulwark.Take(TEXT("Bulwark_capstone_200"), 3);
	TestEqual(TEXT("control: Wrath Incarnate hands over no save"),
			  Bulwark.Stat(UCataclysmAbilitySystemComponent::LethalHitSurvivedAboveHealthStat), 0.0f, 0.001f);
	TestEqual(TEXT("control: and no doubling"),
			  Bulwark.Stat(UCataclysmAbilitySystemComponent::MaxHealthIncreasesCountTwiceStat), 0.0f, 0.001f);
	Bulwark.Take(TEXT("Bulwark_capstone_200"), 2);
	TestEqual(TEXT("Colossus Eternal hands over the save above 50"),
			  Bulwark.Stat(UCataclysmAbilitySystemComponent::LethalHitSurvivedAboveHealthStat), 50.0f, 0.001f);
	TestEqual(TEXT("and the flag"), Bulwark.Stat(UCataclysmAbilitySystemComponent::MaxHealthIncreasesCountTwiceStat),
			  1.0f, 0.001f);

	Berserker.Take(TEXT("Berserker_capstone_200"), 3);
	TestEqual(TEXT("control: Eternal Hunger hands over no chain"), Berserker.Chains().Num(), 0);
	Berserker.Take(TEXT("Berserker_capstone_200"), 2);
	const TArray<FCataclysmPoolAction> Chains = Berserker.Chains();
	if (TestEqual(TEXT("Thousand Deaths hands over one chain"), Chains.Num(), 1))
	{
		TestEqual(TEXT("of 60%"), Chains[0].Percent, 60.0f, 0.001f);
		TestEqual(TEXT("within 5 metres"), Chains[0].EventValue, 5.0f, 0.001f);
		TestEqual(TEXT("on a hit dealt"), Chains[0].Event, FName(TEXT("hit_dealt")));
		TestTrue(TEXT("in Frenzy"), Chains[0].Condition == ECataclysmStatCondition::InFrenzy);
		TestTrue(TEXT("dual wielding, the second condition"),
				 Chains[0].Condition2 == ECataclysmStatCondition::DualWielding);
	}
	return true;
}

#undef CATACLYSM_WAR_VOWS_V3B_TEST

#endif // WITH_AUTOMATION_TESTS
