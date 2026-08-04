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

void ACataclysmCharacterBase::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId ACataclysmCharacterBase::GetGenericTeamId() const
{
	return TeamId;
}

void ACataclysmCharacterBase::InitAbilityActorInfo()
{
	// Subclasses supply the owner and avatar.
}
