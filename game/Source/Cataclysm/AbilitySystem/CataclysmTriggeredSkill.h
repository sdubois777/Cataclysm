// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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
	 *
	 * @param CastFrom  where the skill is cast FROM, or null for the character's own place. Ruled 2026-10-14, for
	 *                  a gadget's copy of its summoner's skill, which is the only caller that gives one: see
	 *                  `UCataclysmSkillTemplate::bFreeRepeatHasOrigin`.
	 * @param CastBy    the actor the skill is cast FROM, or null: the gadget a copy is. Ruled 2026-10-15, so a
	 *                  point-blank copy's share leaves it out; see `UCataclysmSkillTemplate::FreeRepeatCastBy`.
	 */
	static bool Trigger(AActor* Character, const FCataclysmWeaponSkill& Skill, const FVector& Aim,
						float DamageShare = 1.0f, bool bPaysCost = false, const FVector* CastFrom = nullptr,
						AActor* CastBy = nullptr);

	/**
	 * The row of the skill of this name the character HOLDS, from its weapon slots, copied into `Out`. Ruled
	 * 2026-10-14 for "the last skill you used": the record is written only for a skill the character holds as it
	 * is used. A gadget's copy does not ask it; it casts the row as it was recorded.
	 * The basic attack is among what is held. @return whether one was found; false for a character with no weapon
	 * slots and for a name of none.
	 */
	static bool HeldSkillNamed(const AActor* Character, FName Named, FCataclysmWeaponSkill& Out);

	/**
	 * The skills a row may trigger for a character that has just used `UsedSkill`: the skills it HOLDS, less the one
	 * used and less what `RepeatsFromARow` leaves out. Ruled 2026-10-06, for Spellblade's Will.
	 *
	 * WITH `bSpells`, those carrying `Type.Spell`. WITHOUT IT, those with a cooldown above nought, read from the row
	 * when it states one and from its slot when it does not; the basic attack and an aura have none.
	 *
	 * WHAT IS HELD AND NOT THE DAMAGE TYPE'S TABLE, because the words are "an ability" and "one of your spells" of
	 * the character's own. Wild Magic's pool is the table.
	 */
	static TArray<FCataclysmWeaponSkill> HeldSkillsToTrigger(const AActor* Character, FName UsedSkill, bool bSpells);

	/**
	 * Makes the trigger of a different held skill that a row recorded on the character's ability system, and clears
	 * it. Called on the tick after the use, as `MakePendingRepeat` is.
	 *
	 * ONE TRIGGER FOR ONE USE, ruled 2026-10-06. When a spell row passed and the character holds a spell to trigger,
	 * that is the one: it pays its cost, and with too little to pay nothing is triggered at all. Otherwise, when a
	 * cooldown row passed, one held skill with a cooldown is triggered free. The triggered skill's cooldown is not
	 * waited for and none is started, so a skill on cooldown may be triggered.
	 *
	 * WHICH ONE is drawn evenly from the pool; `Cataclysm.TriggerHeldSkillPick` pins the index for a test.
	 * @return whether a skill started.
	 */
	static bool MakePendingHeldTrigger(AActor* Character);

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
	 * THE BASIC ATTACK IS NOT A ROW OF THAT TABLE, so its name is looked up among the skills the character holds
	 * (`HeldBasicAttack`), which is where the weapon's own basic attack is.
	 *
	 * CALLED ON THE TICK AFTER THE USE, for the reason the class comment gives. @return whether a skill started.
	 * False, with nothing left behind, when nothing was recorded, when the table has no such skill, when the rule
	 * above leaves it out, or when the skill refuses itself.
	 */
	static bool MakePendingRepeat(AActor* Character);

	/**
	 * Makes the casts a row asked for by spending its kept stacks, and clears the request: for each row that asked,
	 * every kept stack it holds is spent and the skill used is started free, at the recorded aim, a number of times
	 * drawn from 1 to the stacks spent, each at the row's share of its damage. Ruled 2026-10-12 under the owner's
	 * delegation, for "Shard of Anarchy (10-Piece Bonus): Every 30 seconds, you gain a stack of 'Temporal Flux.' When
	 * you use a skill, you consume all stacks of 'Temporal Flux' and your skill is cast a random number of times. The
	 * bonus is permanent for the rest of the dungeon".
	 *
	 * EACH CAST IS A FREE START (`Trigger`), so it sends no skill-used notice. It therefore raises no `skill_use`:
	 * it spends no stack, is not cast again by this row, and is not repeated by a `repeat_skill` row.
	 *
	 * THE DRAW is `FMath::RandRange(1, stacks held)`; `Cataclysm.KeptStackCastsDraw` pins it for a test, held to
	 * between 1 and the stacks held.
	 *
	 * THE STACKS ARE SPENT HERE AND NOT AT THE USE, and only once the first cast has started. Two judgements of the
	 * writing session follow from that order, and the ruling states neither:
	 *   - A SKILL NO ROW MAY CAST AGAIN SPENDS NOTHING. `RepeatsFromARow` leaves out a movement skill, a self buff
	 *     and the others Wild Magic leaves out; a use of one keeps every stack for the next skill.
	 *   - A SKILL THAT REFUSES ITS FIRST CAST SPENDS NOTHING. A later cast that refuses is not made up for.
	 *
	 * ALL THE CASTS START ON THE SAME TICK, the tick after the use, at the same aim. Nothing spaces them out.
	 *
	 * CALLED ON THE TICK AFTER THE USE, for the reason the class comment gives, after `MakePendingRepeat`.
	 * @return how many casts started
	 */
	static int32 MakePendingKeptStackCasts(AActor* Character);

	/**
	 * The basic attack the character holds, as its weapon slots built it through
	 * `UCataclysmWeaponSkills::BasicAttackFor` and with its element tag; null when it holds none.
	 */
	static const FCataclysmWeaponSkill* HeldBasicAttack(const AActor* Character);

	/**
	 * The tags a row on `attack_use` is asked against for a use of the character's basic attack: the attack's own
	 * tags, and `Type.Melee` for a Strike or `Type.Ranged` for a Projectile.
	 *
	 * ADDED FOR THE QUESTION AND NOT TO THE ATTACK. Ruled 2026-10-06. A Strike basic attack has carried `Type.Melee`
	 * itself since issue #1564, so for it this adds nothing. A Projectile basic attack carries no `Type.Ranged`, and
	 * giving it one would move every row scoped to ranged skills onto it; this lets a row on `attack_use` ask for a
	 * ranged attack and changes the reach of no other row.
	 */
	static FGameplayTagContainer BasicAttackUseTags(const AActor* Character, const FGameplayTagContainer* OwnTags);
};
