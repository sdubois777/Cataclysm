// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"

/**
 * Forces the roll for every chance to apply an ailment, so it can be watched.
 * Issue #899.
 *
 * THE SAME SHAPE AS `Cataclysm.CritRoll` IN CataclysmVitalAttributeSet.cpp, and
 * for the same reason: a test asserting that a blow did or did not apply an
 * ailment would otherwise pass some of the time and fail the rest.
 *
 * -1, the default, rolls normally. 0 applies every ailment a blow carries any
 * chance of, because every chance above zero beats it. 100 applies none,
 * because the comparison is strictly less than and a chance is capped at 100.
 *
 * ONE ROLL PER AILMENT, and the pin sets every one of them. A blunt weapon's
 * own chance to stun is among them, since it joined the chance to stun from gear
 * in one pool. Issue #1034 says that stun rule had no test because its roll
 * could not be pinned.
 */
static TAutoConsoleVariable<float> CVarAilmentRoll(
	TEXT("Cataclysm.AilmentRoll"),
	-1.0f,
	TEXT("Pins the roll for every chance to apply an ailment, 0-100. -1 rolls "
		 "normally. 0 applies every ailment a blow has any chance of; 100 applies "
		 "none."),
	ECVF_Default);

namespace
{
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;
	using EShape = ECataclysmAilmentShape;

	/**
	 * Every ailment, in the order `AILMENT_AFFIXES` lists them in
	 * `sim/cataclysm_sim/affixes.py`, so the two can be read side by side.
	 *
	 * THE TAGS ARE THE ONES THE REST OF THE GAME ALREADY READS. A damage over
	 * time effect grants a `Keyword.DoT.*` tag, which `UCataclysmDebuffs`
	 * counts. A named effect grants a `Status.Debuff.*` tag: `UCataclysmTeams`
	 * reads Madness's, and an enemy's speed reads Cripple's (issue #1152).
	 */
	const FCataclysmAilmentKind EveryKind[] = {
		{TEXT("Bleed"), TEXT("bleed_chance"),
		 TEXT("Cataclysm.AilmentChance.Bleed"),
		 TEXT("DoT_Bleed"), TEXT("Keyword.DoT.Bleed"),
		 &Combat::GetBleedChanceAttribute, EShape::DamageOverTime},
		{TEXT("Poison"), TEXT("poison_chance"),
		 TEXT("Cataclysm.AilmentChance.Poison"),
		 TEXT("DoT_Poison"), TEXT("Keyword.DoT.Poison"),
		 &Combat::GetPoisonChanceAttribute, EShape::DamageOverTime},
		{TEXT("Disease"), TEXT("disease_chance"),
		 TEXT("Cataclysm.AilmentChance.Disease"),
		 TEXT("DoT_Disease"), TEXT("Keyword.DoT.Disease"),
		 &Combat::GetDiseaseChanceAttribute, EShape::DamageOverTime},
		{TEXT("Void Splinter"), TEXT("void_splinter_chance"),
		 TEXT("Cataclysm.AilmentChance.VoidSplinter"),
		 TEXT("DoT_Void_Splinter"), TEXT("Keyword.DoT.VoidSplinter"),
		 &Combat::GetVoidSplinterChanceAttribute, EShape::NotBuilt},
		{TEXT("Necrosis"), TEXT("necrosis_chance"),
		 TEXT("Cataclysm.AilmentChance.Necrosis"),
		 TEXT("DoT_Necrosis"), TEXT("Keyword.DoT.Necrosis"),
		 &Combat::GetNecrosisChanceAttribute, EShape::DamageOverTime},
		{TEXT("Burn"), TEXT("burn_chance"),
		 TEXT("Cataclysm.AilmentChance.Burn"),
		 TEXT("DoT_Burn"), TEXT("Keyword.DoT.Burn"),
		 &Combat::GetBurnChanceAttribute, EShape::DamageOverTime},
		{TEXT("Madness"), TEXT("madness_chance"),
		 TEXT("Cataclysm.AilmentChance.Madness"),
		 TEXT("Debuff_Madness"), TEXT("Status.Debuff.Madness"),
		 &Combat::GetMadnessChanceAttribute, EShape::LongerWithMagnitude},
		{TEXT("Cripple"), TEXT("cripple_chance"),
		 TEXT("Cataclysm.AilmentChance.Cripple"),
		 TEXT("Debuff_Cripple"), TEXT("Status.Debuff.Cripple"),
		 &Combat::GetCrippleChanceAttribute, EShape::AtItsRowsFigures},
		{TEXT("Weaken"), TEXT("weaken_chance"),
		 TEXT("Cataclysm.AilmentChance.Weaken"),
		 TEXT("Debuff_Weaken"), TEXT("Status.Debuff.Weaken"),
		 &Combat::GetWeakenChanceAttribute, EShape::AtItsRowsFigures},
		{TEXT("Shred"), TEXT("shred_chance"),
		 TEXT("Cataclysm.AilmentChance.Shred"),
		 TEXT("Debuff_Shred"), TEXT("Status.Debuff.Shred"),
		 &Combat::GetShredChanceAttribute, EShape::StrongerWithMagnitude},
		{TEXT("Stun"), TEXT("stun_chance"),
		 TEXT("Cataclysm.AilmentChance.Stun"),
		 TEXT("Debuff_Stun"), TEXT("State.Stunned"),
		 &Combat::GetStunChanceAttribute, EShape::Stun},
	};

	/** The roll one chance is compared with: pinned, or drawn. */
	float AilmentRoll()
	{
		const float Pinned = CVarAilmentRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}
}

TArrayView<const FCataclysmAilmentKind> UCataclysmAilments::Kinds()
{
	return MakeArrayView(EveryKind);
}

const FCataclysmAilmentKind* UCataclysmAilments::KindNamed(const FString& Ailment)
{
	const FString Wanted = Ailment.TrimStartAndEnd();
	for (const FCataclysmAilmentKind& Kind : EveryKind)
	{
		if (Wanted.Equals(Kind.Ailment, ESearchCase::IgnoreCase))
		{
			return &Kind;
		}
	}
	return nullptr;
}

void UCataclysmAilments::Application(float TotalChance, float& OutChance,
									 float& OutMagnitude)
{
	const float Total = FMath::Max(TotalChance, 0.0f);
	OutChance = FMath::Min(Total, ChanceCap);
	OutMagnitude = FMath::Max(1.0f, Total / ChanceCap);
}

TMap<FName, float> UCataclysmAilments::ChancesFor(
	const UAbilitySystemComponent* Attacker, const FGameplayTagContainer& SkillTags,
	float SkillHealthCostPercent)
{
	TMap<FName, float> Chances;
	if (!Attacker)
	{
		return Chances;
	}

	// ASKED FOR RATHER THAN READ, the way `UCataclysmSkillEffects::SpellDamageOf`
	// asks for spell damage. The attribute was worked out with no skill in hand
	// and nothing known about the character, so a row requiring a tag or a
	// state is missing from it.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(Attacker);

	for (const FCataclysmAilmentKind& Kind : EveryKind)
	{
		if (Kind.Shape == EShape::NotBuilt)
		{
			continue;
		}

		const FGameplayAttribute Attribute = Kind.Attribute();
		if (!Attacker->HasAttributeSetForAttribute(Attribute))
		{
			continue;
		}

		const float FromAttribute = Attacker->GetNumericAttribute(Attribute);
		const float Chance = Cataclysm
			? Cataclysm->StatForSkill(FName(Kind.Stat), SkillTags, FromAttribute,
									  SkillHealthCostPercent)
			: FromAttribute;

		if (Chance > 0.0f)
		{
			Chances.Add(FName(Kind.DataName), Chance);
		}
	}
	return Chances;
}

int32 UCataclysmAilments::RollOnLandedBlow(const FGameplayEffectSpec& Spec,
										   AActor* Defender, float DealtToHealth,
										   bool bIsBlunt)
{
	const UAbilitySystemComponent* Struck =
		UCataclysmTargeting::AbilitySystemOf(Defender);
	if (!Struck || !Struck->HasAttributeSetForAttribute(Vital::GetHealthAttribute()))
	{
		return 0;
	}

	// A TENTH OF MAXIMUM HEALTH, AND A TARGET STILL ALIVE TO CARRY IT. The first
	// is the owner's rule for an ailment that does not come from the skill's own
	// row, settled on #917 and recorded in `docs/DECISIONS.md` on 2026-09-02. Its
	// constant is the one the incidental stun and the incidental burn read. The
	// second matches `ApplyBurn`, which refuses to set a corpse alight.
	const float Health = Struck->GetNumericAttribute(Vital::GetHealthAttribute());
	const float MaxHealth =
		Struck->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (Health <= 0.0f || MaxHealth <= 0.0f
		|| DealtToHealth
			< MaxHealth * UCataclysmSkillEffects::StunDamageThresholdPercent / 100.0f)
	{
		return 0;
	}

	AActor* Applier = Spec.GetContext().GetInstigator();

	int32 Applied = 0;
	for (const FCataclysmAilmentKind& Kind : EveryKind)
	{
		if (Kind.Shape == EShape::NotBuilt)
		{
			continue;
		}

		float Total = Spec.GetSetByCallerMagnitude(FName(Kind.DataName),
			/*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);

		if (Kind.Shape == EShape::Stun)
		{
			// ONE POOL WITH A BLUNT WEAPON'S OWN CHANCE, which the project owner
			// settled on 2026-08-16 (issue #298). `StunApplication` caps the
			// chance at 100 and turns the rest into a longer stun, up to three
			// seconds. `ApplyStun` enforces the immunity window, a boss's
			// immunity and the target's crowd control resistance.
			if (bIsBlunt)
			{
				Total += UCataclysmDamageCalculation::BluntStunChance;
			}

			float Chance = 0.0f;
			float Seconds = 0.0f;
			UCataclysmDamageCalculation::StunApplication(Total, Chance, Seconds);
			if (Chance > 0.0f && AilmentRoll() < Chance
				&& UCataclysmSkillEffects::ApplyStun(Applier, Defender, Seconds,
					DealtToHealth, /*bStunIsDesigned=*/false))
			{
				++Applied;
			}
			continue;
		}

		if (Total <= 0.0f)
		{
			continue;
		}

		float Chance = 0.0f;
		float Magnitude = 1.0f;
		Application(Total, Chance, Magnitude);
		if (AilmentRoll() < Chance && Apply(Applier, Defender, Kind, Magnitude))
		{
			++Applied;
		}
	}
	return Applied;
}

bool UCataclysmAilments::Apply(AActor* Instigator, AActor* Target,
							   const FCataclysmAilmentKind& Kind, float Magnitude)
{
	if (Kind.Shape == EShape::Stun || Kind.Shape == EShape::NotBuilt)
	{
		return false;
	}

	const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(Kind.TagName), /*ErrorIfNotFound=*/false);
	if (!Tag.IsValid())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s is not in the tag vocabulary, so nothing can apply %s."),
			Kind.TagName, Kind.Ailment);
		return false;
	}

	// THE ROW'S OWN FIGURES, which is what the design document means by the
	// effect being "defined in the status effect data". None of the eleven
	// affixes states a number of its own beyond its chance.
	const FCataclysmStatusEffectNumbers Row =
		UCataclysmSkillEffects::StatusEffectNumbers(Kind.StatusRow, Kind.Ailment);
	const float Scale = FMath::Max(1.0f, Magnitude);

	switch (Kind.Shape)
	{
	case EShape::DamageOverTime:
		// `bUsable` IS THE TABLE'S OWN ANSWER TO "IS THIS DAMAGE OVER TIME": a
		// duration and a per-tick amount. It scales with the attacker's three
		// damage over time stats, as every damage over time a character
		// applies does.
		return Row.bUsable
			&& UCataclysmSkillEffects::ApplyDamageOverTime(Instigator, Target,
				Row.FlatDamagePerTick * Scale, Row.DurationSeconds, Tag,
				/*bScalesWithInstigator=*/true);

	case EShape::StrongerWithMagnitude:
		// NO DAMAGE TYPE, so it cuts the one generic resistance an enemy holds,
		// which is the only resistance an enemy has. A player's hit carries no
		// damage type either. See `UCataclysmSkillEffects::ApplyNamedEffect`.
		return UCataclysmSkillEffects::ApplyNamedEffect(Instigator, Target, Tag,
			Row.DurationSeconds, Row.Strength * Scale, NAME_None);

	case EShape::LongerWithMagnitude:
		return UCataclysmSkillEffects::ApplyNamedEffect(Instigator, Target, Tag,
			Row.DurationSeconds * Scale);

	case EShape::AtItsRowsFigures:
		return UCataclysmSkillEffects::ApplyNamedEffect(Instigator, Target, Tag,
			Row.DurationSeconds);

	default:
		return false;
	}
}
