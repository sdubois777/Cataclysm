// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "Net/UnrealNetwork.h"

UCataclysmAllResistanceAttributeSet::UCataclysmAllResistanceAttributeSet()
{
	// Nothing resists anything by default. A creature's figure comes from its
	// own designed stat block, through ACataclysmEnemyCharacter's
	// ResistancePercent, and the Imp's really is zero.
	InitAllResistance(0.0f);
}

void UCataclysmAllResistanceAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmAllResistanceAttributeSet, AllResistance);
}

void UCataclysmAllResistanceAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// The same floor and the same absence of a ceiling as the eight typed
	// resistances. Negative resistance means taking extra damage, which is a real
	// drawback several enchantments inflict; an upper clamp would delete
	// over-capping, and the 70% figure caps how much resistance reduces damage
	// rather than how much of it a character may hold.
	NewValue = FMath::Max(NewValue, -100.0f);
}

TArray<FGameplayAttribute> UCataclysmAllResistanceAttributeSet::GetAllAttributes()
{
	return { GetAllResistanceAttribute() };
}

CATACLYSM_ON_REP(UCataclysmAllResistanceAttributeSet, AllResistance)
