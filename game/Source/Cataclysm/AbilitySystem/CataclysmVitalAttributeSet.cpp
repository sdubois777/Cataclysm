// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCataclysmVitalAttributeSet::UCataclysmVitalAttributeSet()
{
	// Placeholders only. Real starting values come from a class stat line
	// applied as a gameplay effect; the three Demonic classes are in the design
	// document. These exist so an attribute set constructed with no class
	// attached is still in a valid state rather than at zero health.
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitMana(50.0f);
	InitMaxMana(50.0f);
	InitEnergyShield(0.0f);
	InitMaxEnergyShield(0.0f);
	InitHealthRegen(1.0f);
	InitManaRegen(1.0f);
	InitEnergyShieldRegen(0.0f);
	InitLifeLeech(0.0f);
	InitDamage(0.0f);
}

void UCataclysmVitalAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, Health);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxHealth);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, Mana);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxMana);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShield);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxEnergyShield);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, HealthRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, ManaRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShieldRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, LifeLeech);
	// Damage is a meta attribute. It is never replicated.
}

void UCataclysmVitalAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxMana());
	}
	else if (Attribute == GetEnergyShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxEnergyShield());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// The one maximum that cannot be zero. See the class comment.
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetMaxManaAttribute()
		|| Attribute == GetMaxEnergyShieldAttribute()
		|| Attribute == GetHealthRegenAttribute()
		|| Attribute == GetManaRegenAttribute()
		|| Attribute == GetEnergyShieldRegenAttribute()
		|| Attribute == GetLifeLeechAttribute())
	{
		// Zero is a legitimate value for all of these. A class with no energy
		// shield is a design position, not an error state.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCataclysmVitalAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float LocalDamage = GetDamage();
		SetDamage(0.0f);

		if (LocalDamage > 0.0f)
		{
			// INCOMPLETE ON PURPOSE. The full order -- evasion, block, armor,
			// resistance, penetration, energy shield, and the weapon sub-type
			// modifiers -- is not implemented here, because several steps of it
			// are not yet designed. There is no formula converting armor into
			// damage reduction, no formula for penetration, and no rule saying
			// whether energy shield absorbs before health.
			//
			// Applying damage straight to health is the honest placeholder: it
			// is obviously incomplete rather than quietly wrong. Energy shield
			// currently absorbs nothing, which is why the Ritualist's shield
			// does not defend it yet.
			SetHealth(FMath::Clamp(GetHealth() - LocalDamage, 0.0f, GetMaxHealth()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.0f, GetMaxMana()));
	}
	else if (Data.EvaluatedData.Attribute == GetEnergyShieldAttribute())
	{
		SetEnergyShield(FMath::Clamp(GetEnergyShield(), 0.0f, GetMaxEnergyShield()));
	}
}

TArray<FGameplayAttribute> UCataclysmVitalAttributeSet::GetAllAttributes()
{
	return {
		GetHealthAttribute(), GetMaxHealthAttribute(),
		GetManaAttribute(), GetMaxManaAttribute(),
		GetEnergyShieldAttribute(), GetMaxEnergyShieldAttribute(),
		GetHealthRegenAttribute(), GetManaRegenAttribute(),
		GetEnergyShieldRegenAttribute(), GetLifeLeechAttribute(),
		GetDamageAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, Health)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxHealth)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, Mana)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxMana)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShield)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxEnergyShield)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, HealthRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, ManaRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShieldRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, LifeLeech)
