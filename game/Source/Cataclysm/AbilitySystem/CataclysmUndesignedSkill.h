// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "CataclysmUndesignedSkill.generated.h"

/**
 * Occupies a slot for a skill that is named in the design but has no behaviour.
 *
 * WHY THIS SHIPS RATHER THAN BEING A TEST FIXTURE. Some of the skills the design
 * names have nothing to build an ability from -- no shape, no cooldown, no damage
 * multiplier, no resource cost -- only a name, a description and gameplay tags.
 * Granting this instead means the slot is filled, the key that names that slot
 * reaches something, and swapping weapons visibly changes what is granted, which
 * is the part of issue #36 that can be built and tested today.
 *
 * UNDESIGNED SKILLS: 54. NAMED SKILLS: 112. Those are how many rows of
 * `game/Data/WeaponSkills.csv` carry a skill name and no shape, and how many
 * carry a name at all. The other 58 do have a shape and its parameters -- 51
 * Demonic and 7 War -- and are built as real abilities rather than by this
 * class.
 *
 * THIS COMMENT USED TO SAY THAT *EVERY* DESIGNED SKILL HAD NO NUMBERS, AND THAT
 * WENT ON BEING WRONG FOR WEEKS. It was written on 2026-08-03, when it was true.
 * On 2026-08-21 it was read, believed, and a recommendation was made on it that
 * was wrong. The count above is checked against the table by
 * `tools/tests/test_undesigned_skill_count_is_true.py`, so it cannot go stale
 * quietly again: designing one of the 54 fails that test until this number is
 * corrected.
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
