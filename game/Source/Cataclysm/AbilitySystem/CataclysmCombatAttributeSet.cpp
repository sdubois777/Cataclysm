// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "Net/UnrealNetwork.h"

UCataclysmCombatAttributeSet::UCataclysmCombatAttributeSet()
{
	// Defence and utility start at zero: a character has these only if its
	// class, gear or passives give them. Attributes cannot create a stat, so a
	// zero here means the matching attribute does nothing, which is how a class
	// declines to care about a stat.
	InitArmor(0.0f);
	InitEvasion(0.0f);
	InitBlockChance(0.0f);
	InitDamageReduction(0.0f);
	InitRetaliation(0.0f);
	InitCrowdControlResistance(0.0f);

	InitAttackDamage(0.0f);      // supplied by the equipped weapon
	InitCritChance(0.0f);        // supplied by the skill in use
	InitCritMultiplier(150.0f);  // a critical strike is worth 1.5x by default
	InitAttackSpeed(0.0f);       // supplied by the equipped weapon

	// Percentages of what the skill itself does, so 100 means "unchanged".
	// Zero here would leave Efficacy nothing to scale.
	InitAreaOfEffect(100.0f);
	InitDotFrequency(100.0f);

	InitPenetration(0.0f);
	InitSpellDamage(0.0f);

	InitMovementSpeed(4.0f);     // metres per second
	InitCooldownReduction(0.0f);
	InitMagicFind(0.0f);
	InitLootQuantity(0.0f);
}

void UCataclysmCombatAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Armor);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Evasion);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, BlockChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageReduction);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Retaliation);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CrowdControlResistance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, AttackDamage);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CritChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CritMultiplier);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, AttackSpeed);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, AreaOfEffect);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DotFrequency);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Penetration);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, SpellDamage);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, MovementSpeed);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CooldownReduction);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, MagicFind);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, LootQuantity);
}

void UCataclysmCombatAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetCritChanceAttribute())
	{
		// The one hard cap on this set.
		NewValue = FMath::Clamp(NewValue, 0.0f, CritChanceCap);
		return;
	}

	// Evasion is NOT clamped to its 60% soft cap, and block chance has no cap
	// at all. Both are deliberate: gear enchantments may exceed the evasion cap,
	// and a block removes only half a hit's damage, so even 100% block chance is
	// not immunity.
	//
	// Damage reduction and retaliation are likewise left unbounded here; where
	// they need bounding is in the damage calculation, against the final number,
	// not against each contributing stat.
	NewValue = FMath::Max(NewValue, 0.0f);
}

float UCataclysmCombatAttributeSet::FinalCooldown(float BaseCooldown,
												  float CooldownIncreases,
												  float MoreMultiplier)
{
	// Divide, never subtract. Subtracting reaches zero at 100 points of
	// Efficacy; dividing halves the cooldown there and can never reach zero.
	//
	// The more multiplier divides as well rather than multiplying. A cooldown is
	// a rate, so a source that makes it shorter has to divide; multiplying would
	// mean a cooldown reduction gem made the cooldown longer. Because both
	// buckets divide, no number of sources reaches zero.
	return BaseCooldown / CooldownDivisor(CooldownIncreases, MoreMultiplier);
}

float UCataclysmCombatAttributeSet::DisplayedCooldownReduction(
	float CooldownIncreases, float MoreMultiplier)
{
	const float Divisor = CooldownDivisor(CooldownIncreases, MoreMultiplier);
	return 100.0f * (Divisor - 1.0f) / Divisor;
}

float UCataclysmCombatAttributeSet::CooldownDivisor(float CooldownIncreases,
													float MoreMultiplier)
{
	// Both brackets are floored above zero so the division cannot produce a
	// negative or infinite interval from bad data.
	const float Increases = FMath::Max(CooldownIncreases, 0.0f);
	const float More = FMath::Max(MoreMultiplier, UE_SMALL_NUMBER);
	return (1.0f + Increases) * More;
}

TArray<FGameplayAttribute> UCataclysmCombatAttributeSet::GetAllAttributes()
{
	return {
		GetArmorAttribute(), GetEvasionAttribute(), GetBlockChanceAttribute(),
		GetDamageReductionAttribute(), GetRetaliationAttribute(),
		GetCrowdControlResistanceAttribute(),
		GetAttackDamageAttribute(),
		GetCritChanceAttribute(), GetCritMultiplierAttribute(),
		GetAttackSpeedAttribute(), GetAreaOfEffectAttribute(),
		GetDotFrequencyAttribute(), GetPenetrationAttribute(),
		GetSpellDamageAttribute(),
		GetMovementSpeedAttribute(), GetCooldownReductionAttribute(),
		GetMagicFindAttribute(), GetLootQuantityAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Armor)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Evasion)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, BlockChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageReduction)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Retaliation)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CrowdControlResistance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, AttackDamage)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CritChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CritMultiplier)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, AttackSpeed)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, AreaOfEffect)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DotFrequency)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Penetration)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, SpellDamage)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, MovementSpeed)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CooldownReduction)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, MagicFind)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, LootQuantity)
