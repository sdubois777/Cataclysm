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
}

void UCataclysmClassResourceAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, ClassResource);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, MaxClassResource);
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
	return { GetClassResourceAttribute(), GetMaxClassResourceAttribute() };
}

CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, ClassResource)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, MaxClassResource)
