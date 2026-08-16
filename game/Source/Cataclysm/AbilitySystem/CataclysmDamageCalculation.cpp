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

FCataclysmDamageResult UCataclysmDamageCalculation::Resolve(
	const FCataclysmIncomingHit& Hit,
	const UAbilitySystemComponent* Defender,
	int32 Tier,
	float EvasionRoll,
	float BlockRoll)
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
		const float Ignored = FMath::Clamp(Hit.ArmorPenetration, 0.0f, 100.0f);
		const float Armor = Combat->GetArmor() * (1.0f - Ignored / 100.0f);
		Damage *= 1.0f - ArmorReduction(Armor, Tier) / 100.0f;
	}

	// 4. Resistance, penetrated first and capped second.
	const float Resist = EffectiveResistance(
		ResistanceFor(Defender, Hit.DamageType), Hit.ResistancePenetration);
	Damage *= 1.0f - Resist / 100.0f;

	// 5. Flat damage reduction.
	if (Combat)
	{
		Damage *= 1.0f - Combat->GetDamageReduction() / 100.0f;
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
