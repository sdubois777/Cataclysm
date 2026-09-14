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
		 &Combat::GetBleedChanceAttribute, nullptr, nullptr, EShape::DamageOverTime},
		{TEXT("Poison"), TEXT("poison_chance"),
		 TEXT("Cataclysm.AilmentChance.Poison"),
		 TEXT("DoT_Poison"), TEXT("Keyword.DoT.Poison"),
		 &Combat::GetPoisonChanceAttribute, nullptr, nullptr, EShape::DamageOverTime},
		{TEXT("Disease"), TEXT("disease_chance"),
		 TEXT("Cataclysm.AilmentChance.Disease"),
		 TEXT("DoT_Disease"), TEXT("Keyword.DoT.Disease"),
		 &Combat::GetDiseaseChanceAttribute, nullptr, nullptr, EShape::DamageOverTime},
		{TEXT("Void Splinter"), TEXT("void_splinter_chance"),
		 TEXT("Cataclysm.AilmentChance.VoidSplinter"),
		 TEXT("DoT_Void_Splinter"), TEXT("Keyword.DoT.VoidSplinter"),
		 &Combat::GetVoidSplinterChanceAttribute, nullptr, nullptr, EShape::ShareOfCurrentHealth},
		{TEXT("Necrosis"), TEXT("necrosis_chance"),
		 TEXT("Cataclysm.AilmentChance.Necrosis"),
		 TEXT("DoT_Necrosis"), TEXT("Keyword.DoT.Necrosis"),
		 &Combat::GetNecrosisChanceAttribute, nullptr, nullptr, EShape::DamageOverTime},
		{TEXT("Burn"), TEXT("burn_chance"),
		 TEXT("Cataclysm.AilmentChance.Burn"),
		 TEXT("DoT_Burn"), TEXT("Keyword.DoT.Burn"),
		 &Combat::GetBurnChanceAttribute, nullptr, nullptr, EShape::DamageOverTime},
		{TEXT("Madness"), TEXT("madness_chance"),
		 TEXT("Cataclysm.AilmentChance.Madness"),
		 TEXT("Debuff_Madness"), TEXT("Status.Debuff.Madness"),
		 &Combat::GetMadnessChanceAttribute, nullptr, nullptr, EShape::LongerWithMagnitude},
		{TEXT("Cripple"), TEXT("cripple_chance"),
		 TEXT("Cataclysm.AilmentChance.Cripple"),
		 TEXT("Debuff_Cripple"), TEXT("Status.Debuff.Cripple"),
		 &Combat::GetCrippleChanceAttribute,
		 TEXT("cripple_magnitude"),
		 &Combat::GetCrippleMagnitudeAttribute,
		 EShape::StrongerThenLongerWithMagnitude},
		{TEXT("Weaken"), TEXT("weaken_chance"),
		 TEXT("Cataclysm.AilmentChance.Weaken"),
		 TEXT("Debuff_Weaken"), TEXT("Status.Debuff.Weaken"),
		 &Combat::GetWeakenChanceAttribute,
		 TEXT("weaken_magnitude"),
		 &Combat::GetWeakenMagnitudeAttribute, EShape::StrongerThenLongerOnAStat},
		{TEXT("Shred"), TEXT("shred_chance"),
		 TEXT("Cataclysm.AilmentChance.Shred"),
		 TEXT("Debuff_Shred"), TEXT("Status.Debuff.Shred"),
		 &Combat::GetShredChanceAttribute, nullptr, nullptr, EShape::StrongerWithMagnitude},
		{TEXT("Stun"), TEXT("stun_chance"),
		 TEXT("Cataclysm.AilmentChance.Stun"),
		 TEXT("Debuff_Stun"), TEXT("State.Stunned"),
		 &Combat::GetStunChanceAttribute, nullptr, nullptr, EShape::Stun},
	};

	/**
	 * The name an ailment's magnitude stat travels under on a damage effect.
	 *
	 * DERIVED FROM THE CHANCE'S OWN NAME rather than a twelfth field on the
	 * kind, so the two cannot drift apart and a new ailment gaining a magnitude
	 * stat needs nothing here. Issue #1767.
	 */
	FString MagnitudeDataNameFor(const FCataclysmAilmentKind& Kind)
	{
		return FString(Kind.DataName) + TEXT(".Magnitude");
	}

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
									 float& OutMagnitude, float MagnitudePercent)
{
	const float Total = FMath::Max(TotalChance, 0.0f);
	OutChance = FMath::Min(Total, ChanceCap);
	OutMagnitude = FMath::Max(1.0f, Total / ChanceCap);

	// AND THE STAT SCALES WHATEVER THAT PRODUCED. Issue #1767. A hundred means
	// unchanged, which is why it is the default and why the two attributes
	// behind it start there -- the same shape `UCataclysmDebuffs::DurationOn`
	// uses, dividing by `NormalDuration` of 100.
	//
	// AFTER THE FLOOR OF ONE AND NOT BEFORE. The floor exists because a chance
	// at or below the cap is a normal application rather than a diminished one,
	// and a character investing in magnitude should scale a normal application
	// too. Multiplying first and flooring after would throw that investment
	// away for every character below 100% chance, which is most of them.
	//
	// NEGATIVE IS REFUSED RATHER THAN CLAMPED TO ONE. Nothing reduces these
	// stats today and a row that did would be stating a smaller ailment, which
	// is a legitimate future rule; below zero is not, because it would invert
	// the effect.
	OutMagnitude *= FMath::Max(0.0f, MagnitudePercent) / NormalMagnitude;
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

			// AND THE MAGNITUDE STAT, FOR THE TWO AILMENTS THAT HAVE ONE. Issue
			// #1767. Resolved here rather than where the roll is made, for the
			// reason the chance above is: this is the only place holding the
			// skill's full tags, so a row scoped to melee counts only for a
			// melee skill. It travels on the same effect under its own name.
			//
			// ONLY WHEN THE CHANCE LANDED. A magnitude with no chance to scale
			// is nothing, and stamping it anyway would put a number on every
			// blow that nothing reads.
			if (Kind.MagnitudeStat && Kind.MagnitudeAttribute)
			{
				const FGameplayAttribute Scaling = Kind.MagnitudeAttribute();
				if (Attacker->HasAttributeSetForAttribute(Scaling))
				{
					const float Held = Attacker->GetNumericAttribute(Scaling);
					Chances.Add(FName(MagnitudeDataNameFor(Kind)),
						Cataclysm
							? Cataclysm->StatForSkill(FName(Kind.MagnitudeStat),
													  SkillTags, Held,
													  SkillHealthCostPercent)
							: Held);
				}
			}
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

		// THE MAGNITUDE STAT, IF THIS AILMENT HAS ONE AND THE BLOW CARRIED IT.
		// Issue #1767. `NormalMagnitude` when nothing stamped one, which is
		// every ailment but Cripple and Weaken and every attacker with no
		// investment -- so the arithmetic below is unchanged for them.
		const float MagnitudePercent = Kind.MagnitudeStat
			? Spec.GetSetByCallerMagnitude(FName(MagnitudeDataNameFor(Kind)),
				/*WarnIfNotFound=*/false,
				/*DefaultIfNotFound=*/NormalMagnitude)
			: NormalMagnitude;

		Application(Total, Chance, Magnitude, MagnitudePercent);
		// THE SKILL WHOSE BLOW ROLLED IT goes on the ailment too, so that
		// every tick of it names the skill. Issue #41, slice 4.
		if (AilmentRoll() < Chance
			&& Apply(Applier, Defender, Kind, Magnitude,
					 Spec.GetContext().GetAbilityInstance_NotReplicated()))
		{
			++Applied;
		}
	}
	return Applied;
}

namespace
{
	/** What a capped effect's magnitude buys: a bigger figure, then a longer one. */
	struct FCapThenExtend
	{
		/** The strength to apply, never above the row's cap. */
		float Strength = 0.0f;

		/** What to multiply the row's duration by. 1 until the cap is reached. */
		float Longer = 1.0f;
	};

	/**
	 * The design's rule for an effect whose strength has a cap: the magnitude
	 * raises the strength until it reaches that cap, and the surplus extends the
	 * duration instead.
	 *
	 * `docs/Cataclysm_GDD_v2.md` states it generally -- "A strength with a cap,
	 * such as a slow: the strength up to that cap, then the duration instead" --
	 * and again for each effect that has one, Cripple "to a cap of 80%, then the
	 * duration" and Weaken the same.
	 *
	 * THE DIVISION AT THE CAP IS A JUDGEMENT THE DOCUMENT DOES NOT MAKE. It
	 * fixes that surplus becomes duration and not how much. The multiplier is
	 * split rather than a rate applied, so that the whole of it is spent and the
	 * two sides meet: `Scale / CapScale` is exactly 1 at the cap, which leaves
	 * the duration at the row's own figure there. `docs/DECISIONS.md` carries
	 * the reasoning and the alternative it was chosen over.
	 *
	 * IT NEEDS NO CONSTANT NOBODY HAS, which is the argument that decided it. A
	 * rate in seconds per surplus point would be a number the design states
	 * nowhere, and the project owner declined to invent one.
	 *
	 * ONE COPY FOR TWO EFFECTS. Cripple carries its figure on the tag and Weaken
	 * moves an attribute, so they end in different calls -- but the rule that
	 * decides the two numbers is one rule, and the design states it once.
	 *
	 * A ROW WITH NO CAP TAKES THE WHOLE MULTIPLIER INTO STRENGTH, which is the
	 * honest reading of an empty column rather than a special case: the
	 * generator's own comment says an empty cap means no NUMERIC cap.
	 */
	FCapThenExtend CapThenExtend(const FCataclysmStatusEffectNumbers& Row,
								 float Scale)
	{
		FCapThenExtend Out;

		Out.Strength = Row.StrengthCap > 0.0f
			? FMath::Min(Row.Strength * Scale, Row.StrengthCap)
			: Row.Strength * Scale;

		// HOW MUCH OF THE MULTIPLIER REACHED THE CAP, and the rest extends the
		// duration. Guarded on a strength of zero because that would divide by
		// nothing; such a row has no reduction to raise, so all of the
		// multiplier is surplus and the duration takes it.
		const float CapScale = (Row.StrengthCap > 0.0f && Row.Strength > 0.0f)
			? Row.StrengthCap / Row.Strength
			: 0.0f;

		Out.Longer = (CapScale > 0.0f && Scale > CapScale)
			? Scale / CapScale
			: 1.0f;

		return Out;
	}
}

bool UCataclysmAilments::Apply(AActor* Instigator, AActor* Target,
							   const FCataclysmAilmentKind& Kind, float Magnitude,
							   const UGameplayAbility* Skill)
{
	if (Kind.Shape == EShape::Stun)
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
				/*bScalesWithInstigator=*/true,
				/*DealtBy=*/nullptr, Skill);

	case EShape::StrongerWithMagnitude:
		// NO DAMAGE TYPE, so it cuts the one generic resistance an enemy holds,
		// which is the only resistance an enemy has. A player's hit carries no
		// damage type either. See `UCataclysmSkillEffects::ApplyNamedEffect`.
		return UCataclysmSkillEffects::ApplyNamedEffect(Instigator, Target, Tag,
			Row.DurationSeconds, Row.Strength * Scale, NAME_None);

	case EShape::LongerWithMagnitude:
		return UCataclysmSkillEffects::ApplyNamedEffect(Instigator, Target, Tag,
			Row.DurationSeconds * Scale);

	case EShape::StrongerThenLongerOnAStat:
	{
		// THE STAT AND THE OPERATION BOTH COME FROM THE ROW. Weaken's row names
		// `attack_damage` and says its strength is a proportion of it, so
		// `ApplyNamedEffect` puts a "more" multiplier of `1 - Strength/100` on
		// the attribute that every blow reads. Nothing here needs to know which
		// stat that is.
		//
		// NO DAMAGE TYPE, for the same reason Shred's call passes none: the
		// damage type exists to resolve `resistance_of_source_element`, and a
		// row naming an ordinary stat does not use it.
		const FCapThenExtend Split = CapThenExtend(Row, Scale);
		return UCataclysmSkillEffects::ApplyNamedEffect(Instigator, Target, Tag,
			Row.DurationSeconds * Split.Longer, Split.Strength, NAME_None);
	}

	case EShape::StrongerThenLongerWithMagnitude:
	{
		// THE CAP IS ON THE REDUCTION, NOT ON THE MULTIPLIER. `Strength` is the
		// slow in per cent -- 30 means 30% slower -- so `StrengthCap` of 80
		// means an 80% slow. `CrippleMultiplier` turns whichever figure it
		// receives into `1 - figure/100`.
		const FCapThenExtend Split = CapThenExtend(Row, Scale);

		// THE TAG CARRIES THE FIGURE, because no enemy attribute is read for
		// speed at all: an enemy's walk speed is
		// `DesignedWalkSpeedCmPerSecond * SpeedMultiplier()` and its attack
		// interval divides by the same, neither of which reads one. Issue #1256
		// records the measurement.
		//
		// WEAKEN IS THE OTHER HALF OF THAT SENTENCE AND GOES THE OTHER WAY. Its
		// reduction is a proportion too, and it does have an attribute to move,
		// so it uses `StrongerThenLongerOnAStat` above and the same
		// `CapThenExtend` rule. Which of the two an effect wants is decided by
		// whether a reader exists, not by the shape of its number.
		return UCataclysmSkillEffects::ApplyTagForDuration(Instigator, Target,
			Tag, Row.DurationSeconds * Split.Longer, Split.Strength);
	}

	case EShape::ShareOfCurrentHealth:
		// THE ROW'S SHARE OF CURRENT HEALTH A TICK, TIMES THE MAGNITUDE, so 250%
		// takes 2.5% where the row says 1%. Issue #915. The design document's
		// table says magnitude scales Void Splinter's damage, and the owner's
		// answer on #915 keeps that. The row states a percent and the applier
		// takes a fraction.
		return UCataclysmSkillEffects::ApplyShareOfHealthOverTime(Instigator, Target,
			Row.PercentOfCurrentHealth / 100.0f * Scale, Row.DurationSeconds, Tag);

	default:
		return false;
	}
}
