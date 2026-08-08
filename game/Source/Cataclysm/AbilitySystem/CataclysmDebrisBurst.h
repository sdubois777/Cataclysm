// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmDebrisBurst.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Broken pieces left where something hit, which then go away.
 *
 * WHAT IT IS FOR. Issue #422. `ACataclysmProjectile` stops, deals its damage,
 * waits a hundredth of a second and destroys itself, so a thrown rock is simply
 * gone from one frame to the next. Nothing in the project had an impact effect of
 * any kind, so this is not specific to the Brute.
 *
 * WHY IT IS ITS OWN ACTOR, which is the decision issue #422 says has to be made.
 * The alternatives were an extra argument on `ACataclysmProjectile::Fire`, which
 * already takes eleven and would only ever serve projectiles, or logic on the
 * firing character, which would give every caster its own copy. An actor
 * generalises to anything that wants to leave debris -- a stomp and a ground zone
 * want it as much as a thrown rock does -- and it is the shape this project
 * already uses for `ACataclysmTelegraphMarker` and `ACataclysmGroundZone`.
 *
 * WHO DECIDES TO SPAWN ONE IS THE CASTER, through the projectile's existing
 * `OnFinished` delegate. That keeps `ACataclysmProjectile` generic: it knows
 * nothing about rocks, and a fire bolt that wants sparks instead spawns something
 * else at the same moment.
 *
 * NO PHYSICS, AND THAT IS A DELIBERATE REFUSAL RATHER THAN AN OVERSIGHT. Five
 * simulated bodies per impact, on a dungeon floor that can hold a great many
 * enemies, is a cost nobody in this project has measured. The pieces are placed
 * and removed. If simulated debris is wanted later it goes behind this same
 * call, and the decision can be taken with a frame budget in front of somebody
 * rather than guessed at now.
 *
 * NO MOTION EITHER, for the same reason twice over: it would need this actor to
 * tick, and what it should look like is a judgement to make by watching. What is
 * here is the interface and the placement.
 *
 * THE MATERIAL HAS TO BE PASSED IN, and that is not tidiness. Measured on
 * 2026-08-08: the five `SM_Rampage_Rock_Frag*` meshes in the Paragon pack have
 * `/Engine/EngineMaterials/WorldGridMaterial` assigned -- the engine's grey
 * checkerboard placeholder. Spawning them as they come would put five large
 * checkered lumps on the floor, which is worse than the rock vanishing. The
 * caller supplies the material the pieces should wear; the Brute passes the one
 * from the rock they are pieces of.
 */
UCLASS()
class CATACLYSM_API ACataclysmDebrisBurst : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmDebrisBurst();

	/**
	 * How long the pieces stay, in seconds.
	 *
	 * A JUDGEMENT, AND LABELLED AS ONE. Long enough to be seen after a hit that
	 * the player was probably looking at, short enough that a Brute throwing
	 * every five seconds never has two sets on the floor at once. Nobody has
	 * watched this yet.
	 */
	static constexpr float DefaultSecondsOnTheGround = 2.0f;

	/**
	 * Put broken pieces at a point and take them away after Seconds.
	 *
	 * @param Instigator  whose attack broke; becomes the actor's owner
	 * @param At          where the impact happened
	 * @param Pieces      the meshes to scatter. Nulls in the array are skipped,
	 *   so a caller whose art is missing passes what it has and gets what it has
	 * @param Material    what the pieces wear. Null leaves each mesh's own, which
	 *   for the Paragon fragments is the engine's checkerboard -- see the class
	 *   comment
	 * @param SpreadCm    how far from the point the pieces are placed
	 * @param PieceRadiusCm  how large each piece should be, measured across the
	 *   ground. Scaled from each mesh's own bounds, so pieces of different sizes
	 *   come out consistent with one another
	 * @param Seconds     how long before they go
	 * @return the burst, or null if there was nothing to place
	 */
	static ACataclysmDebrisBurst* Scatter(
		AActor* Instigator, const FVector& At,
		const TArray<UStaticMesh*>& Pieces, UMaterialInterface* Material,
		float SpreadCm, float PieceRadiusCm,
		float Seconds = DefaultSecondsOnTheGround);

	/** How many pieces were actually placed. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Debris")
	int32 PiecesPlaced = 0;

	/** The placed pieces, so a test can read what they wear and how big they are. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Debris")
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;

protected:
	/**
	 * What the pieces hang from.
	 *
	 * NOT DECORATION. An actor whose components are all created at runtime has no
	 * root, and an actor with no root reports its location as the world origin
	 * however it was spawned. `ACataclysmProjectile` carries the same note for
	 * the same reason.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Debris")
	TObjectPtr<USceneComponent> Anchor;
};
