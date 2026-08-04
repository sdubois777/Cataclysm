// Copyright Stephen Dubois. All Rights Reserved.

#include "CataclysmLevelAuthoring.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "Model.h"

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
