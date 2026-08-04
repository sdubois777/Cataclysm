// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "CataclysmUndesignedSkill.generated.h"

/**
 * Occupies a slot for a skill that is named in the design but has no behaviour.
 *
 * WHY THIS SHIPS RATHER THAN BEING A TEST FIXTURE. The weapon skill matrix in
 * docs/All_Things_Cataclysm.xlsx designs 61 skills, and every one of them has a
 * name, a description and gameplay tags but NO numbers: no cooldown, no damage
 * multiplier, no resource cost. There is nothing yet to build a real ability
 * from. Granting this instead means the slot is filled, the key that names that
 * slot reaches something, and swapping weapons visibly changes what is granted
 * -- which is the part of issue #36 that can be built and tested today.
 *
 * It carries the designed skill's name so that anything showing the player their
 * abilities has something true to show, and so an unimplemented slot is
 * identifiable rather than blank.
 *
 * ACTIVATING IT DOES NOTHING AND ENDS IMMEDIATELY. That is deliberate: an
 * ability that silently did something would be worse than one that visibly does
 * not.
 */
UCLASS()
class CATACLYSM_API UCataclysmUndesignedSkill : public UCataclysmGameplayAbility
{
	GENERATED_BODY()

public:
	UCataclysmUndesignedSkill();

	/** The designed skill's name, from the weapon skill matrix. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ability")
	FString SkillName;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ability")
	FString SkillDescription;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;
};
