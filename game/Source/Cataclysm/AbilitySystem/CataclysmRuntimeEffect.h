// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "CataclysmRuntimeEffect.generated.h"

/**
 * A gameplay effect built at run time by `UCataclysmSkillEffects`, for one application. Issue #2683.
 *
 * EVERY EFFECT `MakeRuntimeEffect` BUILDS IS ONE OF THESE, and it is an ordinary `UGameplayEffect` in everything but
 * the one field below: it adds no behaviour and overrides nothing. The engine stacks effects by comparing `Spec.Def`
 * pointers, not classes. Searched 2026-11-01: no code in `game/Source` asks a running effect's class -- no
 * `FGameplayEffectQuery::EffectDefinition`, no removal or count by effect class. The whole automation suite is the
 * check that nothing else changed (ruling J4).
 *
 * IT EXISTS SO A DAMAGE-OVER-TIME EFFECT CAN CARRY A SKILL'S TAGS WITHOUT THE SKILL. A periodic tick reaches the
 * struck character's attribute set long after the call that laid it has returned, so a pointer handed to that call,
 * as `FCataclysmHitDelivery::SkillTagsWithNoSkill` is handed to an instant blow, would be gone by then. The effect
 * itself is what every tick is executed from: `FActiveGameplayEffectsContainer::InternalExecutePeriodicGameplayEffect`
 * executes the active effect's own spec, and `Spec.Def` is this object. So the tags live here.
 */
UCLASS()
class CATACLYSM_API UCataclysmRuntimeEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/**
	 * The tags of the skill this effect's ticks are ticks of, or empty. Issue #2683. Ruled by the coordinating session
	 * 2026-11-01 (J1): IDENTITY ONLY. Written by `UCataclysmSkillEffects::ApplyDamageOverTime` and read by
	 * `UCataclysmSkillEffects::SkillTagsOfABlowWithNoSkill` for a tick whose effect names no skill that still exists,
	 * so the record of the last blow keeps them for the death notice's `KillingSkillTags` and the hit notice names
	 * them. NOTHING THAT PRICES THE EFFECT READS THEM: the burn was priced before they were written, from a real skill
	 * only.
	 *
	 * Written for a buried weapon's burn (its throwing skill's), a ground zone's own ailment (the zone's skill's), a
	 * tether's or a projectile's burn whose skill has gone (the tags it kept), and for every damage over time laid
	 * with a skill (that skill's own, so a burn whose skill goes mid-burn keeps them). Empty for every other effect.
	 *
	 * A UPROPERTY so the container is reflected with the effect it belongs to; nothing edits or saves it.
	 */
	UPROPERTY(Transient)
	FGameplayTagContainer SkillTagsWithNoSkill;
};
