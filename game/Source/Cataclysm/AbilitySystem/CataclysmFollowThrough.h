// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmFollowThrough.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;
class UCataclysmSkillTemplate;
struct FCataclysmDeathNotice;

/**
 * Follow Through, the Ravager's keystone `Ravager_keystone_b_kB`: "Killing an
 * enemy with a melee attack immediately repeats that attack at no cost, no more
 * than once every 3 seconds." Issue #1515.
 *
 * TWO STEPS, BECAUSE THE KILL IS ANNOUNCED WHILE THE KILLING USE IS STILL
 * RUNNING. `UCataclysmSkillEffects::MarkDead` announces a death inside the blow
 * that caused it, and an ability cannot be activated again while it is active.
 * So a melee kill records the repeat (`NoteMeleeKill`), and the character's tick
 * makes it once that use has ended (`MakePendingRepeat`).
 *
 * RULED 2026-09-24, UNDER THE OWNER'S DELEGATION:
 *
 *   - THE SAME SKILL, AIMED AT THE NEAREST LIVING ENEMY WITHIN ITS OWN REACH,
 *     never at the cursor. No enemy in reach means no repeat, and the clock is
 *     not spent. Nearest rather than Path of Exile's Multistrike's random enemy
 *     is a judgement.
 *   - IT IS NOT A USE: no mana, no health, no cooldown, no next-use charge and
 *     no skill_use. See `UCataclysmGameplayAbility::bFreeRepeat`. Its hits are
 *     ordinary melee hits, so Nothing Wasted's store is spent by them and
 *     Rendering Blows counts them.
 *   - A BASIC ATTACK REPEATS AS ONE EXTRA SWING, ignoring the swing interval for
 *     that swing only. The interval is kept by the player controller, which
 *     this never touches, so the next ordinary swing's timing is unchanged.
 *   - A MOVEMENT-SLOT SKILL NEVER REPEATS, and its kill spends nothing: a
 *     repeat would move the character somewhere it did not choose. A judgement.
 *   - The kill must be this character's own and must not be a tick. A minion's
 *     kill is the minion's own, the owner's decision of 2026-09-17.
 */
UCLASS()
class CATACLYSM_API UCataclysmFollowThrough : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The seconds between repeats; above zero means the keystone is held. Its
	 *  row will carry the 3. */
	static const TCHAR* EverySecondsStat;

	/** Whether a death was a melee kill by `Character` itself, not a tick. */
	static bool IsOwnMeleeKill(const FCataclysmDeathNotice& Notice,
							   const AActor* Character);

	/**
	 * Record the repeat a melee kill earns, if the keystone is held, its clock
	 * allows one and the killing skill is not in the Movement slot.
	 *
	 * @return whether a repeat is now waiting to be made
	 */
	static bool NoteMeleeKill(AActor* Character, FName KillingSkillName);

	/**
	 * Make the waiting repeat, once the killing use has ended. Called every tick
	 * by the character; does nothing when nothing waits.
	 *
	 * @return whether a repeat was started now
	 */
	static bool MakePendingRepeat(AActor* Character);

	/** The skill of this name the character has been granted, or null. */
	static UCataclysmSkillTemplate* SkillNamed(UCataclysmAbilitySystemComponent* System,
											   FName Name,
											   FGameplayAbilitySpecHandle& OutHandle);
};
