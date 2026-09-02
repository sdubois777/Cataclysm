// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmTerrain.generated.h"

class UInstancedStaticMeshComponent;
class USceneComponent;

/**
 * What kind of terrain a skill left, as written in the sheet.
 *
 * THE CLOSED LIST `TERRAIN_KINDS` IN `tools/generate_datatables.py` HOLDS, and
 * this enum must agree with it. When the shape list and the generator disagreed
 * before, on issue #621, the consequence was silent: a row naming the missing
 * value read as None, was granted a placeholder that did nothing, and filled its
 * slot for as long as nobody ran the full automation suite.
 */
UENUM(BlueprintType)
enum class ECataclysmTerrainKind : uint8
{
	/** Nothing. A row that named a kind this build does not know. */
	None		UMETA(DisplayName = "None"),

	/**
	 * A hole in the floor. Knocks down whatever falls into it.
	 *
	 * Break the World: "What is left is a broken bowl for 12 seconds: anything
	 * inside must climb to leave, and anything that falls back in is knocked
	 * down again." Crater: "anything inside has to climb out: enemies in the pit
	 * cannot charge or leap."
	 */
	Pit			UMETA(DisplayName = "Pit"),

	/**
	 * A ridge of rock that blocks movement and projectiles.
	 *
	 * Upthrust: "drive a ridge of broken rock up out of the ground along a 10
	 * meter line. The ridge blocks movement and projectiles for 8 seconds."
	 *
	 * THE ONLY KIND THAT IS GEOMETRY RATHER THAN A ZONE. The other three change
	 * what happens to whoever stands in them and let everything walk through.
	 */
	Wall		UMETA(DisplayName = "Wall"),

	/**
	 * A crack that knocks down the next creature to cross it, once.
	 *
	 * Groundbreaker: "every blow you land cracks the ground beneath what it
	 * hits, leaving a fissure that knocks down the next enemy to cross it."
	 */
	Fissure		UMETA(DisplayName = "Fissure"),

	/**
	 * Standing spears that pin whatever walks into them.
	 *
	 * Thicket: "The spears stand for 12 seconds afterward, and anything that
	 * walks into them is pinned as well."
	 */
	Thicket		UMETA(DisplayName = "Thicket"),
};

/**
 * Persistent geometry a skill leaves behind, which changes where a fight can
 * happen.
 *
 * DISTINCT FROM `ACataclysmGroundZone`, AND THE PARAMETER HEADER SAYS SO: that is
 * a damage patch and this decides where "there" is. One burns you for standing
 * in it; this one holds you, floors you, or stops you walking through at all.
 * They are separate actors because they share almost nothing: a ground zone
 * refuses to do anything at all when its damage is zero, and terrain deals no
 * damage by design -- not one of the five rows that states `Terrain` states a
 * figure for it to deal.
 *
 * FOUR KINDS AND ONLY ONE OF THEM IS GEOMETRY. Pit, Fissure and Thicket are
 * swept zones that do something to whoever is standing in them, and reuse the
 * pin and the knockdown that `UCataclysmSkillEffects` already carries. A Wall is
 * a solid thing in the world with collision, built the way
 * `ACataclysmDungeonFloor` builds its walls.
 *
 * IT IS A CAPSULE, NOT A CIRCLE, for the reason the ground zone is one: a wall
 * has a length and a pit has a radius, and a circle is the case where the two
 * ends are the same point. `TerrainSize` is documented as "radius for a pit,
 * fissure or thicket, length for a wall", so the sheet already draws that line.
 *
 * WHAT IT DELIBERATELY DOES NOT DO. Break the World and Crater both say a
 * creature inside "must climb to leave", and nothing here makes leaving cost
 * anything. That needs a movement cost this project has no route for -- the only
 * movement speed on a character is `MaxWalkSpeed`, set once per creature class --
 * so a pit knocks down whatever falls in and does not slow anyone climbing out.
 * Issue #1152 carries it.
 */
UCLASS()
class CATACLYSM_API ACataclysmTerrain : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmTerrain();

	/**
	 * Read a kind out of the sheet's own word for it.
	 *
	 * @return the kind, or None for an empty or unknown cell
	 */
	static ECataclysmTerrainKind KindFromCell(const FString& Cell);

	/**
	 * Put terrain in the world along a segment.
	 *
	 * @param Owner      whose skill left it. It never affects that side
	 * @param Kind       which of the four
	 * @param Start      the near end; a round kind puts both ends here
	 * @param End        the far end
	 * @param SizeCm     radius for a pit, fissure or thicket; ignored for a wall,
	 *                   whose length is the distance from Start to End
	 * @param Duration   seconds before it goes away
	 * @param HoldSeconds how long a pin or a knockdown it applies lasts
	 * @return the terrain, or null if any of the numbers is not positive
	 */
	static ACataclysmTerrain* Spawn(AActor* Owner, ECataclysmTerrainKind Kind,
									const FVector& Start, const FVector& End,
									float SizeCm, float Duration,
									float HoldSeconds);

	/**
	 * Whether this actor is standing in any terrain of this kind right now.
	 *
	 * WHAT ASKS FOR IT. Crater: "enemies in the pit cannot charge or leap." So a
	 * charge and a leap both have to be able to ask, and neither of them holds a
	 * pointer to anything that would know.
	 *
	 * IT WALKS THE LEVEL'S TERRAIN RATHER THAN KEEPING A REGISTRY, because the
	 * count is tiny -- five rows in the whole sheet leave any, and a fissure is
	 * spent by the first creature to cross it -- and a registry would need
	 * sweeping for actors that were destroyed.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Terrain")
	static bool IsStandingIn(const AActor* Actor, ECataclysmTerrainKind Kind);

	/** Which of the four this is. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Terrain")
	ECataclysmTerrainKind Kind = ECataclysmTerrainKind::None;

	/** How wide it is, in centimetres. Its radius when round, half its width when long. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Terrain")
	float RadiusCm = 0.0f;

	/**
	 * The far end, in world space. Equal to the actor's own location when round.
	 *
	 * The near end is the actor's own location, so terrain is where it says it is
	 * in the outliner and its two ends cannot disagree. The same rule
	 * `ACataclysmGroundZone` follows.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Terrain")
	FVector FarEnd = FVector::ZeroVector;

	/** Seconds a pin or a knockdown from this terrain lasts on its target. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Terrain")
	float HoldSeconds = 0.0f;

	/** How many times it has swept. Read by tests; there is nothing else to see. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Terrain")
	int32 TicksElapsed = 0;

	/** How many creatures the last sweep held. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Terrain")
	int32 LastSweepCount = 0;

	/** Seconds between one sweep of who is standing in it and the next. */
	static constexpr float TickSeconds = 0.5f;

	/**
	 * How long a pin or a knockdown lasts when the row states none.
	 *
	 * ONLY A FISSURE NEEDS IT. Groundbreaker states `Terrain=Fissure` and no
	 * `ForcedMovementDuration`, because the duration it does state belongs to the
	 * self buff. Thicket, Break the World and Crater all state one.
	 *
	 * TWO SECONDS IS THE SHORTER OF THE TWO KNOCKDOWNS THE DESIGN NAMES.
	 * Section VI of `docs/Cataclysm_GDD_v2.md`: "Two Ultimates knock down --
	 * Warlord's Decree for 2 seconds and Cataclysm for 3." A fissure is a
	 * Support skill's leftover rather than an Ultimate, so it takes the shorter.
	 */
	static constexpr float DefaultHoldSeconds = 2.0f;

	/**
	 * Hold everything standing in it now. Called on a timer, and by tests.
	 *
	 * A WALL DOES NOTHING HERE and has no timer at all. It stops things by being
	 * solid, which needs no sweep.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Terrain")
	void Sweep();

protected:
	virtual void BeginPlay() override;

	/**
	 * An empty root so the actor has a position at all.
	 *
	 * NOT DECORATION, and `ACataclysmGroundZone` carries the same comment for the
	 * same reason: an actor whose components are all non-scene components has no
	 * root, and an actor with no root reports its location as the world origin
	 * however it was spawned.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Terrain")
	TObjectPtr<USceneComponent> Anchor;

	/**
	 * The solid part, and a Wall is the only kind that has one.
	 *
	 * BUILT THE WAY `ACataclysmDungeonFloor` BUILDS ITS WALLS, including both
	 * traps its comments record: the component must be `Movable` or its geometry
	 * stays at the world origin while the actor reports its real position, and
	 * `SetCanEverAffectNavigation(true)` is required because the default is false
	 * and a solid wall is otherwise invisible to pathfinding.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Terrain")
	TObjectPtr<UInstancedStaticMeshComponent> Blocks;

private:
	/** Raise the blocking geometry a Wall is made of. Does nothing for the rest. */
	void BuildWall();

	/**
	 * Who was standing in it at the end of the last sweep.
	 *
	 * WHAT IT IS FOR. A pit knocks down "anything that FALLS BACK IN", not
	 * anything standing in it, so a creature already inside must not be floored
	 * twice a second for the whole twelve. Comparing this sweep against the last
	 * is what makes "entered" a question with an answer.
	 *
	 * NOT A `UPROPERTY`, because Unreal Header Tool refuses a container of weak
	 * pointers as a Blueprint type and a weak pointer needs no garbage collection
	 * tracking of its own.
	 */
	TArray<TWeakObjectPtr<AActor>> InsideLastSweep;

	/** Its lifetime is SetLifeSpan; only the sweep needs a timer of its own. */
	FTimerHandle SweepTimer;
};
