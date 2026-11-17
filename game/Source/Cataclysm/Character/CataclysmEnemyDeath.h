// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEnemyDeath.generated.h"

/**
 * What dying looks like, decided, and none of the playing.
 *
 * WHY IT EXISTS. Until issue #522 an enemy whose health reached zero was
 * destroyed on the next tick. It played nothing and it was gone within a frame,
 * which is the smallest thing that makes a fight end and is not what dying
 * should look like. This project settles combat by playing it, and death is the
 * most visible moment in a fight.
 *
 * THE TWO DECISIONS ARE HERE AND THE ANIMATION IS NOT, for the same reason
 * `UCataclysmCombatOverlay` splits its judgements from its drawing: an
 * automation test cannot watch a clip play. The test command in
 * `tools/unreal_build.py` passes -nullrhi. What a test CAN check is which clip
 * was chosen out of how many, and how long the corpse is kept for a clip of a
 * given length, so both of those are static functions over plain numbers here.
 *
 * WHAT IS DELIBERATELY NOT DECIDED HERE. Which clips a creature owns. That is
 * per-creature art and it lives on the creature: see
 * `ACataclysmEnemyCharacter::DeathAnimations` and the paths each dressed
 * subclass resolves into it.
 */
UCLASS()
class CATACLYSM_API UCataclysmEnemyDeath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Which death clip to play, out of however many the creature has.
	 *
	 * DRAWN AT RANDOM PER DEATH, which is what the genre does when a creature
	 * has more than one death clip and is one line. The Abyssal Warden has two,
	 * `Death_A` and `Death_B`; the Brute's pack ships one. Choosing by HOW the
	 * creature died -- burnt, crushed, shot -- is a larger design that needs the
	 * damage type carried into the death path, and nothing carries it today.
	 *
	 * @param ClipCount  how many clips the creature has
	 * @param Stream     the draw. A creature's own, not the one its drops came
	 *                   from, so which clip plays cannot change what it dropped
	 * @return the index to play, or INDEX_NONE when the creature has no clips.
	 *         **INDEX_NONE is the ordinary case and not a fault**: five of the
	 *         seven vertical slice creatures have no art at all yet, and a
	 *         placeholder cylinder has nothing to play.
	 */
	static int32 ClipToPlay(int32 ClipCount, FRandomStream& Stream);

	/**
	 * How long the body is kept after the killing blow, for a clip of this
	 * length.
	 *
	 * THE CLIP'S OWN LENGTH, AND THEN THE BODY GOES. Leaving corpses lying
	 * around afterwards is a look rather than a requirement, and it costs an
	 * actor each on a floor the design expects to hold a great many creatures.
	 * If corpses are ever wanted, this is the one number that grants them and
	 * the ceiling below is what would have to move with it.
	 *
	 * A CLIP THAT IS NOT REALLY A CLIP KEEPS NOTHING. A length of zero or less
	 * means the asset is broken or absent, and the honest answer to that is the
	 * behaviour this feature replaced: gone on the next tick. It is not treated
	 * as an error, because a creature with no art reaches it every time it dies.
	 *
	 * @return seconds to wait, or 0 to mean "on the next tick, as before"
	 */
	static float CorpseSecondsFor(float ClipLength);

	/**
	 * The longest a body is ever kept, in seconds.
	 *
	 * A CEILING RATHER THAN A HOPE, and the same shape as the ceiling on waiting
	 * damage numbers. The two death clips that exist are 1.6667 and 0.7667
	 * seconds, so nothing today comes near it. It is here because the clip a
	 * creature dies with is an art choice made in a folder rather than in code,
	 * and a 30 second clip picked by mistake would leave a body standing in the
	 * room with no test able to see it.
	 */
	static constexpr float LongestCorpseSeconds = 4.0f;
};
