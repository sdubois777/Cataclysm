// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonFloor.h"

#include "AI/NavigationSystemBase.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

const TCHAR* ACataclysmDungeonFloor::BlockMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

namespace
{
	/** How wide one cell is, in centimetres. The generator owns the number. */
	float DungeonFloorCellSize()
	{
		return FCataclysmFloorGenerator::CellSizeCm;
	}

	/**
	 * The engine's unit cube is 100 cm on a side, so a scale of one is a metre.
	 *
	 * NAMED FOR THIS FILE. Unreal merges a module's `.cpp` files into one
	 * translation unit, so two files defining the same helper in an anonymous
	 * namespace collide -- and only once both are committed. See
	 * `tools/tests/test_no_two_files_share_an_anonymous_helper.py`.
	 */
	constexpr float DungeonFloorUnitCubeSizeCm = 100.0f;

	/** The four sides of a cell. Named for this file; see the note above. */
	const FIntPoint DungeonFloorSteps[4] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};

	/** Its four corners. */
	const FIntPoint DungeonFloorCorners[4] = {
		FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)
	};
}

ACataclysmDungeonFloor::ACataclysmDungeonFloor()
{
	// NOTHING TO TICK. The floor is built once and does not change until the
	// player takes the stairs, at which point it is built again.
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Ground = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(Root);

	Walls = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Walls"));
	Walls->SetupAttachment(Root);

	// MOVABLE, AND STATIC WAS TRIED FIRST AND WAS WRONG.
	//
	// `L_Sandbox`'s flat floor is static, and copying that looked obviously
	// right: a dungeon floor does not move once it is built either. It broke two
	// tests. Placing the actor and then building left every block at the world
	// origin while `GetActorLocation` reported the new position, because a static
	// component's transform does not follow its actor, so a floor placed anywhere
	// other than the origin had its geometry somewhere else entirely.
	//
	// THE SANDBOX'S FLOOR IS STATIC FOR A REASON THAT DOES NOT APPLY HERE. It is
	// authored into a saved map, where static mobility lets the engine
	// precompute against it. A floor that does not exist until the game runs
	// cannot be precomputed against at all, so static buys nothing and costs the
	// ability to place the floor.
	Ground->SetMobility(EComponentMobility::Movable);
	Walls->SetMobility(EComponentMobility::Movable);

	// COLLISION IS THE WHOLE POINT OF THE WALLS. The navigation mesh is built
	// from collision geometry, so a component set to no collision would produce
	// a floor a character walks straight through and an empty navigation mesh.
	Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	Walls->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Walls->SetCollisionProfileName(TEXT("BlockAll"));

	// AND SAYING SO, BECAUSE COLLISION ALONE IS NOT ENOUGH AND THE DEFAULT IS NO.
	//
	// `UActorComponent::bCanEverAffectNavigation` is set to false in both
	// `UActorComponent`'s constructor and `UPrimitiveComponent`'s, and neither
	// `UStaticMeshComponent` nor `UInstancedStaticMeshComponent` turns it back
	// on. So a mesh component created in C++ is invisible to the navigation
	// system however solid it is, and `UPrimitiveComponent::IsNavigationRelevant`
	// returns false before it looks at collision at all.
	//
	// WITHOUT THIS A DUNGEON FLOOR COULD NEVER HAVE HAD A NAVIGATION MESH. It was
	// found by `Cataclysm.DungeonFloor.AGeneratedFloorGetsANavigationMeshOverIt`,
	// which reported the navigable world bounds as zero-sized while the floor's
	// own extent was 8,000 by 8,000 centimetres. Nothing else noticed, because
	// every other test asks where a block is rather than whether anything can
	// walk on it.
	Ground->SetCanEverAffectNavigation(true);
	Walls->SetCanEverAffectNavigation(true);

	// `ConstructorHelpers::FObjectFinder` RATHER THAN `LoadObject`, and it is not
	// a style preference. A constructor runs while the class default object is
	// being created, and a raw load during that pass can assert with "Illegal
	// call to StaticFindObject() while serializing object data". The finder is
	// the engine's own answer to loading an asset from a constructor and it
	// caches across every later construction of the same class.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BlockMesh(BlockMeshPath);
	if (BlockMesh.Succeeded())
	{
		Ground->SetStaticMesh(BlockMesh.Object);
		Walls->SetStaticMesh(BlockMesh.Object);
	}
}

int32 ACataclysmDungeonFloor::WallPiecesFor(const FCataclysmFloorPlan& InPlan)
{
	int32 Pieces = 0;

	for (int32 Index = 0; Index < InPlan.Cells.Num(); ++Index)
	{
		const FIntPoint Here = InPlan.CellAt(Index);
		if (!InPlan.IsFloor(Here))
		{
			continue;
		}

		for (const FIntPoint& Step : DungeonFloorSteps)
		{
			Pieces += InPlan.IsFloor(Here + Step) ? 0 : 1;
		}

		for (const FIntPoint& Corner : DungeonFloorCorners)
		{
			const bool bBothSidesAreRock =
				!InPlan.IsFloor(Here + FIntPoint(Corner.X, 0))
				&& !InPlan.IsFloor(Here + FIntPoint(0, Corner.Y));

			Pieces += bBothSidesAreRock ? 1 : 0;
		}
	}

	return Pieces;
}

bool ACataclysmDungeonFloor::Build(const FCataclysmFloorPlan& InPlan)
{
	Plan = InPlan;

	if (Ground)
	{
		Ground->ClearInstances();
	}
	if (Walls)
	{
		Walls->ClearInstances();
	}

	if (!Ground || !Walls || Plan.Cells.Num() <= 0)
	{
		return false;
	}

	const float Cell = DungeonFloorCellSize();
	const float BlockScale = Cell / DungeonFloorUnitCubeSizeCm;
	const float ThickScale = WallThicknessCm / DungeonFloorUnitCubeSizeCm;
	const float TallScale = WallHeightCm / DungeonFloorUnitCubeSizeCm;

	// The ground block's top surface has to land at the actor's own height, so a
	// character put at `WorldOfCell` stands on the floor rather than in it. The
	// engine's cube is centred on its origin, so the block's middle sits half its
	// thickness below.
	const FVector GroundScale(BlockScale, BlockScale,
							  GroundThicknessCm / DungeonFloorUnitCubeSizeCm);

	// How far from a cell's middle a wall on its edge stands: to the edge, then
	// out by half the wall's thickness, so the wall sits against the ground
	// rather than over it.
	const float OutTo = Cell * 0.5f + WallThicknessCm * 0.5f;
	const FVector Up(0.0f, 0.0f, WallHeightCm * 0.5f);

	for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
	{
		const FIntPoint Here = Plan.CellAt(Index);
		if (!Plan.IsFloor(Here))
		{
			continue;
		}

		const FVector Middle = WorldOfCell(Here) - GetActorLocation();

		Ground->AddInstance(FTransform(FRotator::ZeroRotator,
			Middle - FVector(0.0f, 0.0f, GroundThicknessCm * 0.5f), GroundScale));

		// A wall on each side where this cell's ground stops and rock begins. It
		// spans the full width of that side and is only as thick as a wall.
		for (const FIntPoint& Step : DungeonFloorSteps)
		{
			if (Plan.IsFloor(Here + Step))
			{
				continue;
			}

			const FVector Out(Step.X * OutTo, Step.Y * OutTo, 0.0f);
			const FVector Scale = (Step.X != 0)
				? FVector(ThickScale, BlockScale, TallScale)
				: FVector(BlockScale, ThickScale, TallScale);

			Walls->AddInstance(FTransform(FRotator::ZeroRotator,
										  Middle + Out + Up, Scale));
		}

		// And a piece in each corner where two of those walls meet at a right
		// angle. Neither of them covers it, because each spans only its own
		// cell's width, and the hole left is exactly a wall's thickness square.
		for (const FIntPoint& Corner : DungeonFloorCorners)
		{
			const bool bBothSidesAreRock =
				!Plan.IsFloor(Here + FIntPoint(Corner.X, 0))
				&& !Plan.IsFloor(Here + FIntPoint(0, Corner.Y));

			if (!bBothSidesAreRock)
			{
				continue;
			}

			const FVector Out(Corner.X * OutTo, Corner.Y * OutTo, 0.0f);
			Walls->AddInstance(FTransform(FRotator::ZeroRotator,
				Middle + Out + Up, FVector(ThickScale, ThickScale, TallScale)));
		}
	}

	// TELL THE NAVIGATION SYSTEM THE GEOMETRY CHANGED. It notices a component
	// being registered and a component moving; it does not notice instances
	// being added to one that is already registered and standing still. Taking
	// the stairs down rebuilds this actor in place, so without this the second
	// floor of a dungeon would be walked on the first floor's navigation mesh.
	FNavigationSystem::UpdateComponentData(*Ground);
	FNavigationSystem::UpdateComponentData(*Walls);

	return IsBuilt();
}

bool ACataclysmDungeonFloor::IsBuilt() const
{
	return Plan.IsBuilt() && GroundBlockCount() > 0;
}

int32 ACataclysmDungeonFloor::GroundBlockCount() const
{
	return Ground ? Ground->GetInstanceCount() : 0;
}

int32 ACataclysmDungeonFloor::WallBlockCount() const
{
	return Walls ? Walls->GetInstanceCount() : 0;
}

FVector ACataclysmDungeonFloor::WorldOfCell(FIntPoint Cell) const
{
	const float Size = DungeonFloorCellSize();
	const float MiddleX = (Plan.Width - 1) * 0.5f;
	const float MiddleY = (Plan.Height - 1) * 0.5f;

	return GetActorLocation()
		+ FVector((Cell.X - MiddleX) * Size, (Cell.Y - MiddleY) * Size, 0.0f);
}

FIntPoint ACataclysmDungeonFloor::CellOfWorld(FVector World) const
{
	const float Size = DungeonFloorCellSize();
	if (Size <= 0.0f)
	{
		return FIntPoint(-1, -1);
	}

	const FVector Offset = World - GetActorLocation();
	const float MiddleX = (Plan.Width - 1) * 0.5f;
	const float MiddleY = (Plan.Height - 1) * 0.5f;

	return FIntPoint(FMath::RoundToInt(Offset.X / Size + MiddleX),
					 FMath::RoundToInt(Offset.Y / Size + MiddleY));
}

FVector ACataclysmDungeonFloor::Extent() const
{
	const float Size = DungeonFloorCellSize();

	// X and Y: half the floor's width in centimetres. The outermost cell's block
	// reaches half a cell past that cell's middle, which is why this is the cell
	// count times the cell size rather than one less.
	//
	// Z IS THE WALL HEIGHT AND NOT HALF THE GEOMETRY'S SPAN, deliberately. The
	// geometry runs from the bottom of a ground block at -40 cm to the top of a
	// wall block at +400 cm, and that span is not centred on the actor. A true
	// half-extent of 220 cm about the actor's own height would cover -220 to
	// +220 and clip the top of every wall. Taking the wall height covers -400 to
	// +400, which contains all of it with room under the floor to spare.
	return FVector(Plan.Width * Size * 0.5f,
				   Plan.Height * Size * 0.5f,
				   WallHeightCm);
}
