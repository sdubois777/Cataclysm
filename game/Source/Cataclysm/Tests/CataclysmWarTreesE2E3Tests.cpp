// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR TREES' ENGINE SLICES E2 AND E3, AND THE RIDERS (2026-11-27): the conditions, scales and actions their rows
 * read that `CataclysmStatExemptionTests.cpp`'s probes do not already measure. See the decisions entry "The War passive
 * trees finished".
 */
namespace CataclysmWarTreesE2E3Test
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** A creature with a class resource pool of 100 and nothing in it. */
	struct FWarTreesE2E3Holder
	{
		FWarTreesE2E3Holder(UWorld* World, const FVector& At)
		{
			Holder = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
			System = Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
			if (System)
			{
				System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
				System->SetStatInputs({});
			}
		}
		bool IsUsable() const { return Holder && System; }
		float Fervour() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
		ACataclysmEnemyCharacter* Holder = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	float E2E3Value(const FCataclysmStatModifier& Row, const FCataclysmStatConditions& State)
	{
		return UCataclysmStatPipeline::Evaluate(0.0f, {Row}, FGameplayTagContainer(), State).Final;
	}

	FCataclysmStatModifier E2E3Scaled(ECataclysmStatScale Scale, float Value, float Step)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Scale = Scale;
		Row.ScaleStep = Step;
		return Row;
	}
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE2E3ConditionsTest,
	"Cataclysm.WarTreesE2E3.TheThreeConditionsReadTheirFieldsAndAnUnknownReadingRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `two_hits_taken_within_seconds` (Counter Stance), `dual_wielding_outside_frenzy` (Death Strike, Feeding Frenzy) and
 * `hit_taken_was_unblocked` (Consecutive Blocks), each against its field, each with its unknown reading first.
 */
bool FCataclysmWarTreesE2E3ConditionsTest::RunTest(const FString&)
{
	FCataclysmStatConditions State;
	const auto Holds = [&State](ECataclysmStatCondition Condition, float Value = 0.0f)
	{
		return UCataclysmStatPipeline::ConditionHolds(Condition, Value, State);
	};

	TestFalse(TEXT("fewer than two hits taken: not two within a second"),
			  Holds(ECataclysmStatCondition::TwoHitsTakenWithinSeconds, 1.0f));
	State.SecondsBetweenLastTwoHitsTaken = 0.6f;
	TestTrue(TEXT("0.6 seconds apart: two within a second"),
			 Holds(ECataclysmStatCondition::TwoHitsTakenWithinSeconds, 1.0f));
	State.SecondsBetweenLastTwoHitsTaken = 1.4f;
	TestFalse(TEXT("1.4 seconds apart: not within a second"),
			  Holds(ECataclysmStatCondition::TwoHitsTakenWithinSeconds, 1.0f));

	TestFalse(TEXT("no equipment read: not dual wielding outside Frenzy"),
			  Holds(ECataclysmStatCondition::DualWieldingOutsideFrenzy));
	State.WeaponsWorn = 2;
	TestTrue(TEXT("two weapons, not in Frenzy"), Holds(ECataclysmStatCondition::DualWieldingOutsideFrenzy));
	State.bInFrenzy = true;
	TestFalse(TEXT("two weapons in Frenzy refuses"), Holds(ECataclysmStatCondition::DualWieldingOutsideFrenzy));
	State.bInFrenzy = false;
	State.WeaponsWorn = 1;
	TestFalse(TEXT("one weapon refuses"), Holds(ECataclysmStatCondition::DualWieldingOutsideFrenzy));

	TestFalse(TEXT("no hit taken in hand: not an unblocked hit"), Holds(ECataclysmStatCondition::HitTakenWasUnblocked));
	State.HitTakenBlocked = 1;
	TestFalse(TEXT("a blocked hit is not unblocked"), Holds(ECataclysmStatCondition::HitTakenWasUnblocked));
	State.HitTakenBlocked = 0;
	TestTrue(TEXT("an unblocked hit"), Holds(ECataclysmStatCondition::HitTakenWasUnblocked));
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE2E3ScalesTest,
	"Cataclysm.WarTreesE2E3.TheTwoScalesCountWholeStepsAndAnUnknownReadingCountsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Colossus's 10 per 1,000 maximum health from other sources, and Grinding Advance's 1 per 10 seconds of combat held. */
bool FCataclysmWarTreesE2E3ScalesTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE2E3Test;
	FCataclysmStatConditions State;
	const FCataclysmStatModifier Other =
		E2E3Scaled(ECataclysmStatScale::PerPointOfMaximumHealthFromOtherSources, 10.0f, 1000.0f);
	TestEqual(TEXT("unread: nothing"), E2E3Value(Other, State), 0.0f);
	State.MaximumHealthFromOtherSources = 3999.0f;
	TestEqual(TEXT("3,999 is three whole steps: 30"), E2E3Value(Other, State), 30.0f, 0.001f);

	const FCataclysmStatModifier Held = E2E3Scaled(ECataclysmStatScale::PerSecondOfCombatHeld, 1.0f, 10.0f);
	TestEqual(TEXT("unread: nothing"), E2E3Value(Held, State), 0.0f);
	State.CombatSecondsHeld = 47.0f;
	TestEqual(TEXT("47 seconds held is four whole steps"), E2E3Value(Held, State), 4.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE2E3ReadingsTest,
	"Cataclysm.WarTreesE2E3.TheGapBetweenTwoHitsCombatHeldAndDamageTakenAreRecordedAsTheyHappen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * As the game records them: the gap between two landed hits that are not ticks (a tick between them does not count),
 * the seconds of combat held carried from one combat into the next, and the damage taken in the last 5 seconds.
 */
bool FCataclysmWarTreesE2E3ReadingsTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE2E3Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarTreesE2E3Holder Holder(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("a holder"), Holder.IsUsable()))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* System = Holder.System;

	// THE GAP: a hit, a tick 0.3 s later, a hit 0.5 s after that. The gap is 0.8, not 0.5.
	System->NoteHitTaken(true, nullptr, /*bDamageOverTime=*/false);
	World->TimeSeconds += 0.3f;
	System->NoteHitTaken(true, nullptr, /*bDamageOverTime=*/true);
	World->TimeSeconds += 0.5f;
	System->NoteHitTaken(true, nullptr, /*bDamageOverTime=*/false);
	FCataclysmStatModifier Double;
	Double.Bucket = ECataclysmStatBucket::Flat;
	Double.Value = 1.0f;
	Double.Condition = ECataclysmStatCondition::TwoHitsTakenWithinSeconds;
	Double.ConditionValue = 1.0f;
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(TEXT("attack_damage"))).Modifiers.Add(Double);
	System->SetStatInputs(MoveTemp(Lines));
	TestEqual(TEXT("two hits 0.8 seconds apart, a tick between them: within a second"),
			  System->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f), 1.0f, 0.001f);

	// COMBAT HELD: 12 seconds in combat, then out of it for 3 + 4: 15 at the lapse, 11 after four more seconds.
	FWarTreesE2E3Holder Fighter(World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("a second holder"), Fighter.IsUsable()))
	{
		return false;
	}
	Fighter.System->NoteHitDealt();
	for (int32 Second = 0; Second < 12; ++Second)
	{
		World->TimeSeconds += 1.0f;
		Fighter.System->NoteHitDealt();
	}
	TestEqual(TEXT("12 seconds in combat"), Fighter.System->CombatSecondsHeld(), 12.0f, 0.01f);
	World->TimeSeconds += 7.0f;
	TestEqual(TEXT("15 held when the combat lapsed, 4 seconds falling since: 11"),
			  Fighter.System->CombatSecondsHeld(), 11.0f, 0.01f);
	Fighter.System->NoteHitDealt();
	World->TimeSeconds += 2.0f;
	TestEqual(TEXT("carried into the next combat: 11 and 2 more"), Fighter.System->CombatSecondsHeld(), 13.0f, 0.01f);

	// DAMAGE TAKEN RECENTLY: 100, then 50 three seconds later; six seconds after the first only the 50 counts.
	System->NoteDamageTakenAmount(100.0f);
	World->TimeSeconds += 3.0f;
	System->NoteDamageTakenAmount(50.0f);
	TestEqual(TEXT("both inside 5 seconds"), System->DamageTakenRecently(), 150.0f, 0.01f);
	World->TimeSeconds += 3.0f;
	TestEqual(TEXT("the first is 6 seconds old and left out"), System->DamageTakenRecently(), 50.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarTreesE2E3RidersTest,
	"Cataclysm.WarTreesE2E3.CarnageWaitsItsSecondAndUnbreakableWillAddsEveryBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Carnage: a kill gains 1 second of the drain (5 a second, so 5 Fervour), and a second kill inside its 1 second
 * cooldown gains nothing, while one after it does. Unbreakable Will: each block adds its share of the blocked damage to
 * the store, rather than keeping the larger.
 */
bool FCataclysmWarTreesE2E3RidersTest::RunTest(const FString&)
{
	using namespace CataclysmWarTreesE2E3Test;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarTreesE2E3Holder Holder(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("a holder"), Holder.IsUsable()))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* System = Holder.System;

	FCataclysmStatModifier Drain;
	Drain.Bucket = ECataclysmStatBucket::Flat;
	Drain.Value = 5.0f;
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(FCataclysmBerserkerStates::DrainPerSecondStat)).Modifiers.Add(Drain);
	System->SetStatInputs(MoveTemp(Lines));
	FCataclysmPoolAction Carnage;
	Carnage.Event = FName(TEXT("kill"));
	Carnage.bFrenzyDrainFervour = true;
	Carnage.Percent = 1.0f;
	Carnage.TriggerCooldownSeconds = 1.0f;
	Carnage.TriggerKey = FName(TEXT("Carnage"));
	FCataclysmPoolAction Will;
	Will.Event = FName(TEXT("block"));
	Will.NextAttackBonusOf = 3;
	Will.Percent = 6.0f;
	Will.TriggerKey = FName(TEXT("UnbreakableWill"));
	System->SetPoolActions({Carnage, Will});

	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
	TestEqual(TEXT("a kill gains a second of the drain"), Holder.Fervour(), 5.0f, 0.01f);
	World->TimeSeconds += 0.5f;
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, true, nullptr);
	TestEqual(TEXT("a second kill half a second later gains nothing"), Holder.Fervour(), 5.0f, 0.01f);
	World->TimeSeconds += 0.6f;
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, true, nullptr);
	TestEqual(TEXT("and one after the second has passed gains again"), Holder.Fervour(), 10.0f, 0.01f);

	System->ActOnEvent(FName(TEXT("block")), nullptr, /*EventAmount=*/1000.0f, true, nullptr);
	TestEqual(TEXT("a block of 1,000 stores 60"), System->NextAttackBonusStored(), 60.0f, 0.01f);
	World->TimeSeconds += 1.0f;
	System->ActOnEvent(FName(TEXT("block")), nullptr, 500.0f, true, nullptr);
	TestEqual(TEXT("and a block of 500 adds 30, not the larger kept"), System->NextAttackBonusStored(), 90.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
