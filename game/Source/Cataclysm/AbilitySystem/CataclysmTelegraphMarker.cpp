// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * Which engine shape a marker is built from.
	 *
	 * TWO MESHES ON ONE ACTOR RATHER THAN TWO ACTOR CLASSES. Everything else
	 * about the two shapes is identical -- no collision, the same lifetime, the
	 * same owner, the same removal -- so splitting them would duplicate all of
	 * that to vary one asset path and one scale.
	 */
	const TCHAR* TelegraphCirclePath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* TelegraphLanePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** Spawns the actor and gives it its lifetime. Shared by both shapes. */
	ACataclysmTelegraphMarker* SpawnTelegraphMarker(AActor* Caster,
													const FVector& Location,
													const FRotator& Rotation,
													float Seconds)
	{
		UWorld* World = Caster ? Caster->GetWorld() : nullptr;
		if (!World || Seconds <= 0.0f)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = Caster;

		// ALWAYS SPAWN. A marker is drawn on ground the attack is going to land
		// on, which is very often ground something is already standing on --
		// that is the point of it. Letting the engine adjust or refuse the spawn
		// would move the warning off the area it warns about.
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ACataclysmTelegraphMarker* Marker =
			World->SpawnActor<ACataclysmTelegraphMarker>(Location, Rotation, SpawnParams);
		if (!Marker)
		{
			return nullptr;
		}

		// A LIFESPAN AS WELL AS AN EXPLICIT REMOVAL, deliberately belt and
		// braces. ACataclysmEnemyController dismisses the marker when the attack
		// lands and when a stun abandons the wind-up, and if some third way of
		// ending a wind-up is ever added and forgets to, the worst that happens
		// is a marker that outstays its attack by nothing rather than one that
		// stays on the floor for the rest of the level.
		Marker->SetLifeSpan(Seconds);
		return Marker;
	}
}

ACataclysmTelegraphMarker::ACataclysmTelegraphMarker()
{
	// Nothing to do per frame. It is drawn once, sits there, and is removed.
	PrimaryActorTick.bCanEverTick = false;

	Patch = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Patch"));
	RootComponent = Patch;
	Patch->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// BOTH FOUND HERE, EVEN THOUGH ONLY ONE IS USED PER MARKER. The static
	// ShowCircle and ShowLine below cannot look an asset up themselves: every
	// ConstructorHelpers finder calls CheckIfIsInConstructor and asserts outside
	// one. So the class default object carries both and each factory picks.
	//
	// Engine content, found by path, so this adds no asset to the project and
	// nothing to Git LFS -- the same arrangement the placeholder bodies use.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoundCircle(TelegraphCirclePath);
	if (FoundCircle.Succeeded())
	{
		CircleMesh = FoundCircle.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoundLane(TelegraphLanePath);
	if (FoundLane.Succeeded())
	{
		LaneMesh = FoundLane.Object;
	}
}

ACataclysmTelegraphMarker* ACataclysmTelegraphMarker::ShowCircle(
	AActor* Caster, const FVector& Centre, float RadiusCm, float Seconds)
{
	// THE ONE METRE FLOOR, REFUSED HERE RATHER THAN BY EVERY CALLER. See the
	// header: a marker smaller than the creature standing in it is not a
	// telegraph. Returning null is the answer, not a failure.
	if (RadiusCm < SmallestUsefulRadiusCm)
	{
		return nullptr;
	}

	ACataclysmTelegraphMarker* Marker =
		SpawnTelegraphMarker(Caster, Centre, FRotator::ZeroRotator, Seconds);
	if (!Marker)
	{
		return nullptr;
	}

	Marker->RadiusCm = RadiusCm;
	Marker->LengthCm = 0.0f;

	if (Marker->Patch)
	{
		Marker->Patch->SetStaticMesh(Marker->CircleMesh);

		// The engine's basic shapes occupy a BasicShapeSize cube, so a scale of
		// 1 is that wide. A circle of RadiusCm needs twice its radius over that,
		// and the height is flattened to MarkerThicknessCm so it reads as a
		// patch on the floor rather than as a pillar.
		Marker->Patch->SetRelativeScale3D(FVector(
			(RadiusCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
			(RadiusCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
			MarkerThicknessCm / ACataclysmCharacterBase::BasicShapeSize));
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A telegraph circle of %.0f cm was drawn for %s for %.2f s."),
		RadiusCm, *GetNameSafe(Caster), Seconds);

	return Marker;
}

ACataclysmTelegraphMarker* ACataclysmTelegraphMarker::ShowLine(
	AActor* Caster, const FVector& Start, const FVector& End,
	float HalfWidthCm, float Seconds)
{
	if (HalfWidthCm < SmallestUsefulRadiusCm)
	{
		return nullptr;
	}

	// FLATTENED BEFORE MEASURING. The caster's centre and the point it aimed at
	// are at different heights -- one is a capsule centre and the other is
	// wherever the target stood -- so an unflattened length would be the
	// hypotenuse rather than the ground the lane covers.
	FVector Along = End - Start;
	Along.Z = 0.0f;
	const float Length = Along.Size();
	if (Length <= 0.0f)
	{
		// Aimed at its own feet. There is no lane, and drawing a zero-length one
		// would leave a square patch that says nothing about a direction.
		return nullptr;
	}

	// Placed at the middle of the lane rather than at its start, because a cube
	// is centred on its own origin. Rotated so its X axis runs along the lane.
	const FVector Middle = Start + Along * 0.5f;

	ACataclysmTelegraphMarker* Marker =
		SpawnTelegraphMarker(Caster, Middle, Along.Rotation(), Seconds);
	if (!Marker)
	{
		return nullptr;
	}

	Marker->RadiusCm = HalfWidthCm;
	Marker->LengthCm = Length;

	if (Marker->Patch)
	{
		Marker->Patch->SetStaticMesh(Marker->LaneMesh);

		// X along the lane, Y across it at twice the projectile's radius, Z
		// flattened. The width is the projectile's own body, so what is marked
		// is exactly what will pass through.
		Marker->Patch->SetRelativeScale3D(FVector(
			Length / ACataclysmCharacterBase::BasicShapeSize,
			(HalfWidthCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
			MarkerThicknessCm / ACataclysmCharacterBase::BasicShapeSize));
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A telegraph lane %.0f cm long and %.0f cm wide was drawn for %s "
			 "for %.2f s."),
		Length, HalfWidthCm * 2.0f, *GetNameSafe(Caster), Seconds);

	return Marker;
}

void ACataclysmTelegraphMarker::Dismiss()
{
	if (!IsActorBeingDestroyed())
	{
		Destroy();
	}
}
