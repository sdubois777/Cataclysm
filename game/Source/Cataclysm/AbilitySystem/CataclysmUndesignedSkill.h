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
 * UNDESIGNED SKILLS: 33. NAMED SKILLS: 198. Those are how many rows of
 * `game/Data/WeaponSkills.csv` carry a skill name and no shape, and how many
 * carry a name at all. The other 165 do have a shape and its parameters -- 56
 * Demonic, 28 War (War's Aura, Blood and Iron, since 2026-11-25, 19 more since the War weapon skills' slice WS1,
 * issue #2972, 2026-11-28, and Recoil Dash since slice WS2), and since issue #2580 the Corrupted Stalker's six kits, one
 * weapon's five rows and the Aura for each of Death, Pestilence, Famine,
 * Celestial, Chaos and Void, and since issue #2885 forty-five more Death rows
 * -- and are built as real abilities rather than by this class. (This said "the
 * other 58 ... 51 Demonic" until #2580, when 63 were already shaped, 54 of 153
 * until #2885, 87 of 198 after its first slice, 77 after its second, 68 after
 * its third, 60 after its fourth, and 54 after its fifth, until the Death
 * finish slice A wrote the Effigy whole on 2026-11-26, and 53 of 198 until the War weapon skills' slice WS1.)
 *
 * ISSUE #2885 MADE THIS CLASS A DESIGNED SKILL'S WAITING ROOM AS WELL AS AN
 * UNDESIGNED ONE'S. The 45 Death skills the owner approved on 2026-11-24 were
 * entered with their names, descriptions and numbers, and those whose code is
 * not built yet with no shape: 33 after the first slice, 23 after the second,
 * 14 after the third, 6 after the fourth, 1 after the fifth (the Effigy), and
 * none since the Death finish slice A (2026-11-26) wrote the Effigy whole;
 * `tools/reapply_death_skills_workbook.py` holds each one's shape and the piece
 * it waits on, and writes the shape when that piece is built.
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

	/**
	 * The designed name, so the skill bar can write it even though this ability
	 * does nothing when activated.
	 *
	 * A SLOT HOLDING ONE OF THESE IS NOT AN EMPTY SLOT, and the difference is
	 * worth showing. The skill is named in the design and granted by the equipped
	 * weapon; what it has no numbers for is behaviour.
	 */
	virtual FString DisplayedName() const override { return SkillName; }

	/** The designed description, for the skill bar's pop-up: a skill with no numbers yet still has its words. Issue #1193. */
	virtual FString DisplayedDescription() const override { return SkillDescription; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;
};
