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
#include "Interface/CataclysmCombatOverlay.h"
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
 * TITANFALL, the Bulwark's keystone, 2026-11-25, on the coordinating session's rulings T1-T6 and the sentence R6:
 * "When a single hit takes more than 20% of your Maximum HP, you cannot be killed for 2 seconds and your next attack
 * deals bonus damage equal to that hit. Can only happen once every 30 seconds."
 *
 * A bare wearer with 100,000 health and no defences, struck by real blows (`ApplyDirectDamage`), as
 * `CataclysmWarVowsV3aTests.cpp` strikes Wrath Incarnate's wearer. Helper types and functions are named after the
 * subject, so no unity build meets two of one name.
 */
namespace CataclysmWarTitanfallTest
{
	struct FTitanfallWearer
	{
		explicit FTitanfallWearer(UWorld* World)
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
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

			// TITANFALL'S ROW, as the generator writes it: 2 seconds, above 20% of maximum health, 30 seconds between.
			FCataclysmPoolAction Titanfall;
			Titanfall.Event = FName(TEXT("big_hit_taken"));
			Titanfall.Percent = 2.0f;
			Titanfall.EventValue = 20.0f;
			Titanfall.TriggerCooldownSeconds = 30.0f;
			Titanfall.bTitanfall = true;
			Titanfall.TriggerKey = FName(TEXT("WarTitanfall:row"));
			System->SetPoolActions({Titanfall});
		}

		float Health() const
		{
			return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		void Heals() const
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	ACataclysmEnemyCharacter* TitanfallCreatureAt(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000000.0f);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}

	float TitanfallHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

#define CATACLYSM_WAR_TITANFALL_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WAR_TITANFALL_TEST(FCataclysmWarTitanfallFiresTest,
	"Cataclysm.WarTitanfall.AHitAboveAFifthOfMaximumHealthOpensTheWindowAndStoresTheHit")
{
	using namespace CataclysmWarTitanfallTest;
	// A BLOW OF 15,000 IS 15%, big but not above 20%: nothing (THE CONTROL). A blow of 25,000 is 25%: the window opens for
	// 2 seconds and 25,000 is stored, and the buffs show both. In the window a blow of 200,000 leaves 1 health. 3 seconds
	// on the window is shut and the store waits. 4 seconds after firing, its 30 not passed, a blow of 200,000 kills
	// (THE CONTROL for the clock).
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTitanfallWearer Wearer(World);
	ACataclysmEnemyCharacter* Striker = TitanfallCreatureAt(World, 2.0f);
	if (!TestNotNull(TEXT("set-up: a creature"), Striker))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 15000.0f);
	TestEqual(TEXT("set-up: the wearer took the whole 15,000"), 100000.0f - Wearer.Health(), 15000.0f, 0.5f);
	TestFalse(TEXT("control: a hit of 15% opens no window"), Wearer.System->IsTitanfallUnkillable());
	TestEqual(TEXT("control: and stores nothing"), Wearer.System->TitanfallStoredDamage(), 0.0f, 0.01f);
	TestTrue(TEXT("control: and the buffs say nothing of it"), UCataclysmCombatOverlay::TitanfallTextFor(Wearer.Actor).IsEmpty());

	Wearer.Heals();
	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 25000.0f);
	TestTrue(TEXT("a hit of 25% opens the window"), Wearer.System->IsTitanfallUnkillable());
	TestEqual(TEXT("for 2 seconds"), Wearer.System->TitanfallUnkillableSecondsLeft(), 2.0f, 0.01f);
	TestEqual(TEXT("and stores the 25,000"), Wearer.System->TitanfallStoredDamage(), 25000.0f, 0.5f);
	TestEqual(TEXT("and the buffs show both"), UCataclysmCombatOverlay::TitanfallTextFor(Wearer.Actor),
			  FString(TEXT("Titanfall: cannot die 2s, next attack +25000")));

	Wearer.Heals();
	World->TimeSeconds += 1.0f;
	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 200000.0f);
	TestEqual(TEXT("in the window a blow of 200,000 leaves 1 health"), Wearer.Health(), 1.0f, 0.01f);
	TestEqual(TEXT("and the store is not replaced: the clock has not passed"), Wearer.System->TitanfallStoredDamage(),
			  25000.0f, 0.5f);

	World->TimeSeconds += 2.0f;
	TestFalse(TEXT("3 seconds on, the window is shut"), Wearer.System->IsTitanfallUnkillable());
	TestEqual(TEXT("and the buffs show the store alone"), UCataclysmCombatOverlay::TitanfallTextFor(Wearer.Actor),
			  FString(TEXT("Titanfall: next attack +25000")));

	Wearer.Heals();
	World->TimeSeconds += 1.0f;
	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 200000.0f);
	// HEALTH AT NOUGHT, NOT A DEATH (window war2-w1): this wearer is a bare actor, and only a character's death marks
	// one dead.
	TestEqual(TEXT("control: 4 seconds after firing, its 30 not passed, a blow of 200,000 takes every point of health"),
			  Wearer.Health(), 0.0f, 0.01f);
	return true;
}

CATACLYSM_WAR_TITANFALL_TEST(FCataclysmWarTitanfallLethalTest,
	"Cataclysm.WarTitanfall.AHitThatWouldKillFiresItAndIsSurvived")
{
	using namespace CataclysmWarTitanfallTest;
	// A SINGLE BLOW OF 200,000 ON 100,000 HEALTH, WITH TITANFALL READY (ruled T2): it leaves 1 health, opens the window
	// and stores the whole blow, its overkill included. THE CONTROL: a wearer with no Titanfall row dies of the same blow.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTitanfallWearer Wearer(World);
	const FTitanfallWearer Plain(World);
	ACataclysmEnemyCharacter* Striker = TitanfallCreatureAt(World, 2.0f);
	if (!TestNotNull(TEXT("set-up: a creature"), Striker))
	{
		return false;
	}
	Plain.System->SetPoolActions(TArray<FCataclysmPoolAction>());
	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Plain.Actor, 200000.0f);
	TestEqual(TEXT("control: with no Titanfall row the blow takes every point of health"), TitanfallHealthOf(Plain.Actor),
			  0.0f, 0.01f);

	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 200000.0f);
	TestEqual(TEXT("with it, the blow leaves 1 health"), Wearer.Health(), 1.0f, 0.01f);
	TestTrue(TEXT("and the window is open"), Wearer.System->IsTitanfallUnkillable());
	TestEqual(TEXT("and the whole blow is stored, its overkill included"), Wearer.System->TitanfallStoredDamage(),
			  200000.0f, 1.0f);
	return true;
}

CATACLYSM_WAR_TITANFALL_TEST(FCataclysmWarTitanfallStoreTest,
	"Cataclysm.WarTitanfall.TheNextAttackDealsTheStoredHitWholeAndOnlyOnce")
{
	using namespace CataclysmWarTitanfallTest;
	// A HIT OF 25,000 STORES 25,000. The wearer's next attack (`ApplyHit`, 100% of an attack damage of 100) deals the
	// stored 25,000 more than the attack after it, which deals its own figure alone (THE CONTROL), and the store is
	// empty after the first.
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FTitanfallWearer Wearer(World);
	ACataclysmEnemyCharacter* Striker = TitanfallCreatureAt(World, 2.0f);
	ACataclysmEnemyCharacter* Target = TitanfallCreatureAt(World, 30.0f);
	if (!TestTrue(TEXT("set-up: two creatures"), Striker && Target))
	{
		return false;
	}
	UCataclysmSkillEffects::ApplyDirectDamage(Striker, Wearer.Actor, 25000.0f);
	if (!TestEqual(TEXT("set-up: 25,000 stored"), Wearer.System->TitanfallStoredDamage(), 25000.0f, 0.5f))
	{
		return false;
	}
	const float Before = TitanfallHealthOf(Target);
	UCataclysmSkillEffects::ApplyHit(Wearer.Actor, Target, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery());
	const float First = Before - TitanfallHealthOf(Target);
	TestEqual(TEXT("the store is spent by the first attack"), Wearer.System->TitanfallStoredDamage(), 0.0f, 0.01f);
	const float Middle = TitanfallHealthOf(Target);
	UCataclysmSkillEffects::ApplyHit(Wearer.Actor, Target, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery());
	const float Second = Middle - TitanfallHealthOf(Target);
	TestTrue(*FString::Printf(TEXT("control: the second attack deals its own figure: %.1f"), Second), Second > 0.0f);
	TestEqual(TEXT("the first deals the 25,000 more, whole"), First - Second, 25000.0f, 1.0f);
	return true;
}

CATACLYSM_WAR_TITANFALL_TEST(FCataclysmWarTitanfallHandedOverTest,
	"Cataclysm.WarTitanfall.ABulwarkTakingTitanfallIsHandedItsAction")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`: Titanfall hands over one action on `big_hit_taken`, 2 s above
	// 20%, waiting 30 s. THE CONTROL: before it is taken, none.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
	State->SetLevelAndExperience(100, 0);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestTrue(TEXT("set-up: its ability system and equipment"), System && Equipment))
	{
		return false;
	}
	const auto Held = [System]()
	{
		TArray<FCataclysmPoolAction> Found;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			if (Action.bTitanfall)
			{
				Found.Add(Action);
			}
		}
		return Found;
	};
	TestEqual(TEXT("control: before Titanfall is taken, no such action"), Held().Num(), 0);
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Bulwark_keystone_jg_final")), 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(System);
	const TArray<FCataclysmPoolAction> Found = Held();
	if (TestEqual(TEXT("taken, one action"), Found.Num(), 1))
	{
		TestEqual(TEXT("on a big hit"), Found[0].Event, FName(TEXT("big_hit_taken")));
		TestEqual(TEXT("2 seconds"), Found[0].Percent, 2.0f, 0.001f);
		TestEqual(TEXT("above 20%"), Found[0].EventValue, 20.0f, 0.001f);
		TestEqual(TEXT("30 seconds between"), Found[0].TriggerCooldownSeconds, 30.0f, 0.001f);
	}
	return true;
}

#undef CATACLYSM_WAR_TITANFALL_TEST

#endif // WITH_AUTOMATION_TESTS
