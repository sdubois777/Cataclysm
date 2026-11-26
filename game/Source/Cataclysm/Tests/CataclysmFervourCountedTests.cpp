// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPassiveTree.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Fervour counted as held (the Death trees' slice C3e, 2026-11-25): the Soul Collector's Chained Souls, "8% per point of
 * the Fervour your minions, gadgets and traps hold back counts as Fervour you hold, for everything that counts Fervour
 * held", and Empty Hands, "+2% increased Attack Speed per point while you hold fewer than 20 Fervour". The share is
 * `FCataclysmStatConditions::ClassResourceCountedAsHeld`, and every reading of the amount held adds it through
 * `UCataclysmStatPipeline::FervourHeldCounted`; whether the bar is full does not, as the coordinating session ruled.
 */

namespace CataclysmFervourCountedTest
{
	/** A stat line of one modifier on a base of 100, evaluated against this state. */
	float FinalUnder(const FCataclysmStatModifier& Row, const FCataclysmStatConditions& State)
	{
		return UCataclysmStatPipeline::Evaluate(100.0f, {Row}, FGameplayTagContainer(), State).Final;
	}

	/** A 50% increase that holds only under `Condition` at `Value`. */
	FCataclysmStatModifier RowUnderCondition(ECataclysmStatCondition Condition, float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = 50.0f;
		Row.Condition = Condition;
		Row.ConditionValue = Value;
		return Row;
	}

	/** 50 held of a bar of 100 with 40 held back, and this much counted as held beside the pool. */
	FCataclysmStatConditions HoldingFiftyWithFortyBack(float CountedAsHeld)
	{
		FCataclysmStatConditions State;
		State.ClassResourceHeld = 50.0f;
		State.ClassResourceMaximum = 100.0f;
		State.ClassResourceReserved = 40.0f;
		State.ClassResourceCountedAsHeld = CountedAsHeld;
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourCountedBelowTest,
	"Cataclysm.FervourCounted.EmptyHandsHoldsBelowTwentyAndNotAtIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `class_resource_points_below` at 20: 19 held holds, 20 does not (strictly below, as "fewer than" says), and 15 held with
 * 6 counted beside it is 21 and does not. THE CONTROL: an unknown holding, every enemy's, does not hold.
 */
bool FCataclysmFervourCountedBelowTest::RunTest(const FString&)
{
	using namespace CataclysmFervourCountedTest;

	const FCataclysmStatModifier Below = RowUnderCondition(ECataclysmStatCondition::ClassResourcePointsBelow, 20.0f);
	FCataclysmStatConditions State;
	State.ClassResourceHeld = 19.0f;
	TestEqual(TEXT("19 held is fewer than 20"), FinalUnder(Below, State), 150.0f, 0.01f);
	State.ClassResourceHeld = 20.0f;
	TestEqual(TEXT("20 held is not"), FinalUnder(Below, State), 100.0f, 0.01f);
	State.ClassResourceHeld = 15.0f;
	State.ClassResourceCountedAsHeld = 6.0f;
	TestEqual(TEXT("15 held and 6 counted is 21, not fewer than 20"), FinalUnder(Below, State), 100.0f, 0.01f);
	State.ClassResourceHeld = -1.0f;
	State.ClassResourceCountedAsHeld = 0.0f;
	TestEqual(TEXT("control: an unknown holding is not fewer than 20"), FinalUnder(Below, State), 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourCountedReadingsTest,
	"Cataclysm.FervourCounted.ChainedSoulsCountsForEveryReadingOfTheAmountButNotTheFullBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 50 held of a bar of 100, 40 held back, and 20 counted as held: at least 70 holds, above 65% holds, fewer than 60 does
 * not, and the held scale counts 7 tens. THE CONTROL for each is the same holding with nothing counted. AND THE FULL BAR
 * DOES NOT COUNT IT: the bar holds 50 of the 60 it can, so it is not full, though 70 is counted.
 */
bool FCataclysmFervourCountedReadingsTest::RunTest(const FString&)
{
	using namespace CataclysmFervourCountedTest;
	using C = ECataclysmStatCondition;

	const FCataclysmStatConditions Counted = HoldingFiftyWithFortyBack(20.0f);
	const FCataclysmStatConditions Plain = HoldingFiftyWithFortyBack(0.0f);

	TestEqual(TEXT("at least 70, counting the share"), FinalUnder(RowUnderCondition(C::ClassResourcePointsAtLeast, 70.0f), Counted),
			  150.0f, 0.01f);
	TestEqual(TEXT("control: not without it"), FinalUnder(RowUnderCondition(C::ClassResourcePointsAtLeast, 70.0f), Plain), 100.0f,
			  0.01f);
	TestEqual(TEXT("above 65%, counting the share"), FinalUnder(RowUnderCondition(C::ClassResourceAbovePercent, 65.0f), Counted),
			  150.0f, 0.01f);
	TestEqual(TEXT("control: not without it"), FinalUnder(RowUnderCondition(C::ClassResourceAbovePercent, 65.0f), Plain), 100.0f,
			  0.01f);
	TestEqual(TEXT("not fewer than 60, counting the share"), FinalUnder(RowUnderCondition(C::ClassResourcePointsBelow, 60.0f), Counted),
			  100.0f, 0.01f);
	TestEqual(TEXT("control: fewer than 60 without it"), FinalUnder(RowUnderCondition(C::ClassResourcePointsBelow, 60.0f), Plain),
			  150.0f, 0.01f);

	FCataclysmStatModifier PerTen;
	PerTen.Bucket = ECataclysmStatBucket::Increased;
	PerTen.Source = ECataclysmModifierSource::PassiveKeystone;
	PerTen.Value = 1.0f;
	PerTen.Scale = ECataclysmStatScale::PerPointOfClassResourceHeld;
	PerTen.ScaleStep = 10.0f;
	TestEqual(TEXT("the held scale counts 7 tens with the share"), FinalUnder(PerTen, Counted), 107.0f, 0.01f);
	TestEqual(TEXT("control: 5 without it"), FinalUnder(PerTen, Plain), 105.0f, 0.01f);

	TestEqual(TEXT("the full bar does not count it: 50 of the 60 the bar can hold is not full"),
			  FinalUnder(RowUnderCondition(C::ClassResourceAtMaximum, 0.0f), Counted), 100.0f, 0.01f);
	FCataclysmStatConditions Full = Counted;
	Full.ClassResourceHeld = 60.0f;
	TestEqual(TEXT("control: 60 of 60 is full"), FinalUnder(RowUnderCondition(C::ClassResourceAtMaximum, 0.0f), Full), 150.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourCountedShareTest,
	"Cataclysm.FervourCounted.ChainedSoulsShareIsItsPercentOfWhatIsHeldBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Chained Souls at six points, 48%: 20 held back counts as 9.6 held. THE CONTROLS: nothing without the node, and
 * nothing of nothing held back.
 */
bool FCataclysmFervourCountedShareTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: an actor"), Actor))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* System = NewObject<UCataclysmAbilitySystemComponent>(Actor);
	System->RegisterComponent();
	System->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
	System->InitAbilityActorInfo(Actor, Actor);

	TestEqual(TEXT("control: without Chained Souls nothing is counted"), System->ReservedFervourCountedAsHeld(20.0f), 0.0f);
	FCataclysmStatModifier Chained;
	Chained.Bucket = ECataclysmStatBucket::Flat;
	Chained.Source = ECataclysmModifierSource::PassiveKeystone;
	Chained.Value = 48.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line =
		Inputs.FindOrAdd(FName(UCataclysmAbilitySystemComponent::ReservedFervourCountsAsHeldPercentStat));
	Line.Base = 0.0f;
	Line.Modifiers = {Chained};
	System->SetStatInputs(MoveTemp(Inputs));
	TestEqual(TEXT("at 48%, 20 held back counts as 9.6 held"), System->ReservedFervourCountedAsHeld(20.0f), 9.6f, 0.001f);
	TestEqual(TEXT("control: nothing held back counts as nothing"), System->ReservedFervourCountedAsHeld(0.0f), 0.0f);
	Actor->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourCountedRealRowsTest,
	"Cataclysm.FervourCounted.TheRealRowsGiveTwelveBelowTwentyAndFortyEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows through the tree's own sum at six points: Empty Hands' 12% increased Attack Speed under fewer than 20
 * Fervour held, and Chained Souls' 48%.
 */
bool FCataclysmFervourCountedRealRowsTest::RunTest(const FString&)
{
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_b_stem1")), 6);
	Allocation.Add(FName(TEXT("SoulCollector_basic_a_stem2")), 6);
	const TMap<FName, TArray<FCataclysmStatModifier>> Rows = UCataclysmPassiveTree::ModifiersFor(
		Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
		{FName(TEXT("Death"))});
	const TArray<FCataclysmStatModifier>* Speed = Rows.Find(FName(TEXT("attack_speed")));
	const TArray<FCataclysmStatModifier>* Chained =
		Rows.Find(FName(UCataclysmAbilitySystemComponent::ReservedFervourCountsAsHeldPercentStat));
	if (!TestNotNull(TEXT("Empty Hands' row"), Speed) || !TestNotNull(TEXT("Chained Souls' row"), Chained)
		|| !TestEqual(TEXT("one row each"), Speed->Num() + Chained->Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("Empty Hands at six points: 12"), (*Speed)[0].Value, 12.0f, 0.001f);
	TestTrue(TEXT("under fewer than ..."), (*Speed)[0].Condition == ECataclysmStatCondition::ClassResourcePointsBelow);
	TestEqual(TEXT("... 20 Fervour"), (*Speed)[0].ConditionValue, 20.0f, 0.001f);
	TestEqual(TEXT("Chained Souls at six points: 48"), (*Chained)[0].Value, 48.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
