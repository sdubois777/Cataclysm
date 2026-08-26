// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "GameplayEffectAggregator.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagsManager.h"

/**
 * Tests for the three-bucket stat pipeline.
 *
 *     Final = (base + flat) x (1 + sum of increases) x more1 x more2 x ...
 *
 * These mirror `sim/tests/test_character.py`. The two implementations have to
 * agree and the Python one is where the numbers were argued out, so several
 * cases here are pinned to values printed by that model rather than to values
 * computed by hand.
 *
 * ONE TEST IS DIFFERENT IN KIND from the rest. AgreesWithTheAbilitySystem
 * builds a real FAggregator and asserts the engine produces the same number.
 * That matters because gear will eventually be applied as ordinary Gameplay
 * Effects, at which point the engine does the arithmetic rather than this
 * class, and the two silently disagreeing would be very hard to notice.
 */

namespace CataclysmStatTest
{
	using FPipeline = UCataclysmStatPipeline;

	FCataclysmStatModifier Make(ECataclysmStatBucket Bucket,
								ECataclysmModifierSource Source,
								float Value,
								const TCHAR* RequiredTag = nullptr)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = Bucket;
		Modifier.Source = Source;
		Modifier.Value = Value;
		if (RequiredTag)
		{
			const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
				FName(RequiredTag), /*ErrorIfNotFound=*/false);
			Modifier.RequiredTags.AddTag(Tag);
		}
		return Modifier;
	}

	FCataclysmStatModifier Flat(float Value)
	{
		return Make(ECataclysmStatBucket::Flat,
					ECataclysmModifierSource::GearAffix, Value);
	}

	FCataclysmStatModifier Increased(float Value, const TCHAR* RequiredTag = nullptr)
	{
		return Make(ECataclysmStatBucket::Increased,
					ECataclysmModifierSource::GearAffix, Value, RequiredTag);
	}

	FCataclysmStatModifier MoreFromGem(float Value, const TCHAR* RequiredTag = nullptr)
	{
		return Make(ECataclysmStatBucket::More,
					ECataclysmModifierSource::Gem, Value, RequiredTag);
	}

	/** An increase that applies only at or below a share of maximum health. */
	FCataclysmStatModifier IncreasedBelowHealth(float Value, float Percent,
												const TCHAR* RequiredTag = nullptr)
	{
		FCataclysmStatModifier Modifier = Increased(Value, RequiredTag);
		Modifier.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Modifier.ConditionValue = Percent;
		return Modifier;
	}

	/** A character standing at this share of its maximum health. */
	FCataclysmStatConditions AtHealth(float Percent)
	{
		return FCataclysmStatConditions::FromHealth(Percent, 100.0f);
	}

	/** An increase that applies only inside a window after a health cost. #962 */
	FCataclysmStatModifier IncreasedAfterHealthCost(float Value, float Seconds,
													const TCHAR* RequiredTag = nullptr)
	{
		FCataclysmStatModifier Modifier = Increased(Value, RequiredTag);
		Modifier.Condition =
			ECataclysmStatCondition::WithinSecondsOfHealthCost;
		Modifier.ConditionValue = Seconds;
		return Modifier;
	}

	/**
	 * A character that paid a health cost this many seconds ago.
	 *
	 * A NEGATIVE ARGUMENT IS "NEVER PAID ONE", which is the state a character
	 * that has not cast anything is in, and is deliberately spelt the same way
	 * an unknown reading is.
	 */
	FCataclysmStatConditions SinceHealthCost(float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceHealthCost = Seconds;
		return State;
	}

	/** An increase that applies only inside a window after foreign damage. */
	FCataclysmStatModifier IncreasedAfterForeignDamage(float Value, float Seconds)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition =
			ECataclysmStatCondition::WithinSecondsOfForeignDamage;
		Modifier.ConditionValue = Seconds;
		return Modifier;
	}

	/** A character that took damage of a foreign type this many seconds ago. */
	FCataclysmStatConditions SinceForeignDamage(float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceForeignDamage = Seconds;
		return State;
	}

	/** An increase worth `Value` per whole `Step` percent of health missing. */
	FCataclysmStatModifier IncreasedPerHealthMissing(float Value, float Step)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Scale =
			ECataclysmStatScale::PerPercentOfMaximumHealthMissing;
		Modifier.ScaleStep = Step;
		return Modifier;
	}

	/** An increase that applies only when the skill in hand cost more. */
	FCataclysmStatModifier IncreasedAboveSkillCost(float Value, float Percent)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition =
			ECataclysmStatCondition::SkillHealthCostAbovePercent;
		Modifier.ConditionValue = Percent;
		return Modifier;
	}

	/**
	 * A blow from a skill that cost this share of maximum health.
	 *
	 * A NEGATIVE ARGUMENT IS "NO SKILL IN HAND", which is the character
	 * sheet and every blow with no skill behind it, and is spelt the same
	 * way an unknown reading is. Zero means a skill that cost nothing.
	 */
	FCataclysmStatConditions SkillCosting(float Percent)
	{
		FCataclysmStatConditions State;
		State.SkillHealthCostPercent = Percent;
		return State;
	}

	/** An increase worth `Value` per whole `Step` points of the class resource. */
	FCataclysmStatModifier IncreasedPerResourceHeld(float Value, float Step)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Scale = ECataclysmStatScale::PerPointOfClassResourceHeld;
		Modifier.ScaleStep = Step;
		return Modifier;
	}

	/**
	 * A character holding this much of the class resource.
	 *
	 * A NEGATIVE ARGUMENT IS "NO SUCH POOL", which is every enemy in the game
	 * and is spelt the same way an unknown reading is. Zero is a real answer and
	 * means the bar is empty.
	 */
	FCataclysmStatConditions HoldingResource(float Amount)
	{
		FCataclysmStatConditions State;
		State.ClassResourceHeld = Amount;
		return State;
	}

	/** An increase worth `Value` per whole `Step` percent of health OWED. */
	FCataclysmStatModifier IncreasedPerHealthOwed(float Value, float Step)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Scale = ECataclysmStatScale::PerPercentOfMaximumHealthOwed;
		Modifier.ScaleStep = Step;
		return Modifier;
	}

	/**
	 * A character owing this share of its maximum health. Issue #994.
	 *
	 * A NEGATIVE ARGUMENT IS "THERE IS NOTHING TO READ", which is every enemy in
	 * the game, the character sheet, and a character whose maximum health is
	 * zero. Zero is a real answer and means the character owes nothing.
	 *
	 * ABOVE A HUNDRED IS LEGITIMATE. A debt larger than the character's whole
	 * pool is exactly what The Reckoning kills them for.
	 */
	FCataclysmStatConditions Owing(float Percent)
	{
		FCataclysmStatConditions State;
		State.HealthOwedPercent = Percent;
		return State;
	}

	/** An increase worth `Value` per whole `Step` stacks of one kind. */
	FCataclysmStatModifier IncreasedPerStack(float Value, float Step,
											 ECataclysmStatScale Kind)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Scale = Kind;
		Modifier.ScaleStep = Step;
		return Modifier;
	}

	/**
	 * A character holding this many stacks of each kind. Issues #1002 to #1004.
	 *
	 * NO NEGATIVE "UNKNOWN" HERE, unlike every other state in this file. A
	 * caller with no character holds no stacks and a character that has earned
	 * none holds no stacks; both are worth nothing to a bonus counting them, and
	 * nothing can act differently on the two, so there is nothing to tell apart.
	 */
	FCataclysmStatConditions Holding(int32 Momentum, int32 Bloodlust,
									 int32 Carnage)
	{
		FCataclysmStatConditions State;
		State.SanguineMomentumStacks = Momentum;
		State.BloodlustStacks = Bloodlust;
		State.CarnageStacks = Carnage;
		return State;
	}

	FGameplayTagContainer Tags(std::initializer_list<const TCHAR*> Names)
	{
		FGameplayTagContainer Container;
		for (const TCHAR* Name : Names)
		{
			Container.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
				FName(Name), /*ErrorIfNotFound=*/false));
		}
		return Container;
	}

	/** No skill in hand. Only unscoped modifiers apply. */
	const FGameplayTagContainer NoTags;
}

// ---------------------------------------------------------------------------
// The order of the three buckets
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineOrderTest,
	"Cataclysm.StatPipeline.ThreeBucketsCombineInTheDesignedOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineOrderTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// Pinned to sim/cataclysm_sim/character.py. A level 100 character on the
	// default stat line has 1,585 maximum health before anything applies.
	// Adding 1,200 flat, 340% and 55% increased, and two more multipliers of
	// 50% and 30% gives, in that model:
	//
	//   base + flat      2,785
	//   sum of increases 395%
	//   more multiplier  1.95   (1.50 x 1.30, not 1.80)
	//   final            26,882.2125
	TArray<FCataclysmStatModifier> Modifiers = {
		Flat(1200.0f),
		Increased(340.0f),
		Increased(55.0f),
		MoreFromGem(50.0f),
		Make(ECataclysmStatBucket::More,
			 ECataclysmModifierSource::PassiveKeystone, 30.0f),
	};

	const FCataclysmStatBreakdown Result =
		FPipeline::Evaluate(1585.0f, Modifiers, NoTags);

	TestTrue(TEXT("flat adds up to 1,200"),
		FMath::IsNearlyEqual(Result.Flat, 1200.0f, 0.001f));
	TestTrue(TEXT("increases sum to 395 percentage points"),
		FMath::IsNearlyEqual(Result.SumOfIncreases, 395.0f, 0.001f));
	TestTrue(TEXT("the two more multipliers give 1.95, not 1.80"),
		FMath::IsNearlyEqual(Result.MoreMultiplier, 1.95f, 0.0001f));
	TestEqual(TEXT("two more sources applied"), Result.MoreSourceCount, 2);
	TestTrue(TEXT("final matches the Python model's 26,882.2125"),
		FMath::IsNearlyEqual(Result.Final, 26882.2125f, 0.05f));

	return true;
}

// ---------------------------------------------------------------------------
// Why there are three buckets and not two
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineDiminishingTest,
	"Cataclysm.StatPipeline.IncreasedDiminishesAndMoreDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineDiminishingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// The comparison recorded in docs/DECISIONS.md, which is the entire reason
	// the two multiplicative buckets are kept apart. A character already at
	// +800% increased who adds another 60% increased gains 6.7%. The same
	// character adding a 60% more multiplier gains 60%.
	const float Base = 100.0f;

	TArray<FCataclysmStatModifier> Starting = { Increased(800.0f) };
	const float Before = FPipeline::Evaluate(Base, Starting, NoTags).Final;
	TestTrue(TEXT("+800% increased gives 9x"),
		FMath::IsNearlyEqual(Before, 900.0f, 0.01f));

	TArray<FCataclysmStatModifier> PlusIncreased = { Increased(800.0f), Increased(60.0f) };
	const float WithIncreased = FPipeline::Evaluate(Base, PlusIncreased, NoTags).Final;

	TArray<FCataclysmStatModifier> PlusMore = { Increased(800.0f), MoreFromGem(60.0f) };
	const float WithMore = FPipeline::Evaluate(Base, PlusMore, NoTags).Final;

	const float IncreasedGain = 100.0f * (WithIncreased / Before - 1.0f);
	const float MoreGain = 100.0f * (WithMore / Before - 1.0f);

	TestTrue(FString::Printf(TEXT("another 60%% increased gains 6.7%%, got %.2f%%"),
			 IncreasedGain),
		FMath::IsNearlyEqual(IncreasedGain, 6.667f, 0.01f));
	TestTrue(FString::Printf(TEXT("a 60%% more multiplier gains 60%%, got %.2f%%"),
			 MoreGain),
		FMath::IsNearlyEqual(MoreGain, 60.0f, 0.01f));

	// Stated the other way round, which is the shape a player actually meets:
	// two 50% sources are worth 2.0x in one bucket and 2.25x in the other.
	TArray<FCataclysmStatModifier> TwoIncreases = { Increased(50.0f), Increased(50.0f) };
	TArray<FCataclysmStatModifier> TwoMores = { MoreFromGem(50.0f), MoreFromGem(50.0f) };

	TestTrue(TEXT("two 50% increases give 2.0x"),
		FMath::IsNearlyEqual(FPipeline::Evaluate(100.0f, TwoIncreases, NoTags).Final,
							 200.0f, 0.01f));
	TestTrue(TEXT("two 50% more multipliers give 2.25x"),
		FMath::IsNearlyEqual(FPipeline::Evaluate(100.0f, TwoMores, NoTags).Final,
							 225.0f, 0.01f));

	return true;
}

// ---------------------------------------------------------------------------
// Agreement with the engine's own aggregator
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineAggregatorTest,
	"Cataclysm.StatPipeline.AgreesWithTheAbilitySystemAggregator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineAggregatorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// The engine computes
	//   ((Base + AddBase) * MultiplyAdditive / DivideAdditive * MultiplyCompound)
	//       + AddFinal
	// where MultiplyAdditive sums its modifiers with a bias of 1.0 and
	// MultiplyCompound multiplies each separately. So the design's three buckets
	// are AddBase, MultiplyAdditive and MultiplyCompound, and this asserts it
	// rather than assuming it.
	//
	// The engine stores a percentage as a factor: +50% is 1.5. This pipeline
	// stores percentage points, so the conversion is done here in the open,
	// which is also what documents the boundary.
	const float Base = 1585.0f;
	const float FlatPoints = 1200.0f;
	const float IncreasedA = 340.0f;
	const float IncreasedB = 55.0f;
	const float MoreA = 50.0f;
	const float MoreB = 30.0f;

	FAggregator Aggregator(Base);
	Aggregator.AddAggregatorMod(FlatPoints, EGameplayModOp::AddBase,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	Aggregator.AddAggregatorMod(1.0f + IncreasedA / 100.0f, EGameplayModOp::MultiplyAdditive,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	Aggregator.AddAggregatorMod(1.0f + IncreasedB / 100.0f, EGameplayModOp::MultiplyAdditive,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	Aggregator.AddAggregatorMod(1.0f + MoreA / 100.0f, EGameplayModOp::MultiplyCompound,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	Aggregator.AddAggregatorMod(1.0f + MoreB / 100.0f, EGameplayModOp::MultiplyCompound,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);

	FAggregatorEvaluateParameters EvaluateParameters;
	const float EngineValue = Aggregator.Evaluate(EvaluateParameters);

	TArray<FCataclysmStatModifier> Modifiers = {
		Flat(FlatPoints),
		Increased(IncreasedA),
		Increased(IncreasedB),
		MoreFromGem(MoreA),
		MoreFromGem(MoreB),
	};
	const float OurValue = FPipeline::Evaluate(Base, Modifiers, NoTags).Final;

	TestTrue(FString::Printf(
			TEXT("the engine gives %.4f and this pipeline gives %.4f"),
			EngineValue, OurValue),
		FMath::IsNearlyEqual(EngineValue, OurValue, 0.05f));

	// Guards the comparison. If the two happened to agree because both were
	// zero, or because the aggregator ignored every modifier, the assertion
	// above would pass while proving nothing.
	TestTrue(TEXT("the aggregator actually applied the modifiers"),
		EngineValue > Base * 2.0f);

	// The engine's own separation of the two multiplicative buckets, checked
	// directly: two compound modifiers must not sum.
	FAggregator Compound(100.0f);
	Compound.AddAggregatorMod(1.5f, EGameplayModOp::MultiplyCompound,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	Compound.AddAggregatorMod(1.5f, EGameplayModOp::MultiplyCompound,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	TestTrue(TEXT("the engine compounds MultiplyCompound to 2.25x"),
		FMath::IsNearlyEqual(Compound.Evaluate(EvaluateParameters), 225.0f, 0.01f));

	FAggregator Additive(100.0f);
	Additive.AddAggregatorMod(1.5f, EGameplayModOp::MultiplyAdditive,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	Additive.AddAggregatorMod(1.5f, EGameplayModOp::MultiplyAdditive,
		EGameplayModEvaluationChannel::Channel0, nullptr, nullptr, false);
	TestTrue(TEXT("the engine sums MultiplyAdditive to 2.0x"),
		FMath::IsNearlyEqual(Additive.Evaluate(EvaluateParameters), 200.0f, 0.01f));

	return true;
}

// ---------------------------------------------------------------------------
// A bonus that depends on the state of the character
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineHealthConditionTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnTheCharactersHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineHealthConditionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S LAST STAND NODE, held at its full eight points: "While at
	// or below 20% health, +3% increased Critical Strike Chance per point", so
	// +24% and only under a fifth of maximum health. Issue #959.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedBelowHealth(24.0f, 20.0f) };

	TestEqual(TEXT("a character at full health does not get it"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(100.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("nor one just above the threshold"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(20.1f)).Final,
		100.0f, 0.01f);

	// AT OR BELOW, NOT BELOW. Every node stating a threshold is written "at or
	// below", so a character sitting exactly on the number gets the bonus. The
	// difference matters at the instant a blow takes health to exactly that
	// figure, which is the instant the node is about.
	TestEqual(TEXT("a character exactly on the threshold gets it"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(20.0f)).Final,
		124.0f, 0.01f);
	TestEqual(TEXT("and one below it"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(5.0f)).Final,
		124.0f, 0.01f);

	// AN UNKNOWN STATE REFUSES, which is what the character sheet asks with.
	// Folding a conditional bonus into a gameplay attribute would make it stale
	// the moment health moved, so the sheet must not see it at all.
	TestEqual(TEXT("a caller that knows nothing about the character does not get it"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);

	// A CONDITION AND A REQUIRED TAG ARE TWO QUESTIONS AND BOTH MUST BE YES.
	// One asks about the character and the other about the skill in hand, so a
	// modifier carrying both is refused when either fails.
	TArray<FCataclysmStatModifier> Both = {
		IncreasedBelowHealth(24.0f, 20.0f, TEXT("Type.AOE")) };
	const FGameplayTagContainer AreaSkill = Tags({ TEXT("Type.AOE") });

	TestEqual(TEXT("low health and the right skill gets it"),
		FPipeline::Evaluate(100.0f, Both, AreaSkill, AtHealth(10.0f)).Final,
		124.0f, 0.01f);
	TestEqual(TEXT("low health and the wrong skill does not"),
		FPipeline::Evaluate(100.0f, Both, NoTags, AtHealth(10.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("the right skill at full health does not"),
		FPipeline::Evaluate(100.0f, Both, AreaSkill, AtHealth(100.0f)).Final,
		100.0f, 0.01f);

	// AND IT JOINS THE SUM OF INCREASES RATHER THAN MULTIPLYING SEPARATELY,
	// which is the design's own rule: "a conditional increase joins the
	// increases bracket rather than becoming a third multiplier". A base of 100
	// with an unconditional +50% and a conditional +50% is 200 through one
	// bracket and 225 through two.
	TArray<FCataclysmStatModifier> Two = {
		Increased(50.0f), IncreasedBelowHealth(50.0f, 20.0f) };
	TestEqual(TEXT("two increases sum into one bracket, reaching 200"),
		FPipeline::Evaluate(100.0f, Two, NoTags, AtHealth(10.0f)).Final,
		200.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineHealthCostWindowTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnAWindowAfterAHealthCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineHealthCostWindowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S BLOOD RUSH NODE, held at its full eight points: "+2%
	// increased damage per point for 2 seconds after you pay a health cost", so
	// +16% for two seconds. Issue #962.
	//
	// WHAT MAKES THIS PREDICATE DIFFERENT FROM THE HEALTH ONE ABOVE. That asks
	// what is true now; this asks how long ago something happened. A character
	// standing perfectly still, changing nothing, stops satisfying it.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedAfterHealthCost(16.0f, 2.0f) };

	TestEqual(TEXT("just after paying, the bonus applies"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SinceHealthCost(0.0f)).Final,
		116.0f, 0.01f);
	TestEqual(TEXT("and part way through the window"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SinceHealthCost(1.5f)).Final,
		116.0f, 0.01f);

	// THE WINDOW INCLUDES ITS LAST INSTANT, matching the "at or below" reading
	// the health predicate uses. No player can time the difference, and two
	// predicates disagreeing about their own boundaries would be worse than
	// either answer.
	TestEqual(TEXT("and at exactly its last instant"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SinceHealthCost(2.0f)).Final,
		116.0f, 0.01f);

	TestEqual(TEXT("a moment later it is gone"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SinceHealthCost(2.1f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and long afterwards"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SinceHealthCost(60.0f)).Final,
		100.0f, 0.01f);

	// A CHARACTER THAT HAS NEVER PAID ONE DOES NOT GET IT, and neither does a
	// caller that knows nothing about the character. Both are spelt as a
	// negative reading, deliberately: both answer no, so they do not have to be
	// told apart. Without this a fresh character would start every fight already
	// inside the window.
	TestEqual(TEXT("a character that has never paid a health cost does not get it"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SinceHealthCost(-1.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("nor does the character sheet, which knows nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);

	// THE TWO PREDICATES ARE INDEPENDENT, so a state that knows one and not the
	// other answers each on its own merits. This is what stopped
	// `CurrentConditions` returning early when a character had no vital
	// attribute set: doing so shut a window that was genuinely open.
	FCataclysmStatConditions PaidButHealthUnknown = SinceHealthCost(1.0f);
	TestEqual(TEXT("a window is judged even when health is unknown"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, PaidButHealthUnknown).Final,
		116.0f, 0.01f);

	TArray<FCataclysmStatModifier> Health = { IncreasedBelowHealth(24.0f, 20.0f) };
	TestEqual(TEXT("and a health threshold is still refused by that same state"),
		FPipeline::Evaluate(100.0f, Health, NoTags, PaidButHealthUnknown).Final,
		100.0f, 0.01f);

	// AND A WINDOW OF NO LENGTH IS REFUSED WHEN THE DATA IS CHECKED, rather than
	// silently granting nothing. `ValidateModifier` is what data import calls.
	TestTrue(TEXT("a window of zero seconds is reported as illegal"),
		!FPipeline::ValidateModifier(
			IncreasedAfterHealthCost(16.0f, 0.0f)).IsEmpty());
	TestTrue(TEXT("and so is a negative one"),
		!FPipeline::ValidateModifier(
			IncreasedAfterHealthCost(16.0f, -2.0f)).IsEmpty());
	TestTrue(TEXT("a real window is not"),
		FPipeline::ValidateModifier(
			IncreasedAfterHealthCost(16.0f, 2.0f)).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineForeignDamageWindowTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnAWindowAfterForeignDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineForeignDamageWindowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S CATACLYSMIC RESONANCE NODE, held at its full eight
	// points: "+1% increased damage per point for 5 seconds after you take
	// damage of a Cataclysm type other than Demonic". Issue #975.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedAfterForeignDamage(8.0f, 5.0f) };

	TestEqual(TEXT("just after the hit, the bonus applies"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							SinceForeignDamage(0.0f)).Final, 108.0f, 0.01f);
	TestEqual(TEXT("and part way through the window"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							SinceForeignDamage(4.0f)).Final, 108.0f, 0.01f);
	TestEqual(TEXT("and at exactly its last instant"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							SinceForeignDamage(5.0f)).Final, 108.0f, 0.01f);
	TestEqual(TEXT("a moment later it is gone"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							SinceForeignDamage(5.1f)).Final, 100.0f, 0.01f);

	// A CHARACTER THAT HAS TAKEN NO SUCH HIT DOES NOT GET IT, and neither does
	// a caller that knows nothing. Both are a negative reading.
	TestEqual(TEXT("a character that has taken no foreign hit does not get it"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							SinceForeignDamage(-1.0f)).Final, 100.0f, 0.01f);
	TestEqual(TEXT("nor does the character sheet, which knows nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);

	// THE TWO WINDOWS ARE SEPARATE EVENTS AND SEPARATE READINGS. A character
	// that paid a health cost a moment ago has not thereby taken foreign
	// damage, and this is what says one enumerator per event is doing real
	// work rather than being two names for one timer.
	TestEqual(TEXT("paying a health cost does not open the foreign damage window"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							SinceHealthCost(0.0f)).Final, 100.0f, 0.01f);

	TArray<FCataclysmStatModifier> AfterCost = {
		IncreasedAfterHealthCost(8.0f, 5.0f) };
	TestEqual(TEXT("and taking foreign damage does not open the health cost one"),
		FPipeline::Evaluate(100.0f, AfterCost, NoTags,
							SinceForeignDamage(0.0f)).Final, 100.0f, 0.01f);

	// AND A WINDOW OF NO LENGTH IS REFUSED WHEN THE DATA IS CHECKED.
	TestTrue(TEXT("a window of zero seconds is reported as illegal"),
		!FPipeline::ValidateModifier(
			IncreasedAfterForeignDamage(8.0f, 0.0f)).IsEmpty());
	TestTrue(TEXT("a real one is not"),
		FPipeline::ValidateModifier(Modifiers[0]).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineHealthMissingScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithHowMuchHealthIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineHealthMissingScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S VICIOUS ONSLAUGHT NODE, held at its full ten points: "+1%
	// increased Attack Damage per point for every 5% of your maximum health that
	// is missing", so ten percentage points per whole 5% missing. Issue #968.
	//
	// A DIFFERENT AXIS FROM A CONDITION, and that is the point of the shape. A
	// condition decides whether a modifier is in the sum at all; this decides
	// how large it is when it is.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedPerHealthMissing(10.0f, 5.0f) };

	// A CHARACTER AT FULL HEALTH HAS NO STEPS AND GETS NOTHING, which is what
	// makes the node a reward for being hurt rather than a flat bonus.
	TestEqual(TEXT("at full health it is worth nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(100.0f)).Final,
		100.0f, 0.01f);

	// WHOLE STEPS, ROUNDED DOWN. Ten percent missing is exactly two steps;
	// eleven and fourteen are still two, because "for every 5%" counts completed
	// blocks. The genre agrees: Path of Exile pays a "per 10 Strength" bonus once
	// at 15 Strength, not one and a half times.
	TestEqual(TEXT("ten percent missing is two whole steps, so +20%"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(90.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("and so is eleven percent missing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(89.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("and fourteen"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(86.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("fifteen percent missing is the third step"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(85.0f)).Final,
		130.0f, 0.01f);

	// A CHARACTER ON ALMOST NOTHING CARRIES TWENTY STEPS, which is what the node
	// is worth at its largest and is worth pinning: this is the figure a balance
	// argument would be made from.
	TestEqual(TEXT("at no health at all it is twenty steps, so +200%"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(0.0f)).Final,
		300.0f, 0.01f);

	// AN UNKNOWN STATE SCALES TO NOTHING, which is what the character sheet asks
	// with. A scaled bonus folded into a gameplay attribute would be stale the
	// moment health moved, so the sheet must not see it at all.
	TestEqual(TEXT("a caller that knows nothing about the character gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);

	// A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, and is
	// refused outright when the data is checked.
	TArray<FCataclysmStatModifier> NoStep = {
		IncreasedPerHealthMissing(10.0f, 0.0f) };
	TestEqual(TEXT("a step of nothing is worth nothing at every state"),
		FPipeline::Evaluate(100.0f, NoStep, NoTags, AtHealth(10.0f)).Final,
		100.0f, 0.01f);
	TestTrue(TEXT("and is reported as illegal when the data is checked"),
		!FPipeline::ValidateModifier(NoStep[0]).IsEmpty());
	TestTrue(TEXT("while a real step is not"),
		FPipeline::ValidateModifier(Modifiers[0]).IsEmpty());

	// THE TWO AXES COMBINE, and neither implies anything about the other. A
	// modifier that both scales and carries a condition is refused entirely when
	// the condition fails, however many steps the character has.
	FCataclysmStatModifier BothAxes = IncreasedPerHealthMissing(10.0f, 5.0f);
	BothAxes.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
	BothAxes.ConditionValue = 50.0f;
	TArray<FCataclysmStatModifier> Combined = { BothAxes };

	TestEqual(TEXT("above the threshold it is refused despite having steps"),
		FPipeline::Evaluate(100.0f, Combined, NoTags, AtHealth(60.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("at the threshold it applies, scaled by ten steps"),
		FPipeline::Evaluate(100.0f, Combined, NoTags, AtHealth(50.0f)).Final,
		200.0f, 0.01f);

	// AND A FIXED MODIFIER IS UNTOUCHED BY ANY OF THIS, which is every modifier
	// in the game before this issue.
	TArray<FCataclysmStatModifier> Plain = { Increased(50.0f) };
	TestEqual(TEXT("a fixed increase is worth its value whatever the health"),
		FPipeline::Evaluate(100.0f, Plain, NoTags, AtHealth(10.0f)).Final,
		150.0f, 0.01f);
	TestEqual(TEXT("and the same with no state at all"),
		FPipeline::Evaluate(100.0f, Plain, NoTags).Final, 150.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineResourceHeldScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithHowMuchClassResourceIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineResourceHeldScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S RECIPROCITY KEYSTONE: "Your Retaliation damage is
	// increased by 1% for each point of Fervour you currently hold." One
	// percentage point per point held, so the step is one. Issue #980.
	//
	// THE SECOND STATE A BONUS'S SIZE CAN GROW WITH, and it counts an absolute
	// amount rather than a percentage. The pool runs 0 to 100 for every class
	// today so the two readings happen to agree, and they would stop agreeing
	// the moment a class had a different maximum.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedPerResourceHeld(1.0f, 1.0f) };

	// AN EMPTY BAR IS WORTH NOTHING, which is the state every character starts
	// a fight in and is what makes the node a reward for generating.
	TestEqual(TEXT("holding nothing is worth nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, HoldingResource(0.0f)).Final,
		100.0f, 0.01f);

	// AND A POINT IS A STEP.
	TestEqual(TEXT("forty points held is +40%"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, HoldingResource(40.0f)).Final,
		140.0f, 0.01f);

	// A FULL BAR DOUBLES IT, which is the figure a balance argument would be
	// made from and is worth pinning. Every class line gives the pool a maximum
	// of 100.
	TestEqual(TEXT("a full bar of a hundred is +100%"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, HoldingResource(100.0f)).Final,
		200.0f, 0.01f);

	// WHOLE STEPS, ROUNDED DOWN, which a step of one cannot show. The design's
	// own node uses a step of one, so this is checked at a step the node does
	// not use rather than left unchecked: without it the rule would be stated in
	// the enumerator and asserted nowhere.
	TArray<FCataclysmStatModifier> InFives = {
		IncreasedPerResourceHeld(10.0f, 5.0f) };
	TestEqual(TEXT("nine points held is one whole step of five, so +10%"),
		FPipeline::Evaluate(100.0f, InFives, NoTags, HoldingResource(9.0f)).Final,
		110.0f, 0.01f);
	TestEqual(TEXT("and ten points is the second step"),
		FPipeline::Evaluate(100.0f, InFives, NoTags, HoldingResource(10.0f)).Final,
		120.0f, 0.01f);

	// NO SUCH POOL SCALES TO NOTHING, and it is a different statement from an
	// empty bar. Every enemy in the game has no class resource attribute set, and
	// so does the character sheet, which has no character in hand at all.
	TestEqual(TEXT("a caller that knows nothing about the character gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("and so does one that says outright there is no pool"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, HoldingResource(-1.0f)).Final,
		100.0f, 0.01f);

	// A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, and is
	// refused outright when the data is checked. The same rule as the other
	// scale, asserted here so that adding a scale cannot skip it.
	TArray<FCataclysmStatModifier> NoStep = {
		IncreasedPerResourceHeld(10.0f, 0.0f) };
	TestEqual(TEXT("a step of nothing is worth nothing at every state"),
		FPipeline::Evaluate(100.0f, NoStep, NoTags, HoldingResource(50.0f)).Final,
		100.0f, 0.01f);
	TestTrue(TEXT("and is reported as illegal when the data is checked"),
		!FPipeline::ValidateModifier(NoStep[0]).IsEmpty());
	TestTrue(TEXT("while a real step is not"),
		FPipeline::ValidateModifier(Modifiers[0]).IsEmpty());

	// THE TWO STATES ARE INDEPENDENT, which is what makes them two scales rather
	// than one. A bonus counting the pool is unmoved by where health is, and a
	// state that knows only the pool cannot answer the other.
	FCataclysmStatConditions PoolOnly = HoldingResource(50.0f);
	TestTrue(TEXT("a state that knows the pool need not know the health"),
		PoolOnly.HealthPercent < 0.0f);
	TestEqual(TEXT("and the pool bonus is worth its fifty steps regardless"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, PoolOnly).Final,
		150.0f, 0.01f);

	TArray<FCataclysmStatModifier> ByHealth = {
		IncreasedPerHealthMissing(1.0f, 1.0f) };
	TestEqual(TEXT("while a health bonus gets nothing from a full pool alone"),
		FPipeline::Evaluate(100.0f, ByHealth, NoTags, HoldingResource(100.0f)).Final,
		100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineHealthOwedScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithHowMuchHealthIsOwed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineHealthOwedScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S COMPOUND INTEREST NODE at its full eight points: "+1%
	// increased damage per point for every 5% of your maximum health you
	// currently owe", so eight percentage points per whole 5% owed. Issue #994.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedPerHealthOwed(8.0f, 5.0f) };

	// OWING NOTHING IS WORTH NOTHING, which is the state every character is in
	// before its first deferred cost and is what makes the node a reward for
	// being in debt rather than a flat bonus.
	TestEqual(TEXT("owing nothing is worth nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, Owing(0.0f)).Final,
		100.0f, 0.01f);

	// AND FOUR WHOLE STEPS OF FIVE IS FOUR TIMES EIGHT.
	TestEqual(TEXT("owing 20% is four steps, so +32%"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, Owing(20.0f)).Final,
		132.0f, 0.01f);

	// WHOLE STEPS, ROUNDED DOWN. Owing 24% is still four completed blocks of
	// five, not four and four fifths, and 25% is the fifth.
	TestEqual(TEXT("owing 24% is still four steps"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, Owing(24.0f)).Final,
		132.0f, 0.01f);
	TestEqual(TEXT("and 25% is the fifth step"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, Owing(25.0f)).Final,
		140.0f, 0.01f);

	// A DEBT LARGER THAN THE WHOLE POOL KEEPS COUNTING. Nothing caps what is
	// owed at a character's maximum health, and The Reckoning is built on that:
	// "If your debt ever exceeds your current health, you die." A bonus that
	// stopped at 100% would quietly make the largest debts worth no more than a
	// full one.
	TArray<FCataclysmStatModifier> Reckoning = {
		IncreasedPerHealthOwed(1.0f, 2.0f) };
	TestEqual(TEXT("owing one and a half times the pool is 75 steps of two"),
		FPipeline::Evaluate(100.0f, Reckoning, NoTags, Owing(150.0f)).Final,
		175.0f, 0.01f);

	// NOTHING TO READ SCALES TO NOTHING, and it is a different statement from
	// owing nothing. The character sheet has no character in hand, and every
	// enemy has no class resource attribute set to hold a debt at all.
	TestEqual(TEXT("a caller that knows nothing about the character gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("and so does one that says outright there is nothing to read"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, Owing(-1.0f)).Final,
		100.0f, 0.01f);

	// A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, and is
	// refused outright when the data is checked. The same rule as the two scales
	// above, asserted here so that adding a scale cannot skip it.
	TArray<FCataclysmStatModifier> NoStep = {
		IncreasedPerHealthOwed(10.0f, 0.0f) };
	TestEqual(TEXT("a step of nothing is worth nothing at every state"),
		FPipeline::Evaluate(100.0f, NoStep, NoTags, Owing(50.0f)).Final,
		100.0f, 0.01f);
	TestTrue(TEXT("and is reported as illegal when the data is checked"),
		!FPipeline::ValidateModifier(NoStep[0]).IsEmpty());
	TestTrue(TEXT("while a real step is not"),
		FPipeline::ValidateModifier(Modifiers[0]).IsEmpty());

	// OWED IS NOT MISSING, WHICH IS THE WHOLE REASON THIS IS A THIRD SCALE.
	// A character that deferred a cost owes health it is still standing on, so
	// it is at FULL health and owes a fifth of it. Reading either state through
	// the other would hand Compound Interest's bonus to Vicious Onslaught's node
	// and the other way round, and no arithmetic would report it.
	FCataclysmStatConditions OwedOnly = Owing(20.0f);
	TestTrue(TEXT("a state that knows what is owed need not know the health"),
		OwedOnly.HealthPercent < 0.0f);

	TArray<FCataclysmStatModifier> ByMissing = {
		IncreasedPerHealthMissing(8.0f, 5.0f) };
	TestEqual(TEXT("a missing-health bonus gets nothing from a debt alone"),
		FPipeline::Evaluate(100.0f, ByMissing, NoTags, OwedOnly).Final,
		100.0f, 0.01f);

	// AND THE OTHER WAY: a character at full health that owes a fifth gets the
	// whole of Compound Interest's bonus, because being at full health says
	// nothing about what is owed.
	FCataclysmStatConditions FullAndInDebt =
		FCataclysmStatConditions::FromHealth(1'000.0f, 1'000.0f);
	FullAndInDebt.HealthOwedPercent = 20.0f;
	TestEqual(TEXT("a character at full health that owes a fifth still gets +32%"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, FullAndInDebt).Final,
		132.0f, 0.01f);
	TestEqual(TEXT("while the missing-health bonus gets nothing from it"),
		FPipeline::Evaluate(100.0f, ByMissing, NoTags, FullAndInDebt).Final,
		100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineStackScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithACountOfStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineStackScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S SANGUINE MOMENTUM NODE at its full six points: "Each stack
	// gives +1% increased attack and cast speed per point", so six percentage
	// points a stack. Issue #1002.
	TArray<FCataclysmStatModifier> Momentum = {
		IncreasedPerStack(6.0f, 1.0f,
						  ECataclysmStatScale::PerStackOfSanguineMomentum) };

	// HOLDING NOTHING IS WORTH NOTHING, which is where every character starts a
	// fight and is what makes the node a reward for keeping the chain going.
	TestEqual(TEXT("no stacks is worth nothing"),
		FPipeline::Evaluate(100.0f, Momentum, NoTags, Holding(0, 0, 0)).Final,
		100.0f, 0.01f);

	// AND A STACK IS A STEP.
	TestEqual(TEXT("three stacks is +18%"),
		FPipeline::Evaluate(100.0f, Momentum, NoTags, Holding(3, 0, 0)).Final,
		118.0f, 0.01f);
	TestEqual(TEXT("and the node's five-stack cap is +30%"),
		FPipeline::Evaluate(100.0f, Momentum, NoTags, Holding(5, 0, 0)).Final,
		130.0f, 0.01f);

	// A CALLER THAT KNOWS NOTHING ABOUT THE CHARACTER GETS NOTHING, which is the
	// character sheet and every enemy in the game. A default state holds no
	// stacks, so this needs no negative sentinel the way the other scales do.
	TestEqual(TEXT("a caller with no character in hand gets nothing"),
		FPipeline::Evaluate(100.0f, Momentum, NoTags).Final, 100.0f, 0.01f);

	// THE THREE KINDS ARE NOT INTERCHANGEABLE, WHICH IS THE WHOLE REASON THEY
	// ARE THREE SCALES. Each is granted by a different event and lasts a
	// different length of time, so a build that mapped one onto another would
	// hand a node somebody else's stacks and no arithmetic would report it.
	TArray<FCataclysmStatModifier> Bloodlust = {
		IncreasedPerStack(8.0f, 1.0f,
						  ECataclysmStatScale::PerStackOfBloodlust) };
	TArray<FCataclysmStatModifier> Carnage = {
		IncreasedPerStack(3.0f, 1.0f,
						  ECataclysmStatScale::PerStackOfCarnage) };

	// A character holding five Momentum stacks and nothing else.
	const FCataclysmStatConditions MomentumOnly = Holding(5, 0, 0);
	TestEqual(TEXT("a Bloodlust bonus gets nothing from Momentum stacks"),
		FPipeline::Evaluate(100.0f, Bloodlust, NoTags, MomentumOnly).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and a Carnage bonus gets nothing from them either"),
		FPipeline::Evaluate(100.0f, Carnage, NoTags, MomentumOnly).Final,
		100.0f, 0.01f);

	// AND EACH READS ITS OWN, so the three are wired to three different fields
	// rather than all to the first one.
	const FCataclysmStatConditions AllThree = Holding(1, 2, 4);
	TestEqual(TEXT("Momentum reads its own one stack"),
		FPipeline::Evaluate(100.0f, Momentum, NoTags, AllThree).Final,
		106.0f, 0.01f);
	TestEqual(TEXT("Bloodlust reads its own two"),
		FPipeline::Evaluate(100.0f, Bloodlust, NoTags, AllThree).Final,
		116.0f, 0.01f);
	TestEqual(TEXT("and Carnage reads its own four"),
		FPipeline::Evaluate(100.0f, Carnage, NoTags, AllThree).Final,
		112.0f, 0.01f);

	// A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, and is
	// refused outright when the data is checked. The same rule the three scales
	// above follow, asserted here so that adding a scale cannot skip it.
	TArray<FCataclysmStatModifier> NoStep = {
		IncreasedPerStack(10.0f, 0.0f,
						  ECataclysmStatScale::PerStackOfCarnage) };
	TestEqual(TEXT("a step of nothing is worth nothing at every count"),
		FPipeline::Evaluate(100.0f, NoStep, NoTags, Holding(0, 0, 10)).Final,
		100.0f, 0.01f);
	TestTrue(TEXT("and is reported as illegal when the data is checked"),
		!FPipeline::ValidateModifier(NoStep[0]).IsEmpty());
	TestTrue(TEXT("while a real step is not"),
		FPipeline::ValidateModifier(Carnage[0]).IsEmpty());

	// A STACK COUNT IS INDEPENDENT OF EVERY OTHER STATE. A state that knows only
	// the stacks does not know where health is, and a health bonus gets nothing
	// from a full set of stacks.
	TestTrue(TEXT("a state that knows the stacks need not know the health"),
		MomentumOnly.HealthPercent < 0.0f);

	TArray<FCataclysmStatModifier> ByMissing = {
		IncreasedPerHealthMissing(1.0f, 1.0f) };
	TestEqual(TEXT("a missing-health bonus gets nothing from stacks alone"),
		FPipeline::Evaluate(100.0f, ByMissing, NoTags, MomentumOnly).Final,
		100.0f, 0.01f);

	// AND A COUNT ABOVE ANY NODE'S CAP STILL COUNTS. Nothing in the pipeline
	// caps a stack -- the cap is enforced where a stack is granted -- so this
	// pins that the two rules live in different places and that neither has
	// quietly been given the other's job.
	TestEqual(TEXT("twenty Carnage stacks are worth twenty steps here"),
		FPipeline::Evaluate(100.0f, Carnage, NoTags, Holding(0, 0, 20)).Final,
		160.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineSkillCostConditionTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnWhatTheSkillInHandCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineSkillCostConditionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S GRAND TITHE NODE, held at its full six points: "A skill
	// whose health cost is above 10% of your maximum health deals 4% increased
	// damage per point", so +24% for such a skill. Issue #983.
	//
	// WHAT MAKES THIS PREDICATE DIFFERENT FROM EVERY OTHER ONE HERE. The other
	// three ask about the CHARACTER, so one character has one answer at any
	// instant. This asks about the SKILL, so the same character using two skills
	// in the same instant gets two different answers.
	TArray<FCataclysmStatModifier> Modifiers = {
		IncreasedAboveSkillCost(24.0f, 10.0f) };

	// STRICTLY ABOVE, AND THE BOUNDARY IS THE WHOLE POINT. Every other threshold
	// in this file is "at or below" and includes its own number. This one
	// excludes it, because the design writes "above 10%".
	//
	// IT IS REACHABLE RATHER THAN PEDANTIC. The Deeper Cuts node at its full ten
	// points adds exactly 10% of maximum health to every skill, so a character
	// with that node and no skill of its own cost lands precisely here.
	TestEqual(TEXT("a skill costing exactly the threshold gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SkillCosting(10.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and a hair under it gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SkillCosting(9.99f)).Final,
		100.0f, 0.01f);

	TestEqual(TEXT("a hair over it gets the whole bonus"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SkillCosting(10.01f)).Final,
		124.0f, 0.01f);

	// AND IT IS A SWITCH RATHER THAN A SCALE. A skill costing far more than the
	// threshold is worth exactly the same as one barely over it, because the
	// node states one bonus and not a bonus per point of cost.
	TestEqual(TEXT("a skill costing far more is worth no more"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SkillCosting(80.0f)).Final,
		124.0f, 0.01f);

	// A SKILL THAT COST NOTHING IS A REAL ANSWER AND REFUSES, which is every
	// skill in the game for a character with no point in Deeper Cuts.
	TestEqual(TEXT("a skill that cost nothing gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SkillCosting(0.0f)).Final,
		100.0f, 0.01f);

	// AND NO SKILL IN HAND REFUSES TOO, which is the character sheet, an enemy's
	// plain attack and a burning patch of ground. It is a different statement
	// from a skill that cost nothing, and both answer no.
	TestEqual(TEXT("a caller with no skill in hand gets nothing"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("and one that says outright there is no skill"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, SkillCosting(-1.0f)).Final,
		100.0f, 0.01f);

	// A THRESHOLD OUTSIDE 0 TO 100 IS REPORTED WHEN THE DATA IS CHECKED. A
	// negative one would be beaten by every skill including the free ones,
	// because the comparison is strictly greater than, so the whole node would
	// quietly become an unconditional bonus.
	FCataclysmStatModifier Negative = IncreasedAboveSkillCost(24.0f, -1.0f);
	TestTrue(TEXT("a negative cost threshold is reported as illegal"),
		!FPipeline::ValidateModifier(Negative).IsEmpty());
	TestTrue(TEXT("and one above a hundred per cent"),
		!FPipeline::ValidateModifier(
			IncreasedAboveSkillCost(24.0f, 101.0f)).IsEmpty());
	TestTrue(TEXT("while the design's own ten per cent is not"),
		FPipeline::ValidateModifier(Modifiers[0]).IsEmpty());

	// THE STATE IS INDEPENDENT OF EVERY OTHER READING, which is what makes it a
	// separate field rather than something derived. A blow that knows what its
	// skill cost need not know where the character's health is, and a bonus
	// about health gets nothing from an expensive skill.
	const FCataclysmStatConditions CostOnly = SkillCosting(50.0f);
	TestTrue(TEXT("a state that knows the skill's cost need not know the health"),
		CostOnly.HealthPercent < 0.0f);

	TArray<FCataclysmStatModifier> ByHealth = {
		IncreasedBelowHealth(24.0f, 20.0f) };
	TestEqual(TEXT("a health bonus gets nothing from an expensive skill alone"),
		FPipeline::Evaluate(100.0f, ByHealth, NoTags, CostOnly).Final,
		100.0f, 0.01f);

	// AND THE TWO COMBINE WHEN BOTH ARE KNOWN, each judged on its own reading.
	FCataclysmStatConditions Both = SkillCosting(50.0f);
	Both.HealthPercent = 10.0f;
	TArray<FCataclysmStatModifier> Pair = {
		IncreasedAboveSkillCost(24.0f, 10.0f), IncreasedBelowHealth(24.0f, 20.0f) };
	TestEqual(TEXT("both apply and sum into one increases bracket"),
		FPipeline::Evaluate(100.0f, Pair, NoTags, Both).Final, 148.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineConditionEdgesTest,
	"Cataclysm.StatPipeline.AnUnknownConditionRefusesAndABadThresholdIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineConditionEdgesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// A HEALTH PERCENTAGE IS BUILT FROM TWO NUMBERS AND EITHER CAN BE ABSENT.
	// A maximum health of zero cannot happen on a live character -- the vital
	// attribute set floors it at one -- but an attribute set that has not been
	// written yet reports zero, and dividing by it would be worse than knowing
	// nothing.
	const FCataclysmStatConditions Unknown =
		FCataclysmStatConditions::FromHealth(50.0f, 0.0f);
	TestTrue(TEXT("no maximum health means nothing is known"),
		Unknown.HealthPercent < 0.0f);

	TestFalse(TEXT("and an unknown state refuses a health condition"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrBelowPercent, 50.0f, Unknown));

	// NO CONDITION ALWAYS HOLDS, INCLUDING FOR AN UNKNOWN STATE. Every modifier
	// in the game before issue #959 is this one, so this is the case that says
	// nothing that existed already was changed.
	TestTrue(TEXT("a modifier with no condition applies whatever is known"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::Always, 0.0f, Unknown));

	// HEALTH ABOVE THE MAXIMUM IS HELD AT 100 rather than reported as more,
	// because a share of maximum health above the maximum is not a state any
	// condition should be judged against differently from full health.
	const FCataclysmStatConditions Overfull =
		FCataclysmStatConditions::FromHealth(500.0f, 100.0f);
	TestEqual(TEXT("health above maximum reads as full"),
		Overfull.HealthPercent, 100.0f, 0.01f);

	// A THRESHOLD OUTSIDE 0 TO 100 IS REPORTED BY THE VALIDATOR, which is what
	// data import reads. Evaluate cannot refuse anything at run time, so this is
	// the only place a bad row can be caught in the engine.
	FCataclysmStatModifier TooHigh = IncreasedBelowHealth(10.0f, 150.0f);
	TestTrue(TEXT("a threshold above 100 is reported"),
		FPipeline::ValidateModifier(TooHigh).Contains(TEXT("health threshold")));

	FCataclysmStatModifier Negative = IncreasedBelowHealth(10.0f, -5.0f);
	TestTrue(TEXT("and one below zero"),
		FPipeline::ValidateModifier(Negative).Contains(TEXT("health threshold")));

	FCataclysmStatModifier Fine = IncreasedBelowHealth(10.0f, 35.0f);
	TestTrue(TEXT("and a legal one is not"),
		FPipeline::ValidateModifier(Fine).IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Tag scoping
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineScopingTest,
	"Cataclysm.StatPipeline.IncreasesAreScopedByTheSkillInHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineScopingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// Pinned to sim/cataclysm_sim/character.py: area of effect baselines at 100
	// because it is a percentage of whatever the skill does, and a 40% increase
	// scoped to Type.AOE takes an area skill to 140 and leaves a single-target
	// skill at 100.
	TArray<FCataclysmStatModifier> Modifiers = { Increased(40.0f, TEXT("Type.AOE")) };

	const FGameplayTagContainer AreaSkill = Tags({ TEXT("Type.AOE.PointBlank") });
	const FGameplayTagContainer SingleTarget = Tags({ TEXT("Type.Strike") });

	TestTrue(TEXT("an area skill gets the increase, reaching 140"),
		FMath::IsNearlyEqual(FPipeline::Evaluate(100.0f, Modifiers, AreaSkill).Final,
							 140.0f, 0.01f));
	TestTrue(TEXT("a single-target skill does not, staying at 100"),
		FMath::IsNearlyEqual(FPipeline::Evaluate(100.0f, Modifiers, SingleTarget).Final,
							 100.0f, 0.01f));

	// The hierarchy is the point of the dotted names: a requirement of Type.AOE
	// is satisfied by the more specific Type.AOE.PointBlank, so an affix does
	// not have to enumerate every sub-kind of area skill that exists.
	TestTrue(TEXT("Type.AOE.PointBlank satisfies a requirement of Type.AOE"),
		FPipeline::ModifierApplies(Modifiers[0], AreaSkill));

	// And not the other way round. A modifier that requires the specific tag is
	// not satisfied by the general one, or scoping would be meaningless.
	TArray<FCataclysmStatModifier> Specific = {
		Increased(40.0f, TEXT("Type.AOE.PointBlank")) };
	TestFalse(TEXT("Type.AOE does not satisfy a requirement of Type.AOE.PointBlank"),
		FPipeline::ModifierApplies(Specific[0], Tags({ TEXT("Type.AOE") })));

	// Scope.Global is the design's way of saying "everything".
	TArray<FCataclysmStatModifier> Global = { Increased(40.0f, TEXT("Scope.Global")) };
	TestTrue(TEXT("Scope.Global applies to a skill with no tags at all"),
		FPipeline::ModifierApplies(Global[0], NoTags));
	TestTrue(TEXT("Scope.Global applies to a single-target skill"),
		FPipeline::ModifierApplies(Global[0], SingleTarget));

	// An unscoped modifier applies to everything as well.
	TestTrue(TEXT("a modifier requiring nothing applies to everything"),
		FPipeline::ModifierApplies(Increased(40.0f), SingleTarget));

	// Every required tag must be matched, not just one of them.
	FCataclysmStatModifier Both = Increased(40.0f, TEXT("Type.AOE"));
	Both.RequiredTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false));
	TestFalse(TEXT("matching one of two required tags is not enough"),
		FPipeline::ModifierApplies(Both, AreaSkill));
	TestTrue(TEXT("matching both required tags is"),
		FPipeline::ModifierApplies(Both, Tags({ TEXT("Type.AOE"), TEXT("Type.Melee") })));

	return true;
}

// ---------------------------------------------------------------------------
// The rules the engine has no opinion on
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineMoreSourceTest,
	"Cataclysm.StatPipeline.OnlySomeSourcesMayGrantAMoreMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineMoreSourceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	TestTrue(TEXT("a gem may grant more"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::Gem));
	TestTrue(TEXT("a passive keystone may grant more"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::PassiveKeystone));
	TestTrue(TEXT("an enchantment may grant more"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::Enchantment));

	TestFalse(TEXT("a gear affix may not"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::GearAffix));
	TestFalse(TEXT("a gear implicit may not"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::GearImplicit));
	TestFalse(TEXT("an attribute point may not"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::Attribute));

	// An affix that claims a more multiplier is ignored rather than clamped:
	// honouring it would break the rule the three-bucket split rests on.
	TArray<FCataclysmStatModifier> Illegal = {
		Make(ECataclysmStatBucket::More, ECataclysmModifierSource::GearAffix, 50.0f) };

	AddExpectedError(TEXT("ignored a More multiplier"),
		EAutomationExpectedErrorFlags::Contains, 1);

	const FCataclysmStatBreakdown Result = FPipeline::Evaluate(100.0f, Illegal, NoTags);
	TestTrue(TEXT("the stat is unchanged at 100"),
		FMath::IsNearlyEqual(Result.Final, 100.0f, 0.01f));
	TestEqual(TEXT("no more source applied"), Result.MoreSourceCount, 0);
	TestEqual(TEXT("and the refusal was counted"), Result.RejectedMoreCount, 1);

	// Data import gets a reason it can print, rather than silence.
	TestFalse(TEXT("validation reports the illegal source"),
		FPipeline::ValidateModifier(Illegal[0]).IsEmpty());
	TestTrue(TEXT("a legal modifier validates clean"),
		FPipeline::ValidateModifier(MoreFromGem(50.0f)).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineLessFloorTest,
	"Cataclysm.StatPipeline.ALessMultiplierCannotZeroOrInvertAStat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineLessFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// A "less" multiplier is an ordinary more multiplier with a negative value,
	// and it is legal. What is not legal is one reaching -100%, because a single
	// source could then zero a stat outright or turn it negative.
	TArray<FCataclysmStatModifier> Less = { MoreFromGem(-40.0f) };
	TestTrue(TEXT("a 40% less multiplier takes 100 to 60"),
		FMath::IsNearlyEqual(FPipeline::Evaluate(100.0f, Less, NoTags).Final,
							 60.0f, 0.01f));

	AddExpectedError(TEXT("clamped a Less multiplier"),
		EAutomationExpectedErrorFlags::Contains, 2);

	// Exactly -100% would zero it.
	TArray<FCataclysmStatModifier> Zeroing = { MoreFromGem(-100.0f) };
	const FCataclysmStatBreakdown AtZero = FPipeline::Evaluate(100.0f, Zeroing, NoTags);
	TestTrue(TEXT("a -100% less multiplier does not reach zero"), AtZero.Final > 0.0f);
	TestEqual(TEXT("and the clamp was counted"), AtZero.ClampedLessCount, 1);

	// Worse than -100% would invert it.
	TArray<FCataclysmStatModifier> Inverting = { MoreFromGem(-250.0f) };
	const FCataclysmStatBreakdown Inverted =
		FPipeline::Evaluate(100.0f, Inverting, NoTags);
	TestTrue(TEXT("a -250% less multiplier does not turn the stat negative"),
		Inverted.Final > 0.0f);

	// Stacking several never gets there either, which is the property that
	// matters: the floor is on each source, and a product of positive factors
	// is positive however many there are.
	TArray<FCataclysmStatModifier> Many;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Many.Add(MoreFromGem(-90.0f));
	}
	TestTrue(TEXT("twenty 90% less multipliers still leave the stat above zero"),
		FPipeline::Evaluate(100.0f, Many, NoTags).Final > 0.0f);

	// Validation refuses it outright, which is what data import should do
	// instead of relying on the runtime clamp.
	TestFalse(TEXT("validation reports a -100% more multiplier"),
		FPipeline::ValidateModifier(MoreFromGem(-100.0f)).IsEmpty());
	TestTrue(TEXT("a -99% more multiplier is legal"),
		FPipeline::ValidateModifier(MoreFromGem(-99.0f)).IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Rates
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineRateTest,
	"Cataclysm.StatPipeline.ARateDividesByBothBucketsAndNeverReachesZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineRateTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// Pinned to sim/cataclysm_sim/character.py: a 4 second cooldown with
	// 33.333% of increases and one 20% more gem comes out at 2.5 seconds, and
	// the character is shown 37.5%.
	TArray<FCataclysmStatModifier> Modifiers = {
		Increased(100.0f / 3.0f),
		MoreFromGem(20.0f),
	};

	const FCataclysmStatBreakdown Result =
		FPipeline::EvaluateRate(4.0f, Modifiers, NoTags);

	TestTrue(FString::Printf(TEXT("a 4 second cooldown becomes 2.5, got %.4f"),
			 Result.Final),
		FMath::IsNearlyEqual(Result.Final, 2.5f, 0.001f));
	TestTrue(FString::Printf(TEXT("the player is shown 37.5%%, got %.4f"),
			 FPipeline::DisplayedRateReduction(Result)),
		FMath::IsNearlyEqual(FPipeline::DisplayedRateReduction(Result), 37.5f, 0.01f));

	// A more multiplier on a rate must make the interval SHORTER. If it
	// multiplied the way it does for a quantity, a cooldown reduction gem would
	// lengthen the cooldown, which is the bug this rule exists to prevent.
	TArray<FCataclysmStatModifier> GemOnly = { MoreFromGem(20.0f) };
	const float WithGem = FPipeline::EvaluateRate(4.0f, GemOnly, NoTags).Final;
	TestTrue(FString::Printf(TEXT("a 20%% more gem shortens 4s, got %.4f"), WithGem),
		WithGem < 4.0f);

	// However much is stacked, division cannot reach zero. That is why the stat
	// needs no cap, and it is checked rather than asserted.
	TArray<FCataclysmStatModifier> Enormous = { Increased(100000.0f) };
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Enormous.Add(MoreFromGem(200.0f));
	}
	const FCataclysmStatBreakdown Extreme =
		FPipeline::EvaluateRate(4.0f, Enormous, NoTags);
	TestTrue(TEXT("an absurd build still has a cooldown above zero"),
		Extreme.Final > 0.0f);

	// The DISPLAYED figure is a different claim from the mechanical one, and it
	// has a limit the cooldown itself does not.
	//
	// Displayed reduction is (divisor - 1) / divisor. Once the divisor passes
	// about 8.4 million, that expression rounds to exactly 1.0 in single
	// precision and the player is shown 100% while the cooldown is still above
	// zero. Double precision moves the threshold but does not remove it. The
	// case above has a divisor near 2x10^17 and does display 100%.
	//
	// This is not reachable in a real build. Getting there needs roughly 34
	// compounding 50% sources on one stat, against six gem sockets. So the
	// mechanical property is asserted at the absurd magnitude and the display is
	// asserted at a large but attainable one: +100,000 percentage points of
	// increases is a divisor of 1,001 and shows 99.9%.
	TArray<FCataclysmStatModifier> LargeButRepresentable = { Increased(100000.0f) };
	const FCataclysmStatBreakdown Large =
		FPipeline::EvaluateRate(4.0f, LargeButRepresentable, NoTags);
	TestTrue(TEXT("a very large build still has a cooldown above zero"),
		Large.Final > 0.0f);
	TestTrue(FString::Printf(TEXT("and is shown a reduction below 100%%, got %.4f"),
			 FPipeline::DisplayedRateReduction(Large)),
		FPipeline::DisplayedRateReduction(Large) < 100.0f);

	// The attribute set's own cooldown helpers use the same divisor, so the two
	// cannot disagree. Its increases are fractions, not percentage points.
	using FCombat = UCataclysmCombatAttributeSet;
	TestTrue(TEXT("the attribute set agrees: 4s at +1/3 with a 1.2 more is 2.5s"),
		FMath::IsNearlyEqual(FCombat::FinalCooldown(4.0f, 1.0f / 3.0f, 1.2f),
							 2.5f, 0.001f));
	TestTrue(TEXT("the attribute set shows 37.5% for the same character"),
		FMath::IsNearlyEqual(FCombat::DisplayedCooldownReduction(1.0f / 3.0f, 1.2f),
							 37.5f, 0.01f));
	TestTrue(TEXT("with no more sources it is unchanged from before"),
		FMath::IsNearlyEqual(FCombat::FinalCooldown(4.0f, 1.0f / 3.0f), 3.0f, 0.001f));

	return true;
}

// ---------------------------------------------------------------------------
// The runtime modifier list. Issue #166: the pipeline was a calculator nothing
// fed, so a buff had a duration and no magnitude.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBuffMayGrantMoreTest,
	"Cataclysm.StatPipeline.ASkillsOwnBuffMayGrantAMoreMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBuffMayGrantMoreTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	// THE RULE THIS IS ABOUT. Only some sources may grant a More multiplier,
	// because a rolled gear affix that did would make a drop unreadable. A
	// skill's own buff is authored, the way a gem, a keystone and an
	// enchantment are authored, so it sits with those three.
	TestTrue(TEXT("a skill buff may grant More"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::SkillBuff));
	TestFalse(TEXT("a gear affix still may not"),
		FPipeline::CanGrantMore(ECataclysmModifierSource::GearAffix));

	const FCataclysmStatModifier More = Make(
		ECataclysmStatBucket::More, ECataclysmModifierSource::SkillBuff, 30.0f);
	TestTrue(TEXT("and validation accepts it"),
		FPipeline::ValidateModifier(More).IsEmpty());

	TArray<FCataclysmStatModifier> Modifiers = { More };
	const FCataclysmStatBreakdown Result =
		FPipeline::Evaluate(100.0f, Modifiers, NoTags);
	TestEqual(TEXT("100 with a 30% more from a skill buff is 130"),
		Result.Final, 130.0f);
	TestEqual(TEXT("and it is counted, not rejected"), Result.RejectedMoreCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierListTest,
	"Cataclysm.StatPipeline.TheAbilitySystemHoldsModifiersThatCanBeTakenAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierListTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>();

	TestEqual(TEXT("it starts empty"), AbilitySystem->GetStatModifiers().Num(), 0);

	const int32 First = AbilitySystem->AddStatModifier(Increased(20.0f));
	const int32 Second = AbilitySystem->AddStatModifier(Increased(30.0f));
	TestTrue(TEXT("adding gives a usable handle"), First != 0);
	TestTrue(TEXT("and the second handle differs from the first"), Second != First);
	TestEqual(TEXT("both are held"), AbilitySystem->GetStatModifiers().Num(), 2);

	TestEqual(TEXT("a handle reads back its own value"),
		AbilitySystem->GetStatModifierValue(Second), 30.0f);

	TestTrue(TEXT("a live value can be changed"),
		AbilitySystem->SetStatModifierValue(Second, 45.0f));
	TestEqual(TEXT("and reads back changed"),
		AbilitySystem->GetStatModifierValue(Second), 45.0f);

	// REMOVING THE FIRST MUST NOT DISTURB THE SECOND. The two arrays behind this
	// are kept aligned by index, so a removal that fixed one and not the other
	// would silently give somebody else's modifier the wrong handle.
	TestTrue(TEXT("the first can be removed"),
		AbilitySystem->RemoveStatModifier(First));
	TestEqual(TEXT("one is left"), AbilitySystem->GetStatModifiers().Num(), 1);
	TestEqual(TEXT("and it is still the second one, unchanged"),
		AbilitySystem->GetStatModifierValue(Second), 45.0f);

	TestFalse(TEXT("removing the same handle twice does nothing"),
		AbilitySystem->RemoveStatModifier(First));
	TestFalse(TEXT("an unknown handle removes nothing"),
		AbilitySystem->RemoveStatModifier(9999));
	TestEqual(TEXT("an unknown handle reads as zero"),
		AbilitySystem->GetStatModifierValue(9999), 0.0f);

	// HANDLES ARE NEVER REUSED, so a stale one cannot take away a modifier that
	// happens to have landed in the same place.
	const int32 Third = AbilitySystem->AddStatModifier(Increased(10.0f));
	TestTrue(TEXT("a later handle is not one already handed out"),
		Third != First && Third != Second);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmModifierListRefusesTest,
	"Cataclysm.StatPipeline.TheAbilitySystemRefusesAMoreItIsNotAllowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmModifierListRefusesTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>();

	// Evaluate SKIPS a More from a source that may not grant one and counts it,
	// which is right for gear a player is wearing: a character sheet can then
	// say the modifier is doing nothing. Asking for one in code is a mistake in
	// the code, so this refuses instead, and the invalid handle is what makes
	// the mistake visible where it was made.
	AddExpectedError(TEXT("refused a stat modifier"), EAutomationExpectedErrorFlags::Contains, 1);

	const FCataclysmStatModifier Illegal = Make(
		ECataclysmStatBucket::More, ECataclysmModifierSource::GearAffix, 50.0f);
	TestEqual(TEXT("a More from a gear affix is refused"),
		AbilitySystem->AddStatModifier(Illegal), 0);
	TestEqual(TEXT("and nothing was stored"),
		AbilitySystem->GetStatModifiers().Num(), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
