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

bool UCataclysmStatPipeline::ModifierApplies(const FCataclysmStatModifier& Modifier,
											 const FGameplayTagContainer& SkillTags)
{
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
				TEXT("a More multiplier from %s. Only a gem, a passive keystone "
					 "or an enchantment may grant one; everything else is flat "
					 "or increased."),
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
	return FString();
}

FCataclysmStatBreakdown UCataclysmStatPipeline::Accumulate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags)
{
	FCataclysmStatBreakdown Out;
	Out.Base = Base;
	Out.MoreMultiplier = 1.0f;

	for (const FCataclysmStatModifier& Modifier : Modifiers)
	{
		if (!ModifierApplies(Modifier, SkillTags))
		{
			continue;
		}

		switch (Modifier.Bucket)
		{
		case ECataclysmStatBucket::Flat:
			Out.Flat += Modifier.Value;
			break;

		case ECataclysmStatBucket::Increased:
			// Everything here adds together and multiplies exactly once, which
			// is what gives this bucket its diminishing returns.
			Out.SumOfIncreases += Modifier.Value;
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
				float Value = Modifier.Value;
				if (Value < LessMultiplierFloor)
				{
					// Clamped rather than ignored. Ignoring would make the stat
					// larger than the data asked for; clamping keeps the
					// direction and preserves the invariant that no Less
					// multiplier zeroes or inverts a stat.
					++Out.ClampedLessCount;
					UE_LOG(LogCataclysm, Warning,
						   TEXT("Stat pipeline clamped a Less multiplier from "
								"%.1f%% to %.1f%%: %s"),
						   Value, LessMultiplierFloor, *ValidateModifier(Modifier));
					Value = LessMultiplierFloor;
				}
				// Each source multiplies on its own. They are NOT summed first,
				// which is the whole difference from the bucket above: two 50%
				// More multipliers give 2.25x where two 50% increases give 2.0x.
				Out.MoreMultiplier *= 1.0f + Value / 100.0f;
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
	const FGameplayTagContainer& SkillTags)
{
	FCataclysmStatBreakdown Out = Accumulate(Base, Modifiers, SkillTags);

	Out.Final = (Out.Base + Out.Flat)
			  * (1.0f + Out.SumOfIncreases / 100.0f)
			  * Out.MoreMultiplier;

	return Out;
}

FCataclysmStatBreakdown UCataclysmStatPipeline::EvaluateRate(
	float Base,
	const TArray<FCataclysmStatModifier>& Modifiers,
	const FGameplayTagContainer& SkillTags)
{
	FCataclysmStatBreakdown Out = Accumulate(Base, Modifiers, SkillTags);

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
