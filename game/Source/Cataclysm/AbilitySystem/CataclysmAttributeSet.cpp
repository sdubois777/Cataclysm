// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCataclysmAttributeSet::UCataclysmAttributeSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitDamage(0.0f);
}

void UCataclysmAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// REPNOTIFY_Always matters here. Without it a value that replicates back to
	// the same number it already had -- healing to full, taking zero damage --
	// fires no notify, and any UI driven off the notify silently stops updating.
	DOREPLIFETIME_CONDITION_NOTIFY(UCataclysmAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCataclysmAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	// Damage is a meta attribute. It is never replicated.
}

void UCataclysmAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}

void UCataclysmAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// Every source of damage routes through the Damage meta attribute so that
	// mitigation is resolved in exactly one place. When the full combat model
	// arrives, armour, block, resistance, penetration and energy shield are
	// applied here, in a defined order, before Health is touched.
	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float LocalDamage = GetDamage();
		SetDamage(0.0f);

		if (LocalDamage > 0.0f)
		{
			SetHealth(FMath::Clamp(GetHealth() - LocalDamage, 0.0f, GetMaxHealth()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
}

void UCataclysmAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCataclysmAttributeSet, Health, OldValue);
}

void UCataclysmAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCataclysmAttributeSet, MaxHealth, OldValue);
}
