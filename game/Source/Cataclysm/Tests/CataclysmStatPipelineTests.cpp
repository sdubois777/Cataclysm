// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
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

	/** An increase that applies only while the character is moving. #41 */
	FCataclysmStatModifier IncreasedWhileMoving(float Value)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition = ECataclysmStatCondition::WhileMoving;
		return Modifier;
	}

	/** An increase that applies only while the character is not moving. #41 */
	FCataclysmStatModifier IncreasedWhileStationary(float Value)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition = ECataclysmStatCondition::WhileStationary;
		return Modifier;
	}

	/** An increase that needs the character to have stood still AT LEAST this
	 *  long. #41 */
	FCataclysmStatModifier IncreasedWhenStationaryFor(float Value, float Seconds)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition = ECataclysmStatCondition::StationaryForSeconds;
		Modifier.ConditionValue = Seconds;
		return Modifier;
	}

	/** An increase that needs the character not to have attacked for AT LEAST
	 *  this long. #41 */
	FCataclysmStatModifier IncreasedWhenNotAttackedFor(float Value, float Seconds)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition = ECataclysmStatCondition::NotAttackedForSeconds;
		Modifier.ConditionValue = Seconds;
		return Modifier;
	}

	/** An increase that needs the blow's own snapshot to be AT LEAST this far.
	 *  The Ravager capstone option Headlong's second clause. #41 */
	FCataclysmStatModifier IncreasedAfterMovingMetres(float Value, float Metres)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition = ECataclysmStatCondition::MetresMovedBeforeAttack;
		Modifier.ConditionValue = Metres;
		return Modifier;
	}

	/**
	 * A character that last moved this many seconds ago.
	 *
	 * A NEGATIVE ARGUMENT IS "NO CHARACTER TO READ" and not "never moved", which
	 * is the difference this slice's readings are built around: a character's
	 * clock starts when it spawns, so a freshly spawned one reads 0. The
	 * character sheet, which asks about nobody, is what reads -1.
	 *
	 * ZERO SECONDS AND MOVING ARE THE SAME INSTANT, so this says both: the
	 * sampler sets the stamp and the moved flag together.
	 */
	FCataclysmStatConditions Stationary(float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceMoved = Seconds;
		State.bIsMoving = Seconds == 0.0f;
		return State;
	}

	/** A character that last attacked this many seconds ago. Negative is "no
	 *  character to read". */
	FCataclysmStatConditions SinceOwnAttack(float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceOwnAttack = Seconds;
		return State;
	}

	/** A blow struck after the character had walked this far. Negative is
	 *  "nothing measured a distance for this blow". */
	FCataclysmStatConditions AfterWalking(float Metres)
	{
		FCataclysmStatConditions State;
		State.MetresMovedBeforeBlow = Metres;
		return State;
	}

	/** A blow from an attack that struck this many enemies together. Negative
	 *  is "no attack in hand". Issue #1515. */
	FCataclysmStatConditions StruckTogether(int32 Enemies)
	{
		FCataclysmStatConditions State;
		State.EnemiesStruckTogether = Enemies;
		return State;
	}

	/** A blow landed from this many metres away. Negative is not known. */
	FCataclysmStatConditions StruckFrom(float Metres)
	{
		FCataclysmStatConditions State;
		State.Blow.OpponentDistanceMetres = Metres;
		return State;
	}

	/**
	 * A target standing this many metres away. Negative is not known.
	 *
	 * THE OTHER END OF THE SAME BLOW FROM `StruckFrom` ABOVE, and a separate
	 * field on purpose. That one is filled only on the defender's damage taken
	 * lookup; this one only on the attacker's own lookups. Issue #1596.
	 */
	FCataclysmStatConditions TargetAt(float Metres)
	{
		FCataclysmStatConditions State;
		State.TargetDistanceMetres = Metres;
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

	/** An increase that applies only while the character is Bleeding. #962. */
	FCataclysmStatModifier IncreasedWhileBleeding(float Value)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Condition = ECataclysmStatCondition::WhileBleeding;
		return Modifier;
	}

	/** An increase worth `Value` per whole `Step` debuffs carried. #962. */
	FCataclysmStatModifier IncreasedPerDebuff(float Value, float Step)
	{
		FCataclysmStatModifier Modifier = Increased(Value);
		Modifier.Scale = ECataclysmStatScale::PerDebuffCarried;
		Modifier.ScaleStep = Step;
		return Modifier;
	}

	/**
	 * A character under this many debuffs, one of which may be Bleeding.
	 *
	 * THE TWO ARE SET SEPARATELY BECAUSE NEITHER IMPLIES THE OTHER, which is
	 * what the tests below check. A stunned character carries one debuff and is
	 * not Bleeding.
	 *
	 * NO NEGATIVE "UNKNOWN", for the reason `Holding` above gives about stacks.
	 */
	FCataclysmStatConditions Carrying(int32 Debuffs, bool bBleeding = false)
	{
		FCataclysmStatConditions State;
		State.DebuffsCarried = Debuffs;
		State.bIsBleeding = bBleeding;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineMovingTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnWhetherTheCharacterIsMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineMovingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// TWO PREDICATES THAT COMPARE NOTHING, unlike every other one in this file.
	// A Saboteur node gives "+5% of your current evasion chance per point while
	// moving"; a Ritualist node gives spell damage "while you have not moved".
	// Issue #41, slice 2.
	TArray<FCataclysmStatModifier> Moving = { IncreasedWhileMoving(20.0f) };
	TArray<FCataclysmStatModifier> Still = { IncreasedWhileStationary(20.0f) };

	TestEqual(TEXT("a moving character gets the moving bonus"),
		FPipeline::Evaluate(100.0f, Moving, NoTags, Stationary(0.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("and not the stationary one"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(0.0f)).Final,
		100.0f, 0.01f);

	TestEqual(TEXT("a character standing still gets the stationary bonus"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(3.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("and not the moving one"),
		FPipeline::Evaluate(100.0f, Moving, NoTags, Stationary(3.0f)).Final,
		100.0f, 0.01f);

	// THE TWO ARE EXACTLY OPPOSITE FOR A CHARACTER THAT CAN BE READ, which is
	// what Torchlight Infinite states outright: Moving and Standing Still "are
	// mutually exclusive". Said as one assertion so a change that made both
	// true, or neither, fails here.
	for (const float Seconds : {0.0f, 0.25f, 1.0f, 60.0f})
	{
		const FCataclysmStatConditions State = Stationary(Seconds);
		const bool bMovingApplied =
			FPipeline::Evaluate(100.0f, Moving, NoTags, State).Final > 100.0f;
		const bool bStillApplied =
			FPipeline::Evaluate(100.0f, Still, NoTags, State).Final > 100.0f;
		TestNotEqual(
			*FString::Printf(
				TEXT("at %.2f seconds since moving, exactly one of the two holds"),
				Seconds),
			bMovingApplied, bStillApplied);
	}

	// NEITHER APPLIES TO A READING OF -1, AND THAT IS THE WHOLE POINT OF -1. It
	// means "no character to read", not "never moved": a character's clock
	// starts when it spawns. Without this the character sheet would show a
	// stationary bonus as though the character were standing still, and the
	// sheet asks about nobody.
	TestEqual(TEXT("an unknown reading gets no stationary bonus"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(-1.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("nor a moving one"),
		FPipeline::Evaluate(100.0f, Moving, NoTags, Stationary(-1.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and the character sheet, which knows nothing, gets neither"),
		FPipeline::Evaluate(100.0f, Still, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("said for the moving half too"),
		FPipeline::Evaluate(100.0f, Moving, NoTags).Final, 100.0f, 0.01f);

	// AND NEITHER TAKES A THRESHOLD, so neither can be refused for one.
	TestTrue(TEXT("a moving bonus is legal with no threshold"),
		FPipeline::ValidateModifier(IncreasedWhileMoving(20.0f)).IsEmpty());
	TestTrue(TEXT("and a stationary one is"),
		FPipeline::ValidateModifier(IncreasedWhileStationary(20.0f)).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineMovementStepsTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnHowLongStillOrHowFarWalked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineMovementStepsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THREE PREDICATES THAT COMPARE A NUMBER, and all three read AT LEAST.
	// A Bulwark node gives damage reduction "while you have not moved in the
	// last 2 seconds"; its capstone doubles block "while you have not attacked
	// in the last 3 seconds"; the Ravager capstone option Headlong gives "your
	// first melee attack after moving 5 metres" 50% increased damage.
	// Issue #41, slice 2.
	TArray<FCataclysmStatModifier> Still = {
		IncreasedWhenStationaryFor(20.0f, 2.0f) };
	TArray<FCataclysmStatModifier> Quiet = {
		IncreasedWhenNotAttackedFor(30.0f, 3.0f) };
	TArray<FCataclysmStatModifier> Walked = {
		IncreasedAfterMovingMetres(50.0f, 5.0f) };

	// BELOW THE THRESHOLD, NOTHING.
	TestEqual(TEXT("one second still is not two"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(1.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("two seconds quiet is not three"),
		FPipeline::Evaluate(100.0f, Quiet, NoTags, SinceOwnAttack(2.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("four metres is not five"),
		FPipeline::Evaluate(100.0f, Walked, NoTags, AfterWalking(4.0f)).Final,
		100.0f, 0.01f);

	// AT THE THRESHOLD EXACTLY, THE BONUS APPLIES. "At least" is the reading
	// every one of these three uses, and it matches the window predicates above,
	// which include their last instant. Two predicates disagreeing about their
	// own boundaries would be worse than either answer.
	TestEqual(TEXT("exactly two seconds still counts"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(2.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("exactly three seconds quiet counts"),
		FPipeline::Evaluate(100.0f, Quiet, NoTags, SinceOwnAttack(3.0f)).Final,
		130.0f, 0.01f);
	TestEqual(TEXT("exactly five metres counts"),
		FPipeline::Evaluate(100.0f, Walked, NoTags, AfterWalking(5.0f)).Final,
		150.0f, 0.01f);

	// AND ABOVE IT, STILL. Unlike a window, none of these runs out: standing
	// still longer does not stop the character being still.
	TestEqual(TEXT("and a minute still"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(60.0f)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("and a minute quiet"),
		FPipeline::Evaluate(100.0f, Quiet, NoTags, SinceOwnAttack(60.0f)).Final,
		130.0f, 0.01f);
	TestEqual(TEXT("and a hundred metres"),
		FPipeline::Evaluate(100.0f, Walked, NoTags, AfterWalking(100.0f)).Final,
		150.0f, 0.01f);

	// A NEGATIVE READING IS REFUSED BY ALL THREE, because it means no character
	// was read rather than a character that has done nothing.
	TestEqual(TEXT("an unknown standing-still reading is refused"),
		FPipeline::Evaluate(100.0f, Still, NoTags, Stationary(-1.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("an unknown last-attack reading is refused"),
		FPipeline::Evaluate(100.0f, Quiet, NoTags, SinceOwnAttack(-1.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("a blow nothing measured a distance for is refused"),
		FPipeline::Evaluate(100.0f, Walked, NoTags, AfterWalking(-1.0f)).Final,
		100.0f, 0.01f);

	// A DISTANCE OF ZERO IS A REAL ANSWER AND NOT AN UNKNOWN ONE, which is why
	// the unknown value is -1 rather than 0. A blow struck without walking a
	// step measured nothing walked, and a node asking for five metres refuses
	// it, while a node asking for none would be satisfied.
	TArray<FCataclysmStatModifier> AnyDistance = {
		IncreasedAfterMovingMetres(50.0f, 0.0f) };
	TestEqual(TEXT("a blow after no walking is refused by a five-metre node"),
		FPipeline::Evaluate(100.0f, Walked, NoTags, AfterWalking(0.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and satisfies one asking for no distance at all"),
		FPipeline::Evaluate(100.0f, AnyDistance, NoTags, AfterWalking(0.0f)).Final,
		150.0f, 0.01f);

	// THE THREE PREDICATES ARE INDEPENDENT, so a state that knows one and not
	// the others answers each on its own merits rather than returning early.
	FCataclysmStatConditions StillButNeverAttacked = Stationary(5.0f);
	TestEqual(TEXT("standing still is judged when the attack clock is unknown"),
		FPipeline::Evaluate(100.0f, Still, NoTags, StillButNeverAttacked).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("and the attack clock is still refused by that same state"),
		FPipeline::Evaluate(100.0f, Quiet, NoTags, StillButNeverAttacked).Final,
		100.0f, 0.01f);

	// AND A THRESHOLD OF NOTHING IS REFUSED WHEN THE DATA IS CHECKED rather
	// than quietly granting the bonus to everybody. `ValidateModifier` is what
	// data import calls. All three valued predicates are checked, because a
	// bound added for one and forgotten for the others is the likely mistake.
	TestTrue(TEXT("zero seconds still is reported as illegal"),
		!FPipeline::ValidateModifier(
			IncreasedWhenStationaryFor(20.0f, 0.0f)).IsEmpty());
	TestTrue(TEXT("and a negative one"),
		!FPipeline::ValidateModifier(
			IncreasedWhenStationaryFor(20.0f, -2.0f)).IsEmpty());
	TestTrue(TEXT("zero seconds quiet is reported as illegal"),
		!FPipeline::ValidateModifier(
			IncreasedWhenNotAttackedFor(30.0f, 0.0f)).IsEmpty());
	TestTrue(TEXT("and a negative one"),
		!FPipeline::ValidateModifier(
			IncreasedWhenNotAttackedFor(30.0f, -3.0f)).IsEmpty());
	TestTrue(TEXT("zero metres walked is reported as illegal"),
		!FPipeline::ValidateModifier(
			IncreasedAfterMovingMetres(50.0f, 0.0f)).IsEmpty());
	TestTrue(TEXT("and a negative one"),
		!FPipeline::ValidateModifier(
			IncreasedAfterMovingMetres(50.0f, -5.0f)).IsEmpty());

	TestTrue(TEXT("real thresholds are not"),
		FPipeline::ValidateModifier(
			IncreasedWhenStationaryFor(20.0f, 2.0f)).IsEmpty()
		&& FPipeline::ValidateModifier(
			IncreasedWhenNotAttackedFor(30.0f, 3.0f)).IsEmpty()
		&& FPipeline::ValidateModifier(
			IncreasedAfterMovingMetres(50.0f, 5.0f)).IsEmpty());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineDebuffScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithTheDebuffsTheCharacterCarries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineDebuffScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S BATTLE-SCARRED NODE at its full eight points: "+2%
	// increased Armor per point for each unique debuff on you", so sixteen
	// percentage points a debuff. Issue #962.
	TArray<FCataclysmStatModifier> Scarred = { IncreasedPerDebuff(16.0f, 1.0f) };

	// CARRYING NOTHING IS WORTH NOTHING, which is where every character starts
	// and is what makes the node a reward for being hurt rather than a flat
	// bonus. This is the direction that matters: a Masochist standing untouched
	// must not be walking around with the armour of one covered in ailments.
	TestEqual(TEXT("no debuffs is worth nothing"),
		FPipeline::Evaluate(100.0f, Scarred, NoTags, Carrying(0)).Final,
		100.0f, 0.01f);

	// AND EACH DEBUFF IS A STEP.
	TestEqual(TEXT("one debuff is +16%"),
		FPipeline::Evaluate(100.0f, Scarred, NoTags, Carrying(1)).Final,
		116.0f, 0.01f);
	TestEqual(TEXT("three debuffs is +48%"),
		FPipeline::Evaluate(100.0f, Scarred, NoTags, Carrying(3)).Final,
		148.0f, 0.01f);

	// A CALLER THAT KNOWS NOTHING ABOUT THE CHARACTER GETS NOTHING, which is the
	// character sheet and every enemy in the game. A default state carries no
	// debuffs, so this needs no negative sentinel the way the health readings
	// do. This is also what keeps a scaling row out of the gameplay attribute:
	// `UCataclysmPlayerClassStats::ApplyTo` evaluates with exactly this state.
	TestEqual(TEXT("a caller with no character in hand gets nothing"),
		FPipeline::Evaluate(100.0f, Scarred, NoTags).Final, 100.0f, 0.01f);

	// IT IS NOT A STACK COUNT AND MUST NOT READ ONE. A character holding five of
	// every kind of stack and carrying no debuff is worth nothing to this, and a
	// character carrying five debuffs and holding no stack is worth nothing to
	// them. Nothing at run time would report a scale wired to the wrong field.
	TArray<FCataclysmStatModifier> Momentum = {
		IncreasedPerStack(10.0f, 1.0f,
						  ECataclysmStatScale::PerStackOfSanguineMomentum) };
	TestEqual(TEXT("a debuff bonus gets nothing from a full set of stacks"),
		FPipeline::Evaluate(100.0f, Scarred, NoTags, Holding(5, 5, 5)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and a stack bonus gets nothing from five debuffs"),
		FPipeline::Evaluate(100.0f, Momentum, NoTags, Carrying(5)).Final,
		100.0f, 0.01f);

	// A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, and is
	// refused outright when the data is checked. The same rule every other scale
	// follows, asserted here so that adding a scale cannot skip it.
	TArray<FCataclysmStatModifier> NoStep = { IncreasedPerDebuff(10.0f, 0.0f) };
	TestEqual(TEXT("a step of nothing is worth nothing at every count"),
		FPipeline::Evaluate(100.0f, NoStep, NoTags, Carrying(8)).Final,
		100.0f, 0.01f);
	TestTrue(TEXT("and is reported as illegal when the data is checked"),
		!FPipeline::ValidateModifier(NoStep[0]).IsEmpty());
	TestTrue(TEXT("while a real step is not"),
		FPipeline::ValidateModifier(Scarred[0]).IsEmpty());

	// THE `more` BUCKET TOO, WHICH IS WHERE DOCTRINE OF PAIN SITS: "You deal 4%
	// more damage for each unique debuff on you." Three debuffs is 1.04 cubed
	// only if each is a separate multiplier; it is one multiplier of 1.12,
	// because the scale grows the modifier's VALUE and the bucket then applies
	// it once. Pinning the number is what says which of the two this is.
	FCataclysmStatModifier Doctrine = IncreasedPerDebuff(4.0f, 1.0f);
	Doctrine.Bucket = ECataclysmStatBucket::More;
	Doctrine.Source = ECataclysmModifierSource::PassiveKeystone;
	TArray<FCataclysmStatModifier> Multiplying = { Doctrine };
	TestEqual(TEXT("three debuffs multiply once by 1.12, not three times"),
		FPipeline::Evaluate(100.0f, Multiplying, NoTags, Carrying(3)).Final,
		112.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineBleedingConditionTest,
	"Cataclysm.StatPipeline.AnIncreaseCanDependOnTheCharacterBleeding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPipelineBleedingConditionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// THE MASOCHIST'S THIRST FOR PAIN NODE at its full eight points: "While you
	// are Bleeding, +2% increased Attack Speed per point". Issue #962.
	TArray<FCataclysmStatModifier> Thirst = { IncreasedWhileBleeding(16.0f) };

	TestEqual(TEXT("a character that is not bleeding gets nothing"),
		FPipeline::Evaluate(100.0f, Thirst, NoTags, Carrying(0)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and one that is bleeding gets the whole bonus"),
		FPipeline::Evaluate(100.0f, Thirst, NoTags, Carrying(1, true)).Final,
		116.0f, 0.01f);

	// CARRYING A DEBUFF IS NOT THE SAME AS BLEEDING, WHICH IS THE WHOLE REASON
	// THE STATE HOLDS TWO READINGS. A stunned character carries one debuff and
	// is not Bleeding, and the node says Bleeding. A build that answered this
	// condition from the count would hand the bonus to a character the sentence
	// never promises it to, and no arithmetic would report it.
	TestEqual(TEXT("three debuffs and none of them Bleeding is still nothing"),
		FPipeline::Evaluate(100.0f, Thirst, NoTags, Carrying(3)).Final,
		100.0f, 0.01f);

	// AND BLEEDING IS NOT THE SAME AS CARRYING A DEBUFF, the other direction. A
	// scaling bonus must not read the Bleeding flag as a count of one.
	TArray<FCataclysmStatModifier> Scarred = { IncreasedPerDebuff(16.0f, 1.0f) };
	TestEqual(TEXT("a bleeding character with no counted debuff scales to none"),
		FPipeline::Evaluate(100.0f, Scarred, NoTags, Carrying(0, true)).Final,
		100.0f, 0.01f);

	// A CALLER WITH NO CHARACTER IN HAND REFUSES, the same as every other
	// condition. That is what keeps a conditional row out of the gameplay
	// attribute: `UCataclysmPlayerClassStats::ApplyTo` evaluates with a default
	// state, so the attribute holds the unconditional answer and only
	// `StatForSkill` can see this bonus.
	TestEqual(TEXT("a caller with no character in hand gets nothing"),
		FPipeline::Evaluate(100.0f, Thirst, NoTags).Final, 100.0f, 0.01f);

	// THE CONDITION READS NO VALUE, so a modifier carrying one behaves exactly
	// as one that does not. The data check refuses a value beside this
	// condition; this pins that the engine ignores one if it ever arrives.
	FCataclysmStatModifier WithAStrayValue = IncreasedWhileBleeding(16.0f);
	WithAStrayValue.ConditionValue = 99.0f;
	TArray<FCataclysmStatModifier> Stray = { WithAStrayValue };
	TestEqual(TEXT("a stray condition value changes nothing when bleeding"),
		FPipeline::Evaluate(100.0f, Stray, NoTags, Carrying(1, true)).Final,
		116.0f, 0.01f);
	TestEqual(TEXT("and changes nothing when not bleeding"),
		FPipeline::Evaluate(100.0f, Stray, NoTags, Carrying(0)).Final,
		100.0f, 0.01f);

	// A CONDITION AND A SCALE ARE TWO AXES AND A ROW MAY CARRY BOTH, which no
	// authored row does today. The condition decides whether the modifier is in
	// the sum at all and the scale decides how large it is when it is.
	FCataclysmStatModifier BothAxes = IncreasedPerDebuff(10.0f, 1.0f);
	BothAxes.Condition = ECataclysmStatCondition::WhileBleeding;
	TArray<FCataclysmStatModifier> Both = { BothAxes };
	TestEqual(TEXT("both axes: not bleeding is nothing whatever the count"),
		FPipeline::Evaluate(100.0f, Both, NoTags, Carrying(3)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and bleeding with three debuffs is three steps"),
		FPipeline::Evaluate(100.0f, Both, NoTags, Carrying(3, true)).Final,
		130.0f, 0.01f);

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
	// IT IS REACHABLE RATHER THAN PEDANTIC, AND WHAT REACHES IT HAS CHANGED.
	// The Deeper Cuts node at its full ten points added exactly 10% of maximum
	// health to every skill, so a character with that node and no cost of its
	// own landed precisely here. Issue #1107 lowered that node to 0.25% a point,
	// which is 2.5% over ten.
	//
	// EXSANGUINATE IS WHAT LANDS ON THE BOUNDARY NOW, and it does so by being
	// hurt rather than by spending a point. It charges 15% of CURRENT health, so
	// a character at exactly two thirds of its pool is paying 10% of its
	// MAXIMUM, which is this line. A character walks across this threshold as it
	// takes damage.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineDistanceThresholdTest,
	"Cataclysm.StatPipeline.ADistanceThresholdIsStrictAndAnUnknownDistanceRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * How far away the character that struck stood, as a predicate.
 *
 * STANDING APART IS THE NODE, the Ritualist's 100-point capstone third option:
 * "You take 25% less damage from enemies more than 6 metres away from you."
 *
 * STRICTLY MORE THAN, BECAUSE THE NODE WRITES "more than", so a character at
 * exactly 6 metres takes full damage. That is the same boundary
 * `SkillHealthCostAbovePercent` and `HealthBelowPercent` draw, and the reason is
 * the same: a sentence saying "more than" and a predicate saying "at least"
 * differ at exactly one distance, which is the distance a player will stand at.
 *
 * ZERO IS A REAL READING HERE AND THAT MAKES IT UNLIKE EVERY PREDICATE ABOVE.
 * Two characters can stand on the same spot, so zero cannot also mean "no
 * answer", which is why the unknown value is -1 and why the guard is written out
 * rather than folded into the comparison. A threshold of -2 with a folded guard
 * would let an unknown distance pass.
 */
bool FCataclysmPipelineDistanceThresholdTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	const ECataclysmStatCondition Beyond =
		ECataclysmStatCondition::OpponentBeyondMetres;

	// THE BOUNDARY, IN THREE LINES. Six metres is the node's own threshold.
	TestFalse(TEXT("a character struck from exactly six metres is not beyond six"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(6.0f)));
	TestTrue(TEXT("and one struck from a hair further is"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(6.1f)));
	TestFalse(TEXT("and one struck from a hair nearer is not"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(5.9f)));

	// WELL INSIDE AND WELL OUTSIDE, so the test says the predicate is a
	// comparison rather than something that happens to answer at the boundary.
	TestTrue(TEXT("a blow from twenty metres is beyond six"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(20.0f)));
	TestFalse(TEXT("and a blow from arm's reach is not"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(1.0f)));

	// ZERO IS A REAL DISTANCE, NOT AN ABSENT ONE. Two characters standing on one
	// spot are nought metres apart, and the predicate must answer "no, that is
	// not beyond six" rather than refuse as though it knew nothing.
	TestFalse(TEXT("nought metres is a real reading and is not beyond six"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(0.0f)));
	TestTrue(TEXT("and nought metres IS beyond a threshold below it"),
		FPipeline::ConditionHolds(Beyond, -1.0f, StruckFrom(0.0f)));

	// AN UNKNOWN DISTANCE REFUSES, WHATEVER THE THRESHOLD. -1 is what a caller
	// with no blow in hand carries -- the character sheet, every lookup that is
	// not the damage taken step -- and what a damage over time tick carries
	// deliberately. `docs/DECISIONS.md` records that judgement.
	//
	// THE SECOND LINE IS THE ONE THAT MATTERS. A threshold below zero is
	// nonsense a sheet cannot write, but if the guard were folded into the
	// comparison rather than written out, -1 would be "beyond -2" and an unknown
	// distance would pass. This is the case that catches that mistake.
	TestFalse(TEXT("an unknown distance refuses an ordinary threshold"),
		FPipeline::ConditionHolds(Beyond, 6.0f, StruckFrom(-1.0f)));
	TestFalse(TEXT("and refuses a threshold below it, which a folded guard would not"),
		FPipeline::ConditionHolds(Beyond, -2.0f, StruckFrom(-1.0f)));

	// AND A CALLER THAT BUILT NO BLOW AT ALL REFUSES, which is the default the
	// struct carries and what every lookup but the damage taken step passes.
	TestFalse(TEXT("a state with no blow in it refuses"),
		FPipeline::ConditionHolds(Beyond, 6.0f, FCataclysmStatConditions()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineTargetWithinTest,
	"Cataclysm.StatPipeline.ATargetAtTheThresholdIsWithinItAndTheTwoDistancesDoNotCross",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * How far away the character being HIT stood, as a predicate. Issue #1596.
 *
 * THE TWO ROWS: Brute's Heart's 2-piece bonus, "You gain 25% increased damage
 * against enemies that are within 5 meters of you", and Demon King's Regalia's,
 * which says the same with "more" in place of "increased".
 *
 * AT OR WITHIN, BECAUSE BOTH ROWS WRITE "within 5 meters", so a target standing
 * at exactly 5 metres earns the bonus. THAT IS THE OPPOSITE BOUNDARY FROM THE
 * TEST ABOVE, whose node writes "more than", and the two are tested next to each
 * other on purpose: the pair is the whole reason the project keeps
 * `HealthAtOrBelowPercent` and `HealthBelowPercent` as separate predicates.
 *
 * THE FOLDED-GUARD CASE IS FAR MORE DANGEROUS HERE THAN ABOVE, and that is the
 * case worth writing this test for. For "beyond", folding the unknown guard into
 * the comparison hides the fault until a sheet writes a negative threshold. For
 * "within", -1 is at or within EVERY threshold a sheet may write, so a folded
 * guard would make every distance-conditioned row hold on every blow that knew
 * nothing -- which is most blows in the game.
 *
 * AND THE LAST PART CHECKS THE TWO READINGS DO NOT CROSS. They are separate
 * fields filled by separate routes, so a row using the wrong one of the two
 * conditions must grant nothing rather than read a plausible number from the
 * wrong end of the blow.
 */
bool FCataclysmPipelineTargetWithinTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	const ECataclysmStatCondition Within =
		ECataclysmStatCondition::TargetWithinMetres;
	const ECataclysmStatCondition Beyond =
		ECataclysmStatCondition::OpponentBeyondMetres;

	// THE BOUNDARY, IN THREE LINES. Five metres is what both rows write.
	TestTrue(TEXT("a target at exactly five metres IS within five"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(5.0f)));
	TestFalse(TEXT("and one a hair further is not"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(5.1f)));
	TestTrue(TEXT("and one a hair nearer is"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(4.9f)));

	// WELL INSIDE AND WELL OUTSIDE, so the test says this is a comparison rather
	// than something that happens to answer at the boundary.
	TestTrue(TEXT("a target at arm's reach is within five"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(1.0f)));
	TestFalse(TEXT("and one across the room is not"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(20.0f)));

	// ZERO IS A REAL DISTANCE, NOT AN ABSENT ONE, for the reason the test above
	// gives: two characters can stand on one spot.
	TestTrue(TEXT("nought metres is a real reading and is within five"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(0.0f)));
	TestFalse(TEXT("and nought metres is NOT within a threshold below it"),
		FPipeline::ConditionHolds(Within, -1.0f, TargetAt(0.0f)));

	// AN UNKNOWN DISTANCE REFUSES, AND THIS IS THE CASE THAT MATTERS MOST. A
	// folded guard would make -1 "within 5" and every row hold on every blow
	// with no target in hand, which is 32 of the 33 lookups in the game. A
	// minion's blow reports -1 deliberately, so this is also what makes a
	// minion's blow earn nothing.
	TestFalse(TEXT("an unknown distance refuses an ordinary threshold"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(-1.0f)));
	TestFalse(TEXT("and refuses a large one, which a folded guard would pass"),
		FPipeline::ConditionHolds(Within, 100.0f, TargetAt(-1.0f)));
	TestFalse(TEXT("and a state with no target in it refuses"),
		FPipeline::ConditionHolds(Within, 5.0f, FCataclysmStatConditions()));

	// AND THE TWO DISTANCES DO NOT CROSS. A state carrying only the defender's
	// reading must not satisfy the attacker's predicate, and the other way
	// round, whatever the numbers are. This is what makes a row that names the
	// wrong one of the two conditions grant nothing instead of reading the
	// distance from the wrong end of the blow.
	TestFalse(TEXT("the attacker's predicate refuses a state holding only the "
				   "defender's reading"),
		FPipeline::ConditionHolds(Within, 5.0f, StruckFrom(1.0f)));
	TestFalse(TEXT("and the defender's predicate refuses one holding only the "
					"attacker's"),
		FPipeline::ConditionHolds(Beyond, 6.0f, TargetAt(20.0f)));

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
// A removal. Issue #1791.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineRemovalTest,
	"Cataclysm.StatPipeline.ARemovedStatResolvesToZeroWhateverElseReachesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You have no armor" leaves no armour, whatever else the character carries.
 *
 * THE PROJECT OWNER'S MECHANIC, 2026-09-16: "Just multiply the final number of
 * the original formula by 0." So all three buckets are present here, each large
 * enough to be seen in the figure, and the removal still leaves nothing -- which
 * is the difference from the Less multiplier above, floored so it never can.
 *
 * THE BREAKDOWN KEEPS EVERY STEP. Only the finished figure changes, so a
 * character sheet can still show what the stat would have been.
 *
 * AND A REMOVAL IS DECIDED LIKE ANY OTHER MODIFIER. One whose condition does not
 * hold removes nothing, and one from a source that may not grant a More
 * multiplier is ignored and logged.
 */
bool FCataclysmPipelineRemovalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	const FCataclysmStatModifier Removal = Make(
		ECataclysmStatBucket::Removed, ECataclysmModifierSource::Enchantment, 1.0f);

	// EVERY BUCKET, AND EACH ONE MOVES THE FIGURE: 100 base and 50 flat is 150,
	// +100% increased makes 300, and a 50% more multiplier makes 450.
	const TArray<FCataclysmStatModifier> Everything = {
		Flat(50.0f), Increased(100.0f), MoreFromGem(50.0f)};
	const FCataclysmStatBreakdown Kept =
		FPipeline::Evaluate(100.0f, Everything, NoTags);
	TestEqual(TEXT("without a removal the stat is 450"), Kept.Final, 450.0f, 0.01f);
	TestEqual(TEXT("and no removal was counted"), Kept.RemovedCount, 0);

	TArray<FCataclysmStatModifier> WithRemoval = Everything;
	WithRemoval.Add(Removal);
	const FCataclysmStatBreakdown Gone =
		FPipeline::Evaluate(100.0f, WithRemoval, NoTags);
	TestEqual(TEXT("with one the stat is nothing"), Gone.Final, 0.0f, 0.0001f);
	TestEqual(TEXT("the removal was counted"), Gone.RemovedCount, 1);
	TestEqual(TEXT("and the flat step is still in the breakdown"),
		Gone.Flat, 50.0f, 0.01f);
	TestEqual(TEXT("and the increases"), Gone.SumOfIncreases, 100.0f, 0.01f);
	TestEqual(TEXT("and the more multiplier"), Gone.MoreMultiplier, 1.5f, 0.0001f);

	// WHERE IT SITS IN THE LIST DOES NOT MATTER, because the multiplication by
	// nothing is the last step rather than a step taken when the removal is met.
	TArray<FCataclysmStatModifier> RemovalFirst = {Removal};
	RemovalFirst.Append(Everything);
	TestEqual(TEXT("a removal first in the list leaves nothing too"),
		FPipeline::Evaluate(100.0f, RemovalFirst, NoTags).Final, 0.0f, 0.0001f);

	// TWO REMOVALS ARE NOT A DEEPER ZERO. Two sentences removing one stat is
	// what "You no longer regenerate mana" beside "You cannot regenerate mana
	// through any means" does.
	TArray<FCataclysmStatModifier> Twice = WithRemoval;
	Twice.Add(Removal);
	const FCataclysmStatBreakdown TwiceGone =
		FPipeline::Evaluate(100.0f, Twice, NoTags);
	TestEqual(TEXT("two removals leave nothing"), TwiceGone.Final, 0.0f, 0.0001f);
	TestEqual(TEXT("and both were counted"), TwiceGone.RemovedCount, 2);

	// A REMOVAL WITH A CONDITION REMOVES ONLY WHILE IT HOLDS. The condition is
	// judged by `ModifierApplies` before any bucket is chosen, the same as for
	// every other modifier.
	FCataclysmStatModifier WhileLow = Removal;
	WhileLow.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
	WhileLow.ConditionValue = 35.0f;
	TArray<FCataclysmStatModifier> Conditional = Everything;
	Conditional.Add(WhileLow);
	const FCataclysmStatBreakdown Healthy =
		FPipeline::Evaluate(100.0f, Conditional, NoTags, AtHealth(80.0f));
	TestEqual(TEXT("a removal at or below 35% health removes nothing at 80%"),
		Healthy.Final, 450.0f, 0.01f);
	TestEqual(TEXT("and is not counted there"), Healthy.RemovedCount, 0);
	const FCataclysmStatBreakdown Low =
		FPipeline::Evaluate(100.0f, Conditional, NoTags, AtHealth(20.0f));
	TestEqual(TEXT("and removes the stat at 20%"), Low.Final, 0.0f, 0.0001f);
	TestEqual(TEXT("where it is counted"), Low.RemovedCount, 1);

	// A REMOVAL FROM A SOURCE THAT MAY NOT GRANT A MORE MULTIPLIER IS IGNORED.
	// An ordinary affix may take nothing away, for the reason it may multiply
	// nothing.
	AddExpectedError(TEXT("ignored a removal"),
		EAutomationExpectedErrorFlags::Contains, 1);
	const FCataclysmStatModifier FromAffix = Make(
		ECataclysmStatBucket::Removed, ECataclysmModifierSource::GearAffix, 1.0f);
	TArray<FCataclysmStatModifier> AffixRemoval = Everything;
	AffixRemoval.Add(FromAffix);
	const FCataclysmStatBreakdown Refused =
		FPipeline::Evaluate(100.0f, AffixRemoval, NoTags);
	TestEqual(TEXT("a removal from a gear affix leaves the stat at 450"),
		Refused.Final, 450.0f, 0.01f);
	TestEqual(TEXT("and is not counted"), Refused.RemovedCount, 0);
	TestFalse(TEXT("validation reports a removal from a gear affix"),
		FPipeline::ValidateModifier(FromAffix).IsEmpty());
	TestTrue(TEXT("and accepts one from an enchantment"),
		FPipeline::ValidateModifier(Removal).IsEmpty());

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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineStrictHealthThresholdTest,
	"Cataclysm.StatPipeline.BelowAndAtOrBelowDifferAtExactlyTheThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two health threshold predicates. Issue #1051.
 *
 * WHY THERE ARE TWO. Seven nodes across the four trees say "at or below" and
 * take `HealthAtOrBelowPercent`. The Final Vow's first option, The Last Drop,
 * says "While below 20% health", and it is the only node in the game that
 * states a health threshold as a STATE and words it "below". The two rules
 * differ at exactly the threshold and nowhere else, so that one reading is the
 * whole of what this test is for.
 *
 * THE PRECEDENT IS `SkillHealthCostAbovePercent`, which exists for the same
 * reason on the other boundary: "above" genuinely differs from "at or below",
 * and a character sitting precisely on that threshold correctly gets nothing.
 */
bool FCataclysmPipelineStrictHealthThresholdTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	// EXACTLY A FIFTH OF ITS HEALTH. Two hundred out of a thousand.
	const FCataclysmStatConditions Exactly =
		FCataclysmStatConditions::FromHealth(200.0f, 1'000.0f);
	TestEqual(TEXT("the character is on exactly twenty per cent"),
		Exactly.HealthPercent, 20.0f, 0.01f);

	// THE WHOLE DIFFERENCE BETWEEN THE TWO PREDICATES, in two lines.
	TestTrue(TEXT("at or below twenty includes a character on twenty"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrBelowPercent, 20.0f, Exactly));
	TestFalse(TEXT("and below twenty does not"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthBelowPercent, 20.0f, Exactly));

	// AND A HAIR UNDER IT, BOTH HOLD. That is what says the strict one is a
	// boundary rather than being broken outright.
	const FCataclysmStatConditions JustUnder =
		FCataclysmStatConditions::FromHealth(199.0f, 1'000.0f);
	TestTrue(TEXT("at or below twenty holds just under it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrBelowPercent, 20.0f, JustUnder));
	TestTrue(TEXT("and below twenty holds there too"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthBelowPercent, 20.0f, JustUnder));

	// AND ABOVE IT, NEITHER DOES.
	const FCataclysmStatConditions Healthy =
		FCataclysmStatConditions::FromHealth(500.0f, 1'000.0f);
	TestFalse(TEXT("neither holds at half health"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrBelowPercent, 20.0f, Healthy)
		|| FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthBelowPercent, 20.0f, Healthy));

	// AN UNKNOWN STATE REFUSES THE STRICT ONE TOO, and this is the case most
	// easily got wrong. An unknown health reads -1, which IS strictly below
	// every threshold, so a comparison written as `HealthPercent < Value` alone
	// would hand the bonus to the character sheet and to every caller with no
	// character in hand.
	const FCataclysmStatConditions Unknown =
		FCataclysmStatConditions::FromHealth(50.0f, 0.0f);
	TestTrue(TEXT("no maximum health means nothing is known"),
		Unknown.HealthPercent < 0.0f);
	TestFalse(TEXT("and an unknown state refuses the strict threshold"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthBelowPercent, 20.0f, Unknown));

	// AND THE STRICT ONE IS BOUNDED THE SAME WAY BY THE VALIDATOR, which is what
	// data import reads. A threshold outside 0 to 100 is not a percentage of
	// maximum health whichever comparison reads it.
	FCataclysmStatModifier TooHigh;
	TooHigh.Bucket = ECataclysmStatBucket::Flat;
	TooHigh.Source = ECataclysmModifierSource::PassiveKeystone;
	TooHigh.Value = 1.0f;
	TooHigh.Condition = ECataclysmStatCondition::HealthBelowPercent;
	TooHigh.ConditionValue = 150.0f;
	TestTrue(TEXT("a strict threshold above 100 is reported"),
		FPipeline::ValidateModifier(TooHigh).Contains(TEXT("health threshold")));

	FCataclysmStatModifier Fine = TooHigh;
	Fine.ConditionValue = 20.0f;
	TestTrue(TEXT("and a legal one is not"),
		FPipeline::ValidateModifier(Fine).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineHealthAboveTest,
	"Cataclysm.StatPipeline.AboveAThresholdIsItsOwnPredicateAndPointsUpwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The third health predicate, and the only one that asks whether health is
 * HIGH. Issue #1070.
 *
 * WHY IT EXISTS. The Second Vow's third option, Ceaseless Penance, reads
 * "Debuffs on you no longer expire while you are above 50% health". Strictly
 * above 50 and at or below 50 are complements, so the two cover every character
 * between them -- but a modifier carries one predicate and the pipeline has no
 * "not", so a node wanting the upper side needs an enumerator that says so.
 */
bool FCataclysmPipelineHealthAboveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	/** A flat 1 that applies only above a share of maximum health. #1070. */
	auto AboveHealth = [](float Value, float Percent)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Flat;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = Value;
		Modifier.Condition = ECataclysmStatCondition::HealthAbovePercent;
		Modifier.ConditionValue = Percent;
		return Modifier;
	};

	// EXACTLY HALF ITS HEALTH. Five hundred out of a thousand.
	const FCataclysmStatConditions Exactly =
		FCataclysmStatConditions::FromHealth(500.0f, 1'000.0f);
	TestEqual(TEXT("the character is on exactly fifty per cent"),
		Exactly.HealthPercent, 50.0f, 0.01f);

	// STRICTLY ABOVE, SO SITTING ON THE LINE IS NOT ABOVE IT. That is the
	// boundary the option's own word decides, and it is the one place this
	// predicate and its complement have to agree about who gets what.
	TestFalse(TEXT("above fifty excludes a character on fifty"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAbovePercent, 50.0f, Exactly));
	TestTrue(TEXT("and at or below fifty includes it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrBelowPercent, 50.0f, Exactly));

	// AND A HAIR ABOVE IT, IT HOLDS. That is what says the predicate is a
	// boundary rather than broken outright.
	const FCataclysmStatConditions JustOver =
		FCataclysmStatConditions::FromHealth(501.0f, 1'000.0f);
	TestTrue(TEXT("above fifty holds just over it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAbovePercent, 50.0f, JustOver));
	TestTrue(TEXT("and at full health"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::HealthAbovePercent,
								  50.0f, AtHealth(100.0f)));

	// AND BELOW IT, IT DOES NOT.
	TestFalse(TEXT("and not at a tenth of maximum health"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::HealthAbovePercent,
								  50.0f, AtHealth(10.0f)));

	// AN UNKNOWN STATE REFUSES, which is what the character sheet asks with. A
	// conditional bonus folded into a gameplay attribute would be stale the
	// moment health moved, so the sheet must not see it at all.
	const FCataclysmStatConditions Unknown =
		FCataclysmStatConditions::FromHealth(50.0f, 0.0f);
	TestTrue(TEXT("no maximum health means nothing is known"),
		Unknown.HealthPercent < 0.0f);
	TestFalse(TEXT("and an unknown state refuses it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAbovePercent, 50.0f, Unknown));

	// AND THE WHOLE PIPELINE AGREES WITH THE PREDICATE, which is the step that
	// says the enumerator is really wired in rather than merely declared.
	const TArray<FCataclysmStatModifier> Flag = { AboveHealth(1.0f, 50.0f) };
	TestEqual(TEXT("the flag is worth one above half health"),
		FPipeline::Evaluate(0.0f, Flag, NoTags, AtHealth(60.0f)).Final,
		1.0f, 0.01f);
	TestEqual(TEXT("and nothing at half health"),
		FPipeline::Evaluate(0.0f, Flag, NoTags, AtHealth(50.0f)).Final,
		0.0f, 0.01f);
	TestEqual(TEXT("and nothing to a caller with no character at all"),
		FPipeline::Evaluate(0.0f, Flag, NoTags).Final, 0.0f, 0.01f);

	// AND IT IS BOUNDED THE SAME WAY THE OTHER TWO ARE. A threshold of 150 is a
	// modifier that never applies, which is the same silent failure reached
	// from the opposite side.
	TestTrue(TEXT("a threshold above 100 is reported"),
		FPipeline::ValidateModifier(AboveHealth(1.0f, 150.0f))
			.Contains(TEXT("health threshold")));
	TestTrue(TEXT("and a legal one is not"),
		FPipeline::ValidateModifier(AboveHealth(1.0f, 50.0f)).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineHealthAtOrAboveTest,
	"Cataclysm.StatPipeline.AtOrAboveAThresholdIncludesTheCharacterSittingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The fourth health predicate, and the second of a pair pointing upwards.
 * Issues #1653 and #41.
 *
 * WHY IT EXISTS, and it is a data row rather than a hypothetical. The enchantment
 * "Your ultimate ability cannot be used unless you are below 50% HP" locks the
 * skill when health is AT OR ABOVE 50. Written with `HealthAbovePercent`, which
 * is strictly above, a character sitting on exactly half health could use a skill
 * the sentence forbids -- so the row would be delivered differently from how it
 * reads, for one value of health.
 *
 * THE BOUNDARY IS REACHABLE, WHICH IS WHAT MAKES IT WORTH AN ENUMERATOR. Health
 * is a state a character can hold: a flat heal, a clamped maximum or a resting
 * player can sit on exactly half and stay there. That is not true of every
 * threshold -- `StationaryForSeconds` compares at least against rows that say
 * "more than", and those differ only at an instant an accumulating clock passes
 * through, which no player can occupy.
 *
 * THE TWO ARE ASSERTED SIDE BY SIDE ON ONE STATE rather than the new one alone.
 * "At or above fifty holds at fifty" would pass whether or not the strict
 * predicate still excludes that character, and the pair is the whole point.
 */
bool FCataclysmPipelineHealthAtOrAboveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	/** A flat 1 that applies at or above a share of maximum health. */
	auto AtOrAboveHealth = [](float Value, float Percent)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Flat;
		Modifier.Source = ECataclysmModifierSource::Enchantment;
		Modifier.Value = Value;
		Modifier.Condition = ECataclysmStatCondition::HealthAtOrAbovePercent;
		Modifier.ConditionValue = Percent;
		return Modifier;
	};

	// EXACTLY HALF ITS HEALTH. Five hundred out of a thousand.
	const FCataclysmStatConditions Exactly =
		FCataclysmStatConditions::FromHealth(500.0f, 1'000.0f);
	TestEqual(TEXT("the character is on exactly fifty per cent"),
		Exactly.HealthPercent, 50.0f, 0.01f);

	// THE WHOLE DIFFERENCE BETWEEN THE TWO PREDICATES, IN TWO LINES ON ONE STATE.
	TestTrue(TEXT("at or above fifty includes a character on fifty"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 50.0f, Exactly));
	TestFalse(TEXT("and strictly above fifty does not"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAbovePercent, 50.0f, Exactly));

	// A HAIR ABOVE IT, BOTH HOLD. That is what says the new one is a boundary
	// rather than being broken open -- it must not be a predicate that holds
	// everywhere.
	const FCataclysmStatConditions JustOver =
		FCataclysmStatConditions::FromHealth(501.0f, 1'000.0f);
	TestTrue(TEXT("at or above fifty holds just over it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 50.0f, JustOver));
	TestTrue(TEXT("and strictly above fifty holds there too"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAbovePercent, 50.0f, JustOver));

	// AND A HAIR UNDER IT, NEITHER DOES.
	const FCataclysmStatConditions JustUnder =
		FCataclysmStatConditions::FromHealth(499.0f, 1'000.0f);
	TestFalse(TEXT("neither holds just under fifty"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 50.0f, JustUnder)
		|| FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAbovePercent, 50.0f, JustUnder));

	// AN UNKNOWN STATE REFUSES, AND THIS ASSERTION IS WEAKER THAN IT LOOKS.
	// Measured: with a reading of -1 the comparison alone already answers no for
	// both upward predicates, at every threshold the validator allows. So this
	// pins the behaviour and does NOT demonstrate that the guard is doing the
	// work -- it is the two DOWNWARD predicates where removing the guard changes
	// an answer. Said out loud so nobody reads this line as proving more than it
	// does.
	const FCataclysmStatConditions Unknown =
		FCataclysmStatConditions::FromHealth(500.0f, 0.0f);
	TestTrue(TEXT("no maximum health means nothing is known"),
		Unknown.HealthPercent < 0.0f);
	TestFalse(TEXT("and an unknown state refuses it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 50.0f, Unknown));

	// A THRESHOLD OF ZERO MEANS ALWAYS, which is the mirror of what zero means
	// for "at or below" -- there it is "only at exactly no health". Legitimate
	// rather than a data error, and worth pinning because the two endpoints of
	// this predicate are easy to get backwards.
	TestTrue(TEXT("at or above zero holds for a living character"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 0.0f, Exactly));
	TestFalse(TEXT("but not for a caller with no character"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 0.0f, Unknown));

	// AND A HUNDRED MEANS ONLY AT FULL HEALTH.
	const FCataclysmStatConditions Full =
		FCataclysmStatConditions::FromHealth(1'000.0f, 1'000.0f);
	TestTrue(TEXT("at or above a hundred holds at full health"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 100.0f, Full));
	TestFalse(TEXT("and not one point below it"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::HealthAtOrAbovePercent, 100.0f, JustOver));

	// AND THE WHOLE PIPELINE AGREES WITH THE PREDICATE, which is the step that
	// says the enumerator is wired in rather than merely declared. A modifier
	// reaching `ConditionHolds` is not the same as one reaching `Evaluate`.
	const TArray<FCataclysmStatModifier> Flag = { AtOrAboveHealth(1.0f, 50.0f) };
	TestEqual(TEXT("the flag is worth one at exactly half health"),
		FPipeline::Evaluate(0.0f, Flag, NoTags, AtHealth(50.0f)).Final,
		1.0f, 0.01f);
	TestEqual(TEXT("and nothing just below it"),
		FPipeline::Evaluate(0.0f, Flag, NoTags, AtHealth(49.9f)).Final,
		0.0f, 0.01f);
	TestEqual(TEXT("and nothing to a caller with no character at all"),
		FPipeline::Evaluate(0.0f, Flag, NoTags).Final, 0.0f, 0.01f);

	// AND IT IS BOUNDED THE SAME WAY THE OTHER THREE ARE. The bound is written
	// as a list of health predicates and its own comment warns that such a list
	// has to be extended by hand; this is the assertion that would have caught
	// the omission, since an unbounded threshold fails silently.
	TestTrue(TEXT("a threshold above 100 is reported"),
		FPipeline::ValidateModifier(AtOrAboveHealth(1.0f, 150.0f))
			.Contains(TEXT("health threshold")));
	TestTrue(TEXT("a negative threshold is reported too"),
		FPipeline::ValidateModifier(AtOrAboveHealth(1.0f, -1.0f))
			.Contains(TEXT("health threshold")));
	TestTrue(TEXT("and a legal one is not"),
		FPipeline::ValidateModifier(AtOrAboveHealth(1.0f, 50.0f)).IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Pricing a hit, which is where the pipeline is run for real. Issue #1685.
// ---------------------------------------------------------------------------

/**
 * Pricing a hit asks the character what is true of it.
 *
 * WHAT IS WRONG. `UCataclysmSkillEffects::ModifiedDamage` runs the whole stat
 * pipeline over a character's runtime modifier list and passes NO condition
 * state. `UCataclysmStatPipeline::Evaluate` defaults that argument to an empty
 * `FCataclysmStatConditions`, so every condition on a runtime modifier is asked
 * against a world in which nothing is true, and the modifier is worth nothing.
 *
 * FOUR LIVE DAMAGE PATHS GO THROUGH IT: retaliation, a hit, a skill template's
 * hit damage, and a damage-over-time tick.
 *
 * WHY NOTHING NOTICED, AND WHY THIS FILE COULD NOT HAVE CAUGHT IT. Every other
 * test here hands `Evaluate` a state it built itself -- `AtHealth(10.0f)`,
 * `Stationary(3.0f)`, `SinceHealthCost(1.0f)`. So the pipeline is thoroughly
 * checked at reading a state, and NOTHING checked whether the one caller that
 * runs it for a real character supplies one. A test that supplies the missing
 * step cannot see the step missing.
 *
 * AND BOTH RUNTIME MODIFIERS IN THE GAME TODAY ARE UNCONDITIONAL -- a skill's
 * own self buff and an aura's ally increase -- so the fault costs nothing yet.
 * The first conditional one anybody writes is silently worth zero, in the
 * player's favour, with nothing in the data, the log or a test saying so.
 *
 * THE TWO CONTROLS ARE WHAT MAKE THE THIRD CHECK MEAN ONE THING. If the
 * pipeline applied the modifier when handed a state, and the character's own
 * reading of itself satisfied the modifier, then a hit priced at the base
 * amount can only be `ModifiedDamage` failing to ask.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPricedHitAsksTheCharacterTest,
	"Cataclysm.StatPipeline.PricingAHitAsksWhatIsTrueOfTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPricedHitAsksTheCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	// A WORLD AND A REAL ACTOR, WHICH THE FIRST VERSION OF THIS TEST DID NOT
	// HAVE. It built the component with a bare `NewObject` and no owner, on the
	// reasoning that `CurrentConditions` only reads attribute sets and never
	// asks for a world. That is true of `CurrentConditions` and false of this
	// test: WRITING an attribute needs an owner, and the engine says so --
	// "This ActiveGameplayEffectsContainer has an invalid owner. Unable to set
	// attribute MaxHealth" -- and then takes the whole automation run down,
	// which reports as no results rather than as a failure.
	//
	// THIS IS THE FIXTURE CataclysmStacksTests.cpp USES, and for the same
	// reason: an actor, a registered component, the attribute sets, then
	// `InitAbilityActorInfo`.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor to own the ability system"), Actor))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();

	// Raw pointer on purpose: AddAttributeSetSubobject is a template and a
	// TObjectPtr deduces the wrapper rather than the set.
	UCataclysmVitalAttributeSet* Vitals =
		NewObject<UCataclysmVitalAttributeSet>(Actor);
	AbilitySystem->AddAttributeSetSubobject(Vitals);
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	// A CHARACTER AT A TENTH OF ITS HEALTH, well under the threshold below, so
	// the check is not about where the boundary falls.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 100.0f);

	// 50% increased, while at or below half health. Written large so that a
	// wrong answer is the base amount rather than something near it.
	const FCataclysmStatModifier WhileHurt = IncreasedBelowHealth(50.0f, 50.0f);
	TArray<FCataclysmStatModifier> Modifiers = { WhileHurt };

	// --- CONTROL ONE: the pipeline applies it when handed a state ----------

	TestEqual(
		TEXT("handed a state saying the character is at a tenth of its health, "
			 "the pipeline applies the increase"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, AtHealth(10.0f)).Final,
		150.0f, 0.01f);

	// --- CONTROL TWO: the character's own reading of itself satisfies it ----
	//
	// This is the state `ModifiedDamage` could ask for and does not. Checking
	// it separately means a failure below cannot be blamed on the attribute set
	// being unset, on `CurrentConditions` refusing a character with no world,
	// or on the threshold being read the wrong way round.

	TestEqual(
		TEXT("and the character's own reading of itself satisfies it too"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							AbilitySystem->CurrentConditions()).Final,
		150.0f, 0.01f);

	// --- THE SUBJECT -------------------------------------------------------

	AbilitySystem->AddStatModifier(WhileHurt);
	TestEqual(TEXT("the character is holding it"),
		AbilitySystem->GetStatModifiers().Num(), 1);

	TestEqual(
		TEXT("so pricing a hit through ModifiedDamage applies it as well"),
		UCataclysmSkillEffects::ModifiedDamage(AbilitySystem, 100.0f, NoTags),
		150.0f, 0.01f);

	// --- AND THE CASE THAT WAS NEVER BROKEN, WHICH IS WHY NOTHING NOTICED ---
	//
	// An unconditional modifier has always worked through this path, and both
	// runtime modifiers the game adds today are unconditional. Checking it here
	// says that this change fixes the conditional case WITHOUT disturbing the
	// case everything currently relies on.

	AActor* Bare = World->SpawnActor<AActor>();
	UCataclysmAbilitySystemComponent* Plain =
		NewObject<UCataclysmAbilitySystemComponent>(Bare);
	Plain->RegisterComponent();
	Plain->InitAbilityActorInfo(Bare, Bare);

	// NO ATTRIBUTE SET ON THIS ONE, deliberately. It is a character of which
	// nothing is known, which is what every enemy's plain melee attack is when
	// it comes through here.
	Plain->AddStatModifier(Increased(50.0f));
	TestEqual(
		TEXT("an unconditional increase prices the same, with no attribute set "
			 "and nothing true of the character at all"),
		UCataclysmSkillEffects::ModifiedDamage(Plain, 100.0f, NoTags),
		150.0f, 0.01f);

	return true;
}

/**
 * Pricing a hit asks about the BLOW as well as about the character.
 *
 * WHAT WAS WRONG, AND IT IS THE SECOND HALF OF THE TEST ABOVE. Issue #1729.
 * #1685 made `ModifiedDamage` ask the character what is true of it, which fixed
 * every condition about health, stacks, debuffs and movement. It left the four
 * facts that belong to the BLOW at their defaults -- the health cost paid, the
 * distance moved, the target's distance, whether the target was staggered --
 * because they are not properties of a character and the function did not have
 * them. So a modifier conditioned on one was still judged against a world in
 * which no blow exists, and was still silently worth nothing.
 *
 * WHY THE TEST ABOVE COULD NOT CATCH IT. It builds a modifier conditioned on
 * the character's health. That is exactly the half that was fixed. A condition
 * about the blow takes a different route through the same call and nothing
 * exercised it.
 *
 * THE THIRD CHECK IS THE ONE THAT WOULD HAVE BEEN NEW. The first two are the
 * same shape of control the test above uses, and the fourth is the case that
 * must NOT change: a caller with no blow in hand still gets no increase, which
 * is what retaliation, a burn spread and a patch of burning ground all are.
 * Without that fourth check this test would pass just as happily if the
 * defaults had been changed to "always true", which would be a worse fault than
 * the one being fixed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPricedHitAsksAboutTheBlowTest,
	"Cataclysm.StatPipeline.PricingAHitAsksWhatIsTrueOfTheBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPricedHitAsksAboutTheBlowTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	// THE SAME FIXTURE AS THE TEST ABOVE, and for the same reason: writing an
	// attribute needs an owner, and a bare NewObject component takes the whole
	// automation run down rather than failing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor to own the ability system"), Actor))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	// 50% INCREASED WHILE THE TARGET IS WITHIN FIVE METRES. Written large so a
	// wrong answer is the base amount rather than something near it, and chosen
	// because the distance is the per-blow fact that already reaches the
	// pipeline from the item path -- so a failure here is this call site and not
	// the condition itself.
	FCataclysmStatModifier WhileClose = Increased(50.0f);
	WhileClose.Condition = ECataclysmStatCondition::TargetWithinMetres;
	WhileClose.ConditionValue = 5.0f;
	TArray<FCataclysmStatModifier> Modifiers = { WhileClose };

	// --- CONTROL ONE: the pipeline applies it when handed a target ---------

	TestEqual(
		TEXT("handed a state saying the target stands two metres away, the "
			 "pipeline applies the increase"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags, TargetAt(2.0f)).Final,
		150.0f, 0.01f);

	// --- CONTROL TWO: and refuses when no target is known ------------------
	//
	// THIS IS WHAT MAKES THE THIRD CHECK MEAN ONE THING. The character's own
	// reading of itself cannot satisfy this modifier, because a target distance
	// is not something a character knows about itself. So if the priced hit
	// below comes out at 150 it can only be because the distance was passed in.

	TestEqual(
		TEXT("and the character's own reading of itself does NOT satisfy it, "
			 "because no character knows how far away it is being hit from"),
		FPipeline::Evaluate(100.0f, Modifiers, NoTags,
							AbilitySystem->CurrentConditions()).Final,
		100.0f, 0.01f);

	// --- THE SUBJECT -------------------------------------------------------

	AbilitySystem->AddStatModifier(WhileClose);
	TestEqual(TEXT("the character is holding it"),
		AbilitySystem->GetStatModifiers().Num(), 1);

	TestEqual(
		TEXT("pricing a hit told the target stands two metres away applies it"),
		UCataclysmSkillEffects::ModifiedDamage(
			AbilitySystem, 100.0f, NoTags,
			/*SkillHealthCostPercent=*/-1.0f,
			/*MetresMovedBeforeBlow=*/-1.0f,
			/*TargetDistanceMetres=*/2.0f),
		150.0f, 0.01f);

	// --- AND THE CASE THAT MUST NOT CHANGE ---------------------------------
	//
	// A CALLER WITH NO BLOW IN HAND STILL GETS NOTHING, and that is correct
	// rather than a gap. Retaliation takes a defender and an amount; a burn
	// spread prices the caster's own attack with no target chosen; a patch of
	// burning ground is priced ONCE when it is created, deliberately not per
	// tick, because it outlives the skill that left it. A modifier conditioned
	// on the target's distance has no target to measure against in any of them.
	//
	// WITHOUT THIS CHECK the test would pass just as happily if "not known" had
	// been made to satisfy every condition, which would be a worse fault than
	// the silent zero being fixed: every conditional modifier would apply to
	// everything.

	TestEqual(
		TEXT("and a hit priced with no blow in hand still gets no increase"),
		UCataclysmSkillEffects::ModifiedDamage(AbilitySystem, 100.0f, NoTags),
		100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineEnemiesStruckAtLeastTest,
	"Cataclysm.StatPipeline.AnIncreaseCanRequireAnAttackToStrikeEnoughEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * How many enemies one attack struck together, as a condition. Issue #1515.
 *
 * `Ravager_keystone_b_kA` Sundering: "Your melee attacks ignore enemy Armor
 * entirely when they hit three or more enemies at once." At least three holds
 * at three and above and not at two. An unknown count refuses, which is every
 * lookup made with no attack in hand, and so does a threshold of nothing, which
 * would otherwise hold for every blow in the game.
 */
bool FCataclysmPipelineEnemiesStruckAtLeastTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	FCataclysmStatModifier AtLeastThree = Increased(50.0f);
	AtLeastThree.Condition = ECataclysmStatCondition::EnemiesStruckTogetherAtLeast;
	AtLeastThree.ConditionValue = 3.0f;
	const TArray<FCataclysmStatModifier> Three = { AtLeastThree };

	TestEqual(TEXT("two enemies struck is not three"),
		FPipeline::Evaluate(100.0f, Three, NoTags, StruckTogether(2)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("exactly three counts"),
		FPipeline::Evaluate(100.0f, Three, NoTags, StruckTogether(3)).Final,
		150.0f, 0.01f);
	TestEqual(TEXT("and so do five"),
		FPipeline::Evaluate(100.0f, Three, NoTags, StruckTogether(5)).Final,
		150.0f, 0.01f);

	TestEqual(TEXT("an unknown count refuses"),
		FPipeline::Evaluate(100.0f, Three, NoTags, StruckTogether(-1)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and so does a state nobody filled in"),
		FPipeline::Evaluate(100.0f, Three, NoTags, FCataclysmStatConditions()).Final,
		100.0f, 0.01f);

	FCataclysmStatModifier AtLeastNothing = AtLeastThree;
	AtLeastNothing.ConditionValue = 0.0f;
	const TArray<FCataclysmStatModifier> Nothing = { AtLeastNothing };
	TestEqual(TEXT("a threshold of nothing refuses, even at ten enemies struck"),
		FPipeline::Evaluate(100.0f, Nothing, NoTags, StruckTogether(10)).Final,
		100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineEnemiesStruckBeyondTheFirstTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithEachEnemyStruckBeyondTheFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The same count as a scale, less one. Issue #1515.
 *
 * `Ravager_basic_b_a2` Cleaving Arc: "+1% increased Attack Damage per point for
 * each enemy your attack hits beyond the first." One enemy struck is worth
 * nothing and three are worth two steps. An unknown count is worth nothing
 * rather than the row's bare value, which is the case a build that forgets the
 * count gets wrong in the player's favour.
 */
bool FCataclysmPipelineEnemiesStruckBeyondTheFirstTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	FCataclysmStatModifier PerExtraEnemy = Increased(10.0f);
	PerExtraEnemy.Scale = ECataclysmStatScale::PerEnemyStruckTogetherBeyondTheFirst;
	PerExtraEnemy.ScaleStep = 1.0f;
	const TArray<FCataclysmStatModifier> Arc = { PerExtraEnemy };

	TestEqual(TEXT("one enemy struck is worth nothing"),
		FPipeline::Evaluate(100.0f, Arc, NoTags, StruckTogether(1)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("two are worth one step"),
		FPipeline::Evaluate(100.0f, Arc, NoTags, StruckTogether(2)).Final,
		110.0f, 0.01f);
	TestEqual(TEXT("three are worth two"),
		FPipeline::Evaluate(100.0f, Arc, NoTags, StruckTogether(3)).Final,
		120.0f, 0.01f);
	TestEqual(TEXT("no enemies at all are worth nothing"),
		FPipeline::Evaluate(100.0f, Arc, NoTags, StruckTogether(0)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and an unknown count is worth nothing, not the row's value"),
		FPipeline::Evaluate(100.0f, Arc, NoTags, StruckTogether(-1)).Final,
		100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineDamageReductionScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithWholeStepsOfDamageReduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A bonus sized by the character's damage reduction. Issue #1515.
 *
 * `Ravager_capstone_100`'s second option, Weight Against Them: "+1% increased
 * Attack Damage for every 2% of Damage Reduction you have." This is the
 * arithmetic alone, handed a reading. Where the reading comes from, and the cap
 * it stops at, is
 * `Cataclysm.ConditionalDamage.AttackDamageGrowsWithTheDamageReductionTheCharacterHasUpToItsCap`.
 */
bool FCataclysmPipelineDamageReductionScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	FCataclysmStatModifier PerTwoPercent = Increased(1.0f);
	PerTwoPercent.Scale = ECataclysmStatScale::PerPercentOfDamageReduction;
	PerTwoPercent.ScaleStep = 2.0f;
	const TArray<FCataclysmStatModifier> Weight = { PerTwoPercent };

	const auto Reducing = [](float Percent)
	{
		FCataclysmStatConditions State;
		State.DamageReductionPercent = Percent;
		return State;
	};

	// THE NAME A ROW WILL CARRY, the one `tools/generate_datatables.py` knows.
	ECataclysmStatScale Named = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("the sheet's name damage_reduction is known"),
		FPipeline::ScaleNamed(TEXT("damage_reduction"), Named));
	TestEqual(TEXT("and it is this scale"), static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatScale::PerPercentOfDamageReduction));

	TestEqual(TEXT("no damage reduction is worth nothing"),
		FPipeline::Evaluate(100.0f, Weight, NoTags, Reducing(0.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("10% is five steps of two, so +5%"),
		FPipeline::Evaluate(100.0f, Weight, NoTags, Reducing(10.0f)).Final,
		105.0f, 0.01f);

	// WHOLE STEPS, ROUNDED DOWN: 9% is four completed steps of two, not four
	// and a half.
	TestEqual(TEXT("9% is still four steps"),
		FPipeline::Evaluate(100.0f, Weight, NoTags, Reducing(9.0f)).Final,
		104.0f, 0.01f);

	// NOTHING TO READ SCALES TO NOTHING, which is a different statement from
	// having none: the character sheet has no character in hand.
	TestEqual(TEXT("a caller that knows nothing about the character gets nothing"),
		FPipeline::Evaluate(100.0f, Weight, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("and so does one that says outright there is nothing to read"),
		FPipeline::Evaluate(100.0f, Weight, NoTags, Reducing(-1.0f)).Final,
		100.0f, 0.01f);

	// A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero.
	FCataclysmStatModifier NoStep = PerTwoPercent;
	NoStep.ScaleStep = 0.0f;
	const TArray<FCataclysmStatModifier> Stepless = { NoStep };
	TestEqual(TEXT("a step of nothing is worth nothing"),
		FPipeline::Evaluate(100.0f, Stepless, NoTags, Reducing(50.0f)).Final,
		100.0f, 0.01f);

	// AND IT READS ITS OWN FIELD. A state that knows only the maximum mana is
	// worth nothing to it, so a case reading the other new field fails here.
	FCataclysmStatConditions ManaOnly;
	ManaOnly.MaximumMana = 1'000.0f;
	TestEqual(TEXT("maximum mana alone is worth nothing to it"),
		FPipeline::Evaluate(100.0f, Weight, NoTags, ManaOnly).Final,
		100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineMaximumManaScaleTest,
	"Cataclysm.StatPipeline.AnIncreaseCanGrowWithWholeStepsOfMaximumMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A bonus sized by the character's maximum mana. Issue #1515.
 *
 * `Ritualist_basic_d_a2` Drawn Deep: "+1% increased Spell Damage per point for
 * every full 200 maximum mana you have." The arithmetic alone, handed a
 * reading. Where the reading comes from is
 * `Cataclysm.ConditionalDamage.SpellDamageGrowsWithTheMaximumManaAndNotTheManaInHand`.
 */
bool FCataclysmPipelineMaximumManaScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	FCataclysmStatModifier PerFullTwoHundred = Increased(1.0f);
	PerFullTwoHundred.Scale = ECataclysmStatScale::PerPointOfMaximumMana;
	PerFullTwoHundred.ScaleStep = 200.0f;
	const TArray<FCataclysmStatModifier> Deep = { PerFullTwoHundred };

	const auto WithMaximum = [](float Mana)
	{
		FCataclysmStatConditions State;
		State.MaximumMana = Mana;
		return State;
	};

	ECataclysmStatScale Named = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("the sheet's name max_mana is known"),
		FPipeline::ScaleNamed(TEXT("max_mana"), Named));
	TestEqual(TEXT("and it is this scale"), static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatScale::PerPointOfMaximumMana));

	TestEqual(TEXT("no maximum mana is worth nothing"),
		FPipeline::Evaluate(100.0f, Deep, NoTags, WithMaximum(0.0f)).Final,
		100.0f, 0.01f);

	// "EVERY FULL 200": 199 is not one, 200 is.
	TestEqual(TEXT("199 is not a full 200"),
		FPipeline::Evaluate(100.0f, Deep, NoTags, WithMaximum(199.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("200 is one step"),
		FPipeline::Evaluate(100.0f, Deep, NoTags, WithMaximum(200.0f)).Final,
		101.0f, 0.01f);
	TestEqual(TEXT("450 is two full steps"),
		FPipeline::Evaluate(100.0f, Deep, NoTags, WithMaximum(450.0f)).Final,
		102.0f, 0.01f);

	TestEqual(TEXT("a caller that knows nothing about the character gets nothing"),
		FPipeline::Evaluate(100.0f, Deep, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("and so does one that says outright there is nothing to read"),
		FPipeline::Evaluate(100.0f, Deep, NoTags, WithMaximum(-1.0f)).Final,
		100.0f, 0.01f);

	FCataclysmStatModifier NoStep = PerFullTwoHundred;
	NoStep.ScaleStep = 0.0f;
	const TArray<FCataclysmStatModifier> Stepless = { NoStep };
	TestEqual(TEXT("a step of nothing is worth nothing"),
		FPipeline::Evaluate(100.0f, Stepless, NoTags, WithMaximum(1'000.0f)).Final,
		100.0f, 0.01f);

	// AND IT READS ITS OWN FIELD, the mirror of the check in the test above.
	FCataclysmStatConditions ReductionOnly;
	ReductionOnly.DamageReductionPercent = 50.0f;
	TestEqual(TEXT("damage reduction alone is worth nothing to it"),
		FPipeline::Evaluate(100.0f, Deep, NoTags, ReductionOnly).Final,
		100.0f, 0.01f);

	return true;
}


// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineNewConditionsTest,
	"Cataclysm.StatPipeline.FourNewConditionsHoldOnTheirReadingAndRefuseWithoutOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The four conditions added for issue #1981's follow-on, each at its boundary
 * and each refusing when it knows nothing.
 *
 * THE REFUSING HALF IS THE POINT. A condition that always held would pass a test
 * that only checked the cases it is meant to hold in, and would hand every row
 * written on it to every character in the game. Each of the four is asked once
 * where it must hold and once where it must not.
 *
 * NONE OF THEM TRACKS ANYTHING NEW. Every field read here was already in
 * `FCataclysmStatConditions` and already filled on the lookup the row's sentence
 * needs; these are judgements over readings the pipeline already had.
 */
bool FCataclysmPipelineNewConditionsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	const auto Moved = [](float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceMoved = Seconds;
		return State;
	};
	const auto Resource = [](float Held, float Maximum)
	{
		FCataclysmStatConditions State;
		State.ClassResourceHeld = Held;
		State.ClassResourceMaximum = Maximum;
		return State;
	};
	const auto Shield = [](float Held)
	{
		FCataclysmStatConditions State;
		State.EnergyShieldHeld = Held;
		return State;
	};

	// THE NAMES A SHEET WRITES REACH THESE CONDITIONS. A name the generator
	// accepts and the engine does not know is a row that grants nothing.
	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("opponent_within_metres is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("opponent_within_metres"), Named));
	TestEqual(TEXT("and it is the near reading of the blow's distance"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::OpponentWithinMetres));
	TestTrue(TEXT("moved_within_seconds is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("moved_within_seconds"), Named));
	TestEqual(TEXT("and it is the recent-movement reading"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::MovedWithinSeconds));
	TestTrue(TEXT("class_resource_above is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("class_resource_above"), Named));
	TestEqual(TEXT("and it is the share of the class resource bar"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::ClassResourceAbovePercent));
	TestTrue(TEXT("energy_shield_above_zero is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("energy_shield_above_zero"), Named));
	TestEqual(TEXT("and it is the shield-is-holding-something reading"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::EnergyShieldAboveZero));

	// AT OR WITHIN, THE OPPOSITE BOUNDARY FROM ITS FAR TWIN. Five metres is the
	// figure every near row in the game already uses.
	const ECataclysmStatCondition Within =
		ECataclysmStatCondition::OpponentWithinMetres;
	TestTrue(TEXT("a blow from exactly five metres is within five"),
		FPipeline::ConditionHolds(Within, 5.0f, StruckFrom(5.0f)));
	TestFalse(TEXT("and one from a hair further is not"),
		FPipeline::ConditionHolds(Within, 5.0f, StruckFrom(5.1f)));
	TestTrue(TEXT("and one from a hair nearer is"),
		FPipeline::ConditionHolds(Within, 5.0f, StruckFrom(4.9f)));

	// ZERO IS A REAL DISTANCE AND -1 IS NOT A DISTANCE AT ALL. Two characters
	// can stand on one spot, so the unknown reading has to be told apart from
	// the nearest real one -- and an unknown reading is at or within every
	// threshold a sheet may write, which is why it must refuse.
	TestTrue(TEXT("nought metres is a real reading and is within five"),
		FPipeline::ConditionHolds(Within, 5.0f, StruckFrom(0.0f)));
	TestFalse(TEXT("an unknown distance refuses rather than satisfying it"),
		FPipeline::ConditionHolds(Within, 5.0f, StruckFrom(-1.0f)));

	// IT READS THE BLOW AND NOT THE ATTACKER-SIDE DISTANCE. A row that used the
	// wrong half of the pair must grant nothing rather than read a plausible
	// number from the wrong end.
	TestFalse(TEXT("the attacker's own target distance does not satisfy it"),
		FPipeline::ConditionHolds(Within, 5.0f, TargetAt(1.0f)));

	// WITHIN IS INCLUSIVE HERE TOO.
	const ECataclysmStatCondition Recently =
		ECataclysmStatCondition::MovedWithinSeconds;
	TestTrue(TEXT("a character moving this instant has moved within two seconds"),
		FPipeline::ConditionHolds(Recently, 2.0f, Moved(0.0f)));
	TestTrue(TEXT("and one that moved exactly two seconds ago has"),
		FPipeline::ConditionHolds(Recently, 2.0f, Moved(2.0f)));
	TestFalse(TEXT("and one a hair past that has not"),
		FPipeline::ConditionHolds(Recently, 2.0f, Moved(2.1f)));
	TestFalse(TEXT("a character no movement sample has looked at refuses"),
		FPipeline::ConditionHolds(Recently, 2.0f, Moved(-1.0f)));

	// THE OVERLAP WITH `StationaryForSeconds`, PINNED RATHER THAN DISCOVERED.
	// At exactly the threshold both readings of the same field hold: two
	// seconds since moving is two seconds stood still AND movement within the
	// last two. One instant wide, and stated so nobody reads it as a fault.
	TestTrue(TEXT("at exactly the threshold the stationary reading holds too"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::StationaryForSeconds,
								  2.0f, Moved(2.0f)));

	// STRICTLY ABOVE, AND A SHARE RATHER THAN A COUNT OF POINTS.
	const ECataclysmStatCondition Above =
		ECataclysmStatCondition::ClassResourceAbovePercent;
	TestTrue(TEXT("76 points of 100 is above 75 per cent"),
		FPipeline::ConditionHolds(Above, 75.0f, Resource(76.0f, 100.0f)));
	TestFalse(TEXT("and exactly 75 of 100 is not above it"),
		FPipeline::ConditionHolds(Above, 75.0f, Resource(75.0f, 100.0f)));
	TestTrue(TEXT("8 of 10 is the same share and holds, so it reads a share"),
		FPipeline::ConditionHolds(Above, 75.0f, Resource(8.0f, 10.0f)));

	// THE TWO REFUSALS THE FULL-BAR READINGS ALREADY MAKE. An unknown pair would
	// compare -1 against -1; a bar that can hold nothing would be divided by.
	TestFalse(TEXT("a character with no class resource at all refuses"),
		FPipeline::ConditionHolds(Above, 75.0f, Resource(-1.0f, -1.0f)));
	TestFalse(TEXT("and a bar that cannot hold anything refuses"),
		FPipeline::ConditionHolds(Above, 75.0f, Resource(0.0f, 0.0f)));

	// HELD ABOVE ZERO, WHICH IS NOT THE SAME QUESTION AS "IS IT FULL".
	const ECataclysmStatCondition Live =
		ECataclysmStatCondition::EnergyShieldAboveZero;
	TestTrue(TEXT("a shield holding anything at all is active"),
		FPipeline::ConditionHolds(Live, 0.0f, Shield(1.0f)));
	TestFalse(TEXT("a shield at nothing is not"),
		FPipeline::ConditionHolds(Live, 0.0f, Shield(0.0f)));
	TestFalse(TEXT("and an unknown reading refuses"),
		FPipeline::ConditionHolds(Live, 0.0f, Shield(-1.0f)));

	// IT STATES NO THRESHOLD, SO THE VALUE IS NOT READ. Both lines below would
	// change answer if it were.
	TestTrue(TEXT("a value on the row changes nothing when a shield is held"),
		FPipeline::ConditionHolds(Live, 999.0f, Shield(1.0f)));
	TestFalse(TEXT("and changes nothing when the reading is unknown"),
		FPipeline::ConditionHolds(Live, -5.0f, Shield(-1.0f)));

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineNewScalesTest,
	"Cataclysm.StatPipeline.ABonusCanGrowWithMetresToTheTargetOrSecondsStoodStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two scale sources added for issue #1981's follow-on.
 *
 * TWO DIFFERENT READINGS EACH, so a scale stuck on one value fails rather than
 * passing the single case it was written against.
 *
 * AND THE FLOOR, MEASURED HERE RATHER THAN ASSERTED IN PROSE. "All damage dealt
 * is reduced by 15%-25% for each second you stand still" states no cap, and its
 * row is a MULTIPLYING reduction on purpose: `LessMultiplierFloor` stops one at
 * -99%, so one per cent of the hit survives however long the character stands
 * there. `docs/DECISIONS.md` carries why an increased row would not have.
 */
bool FCataclysmPipelineNewScalesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStatTest;

	const auto Moved = [](float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceMoved = Seconds;
		return State;
	};

	ECataclysmStatScale Named = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("the sheet's name metres_to_target is known"),
		FPipeline::ScaleNamed(TEXT("metres_to_target"), Named));
	TestEqual(TEXT("and it is the distance to what is being struck"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatScale::PerMetreToTarget));
	TestTrue(TEXT("the sheet's name seconds_stationary is known"),
		FPipeline::ScaleNamed(TEXT("seconds_stationary"), Named));
	TestEqual(TEXT("and it is the time stood still"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatScale::PerSecondStationary));

	FCataclysmStatModifier PerMetre = Increased(1.0f);
	PerMetre.Scale = ECataclysmStatScale::PerMetreToTarget;
	PerMetre.ScaleStep = 1.0f;
	const TArray<FCataclysmStatModifier> Ranged = { PerMetre };

	TestEqual(TEXT("a target on the same spot is worth nothing"),
		FPipeline::Evaluate(100.0f, Ranged, NoTags, TargetAt(0.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("4.9 metres is four whole steps and not five"),
		FPipeline::Evaluate(100.0f, Ranged, NoTags, TargetAt(4.9f)).Final,
		104.0f, 0.01f);
	TestEqual(TEXT("and ten metres is ten"),
		FPipeline::Evaluate(100.0f, Ranged, NoTags, TargetAt(10.0f)).Final,
		110.0f, 0.01f);

	// A LOOKUP WITH NO TARGET IN HAND IS WORTH NOTHING, and the defender-side
	// distance is a different field that must not feed it.
	TestEqual(TEXT("a caller with no target in hand gets nothing"),
		FPipeline::Evaluate(100.0f, Ranged, NoTags).Final, 100.0f, 0.01f);
	TestEqual(TEXT("and the blow's own distance does not feed this scale"),
		FPipeline::Evaluate(100.0f, Ranged, NoTags, StruckFrom(10.0f)).Final,
		100.0f, 0.01f);

	FCataclysmStatModifier PerSecond = Increased(2.0f);
	PerSecond.Scale = ECataclysmStatScale::PerSecondStationary;
	PerSecond.ScaleStep = 1.0f;
	const TArray<FCataclysmStatModifier> Waiting = { PerSecond };

	TestEqual(TEXT("a character that has just moved has stood still for nothing"),
		FPipeline::Evaluate(100.0f, Waiting, NoTags, Moved(0.0f)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("1.9 seconds is one whole step"),
		FPipeline::Evaluate(100.0f, Waiting, NoTags, Moved(1.9f)).Final,
		102.0f, 0.01f);
	TestEqual(TEXT("and five seconds is five"),
		FPipeline::Evaluate(100.0f, Waiting, NoTags, Moved(5.0f)).Final,
		110.0f, 0.01f);
	TestEqual(TEXT("a character no movement sample has looked at gets nothing"),
		FPipeline::Evaluate(100.0f, Waiting, NoTags, Moved(-1.0f)).Final,
		100.0f, 0.01f);

	// THE UNCAPPED DRAWBACK, AND WHY IT NEEDS NO CAP. Fifteen per cent a second
	// asks for -105% at seven seconds; the floor stops it at -99%, so a hundred
	// damage becomes one rather than none or a negative.
	FCataclysmStatModifier PerSecondLess = MoreFromGem(-15.0f);
	PerSecondLess.Scale = ECataclysmStatScale::PerSecondStationary;
	PerSecondLess.ScaleStep = 1.0f;
	const TArray<FCataclysmStatModifier> StandingStill = { PerSecondLess };

	TestEqual(TEXT("four seconds standing still takes sixty per cent of the hit"),
		FPipeline::Evaluate(100.0f, StandingStill, NoTags, Moved(4.0f)).Final,
		40.0f, 0.01f);
	TestEqual(TEXT("seven seconds asks for more than all of it and leaves one"),
		FPipeline::Evaluate(100.0f, StandingStill, NoTags, Moved(7.0f)).Final,
		1.0f, 0.01f);
	TestTrue(TEXT("and no length of standing still reaches nothing"),
		FPipeline::Evaluate(100.0f, StandingStill, NoTags, Moved(600.0f)).Final
			> 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelinePointsThresholdTest,
	"Cataclysm.StatPipeline.AClassResourceThresholdInPointsIsNotAShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `ClassResourcePointsAtLeast` counts POINTS, and the two readings are
 * separated in BOTH directions rather than in one.
 *
 * WHY BOTH DIRECTIONS. A test that only showed points holding where a share
 * refuses would pass for a condition that always held; one that only showed a
 * share refusing where points hold would pass for a condition that always
 * refused. The pair below has one case each way, on the same threshold of fifty:
 * fifty of a hundred and fifty is a THIRD of the bar and holds, and forty of
 * sixty is TWO THIRDS of the bar and does not. A share reading at fifty answers
 * the opposite for both.
 *
 * AND THE BOUNDARY AGAINST ITS NEIGHBOUR, which reads the same pool as a share
 * and strictly above. At exactly fifty of a hundred this holds and that one does
 * not: "50 or more" against "above 50%". Pinned here so the difference is a
 * measured fact rather than a sentence in a comment. Issue #1515.
 */
bool FCataclysmPipelinePointsThresholdTest::RunTest(const FString&)
{
	using FPipeline = UCataclysmStatPipeline;

	const auto Resource = [](float Held, float Maximum)
	{
		FCataclysmStatConditions State;
		State.ClassResourceHeld = Held;
		State.ClassResourceMaximum = Maximum;
		return State;
	};

	const ECataclysmStatCondition AtLeast =
		ECataclysmStatCondition::ClassResourcePointsAtLeast;

	// "50 OR MORE", SO FIFTY ITSELF HOLDS.
	TestTrue(TEXT("exactly fifty points of a hundred holds"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(50.0f, 100.0f)));
	TestFalse(TEXT("and forty-nine does not"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(49.0f, 100.0f)));
	TestTrue(TEXT("and fifty-one does"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(51.0f, 100.0f)));

	// THE UNIT, SEPARATED IN BOTH DIRECTIONS ON ONE THRESHOLD.
	TestTrue(TEXT("fifty of a hundred and fifty holds, though it is a third of "
				  "the bar, so it counts points"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(50.0f, 150.0f)));
	TestFalse(TEXT("and forty of sixty refuses, though it is two thirds of the "
				   "bar, so it is not reading a share"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(40.0f, 60.0f)));

	// THE MAXIMUM IS NOT READ AT ALL, which is why this has two clauses where
	// its neighbour has three: there is no bar-of-nothing to refuse.
	TestTrue(TEXT("fifty points holds even when the bar's top is unknown"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(50.0f, -1.0f)));

	// AN UNKNOWN READING REFUSES. Every enemy in the game has no class resource
	// attribute set and reads -1; without this a threshold of zero would hand
	// them all a bonus written for a Ravager.
	TestFalse(TEXT("a character with no class resource at all refuses"),
		FPipeline::ConditionHolds(AtLeast, 50.0f, Resource(-1.0f, -1.0f)));
	TestFalse(TEXT("and refuses even against a threshold of nothing"),
		FPipeline::ConditionHolds(AtLeast, 0.0f, Resource(-1.0f, 100.0f)));

	// THE BOUNDARY ITS NEIGHBOUR DRAWS THE OTHER WAY.
	TestFalse(TEXT("at fifty of a hundred the strictly-above share condition "
				   "does not hold"),
		FPipeline::ConditionHolds(
			ECataclysmStatCondition::ClassResourceAbovePercent, 50.0f,
			Resource(50.0f, 100.0f)));

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossWindowConditionTest,
	"Cataclysm.StatPipeline.TheWindowAfterStrikingABossHoldsInsideItAndRefusesWithoutOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `seconds_after_striking_a_boss` is a name this build knows, holds at or within
 * its number of seconds, and refuses a character that has struck no Boss.
 *
 * THE REFUSAL IS THE HALF WORTH WRITING. A character that has never struck a
 * Boss reads -1, which is at or within every threshold a sheet may write, so a
 * comparison with no guard in front of it would grant the bonus to everybody who
 * had never seen a Boss -- the opposite of the row.
 */
bool FCataclysmBossWindowConditionTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("seconds_after_striking_a_boss is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("seconds_after_striking_a_boss"), Named));
	TestEqual(TEXT("and it is the window a Boss strike opens"),
		static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::WithinSecondsOfStrikingABoss));

	const auto StruckABoss = [](float Seconds)
	{
		FCataclysmStatConditions State;
		State.SecondsSinceStruckABoss = Seconds;
		return State;
	};

	// FOUR SECONDS IS THE ROW'S FIGURE: "Your cooldowns reset 50%-100% faster
	// when fighting Boss enemies, for 4 seconds after you strike one".
	const ECataclysmStatCondition Window =
		ECataclysmStatCondition::WithinSecondsOfStrikingABoss;
	TestTrue(TEXT("a character striking a Boss this instant is inside the window"),
		FPipeline::ConditionHolds(Window, 4.0f, StruckABoss(0.0f)));
	TestTrue(TEXT("and one that struck exactly four seconds ago is, because within is inclusive"),
		FPipeline::ConditionHolds(Window, 4.0f, StruckABoss(4.0f)));
	TestFalse(TEXT("and one a hair past four seconds is not"),
		FPipeline::ConditionHolds(Window, 4.0f, StruckABoss(4.1f)));
	TestFalse(TEXT("and one well past it is not"),
		FPipeline::ConditionHolds(Window, 4.0f, StruckABoss(30.0f)));
	TestFalse(TEXT("a character that has struck no Boss refuses rather than satisfying it"),
		FPipeline::ConditionHolds(Window, 4.0f, StruckABoss(-1.0f)));

	// AND IT READS ITS OWN FIELD AND NOT A NEIGHBOUR'S. A build that read the
	// hit-taken clock would pass every line above on a character that had just
	// been hit, which is the opposite of the row.
	FCataclysmStatConditions HitTakenOnly;
	HitTakenOnly.SecondsSinceHitTaken = 0.0f;
	TestFalse(TEXT("being hit a moment ago is not striking a Boss"),
		FPipeline::ConditionHolds(Window, 4.0f, HitTakenOnly));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFiveMovementWindowsConditionTest,
	"Cataclysm.StatPipeline.TheFiveMovementRowWindowsHoldInsideThemAndRefuseWithoutThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The five windows the movement rows read -- after using the support skill,
 * after using the movement skill, after casting a spell, after a melee hit
 * taken, after applying crowd control -- each hold inside their window and
 * refuse outside it, with no event at all, and on another window's clock.
 * Issue #1815.
 *
 * THE LAST CASE IS WHAT CATCHES A JUDGEMENT READING THE WRONG FIELD. Every
 * window is judged with only the NEXT one's clock set, so a case written
 * against its neighbour's field passes the first four assertions and fails
 * there.
 */
bool FCataclysmFiveMovementWindowsConditionTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	struct FCase
	{
		const TCHAR* Name;
		ECataclysmStatCondition Condition;
		float FCataclysmStatConditions::* Clock;
	};

	const FCase Cases[] = {
		{TEXT("seconds_after_support_skill"),
		 ECataclysmStatCondition::WithinSecondsOfSupportSkill,
		 &FCataclysmStatConditions::SecondsSinceSupportSkill},
		{TEXT("seconds_after_movement_skill"),
		 ECataclysmStatCondition::WithinSecondsOfMovementSkill,
		 &FCataclysmStatConditions::SecondsSinceMovementSkill},
		{TEXT("seconds_after_spell"),
		 ECataclysmStatCondition::WithinSecondsOfSpell,
		 &FCataclysmStatConditions::SecondsSinceSpell},
		{TEXT("seconds_after_melee_hit_taken"),
		 ECataclysmStatCondition::WithinSecondsOfMeleeHitTaken,
		 &FCataclysmStatConditions::SecondsSinceMeleeHitTaken},
		{TEXT("seconds_after_crowd_control"),
		 ECataclysmStatCondition::WithinSecondsOfCrowdControl,
		 &FCataclysmStatConditions::SecondsSinceCrowdControl},
	};
	const int32 Count = UE_ARRAY_COUNT(Cases);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FCase& Case = Cases[Index];

		ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
		TestTrue(FString::Printf(TEXT("%s is a name this build knows"), Case.Name),
			FPipeline::ConditionNamed(Case.Name, Named));
		TestEqual(FString::Printf(TEXT("and %s names its own window"), Case.Name),
			static_cast<int32>(Named), static_cast<int32>(Case.Condition));

		const auto At = [&Case](float Seconds)
		{
			FCataclysmStatConditions State;
			State.*(Case.Clock) = Seconds;
			return State;
		};

		TestTrue(FString::Printf(TEXT("%s holds on the instant"), Case.Name),
			FPipeline::ConditionHolds(Case.Condition, 3.0f, At(0.0f)));
		TestTrue(FString::Printf(TEXT("%s holds at exactly three seconds"), Case.Name),
			FPipeline::ConditionHolds(Case.Condition, 3.0f, At(3.0f)));
		TestFalse(FString::Printf(TEXT("%s refuses a hair past three"), Case.Name),
			FPipeline::ConditionHolds(Case.Condition, 3.0f, At(3.1f)));
		TestFalse(FString::Printf(TEXT("%s refuses a character it never happened to"),
								  Case.Name),
			FPipeline::ConditionHolds(Case.Condition, 3.0f, At(-1.0f)));

		FCataclysmStatConditions Neighbour;
		Neighbour.*(Cases[(Index + 1) % Count].Clock) = 0.0f;
		TestFalse(FString::Printf(TEXT("%s refuses when only %s's clock is open"),
								  Case.Name, Cases[(Index + 1) % Count].Name),
			FPipeline::ConditionHolds(Case.Condition, 3.0f, Neighbour));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineManaBelowTest,
	"Cataclysm.StatPipeline.ManaBelowIsStrictAndRefusesACharacterWithNoPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `mana_below`, the first mana predicate. Issues #1820 and #41.
 *
 * WRITTEN FOR THE DUNGEON FLOOR RULE `Famine_Desperate_Measures` AT 10 AND MEANT
 * TO BE REUSED, so it is pinned here as a general threshold: both sides of the
 * boundary, the two readings that must refuse, and the bound a sheet may write.
 */
bool FCataclysmPipelineManaBelowTest::RunTest(const FString&)
{
	using FPipeline = UCataclysmStatPipeline;

	const auto Mana = [](float Percent)
	{
		FCataclysmStatConditions State;
		State.ManaPercent = Percent;
		return State;
	};

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("mana_below is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("mana_below"), Named));
	TestEqual(TEXT("and it is the mana-below reading"), static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::ManaBelowPercent));

	const ECataclysmStatCondition Below = ECataclysmStatCondition::ManaBelowPercent;
	TestTrue(TEXT("9.9% of mana is below 10%"),
		FPipeline::ConditionHolds(Below, 10.0f, Mana(9.9f)));
	TestFalse(TEXT("exactly 10% is not below 10%"),
		FPipeline::ConditionHolds(Below, 10.0f, Mana(10.0f)));
	TestFalse(TEXT("and 50% is not"),
		FPipeline::ConditionHolds(Below, 10.0f, Mana(50.0f)));
	TestTrue(TEXT("an empty pool is below 10%"),
		FPipeline::ConditionHolds(Below, 10.0f, Mana(0.0f)));

	// -1 IS BELOW EVERY THRESHOLD, WHICH IS WHY IT MUST REFUSE ON PURPOSE. It is
	// what an unknown reading and a character with no maximum mana both read.
	TestFalse(TEXT("an unknown reading, or no pool at all, is never below"),
		FPipeline::ConditionHolds(Below, 10.0f, Mana(-1.0f)));

	// AND A SHEET MAY WRITE ONLY A SHARE.
	FCataclysmStatModifier Modifier;
	Modifier.Bucket = ECataclysmStatBucket::Flat;
	Modifier.Source = ECataclysmModifierSource::Enchantment;
	Modifier.Value = 1.0f;
	Modifier.Condition = Below;
	Modifier.ConditionValue = 10.0f;
	TestTrue(TEXT("a threshold of 10% is accepted"),
		FPipeline::ValidateModifier(Modifier).IsEmpty());
	Modifier.ConditionValue = 150.0f;
	TestFalse(TEXT("and one of 150% is refused"),
		FPipeline::ValidateModifier(Modifier).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFirstHitConditionsTest,
	"Cataclysm.StatPipeline.TheFirstHitConditionsRefuseAnUnreadTargetAndHoldOnlyBeforeTheFirstHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `target_not_yet_struck_by_you` and `target_not_yet_crit_by_you` hold only
 * while the target's record was read and does not hold the asker. Issue #1815.
 *
 * THE FIRST CASE IS THE ONE A PLAIN NEGATION GETS WRONG. A lookup with no target
 * leaves the record unread, and "not struck" must refuse there rather than call
 * everything unstruck -- a character sheet would otherwise show the first-hit
 * bonus permanently.
 */
bool FCataclysmFirstHitConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("target_not_yet_struck_by_you is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("target_not_yet_struck_by_you"), Named)
			&& Named == ECataclysmStatCondition::TargetNotYetStruckByYou);
	TestTrue(TEXT("and so is target_not_yet_crit_by_you"),
		FPipeline::ConditionNamed(TEXT("target_not_yet_crit_by_you"), Named)
			&& Named == ECataclysmStatCondition::TargetNotYetCritByYou);
	TestFalse(TEXT("neither compares a value"),
		FPipeline::ConditionTakesAValue(ECataclysmStatCondition::TargetNotYetStruckByYou)
			|| FPipeline::ConditionTakesAValue(ECataclysmStatCondition::TargetNotYetCritByYou));

	const ECataclysmStatCondition Struck = ECataclysmStatCondition::TargetNotYetStruckByYou;
	const ECataclysmStatCondition Crit = ECataclysmStatCondition::TargetNotYetCritByYou;

	FCataclysmStatConditions Unread;
	TestFalse(TEXT("an unread target is not called unstruck"),
		FPipeline::ConditionHolds(Struck, 0.0f, Unread));
	TestFalse(TEXT("nor un-crit"), FPipeline::ConditionHolds(Crit, 0.0f, Unread));

	FCataclysmStatConditions Fresh;
	Fresh.bTargetStrikeHistoryKnown = true;
	TestTrue(TEXT("a target nobody has struck is not yet struck by you"),
		FPipeline::ConditionHolds(Struck, 0.0f, Fresh));
	TestTrue(TEXT("nor crit by you"), FPipeline::ConditionHolds(Crit, 0.0f, Fresh));

	FCataclysmStatConditions HitOnce = Fresh;
	HitOnce.bTargetStruckByYou = true;
	TestFalse(TEXT("once struck, the first-hit condition refuses"),
		FPipeline::ConditionHolds(Struck, 0.0f, HitOnce));
	TestTrue(TEXT("while the first-crit one still holds, as no crit landed"),
		FPipeline::ConditionHolds(Crit, 0.0f, HitOnce));

	FCataclysmStatConditions CritOnce = HitOnce;
	CritOnce.bTargetCritByYou = true;
	TestFalse(TEXT("and once crit, the first-crit condition refuses too"),
		FPipeline::ConditionHolds(Crit, 0.0f, CritOnce));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatConditionsTest,
	"Cataclysm.StatPipeline.TheCombatConditionsRefuseAnUnreadCharacterAndHoldOnOneSideEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `in_combat` and `out_of_combat` each hold on their own side of the line, and
 * neither holds for a character that cannot be read. Issue #1815.
 *
 * THE UNREAD CASE IS THE ONE A PLAIN NEGATION GETS WRONG. Were `out_of_combat`
 * written as "not in combat", a character sheet with no character in hand would
 * show the out-of-combat bonus permanently.
 *
 * AND THE TWO SCALES READ THE SAME TWO FIELDS, each granting nothing on the
 * other side.
 */
bool FCataclysmCombatConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("in_combat is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("in_combat"), Named)
			&& Named == ECataclysmStatCondition::InCombat);
	TestTrue(TEXT("and so is out_of_combat"),
		FPipeline::ConditionNamed(TEXT("out_of_combat"), Named)
			&& Named == ECataclysmStatCondition::OutOfCombat);
	TestFalse(TEXT("neither compares a value"),
		FPipeline::ConditionTakesAValue(ECataclysmStatCondition::InCombat)
			|| FPipeline::ConditionTakesAValue(ECataclysmStatCondition::OutOfCombat));

	const ECataclysmStatCondition In = ECataclysmStatCondition::InCombat;
	const ECataclysmStatCondition Out = ECataclysmStatCondition::OutOfCombat;

	FCataclysmStatConditions Unread;
	TestFalse(TEXT("an unread character is not in combat"),
		FPipeline::ConditionHolds(In, 0.0f, Unread));
	TestFalse(TEXT("nor out of it"), FPipeline::ConditionHolds(Out, 0.0f, Unread));

	FCataclysmStatConditions Fighting;
	Fighting.SecondsInCombat = 2.0f;
	TestTrue(TEXT("two seconds into a combat is in combat"),
		FPipeline::ConditionHolds(In, 0.0f, Fighting));
	TestFalse(TEXT("and not out of it"), FPipeline::ConditionHolds(Out, 0.0f, Fighting));

	FCataclysmStatConditions Resting;
	Resting.SecondsOutOfCombat = 2.0f;
	TestTrue(TEXT("two seconds after a combat lapsed is out of combat"),
		FPipeline::ConditionHolds(Out, 0.0f, Resting));
	TestFalse(TEXT("and not in it"), FPipeline::ConditionHolds(In, 0.0f, Resting));

	ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("seconds_in_combat is a scale this build knows"),
		FPipeline::ScaleNamed(TEXT("seconds_in_combat"), Scale)
			&& Scale == ECataclysmStatScale::PerSecondInCombat);
	TestTrue(TEXT("and so is seconds_out_of_combat"),
		FPipeline::ScaleNamed(TEXT("seconds_out_of_combat"), Scale)
			&& Scale == ECataclysmStatScale::PerSecondOutOfCombat);

	FCataclysmStatModifier PerSecondIn;
	PerSecondIn.Bucket = ECataclysmStatBucket::Increased;
	PerSecondIn.Value = 10.0f;
	PerSecondIn.Scale = ECataclysmStatScale::PerSecondInCombat;
	PerSecondIn.ScaleStep = 1.0f;

	FCataclysmStatModifier PerSecondOut = PerSecondIn;
	PerSecondOut.Scale = ECataclysmStatScale::PerSecondOutOfCombat;

	// 4.9 SECONDS, SO ROUNDING UP OR TO NEAREST WOULD GIVE 50 AND NOT 40.
	FCataclysmStatConditions FourPointNineIn;
	FourPointNineIn.SecondsInCombat = 4.9f;
	TestEqual(TEXT("4.9 seconds in combat is four whole steps"),
		FPipeline::ScaledValue(PerSecondIn, FourPointNineIn), 40.0f, 0.001f);
	TestEqual(TEXT("and the out-of-combat scale grants nothing in combat"),
		FPipeline::ScaledValue(PerSecondOut, FourPointNineIn), 0.0f, 0.001f);

	FCataclysmStatConditions FourPointNineOut;
	FourPointNineOut.SecondsOutOfCombat = 4.9f;
	TestEqual(TEXT("4.9 seconds out of combat is four whole steps"),
		FPipeline::ScaledValue(PerSecondOut, FourPointNineOut), 40.0f, 0.001f);
	TestEqual(TEXT("and the in-combat scale grants nothing out of it"),
		FPipeline::ScaledValue(PerSecondIn, FourPointNineOut), 0.0f, 0.001f);

	TestEqual(TEXT("an unread character scales to nothing either way"),
		FPipeline::ScaledValue(PerSecondIn, Unread)
			+ FPipeline::ScaledValue(PerSecondOut, Unread), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmScaleCapTest,
	"Cataclysm.StatPipeline.AScaleCapStopsTheStepsAndZeroMeansNoCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `ScaleMaxSteps` stops a scaled value growing, counted in steps, and zero
 * leaves it uncapped. Issue #1815: "up to 10 stacks", and "for every 10 seconds
 * ..., up to 60 seconds", a step of 10 and a cap of 6.
 *
 * TWENTY-FIVE SECONDS, FAR ABOVE EVERY CAP HERE, so a build that ignored the cap
 * answers 250 where the cap answers 100, and a reading BELOW the cap shows the
 * cap does not flatten what it should leave alone.
 */
bool FCataclysmScaleCapTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	FCataclysmStatModifier PerSecond;
	PerSecond.Bucket = ECataclysmStatBucket::Increased;
	PerSecond.Value = 10.0f;
	PerSecond.Scale = ECataclysmStatScale::PerSecondInCombat;
	PerSecond.ScaleStep = 1.0f;

	FCataclysmStatConditions Long;
	Long.SecondsInCombat = 25.0f;
	FCataclysmStatConditions Short;
	Short.SecondsInCombat = 4.0f;

	TestEqual(TEXT("with no cap, 25 seconds is 25 steps"),
		FPipeline::ScaledValue(PerSecond, Long), 250.0f, 0.001f);

	FCataclysmStatModifier UpToTen = PerSecond;
	UpToTen.ScaleMaxSteps = 10;
	TestEqual(TEXT("up to 10 stacks stops at 10 steps"),
		FPipeline::ScaledValue(UpToTen, Long), 100.0f, 0.001f);
	TestEqual(TEXT("and leaves 4 steps as 4"),
		FPipeline::ScaledValue(UpToTen, Short), 40.0f, 0.001f);

	// A NEGATIVE VALUE IS CAPPED BY SIZE, so a reduction stops too.
	FCataclysmStatModifier Reduction = UpToTen;
	Reduction.Value = -3.0f;
	TestEqual(TEXT("a reduction up to 10 stacks stops at -30"),
		FPipeline::ScaledValue(Reduction, Long), -30.0f, 0.001f);

	// "FOR EVERY 10 SECONDS ..., UP TO 60 SECONDS" is a step of 10 and a cap of 6.
	FCataclysmStatModifier PerTen = PerSecond;
	PerTen.Value = 5.0f;
	PerTen.ScaleStep = 10.0f;
	PerTen.ScaleMaxSteps = 6;
	FCataclysmStatConditions VeryLong;
	VeryLong.SecondsInCombat = 95.0f;
	TestEqual(TEXT("95 seconds at a step of 10 and a cap of 6 is 6 steps"),
		FPipeline::ScaledValue(PerTen, VeryLong), 30.0f, 0.001f);

	// AND THE CAP IS WHAT THE BUCKETS SEE, not only what this function answers.
	const FCataclysmStatBreakdown Evaluated = FPipeline::Evaluate(
		100.0f, {UpToTen}, FGameplayTagContainer(), Long);
	TestEqual(TEXT("the evaluated stat carries the capped increase"),
		Evaluated.Final, 200.0f, 0.001f);

	FCataclysmStatModifier NegativeCap = UpToTen;
	NegativeCap.ScaleMaxSteps = -1;
	TestFalse(TEXT("a negative cap is refused"),
		FPipeline::ValidateModifier(NegativeCap).IsEmpty());

	FCataclysmStatModifier CapOnFixed;
	CapOnFixed.Bucket = ECataclysmStatBucket::Increased;
	CapOnFixed.Value = 10.0f;
	CapOnFixed.ScaleMaxSteps = 5;
	TestFalse(TEXT("a cap on a value that does not scale is refused"),
		FPipeline::ValidateModifier(CapOnFixed).IsEmpty());

	TestTrue(TEXT("and a cap on a scaled value is accepted"),
		FPipeline::ValidateModifier(UpToTen).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetDebuffConditionsTest,
	"Cataclysm.StatPipeline.TheTargetDebuffConditionsRefuseAnUnreadTargetAndAStunIsNotADamageOverTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `target_carries_any_debuff` holds for any debuff on the target, and
 * `target_carries_a_dot` only for a damage over time. Issue #1815.
 *
 * THE STUN IS THE CASE THAT SEPARATES THEM. It is a debuff and deals no damage,
 * so a build that answered the second condition with the first would pass every
 * bleeding case and fail this one.
 */
bool FCataclysmTargetDebuffConditionsTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("target_carries_any_debuff is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("target_carries_any_debuff"), Named)
			&& Named == ECataclysmStatCondition::TargetCarriesAnyDebuff);
	TestTrue(TEXT("and so is target_carries_a_dot"),
		FPipeline::ConditionNamed(TEXT("target_carries_a_dot"), Named)
			&& Named == ECataclysmStatCondition::TargetCarriesADot);
	TestFalse(TEXT("neither compares a value"),
		FPipeline::ConditionTakesAValue(ECataclysmStatCondition::TargetCarriesAnyDebuff)
			|| FPipeline::ConditionTakesAValue(ECataclysmStatCondition::TargetCarriesADot));

	const ECataclysmStatCondition Any = ECataclysmStatCondition::TargetCarriesAnyDebuff;
	const ECataclysmStatCondition Dot = ECataclysmStatCondition::TargetCarriesADot;

	const FGameplayTag Stunned = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Stunned")), /*ErrorIfNotFound=*/false);
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	if (!TestTrue(TEXT("the stun and bleed tags exist"), Stunned.IsValid() && Bleed.IsValid()))
	{
		return false;
	}

	FCataclysmStatConditions Unread;
	TestFalse(TEXT("an unread target carries no debuff"),
		FPipeline::ConditionHolds(Any, 0.0f, Unread));
	TestFalse(TEXT("and no damage over time"), FPipeline::ConditionHolds(Dot, 0.0f, Unread));

	FCataclysmStatConditions StunnedOnly;
	StunnedOnly.TargetDebuffs.AddTag(Stunned);
	TestTrue(TEXT("a stunned target carries a debuff"),
		FPipeline::ConditionHolds(Any, 0.0f, StunnedOnly));
	TestFalse(TEXT("and a stun is not a damage over time"),
		FPipeline::ConditionHolds(Dot, 0.0f, StunnedOnly));

	FCataclysmStatConditions Bleeding;
	Bleeding.TargetDebuffs.AddTag(Bleed);
	TestTrue(TEXT("a bleeding target carries a debuff"),
		FPipeline::ConditionHolds(Any, 0.0f, Bleeding));
	TestTrue(TEXT("and a damage over time"), FPipeline::ConditionHolds(Dot, 0.0f, Bleeding));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffBuffManaScalesTest,
	"Cataclysm.StatPipeline.TheTargetDebuffBuffAndManaScalesCountWholeThings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `target_debuffs`, `buffs_held` and `mana_held_percent` each multiply a value by whole
 * steps of their reading. Issue #1815.
 *
 * 250.7 MANA, SO ROUNDING UP OR TO NEAREST WOULD GIVE A DIFFERENT ANSWER, and a
 * value of 0.2 a point is the shape "10%-30% of your current mana" is written in.
 */
bool FCataclysmDebuffBuffManaScalesTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("target_debuffs is a scale this build knows"),
		FPipeline::ScaleNamed(TEXT("target_debuffs"), Scale)
			&& Scale == ECataclysmStatScale::PerTargetDebuff);
	TestTrue(TEXT("and buffs_held"),
		FPipeline::ScaleNamed(TEXT("buffs_held"), Scale)
			&& Scale == ECataclysmStatScale::PerBuffHeld);
	TestTrue(TEXT("and mana_held_percent"),
		FPipeline::ScaleNamed(TEXT("mana_held_percent"), Scale)
			&& Scale == ECataclysmStatScale::PercentOfManaHeld);

	FCataclysmStatModifier Per;
	Per.Bucket = ECataclysmStatBucket::Increased;
	Per.Value = 5.0f;
	Per.ScaleStep = 1.0f;

	FCataclysmStatConditions State;
	State.TargetDebuffs.AddTag(UCataclysmDebuffs::BleedTag());
	State.TargetDebuffs.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Stunned")), /*ErrorIfNotFound=*/false));
	State.BuffsHeld = 3;
	State.ManaHeld = 250.7f;

	FCataclysmStatModifier PerDebuff = Per;
	PerDebuff.Scale = ECataclysmStatScale::PerTargetDebuff;
	TestEqual(TEXT("two debuffs on the target is two steps"),
		FPipeline::ScaledValue(PerDebuff, State), 10.0f, 0.001f);

	FCataclysmStatModifier PerBuff = Per;
	PerBuff.Scale = ECataclysmStatScale::PerBuffHeld;
	TestEqual(TEXT("three buffs held is three steps"),
		FPipeline::ScaledValue(PerBuff, State), 15.0f, 0.001f);

	FCataclysmStatModifier PerMana = Per;
	PerMana.Bucket = ECataclysmStatBucket::Flat;
	PerMana.Value = 20.0f;
	PerMana.Scale = ECataclysmStatScale::PercentOfManaHeld;
	TestEqual(TEXT("20% of 250.7 mana is 20% of 250 whole points, 50"),
		FPipeline::ScaledValue(PerMana, State), 50.0f, 0.001f);

	// AND ITS CAP COUNTS STEPS, NOT THE VALUE. Ten steps of 20% are 2, where a
	// cap reading "value per step" would allow 200.
	FCataclysmStatModifier CappedMana = PerMana;
	CappedMana.ScaleMaxSteps = 10;
	TestEqual(TEXT("capped at ten points of mana, 20% of them is 2"),
		FPipeline::ScaledValue(CappedMana, State), 2.0f, 0.001f);

	FCataclysmStatConditions Unread;
	TestEqual(TEXT("an unread character scales to nothing on all three"),
		FPipeline::ScaledValue(PerDebuff, Unread) + FPipeline::ScaledValue(PerBuff, Unread)
			+ FPipeline::ScaledValue(PerMana, Unread), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPipelineTwoHandedTest,
	"Cataclysm.StatPipeline.WieldingTwoHandedHoldsOnlyForTwoHandsAndRefusesAnUnknownWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `wielding_two_handed_weapon`, for the Two Hands node. Issue #1515.
 *
 * THREE READINGS: a weapon of two hands holds; a weapon of one does not; and
 * -1, what no weapon, no table and a weapon type with no base row all read,
 * refuses rather than being guessed.
 */
bool FCataclysmPipelineTwoHandedTest::RunTest(const FString&)
{
	using FPipeline = UCataclysmStatPipeline;

	const auto Hands = [](int32 Count)
	{
		FCataclysmStatConditions State;
		State.WeaponHands = Count;
		return State;
	};

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("wielding_two_handed_weapon is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("wielding_two_handed_weapon"), Named));
	TestEqual(TEXT("and it is the two-handed reading"), static_cast<int32>(Named),
		static_cast<int32>(ECataclysmStatCondition::WieldingTwoHandedWeapon));

	const ECataclysmStatCondition TwoHanded =
		ECataclysmStatCondition::WieldingTwoHandedWeapon;
	TestTrue(TEXT("a weapon of two hands holds"),
		FPipeline::ConditionHolds(TwoHanded, 0.0f, Hands(2)));
	TestFalse(TEXT("a weapon of one hand does not"),
		FPipeline::ConditionHolds(TwoHanded, 0.0f, Hands(1)));
	TestFalse(TEXT("and an unknown weapon refuses"),
		FPipeline::ConditionHolds(TwoHanded, 0.0f, Hands(-1)));
	TestFalse(TEXT("it compares no value"),
		FPipeline::ConditionTakesAValue(TwoHanded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamagedByYouConditionTest,
	"Cataclysm.StatPipeline.DamagedByYouHoldsWithinItsSecondsInclusiveAndRefusesAnUnreadOrUnstruckTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `target_damaged_by_you_within_seconds`, the condition Set Upon and Set the
 * Pack On ask: "enemies you have damaged in the last 2 seconds". Issue #1515.
 *
 * THE REFUSALS ARE THE HALF WORTH WRITING. A target nobody read and a target
 * never struck both carry -1 or nothing, and a comparison with no guard in
 * front of it would hand the bonus to every enemy in the game.
 */
bool FCataclysmDamagedByYouConditionTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatCondition Named = ECataclysmStatCondition::Always;
	TestTrue(TEXT("target_damaged_by_you_within_seconds is a name this build knows"),
		FPipeline::ConditionNamed(TEXT("target_damaged_by_you_within_seconds"), Named)
			&& Named == ECataclysmStatCondition::TargetDamagedByYouWithinSeconds);
	TestTrue(TEXT("and it compares a number of seconds"),
		FPipeline::ConditionTakesAValue(
			ECataclysmStatCondition::TargetDamagedByYouWithinSeconds));

	const ECataclysmStatCondition Window =
		ECataclysmStatCondition::TargetDamagedByYouWithinSeconds;
	const auto Read = [](float Seconds)
	{
		FCataclysmStatConditions State;
		State.bTargetStrikeHistoryKnown = true;
		State.SecondsSinceStruckByYou = Seconds;
		return State;
	};

	// TWO SECONDS IS THE ROWS' FIGURE.
	TestTrue(TEXT("a target you struck this instant is inside the window"),
		FPipeline::ConditionHolds(Window, 2.0f, Read(0.0f)));
	TestTrue(TEXT("and one struck exactly two seconds ago is, because within is inclusive"),
		FPipeline::ConditionHolds(Window, 2.0f, Read(2.0f)));
	TestFalse(TEXT("and one struck 2.1 seconds ago is not"),
		FPipeline::ConditionHolds(Window, 2.0f, Read(2.1f)));
	TestFalse(TEXT("a target you never struck refuses"),
		FPipeline::ConditionHolds(Window, 2.0f, Read(-1.0f)));

	// AND AN UNREAD TARGET REFUSES EVEN WITH A READING IN THE FIELD, which is
	// the guard the known flag exists for: a lookup with no target in hand must
	// not answer from a field nobody filled.
	FCataclysmStatConditions Unread;
	Unread.SecondsSinceStruckByYou = 0.5f;
	TestFalse(TEXT("an unread target refuses whatever the field holds"),
		FPipeline::ConditionHolds(Window, 2.0f, Unread));

	// AND IT READS ITS OWN FIELD, NOT THE FIRST-HIT FLAG BESIDE IT. A build
	// reading `bTargetStruckByYou` would hold here, for a blow of any age.
	FCataclysmStatConditions StruckLongAgo = Read(-1.0f);
	StruckLongAgo.bTargetStruckByYou = true;
	TestFalse(TEXT("being on the record at all is not being struck within the window"),
		FPipeline::ConditionHolds(Window, 2.0f, StruckLongAgo));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraSingleTargetCrowdControlTest,
	"Cataclysm.StatPipeline.AurasRunningASingleTargetAndACrowdControlledAttackerAreRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three readings issue #1686's first window adds. `auras_held` multiplies
 * by the auras running; `enemies_hit_at_most` holds at or below its count and
 * refuses an unknown one; `opponent_is_crowd_controlled` reads the blow's own
 * field and nothing else.
 *
 * AT MOST ONE IS TESTED AGAINST -1 ON PURPOSE. -1 is below one by arithmetic,
 * so a predicate that forgot the "no attack in hand" refusal would pass every
 * other line here and put the drawback on every creature's blow.
 */
bool FCataclysmAuraSingleTargetCrowdControlTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("auras_held is a scale this build knows"),
		FPipeline::ScaleNamed(TEXT("auras_held"), Scale)
			&& Scale == ECataclysmStatScale::PerAuraHeld);

	ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;
	TestTrue(TEXT("enemies_hit_at_most is a condition this build knows"),
		FPipeline::ConditionNamed(TEXT("enemies_hit_at_most"), Condition)
			&& Condition == ECataclysmStatCondition::EnemiesStruckTogetherAtMost);
	TestTrue(TEXT("and opponent_is_crowd_controlled"),
		FPipeline::ConditionNamed(TEXT("opponent_is_crowd_controlled"), Condition)
			&& Condition == ECataclysmStatCondition::OpponentIsCrowdControlled);

	// THE AURAS. Two because two can run, and a self buff beside them does not
	// count, which is the line that tells the two counts apart.
	FCataclysmStatModifier PerAura;
	PerAura.Bucket = ECataclysmStatBucket::More;
	PerAura.Value = 10.0f;
	PerAura.Scale = ECataclysmStatScale::PerAuraHeld;
	PerAura.ScaleStep = 1.0f;

	FCataclysmStatConditions Auras;
	Auras.AurasHeld = 2;
	Auras.BuffsHeld = 3;
	TestEqual(TEXT("two auras running is two steps, and the buffs are not auras"),
		FPipeline::ScaledValue(PerAura, Auras), 20.0f, 0.001f);
	TestEqual(TEXT("no aura running is nothing"),
		FPipeline::ScaledValue(PerAura, FCataclysmStatConditions()), 0.0f, 0.001f);

	// A SINGLE TARGET.
	FCataclysmStatModifier AtMostOne = MoreFromGem(-20.0f);
	AtMostOne.Condition = ECataclysmStatCondition::EnemiesStruckTogetherAtMost;
	AtMostOne.ConditionValue = 1.0f;
	const TArray<FCataclysmStatModifier> Single = { AtMostOne };

	TestEqual(TEXT("one enemy struck takes the drawback"),
		FPipeline::Evaluate(100.0f, Single, NoTags, StruckTogether(1)).Final,
		80.0f, 0.01f);
	TestEqual(TEXT("two enemies struck do not"),
		FPipeline::Evaluate(100.0f, Single, NoTags, StruckTogether(2)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("an unknown count refuses, though -1 is below one"),
		FPipeline::Evaluate(100.0f, Single, NoTags, StruckTogether(-1)).Final,
		100.0f, 0.01f);
	TestEqual(TEXT("and so does a state nobody filled in"),
		FPipeline::Evaluate(100.0f, Single, NoTags, FCataclysmStatConditions()).Final,
		100.0f, 0.01f);

	TestFalse(TEXT("a count of nothing refuses, since no blow comes from an attack on none"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::EnemiesStruckTogetherAtMost,
			0.0f, StruckTogether(0)));

	// THE CROWD CONTROLLED ATTACKER. The blow's field, and not the staggered one
	// beside it.
	FCataclysmStatConditions Held;
	Held.Blow.bOpponentIsCrowdControlled = true;
	FCataclysmStatConditions Staggered;
	Staggered.Blow.bOpponentIsStaggered = true;
	TestTrue(TEXT("a crowd controlled attacker answers yes"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::OpponentIsCrowdControlled,
			0.0f, Held));
	TestFalse(TEXT("a staggered one does not, for stagger is not crowd control"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::OpponentIsCrowdControlled,
			0.0f, Staggered));
	TestFalse(TEXT("and no blow in hand answers no"),
		FPipeline::ConditionHolds(ECataclysmStatCondition::OpponentIsCrowdControlled,
			0.0f, FCataclysmStatConditions()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassPointScaleTest,
	"Cataclysm.StatPipeline.AClassPointRowCountsOnlyThePointsAboveItsOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `class_points_spent` with a Scale Offset. Issue #1686: "for every 10 class
 * points spent above 100" is a step of 10 and an offset of 100.
 *
 * 159 POINTS, SO ROUNDING THE STEPS UP OR TO NEAREST GIVES SIX, and whole
 * steps give five. At and below the offset is nothing, never a negative number
 * of steps; an unknown count is nothing.
 */
bool FCataclysmClassPointScaleTest::RunTest(const FString&)
{
	using namespace CataclysmStatTest;

	ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;
	TestTrue(TEXT("class_points_spent is a scale this build knows"),
		FPipeline::ScaleNamed(TEXT("class_points_spent"), Scale)
			&& Scale == ECataclysmStatScale::PerClassPointSpent);

	FCataclysmStatModifier PerTen;
	PerTen.Bucket = ECataclysmStatBucket::More;
	PerTen.Value = -2.5f;
	PerTen.Scale = ECataclysmStatScale::PerClassPointSpent;
	PerTen.ScaleStep = 10.0f;
	PerTen.ScaleOffset = 100.0f;

	const auto At = [&PerTen](int32 Points)
	{
		FCataclysmStatConditions State;
		State.ClassPointsSpent = Points;
		return FPipeline::ScaledValue(PerTen, State);
	};

	TestEqual(TEXT("159 points are 59 above 100: five whole steps"), At(159), -12.5f, 0.001f);
	TestEqual(TEXT("exactly 100 is nothing"), At(100), 0.0f, 0.001f);
	TestEqual(TEXT("below 100 is nothing, not a negative number of steps"), At(40), 0.0f, 0.001f);
	TestEqual(TEXT("an unknown count is nothing"), At(-1), 0.0f, 0.001f);
	TestEqual(TEXT("and so is a state nobody filled in"),
		FPipeline::ScaledValue(PerTen, FCataclysmStatConditions()), 0.0f, 0.001f);

	FCataclysmStatModifier NoOffset = PerTen;
	NoOffset.ScaleOffset = 0.0f;
	FCataclysmStatConditions Spent;
	Spent.ClassPointsSpent = 159;
	TestEqual(TEXT("with no offset, 159 points are fifteen steps"),
		FPipeline::ScaledValue(NoOffset, Spent), -37.5f, 0.001f);

	// AND WHAT VALIDATION REFUSES: an offset on a scale that does not read one,
	// and a negative offset.
	TestTrue(TEXT("the class point row itself is valid"),
		FPipeline::ValidateModifier(PerTen).IsEmpty());
	FCataclysmStatModifier Elsewhere = PerTen;
	Elsewhere.Scale = ECataclysmStatScale::PerDebuffCarried;
	TestFalse(TEXT("an offset on another scale is refused"),
		FPipeline::ValidateModifier(Elsewhere).IsEmpty());
	FCataclysmStatModifier Negative = PerTen;
	Negative.ScaleOffset = -1.0f;
	TestFalse(TEXT("a negative offset is refused"),
		FPipeline::ValidateModifier(Negative).IsEmpty());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
