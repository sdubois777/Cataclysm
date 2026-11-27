// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmHordeAscendant.generated.h"

class AActor;
struct FCataclysmWeaponSkill;

/**
 * The Horde Ascendant, the Necromancer's Final Rite, option 3, built AS WRITTEN by the coordinating session's ruling of
 * 2026-11-26: "Whenever you cast a skill, every minion within 10 metres casts it too, at 10% of its damage." The
 * finishing of the Death Cataclysm, slice C. `docs/DECISIONS.md` carries the judgements.
 *
 * MAGNUM OPUS'S SHAPE, MADE GENERAL. The Saboteur's vow has its eldest gadget cast each skill used
 * (`ACataclysmMinion::CastSkillCopy`, called by `ACataclysmPlayerCharacter::OnSkillWasUsed` on the next tick); this has
 * every follower within the reach cast it, ANY FOLLOWER -- an imp, a husk, a Barrow Guard, a thrall (the owner's ruling
 * of 2026-09-16 that a minion means imps and thralls alike) -- but not a machine, which is placed rather than raised.
 * Each copy is granted to the player for one use, free, cast FROM the follower (`UCataclysmTriggeredSkill::Trigger`'s
 * `CastFrom` and `CastBy`, as a gadget's copy is), at the follower's own target or, with none, at where the player aimed,
 * at the stated share of the skill's damage.
 *
 * WHICH SKILLS: those a row may cast again (`UCataclysmTriggeredSkill::RepeatsFromARow`), as Magnum Opus's are -- not the
 * basic attack, not a movement skill, not a summon, an aura or a self buff -- a judgement.
 */
UCLASS()
class CATACLYSM_API UCataclysmHordeAscendant : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The reach in metres within which a follower casts the skill too (10). Above nought IS the vow being held. */
	static const TCHAR* ReachMetresStat;

	/** The share of the skill's damage each follower's cast deals, in per cent (10). */
	static const TCHAR* DamagePercentStat;

	/**
	 * Every follower of `Player`'s within the vow's reach casts `Skill`, aimed at its own target or else at `Aim`.
	 * Answers how many casts started. Nought when the player holds no reach, or the skill is one no row may cast again.
	 * CALLED ON THE TICK AFTER THE USE, for the reason a Magnum Opus copy waits: a skill cannot be granted inside another
	 * skill's activation.
	 */
	static int32 EveryFollowerNearCasts(AActor* Player, const FCataclysmWeaponSkill& Skill, const FVector& Aim);

	/** Whether `Player` holds the vow: its reach above nought. */
	static bool Holds(const AActor* Player);
};
