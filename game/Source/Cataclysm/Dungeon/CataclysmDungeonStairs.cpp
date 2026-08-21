// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonStairs.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

const TCHAR* ACataclysmDungeonStairs::StepMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

namespace
{
	/**
	 * The engine's unit cube is 100 cm on a side, so a scale of one is a metre.
	 *
	 * NAMED FOR THIS FILE. Unreal merges a module's `.cpp` files into one
	 * translation unit, so two files defining the same helper in an anonymous
	 * namespace collide -- and only once both are committed.
	 * `tools/tests/test_no_two_files_share_an_anonymous_helper.py` holds it.
	 */
	constexpr float DungeonStairsUnitCubeSizeCm = 100.0f;
}

ACataclysmDungeonStairs::ACataclysmDungeonStairs()
{
	// NOTHING TO TICK. It looks for the player on a timer instead, a quarter of a
	// second apart, which is four looks a second rather than sixty or more.
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Steps = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Steps"));
	Steps->SetupAttachment(Root);

	// MOVABLE, FOR THE REASON THE DUNGEON FLOOR NEXT DOOR RECORDS AT LENGTH. A
	// static component's transform does not follow its actor, so a marker built
	// once and then moved to the next floor's exit would leave its blocks behind
	// at the last one while `GetActorLocation` reported the new position.
	Steps->SetMobility(EComponentMobility::Movable);

	// SOLID, SO IT READS AS SOMETHING RATHER THAN AS A DECAL. The steps are 20 cm
	// each, which is well under the 45 cm a character can step over, so the
	// player walks onto it.
	Steps->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Steps->SetCollisionProfileName(TEXT("BlockAll"));

	// AND DELIBERATELY INVISIBLE TO NAVIGATION, which is the opposite of what the
	// dungeon floor does and is a decision rather than the default being left
	// alone.
	//
	// THE FLOOR'S NAVIGATION MESH IS MEASURED BY TESTS AND THIS MUST NOT MOVE IT.
	// `Cataclysm.DungeonFloor.AGeneratedFloorGetsANavigationMeshOverIt` and the
	// tests beside it check what can be walked on against the floor plan. A 60 cm
	// obstacle appearing on one cell would carve a hole in that mesh, on a
	// different cell of every floor, and the exit is the one cell the player has
	// to be able to reach. A 20 cm step is inside what a navigation agent steps
	// over anyway, so leaving it out of the mesh costs nothing.
	Steps->SetCanEverAffectNavigation(false);

	// `ConstructorHelpers::FObjectFinder` RATHER THAN `LoadObject`, the same as
	// the dungeon floor and for the reason recorded there: a raw load while the
	// class default object is being created can assert.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> StepMesh(StepMeshPath);
	if (StepMesh.Succeeded())
	{
		Steps->SetStaticMesh(StepMesh.Object);
	}
}

void ACataclysmDungeonStairs::BuildSteps()
{
	if (!Steps)
	{
		return;
	}

	// CLEARED FIRST, so that placing the marker twice does not draw two markers
	// on top of each other. `ACataclysmDungeonFloor::Build` says the same about
	// rebuilding being the ordinary case rather than an edge one.
	Steps->ClearInstances();

	for (int32 Tier = 0; Tier < TierCount; ++Tier)
	{
		// THE OUTERMOST STEP IS THE WIDEST AND THE LOWEST. Each one inside it is
		// a step narrower and a step taller, so from above the marker reads as
		// squares inside squares and from the side as steps.
		const float Width = WidestTierCm - Tier * TierNarrowingCm;
		const float Height = (Tier + 1) * TierRiseCm;

		// THE BLOCK'S MIDDLE SITS AT HALF ITS HEIGHT, so its underside rests on
		// the walking surface and its top is `Height` above it. The engine's cube
		// has its origin at its own middle.
		FTransform Where;
		Where.SetScale3D(FVector(Width / DungeonStairsUnitCubeSizeCm,
								 Width / DungeonStairsUnitCubeSizeCm,
								 Height / DungeonStairsUnitCubeSizeCm));
		Where.SetLocation(FVector(0.0f, 0.0f, Height * 0.5f));

		Steps->AddInstance(Where);
	}
}

void ACataclysmDungeonStairs::PlaceAt(const FVector& Where)
{
	SetActorLocation(Where);

	if (StepBlockCount() != TierCount)
	{
		BuildSteps();
	}
}

int32 ACataclysmDungeonStairs::StepBlockCount() const
{
	return Steps ? Steps->GetInstanceCount() : 0;
}

bool ACataclysmDungeonStairs::IsWithinReach(const FVector& Where) const
{
	// FLAT, IGNORING HEIGHT. The marker is a platform the player may be standing
	// on top of, so a distance that counted height would ask the player to be at
	// the marker's own height as well as its position.
	return FVector::Dist2D(Where, GetActorLocation()) <= ReachCm;
}

bool ACataclysmDungeonStairs::ArriveAt(const FVector& Where)
{
	if (!IsWithinReach(Where))
	{
		return false;
	}

	// STOPPED BEFORE THE BROADCAST AND NOT AFTER IT. What is bound to this
	// rebuilds the floor and moves the player, all inside this call, and a look
	// arriving part way through would be a look at a floor that is half replaced.
	StopWatching();

	OnTaken.Broadcast();
	return true;
}

bool ACataclysmDungeonStairs::LookForThePlayer()
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;

	return Pawn ? ArriveAt(Pawn->GetActorLocation()) : false;
}

void ACataclysmDungeonStairs::StartWatching()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		LookHandle, this, &ACataclysmDungeonStairs::LookForThePlayerOnTimer,
		LookIntervalSeconds, /*bLoop=*/true);
}

void ACataclysmDungeonStairs::StopWatching()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LookHandle);
	}
}

bool ACataclysmDungeonStairs::IsWatching() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(LookHandle);
}

void ACataclysmDungeonStairs::LookForThePlayerOnTimer()
{
	LookForThePlayer();
}
