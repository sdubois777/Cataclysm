// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "CataclysmDungeonFloor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * The geometry of one dungeon floor: the thing a player can actually stand on.
 *
 * WHAT IT IS FOR. `FCataclysmFloorGenerator` decides which cells can be walked
 * on and stops there. This turns that decision into blocks in the world. Until
 * it existed the generator produced data nothing read, and the combat systems
 * had no level to run in except `L_Sandbox`, which is a flat square.
 *
 * BUILT FROM C++ RATHER THAN AUTHORED INTO A MAP, which is the pattern
 * `ACataclysmGameMode::StartPlay` already follows for the sandbox's creatures and
 * says why: "Spawning from here rather than from the level means the sandbox's
 * contents are reviewable text rather than bytes inside L_Sandbox.umap." A
 * procedural floor has a harder version of the same requirement. It does not
 * exist until the game runs, so there is no map to author it into at all.
 *
 * TWO INSTANCED MESH COMPONENTS, NOT HUNDREDS OF ACTORS. A 40 by 40 floor is
 * 1,600 cells and a typical one is around 700 walkable cells and 400 walls. That
 * is a number of separate actors nothing would survive, so both are drawn as
 * instances of one block mesh.
 *
 * THE GROUND IS EXACTLY THE WALKABLE CELLS. One ground block per walkable cell
 * and none anywhere else, rather than one large plane under everything. It costs
 * more instances and it buys the property that matters: **the surface a player
 * can stand on is the same set of cells the generator's tests measured.** A
 * plane would put walkable surface outside the walls, where the navigation mesh
 * would then cover ground no floor plan knows about.
 *
 * WHAT IT DOES NOT DO. It does not place the player, spawn enemies, build a
 * navigation mesh, or handle the stairs down. Those are the next piece of work.
 */
UCLASS()
class CATACLYSM_API ACataclysmDungeonFloor : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmDungeonFloor();

	// ----------------------------------------------------------------------
	// Size
	// ----------------------------------------------------------------------

	/**
	 * How tall a wall block is, in centimetres.
	 *
	 * A JUDGEMENT, AND IT HAS NOT BEEN LOOKED AT. One cell, so a wall block is a
	 * cube rather than a slab. The camera looks down, so a taller wall hides more
	 * of the floor beside it and a shorter one stops reading as a wall. Four
	 * metres is about twice the height of the player's capsule, which is the
	 * least that cannot be seen over.
	 */
	static constexpr float WallHeightCm = 400.0f;

	/**
	 * How thick a wall is, in centimetres.
	 *
	 * HALF A CELL. A wall used to fill a whole cell, so every wall between two
	 * rooms was four metres of solid rock and a floor read as blocks with gaps
	 * rather than as rooms with walls. Set by the project owner on 2026-08-21:
	 * "maybe make the walls half as thick".
	 *
	 * THE HEIGHT IS UNCHANGED at `WallHeightCm`. Thickness is what was asked
	 * about; how tall a wall is under a camera that looks down is a separate
	 * question and nobody has looked at it yet.
	 */
	static constexpr float WallThicknessCm = 200.0f;

	/**
	 * How thick the ground block under a walkable cell is, in centimetres.
	 *
	 * Its top surface sits at the actor's own height, so a character placed at
	 * the actor's Z stands on the floor rather than inside it. `L_Sandbox`'s
	 * generator does the same thing with its flat floor and says why.
	 */
	static constexpr float GroundThicknessCm = 40.0f;

	/** The engine's unit cube, which both components draw. 100 cm on a side. */
	static const TCHAR* BlockMeshPath;

	// ----------------------------------------------------------------------
	// Building
	// ----------------------------------------------------------------------

	/**
	 * Builds the geometry for a floor plan, discarding anything built before.
	 *
	 * REBUILDING IS THE ORDINARY CASE, not an edge one: going down the stairs
	 * replaces the floor. So this clears first rather than adding, and
	 * `Cataclysm.DungeonFloor.BuildingTwiceDoesNotDoubleTheGeometry` holds it.
	 *
	 * @return false when the plan has nothing to stand on
	 */
	bool Build(const FCataclysmFloorPlan& InPlan);

	/** The plan this was built from. Empty until `Build` is called. */
	const FCataclysmFloorPlan& GetPlan() const { return Plan; }

	/** Whether `Build` has run and produced ground to stand on. */
	bool IsBuilt() const;

	// ----------------------------------------------------------------------
	// Where things are
	// ----------------------------------------------------------------------

	/**
	 * The middle of a cell in world space, at the height a character stands.
	 *
	 * THE FLOOR IS CENTRED ON THE ACTOR rather than growing out of it in one
	 * direction, so moving the actor moves the middle of the floor and not its
	 * corner.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	FVector WorldOfCell(FIntPoint Cell) const;

	/**
	 * Which cell a world position stands in.
	 *
	 * The inverse of `WorldOfCell`, and a test walks a position through both to
	 * prove they agree. Height is ignored. A position outside the floor answers
	 * with the cell it would be, which may be off the grid; ask
	 * `FCataclysmFloorPlan::Contains` if that matters.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	FIntPoint CellOfWorld(FVector World) const;

	/** Where the player arrives, in world space. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	FVector EntranceWorld() const { return WorldOfCell(Plan.Entrance); }

	/** Where the stairs down are, in world space. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	FVector ExitWorld() const { return WorldOfCell(Plan.Exit); }

	/**
	 * How far the floor reaches from its middle, in centimetres, on each axis.
	 *
	 * What a navigation bounds volume has to cover. Z reaches from under the
	 * ground blocks to over the wall blocks.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	FVector Extent() const;

	// ----------------------------------------------------------------------
	// What was built
	// ----------------------------------------------------------------------

	/** One block per walkable cell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> Ground;

	/** One block per solid cell that touches a walkable one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TObjectPtr<UInstancedStaticMeshComponent> Walls;

	/** How many ground blocks were placed. Equals the plan's walkable cells. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 GroundBlockCount() const;

	/** How many wall blocks were placed. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	int32 WallBlockCount() const;

	/**
	 * How many wall pieces a floor needs.
	 *
	 * A WALL IS A FACE, NOT A CELL. One piece stands on each side where walkable
	 * ground meets rock, just outside the ground rather than filling a cell of
	 * its own. That is what makes a wall half a cell thick instead of a whole
	 * one, and it means no piece is ever built against rock nobody can see.
	 *
	 * PLUS ONE PIECE PER CONVEX CORNER. Two walls meeting at a right angle each
	 * span their own cell's width and leave a square hole between them, because
	 * neither walkable cell owns that corner. It is filled by a piece the
	 * thickness of a wall on each side. Without it every corner of every room has
	 * a hole you can see through.
	 *
	 * A CORNER IS NEEDED WHENEVER BOTH SIDES ARE ROCK, whatever lies diagonally
	 * beyond it. The rule once also asked that the diagonal cell be rock, which
	 * left the hole open wherever two walkable cells touch only at a corner.
	 * Nothing can walk that diagonal -- movement is along sides -- but the hole is
	 * two metres across and the navigation mesh would route a character through
	 * it, taking a shortcut the floor plan says is solid.
	 */
	static int32 WallPiecesFor(const FCataclysmFloorPlan& Plan);

private:
	/** The plan the geometry was built from. */
	FCataclysmFloorPlan Plan;
};
