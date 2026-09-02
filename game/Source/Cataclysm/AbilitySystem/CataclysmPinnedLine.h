// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CataclysmPinnedLine.generated.h"

/**
 * Remembers which pinned creatures were run through by one throw, so that
 * killing any of them frees the others.
 *
 * WHAT ASKS FOR IT. The Spear's Skewer: "Hurl the spear through a line up to 14
 * meters. Every enemy it passes is set alight and run through onto the one
 * behind them, and the whole line is held together for 4 seconds. Killing any
 * one of them frees the rest." Its row states `OnDeath=Release`, and until now
 * `Release` was one of the three values `OnDeath` may take that nothing handled.
 *
 * A COMPONENT RATHER THAN STATE KEPT SOMEWHERE ELSE, for the reason
 * `UCataclysmCurseSpread` gives: the instruction outlives the skill that gave
 * it. Skewer is a Projectile and ends in the same frame its projectile lands,
 * while the line it made holds for four seconds afterwards. A map keyed by actor
 * on a static would have to be swept for creatures that died, were destroyed or
 * left the level; a component is destroyed with its actor and needs no sweeping.
 *
 * EVERY MEMBER HOLDS THE WHOLE LINE, INCLUDING ITSELF. Storing "the others"
 * per member would mean building a different list for each one, and the saving
 * is a single pointer comparison at the moment somebody dies. Holding the same
 * list on all of them is also what makes the binding obviously symmetric: there
 * is no first member and no owner.
 *
 * IT DOES NOT PIN ANYTHING ITSELF. `UCataclysmSkillEffects::ApplyPin` is what
 * holds each creature and what carries the duration; this only records that
 * several pins belong together. So a line whose members were pinned for
 * different lengths still frees correctly, and a member whose pin has already
 * run out is simply not holding one to remove.
 *
 * ONE PER ACTOR, REPLACED RATHER THAN ADDED TO. A creature caught by a second
 * Skewer belongs to the second line and not to both: the older line has already
 * dealt its damage, and a creature in two lines would free two groups from one
 * death. Which matches the single-stack rule every player-applied effect here
 * follows.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmPinnedLine : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Record that these creatures are pinned together.
	 *
	 * A LINE OF ONE IS NOT A LINE AND IS REFUSED. "Killing any one of them frees
	 * the rest" says nothing when there is no rest, and binding a lone creature
	 * would leave a component behind that could never do anything.
	 *
	 * @param Line  everything one cast pinned. Anything invalid is dropped.
	 * @return how many creatures were bound, or zero if fewer than two remained
	 */
	static int32 BindTogether(const TArray<AActor*>& Line);

	/**
	 * Free every other creature bound to this one, and say how many were freed.
	 *
	 * CALLED FROM `UCataclysmSkillEffects::MarkDead`, which is the one place a
	 * death is recorded for the player and for every creature. Doing it there
	 * rather than in each character class is why a creature killed by a burn, by
	 * a patch of burning ground or by another creature frees its line the same
	 * way one killed by a blow does -- and a pinned creature standing in fire is
	 * exactly how a line is likely to end.
	 *
	 * THE DYING CREATURE IS NOT FREED. It is about to stop existing and its own
	 * pin goes with it, so removing an effect from it first would be work
	 * nothing could observe.
	 *
	 * THE LINE IS NOT UNBOUND AFTERWARDS, deliberately. Every survivor keeps its
	 * component, so a second death in the same line frees whatever is left --
	 * which does nothing, because they were already freed, and which is cheaper
	 * and simpler than walking the line to clear it. `ReleasePin` answers false
	 * for a creature that holds no pin.
	 */
	static int32 ReleaseFromDying(AActor* Dying);

	/**
	 * Everything pinned by the same cast, including the actor holding this.
	 *
	 * NOT `BlueprintReadOnly`, UNLIKE EVERY OTHER PROPERTY IN THIS MODULE, and
	 * the compiler decides that rather than a preference: Unreal Header Tool
	 * refuses `TArray<TWeakObjectPtr<AActor>>` as a Blueprint type outright --
	 * "Type is not supported by blueprint" -- while a single `TWeakObjectPtr` is
	 * fine, which is why `UCataclysmCurseSpread::Caster` beside it is exposed and
	 * this is not. Nothing in Blueprint needs to read a pinned line.
	 */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> Line;
};
