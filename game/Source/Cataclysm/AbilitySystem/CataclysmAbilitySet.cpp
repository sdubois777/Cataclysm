// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAbilitySet.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AttributeSet.h"
#include "GameplayEffect.h"

void FCataclysmAbilitySetHandles::AddAbility(const FGameplayAbilitySpecHandle& Handle)
{
	if (Handle.IsValid())
	{
		AbilitySpecHandles.Add(Handle);
	}
}

void FCataclysmAbilitySetHandles::AddEffect(const FActiveGameplayEffectHandle& Handle)
{
	if (Handle.IsValid())
	{
		EffectHandles.Add(Handle);
	}
}

void FCataclysmAbilitySetHandles::AddAttributeSet(UAttributeSet* Set)
{
	GrantedAttributeSets.Add(Set);
}

void FCataclysmAbilitySetHandles::TakeFromAbilitySystem(UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& Handle : AbilitySpecHandles)
	{
		if (Handle.IsValid())
		{
			AbilitySystem->ClearAbility(Handle);
		}
	}

	for (const FActiveGameplayEffectHandle& Handle : EffectHandles)
	{
		if (Handle.IsValid())
		{
			AbilitySystem->RemoveActiveGameplayEffect(Handle);
		}
	}

	for (UAttributeSet* Set : GrantedAttributeSets)
	{
		AbilitySystem->RemoveSpawnedAttribute(Set);
	}

	AbilitySpecHandles.Reset();
	EffectHandles.Reset();
	GrantedAttributeSets.Reset();
}

void UCataclysmAbilitySet::GiveToAbilitySystem(UCataclysmAbilitySystemComponent* AbilitySystem,
											   FCataclysmAbilitySetHandles* OutHandles,
											   UObject* SourceObject) const
{
	check(AbilitySystem);

	// Granting is server-only. On a client this silently does nothing useful,
	// so fail loudly rather than leave a character with no abilities and no
	// explanation.
	if (!AbilitySystem->IsOwnerActorAuthoritative())
	{
		return;
	}

	// Attribute sets first: abilities and effects may reference the attributes
	// they define, and an effect applied before its attribute set exists is
	// silently dropped.
	for (const TSubclassOf<UAttributeSet>& SetClass : GrantedAttributeSets)
	{
		if (!IsValid(SetClass))
		{
			continue;
		}

		UAttributeSet* NewSet = NewObject<UAttributeSet>(AbilitySystem->GetOwner(), SetClass);
		AbilitySystem->AddAttributeSetSubobject(NewSet);

		if (OutHandles)
		{
			OutHandles->AddAttributeSet(NewSet);
		}
	}

	for (const FCataclysmAbilitySetEntry& Entry : GrantedAbilities)
	{
		if (!IsValid(Entry.Ability))
		{
			continue;
		}

		FGameplayAbilitySpec Spec(Entry.Ability, Entry.AbilityLevel);
		Spec.SourceObject = SourceObject;

		const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbility(Spec);
		if (OutHandles)
		{
			OutHandles->AddAbility(Handle);
		}
	}

	for (const TSubclassOf<UGameplayEffect>& EffectClass : GrantedEffects)
	{
		if (!IsValid(EffectClass))
		{
			continue;
		}

		const UGameplayEffect* Effect = EffectClass->GetDefaultObject<UGameplayEffect>();
		const FActiveGameplayEffectHandle Handle = AbilitySystem->ApplyGameplayEffectToSelf(
			Effect, /*Level=*/1.0f, AbilitySystem->MakeEffectContext());

		if (OutHandles)
		{
			OutHandles->AddEffect(Handle);
		}
	}
}
