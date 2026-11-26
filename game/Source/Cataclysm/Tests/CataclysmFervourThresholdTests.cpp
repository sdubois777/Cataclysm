// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"

/**
 * The Soul Collector's four Fervour thresholds (the Death trees' slice C3d, 2026-11-25): the condition
 * `fervour_threshold_reached` and the scale `fervour_thresholds_reached`, both read from one answer the conditions fill
 * works out, `UCataclysmAbilitySystemComponent::FervourThresholdsReachedFor`; Generous Count's reduction, Long Shadow's
 * seconds after a fall, Steady Count's count; and the fourth vow's faster cooldown recovery, a rate the regeneration
 * step applies, as the coordinating session ruled.
 */

namespace CataclysmFervourThresholdTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** A bare actor with health and a Fervour pool of 100. Not a player, so it has no points spent of its own. */
	struct FThresholdHolder
	{
		explicit FThresholdHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
		}

		~FThresholdHolder()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Hold(float Fervour) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   Fervour);
		}

		/** One stat given flat by hand, as its row will give it. Replaces every stat given before. */
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
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** The bits for these thresholds, by figure. */
	int32 Bits(std::initializer_list<int32> Figures)
	{
		int32 Out = 0;
		for (const int32 Figure : Figures)
		{
			Out |= 1 << CataclysmFervourThresholds::IndexOf(static_cast<float>(Figure));
		}
		return Out;
	}

	/** The longest time left on a slot's own cooldown, or nought. */
	float SecondsLeftOnKey(const UAbilitySystemComponent* System, ECataclysmAbilitySlot Slot)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
		float Most = 0.0f;
		for (const float Left : System->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Most = FMath::Max(Most, Left);
		}
		return Most;
	}

	/**
	 * A real level 30 Death character with The Final Vow's point and 200 passive points spent in all, the rest on a
	 * name no node table holds, which counts toward the points and grants nothing; and no starting node, so its Fervour
	 * does not decay. Its regeneration step runs on the test clock. Null when it could not be built.
	 */
	UCataclysmAbilitySystemComponent* FinalVowHolder(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* SpawnedState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!SpawnedState || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(SpawnedState);
		Controller->Possess(Character);
		ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
		UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
		UCataclysmAbilitySystemComponent* System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (!State || !Equipment || !System)
		{
			return nullptr;
		}
		State->SetCreationChoice(FName(TEXT("Dagger")), FName(TEXT("Death")));
		// THE STARTING WEAPON TAKEN OFF: `ACataclysmPlayerCharacter::GiveStartingWeapon` made it at spawn with the default
		// damage type, before Death was chosen, so it carried the character to the Demonic trees and no Death tree was
		// reachable. Window death3-w2 found it. With nothing worn the creation type is what the character carries.
		Equipment->UnequipEverything();
		State->SetLevelAndExperience(30, 0);
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TEXT("SoulCollector_capstone_200")), 1);
		Allocation.Add(FName(TEXT("Test_PointsSpentAndNothingElse")), 199);
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		return System;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourThresholdOpenTest,
	"Cataclysm.FervourThreshold.AThresholdOpensByPointsSpentAndIsReachedByFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The four thresholds open at 25, 50, 100 and 200 points spent and are reached at 20, 40, 60 and 100 Fervour held. THE
 * CONTROLS: 45 held with 30 points spent reaches only the first, whose figure it holds; 19 held reaches none; an unknown
 * holding or points spent reaches none and says so.
 */
bool FCataclysmFervourThresholdOpenTest::RunTest(const FString&)
{
	using namespace CataclysmFervourThresholdTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FThresholdHolder Holder(World);
	const UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;

	TestEqual(TEXT("45 held, 60 points: the first two"), System->FervourThresholdsReachedFor(45.0f, 60), Bits({20, 40}));
	TestEqual(TEXT("control: 45 held, 30 points: only the first is open"), System->FervourThresholdsReachedFor(45.0f, 30),
			  Bits({20}));
	TestEqual(TEXT("100 held, 200 points: all four"), System->FervourThresholdsReachedFor(100.0f, 200),
			  Bits({20, 40, 60, 100}));
	TestEqual(TEXT("control: 19 held reaches none"), System->FervourThresholdsReachedFor(19.0f, 200), 0);
	TestEqual(TEXT("control: no holding known"), System->FervourThresholdsReachedFor(-1.0f, 200), -1);
	TestEqual(TEXT("control: no points known"), System->FervourThresholdsReachedFor(45.0f, -1), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourThresholdGenerousTest,
	"Cataclysm.FervourThreshold.GenerousCountReachesEachThresholdSooner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Generous Count at six points: every threshold 6 sooner, so 34 held reaches the second, at 40 less 6. THE CONTROL: the
 * same holding without the node reaches only the first.
 */
bool FCataclysmFervourThresholdGenerousTest::RunTest(const FString&)
{
	using namespace CataclysmFervourThresholdTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FThresholdHolder Holder(World);
	TestEqual(TEXT("control: 34 held without Generous Count reaches only the first"),
			  Holder.AbilitySystem->FervourThresholdsReachedFor(34.0f, 200), Bits({20}));
	Holder.Grant(UCataclysmAbilitySystemComponent::FervourThresholdReductionStat, 6.0f);
	TestEqual(TEXT("with it at 6, 34 held reaches the second too"),
			  Holder.AbilitySystem->FervourThresholdsReachedFor(34.0f, 200), Bits({20, 40}));
	TestEqual(TEXT("and 33 does not"), Holder.AbilitySystem->FervourThresholdsReachedFor(33.0f, 200), Bits({20}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourThresholdLongShadowTest,
	"Cataclysm.FervourThreshold.LongShadowKeepsAThresholdFiveSecondsAfterAFall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Long Shadow at 5 seconds: Fervour held falls from 45 to 30, and the second threshold stays reached for 5 seconds,
 * recorded by the class resource set just before the fall. THE CONTROLS: a holder without the keystone loses it at the
 * fall, and 5.1 seconds after the fall the keystone's holder has lost it too.
 */
bool FCataclysmFervourThresholdLongShadowTest::RunTest(const FString&)
{
	using namespace CataclysmFervourThresholdTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FThresholdHolder Plain(World);
	const FThresholdHolder Shadowed(World);
	Shadowed.Grant(UCataclysmAbilitySystemComponent::FervourThresholdLingerSecondsStat, 5.0f);

	Plain.Hold(45.0f);
	Plain.Hold(30.0f);
	TestEqual(TEXT("control: without Long Shadow the fall from 45 to 30 loses the second at once"),
			  Plain.AbilitySystem->FervourThresholdsReachedFor(30.0f, 60), Bits({20}));

	Shadowed.Hold(45.0f);
	Shadowed.Hold(30.0f);
	TestEqual(TEXT("with it, the second is still reached just after the fall"),
			  Shadowed.AbilitySystem->FervourThresholdsReachedFor(30.0f, 60), Bits({20, 40}));
	RunClock(World, 4.9f);
	TestEqual(TEXT("and 4.9 seconds after"), Shadowed.AbilitySystem->FervourThresholdsReachedFor(30.0f, 60),
			  Bits({20, 40}));
	RunClock(World, 0.2f);
	TestEqual(TEXT("control: and not 5.1 seconds after"), Shadowed.AbilitySystem->FervourThresholdsReachedFor(30.0f, 60),
			  Bits({20}));

	Shadowed.Hold(45.0f);
	Shadowed.AbilitySystem->NoteFervourThresholdsHeld(45.0f);
	Shadowed.AbilitySystem->ClearWhatDeathEnds();
	TestEqual(TEXT("a death forgets what was held before it"),
			  Shadowed.AbilitySystem->FervourThresholdsReachedFor(30.0f, 60), Bits({20}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourThresholdPipelineTest,
	"Cataclysm.FervourThreshold.TheConditionAndSteadyCountReadOneAnswer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The pipeline reads `FervourThresholdsReached`: a row under the threshold at 40 holds when its bit is set and not
 * otherwise, and a row under 50, which is none of the four, never holds; Steady Count at six points, 6% increased damage
 * for each threshold reached, is 18% with three reached. THE CONTROLS: nothing reached, and the answer unknown (-1),
 * give neither.
 */
bool FCataclysmFervourThresholdPipelineTest::RunTest(const FString&)
{
	using namespace CataclysmFervourThresholdTest;

	FCataclysmStatModifier AtForty;
	AtForty.Bucket = ECataclysmStatBucket::Increased;
	AtForty.Source = ECataclysmModifierSource::PassiveKeystone;
	AtForty.Value = 50.0f;
	AtForty.Condition = ECataclysmStatCondition::FervourThresholdReached;
	AtForty.ConditionValue = 40.0f;
	FCataclysmStatModifier AtFifty = AtForty;
	AtFifty.ConditionValue = 50.0f;
	FCataclysmStatModifier Steady;
	Steady.Bucket = ECataclysmStatBucket::Increased;
	Steady.Source = ECataclysmModifierSource::PassiveKeystone;
	Steady.Value = 6.0f;
	Steady.Scale = ECataclysmStatScale::PerFervourThresholdReached;
	Steady.ScaleStep = 1.0f;

	const auto Final = [](const FCataclysmStatModifier& Row, int32 Reached)
	{
		FCataclysmStatConditions State;
		State.FervourThresholdsReached = Reached;
		return UCataclysmStatPipeline::Evaluate(100.0f, {Row}, FGameplayTagContainer(), State).Final;
	};
	TestEqual(TEXT("under the threshold at 40, with it reached: 150"), Final(AtForty, Bits({20, 40})), 150.0f, 0.01f);
	TestEqual(TEXT("control: with only the first reached: 100"), Final(AtForty, Bits({20})), 100.0f, 0.01f);
	TestEqual(TEXT("under 50, none of the four, with every one reached: 100"), Final(AtFifty, 15), 100.0f, 0.01f);
	TestEqual(TEXT("Steady Count with three reached: 118"), Final(Steady, Bits({20, 40, 60})), 118.0f, 0.01f);
	TestEqual(TEXT("control: with none reached: 100"), Final(Steady, 0), 100.0f, 0.01f);
	TestEqual(TEXT("control: with the answer unknown: 100"), Final(Steady, -1), 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourThresholdRecoveryTest,
	"Cataclysm.FervourThreshold.TheFinalVowSpeedsARunningCooldownAboveOneHundredAndNotBelow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The fourth vow's clause through its real row, as the coordinating session ruled it: a rate. Two real Death characters
 * with The Final Vow's point and 200 points spent each start a 20 second cooldown holding 50 Fervour, and their own
 * regeneration steps run on the test clock. One reaches 100 Fervour after a second, falls back to 50 after two more, and
 * reaches 100 again: its cooldown runs twice as fast only while it holds 100, and has ended 11.2 seconds after it began.
 * THE CONTROL: the other stays at 50 and has about 8.8 seconds left then.
 *
 * NO STEP IS CALLED BY HAND. The precedent for a character's own regeneration timer running on `RunClock` is
 * `Cataclysm.MasochistVowRows.CeaselessPenanceKeepsARealMasochistsDebuffsWhileAboveHalfHealth`, which passed in two
 * windows on 2026-11-25; see the slice C3d entry in docs/DECISIONS.md.
 */
bool FCataclysmFervourThresholdRecoveryTest::RunTest(const FString&)
{
	using namespace CataclysmFervourThresholdTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* Rising = FinalVowHolder(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Steady = FinalVowHolder(World, FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Death character with The Final Vow"), Rising)
		|| !TestNotNull(TEXT("set-up: and a second"), Steady))
	{
		return false;
	}
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const FGameplayTag Heavy = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);
	for (UCataclysmAbilitySystemComponent* Each : {Rising, Steady})
	{
		Each->SetNumericAttributeBase(Pool, 50.0f);
		UCataclysmGameplayAbility::ApplyCooldownEffect(Each, Heavy, 20.0f);
	}

	RunClock(World, 1.0f);
	TestEqual(TEXT("holding 50, a second takes a second off"),
			  SecondsLeftOnKey(Rising, ECataclysmAbilitySlot::Heavy), 19.0f, 0.3f);
	Rising->SetNumericAttributeBase(Pool, 100.0f);
	RunClock(World, 2.0f);
	TestEqual(TEXT("holding 100, two seconds take four off"),
			  SecondsLeftOnKey(Rising, ECataclysmAbilitySlot::Heavy), 15.0f, 0.3f);
	Rising->SetNumericAttributeBase(Pool, 50.0f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("back at 50, a second takes a second off again"),
			  SecondsLeftOnKey(Rising, ECataclysmAbilitySlot::Heavy), 14.0f, 0.3f);
	Rising->SetNumericAttributeBase(Pool, 100.0f);
	RunClock(World, 7.2f);
	TestEqual(TEXT("at 100 again, the cooldown has ended 11.2 seconds after it began"),
			  SecondsLeftOnKey(Rising, ECataclysmAbilitySlot::Heavy), 0.0f, 0.001f);
	TestEqual(TEXT("control: the one that stayed at 50 has about 8.8 left"),
			  SecondsLeftOnKey(Steady, ECataclysmAbilitySlot::Heavy), 8.8f, 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourThresholdRealRowsTest,
	"Cataclysm.FervourThreshold.TheFourVowClausesAreRowsWithNoOptionOnTheirCapstones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The four vows' clauses are their capstones' rows with no option, each under its threshold's figure: 20% more Area of
 * Effect at 20, an extra projectile at 40, mana cost removed at 60, cooldowns recovering 100% faster at 100. THE
 * CONTROL: each capstone's option rows are still options.
 */
bool FCataclysmFervourThresholdRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FClause
	{
		const TCHAR* Node;
		const TCHAR* Stat;
		float Figure;
	};
	const FClause Clauses[] = {
		{TEXT("SoulCollector_capstone_25"), TEXT("area_of_effect"), 20.0f},
		{TEXT("SoulCollector_capstone_50"), TEXT("projectile_count"), 40.0f},
		{TEXT("SoulCollector_capstone_100"), TEXT("mana_cost"), 60.0f},
		{TEXT("SoulCollector_capstone_200"), UCataclysmAbilitySystemComponent::CooldownsRecoverFasterPercentStat, 100.0f},
	};
	for (const FClause& Clause : Clauses)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Clause.Node)))
		{
			if (Row->Option != 0)
			{
				continue;
			}
			++Found;
			TestEqual(*FString::Printf(TEXT("%s: the clause's stat"), Clause.Node), Row->Stat, FString(Clause.Stat));
			TestEqual(*FString::Printf(TEXT("%s: under a Fervour threshold"), Clause.Node), Row->Condition,
					  FString(TEXT("fervour_threshold_reached")));
			TestEqual(*FString::Printf(TEXT("%s: its figure"), Clause.Node), Row->ConditionValue, Clause.Figure, 0.001f);
		}
		TestEqual(*FString::Printf(TEXT("%s: one row with no option"), Clause.Node), Found, 1);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
