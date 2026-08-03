// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystemComponent.h"

namespace
{
	/** The resistance attribute matching a damage type name. */
	float ResistanceFor(const UCataclysmResistanceAttributeSet* Set, FName DamageType)
	{
		if (!Set || DamageType.IsNone())
		{
			return 0.0f;
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
			return (*Getter)(Set);
		}
		return 0.0f;
	}
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
