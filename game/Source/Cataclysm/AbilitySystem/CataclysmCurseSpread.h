// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CataclysmCurseSpread.generated.h"

/**
 * Marks a cursed creature as one whose curses pass on when it dies.
 *
 * WHAT ASKS FOR IT. The Wand's Anathema: "anything that dies while damned
 * passes the curse to the nearest living enemy." Its row states
 * `OnDeath=SpreadDebuff` and `OnDeathRange=8`, and nothing read either.
 *
 * A COMPONENT RATHER THAN STATE KEPT SOMEWHERE ELSE, for one reason that
 * settles it: the instruction outlives the skill that gave it. Anathema is a
 * Strike; it ends in the same frame it activates, and the curse it left runs for
 * ten seconds afterwards. So the instruction cannot live on the ability, and a
 * map keyed by actor kept on a static would have to be swept for creatures that
 * died, were destroyed, or left the level. A component is destroyed with its
 * actor and needs no sweeping.
 *
 * IT CARRIES NO CURSE OF ITS OWN. What spreads is whatever `Status.*` tags the
 * dying creature happens to be carrying at the moment it dies, read by
 * `UCataclysmSkillEffects::CopyDebuffsTo`. So a creature damned by Anathema and
 * shredded by something else passes both, which is what "the curse" means when
 * a creature can carry several.
 *
 * ONE PER ACTOR, REFRESHED RATHER THAN ADDED TO. A second Anathema on the same
 * creature overwrites the range and the caster rather than making two
 * components, the same way every player-applied effect in this design is single
 * stack.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmCurseSpread : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Attach one to this actor, or refresh the one it already has.
	 *
	 * @param Target   the cursed creature
	 * @param Caster   who cursed it, credited with whatever the spread applies
	 * @param RangeCm  how far the curse reaches for its next holder
	 * @return the component, or null if the actor cannot take one
	 */
	static UCataclysmCurseSpread* MarkOn(AActor* Target, AActor* Caster,
										 float RangeCm);

	/**
	 * Pass this actor's curses to the nearest living enemy, and say how many
	 * applications landed.
	 *
	 * CALLED FROM `UCataclysmSkillEffects::MarkDead`, which is the one place a
	 * death is recorded for both the player and every creature. Doing it there
	 * rather than in each character class is why an enemy killed by a burn, by a
	 * patch of ground or by another enemy spreads the same way one killed by a
	 * blow does.
	 *
	 * THE NEAREST ONE AND NOT EVERY ONE IN RANGE. The row says "the nearest
	 * living enemy", singular. A curse that jumped to everything nearby would
	 * clear a room from one cast.
	 *
	 * IT RUNS BEFORE THE CORPSE IS COUNTED AS DEAD, so `FindEnemiesInSphere`
	 * does not have to be asked to skip it: the dying creature is excluded by
	 * name instead.
	 */
	static int32 SpreadFromDying(AActor* Dying);

	/** How far the curse reaches for its next holder, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Curse")
	float RangeCm = 0.0f;

	/** Who is credited with whatever the spread applies. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Curse")
	TWeakObjectPtr<AActor> Caster;
};
