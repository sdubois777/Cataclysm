// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "Net/UnrealNetwork.h"

UCataclysmResistanceAttributeSet::UCataclysmResistanceAttributeSet()
{
	// No class starts resistant to anything. Resistance comes from gear,
	// passives and, where a class chooses it, its own stat line.
	InitAllResistance(0.0f);
	InitWarResistance(0.0f);
	InitDemonicResistance(0.0f);
	InitDeathResistance(0.0f);
	InitPestilenceResistance(0.0f);
	InitFamineResistance(0.0f);
	InitCelestialResistance(0.0f);
	InitChaosResistance(0.0f);
	InitVoidResistance(0.0f);
}

void UCataclysmResistanceAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, AllResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, WarResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, DemonicResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, DeathResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, PestilenceResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, FamineResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, CelestialResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, ChaosResistance);
	CATACLYSM_REPLICATE(UCataclysmResistanceAttributeSet, VoidResistance);
}

void UCataclysmResistanceAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// Negative resistance is meaningful: several enchantments reduce it, and
	// taking extra damage from a type is a real drawback. So there is a floor
	// well below zero rather than at zero, and NO upper clamp at all.
	//
	// An upper clamp here would delete over-capping, which the design relies on
	// against enemy penetration. The 70% figure caps how much resistance
	// reduces damage, and belongs in the damage calculation.
	NewValue = FMath::Max(NewValue, -100.0f);
}

TArray<FGameplayAttribute> UCataclysmResistanceAttributeSet::GetAllAttributes()
{
	return {
		GetAllResistanceAttribute(),
		GetWarResistanceAttribute(), GetDemonicResistanceAttribute(),
		GetDeathResistanceAttribute(), GetPestilenceResistanceAttribute(),
		GetFamineResistanceAttribute(), GetCelestialResistanceAttribute(),
		GetChaosResistanceAttribute(), GetVoidResistanceAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, AllResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, WarResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, DemonicResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, DeathResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, PestilenceResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, FamineResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, CelestialResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, ChaosResistance)
CATACLYSM_ON_REP(UCataclysmResistanceAttributeSet, VoidResistance)
