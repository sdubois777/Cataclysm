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
	// Zero until a passive tree grants one, which nothing does yet. See the
	// attribute's comment for why that is stated rather than left to be found.
	InitDamageReductionMore(0.0f);
	InitRetaliation(0.0f);
	InitCrowdControlResistance(0.0f);

	InitAttackDamage(0.0f);      // supplied by the equipped weapon
	InitCritChance(0.0f);        // supplied by the skill in use
	// 100 UNLESS AN ENCHANTMENT LOWERS IT. Nothing does yet; see the attribute.
	InitMaxCritChance(CritChanceCap);
	InitCritMultiplier(150.0f);  // a critical strike is worth 1.5x by default
	InitAttackSpeed(0.0f);       // supplied by the equipped weapon

	// Percentages of what the skill or the effect itself does, so 100 means
	// "unchanged". Zero here would leave Efficacy nothing to scale.
	//
	// The three damage over time levers all start at 100 for the same reason.
	// They multiply each other, so a zero on any one of them would take a
	// damage over time build's whole output to zero rather than only its own
	// third of it. Issue #205.
	InitAreaOfEffect(100.0f);
	InitDotDamage(100.0f);
	InitDotFrequency(100.0f);
	InitDotDuration(100.0f);

	InitPenetration(0.0f);
	// Zero, not 100, because it is an added percentage rather than a
	// percentage OF something. Same shape as the resistance penetration
	// above, and the rule test_stat_baselines_match_the_attribute_set.py
	// holds. Issue #520.
	InitArmorPenetration(0.0f);
	InitSpellDamage(0.0f);

	// Increased damage against one type of enemy. Zero for every class: no class
	// is born better against a Cataclysm, because which Cataclysms a run faces
	// is drawn at run start and the class is chosen before that.
	InitDamageVsWar(0.0f);
	InitDamageVsDemonic(0.0f);
	InitDamageVsDeath(0.0f);
	InitDamageVsPestilence(0.0f);
	InitDamageVsFamine(0.0f);
	InitDamageVsCelestial(0.0f);
	InitDamageVsChaos(0.0f);
	InitDamageVsVoid(0.0f);

	InitMovementSpeed(4.0f);     // metres per second
	InitCooldownReduction(0.0f);

	// ZERO, AND IT STAYS ZERO ON THIS ATTRIBUTE BY DESIGN. Issue #973. Its only
	// source is a passive node carrying a health condition, and a conditional
	// bonus is never written onto a gameplay attribute. See the header.
	InitCooldownSkipChance(0.0f);

	// ZERO, AND IT STAYS ZERO FOR ANY CHARACTER WITHOUT THE NODE.
	// Issue #1032. The Masochist's Mutilation Mastery is its only source.
	InitBleedOnCritChance(0.0f);

	// AND DAMAGE OVER TIME HURTS EVERY CHARACTER UNLESS ONE CAPSTONE OPTION
	// SAYS OTHERWISE. Issue #1039. The Masochist's Vessel Unbroken is its only
	// source, and zero is the ordinary case.
	InitDebuffDamageSuppressed(0.0f);

	// A HUNDRED MEANS UNCHANGED, the same as AreaOfEffect above. Issue #1026.
	// A character built by `UCataclysmPlayerClassStats::ApplyTo` has both
	// overwritten from `EngineSuppliedBases`, which states the same 100. This is
	// what an ability system that never runs that holds -- every enemy, and any
	// test that builds one by hand -- and it means such a character takes exactly
	// the damage it always did rather than none at all.
	InitDamageTaken(100.0f);
	InitDamageOverTimeTaken(100.0f);

	// Magic find is an added percentage and has a flat source, the "Flat magic
	// find" affix, so zero is right. Loot quantity is a percentage of what the
	// dungeon would otherwise drop and every source of it is an increase, so
	// 100 means "unchanged" and zero would leave every source nothing to
	// scale. Issue #243.
	InitMagicFind(0.0f);
	InitLootQuantity(100.0f);
}

void UCataclysmCombatAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Armor);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Evasion);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, BlockChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageReduction);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageReductionMore);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Retaliation);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CrowdControlResistance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, AttackDamage);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CritChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, MaxCritChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CritMultiplier);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, AttackSpeed);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, AreaOfEffect);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DotDamage);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DotFrequency);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DotDuration);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, Penetration);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, ArmorPenetration);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, SpellDamage);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsWar);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsDemonic);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsDeath);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsPestilence);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsFamine);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsCelestial);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsChaos);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageVsVoid);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, MovementSpeed);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CooldownReduction);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, CooldownSkipChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, BleedOnCritChance);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageTaken);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DamageOverTimeTaken);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, DebuffDamageSuppressed);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, MagicFind);
	CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, LootQuantity);
}

void UCataclysmCombatAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetCritChanceAttribute())
	{
		// THE CAP IS THIS CHARACTER'S RATHER THAN THE GAME'S, since issue #680.
		// It is 100 for everyone until an enchantment lowers it, and the
		// Enchantments sheet of docs/All_Things_Cataclysm.xlsx carries one that
		// does: "Your critical hit chance cannot exceed 30%-50%". Reading the
		// attribute rather than the constant is what gives it somewhere to land.
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxCritChance());
		return;
	}

	// A CHANCE IS BETWEEN NOTHING AND CERTAINTY. Issue #973. A value above 100
	// is not a larger chance and a negative one is not a smaller chance; both
	// are data that means nothing, and clamping says so once here rather than at
	// the roll. No node reaches 100: The Catalyst stops at 40.
	if (Attribute == GetCooldownSkipChanceAttribute()
		|| Attribute == GetBleedOnCritChanceAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, 100.0f);
		return;
	}

	if (Attribute == GetMaxCritChanceAttribute())
	{
		// THE CEILING ITSELF IS BOUNDED BY THE CONSTANT, so it can be lowered and
		// never raised. The project owner ruled on 2026-08-17 that critical strike
		// chance is hard-capped at 100% and nothing raises it. That is the
		// opposite of maximum resistance, where one enchantment raises the cap to
		// a ceiling of 90%.
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
	//
	// THAT IS NOW TRUE OF DAMAGE REDUCTION AND IT WAS NOT UNTIL ISSUE #644. The
	// paragraph above described where the bound belonged, and no bound existed
	// in the damage calculation or anywhere else, so a character at 100 took
	// nothing at all. It is capped now, at
	// UCataclysmDamageCalculation::DamageReductionCap, inside
	// EffectiveDamageReduction -- which is exactly where this comment always
	// said it should be.
	//
	// RETALIATION IS STILL UNBOUNDED AND THAT IS A DIFFERENT QUESTION. It deals
	// damage back rather than preventing any, so no amount of it makes anything
	// immune to anything.
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
		GetDamageReductionAttribute(), GetDamageReductionMoreAttribute(),
		GetRetaliationAttribute(),
		GetCrowdControlResistanceAttribute(),
		GetAttackDamageAttribute(),
		GetCritChanceAttribute(), GetMaxCritChanceAttribute(),
		GetCritMultiplierAttribute(),
		GetAttackSpeedAttribute(), GetAreaOfEffectAttribute(),
		GetDotDamageAttribute(), GetDotFrequencyAttribute(),
		GetDotDurationAttribute(), GetPenetrationAttribute(),
		GetArmorPenetrationAttribute(),
		GetSpellDamageAttribute(),
		GetDamageVsWarAttribute(), GetDamageVsDemonicAttribute(),
		GetDamageVsDeathAttribute(), GetDamageVsPestilenceAttribute(),
		GetDamageVsFamineAttribute(), GetDamageVsCelestialAttribute(),
		GetDamageVsChaosAttribute(), GetDamageVsVoidAttribute(),
		GetMovementSpeedAttribute(), GetCooldownReductionAttribute(),
		GetCooldownSkipChanceAttribute(), GetBleedOnCritChanceAttribute(),
		GetDamageTakenAttribute(), GetDamageOverTimeTakenAttribute(),
		GetDebuffDamageSuppressedAttribute(),
		GetMagicFindAttribute(), GetLootQuantityAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Armor)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Evasion)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, BlockChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageReduction)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageReductionMore)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Retaliation)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CrowdControlResistance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, AttackDamage)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CritChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, MaxCritChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CritMultiplier)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, AttackSpeed)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, AreaOfEffect)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DotDamage)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DotFrequency)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DotDuration)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, Penetration)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, ArmorPenetration)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, SpellDamage)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsWar)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsDemonic)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsDeath)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsPestilence)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsFamine)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsCelestial)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsChaos)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageVsVoid)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, MovementSpeed)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CooldownReduction)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, CooldownSkipChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, BleedOnCritChance)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageTaken)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DamageOverTimeTaken)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, DebuffDamageSuppressed)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, MagicFind)
CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, LootQuantity)
