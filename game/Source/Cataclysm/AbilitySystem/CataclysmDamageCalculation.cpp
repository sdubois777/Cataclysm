// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

namespace
{
	/**
	 * How much of a hit of this type the defender resists.
	 *
	 * TWO PARTS ADDED TOGETHER: a generic figure that applies whatever the hit is,
	 * and the one of eight the hit's own type selects. That shape is what lets the
	 * two sides of a fight use one attribute set differently, which the project
	 * owner ruled for on 2026-08-12: an enemy carries one generic figure, a player
	 * carries eight typed ones.
	 *
	 * AN UNTYPED HIT STILL MEETS THE GENERIC PART. That is the whole reason the
	 * generic part exists. Player damage is deliberately untyped -- the enemy
	 * resists everything equally, so a type would select between eight copies of
	 * one number -- and before issue #486 an untyped hit returned zero here and
	 * the enemy's resistance did nothing at all.
	 */
	float ResistanceFor(const UCataclysmResistanceAttributeSet* Set, FName DamageType)
	{
		if (!Set)
		{
			return 0.0f;
		}

		const float Generic = Set->GetAllResistance();
		if (DamageType.IsNone())
		{
			return Generic;
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
			return Generic + (*Getter)(Set);
		}

		// A damage type nobody has heard of. The generic part still applies,
		// because it applies to everything by definition.
		return Generic;
	}
}

const TCHAR* UCataclysmDamageCalculation::ElementTagPrefix = TEXT("Element.");

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
	return FMath::Clamp(Resistance - Penetration, ResistanceFloor, ResistanceCap);
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
	const UCataclysmResistanceAttributeSet* Resistances =
		Defender->GetSet<UCataclysmResistanceAttributeSet>();

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
		ResistanceFor(Resistances, Hit.DamageType), Hit.ResistancePenetration);
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
