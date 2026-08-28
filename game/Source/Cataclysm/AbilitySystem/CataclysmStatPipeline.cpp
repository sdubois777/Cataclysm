// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Cataclysm.h"

FGameplayTag UCataclysmStatPipeline::GlobalScopeTag()
{
	// Requested rather than constructed, so that a typo here fails as an
	// invalid tag instead of silently matching nothing. The tag is declared in
	// game/Config/Tags/CataclysmTags.ini, generated from the design workbook.
	static const FGameplayTag Tag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Scope.Global")),
										 /*ErrorIfNotFound=*/false);
	return Tag;
}

bool UCataclysmStatPipeline::CanGrantMore(ECataclysmModifierSource Source)
{
	switch (Source)
	{
	case ECataclysmModifierSource::Gem:
	case ECataclysmModifierSource::PassiveKeystone:
	case ECataclysmModifierSource::Enchantment:

	// A skill's own buff is authored the way those three are, not rolled, so
	// the readability rule below does not apply to it.
	case ECataclysmModifierSource::SkillBuff:
		return true;

	// An ordinary affix is flat or increased and never more. That is what keeps
	// a rare drop readable, and it is the reason an enchantment is worth a slot
	// that an affix could have taken.
	case ECataclysmModifierSource::GearAffix:
	case ECataclysmModifierSource::GearImplicit:
	case ECataclysmModifierSource::Attribute:
	default:
		return false;
	}
}

bool UCataclysmStatPipeline::ConditionHolds(ECataclysmStatCondition Condition,
										   float Value,
										   const FCataclysmStatConditions& State)
{
	switch (Condition)
	{
	case ECataclysmStatCondition::Always:
		return true;

	case ECataclysmStatCondition::HealthAtOrBelowPercent:
		// AN UNKNOWN STATE REFUSES. A caller with no character in hand -- the
		// character sheet, or a test passing plain numbers -- must not be handed
		// a bonus that depends on where the character's health is. Issue #959.
		//
		// AT OR BELOW, NOT BELOW. The seven nodes that take this predicate are
		// all written "While at or below 20% health", so a character sitting
		// exactly on the number gets the bonus.
		//
		// THIS COMMENT USED TO SAY EVERY NODE STATING A HEALTH THRESHOLD WAS
		// WRITTEN THAT WAY, and issue #1051 is where that stopped being true.
		// The Final Vow's first option says "While below 20% health" and takes
		// the predicate below instead. A comment claiming a shape is universal
		// is read as a reason not to look for the exception.
		return State.HealthPercent >= 0.0f && State.HealthPercent <= Value;

	case ECataclysmStatCondition::HealthBelowPercent:
		// STRICTLY BELOW, WHICH IS THE WHOLE DIFFERENCE FROM THE PREDICATE
		// ABOVE. Issue #1051. A character on exactly 20% health gets nothing
		// from The Last Drop and does get the bonus from every "at or below 20%"
		// node, which is what the two sentences say.
		//
		// AN UNKNOWN STATE REFUSES, the same as above and for the same reason.
		// Note that this cannot be folded into the comparison the way "at or
		// below" nearly could: an unknown health reads -1, which IS strictly
		// below every threshold, so dropping the first half here would hand the
		// bonus to the character sheet and to every caller with no character.
		return State.HealthPercent >= 0.0f && State.HealthPercent < Value;

	case ECataclysmStatCondition::HealthAbovePercent:
		// STRICTLY ABOVE, AND THE ONLY HEALTH PREDICATE THAT POINTS UPWARDS.
		// Issue #1070. Ceaseless Penance reads "while you are above 50% health",
		// so a character sitting exactly on half health is not above it and its
		// debuffs expire normally.
		//
		// AN UNKNOWN STATE REFUSES, and the guard is written out rather than
		// left to the comparison. An unknown health reads -1, which is not above
		// any threshold, so the second half alone would already answer no -- by
		// accident. The character sheet has to be refused on purpose, the same
		// way the two predicates above refuse it.
		return State.HealthPercent >= 0.0f && State.HealthPercent > Value;

	case ECataclysmStatCondition::WithinSecondsOfHealthCost:
		// A NEGATIVE READING IS "NEVER PAID ONE, OR NOT KNOWN", and both answer
		// no, so they do not have to be told apart. Issue #962.
		//
		// THE WINDOW INCLUDES ITS LAST INSTANT, matching "at or below" above. A
		// node saying "for 2 seconds after" covers the moment exactly two
		// seconds later, which no player can time and which keeps the two
		// predicates from disagreeing about their own boundaries.
		return State.SecondsSinceHealthCost >= 0.0f
			&& State.SecondsSinceHealthCost <= Value;

	case ECataclysmStatCondition::WithinSecondsOfForeignDamage:
		// THE SAME TWO RULES AS THE WINDOW ABOVE: a negative reading means
		// never or not known, and the window includes its last instant.
		// Issue #975.
		return State.SecondsSinceForeignDamage >= 0.0f
			&& State.SecondsSinceForeignDamage <= Value;

	case ECataclysmStatCondition::SkillHealthCostAbovePercent:
		// STRICTLY ABOVE, WHICH IS THE OPPOSITE BOUNDARY FROM EVERY OTHER
		// PREDICATE HERE, and it is read straight off the design's own words:
		// "A skill whose health cost is above 10% of your maximum health".
		// Issue #983.
		//
		// THE BOUNDARY IS REACHABLE RATHER THAN THEORETICAL. Deeper Cuts at its
		// full ten points adds exactly 10% of maximum health to every skill, so
		// a character with that node and no skill of its own cost sits precisely
		// on the number. Copying the "at or below" form from the neighbouring
		// case would hand that character the bonus the design withholds.
		//
		// A NEGATIVE READING IS "NO SKILL IN HAND" AND REFUSES, the same as
		// every other unknown here. It cannot be confused with a real answer:
		// a cost is never negative, and a threshold is never below zero.
		return State.SkillHealthCostPercent >= 0.0f
			&& State.SkillHealthCostPercent > Value;

	case ECataclysmStatCondition::WhileBleeding:
		// NO THRESHOLD, SO `Value` IS NOT READ. Issue #962. The other four
		// predicates compare a reading against a number; this one asks whether
		// the character is carrying a kind of effect, and the kind is the
		// enumerator. `tools/generate_datatables.py` refuses to write a value on
		// a row carrying this, so ignoring it here cannot hide one.
		//
		// NOTHING TO REFUSE AS UNKNOWN, WHICH IS WHY THERE IS NO NEGATIVE GUARD.
		// A caller with no character in hand is not bleeding and neither is an
		// unhurt character, and a bonus that applies while bleeding is correctly
		// withheld from both. The readings above need the distinction because a
		// health percentage of zero is a corpse and an unknown one is the
		// character sheet; there is no such pair here.
		return State.bIsBleeding;

	case ECataclysmStatCondition::ClassResourceAtMaximum:
		// NO THRESHOLD, SO `Value` IS NOT READ, the same as the predicate above.
		// Issue #1026. "While your Fervour is at maximum" names the top of the
		// bar rather than a number, and the tool refuses a value on a row
		// carrying this.
		//
		// BOTH READINGS HAVE TO BE KNOWN, AND THAT IS WHAT REFUSES AN ENEMY. An
		// ability system with no class resource attribute set leaves both
		// negative, and this is the one place where "there is no bar" and "the
		// bar is empty" have to be told apart: an unknown pair would otherwise
		// compare -1 against -1 and answer yes, handing every enemy in the game
		// a bonus written for a full Masochist.
		//
		// A MAXIMUM OF NOTHING REFUSES TOO. A pool that cannot hold anything is
		// not at its maximum in any sense a node means, and answering yes would
		// give the bonus to a character that has never generated a point.
		return State.ClassResourceHeld >= 0.0f
			&& State.ClassResourceMaximum > 0.0f
			&& State.ClassResourceHeld >= State.ClassResourceMaximum;
	}

	// A CONDITION THIS BUILD DOES NOT KNOW REFUSES rather than applying. A saved
	// or imported modifier naming one is a modifier this build cannot judge, and
	// granting it would be granting something unread.
	return false;
}

namespace
{
	/**
	 * What a modifier is worth at this many stacks. Issues #1002 to #1004.
	 *
	 * SHARED BY ALL THREE STACK SCALES, because a stack count needs none of the
	 * arithmetic the other scales do. There is no reading to divide by a step
	 * and nothing to round: the count is already whole.
	 *
	 * THE STEP IS STILL HONOURED, so a bonus written "per 2 stacks" would work
	 * if the design ever asked for one. All three of today's nodes say "each
	 * stack", which is a step of one, and the data check refuses any other step
	 * for a sentence that names none.
	 *
	 * A STEP OF NOTHING IS WORTH NOTHING rather than dividing by zero, the same
	 * rule the three scales above follow.
	 */
	float StackedValue(const FCataclysmStatModifier& Modifier, int32 Stacks)
	{
		if (Stacks <= 0 || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		const float Steps = FMath::FloorToFloat(
			static_cast<float>(Stacks) / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}
}

float UCataclysmStatPipeline::ScaledValue(const FCataclysmStatModifier& Modifier,
										  const FCataclysmStatConditions& State)
{
	switch (Modifier.Scale)
	{
	case ECataclysmStatScale::Fixed:
		return Modifier.Value;

	case ECataclysmStatScale::PerPercentOfMaximumHealthMissing:
	{
		// AN UNKNOWN HEALTH READING SCALES TO NOTHING, for the reason it refuses
		// a condition: the character sheet has no character in hand, and a bonus
		// whose size depends on where health is must not be written onto a
		// gameplay attribute. Issue #968.
		//
		// AND A STEP OF NOTHING SCALES TO NOTHING, rather than dividing by zero.
		// `ValidateModifier` refuses it when data is imported; this is what
		// happens if one reaches the game anyway.
		if (State.HealthPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN. "For every 5% of your maximum health that
		// is missing" is a count of completed blocks: 12% down is two steps, not
		// two and two fifths. See the enumerator for the words and the genre
		// precedent this is read from.
		const float Missing = 100.0f - State.HealthPercent;
		const float Steps = FMath::FloorToFloat(Missing / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPointOfClassResourceHeld:
	{
		// THE SAME TWO REFUSALS AS THE READING ABOVE, and for the same reasons.
		// Issue #980. An ability system with no class resource attribute set --
		// every enemy in the game -- leaves the reading negative, and a step of
		// nothing would divide by zero.
		//
		// A HELD AMOUNT OF ZERO IS NOT A REFUSAL. It falls through and answers
		// zero by the arithmetic, which is the honest answer for an empty bar
		// rather than a special case.
		if (State.ClassResourceHeld < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the same rule as the reading above. The
		// design's own node uses a step of 1, where rounding cannot show, so the
		// rule is read off the other scale rather than off this node's words.
		const float Steps =
			FMath::FloorToFloat(State.ClassResourceHeld / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPercentOfMaximumHealthOwed:
	{
		// THE SAME TWO REFUSALS AGAIN. Issue #994. The reading is negative for a
		// caller with no character in hand, for an ability system with no class
		// resource attribute set, and for one whose maximum health is nothing;
		// all three are cases where the share cannot be worked out at all rather
		// than cases where it is zero.
		//
		// OWING NOTHING IS NOT A REFUSAL. A character at full health that has
		// deferred no cost reads zero and gets nothing by the arithmetic, which
		// is the honest answer and is what makes the node a reward for being in
		// debt rather than a flat bonus.
		if (State.HealthOwedPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the rule the other two follow. Compound
		// Interest reads "for every 5% of your maximum health you currently
		// owe", so owing 12% is two completed blocks rather than two and two
		// fifths.
		const float Steps =
			FMath::FloorToFloat(State.HealthOwedPercent / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	case ECataclysmStatScale::PerPercentOfLifeLeech:
	{
		// THE SAME TWO REFUSALS AS THE READINGS ABOVE. Issue #1045. The reading
		// is negative for a caller with no character in hand and for an ability
		// system with no vital attribute set, which is where life leech lives;
		// and a step of nothing would divide by zero.
		//
		// HAVING NONE IS NOT A REFUSAL. A character with no life leech reads
		// zero and gets nothing by the arithmetic, which is the honest answer
		// and is what makes Glutton a reward for investing in leech rather than
		// a flat bonus.
		if (State.LifeLeechPercent < 0.0f || Modifier.ScaleStep <= 0.0f)
		{
			return 0.0f;
		}

		// WHOLE STEPS, ROUNDED DOWN, the rule every scale here follows. Glutton
		// uses a step of 1, where rounding cannot show, so the rule is read off
		// the other scales rather than off this node's words.
		const float Steps =
			FMath::FloorToFloat(State.LifeLeechPercent / Modifier.ScaleStep);
		return Modifier.Value * FMath::Max(0.0f, Steps);
	}

	// THE THREE STACK COUNTS SHARE ONE PIECE OF ARITHMETIC, because a stack is
	// already a whole thing: there is no reading to divide and nothing to round.
	// Issues #1002, #1003 and #1004.
	case ECataclysmStatScale::PerStackOfSanguineMomentum:
		return StackedValue(Modifier, State.SanguineMomentumStacks);

	case ECataclysmStatScale::PerStackOfBloodlust:
		return StackedValue(Modifier, State.BloodlustStacks);

	case ECataclysmStatScale::PerStackOfCarnage:
		return StackedValue(Modifier, State.CarnageStacks);

	// AND THE DEBUFFS THE CHARACTER IS UNDER, COUNTED THE SAME WAY. Issue #962.
	// A debuff is a whole thing exactly as a stack is, so the arithmetic is
	// shared even though what is being counted is not a stack: a stack is an
	// event this project chose to remember, and a debuff is a gameplay effect
	// somebody applied that the ability system is already holding.
	case ECataclysmStatScale::PerDebuffCarried:
		return StackedValue(Modifier, State.DebuffsCarried);
	}

	// A SCALE THIS BUILD DOES NOT KNOW IS WORTH NOTHING rather than its full
	// value. The same argument `ConditionHolds` makes: granting the whole thing
	// would be granting something unread, silently and in the player's favour.
	return 0.0f;
}

bool UCataclysmStatPipeline::ModifierApplies(const FCataclysmStatModifier& Modifier,
											 const FGameplayTagContainer& SkillTags,
											 const FCataclysmStatConditions& State)
{
	// THE CHARACTER'S STATE FIRST, because it is the cheaper question and
	// because a modifier carrying both a condition and a required tag needs both.
	// Issue #959.
	if (!ConditionHolds(Modifier.Condition, Modifier.ConditionValue, State))
	{
		return false;
	}

	const FGameplayTag Global = GlobalScopeTag();

	for (const FGameplayTag& Required : Modifier.RequiredTags)
	{
		if (Global.IsValid() && Required == Global)
		{
			// Applies to everything, so it constrains nothing.
			continue;
		}

		// HasTag matches a held tag against the required tag's children as well,
		// so a skill tagged Type.AOE.PointBlank satisfies a requirement of
		// Type.AOE. That hierarchy is the reason the design's tags are dotted.
		if (!SkillTags.HasTag(Required))
		{
			return false;
		}
	}
	return true;
}

FString UCataclysmStatPipeline::ValidateModifier(const FCataclysmStatModifier& Modifier)
{
	if (Modifier.Bucket == ECataclysmStatBucket::More)
	{
		if (!CanGrantMore(Modifier.Source))
		{
			return FString::Printf(
				TEXT("a More multiplier from %s. Only a gem, a passive keystone, "
					 "an enchantment or a skill's own buff may grant one; "
					 "everything else is flat or increased."),
				*UEnum::GetValueAsString(Modifier.Source));
		}
		if (Modifier.Value <= -100.0f)
		{
			return FString::Printf(
				TEXT("a More multiplier of %.1f%% would zero or invert the "
					 "stat. A Less multiplier cannot reach -100%%."),
				Modifier.Value);
		}
	}

	// A HEALTH THRESHOLD OUTSIDE 0 TO 100 IS A MODIFIER THAT NEVER APPLIES OR
	// ALWAYS DOES, and either way it is not what was meant. Issue #959. Zero is
	// legitimate and means "only at exactly no health", which is unreachable in
	// play but is not a data error; 100 is legitimate and means "always", though
	// `Always` says that more plainly.
	//
	// BOTH HEALTH THRESHOLDS ARE BOUNDED HERE, since issue #1051. The strict one
	// reads the same percentage and the bound says the same thing about it. Its
	// endpoints mean the opposite of the other predicate's, which changes
	// nothing about the bound: 0 means "never", because nothing is strictly
	// below no health, and 100 means "always except at full health".
	//
	// ALL THREE, SINCE ISSUE #1070, and the third one is the reason to say this
	// out loud rather than to leave the list to be read. `HealthAbovePercent`
	// points the other way, and a threshold of 150 on it would be a modifier
	// that never applies -- the same silent failure, arrived at from the
	// opposite side. A bound written as a list is a bound somebody has to
	// remember to extend.
	if ((Modifier.Condition == ECataclysmStatCondition::HealthAtOrBelowPercent
		 || Modifier.Condition == ECataclysmStatCondition::HealthBelowPercent
		 || Modifier.Condition == ECataclysmStatCondition::HealthAbovePercent)
		&& (Modifier.ConditionValue < 0.0f || Modifier.ConditionValue > 100.0f))
	{
		return FString::Printf(
			TEXT("a health threshold of %.1f%%. A percentage of maximum health "
				 "is between 0 and 100."),
			Modifier.ConditionValue);
	}

	// AND A SKILL'S COST THRESHOLD IS A PERCENTAGE OF MAXIMUM HEALTH TOO, so it
	// is bounded the same way. Issue #983. A negative threshold would be
	// satisfied by every skill including the ones that cost nothing, since the
	// comparison is strictly greater than; the whole node would become an
	// unconditional bonus.
	//
	// THE UPPER BOUND IS NOT 100. A skill's total cost is its own share of
	// CURRENT health plus the character's added share of MAXIMUM health, and
	// only the second of those is bounded by the maximum, so a cost above 100%
	// of maximum health is arithmetically possible even though nothing in the
	// designed data reaches it. The bound is a sanity limit on the THRESHOLD
	// rather than on the cost: a threshold above 100% is far likelier to be a
	// number in the wrong column than a deliberate design.
	if (Modifier.Condition == ECataclysmStatCondition::SkillHealthCostAbovePercent
		&& (Modifier.ConditionValue < 0.0f || Modifier.ConditionValue > 100.0f))
	{
		return FString::Printf(
			TEXT("a skill cost threshold of %.1f%%. A percentage of maximum "
				 "health is between 0 and 100."),
			Modifier.ConditionValue);
	}

	// A WINDOW OF NO LENGTH NEVER HOLDS, and a negative one is not a shorter
	// window but a nonsensical one. Issue #962. Either is a modifier that grants
	// nothing while looking as though it grants something, which is the same
	// class of silent failure the threshold check above exists for.
	if ((Modifier.Condition == ECataclysmStatCondition::WithinSecondsOfHealthCost
			|| Modifier.Condition
				== ECataclysmStatCondition::WithinSecondsOfForeignDamage)
		&& Modifier.ConditionValue <= 0.0f)
	{
		return FString::Printf(
			TEXT("a window of %.1f seconds. A window has to be longer than "
				 "nothing or the bonus never applies."),
			Modifier.ConditionValue);
	}

	// A SCALE WITH NO STEP SIZE IS WORTH NOTHING AT EVERY STATE. Issue #968.
	// `ScaledValue` answers zero for it rather than dividing by nothing, so the
	// modifier applies, the arithmetic runs, and the node grants nothing at all
	// -- the same silent shape the two condition checks above exist for.
	if (Modifier.Scale != ECataclysmStatScale::Fixed && Modifier.ScaleStep <= 0.0f)
	{
		return FString::Printf(
			TEXT("a scaling step of %.1f. A modifier that grows with a state "
				 "needs a step larger than nothing, or it is worth nothing at "
				 "every state."),
			Modifier.ScaleStep);
	}

	return FString();
}

FCataclysmStatBreakdown UCataclysmStatPipeline::Accumulate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags,
	const FCataclysmStatConditions& State)
{
	FCataclysmStatBreakdown Out;
	Out.Base = Base;
	Out.MoreMultiplier = 1.0f;

	for (const FCataclysmStatModifier& Modifier : Modifiers)
	{
		if (!ModifierApplies(Modifier, SkillTags, State))
		{
			continue;
		}

		// WHAT IT IS WORTH RIGHT NOW, WHICH IS NOT ALWAYS WHAT IT SAYS. Issue
		// #968. A modifier whose size grows with the character's state -- "+1%
		// increased Attack Damage per point for every 5% of your maximum health
		// that is missing" -- is worth its value times however many whole steps
		// of that state the character currently has.
		//
		// ONE PLACE, BEFORE THE BUCKETS, so all three get the same treatment and
		// none of them has to know that a value can scale.
		//
		// A FIXED MODIFIER ANSWERS ITS OWN VALUE, so nothing that existed before
		// that issue changes by a single number.
		const float Value = ScaledValue(Modifier, State);

		switch (Modifier.Bucket)
		{
		case ECataclysmStatBucket::Flat:
			Out.Flat += Value;
			break;

		case ECataclysmStatBucket::Increased:
			// Everything here adds together and multiplies exactly once, which
			// is what gives this bucket its diminishing returns.
			Out.SumOfIncreases += Value;
			break;

		case ECataclysmStatBucket::More:
			if (!CanGrantMore(Modifier.Source))
			{
				// Ignored rather than clamped. Honouring it would break the rule
				// the whole three-bucket split rests on.
				++Out.RejectedMoreCount;
				UE_LOG(LogCataclysm, Warning,
					   TEXT("Stat pipeline ignored a More multiplier: %s"),
					   *ValidateModifier(Modifier));
				break;
			}
			{
				float MoreValue = Value;
				if (MoreValue < LessMultiplierFloor)
				{
					// Clamped rather than ignored. Ignoring would make the stat
					// larger than the data asked for; clamping keeps the
					// direction and preserves the invariant that no Less
					// multiplier zeroes or inverts a stat.
					++Out.ClampedLessCount;
					UE_LOG(LogCataclysm, Warning,
						   TEXT("Stat pipeline clamped a Less multiplier from "
								"%.1f%% to %.1f%%: %s"),
						   MoreValue, LessMultiplierFloor,
						   *ValidateModifier(Modifier));
					MoreValue = LessMultiplierFloor;
				}
				// Each source multiplies on its own. They are NOT summed first,
				// which is the whole difference from the bucket above: two 50%
				// More multipliers give 2.25x where two 50% increases give 2.0x.
				Out.MoreMultiplier *= 1.0f + MoreValue / 100.0f;
				++Out.MoreSourceCount;
			}
			break;
		}
	}

	return Out;
}

FCataclysmStatBreakdown UCataclysmStatPipeline::Evaluate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags,
	const FCataclysmStatConditions& State)
{
	FCataclysmStatBreakdown Out = Accumulate(Base, Modifiers, SkillTags, State);

	Out.Final = (Out.Base + Out.Flat)
			  * (1.0f + Out.SumOfIncreases / 100.0f)
			  * Out.MoreMultiplier;

	return Out;
}

FCataclysmStatBreakdown UCataclysmStatPipeline::EvaluateRate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags,
	const FCataclysmStatConditions& State)
{
	FCataclysmStatBreakdown Out = Accumulate(Base, Modifiers, SkillTags, State);

	// A rate divides by both buckets. Dividing is what stops any amount of
	// cooldown reduction reaching zero, so the stat needs no cap.
	const float Divisor = (1.0f + Out.SumOfIncreases / 100.0f) * Out.MoreMultiplier;

	// The floor under a Less multiplier already keeps MoreMultiplier above
	// zero, and increases are not permitted to be negative enough to invert the
	// first bracket in any designed data. This guard is here so that a divisor
	// at or below zero produces the unmodified base rather than a negative or
	// infinite interval.
	Out.Final = Divisor > UE_SMALL_NUMBER ? Out.Base / Divisor : Out.Base;

	return Out;
}

float UCataclysmStatPipeline::DisplayedRateReduction(
	const FCataclysmStatBreakdown& Breakdown)
{
	const float Divisor = (1.0f + Breakdown.SumOfIncreases / 100.0f)
						* Breakdown.MoreMultiplier;

	if (Divisor <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// Storing the sum of increases rather than the displayed figure is what
	// keeps this below 100 however large the sum gets: a character with +100%
	// worth of increases is shown 50%, not 100%.
	//
	// One limit, deliberately not papered over. Past a divisor of about 8.4
	// million this expression rounds to exactly 1.0 in single precision and a
	// player is shown 100% while the interval itself is still above zero. The
	// mechanical guarantee is unaffected -- EvaluateRate divides, so it cannot
	// reach zero -- and the magnitude needed is not reachable, requiring roughly
	// 34 compounding 50% sources on one stat against six gem sockets. No clamp
	// is applied here because inventing a display ceiling is the interface
	// work's decision, not this class's.
	return 100.0f * (Divisor - 1.0f) / Divisor;
}
