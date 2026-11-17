// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDebrisBurst.h"
#include "AbilitySystem/CataclysmMeshWidth.h"
#include "Cataclysm.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

ACataclysmDebrisBurst::ACataclysmDebrisBurst()
{
	// Nothing to do per frame. The pieces are placed once and removed by the
	// lifespan; see the class comment on why they do not move.
	PrimaryActorTick.bCanEverTick = false;

	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);
}

ACataclysmDebrisBurst* ACataclysmDebrisBurst::Scatter(
	AActor* Instigator, const FVector& At,
	const TArray<UStaticMesh*>& Pieces, UMaterialInterface* Material,
	float SpreadCm, float PieceRadiusCm, float Seconds)
{
	UWorld* World = Instigator ? Instigator->GetWorld() : nullptr;
	if (!World || Seconds <= 0.0f)
	{
		return nullptr;
	}

	// COUNTED BEFORE ANYTHING IS SPAWNED. A caller without its art passes an
	// array of nulls, and an empty actor sitting in the world for two seconds is
	// a thing to clean up rather than an effect.
	int32 Usable = 0;
	for (const UStaticMesh* Piece : Pieces)
	{
		if (Piece)
		{
			++Usable;
		}
	}
	if (Usable == 0)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Instigator;

	// ALWAYS SPAWN. Debris appears exactly where something was hit, which is
	// where something is standing more often than not.
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmDebrisBurst* Burst = World->SpawnActor<ACataclysmDebrisBurst>(
		At, FRotator::ZeroRotator, SpawnParams);
	if (!Burst)
	{
		return nullptr;
	}

	int32 Index = 0;
	for (UStaticMesh* Piece : Pieces)
	{
		if (!Piece)
		{
			continue;
		}

		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Burst);
		Component->SetupAttachment(Burst->Anchor);
		Component->RegisterComponent();
		Component->SetStaticMesh(Piece);

		// NO COLLISION, like every other placeholder in this project. Debris that
		// blocked movement would let a thrown rock wall a doorway, and debris
		// that swept for overlaps would be a second way to hit somebody.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (Material)
		{
			// SEE THE CLASS COMMENT. The Paragon fragments carry the engine's
			// checkerboard placeholder, so without this they are five grey
			// checkered lumps.
			Component->SetMaterial(0, Material);
		}

		// SCALED FROM EACH MESH'S OWN BOUNDS, so that pieces of different sizes
		// come out consistent with one another rather than at whatever size they
		// happen to have been authored. Shared with the projectile's flying body
		// and the rock the Brute carries; see CataclysmMeshWidth.h and #453.
		const float Scale = CataclysmMeshWidth::ScaleFor(Piece, PieceRadiusCm);
		if (Scale > 0.0f)
		{
			Component->SetRelativeScale3D(FVector(Scale, Scale, Scale));
		}

		// SPREAD EVENLY AROUND THE POINT RATHER THAN AT RANDOM. A ring is not
		// what broken rock does, but it is what can be checked: a test can say
		// where every piece is. Randomness here would buy a slightly better look
		// and cost the only assertion worth making about placement.
		const float Angle = (2.0f * PI * static_cast<float>(Index))
			/ static_cast<float>(Usable);
		Component->SetRelativeLocation(FVector(
			FMath::Cos(Angle) * SpreadCm, FMath::Sin(Angle) * SpreadCm, 0.0f));

		// Turned to face outward, so five copies of the same silhouette do not
		// read as five copies.
		Component->SetRelativeRotation(
			FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f));

		Burst->Pieces.Add(Component);
		++Index;
	}

	Burst->PiecesPlaced = Burst->Pieces.Num();
	Burst->SetLifeSpan(Seconds);

	UE_LOG(LogCataclysm, Verbose,
		TEXT("%d pieces of debris were left by %s for %.2f s."),
		Burst->PiecesPlaced, *GetNameSafe(Instigator), Seconds);

	return Burst;
}
