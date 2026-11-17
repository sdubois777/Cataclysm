// Copyright Stephen Dubois. All Rights Reserved.

#include "CataclysmLevelAuthoring.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "Model.h"
#include "NavigationSystem.h"

ANavMeshBoundsVolume* UCataclysmLevelAuthoring::AddNavMeshBounds(UWorld* World, FVector Origin, FVector Size)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANavMeshBoundsVolume* Volume =
		World->SpawnActor<ANavMeshBoundsVolume>(Origin, FRotator::ZeroRotator, Params);
	if (!Volume)
	{
		return nullptr;
	}

	// A volume spawned this way has no brush. Placing one by hand in the editor
	// goes through an actor factory that builds it; spawning does not.
	Volume->Brush = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
	Volume->Brush->Initialize(nullptr, true);
	Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, RF_Transactional);
	Volume->GetBrushComponent()->Brush = Volume->Brush;

	UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume);
	Builder->X = Size.X;
	Builder->Y = Size.Y;
	Builder->Z = Size.Z;
	Volume->BrushBuilder = Builder;

	Builder->Build(World, Volume);
	Volume->ReregisterAllComponents();

	return Volume;
}

FVector UCataclysmLevelAuthoring::GetVolumeExtent(const ANavMeshBoundsVolume* Volume)
{
	if (!Volume)
	{
		return FVector::ZeroVector;
	}

	return Volume->GetComponentsBoundingBox(/*bNonColliding=*/true).GetExtent();
}

bool UCataclysmLevelAuthoring::BuildNavigation(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return false;
	}

	// RELEASING THE LOCK IS THE WHOLE POINT OF THIS FUNCTION. Loading a map in a
	// commandlet loads it asynchronously, and the navigation system locks
	// building while that happens, with the flag it calls AsyncLoadLock. Nothing
	// releases that lock afterwards here, because normally the editor's own map
	// loading does it. Build() then returns immediately having done nothing, and
	// says so only at Warning level:
	//
	//   Navigation NOT building because navigation build is locked (flags: 0x20)
	//
	// InitialLock is released too, because a level that used streaming would hold
	// that one instead and the symptom would be identical.
	//
	// NoRebuild, because Build() below does the building. Asking for a rebuild
	// here as well would run it twice.
	NavSys->RemoveNavigationBuildLock(
		ENavigationBuildLock::AsyncLoadLock | ENavigationBuildLock::InitialLock,
		UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);

	// Checked rather than assumed. Build() ignores the editor-only flag, so this
	// asks the same question Build() asks before deciding to do nothing.
	if (NavSys->IsNavigationBuildingLocked(
			static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
	{
		return false;
	}

	// Synchronous. Build() calls RebuildAll and then EnsureBuildCompletion on
	// every navigation data, so there is nothing to wait for afterwards.
	//
	// FEditorBuildUtils::EditorBuild, which is what the editor's Build menu uses,
	// is not an option here: called from a commandlet it dies with an access
	// violation inside UnrealEd, because it expects editor windows that a
	// commandlet does not have.
	NavSys->Build();
	return true;
}

bool UCataclysmLevelAuthoring::IsPointOnNavMesh(UWorld* World, FVector Point, float SearchSize)
{
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return false;
	}

	FNavLocation Projected;
	return NavSys->ProjectPointToNavigation(Point, Projected, FVector(SearchSize));
}
