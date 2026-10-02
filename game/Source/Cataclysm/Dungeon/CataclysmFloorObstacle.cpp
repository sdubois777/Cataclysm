// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorObstacle.h"

#include "AbilitySystem/CataclysmElementVisuals.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "UObject/ConstructorHelpers.h"

const FName ACataclysmFloorObstacle::PillarProfile(TEXT("BlockAll"));
const FName ACataclysmFloorObstacle::PitProfile(TEXT("InvisibleWall"));

namespace CataclysmFloorObstacle
{
	/** The engine cube's side, which every scale below divides by. */
	constexpr float UnitCubeCm = 100.0f;
}

ACataclysmFloorObstacle::ACataclysmFloorObstacle()
{
	PrimaryActorTick.bCanEverTick = false;

	// NOTHING THIS ACTOR DRAWS OR COLLIDES WITH AFFECTS NAVIGATION. The navigation modifier made in `Raise` is the one
	// thing that changes the navigation mesh, which is the method measured on 2026-10-02: a mesh that did would put
	// walkable polygons on a pillar's top, and the measurement found every blocked cell still on the mesh that way.
	Drawn = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
	RootComponent = Drawn;
	Drawn->SetMobility(EComponentMobility::Movable);
	Drawn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Drawn->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(ACataclysmDungeonFloor::BlockMeshPath);
	if (Cube.Succeeded())
	{
		Drawn->SetStaticMesh(Cube.Object);
	}
}

ACataclysmFloorObstacle* ACataclysmFloorObstacle::Place(UWorld* World, const ACataclysmDungeonFloor& Floor,
														const TArray<FIntPoint>& InCells, ECataclysmObstacleKind InKind,
														FName InRowKey, FName InCataclysmType)
{
	if (!World || InCells.IsEmpty())
	{
		return nullptr;
	}

	// IN THE MIDDLE OF ITS CELLS, at the floor's own height: the average of their centres is a cell's centre for one
	// and the shared corner for two by two.
	FVector Middle = FVector::ZeroVector;
	for (const FIntPoint& Cell : InCells)
	{
		Middle += Floor.WorldOfCell(Cell);
	}
	Middle /= static_cast<double>(InCells.Num());

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmFloorObstacle* Obstacle =
		World->SpawnActor<ACataclysmFloorObstacle>(ACataclysmFloorObstacle::StaticClass(), Middle,
												   FRotator::ZeroRotator, Spawn);
	if (!Obstacle)
	{
		return nullptr;
	}
	Obstacle->Cells = InCells;
	Obstacle->Kind = InKind;
	Obstacle->Row = InRowKey;
	Obstacle->ObstacleType = InCataclysmType;
	Obstacle->FloorZ = Middle.Z;
	Obstacle->HalfWidth = FMath::Sqrt(static_cast<float>(InCells.Num())) * FCataclysmFloorGenerator::CellSizeCm * 0.5f;
	Obstacle->DrawAs(/*bFullHeight=*/false, /*Brightness=*/1.0f);
	return Obstacle;
}

void ACataclysmFloorObstacle::Raise()
{
	if (bRaised)
	{
		return;
	}
	bRaised = true;
	const float Tall = ACataclysmDungeonFloor::WallHeightCm;

	if (Kind == ECataclysmObstacleKind::Pillar)
	{
		// A PILLAR IS SOLID: its own mesh, to the wall height, blocks movement and shots alike.
		DrawAs(/*bFullHeight=*/true, /*Brightness=*/1.0f);
		Drawn->SetCollisionProfileName(PillarProfile);
		Drawn->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		// A PIT IS DRAWN AS A DARK PLATE and blocked by a box no one sees: full height, so the Warden's charge meets it,
		// and "InvisibleWall", so a shot's Visibility trace passes over.
		DrawAs(/*bFullHeight=*/false, /*Brightness=*/0.2f);
		Blocker = NewObject<UBoxComponent>(this, TEXT("PitBlocker"));
		Blocker->SetCanEverAffectNavigation(false);
		Blocker->SetMobility(EComponentMobility::Movable);
		// NOT ATTACHED TO THE DRAWN PLATE, which is scaled flat: attached, the box would be scaled with it. The obstacle
		// never moves, so a box placed once in the world is where it stays.
		Blocker->SetWorldLocationAndRotation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorZ + Tall * 0.5f),
											 FRotator::ZeroRotator);
		Blocker->SetBoxExtent(FVector(HalfWidth, HalfWidth, Tall * 0.5f), /*bUpdateOverlaps=*/false);
		Blocker->SetCollisionProfileName(PitProfile);
		AddInstanceComponent(Blocker);
		Blocker->RegisterComponent();
	}

	// AND OFF THE NAVIGATION MESH. Half its width each way and half the wall height up and down from the floor, which
	// holds the walking surface at the floor's height.
	NavBlock = NewObject<UNavModifierComponent>(this, TEXT("ObstacleNavigationBlock"));
	NavBlock->FailsafeExtent = FVector(HalfWidth, HalfWidth, Tall * 0.5f);
	NavBlock->SetAreaClass(UNavArea_Null::StaticClass());
	AddInstanceComponent(NavBlock);
	NavBlock->RegisterComponent();
}

void ACataclysmFloorObstacle::DrawAs(bool bFullHeight, float Brightness)
{
	using namespace CataclysmFloorObstacle;
	const float Tall = bFullHeight ? ACataclysmDungeonFloor::WallHeightCm : PlateThicknessCm;
	const float Side = HalfWidth * 2.0f;
	Drawn->SetWorldScale3D(FVector(Side / UnitCubeCm, Side / UnitCubeCm, Tall / UnitCubeCm));

	// THE ENGINE CUBE IS CENTRED ON ITS ORIGIN, and the root is the cube, so the actor stands half its height up for
	// the shape to rest on the floor rather than half in it.
	FVector Where = GetActorLocation();
	Where.Z = FloorZ + Tall * 0.5f;
	SetActorLocation(Where, /*bSweep=*/false);

	// COLOURED BY THE ROW'S TYPE, through the same table every element's effects read, so a Celestial pillar and a
	// Death pit are the colours of their Cataclysms. The engine's basic shape material carries a vector parameter;
	// a type with no colour leaves the mesh as it is.
	FLinearColor Primary;
	FLinearColor Secondary;
	if (UCataclysmElementVisuals::ColoursFor(ObstacleType, Primary, Secondary))
	{
		if (UMaterialInstanceDynamic* Paint = Drawn->CreateDynamicMaterialInstance(0))
		{
			Paint->SetVectorParameterValue(TEXT("Color"), Primary * Brightness);
		}
	}
}
