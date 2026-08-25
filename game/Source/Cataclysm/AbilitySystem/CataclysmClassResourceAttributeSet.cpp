// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCataclysmClassResourceAttributeSet::UCataclysmClassResourceAttributeSet()
{
	// Most class resources build up from nothing during a fight rather than
	// starting full, so the current value starts at zero. The maximum matches
	// the 0-100 range the one designed resource uses.
	InitClassResource(0.0f);
	InitMaxClassResource(100.0f);

	// ZERO FOR ALL THREE RATES, AND THAT IS THE DESIGN RATHER THAN A GAP. A
	// character gains and loses no Fervour until it spends a point on a tree's
	// generator node, which is what makes that node worth a point. Issue #954.
	InitFervourFromDamage(0.0f);
	InitFervourFromCost(0.0f);
	InitFervourLostToHealing(0.0f);
}

void UCataclysmClassResourceAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, ClassResource);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, MaxClassResource);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourFromDamage);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourFromCost);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourLostToHealing);
}

void UCataclysmClassResourceAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetClassResourceAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxClassResource());
	}
	else if (Attribute == GetMaxClassResourceAttribute())
	{
		// Zero is legitimate: a character with no class tree invested has no
		// resource, and should not be given a phantom one.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetFervourFromDamageAttribute()
		|| Attribute == GetFervourFromCostAttribute()
		|| Attribute == GetFervourLostToHealingAttribute())
	{
		// FLOORED AT ZERO, WHICH MEANS "THIS DOES NOT MOVE THE BAR". A negative
		// rate would invert the rule the node states: taking damage would empty
		// Fervour and healing would fill it. Nothing in the design asks for
		// that, and the one node that reduces a rate -- the Masochist's Staunch,
		// at 5% per point over at most six points -- reaches 70% of the rate and
		// not past zero. Issue #954.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCataclysmClassResourceAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetClassResourceAttribute())
	{
		SetClassResource(
			FMath::Clamp(GetClassResource(), 0.0f, GetMaxClassResource()));
	}
}

TArray<FGameplayAttribute> UCataclysmClassResourceAttributeSet::GetAllAttributes()
{
	TArray<FGameplayAttribute> All = {
		GetClassResourceAttribute(), GetMaxClassResourceAttribute()
	};
	All.Append(GetRateAttributes());
	return All;
}

TArray<FGameplayAttribute> UCataclysmClassResourceAttributeSet::GetRateAttributes()
{
	return { GetFervourFromDamageAttribute(), GetFervourFromCostAttribute(),
			 GetFervourLostToHealingAttribute() };
}

CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, ClassResource)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, MaxClassResource)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourFromDamage)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourFromCost)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourLostToHealing)
