// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

const TCHAR* const ACataclysmTelegraphMarker::DesignedFillHex = TEXT("00B8C4");
const TCHAR* const ACataclysmTelegraphMarker::DesignedOutlineHex = TEXT("0A0F12");

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

	/** Built by tools/generate_telegraph_material.py. Unlit, one vector
	 *  parameter. */
	const TCHAR* TelegraphMaterialPath =
		TEXT("/Game/Effects/M_TelegraphMarker.M_TelegraphMarker");

	/** The parameter that script names. Changing one without the other leaves
	 *  every marker at the material's default, which is the fill colour -- so
	 *  the rim would silently become a second fill. */
	const TCHAR* TelegraphColourParameter = TEXT("Colour");

	/**
	 * Live overrides for the two colours, as sRGB hex without a leading hash.
	 *
	 * WHY THEY ARE WANTED. The project owner accepted cyan on 2026-08-12 with a
	 * stated reservation: red is the genre's colour for danger and a blue-green
	 * warning "just doesn't feel threatening". That is a judgement about how the
	 * game feels, which is the kind of thing that has to be settled by looking
	 * at it rather than by measuring swatches.
	 *
	 * The measured case against red is real and is why cyan is the default: this
	 * game's Demonic Cataclysm is lava and fire, so a red warning is at its least
	 * readable in the one environment that already exists. Both are one console
	 * command away, and the sandbox is where that argument gets settled.
	 *
	 *     Cataclysm.Telegraph.FillColour FF3020
	 *     Cataclysm.Telegraph.FillColour ""
	 *
	 * Empty uses the designed value, matching the other live overrides in this
	 * project, where zero means "use the design".
	 */
	TAutoConsoleVariable<FString> CVarTelegraphFillColour(
		TEXT("Cataclysm.Telegraph.FillColour"),
		TEXT(""),
		TEXT("sRGB hex, no leading hash, for the telegraph's fill. Empty uses "
			 "the designed 00B8C4. Try FF3020 for the red the genre uses; note "
			 "that red is at its worst against Demonic lava, which is the only "
			 "environment art this project has."),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarTelegraphOutlineColour(
		TEXT("Cataclysm.Telegraph.OutlineColour"),
		TEXT(""),
		TEXT("sRGB hex, no leading hash, for the telegraph's rim. Empty uses "
			 "the designed 0A0F12. The rim is what makes the shape readable "
			 "against a bright environment; the fill alone reaches only 1.91:1 "
			 "against Celestial's gold and white."),
		ECVF_Default);

	/**
	 * A hex string as a linear colour, falling back to Designed when it is empty
	 * or will not parse.
	 *
	 * IT FALLS BACK RATHER THAN FAILING because this reads a console variable a
	 * person typed. A typo should put the designed colour back and say so, not
	 * leave a marker black or refuse to draw the warning at all.
	 */
	FLinearColor ResolveColour(const FString& Override, const TCHAR* Designed)
	{
		FString Hex = Override.TrimStartAndEnd();
		Hex.RemoveFromStart(TEXT("#"));

		// THE LENGTH IS NOT ENOUGH ON ITS OWN, and checking only it was a real
		// bug here: FColor::FromHex does not report bad input, it returns
		// something. The word "nonsense" is eight characters, so a length-only
		// check accepted it and produced a colour nobody asked for instead of
		// falling back. The automation test for the fallback is what found it.
		bool bIsHex = Hex.Len() == 6 || Hex.Len() == 8;
		for (const TCHAR Character : Hex)
		{
			if (!FChar::IsHexDigit(Character))
			{
				bIsHex = false;
				break;
			}
		}

		if (bIsHex)
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
		}

		if (!Hex.IsEmpty())
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Telegraph colour override '%s' is not 6 or 8 hex digits. "
					 "Using the designed %s instead."),
				*Override, Designed);
		}

		return FLinearColor::FromSRGBColor(FColor::FromHex(Designed));
	}

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

	// THE RIM IS A SIBLING OF THE FILL, NOT A CHILD OF IT. The fill is scaled to
	// the marker's size, and a child would inherit that scale -- so a rim sized
	// as a fraction of its parent would grow with the marker instead of staying
	// a constant edge, which is the thing OutlineThicknessCm exists to avoid.
	Edge = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Edge"));
	Edge->SetupAttachment(RootComponent);
	Edge->SetAbsolute(false, false, /*bInAbsoluteScale=*/true);
	Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FoundMaterial(
		TelegraphMaterialPath);
	if (FoundMaterial.Succeeded())
	{
		MarkerMaterial = FoundMaterial.Object;
	}
}

void ACataclysmTelegraphMarker::ApplyColours()
{
	if (!MarkerMaterial)
	{
		// Left on the engine default, which is lit. Said out loud, because the
		// symptom -- a warning that dims with the room -- is exactly what issue
		// #539 was about and is easy to mistake for a lighting problem.
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s was not found, so this telegraph is drawn with the engine's "
				 "lit default and its brightness will follow the room's lighting."),
			TelegraphMaterialPath);
		return;
	}

	const FLinearColor Fill = ResolveColour(
		CVarTelegraphFillColour.GetValueOnAnyThread(), DesignedFillHex);
	const FLinearColor Outline = ResolveColour(
		CVarTelegraphOutlineColour.GetValueOnAnyThread(), DesignedOutlineHex);

	// ONE MATERIAL, TWO INSTANCES. The fill and the rim differ only in the value
	// of one parameter, so they share the asset and each gets its own dynamic
	// instance to set it on.
	if (Patch)
	{
		if (UMaterialInstanceDynamic* FillMaterial =
				Patch->CreateDynamicMaterialInstance(0, MarkerMaterial))
		{
			FillMaterial->SetVectorParameterValue(TelegraphColourParameter, Fill);
		}
	}

	if (Edge)
	{
		if (UMaterialInstanceDynamic* OutlineMaterial =
				Edge->CreateDynamicMaterialInstance(0, MarkerMaterial))
		{
			OutlineMaterial->SetVectorParameterValue(TelegraphColourParameter, Outline);
		}
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

	if (Marker->Edge)
	{
		Marker->Edge->SetStaticMesh(Marker->CircleMesh);

		// The same circle, one rim wider all round, and pushed down by a
		// fraction of the marker's own thickness so the fill wins where they
		// overlap without the two fighting over the same depth.
		const float EdgeRadius = RadiusCm + OutlineThicknessCm;
		Marker->Edge->SetWorldScale3D(FVector(
			(EdgeRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
			(EdgeRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
			MarkerThicknessCm / ACataclysmCharacterBase::BasicShapeSize));
		Marker->Edge->SetRelativeLocation(FVector(0.0f, 0.0f, -MarkerThicknessCm * 0.25f));
	}

	Marker->ApplyColours();

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

	if (Marker->Edge)
	{
		Marker->Edge->SetStaticMesh(Marker->LaneMesh);

		// One rim wider on both axes. The lane grows at both ends as well as
		// both sides, which is right: the rim marks the edge of the danger, and
		// the danger has an end.
		Marker->Edge->SetWorldScale3D(FVector(
			(Length + OutlineThicknessCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
			(HalfWidthCm * 2.0f + OutlineThicknessCm * 2.0f)
				/ ACataclysmCharacterBase::BasicShapeSize,
			MarkerThicknessCm / ACataclysmCharacterBase::BasicShapeSize));
		Marker->Edge->SetRelativeLocation(FVector(0.0f, 0.0f, -MarkerThicknessCm * 0.25f));
	}

	Marker->ApplyColours();

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
