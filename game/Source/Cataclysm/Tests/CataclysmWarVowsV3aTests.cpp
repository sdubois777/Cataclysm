// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' THIRD SLICE, V3a, 2026-11-25, on the coordinating session's rulings that day: Thornwall's stack
 * on the attacker, the big hit with Wrath Incarnate's pulse and Absorb and Return's share, and Eternal Hunger's health
 * above the maximum held in the temporary absorb. The three new stats are measured by their probes in
 * `CataclysmStatExemptionTests.cpp`; this file holds the mechanisms in the engine and that a War player is handed them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV3aTest
{
	/** One node to hand over, with the option chosen, or 0 for a node with no options. */
	struct FWarVowsV3aPick
	{
		const TCHAR* Node;
		int32 Points;
		int32 Option;
	};

	/** A possessed level 100 War player, its equipment and its ability system. */
	struct FWarVowsV3aPlayer
	{
		explicit FWarVowsV3aPlayer(UWorld* World)
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

		/** Hand over these nodes, with these points and options, and refresh. */
		void Take(const TArray<FWarVowsV3aPick>& Picks) const
		{
			FCataclysmPassiveAllocation Allocation;
			for (const FWarVowsV3aPick& Each : Picks)
			{
				Allocation.Add(FName(Each.Node), Each.Points);
				if (Each.Option > 0)
				{
					Allocation.SetChosenOption(FName(Each.Node), Each.Option);
				}
			}
			State->SetPassiveAllocation(Allocation, TArray<FName>());
			Equipment->RefreshAttributes(System);
		}

		float Stat(const TCHAR* Name) const
		{
			return System->StatForSkill(FName(Name), FGameplayTagContainer(), 0.0f);
		}

		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** A bare actor with an ability system, 100,000 health, no defences and retaliation at `Retaliation`, standing at
	 *  the origin and acting on the actions it is handed. The shape `CataclysmWarVowsV2bTests.cpp`'s wearer has. */
	struct FWarVowsV3aWearer
	{
		FWarVowsV3aWearer(UWorld* World, float Retaliation)
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
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetRetaliationAttribute(), Retaliation);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		}

		/** Give this wearer these flat stats, and no other. */
		void Grant(const TMap<FName, float>& Stats) const
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TPair<FName, float>& Each : Stats)
			{
				FCataclysmStatModifier Flat;
				Flat.Bucket = ECataclysmStatBucket::Flat;
				Flat.Source = ECataclysmModifierSource::PassiveKeystone;
				Flat.Value = Each.Value;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(Each.Key);
				Line.Base = 0.0f;
				Line.Modifiers = {Flat};
			}
			System->SetStatInputs(MoveTemp(Inputs));
		}

		/** A leech payment of `Amount` to this wearer's health, through the top-up every leech payment goes through. */
		void Leech(float Amount) const
		{
			FGameplayTagContainer Leeching;
			Leeching.AddTag(UCataclysmFervour::LeechTag());
			UCataclysmRegeneration::TopUp(*System, UCataclysmVitalAttributeSet::GetHealthAttribute(),
										  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Amount, Leeching);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature on the monsters' side `Metres` from the origin along X, with no defences and 100,000 health. */
	ACataclysmEnemyCharacter* WarVowsV3aCreatureAt(UWorld* World, float Metres)
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

	float WarVowsV3aHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	float WarVowsV3aRetaliationTakenBy(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
		return System ? System->RetaliationTakenPercentNow() : -1.0f;
	}

	/** The pool actions handed to `System` for which `Which` says yes. */
	template <typename FWhich>
	TArray<FCataclysmPoolAction> WarVowsV3aActions(const UCataclysmAbilitySystemComponent* System, FWhich Which)
	{
		TArray<FCataclysmPoolAction> Found;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			if (Which(Action))
			{
				Found.Add(Action);
			}
		}
		return Found;
	}
}

#define CATACLYSM_WAR_VOWS_V3A_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Thornwall
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3A_TEST(FCataclysmWarVowsV3aThornwallTest,
	"Cataclysm.WarVowsV3a.ThornwallStacksOnTheAttackerOfALandedHitAndRaisesTheRetaliationItTakes")
{
	using namespace CataclysmWarVowsV3aTest;
	// A WEARER WITH RETALIATION AT 20 AND ONE ACTION IN THORNWALL'S SHAPE: 5% a stack, up to 10, for 10 seconds, on
	// `hit_taken`, placed on the attacker. THE CONTROLS: a tick and an evaded blow from the first creature place
	// nothing, and the second creature, which never struck, carries nothing. Twelve landed hits place the cap of ten,
	// 50%. The wearer's payment for a blow of 500 is 100; the first creature takes 150 of it and the second, the
	// control, 100. Eleven seconds after the last hit the stacks have lapsed.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3aWearer Wearer(World, /*Retaliation=*/20.0f);
	ACataclysmEnemyCharacter* Striker = WarVowsV3aCreatureAt(World, 2.0f);
	ACataclysmEnemyCharacter* Bystander = WarVowsV3aCreatureAt(World, 3.0f);
	if (!TestTrue(TEXT("set-up: two creatures"), Striker && Bystander))
	{
		return false;
	}
	FCataclysmPoolAction Thorns;
	Thorns.Event = FName(TEXT("hit_taken"));
	Thorns.Percent = 5.0f;
	Thorns.PlacedKey = FName(TEXT("WarVowsV3a:attacker_retaliation_taken"));
	Thorns.StackSeconds = 10.0f;
	Thorns.StackCap = 10;
	Thorns.bPlacedRaisesRetaliationTaken = true;
	Wearer.System->SetPoolActions({Thorns});

	Wearer.System->NoteHitTaken(/*bLanded=*/true, Striker, /*bDamageOverTime=*/true);
	TestEqual(TEXT("control: a tick places no stack"), WarVowsV3aRetaliationTakenBy(Striker), 0.0f, 0.001f);
	Wearer.System->NoteHitTaken(/*bLanded=*/false, Striker, /*bDamageOverTime=*/false);
	TestEqual(TEXT("control: an evaded blow places none"), WarVowsV3aRetaliationTakenBy(Striker), 0.0f, 0.001f);

	Wearer.System->NoteHitTaken(/*bLanded=*/true, Striker, /*bDamageOverTime=*/false);
	TestEqual(TEXT("one landed hit places one stack, 5%"), WarVowsV3aRetaliationTakenBy(Striker), 5.0f, 0.001f);
	for (int32 Hit = 0; Hit < 11; ++Hit)
	{
		World->TimeSeconds += 0.1f;
		Wearer.System->NoteHitTaken(/*bLanded=*/true, Striker, /*bDamageOverTime=*/false);
	}
	TestEqual(TEXT("twelve landed hits place the cap of ten, 50%"), WarVowsV3aRetaliationTakenBy(Striker), 50.0f,
			  0.001f);
	TestEqual(TEXT("control: the creature that never struck carries none"), WarVowsV3aRetaliationTakenBy(Bystander),
			  0.0f, 0.001f);
	TestEqual(TEXT("the payment's multiplier on the striker is 1.5"), UCataclysmRetaliation::TakenMultiplierOf(Striker),
			  1.5f, 0.0001f);

	TestEqual(TEXT("the striker takes 150 of a payment of 100"),
			  UCataclysmRetaliation::Pay(Wearer.System, Wearer.Actor, Striker, /*DamageTaken=*/500.0f), 150.0f, 0.5f);
	TestEqual(TEXT("control: the bystander takes the 100"),
			  UCataclysmRetaliation::Pay(Wearer.System, Wearer.Actor, Bystander, /*DamageTaken=*/500.0f), 100.0f,
			  0.5f);

	World->TimeSeconds += 11.0f;
	TestEqual(TEXT("eleven seconds after the last hit the stacks have lapsed"), WarVowsV3aRetaliationTakenBy(Striker),
			  0.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The big hit and Wrath Incarnate's pulse
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3A_TEST(FCataclysmWarVowsV3aBigHitPulseTest,
	"Cataclysm.WarVowsV3a.ABigHitPaysWrathIncarnatesPulseToEveryEnemyWithinItsRadius")
{
	using namespace CataclysmWarVowsV3aTest;
	// A WEARER WITH 100,000 HEALTH, NO DEFENCES, RETALIATION AT 10 AND ONE ACTION IN WRATH INCARNATE'S SHAPE: 200% of
	// the retaliation the big hit earns, within 4 metres, on `big_hit_taken`. A creature 2 metres away strikes it with
	// real blows (`ApplyDirectDamage`), with a second creature 3 metres away and a third 7 metres away.
	// THE CONTROL: a blow of 5,000 is 5% of maximum health, not big: the striker takes the ordinary 500 and nobody else
	// anything. A blow of 15,000 is 15%: the striker takes the ordinary 1,500 and the pulse's 3,000, the creature 3
	// metres away the pulse's 3,000, and the one 7 metres away nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3aWearer Wearer(World, /*Retaliation=*/10.0f);
	ACataclysmEnemyCharacter* Striker = WarVowsV3aCreatureAt(World, 2.0f);
	ACataclysmEnemyCharacter* Beside = WarVowsV3aCreatureAt(World, 3.0f);
	ACataclysmEnemyCharacter* Far = WarVowsV3aCreatureAt(World, 7.0f);
	if (!TestTrue(TEXT("set-up: three creatures"), Striker && Beside && Far))
	{
		return false;
	}
	FCataclysmPoolAction Pulse;
	Pulse.Event = FName(TEXT("big_hit_taken"));
	Pulse.Percent = 200.0f;
	Pulse.EventValue = 4.0f;
	Pulse.bRetaliationPulse = true;
	Pulse.TriggerKey = FName(TEXT("WarVowsV3a:pulse"));
	Wearer.System->SetPoolActions({Pulse});

	const auto Losses = [&](float Blow, float& StrikerLost, float& BesideLost, float& FarLost)
	{
		const float StrikerBefore = WarVowsV3aHealthOf(Striker);
		const float BesideBefore = WarVowsV3aHealthOf(Beside);
		const float FarBefore = WarVowsV3aHealthOf(Far);
		Wearer.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
		World->TimeSeconds += 1.0f;
		UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, Blow);
		StrikerLost = StrikerBefore - WarVowsV3aHealthOf(Striker);
		BesideLost = BesideBefore - WarVowsV3aHealthOf(Beside);
		FarLost = FarBefore - WarVowsV3aHealthOf(Far);
	};

	float StrikerLost = 0.0f;
	float BesideLost = 0.0f;
	float FarLost = 0.0f;
	Losses(5000.0f, StrikerLost, BesideLost, FarLost);
	TestEqual(TEXT("set-up: the wearer took the whole blow of 5,000"),
			  100000.0f - WarVowsV3aHealthOf(Wearer.Actor), 5000.0f, 0.5f);
	TestEqual(TEXT("control: a blow of 5% is not big, and the striker takes the ordinary 500"), StrikerLost, 500.0f,
			  0.5f);
	TestEqual(TEXT("control: and the creature 3 metres away nothing"), BesideLost, 0.0f, 0.01f);

	Losses(15000.0f, StrikerLost, BesideLost, FarLost);
	TestEqual(TEXT("a blow of 15% is big: the striker takes the ordinary 1,500 and the pulse's 3,000"), StrikerLost,
			  4500.0f, 0.5f);
	TestEqual(TEXT("and the creature 3 metres away the pulse's 3,000"), BesideLost, 3000.0f, 0.5f);
	TestEqual(TEXT("and the one 7 metres away, outside the 4, nothing"), FarLost, 0.0f, 0.01f);
	TestFalse(TEXT("and once the rows have acted, retaliation is no longer paid as for a big hit"),
			  Wearer.System->IsPayingForABigHit());
	return true;
}

// ---------------------------------------------------------------------------
// Eternal Hunger
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3A_TEST(FCataclysmWarVowsV3aEternalHungerTest,
	"Cataclysm.WarVowsV3a.EternalHungersPartOfTheAbsorbIsCutByDamageAndDecaysAloneWhileNotLeeching")
{
	using namespace CataclysmWarVowsV3aTest;
	// A WEARER AT FULL HEALTH, 100,000, HOLDING ETERNAL HUNGER'S TWO STATS: a cap of 100% and a decay of 1% a second,
	// 1,000 a second. A leech payment of 3,000 at full health puts all 3,000 in the absorb as its part. Another
	// source's shield of 5,000 raises the whole to 5,000 and leaves the part at 3,000. THE CONTROL: within a second of
	// the leech nothing decays. After it, five seconds take the part's 3,000 and no more: the whole stops at 2,000.
	// THEN THE CUT: a fresh part of 3,000, and damage that takes 1,000 of the absorb leaves part and whole at 2,000.
	// THE SECOND CONTROL: a heal that is not leech overflows into nothing this option counts.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3aWearer Wearer(World, /*Retaliation=*/0.0f);
	Wearer.Grant({{FName(UCataclysmAbilitySystemComponent::LeechOverhealAbsorbCapStat), 100.0f},
				  {FName(UCataclysmAbilitySystemComponent::LeechOverhealAbsorbDecayStat), 1.0f}});

	UCataclysmRegeneration::TopUp(*Wearer.System, UCataclysmVitalAttributeSet::GetHealthAttribute(),
								  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 3000.0f);
	TestEqual(TEXT("control: a heal that is not leech puts nothing in Eternal Hunger's part"),
			  Wearer.System->LeechAbsorbHeld(), 0.0f, 0.01f);

	Wearer.Leech(3000.0f);
	TestEqual(TEXT("a leech payment of 3,000 at full health is all held in the part"), Wearer.System->LeechAbsorbHeld(),
			  3000.0f, 0.5f);
	TestEqual(TEXT("and in the whole absorb"), Wearer.System->TemporaryAbsorbHeld(), 3000.0f, 0.5f);

	Wearer.System->GrantTemporaryAbsorb(5000.0f);
	TestEqual(TEXT("set-up: another source raises the whole to 5,000"), Wearer.System->TemporaryAbsorbHeld(), 5000.0f,
			  0.5f);

	World->TimeSeconds += 0.5f;
	Wearer.System->DecayLeechAbsorb(0.5f);
	TestEqual(TEXT("control: within a second of the leech nothing decays"), Wearer.System->LeechAbsorbHeld(), 3000.0f,
			  0.5f);

	World->TimeSeconds += 1.0f;
	Wearer.System->DecayLeechAbsorb(2.0f);
	TestEqual(TEXT("two seconds take 2,000 of the part"), Wearer.System->LeechAbsorbHeld(), 1000.0f, 0.5f);
	TestEqual(TEXT("and of the whole"), Wearer.System->TemporaryAbsorbHeld(), 3000.0f, 0.5f);
	Wearer.System->DecayLeechAbsorb(3.0f);
	TestEqual(TEXT("three more take the part's last 1,000 and no more"), Wearer.System->LeechAbsorbHeld(), 0.0f, 0.01f);
	TestEqual(TEXT("so the other source keeps its 2,000"), Wearer.System->TemporaryAbsorbHeld(), 2000.0f, 0.5f);

	Wearer.System->SpendTemporaryAbsorb(2000.0f);
	Wearer.Leech(3000.0f);
	Wearer.System->SpendTemporaryAbsorb(1000.0f);
	TestEqual(TEXT("damage taking 1,000 of a part of 3,000 leaves the whole at 2,000"),
			  Wearer.System->TemporaryAbsorbHeld(), 2000.0f, 0.5f);
	TestEqual(TEXT("and the part cut to it"), Wearer.System->LeechAbsorbHeld(), 2000.0f, 0.5f);
	return true;
}

// ---------------------------------------------------------------------------
// The options, handed to a War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V3A_TEST(FCataclysmWarVowsV3aHandedOverTest,
	"Cataclysm.WarVowsV3a.AWarPlayerTakingTheOptionsIsHandedTheirActionsAndStats")
{
	using namespace CataclysmWarVowsV3aTest;
	// LEVEL 100 WAR PLAYERS THROUGH `SetPassiveAllocation`, the way the game hands a build over. A Bulwark holding
	// Thornwall, Wrath Incarnate and 2 points of Absorb and Return is handed Thornwall's placed stack (5%, 10 seconds,
	// 10 stacks, on a hit taken), the pulse (200% within 4 metres on a big hit) and 6 of `retaliation_for_big_hits`. A
	// Berserker holding Eternal Hunger is handed the cap of 100 and the decay of 1. THE CONTROLS: the first options of
	// the same capstones, and no Absorb and Return, hand over none of it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV3aPlayer Bulwark(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Bulwark.IsUsable()))
	{
		return false;
	}
	const auto Thorns = [](const FCataclysmPoolAction& Action) { return Action.bPlacedRaisesRetaliationTaken; };
	const auto Pulses = [](const FCataclysmPoolAction& Action) { return Action.bRetaliationPulse; };

	Bulwark.Take({{TEXT("Bulwark_capstone_50"), 1, 1}, {TEXT("Bulwark_capstone_200"), 1, 1}});
	TestEqual(TEXT("control: the first options hand over no Thornwall stack"),
			  WarVowsV3aActions(Bulwark.System, Thorns).Num(), 0);
	TestEqual(TEXT("control: and no pulse"), WarVowsV3aActions(Bulwark.System, Pulses).Num(), 0);
	TestEqual(TEXT("control: and no share for big hits"), Bulwark.Stat(UCataclysmRetaliation::BigHitShareStat), 0.0f,
			  0.001f);

	Bulwark.Take({{TEXT("Bulwark_capstone_50"), 1, 3}, {TEXT("Bulwark_capstone_200"), 1, 3},
				  {TEXT("Bulwark_basic_tw_t2_001"), 2, 0}});
	const TArray<FCataclysmPoolAction> Thornwall = WarVowsV3aActions(Bulwark.System, Thorns);
	if (TestEqual(TEXT("Thornwall hands over one placed stack"), Thornwall.Num(), 1))
	{
		TestEqual(TEXT("of 5%"), Thornwall[0].Percent, 5.0f, 0.001f);
		TestEqual(TEXT("for 10 seconds"), Thornwall[0].StackSeconds, 10.0f, 0.001f);
		TestEqual(TEXT("up to 10 stacks"), Thornwall[0].StackCap, 10);
		TestEqual(TEXT("on a hit taken"), Thornwall[0].Event, FName(TEXT("hit_taken")));
		TestFalse(TEXT("under its own key"), Thornwall[0].PlacedKey.IsNone());
	}
	const TArray<FCataclysmPoolAction> Wrath = WarVowsV3aActions(Bulwark.System, Pulses);
	if (TestEqual(TEXT("Wrath Incarnate hands over one pulse"), Wrath.Num(), 1))
	{
		TestEqual(TEXT("of 200%"), Wrath[0].Percent, 200.0f, 0.001f);
		TestEqual(TEXT("within 4 metres"), Wrath[0].EventValue, 4.0f, 0.001f);
		TestEqual(TEXT("on a big hit"), Wrath[0].Event, FName(TEXT("big_hit_taken")));
	}
	TestEqual(TEXT("and 2 points of Absorb and Return hand over 6 for big hits"),
			  Bulwark.Stat(UCataclysmRetaliation::BigHitShareStat), 6.0f, 0.001f);

	FWarVowsV3aPlayer Berserker(World);
	if (!TestTrue(TEXT("set-up: a second player"), Berserker.IsUsable()))
	{
		return false;
	}
	Berserker.Take({{TEXT("Berserker_capstone_200"), 1, 1}});
	TestEqual(TEXT("control: Ragnarok hands over no leech absorb cap"),
			  Berserker.Stat(UCataclysmAbilitySystemComponent::LeechOverhealAbsorbCapStat), 0.0f, 0.001f);
	Berserker.Take({{TEXT("Berserker_capstone_200"), 1, 3}});
	TestEqual(TEXT("Eternal Hunger hands over a cap of 100"),
			  Berserker.Stat(UCataclysmAbilitySystemComponent::LeechOverhealAbsorbCapStat), 100.0f, 0.001f);
	TestEqual(TEXT("and a decay of 1"), Berserker.Stat(UCataclysmAbilitySystemComponent::LeechOverhealAbsorbDecayStat),
			  1.0f, 0.001f);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V3A_TEST

#endif // WITH_AUTOMATION_TESTS
