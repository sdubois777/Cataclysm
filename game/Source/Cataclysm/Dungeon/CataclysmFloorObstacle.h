// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmFloorObstacle.generated.h"

class ACataclysmDungeonFloor;
class UBoxComponent;
class UNavModifierComponent;
class UStaticMeshComponent;

/** What an obstacle placed during play is. */
UENUM(BlueprintType)
enum class ECataclysmObstacleKind : uint8
{
	/** Solid to the wall height: stops the player, creatures and shots. Heaven's Quake. */
	Pillar,

	/** A hole: stops the player and creatures, and a shot flies over it. Cryptquake. */
	Pit,
};

/**
 * An obstacle a rule puts on a dungeon floor DURING PLAY, over one cell or a block of them. Issues #1820 and #41.
 *
 * THE FLOOR IS BUILT ONCE AND DOES NOT CHANGE, so a cell a rule closes mid-floor is closed by this actor standing on
 * it, not by rebuilding the floor's geometry. Three things close it, and each answers a different reader:
 *
 * - ITS COLLISION stops the player's keyboard movement and every creature's capsule. A pillar's mesh blocks with the
 *   "BlockAll" profile, so a shot stops on it too. A pit's blocker is a box of the full wall height with the engine's
 *   "InvisibleWall" profile -- WorldStatic, Visibility ignored, everything else blocked -- so a shot, which finds walls
 *   on the Visibility channel (`ACataclysmProjectile::TraceStep`), flies over it. Full height, because the Abyssal
 *   Warden's charge looks for WorldStatic objects with a sphere at body height and passes under anything shorter.
 * - A NAVIGATION MODIFIER OF NavArea_Null over its cells takes them off the navigation mesh, so click-to-move and every
 *   creature's path go round. Measured 2026-10-02 against two other ways (docs/DECISIONS.md): the cheapest main-thread
 *   update, and the only one of the three that left nothing walkable on the obstacle. Its box is `FailsafeExtent`,
 *   because `UNavModifierComponent::CalculateBounds` sizes itself only from components that affect navigation, and
 *   nothing this actor draws does.
 * - THE FLOOR PLAN: `ACataclysmDungeonFloor::BlockCell` turns its cells Solid, so every rule that chooses a cell from
 *   the plan passes it by. The game mode does that, not this actor, because the plan is the floor's.
 *
 * A WARNING FIRST. `Place` puts down only the marker -- a thin plate on the floor, no collision, no navigation change --
 * and `Raise` turns it into the obstacle when the warning is over.
 */
UCLASS()
class CATACLYSM_API ACataclysmFloorObstacle : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmFloorObstacle();

	/**
	 * A warning marker over these cells, coloured by the row's type, or null. The cells must be a square block: one
	 * cell, or two by two. The obstacle stands in their middle.
	 */
	static ACataclysmFloorObstacle* Place(UWorld* World, const ACataclysmDungeonFloor& Floor,
										  const TArray<FIntPoint>& InCells, ECataclysmObstacleKind InKind, FName InRowKey,
										  FName InCataclysmType);

	/** The warning is over: the obstacle stands, with its collision and its navigation modifier. */
	void Raise();

	/** Still only the warning marker. */
	bool IsWarning() const { return !bRaised; }

	/** What it is. */
	ECataclysmObstacleKind ObstacleKind() const { return Kind; }

	/** The cells it covers. */
	const TArray<FIntPoint>& CoveredCells() const { return Cells; }

	/** The row that placed it. */
	FName RowKey() const { return Row; }

	/** The row's type, which colours it. */
	FName CataclysmType() const { return ObstacleType; }

	/** Half its width, in centimetres: half a cell for one cell, a whole cell for two by two. */
	float HalfWidthCm() const { return HalfWidth; }

	/** Its navigation modifier, or null while it is a warning. */
	UNavModifierComponent* NavigationBlock() const { return NavBlock; }

	/** Its collision box, or null for a pillar, whose mesh collides. */
	UBoxComponent* PitBlocker() const { return Blocker; }

	/** What it draws. */
	UStaticMeshComponent* Shape() const { return Drawn; }

	/** How thick the warning marker and a pit's floor plate are. */
	static constexpr float PlateThicknessCm = 6.0f;

	/** The collision profile a pillar uses. */
	static const FName PillarProfile;

	/** The collision profile a pit's blocker uses. */
	static const FName PitProfile;

private:
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Dungeon")
	TObjectPtr<UStaticMeshComponent> Drawn;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Dungeon")
	TObjectPtr<UBoxComponent> Blocker;

	UPROPERTY()
	TObjectPtr<UNavModifierComponent> NavBlock;

	TArray<FIntPoint> Cells;
	ECataclysmObstacleKind Kind = ECataclysmObstacleKind::Pillar;
	FName Row;
	FName ObstacleType;
	float HalfWidth = 0.0f;
	/** The floor's height under it, which the drawn shape and the pit's box stand on. */
	float FloorZ = 0.0f;
	bool bRaised = false;

	/** Sets what is drawn: the thin plate, or a pillar's full height. */
	void DrawAs(bool bFullHeight, float Brightness);
};
