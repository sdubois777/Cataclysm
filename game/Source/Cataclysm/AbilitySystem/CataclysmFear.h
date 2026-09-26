// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmFear.generated.h"

class AActor;

/**
 * Fear, and the fleeing it causes. Proposed and ruled on 2026-09-25 under the
 * project owner's delegation; `docs/DECISIONS.md` has the entry and the sources.
 *
 * WHAT FEAR IS. A crowd-control status with a duration, `State.Feared`. A feared
 * character moves away from the point it was frightened from and cannot attack
 * or use a skill. Where it cannot move further it stands still. Diablo IV's fear
 * does exactly this: "Fear only prevents use of attacks and active skills", and
 * "If feared enemies are immobilized, cornered or otherwise cannot flee any
 * farther, they just stand idle".
 *
 * THE ANTI-STUN-LOCK RULES, AS MADNESS TAKES THEM. Section "Stun and the
 * Anti-Stun-Lock Rule" of `docs/Cataclysm_GDD_v2.md`. Fear moves the target
 * rather than stopping it, as madness does, so it takes the 5 second immunity
 * window shared with stun and knockdown and boss immunity, and not the 10% damage
 * threshold. Crowd-control resistance shortens it, as it shortens a stun.
 *
 * FLEEING WITHOUT FEAR. A creature may also be told to flee by a rule rather than
 * by a status -- The Plaguebearer, Morale Break -- through
 * `ACataclysmEnemyCharacter::FleeFrom`. It moves the same way and carries no tag,
 * so none of the rules above apply to it.
 *
 * NOWHERE TO RUN WINS. A creature held by that keystone cannot move away from its
 * holder; a feared one it holds stands still, as a cornered one does.
 */
UCLASS()
class CATACLYSM_API UCataclysmFear : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** `State.Feared`: the character is fleeing and cannot attack or use a skill. */
	static FGameplayTag FearedTag();

	/**
	 * `State.FearImmune`: the character cannot be feared until this expires.
	 * Dirge Resonance grants it to every creature at a crescendo.
	 */
	static FGameplayTag FearImmuneTag();

	/**
	 * How far along the line away from the source each flee move aims, in
	 * metres. A play-test value ruled on 2026-09-25. The brain thinks four times
	 * a second, so a moving creature never reaches it before the next one.
	 */
	static constexpr float FleeStepMetres = 6.0f;

	/**
	 * Frighten `Target` away from `From` for `Seconds`.
	 *
	 * REFUSED, in this order, for: no time; a duration crowd-control resistance
	 * takes to nothing; a target inside the immunity window stun and knockdown
	 * share; one holding `State.FearImmune`; one running a skill immune to "Fear";
	 * a boss. A fear that lands opens that same immunity window.
	 *
	 * @return whether the target is now feared
	 */
	static bool ApplyFear(AActor* Instigator, AActor* Target, float Seconds,
						  const FVector& From);

	static bool IsFeared(const AActor* Actor);

	/**
	 * The point a character is moving away from right now: the source of its
	 * fear, or the point a rule told a creature to flee from.
	 *
	 * @return false when the character is not fleeing
	 */
	static bool FleeSourceOf(const AActor* Character, FVector& OutFrom);

	/**
	 * Where one flee move aims: `FleeStepMetres` further along the flat line from
	 * `From` through the character. A character standing on `From` flees the way
	 * it faces.
	 */
	static FVector FleeGoalFor(const AActor* Character, const FVector& From);

	/**
	 * The flat direction a fleeing character moves in, or zero when it is not
	 * fleeing. The player controller walks a feared player along it.
	 */
	static FVector FleeDirectionFor(const AActor* Character);
};
