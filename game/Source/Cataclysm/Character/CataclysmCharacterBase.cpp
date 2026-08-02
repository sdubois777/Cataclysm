// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"

ACataclysmCharacterBase::ACataclysmCharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

UAbilitySystemComponent* ACataclysmCharacterBase::GetAbilitySystemComponent() const
{
	// Subclasses decide where the component lives.
	return nullptr;
}

void ACataclysmCharacterBase::InitAbilityActorInfo()
{
	// Subclasses supply the owner and avatar.
}
