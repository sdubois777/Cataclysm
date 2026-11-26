// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDeviceWire.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * The engine's cube, a bar once squashed, and the telegraph markers' material, unlit and translucent. Found by path,
	 * so this adds no asset to the project.
	 *
	 * NAMED FOR THIS FILE. Unreal builds this module as a unity build, where an anonymous namespace is shared by every
	 * file in the unit; `CataclysmTelegraphMarker.cpp` and `CataclysmGuideFootprints.cpp` name the same assets.
	 */
	const TCHAR* DeviceWireMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* DeviceWireMaterialPath = TEXT("/Game/Effects/M_TelegraphMarker.M_TelegraphMarker");

	/** The two parameters `tools/generate_telegraph_material.py` gives the material. */
	const TCHAR* DeviceWireColourParameter = TEXT("Colour");
	const TCHAR* DeviceWireOpacityParameter = TEXT("Opacity");

	/** The engine's cube is 100 cm along each side. */
	constexpr float DeviceWireCubeCm = 100.0f;
}

const TCHAR* const ACataclysmDeviceWire::ColourHex = TEXT("C8D2DC");

ACataclysmDeviceWire::ACataclysmDeviceWire()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoundMesh(DeviceWireMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FoundMaterial(DeviceWireMaterialPath);
	if (FoundMaterial.Succeeded())
	{
		WireMaterial = FoundMaterial.Object;
	}

	Line = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Line"));
	Line->SetupAttachment(Root);
	Line->SetMobility(EComponentMobility::Movable);
	Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Line->SetGenerateOverlapEvents(false);
	Line->SetCanEverAffectNavigation(false);
	Line->SetCastShadow(false);
	if (FoundMesh.Succeeded())
	{
		Line->SetStaticMesh(FoundMesh.Object);
	}
}

ACataclysmDeviceWire* ACataclysmDeviceWire::Stretch(UWorld* World, AActor* Owner, const FVector& From, const FVector& To)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.Owner = Owner;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmDeviceWire* Wire =
		World->SpawnActor<ACataclysmDeviceWire>((From + To) * 0.5f, FRotator::ZeroRotator, Parameters);
	if (!Wire)
	{
		return nullptr;
	}
	if (Wire->WireMaterial)
	{
		if (UMaterialInstanceDynamic* Paint = Wire->Line->CreateDynamicMaterialInstance(0, Wire->WireMaterial))
		{
			Paint->SetVectorParameterValue(DeviceWireColourParameter, FLinearColor(FColor::FromHex(ColourHex)));
			Paint->SetScalarParameterValue(DeviceWireOpacityParameter, 1.0f);
		}
	}
	Wire->Span(From, To);
	return Wire;
}

void ACataclysmDeviceWire::Span(const FVector& From, const FVector& To)
{
	const FVector Along = To - From;
	LengthCm = Along.Size();
	SetActorLocationAndRotation((From + To) * 0.5f, Along.IsNearlyZero() ? FRotator::ZeroRotator : Along.Rotation(),
								/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	Line->SetRelativeScale3D(FVector(FMath::Max(LengthCm, 1.0f) / DeviceWireCubeCm, ThicknessCm / DeviceWireCubeCm,
									 ThicknessCm / DeviceWireCubeCm));
}
