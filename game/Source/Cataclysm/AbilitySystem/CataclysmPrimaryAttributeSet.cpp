// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "Net/UnrealNetwork.h"

UCataclysmPrimaryAttributeSet::UCataclysmPrimaryAttributeSet()
{
	// A new character has spent nothing. Points are granted one per level and
	// allocated by the player, so every attribute starts at zero rather than at
	// some notional baseline.
	InitAgility(0.0f);
	InitFerocity(0.0f);
	InitConstitution(0.0f);
	InitVitality(0.0f);
	InitMind(0.0f);
	InitSpirit(0.0f);
	InitEfficacy(0.0f);
	InitLuck(0.0f);
}

void UCataclysmPrimaryAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Agility);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Ferocity);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Constitution);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Vitality);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Mind);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Spirit);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Efficacy);
	CATACLYSM_REPLICATE(UCataclysmPrimaryAttributeSet, Luck);
}

void UCataclysmPrimaryAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// Attribute points cannot go negative. There is no upper clamp here: the
	// budget is one point per level and is enforced where points are spent, not
	// here, because gear and passives may also grant attributes.
	NewValue = FMath::Max(NewValue, 0.0f);

	// An attribute is a whole number of points. The affix that raises one is a
	// percentage, so 33 Spirit with a +12% affix arrives here as 36.96 and
	// leaves as 37. Rounding here rather than in the character screen is the
	// point of the rule: every reader of the attribute gets the same number, so
	// a player cannot compute what 37 Spirit should give and be handed what
	// 36.96 gives. Issue #225.
	NewValue = RoundedPoints(NewValue);
}

float UCataclysmPrimaryAttributeSet::RoundedPoints(float Raw)
{
	// FMath::RoundToFloat rounds a half away from zero, and an attribute is
	// never negative, so this is "round half up" as the design states it.
	return FMath::RoundToFloat(FMath::Max(Raw, 0.0f));
}

TArray<FGameplayAttribute> UCataclysmPrimaryAttributeSet::GetAllAttributes()
{
	return {
		GetAgilityAttribute(), GetFerocityAttribute(), GetConstitutionAttribute(),
		GetVitalityAttribute(), GetMindAttribute(), GetSpiritAttribute(),
		GetEfficacyAttribute(), GetLuckAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Agility)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Ferocity)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Constitution)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Vitality)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Mind)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Spirit)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Efficacy)
CATACLYSM_ON_REP(UCataclysmPrimaryAttributeSet, Luck)
