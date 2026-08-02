// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "CataclysmAbilitySystemComponent.generated.h"

/**
 * The project's ability system component.
 *
 * Exists as a subclass from the start so that behaviour common to every actor
 * with abilities has somewhere to live without a later refactor touching every
 * character class.
 */
UCLASS()
class CATACLYSM_API UCataclysmAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UCataclysmAbilitySystemComponent();
};
