// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmTaunt.generated.h"

class AActor;

/**
 * A taunt on a creature. Ruled 2026-10-17 by the project owner (#6) for Warlord's Will's six-piece bonus, "The
 * Warhound taunts enemies", and 2026-10-18 by the coordinating session (Q1, Q2); `docs/DECISIONS.md` has the entry and
 * the sources.
 *
 * WHAT A TAUNT IS. A crowd-control status with a duration, placed on a creature by the character that taunted it. For
 * its seconds the creature targets only its taunter: `ACataclysmEnemyController::ChooseTarget` asks `TaunterOf` before
 * anything else and returns the taunter, wherever it stands (Q1). The creature still does everything else as it would:
 * a stun, a knockdown or a fear still stops or moves it, because `Think` asks those before it chooses a target.
 *
 * IT ENDS AT ONCE IF ITS TAUNTER DIES OR GOES (Q1): `TaunterOf` answers null, and forgets the taunt, as soon as the
 * taunter is dead or no longer in the world.
 *
 * A SECOND TAUNT (Q2). The same taunter restarts the seconds; another taunter is refused until the running one ends.
 *
 * CROWD CONTROL, AS A SILENCE IS (ruled 2026-10-18, B): the health ceiling the applier carries refuses it, and
 * crowd-control resistance shortens it, each as for a stun; `UCataclysmSkillEffects::IsCrowdControlled` answers yes
 * while it lasts; and a taunt that lands, placed or restarted, raises the crowd-control event on whoever applied it.
 * BOSSES ARE IMMUNE (the Boss and Cataclysm Boss rungs), as for a stun. A skill the target is running that states
 * `Immune=Taunt` or `Immune=CrowdControl` refuses it too. As for a silence, it neither asks nor opens the 5 second
 * window stun, knockdown and fear share, and it needs no share of maximum health.
 *
 * HELD HERE AND NOT AS A GAMEPLAY TAG. A tag needs a row on the design workbook's Tags sheet, which this layer may not
 * add, so the taunt is a register keyed weakly by the creature taunted, as `UCataclysmGroundPound` keeps its clocks.
 *
 * THE PLAYER IS NOT DRIVEN BY THIS. Nothing a player does reads `TaunterOf`.
 */
UCLASS()
class CATACLYSM_API UCataclysmTaunt : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Taunt `Target` for `Seconds`, as `Taunter`.
	 *
	 * REFUSED, in this order, for: no creature, no taunter or no time; a dead taunter or a dead target; a target not
	 * hostile to the taunter; a boss; a target already taunted by another taunter that is still standing (Q2); a
	 * target whose health is above the ceiling of the taunter's commander, or of the taunter itself when it has none
	 * (`UCataclysmSkillEffects::CrowdControlRefusedByHealthCeiling`); a target running a skill immune to it; a
	 * duration crowd-control resistance takes to nothing.
	 *
	 * THE SAME TAUNTER RESTARTS IT at the seconds left after resistance (Q2).
	 *
	 * A TAUNT THAT LANDS, PLACED OR RESTARTED, IS CROWD CONTROL APPLIED (B): `NoteCrowdControlApplied` on the ability
	 * system of whoever the act belongs to, `UCataclysmCombatEvents::AttackerOf(Taunter)`, as a silence's is. A refusal
	 * notes nothing.
	 *
	 * @return whether the target is now taunted by `Taunter`
	 */
	static bool ApplyTaunt(AActor* Taunter, AActor* Target, float Seconds);

	/** The character taunting `Target` now, or null. Null once the taunt's seconds have passed or its taunter died or went. */
	static AActor* TaunterOf(const AActor* Target);

	/** The seconds left on the taunt on `Target`, or nought when none runs. */
	static float SecondsLeftOn(const AActor* Target);

	/**
	 * Whether a taunt runs on `Actor` now, by the same test `TaunterOf` makes. Read by
	 * `UCataclysmSkillEffects::IsCrowdControlled`, ruled 2026-10-18 (B): a taunted creature is under crowd control
	 * while the taunt lasts, as a silenced one is.
	 */
	static bool IsTaunted(const AActor* Actor);
};
