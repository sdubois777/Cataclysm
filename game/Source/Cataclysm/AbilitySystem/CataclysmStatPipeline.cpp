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
		// AT OR BELOW, NOT BELOW. Every node that states a health threshold is
		// written "While at or below 20% health", so a character sitting exactly
		// on the number gets the bonus.
		return State.HealthPercent >= 0.0f && State.HealthPercent <= Value;

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
	}

	// A CONDITION THIS BUILD DOES NOT KNOW REFUSES rather than applying. A saved
	// or imported modifier naming one is a modifier this build cannot judge, and
	// granting it would be granting something unread.
	return false;
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
	if (Modifier.Condition == ECataclysmStatCondition::HealthAtOrBelowPercent
		&& (Modifier.ConditionValue < 0.0f || Modifier.ConditionValue > 100.0f))
	{
		return FString::Printf(
			TEXT("a health threshold of %.1f%%. A percentage of maximum health "
				 "is between 0 and 100."),
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
