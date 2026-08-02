// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystemComponent.h"

UCataclysmGameplayAbility::UCataclysmGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// Abilities run on the server and the result replicates. The alternative,
	// LocalPredicted, is worth adopting per-ability later for responsiveness,
	// but it requires prediction keys to be handled correctly and is not a
	// sensible default to start from.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UCataclysmGameplayAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,
											const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);

	if (bActivateOnGranted && ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle, /*bAllowRemoteActivation=*/false);
	}
}
