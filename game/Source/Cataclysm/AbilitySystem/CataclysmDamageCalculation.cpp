// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

namespace
{
	/**
	 * How much of a hit of this type the defender resists.
	 *
	 * IT ASKS THE DEFENDER WHICH KIND OF RESISTANCE IT HAS, because the two sides
	 * of a fight hold different attribute sets and never both. The project owner
	 * ruled it on 2026-08-12:
	 *
	 *     an ENEMY holds UCataclysmAllResistanceAttributeSet: one figure, met by
	 *     a hit of any type including an untyped one
	 *
	 *     a PLAYER holds UCataclysmResistanceAttributeSet: eight figures, and the
	 *     hit's own damage type selects which one applies
	 *
	 * The two are added rather than one winning, so a character that somehow held
	 * both would get a defined answer instead of an accidental one. Nothing holds
	 * both today.
	 *
	 * AN UNTYPED HIT STILL MEETS THE GENERIC FIGURE. That is what the generic
	 * figure is for. Player damage carries no type -- the enemy resists everything
	 * equally, so a type would be choosing between copies of one number -- and
	 * before issue #486 an untyped hit found no slot to read, so every resistance
	 * on either side did nothing at all.
	 */
	float ResistanceFor(const UAbilitySystemComponent* Defender, FName DamageType)
	{
		if (!Defender)
		{
			return 0.0f;
		}

		float Total = 0.0f;
		if (const UCataclysmAllResistanceAttributeSet* Generic =
				Defender->GetSet<UCataclysmAllResistanceAttributeSet>())
		{
			Total += Generic->GetAllResistance();
		}

		const UCataclysmResistanceAttributeSet* Set =
			Defender->GetSet<UCataclysmResistanceAttributeSet>();
		if (!Set || DamageType.IsNone())
		{
			return Total;
		}

		static const TMap<FName, TFunction<float(const UCataclysmResistanceAttributeSet*)>> Lookup = {
			{ TEXT("War"),        [](const UCataclysmResistanceAttributeSet* S) { return S->GetWarResistance(); } },
			{ TEXT("Demonic"),    [](const UCataclysmResistanceAttributeSet* S) { return S->GetDemonicResistance(); } },
			{ TEXT("Death"),      [](const UCataclysmResistanceAttributeSet* S) { return S->GetDeathResistance(); } },
			{ TEXT("Pestilence"), [](const UCataclysmResistanceAttributeSet* S) { return S->GetPestilenceResistance(); } },
			{ TEXT("Famine"),     [](const UCataclysmResistanceAttributeSet* S) { return S->GetFamineResistance(); } },
			{ TEXT("Celestial"),  [](const UCataclysmResistanceAttributeSet* S) { return S->GetCelestialResistance(); } },
			{ TEXT("Chaos"),      [](const UCataclysmResistanceAttributeSet* S) { return S->GetChaosResistance(); } },
			{ TEXT("Void"),       [](const UCataclysmResistanceAttributeSet* S) { return S->GetVoidResistance(); } },
		};

		if (const auto* Getter = Lookup.Find(DamageType))
		{
			Total += (*Getter)(Set);
		}

		// A damage type nobody has heard of adds nothing, and the generic figure
		// still applies, because it applies to everything by definition.
		return Total;
	}
}

const TCHAR* UCataclysmDamageCalculation::ElementTagPrefix = TEXT("Element.");
const TCHAR* UCataclysmDamageCalculation::AreaDamageTagName = TEXT("Type.AOE");
const TCHAR* UCataclysmDamageCalculation::DamageOverTimeTagName = TEXT("Keyword.DoT");
const TCHAR* UCataclysmDamageCalculation::NoCriticalStrikeTagName =
	TEXT("Keyword.NoCrit");
const TCHAR* UCataclysmDamageCalculation::NoPenetrationTagName =
	TEXT("Keyword.NoPenetration");
const TCHAR* UCataclysmDamageCalculation::NoWeaponSubTypeTagName =
	TEXT("Keyword.NoWeaponSubType");
const TCHAR* UCataclysmDamageCalculation::NoLeechTagName =
	TEXT("Keyword.NoLeech");
const TCHAR* UCataclysmDamageCalculation::NoRetaliationTagName =
	TEXT("Keyword.NoRetaliation");
const TCHAR* UCataclysmDamageCalculation::SkillCritChanceDataTagName =
	TEXT("Data.SkillCritChance");
const TCHAR* UCataclysmDamageCalculation::ElementIsForColourOnlyTagName =
	TEXT("Data.ElementIsForColourOnly");

namespace
{
	/**
	 * Requested by name rather than declared natively, for the same reason
	 * UCataclysmSkillEffects::BurnTag is: a native declaration would create the
	 * tag whether or not the vocabulary still lists it, which hides exactly the
	 * disagreement worth catching. `CataclysmDamageTypeTests.cpp` checks each of
	 * these is valid, because an invalid one would silently stop a property
	 * travelling and every test that did not look would still pass.
	 */
	FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}
}

FGameplayTag UCataclysmDamageCalculation::AreaDamageTag()
{
	return TagNamed(AreaDamageTagName);
}

FGameplayTag UCataclysmDamageCalculation::DamageOverTimeTag()
{
	return TagNamed(DamageOverTimeTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoCriticalStrikeTag()
{
	return TagNamed(NoCriticalStrikeTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoPenetrationTag()
{
	return TagNamed(NoPenetrationTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoWeaponSubTypeTag()
{
	return TagNamed(NoWeaponSubTypeTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoLeechTag()
{
	return TagNamed(NoLeechTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoRetaliationTag()
{
	return TagNamed(NoRetaliationTagName);
}

FGameplayTag UCataclysmDamageCalculation::ElementIsForColourOnlyTag()
{
	return TagNamed(ElementIsForColourOnlyTagName);
}

FGameplayTag UCataclysmDamageCalculation::SkillCritChanceDataTag()
{
	return TagNamed(SkillCritChanceDataTagName);
}

FGameplayTag UCataclysmDamageCalculation::ElementTagFor(FName DamageType)
{
	if (DamageType.IsNone())
	{
		return FGameplayTag();
	}

	// Requested by name rather than declared natively, for the same reason
	// UCataclysmSkillEffects::BurnTag is: a native declaration would create the
	// tag whether or not the vocabulary still lists it, which hides exactly the
	// disagreement worth catching.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(*FString::Printf(TEXT("%s%s"), ElementTagPrefix, *DamageType.ToString())),
		/*ErrorIfNotFound=*/false);
}

FName UCataclysmDamageCalculation::DamageTypeFromTags(const FGameplayTagContainer& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		const FString Name = Tag.ToString();
		if (Name.StartsWith(ElementTagPrefix))
		{
			// The leaf, so `Element.Demonic` gives `Demonic`, which is what the
			// resistance lookup above is keyed by.
			return FName(*Name.RightChop(FCString::Strlen(ElementTagPrefix)));
		}
	}
	return NAME_None;
}

float UCataclysmDamageCalculation::ArmorReduction(float Armor, int32 Tier)
{
	if (Armor <= 0.0f)
	{
		return 0.0f;
	}
	const float K = ArmorConstantPerTier * static_cast<float>(FMath::Max(1, Tier));
	return FMath::Min(ArmorReductionCap, 100.0f * Armor / (Armor + K));
}

float UCataclysmDamageCalculation::EffectiveResistance(float Resistance,
													   float Penetration)
{
	// Penetration stops at zero. Anything past the target's own resistance is
	// wasted rather than pushing the figure negative, or over-stacking becomes a
	// damage multiplier against the targets that resist least. A resistance that
	// is ALREADY negative is left where it is: that state is inflicted
	// deliberately by enchantments and penetration must not manufacture it.
	// Issue #482, and `effective_resistance` in `sim/cataclysm_sim/damage.py`
	// carries the same rule.
	const float ReachableByPenetration = FMath::Min(Resistance, 0.0f);
	const float Penetrated =
		FMath::Max(Resistance - Penetration, ReachableByPenetration);
	return FMath::Clamp(Penetrated, ResistanceFloor, ResistanceCap);
}

float UCataclysmDamageCalculation::EffectiveDamageReduction(float Reduction)
{
	return FMath::Clamp(Reduction, 0.0f, DamageReductionCap);
}

float UCataclysmDamageCalculation::CombinedMoreDamageReduction(
	const TArray<float>& Factors)
{
	float Remaining = 1.0f;
	for (const float Factor : Factors)
	{
		Remaining *=
			1.0f - FMath::Clamp(Factor, 0.0f, MoreDamageReductionCap) / 100.0f;
	}

	// BOUNDED AT THE END AS WELL AS PER SOURCE, AND THAT IS NOT BELT AND BRACES.
	// "Multiplicative stacking cannot reach 100%" is true of exact arithmetic and
	// FALSE of the arithmetic this actually runs in. A float carries about seven
	// decimal digits, so forty sources of 50% leave 9.1e-13 of the damage, and
	// `100 * (1 - 9.1e-13)` rounds to exactly 100.0f. That is immunity, reached
	// by the layer the design says cannot reach it.
	//
	// FOUND BY THE TEST BELOW RATHER THAN BY READING. The Python model computes
	// the same expression in double precision, where it comes out at
	// 99.99999999999991 and stays under, so the two languages disagreed and only
	// the engine was wrong. The model is bounded the same way now, so the two
	// agree by construction rather than by both being far from the edge.
	return FMath::Min(100.0f * (1.0f - Remaining), MoreDamageReductionCap);
}

void UCataclysmDamageCalculation::StunApplication(float TotalChance,
												  float& OutChance,
												  float& OutSeconds)
{
	const float Chance = FMath::Max(0.0f, TotalChance);
	OutChance = FMath::Min(StunChanceCap, Chance);

	// EVERYTHING PAST CERTAINTY BECOMES DURATION rather than being wasted, which
	// is what stops a stun build hitting a ceiling and every point past it being
	// dead. The multiplier is never below one, so chance under 100% shortens
	// nothing.
	const float Multiplier = FMath::Max(1.0f, Chance / StunChanceCap);
	OutSeconds = FMath::Min(LongestStunSeconds,
							IncidentalStunSeconds * Multiplier);
}

FCataclysmDamageResult UCataclysmDamageCalculation::Resolve(
	const FCataclysmIncomingHit& Hit,
	const UAbilitySystemComponent* Defender,
	int32 Tier,
	float EvasionRoll,
	float BlockRoll,
	float CritRoll)
{
	FCataclysmDamageResult Result;
	if (!Defender || Hit.Damage <= 0.0f)
	{
		return Result;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		Defender->GetSet<UCataclysmVitalAttributeSet>();
	const UCataclysmCombatAttributeSet* Combat =
		Defender->GetSet<UCataclysmCombatAttributeSet>();

	if (!Vitals)
	{
		// Nothing to damage. Better to do nothing than to guess.
		return Result;
	}

	// 1. Evasion. Direct attacks only; area damage lands regardless.
	if (!Hit.bIsArea && Combat)
	{
		const float Roll = EvasionRoll >= 0.0f ? EvasionRoll
											   : FMath::FRandRange(0.0f, 100.0f);
		if (Roll < Combat->GetEvasion())
		{
			Result.bEvaded = true;
			return Result;
		}
	}

	float Damage = Hit.Damage;

	// THE CRITICAL STRIKE, WHICH IS NOT ONE OF THE DESIGN'S EIGHT STEPS. Those
	// eight are what the defender does to a hit. A critical strike belongs to
	// whoever is swinging, and it multiplies the whole finished hit before any
	// mitigation touches it. That is what the model does:
	// `average_damage_per_hit` in `sim/cataclysm_sim/enemy_stats.py` scales the
	// per-hit damage, and `sim/cataclysm_sim/reference_build.py` hands the scaled
	// figure to the mitigation code as the raw hit.
	//
	// IT IS ROLLED HERE AND AVERAGED THERE, and that difference is deliberate.
	// The model has no use for a single hit, so it multiplies every hit by
	// (1 - chance + chance x multiplier), the long-run average. A game cannot do
	// that: a hit that is 15.8% larger than usual is not a critical strike and
	// cannot be drawn as one. Over many hits the two agree, which is what keeps
	// the model's damage targets true of the game.
	//
	// AFTER EVASION AND NOT BEFORE IT. Every step below is a multiplication or a
	// minimum against what is left, so where the multiplier sits among them does
	// not change the number by a fraction. It sits after the evasion step's early
	// return so that a hit which never landed is never reported as a critical
	// strike, which would put an exclamation mark on the word "Evaded".
	if (Hit.CritChance > 0.0f && Hit.CritMultiplier > 0.0f)
	{
		const float Roll = CritRoll >= 0.0f ? CritRoll
											: FMath::FRandRange(0.0f, 100.0f);
		if (Roll < Hit.CritChance)
		{
			Result.bWasCritical = true;
			Damage *= Hit.CritMultiplier / 100.0f;
		}
	}

	// 2. Block. Applies to area damage too, and removes half rather than all.
	if (Combat)
	{
		const float Roll = BlockRoll >= 0.0f ? BlockRoll
											 : FMath::FRandRange(0.0f, 100.0f);
		if (Roll < Combat->GetBlockChance())
		{
			Result.bBlocked = true;
			Damage *= 1.0f - BlockDamageReduction / 100.0f;
		}
	}

	// 3. Armor, after whatever share of it the attacker ignores.
	if (Combat)
	{
		// THE ATTACKER'S OWN STAT PLUS WHAT THE WEAPON IGNORES, clamped once at the
		// end. Mirrors `Attacker.total_armor_ignored` in
		// `sim/cataclysm_sim/damage.py`: gear and sub-type add, and the sum is what
		// the armour step sees. Issue #639 gave the sub-type half somewhere to
		// arrive from; issue #520 gave the gear half an attribute to come from.
		const float FromWeapon = Hit.bIsPiercing ? PiercingArmorIgnored : 0.0f;
		const float Ignored =
			FMath::Clamp(Hit.ArmorPenetration + FromWeapon, 0.0f, 100.0f);
		const float Armor = Combat->GetArmor() * (1.0f - Ignored / 100.0f);
		Damage *= 1.0f - ArmorReduction(Armor, Tier) / 100.0f;
	}

	// 4. Resistance, penetrated first and capped second.
	const float Resist = EffectiveResistance(
		ResistanceFor(Defender, Hit.DamageType), Hit.ResistancePenetration);
	Damage *= 1.0f - Resist / 100.0f;

	// 5. Flat damage reduction, capped. Until issue #644 this was the one step
	// that read a defender's attribute straight into the arithmetic with nothing
	// bounding it, so at 100 a character was exactly immune.
	if (Combat)
	{
		Damage *= 1.0f
			- EffectiveDamageReduction(Combat->GetDamageReduction()) / 100.0f;

		// AND THE MULTIPLICATIVE BUCKET, WHICH THAT CAP DOES NOT REACH. Twelve
		// passive tree nodes grant damage reduction and call it
		// "(multiplicative)". The project owner confirmed on 2026-08-17 that
		// multiplicative means "more", the same word Path of Exile and Last Epoch
		// use, so each source removes a share of what the ones before it left
		// rather than joining the pool above. The 75% cap binds that pool only,
		// and this bucket cannot reach 100% however many sources feed it.
		//
		// ONE ATTRIBUTE HOLDING THE PRODUCT, because an attribute is one number.
		// CombinedMoreDamageReduction is where several sources are multiplied
		// together, and it is what whoever writes the attribute must use.
		//
		// EVERY CHARACTER SITS AT ZERO TODAY, so this changes nothing yet:
		// nothing in game/Source loads a passive tree, so there is no source for
		// it. Issue #665.
		Damage *= 1.0f - FMath::Clamp(Combat->GetDamageReductionMore(),
									  0.0f, MoreDamageReductionCap) / 100.0f;
	}

	// 6. Mana, but only for damage over time and only for a character built for
	// it. Routing damage to mana comes from an enchantment, so there is nothing
	// to read here yet; the step is left in place and does nothing.
	// See the issue on the affix pool.

	// 7. Energy shield. It does not absorb damage over time, which is what makes
	// it a distinct defence rather than a second health bar.
	const bool bShieldApplies = !Hit.bIsDamageOverTime;
	if (bShieldApplies && Vitals->GetEnergyShield() > 0.0f)
	{
		const float Magic = Hit.bIsMagic ? 1.0f + SubtypeBonus / 100.0f : 1.0f;
		Result.AbsorbedByShield =
			FMath::Min(Vitals->GetEnergyShield(), Damage * Magic);
		// Convert what the shield stopped back into raw damage, so the magic
		// bonus never destroys more raw damage than the hit contained.
		Damage = FMath::Max(0.0f, Damage - Result.AbsorbedByShield / Magic);
	}

	// 8. Health takes the remainder.
	if (Hit.bIsSlashing)
	{
		Damage *= 1.0f + SubtypeBonus / 100.0f;
	}
	Result.DealtToHealth = FMath::Min(Damage, Vitals->GetHealth());
	return Result;
}
