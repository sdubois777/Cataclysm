// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmTriggeredSkill.generated.h"

class AActor;
struct FCataclysmWeaponSkill;

/**
 * A skill a character did not press: granted from its row with no key, started free at an aim point, and removed when
 * it ends. Built for `Chaos_Wild_Magic`, "a 5% chance to trigger the effect of a random different skill". Issue #41.
 *
 * WHY A GRANT AND NOT A FUNCTION THAT RUNS A ROW. A skill's effect is the body of its template's `ActivateAbility`,
 * an instance method that reads the instance's parameters, tags and timers. Nothing runs a row without an instance,
 * and writing one would be a second copy of every shape. So the row gets a real instance for one use.
 *
 * WHAT "FREE" IS: FOLLOW THROUGH'S, UNCHANGED. `bFreeRepeat` and `FreeRepeatAim` (issue #1515, ruled 2026-09-24):
 * no mana, no cooldown started or asked, no health cost, no skill-used notice, no next-use charge spent, and the aim
 * is the point given rather than the cursor. A triggered skill therefore cannot trigger another through the notice.
 * WHAT IS NOT FREE, as for Follow Through: Fervour a hit earns or buys, the turn to face the aim, the attack clip.
 *
 * THREE ENGINE RULES SHAPE THE ORDER, each read in UE 5.8's AbilitySystemComponent_Abilities.cpp:
 *
 *   - `TryActivateAbility` refuses a spec whose `RemoveAfterActivation` is already set, so the flag is set AFTER the
 *     activation, through `SetRemoveAbilityOnEnd`, which removes at once a skill that has already ended.
 *   - `GiveAbility` inside another ability's activation is deferred and returns a handle with no instance behind it,
 *     so there is nothing to stamp. `Trigger` refuses then and takes the pending grant back. A CALLER REACTING TO A
 *     SKILL USE MUST WAIT FOR THE NEXT TICK, as Follow Through does and for the same reason.
 *   - A spec is removed inside the `EndAbility` call that ends it, on the authority.
 *
 * NO KEY FINDS IT. Input finds a skill by the `Slot.*` tag on its spec, which `GiveAbilityInSlot` adds and this does
 * not. The slot PROPERTY is set, because that is where a skill finds its damage percent when its row states none.
 * It reads the equipped weapon's damage and speed like any skill; no template compares a row's weapon type with the
 * weapon held.
 *
 * WHAT A REMOVED SKILL NO LONGER ANSWERS FOR. A blow carries a weak pointer to the skill that dealt it. Once the
 * skill is removed, a burn it left that kills later names no killing skill, and a thrown rack's last axes lose it
 * too. Their damage is unchanged: every figure is copied when the blow or the projectile is made.
 *
 * NOT EVERY ROW SHOULD BE TRIGGERED, and this does not decide which. A skill that waits for a key release, moves the
 * character or reserves a resource is the caller's to leave out; `Chaos_Wild_Magic`'s pool is where that is ruled.
 */
UCLASS()
class CATACLYSM_API UCataclysmTriggeredSkill : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Grants the row's skill to the character with no key, starts it free at `Aim`, and removes it when it ends.
	 *
	 * @return whether the skill started. False leaves nothing behind: for a row with no shape, for a character with
	 *         no ability system, when called inside another ability's activation, and when the skill itself refuses
	 *         (locked skills, an unmet `Requires`).
	 */
	static bool Trigger(AActor* Character, const FCataclysmWeaponSkill& Skill, const FVector& Aim,
						float DamageShare = 1.0f);

	/**
	 * Whether a row action may repeat this skill. Mechanism B2, ruled 2026-10-05.
	 *
	 * WILD MAGIC'S RULE AND ONE MORE: every skill `UCataclysmDungeonModifierEffects::WildMagicLeavesOut` lets
	 * through, EXCEPT A SELF BUFF. A second running copy of a self buff adds a second More modifier, and on gear that
	 * is worn for a whole character that is on every press; Wild Magic's own pool keeps its self buffs. ITS OWN NAMED
	 * RULE so that pool does not change when this one does.
	 */
	static bool RepeatsFromARow(const FCataclysmWeaponSkill& Skill);

	/**
	 * Makes the repeat a row action recorded on the character's ability system, and clears it: the skill of that
	 * name, among the skills of the character's damage type, started free at the recorded aim and share.
	 *
	 * CALLED ON THE TICK AFTER THE USE, for the reason the class comment gives. @return whether a skill started.
	 * False, with nothing left behind, when nothing was recorded, when the table has no such skill, when the rule
	 * above leaves it out, or when the skill refuses itself.
	 */
	static bool MakePendingRepeat(AActor* Character);
};
