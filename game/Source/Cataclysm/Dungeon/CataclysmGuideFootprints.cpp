// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmGuideFootprints.h"

#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
// For the player's default walk, which the pool is sized against. Issue #2862.
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * The engine's cylinder, a print's shape once squashed, and the telegraph markers' material, unlit and translucent.
	 * Found by path, so this adds no asset to the project.
	 *
	 * NAMED FOR THIS FILE. Unreal builds this module as a unity build, where an anonymous namespace is shared by every
	 * file in the unit; `CataclysmTelegraphMarker.cpp` names the same material and parameters under its own names.
	 */
	const TCHAR* GuideFootprintsMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GuideFootprintsMaterialPath = TEXT("/Game/Effects/M_TelegraphMarker.M_TelegraphMarker");

	/** The two parameters `tools/generate_telegraph_material.py` gives the material. */
	const TCHAR* GuideFootprintsColourParameter = TEXT("Colour");
	const TCHAR* GuideFootprintsOpacityParameter = TEXT("Opacity");
}

// THE POOL HOLDS A WHOLE LIFETIME'S TRAIL AT THE GUIDE'S DEFAULT WALK, measured against the player's own default here,
// where that constant can be named. The effects header holds the same check with the figure written out.
static_assert(UCataclysmDungeonModifierEffects::DemonicGuideFootprintsAtMost
				  * UCataclysmDungeonModifierEffects::DemonicGuideFootprintSpacingCm
			  >= UCataclysmDungeonModifierEffects::DemonicGuideFootprintSeconds
				  * ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond
				  * UCataclysmDungeonModifierEffects::DemonicGuideWalkShareOfPlayer,
			  "A pool smaller than a lifetime's trail at the default walk would cut the guide's trail short at its "
			  "ordinary pace.");

ACataclysmGuideFootprints::ACataclysmGuideFootprints()
{
	using Effects = UCataclysmDungeonModifierEffects;

	// STEPPED BY THE GAME MODE'S BEAT, so it never ticks.
	PrimaryActorTick.bCanEverTick = false;

	// A BARE SCENE ROOT: an actor with no scene root reports the world origin as its location however it was spawned.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// FOUND HERE BECAUSE A FINDER ASSERTS OUTSIDE A CONSTRUCTOR, as the telegraph marker's are.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoundMesh(GuideFootprintsMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FoundMaterial(GuideFootprintsMaterialPath);
	if (FoundMaterial.Succeeded())
	{
		PrintMaterial = FoundMaterial.Object;
	}

	// THE WHOLE POOL, MADE ONCE. Each print is placed in the world's frame, not the root's, and starts hidden.
	Prints.Reserve(Effects::DemonicGuideFootprintsAtMost);
	for (int32 Index = 0; Index < Effects::DemonicGuideFootprintsAtMost; ++Index)
	{
		UStaticMeshComponent* Print = CreateDefaultSubobject<UStaticMeshComponent>(
			FName(*FString::Printf(TEXT("Print%d"), Index)));
		Print->SetupAttachment(Root);
		Print->SetMobility(EComponentMobility::Movable);
		Print->SetUsingAbsoluteLocation(true);
		Print->SetUsingAbsoluteRotation(true);
		Print->SetUsingAbsoluteScale(true);
		Print->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Print->SetGenerateOverlapEvents(false);
		Print->SetCanEverAffectNavigation(false);
		Print->SetCastShadow(false);
		Print->SetVisibility(false);
		if (FoundMesh.Succeeded())
		{
			Print->SetStaticMesh(FoundMesh.Object);
		}
		Prints.Add(Print);
	}
	Paints.Init(nullptr, Prints.Num());
	Ages.Init(-1.0f, Prints.Num());
}

ACataclysmGuideFootprints* ACataclysmGuideFootprints::Begin(UWorld* World, const FVector& Where)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ACataclysmGuideFootprints>(Where, FRotator::ZeroRotator, Parameters);
}

float ACataclysmGuideFootprints::OpacityAtAge(float AgeSeconds)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// EVENLY TO NOTHING, so the newest print is the brightest and the trail reads in the direction the guide went.
	const float Left = 1.0f - FMath::Clamp(AgeSeconds / Effects::DemonicGuideFootprintSeconds, 0.0f, 1.0f);
	return Effects::DemonicGuideFootprintOpacity * Left;
}

int32 ACataclysmGuideFootprints::Step(const FVector& FeetAt, const FRotator& Facing, float Seconds, bool bGuideWaits)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE PRINTS DOWN ALREADY AGE FIRST, so a print laid on this beat starts at nought. NOT WHILE THE GUIDE WAITS: it
	// waits when the player has fallen behind, which is when the trail is wanted.
	if (!bGuideWaits && Seconds > 0.0f)
	{
		for (int32 Slot = 0; Slot < Ages.Num(); ++Slot)
		{
			if (Ages[Slot] < 0.0f)
			{
				continue;
			}
			Ages[Slot] += Seconds;
			if (Ages[Slot] >= Effects::DemonicGuideFootprintSeconds)
			{
				Lift(Slot);
			}
			else
			{
				Paint(Slot);
			}
		}
	}

	FVector Pointing = Facing.Vector();
	Pointing.Z = 0.0f;
	if (!Pointing.Normalize())
	{
		Pointing = FVector::ForwardVector;
	}

	// THE FIRST STEP, AND A GUIDE PUT SOMEWHERE RATHER THAN WALKED THERE: one print where it stands, and the trail goes
	// on from it. A gap is not printed.
	if (!bStarted || FVector::Dist2D(LastPrintAt, FeetAt) > Effects::DemonicGuideFootprintJumpCm)
	{
		LayAt(FeetAt, Pointing);
		LastPrintAt = FeetAt;
		bStarted = true;
		return 1;
	}

	// ONE PRINT EVERY SPACING ALONG THE LINE WALKED SINCE THE LAST, which is straight enough over one beat. AT MOST A
	// POOL'S WORTH, which a move no longer than a jump never reaches; it bounds the loop whatever is passed.
	int32 LaidNow = 0;
	while (LaidNow < Prints.Num())
	{
		FVector Towards = FeetAt - LastPrintAt;
		const float Height = Towards.Z;
		Towards.Z = 0.0f;
		const float Apart = Towards.Size();
		if (Apart < Effects::DemonicGuideFootprintSpacingCm)
		{
			break;
		}
		const FVector Along = Towards / Apart;
		FVector Next = LastPrintAt + Along * Effects::DemonicGuideFootprintSpacingCm;
		// THE HEIGHT FOLLOWS THE FEET IN THE SAME SHARE, so a print on a slope or a stair lies on it.
		Next.Z = LastPrintAt.Z + Height * (Effects::DemonicGuideFootprintSpacingCm / Apart);
		LayAt(Next, Along);
		LastPrintAt = Next;
		++LaidNow;
	}
	return LaidNow;
}

void ACataclysmGuideFootprints::LayAt(const FVector& Centre, const FVector& Along)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (Prints.IsEmpty())
	{
		return;
	}

	// THE PRINT LAID LONGEST AGO, whether it has faded or not: prints are laid in turn, so the next slot is the oldest.
	const int32 Slot = NextSlot;
	NextSlot = (NextSlot + 1) % Prints.Num();

	UStaticMeshComponent* Print = Prints[Slot];
	if (!Print)
	{
		return;
	}

	// LEFT AND RIGHT IN TURN, EACH A STEP'S WIDTH OFF THE LINE, so the trail reads as feet rather than as a dotted line.
	const FVector Side(-Along.Y, Along.X, 0.0f);
	const float Offset = bNextOnTheLeft ? -Effects::DemonicGuideFootprintSideCm : Effects::DemonicGuideFootprintSideCm;
	bNextOnTheLeft = !bNextOnTheLeft;

	const FVector Where = Centre + Side * Offset + FVector(0.0f, 0.0f, PrintLiftCm);
	Print->SetWorldLocationAndRotation(Where, FRotator(0.0f, Along.Rotation().Yaw, 0.0f));

	// THE CYLINDER IS A 100 CM BLOCK, so each axis is the figure over that: long along the walk, narrow across it, flat.
	Print->SetWorldScale3D(FVector(
		Effects::DemonicGuideFootprintLengthCm / ACataclysmCharacterBase::BasicShapeSize,
		Effects::DemonicGuideFootprintWidthCm / ACataclysmCharacterBase::BasicShapeSize,
		PrintThicknessCm / ACataclysmCharacterBase::BasicShapeSize));

	Ages[Slot] = 0.0f;
	Print->SetVisibility(true);
	Paint(Slot);
	++Laid;
}

void ACataclysmGuideFootprints::Paint(int32 Slot)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!PrintMaterial)
	{
		if (!bSaidNoMaterial)
		{
			bSaidNoMaterial = true;
			UE_LOG(LogCataclysm, Warning,
				   TEXT("%s was not found, so Demonic Guide's footprints are drawn in the engine's default, unfaded."),
				   GuideFootprintsMaterialPath);
		}
		return;
	}

	UMaterialInstanceDynamic* Instance = Paints[Slot];
	if (!Instance)
	{
		// THE COLOUR ONCE, WHEN THE INSTANCE IS MADE: it never changes. The opacity on every beat.
		Instance = Prints[Slot]->CreateDynamicMaterialInstance(0, PrintMaterial);
		Paints[Slot] = Instance;
		if (Instance)
		{
			Instance->SetVectorParameterValue(
				GuideFootprintsColourParameter,
				FLinearColor(FColor::FromHex(Effects::DemonicGuideFootprintColourHex)));
		}
	}
	if (Instance)
	{
		Instance->SetScalarParameterValue(GuideFootprintsOpacityParameter, OpacityAtAge(Ages[Slot]));
	}
}

void ACataclysmGuideFootprints::Lift(int32 Slot)
{
	Ages[Slot] = -1.0f;
	if (UStaticMeshComponent* Print = Prints[Slot])
	{
		Print->SetVisibility(false);
	}
}

int32 ACataclysmGuideFootprints::PrintsShown() const
{
	int32 Shown = 0;
	for (const float Age : Ages)
	{
		Shown += Age >= 0.0f ? 1 : 0;
	}
	return Shown;
}

int32 ACataclysmGuideFootprints::PrintComponentsVisible() const
{
	int32 Visible = 0;
	for (const TObjectPtr<UStaticMeshComponent>& Print : Prints)
	{
		Visible += Print && Print->IsVisible() ? 1 : 0;
	}
	return Visible;
}

void ACataclysmGuideFootprints::ShownPrints(TArray<FVector>& OutWhere, TArray<float>& OutOpacity) const
{
	OutWhere.Reset();
	OutOpacity.Reset();

	// FROM THE NEXT SLOT ROUND, which is the order they were laid in: the next slot holds the oldest.
	for (int32 Index = 0; Index < Prints.Num(); ++Index)
	{
		const int32 Slot = (NextSlot + Index) % Prints.Num();
		if (Ages[Slot] < 0.0f || !Prints[Slot])
		{
			continue;
		}
		OutWhere.Add(Prints[Slot]->GetComponentLocation());
		OutOpacity.Add(OpacityAtAge(Ages[Slot]));
	}
}
