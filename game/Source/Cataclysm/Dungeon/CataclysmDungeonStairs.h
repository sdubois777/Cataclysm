// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmDungeonStairs.generated.h"

class UInstancedStaticMeshComponent;

/** Broadcast when the player has reached the stairs and is going down. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCataclysmStairsTaken);

/**
 * The way down to the next floor: a marker at the floor's exit that notices when
 * the player reaches it.
 *
 * WHAT IT IS FOR. `FCataclysmFloorGenerator` has chosen an exit cell on every
 * floor since the generator was written, `ACataclysmDungeonFloor::ExitWorld`
 * has been able to say where it is in the world, and nothing stood there or did
 * anything when the player walked to it. The floor number was a setting.
 *
 * IT NOTICES BY DISTANCE RATHER THAN BY OVERLAP, which is the pattern this
 * project already uses for picking a drop up off the ground:
 * `UCataclysmDropPickup::IsWithinPickupRange` compares two positions and there is
 * no trigger volume anywhere in the module. The reason to copy it is not
 * consistency. An overlap needs a collision component with overlap events turned
 * on, the right collision channels, and a pawn that generates overlaps against
 * it, and every one of those fails silently -- which is exactly how
 * `bCanEverAffectNavigation` left the dungeon floor with no navigation mesh and
 * nothing but a navigation test noticed. A distance is one number and a test can
 * ask for it without a physics scene.
 *
 * A MARKER, NOT STAIRS. It is a low stepped platform of untextured blocks, which
 * is the blockout the project owner approved on 2026-08-21. Real stairs go
 * *down*, and down needs a hole in the floor: the floor is one ground block per
 * walkable cell with nothing under it, so cutting a hole would show the void.
 * Whatever the art turns out to be, the floor change itself is a teleport, so the
 * shape is what a player looks for and not what they travel through.
 */
UCLASS()
class CATACLYSM_API ACataclysmDungeonStairs : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmDungeonStairs();

	// ----------------------------------------------------------------------
	// Size
	// ----------------------------------------------------------------------

	/**
	 * How close the player must come, in centimetres, measured flat.
	 *
	 * TWO METRES IS HALF A CELL, so the player has to be standing on the stairs'
	 * own cell rather than walking past the next one along. It is measured
	 * ignoring height because the marker is a platform the player may be standing
	 * on top of, and a distance that included height would need the player to be
	 * at the marker's own height as well as its position.
	 */
	static constexpr float ReachCm = 200.0f;

	/**
	 * How often it looks to see whether the player has arrived, in seconds.
	 *
	 * A QUARTER SECOND, THE SAME AS AN ENEMY'S BRAIN, and the arithmetic that
	 * makes it safe is worth writing down. The fastest designed player class
	 * moves at 4.6 metres a second, so it covers 1.15 metres between two looks,
	 * and the reach above is four metres across. A player running straight
	 * through the middle is inside it for at least three looks. Sampling a
	 * position is how a fast-moving thing gets missed, and this is why it is not
	 * missed.
	 */
	static constexpr float LookIntervalSeconds = 0.25f;

	/** How many steps the marker is built from. */
	static constexpr int32 TierCount = 3;

	/**
	 * How much taller each step is than the one outside it, in centimetres.
	 *
	 * TWENTY IS WELL UNDER WHAT A CHARACTER CAN STEP OVER, which Unreal's
	 * `UCharacterMovementComponent::MaxStepHeight` puts at 45 by default. So the
	 * player walks up the marker rather than being stopped by it, and a creature
	 * that wanders onto it can walk off again.
	 */
	static constexpr float TierRiseCm = 20.0f;

	/** How wide the outermost step is, in centimetres. One cell. */
	static constexpr float WidestTierCm = 400.0f;

	/** How much narrower each step is than the one outside it, in centimetres. */
	static constexpr float TierNarrowingCm = 100.0f;

	/** The engine's unit cube, which the steps are drawn with. 100 cm a side. */
	static const TCHAR* StepMeshPath;

	// ----------------------------------------------------------------------
	// Where it is
	// ----------------------------------------------------------------------

	/**
	 * Puts the marker somewhere, building its steps if they are not built yet.
	 *
	 * MOVED RATHER THAN REPLACED when the floor changes. One actor is spawned per
	 * dungeon and walks down it with the player, which is one fewer thing to
	 * destroy at the wrong moment.
	 *
	 * @param Where the top of the walking surface at the floor's exit
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void PlaceAt(const FVector& Where);

	/** Whether a position is close enough to take the stairs. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	bool IsWithinReach(const FVector& Where) const;

	// ----------------------------------------------------------------------
	// Taking them
	// ----------------------------------------------------------------------

	/**
	 * Takes the stairs if the position given is close enough.
	 *
	 * SPLIT FROM `LookForThePlayer` SO A TEST CAN REACH IT. An automation test
	 * has no player controller and cannot be given one -- `CataclysmTestWorld.h`
	 * records why -- so a check that only ever ran against
	 * `GetFirstPlayerController` would be a check nothing covers, and the whole
	 * floor change behind it would be untested with it.
	 *
	 * @return whether the stairs were taken
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool ArriveAt(const FVector& Where);

	/**
	 * Looks at where the player is standing and takes the stairs if they have
	 * arrived. Called on a timer while watching.
	 *
	 * @return whether the stairs were taken
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	bool LookForThePlayer();

	/** Broadcast when the player reaches the stairs. */
	UPROPERTY(BlueprintAssignable, Category = "Cataclysm|Dungeon")
	FCataclysmStairsTaken OnTaken;

	// ----------------------------------------------------------------------
	// Watching
	// ----------------------------------------------------------------------

	/** Starts looking for the player every `LookIntervalSeconds`. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void StartWatching();

	/**
	 * Stops looking.
	 *
	 * WHAT IT IS FOR IS NOT TIDINESS. The floor is rebuilt from inside the
	 * broadcast this actor makes, and the player is moved to the new entrance
	 * during it. Stopping first means a second look cannot arrive part way
	 * through and take the stairs again on a floor that is being replaced.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	void StopWatching();

	/** Whether it is looking for the player. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	bool IsWatching() const;

	/** The steps, drawn as instances of one block. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> Steps;

	/** How many step blocks were built. `TierCount` once placed. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 StepBlockCount() const;

private:
	/** Builds the steps, discarding any built before. */
	void BuildSteps();

	/**
	 * What the timer calls.
	 *
	 * A WRAPPER BECAUSE A TIMER WANTS A FUNCTION RETURNING NOTHING, and
	 * `LookForThePlayer` returns whether the stairs were taken, which is what
	 * makes it worth calling from a test.
	 */
	void LookForThePlayerOnTimer();

	/** The timer that looks for the player. */
	FTimerHandle LookHandle;
};
