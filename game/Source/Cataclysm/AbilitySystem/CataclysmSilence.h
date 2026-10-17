// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSilence.generated.h"

class AActor;

/**
 * Silence on a creature. Ruled 2026-10-17 for the Null Emperor set's 2-piece bonus, "Your attacks have a 25% chance to
 * silence an enemy for 1 second"; `docs/DECISIONS.md` has the entry and the sources.
 *
 * WHAT SILENCE IS. A crowd-control status with a duration, `State.Silenced`. A silenced creature cannot START any of its
 * abilities but its basic attack: `ACataclysmEnemyController::ChooseAbility` skips every entry of `EnemyAbilities`
 * whose `bIsBasicAttack` is false. The owner, 2026-10-17 (O1): a silenced creature loses every ability except its basic
 * attack, and its timed behaviours keep running. So its own swing (`AttackTarget`), its movement and every timed
 * behaviour go on, and a wind-up, channel or charge already running when silence lands finishes (O3, the player
 * lock's principle: "refuses the next activation and does not interrupt").
 *
 * NOT A HARD STOP. It is not in `UCataclysmSkillEffects::CannotAct`, which is stun and knockdown; it is in the narrow
 * group with the pin.
 *
 * CROWD CONTROL, AND NOT THE STUN WINDOW (O4). The health ceiling refuses it and crowd-control resistance shortens it,
 * as for a fear; `UCataclysmSkillEffects::IsCrowdControlled` answers yes while it lasts; and a silence that lands
 * raises the crowd-control event on whoever applied it. It neither asks nor opens the 5 second window stun, knockdown
 * and fear share.
 *
 * A BOSS IS SILENCED (O2), for the full duration, as a pin affects one.
 *
 * REAPPLYING REFRESHES IT (O5), through `UCataclysmSkillEffects::ApplyTagForDuration`: one effect, its start moved to
 * now, never stacked.
 *
 * THE PLAYER IS NOT SILENCED BY THIS. A player's skills are locked by the `skill_locked` stat, read in
 * `UCataclysmSkillTemplate::CanActivateAbility`; nothing a player does reads this tag.
 */
UCLASS()
class CATACLYSM_API UCataclysmSilence : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `State.Silenced`: the character cannot use its abilities until this expires. Requested by name, so a workbook
	 * that has lost the tag gives an invalid tag here and every `IsSilenced` answers no.
	 */
	static FGameplayTag SilencedTag();

	/**
	 * How long the Null Emperor 2-piece's silence lasts, in seconds: "silence an enemy for 1 second". The row's
	 * `apply_status` "Silence" applies this; the row states only its chance.
	 */
	static constexpr float SilenceSeconds = 1.0f;

	/**
	 * Silence `Target` for `Seconds`.
	 *
	 * REFUSED, in this order, for: no character or no time; an enemy whose health is above the instigator's ceiling,
	 * `UCataclysmSkillEffects::CrowdControlRefusedByHealthCeiling`; a duration crowd-control resistance takes to
	 * nothing. NOT refused for a boss, nor inside the stun window, and a silence that lands opens no window.
	 *
	 * @return whether the target is now silenced
	 */
	static bool ApplySilence(AActor* Instigator, AActor* Target, float Seconds);

	static bool IsSilenced(const AActor* Actor);
};
