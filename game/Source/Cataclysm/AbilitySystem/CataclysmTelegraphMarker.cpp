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

const TCHAR* const ACataclysmTelegraphMarker::DesignedOutlineHex = TEXT("0A0F12");
const TCHAR* const ACataclysmTelegraphMarker::DesignedRingHex = TEXT("FF3020");
const TCHAR* const ACataclysmTelegraphMarker::DesignedInnerHex = TEXT("FFD9CF");
const TCHAR* const ACataclysmTelegraphMarker::DesignedFillHex = TEXT("FF3020");

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
	const TCHAR* TelegraphOpacityParameter = TEXT("Opacity");

	/**
	 * The four the sweeping fill is built from, named by
	 * tools/generate_telegraph_material.py.
	 *
	 * SweepOrigin and SweepScale are in the MESH's own space, not the world's
	 * and not the marker's. That is what lets one material serve both shapes:
	 * the cylinder and the cube map their texture coordinates differently, and
	 * local position does not. The scale is one over the distance from the
	 * origin to the edge along each axis, so the material's `Where` reaches
	 * exactly 1 at the edge, and an axis set to zero is one the sweep ignores.
	 *
	 * AN ALL-ZERO SweepScale MEANS NO SWEEP, which is the material's default and
	 * therefore what the three rings get without being told anything.
	 */
	const TCHAR* TelegraphSweepOriginParameter = TEXT("SweepOrigin");
	const TCHAR* TelegraphSweepScaleParameter = TEXT("SweepScale");
	const TCHAR* TelegraphSweepStartParameter = TEXT("SweepStartTime");
	const TCHAR* TelegraphSweepDurationParameter = TEXT("SweepDuration");
	const TCHAR* TelegraphSweepBandParameter = TEXT("SweepBand");


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
	TAutoConsoleVariable<FString> CVarTelegraphRingColour(
		TEXT("Cataclysm.Telegraph.RingColour"),
		TEXT(""),
		TEXT("sRGB hex, no leading hash, for the telegraph's bright ring and "
			 "its fill. Empty uses the designed FF3020."),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarTelegraphOutlineColour(
		TEXT("Cataclysm.Telegraph.OutlineColour"),
		TEXT(""),
		TEXT("sRGB hex, no leading hash, for the telegraph's outermost ring. "
			 "Empty uses the designed 0A0F12. This ring is what keeps the shape "
			 "readable against Celestial's gold and white, where the red ring "
			 "alone reaches 1.33:1."),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarTelegraphInnerColour(
		TEXT("Cataclysm.Telegraph.InnerColour"),
		TEXT(""),
		TEXT("sRGB hex, no leading hash, for the light line just outside the "
			 "fill. Empty uses the designed FFD9CF. This line is what keeps the "
			 "shape readable on War's mid grey and on Demonic lava, and without "
			 "it the worst case across the eight environments is 2.47:1, below "
			 "the 3:1 threshold."),
		ECVF_Default);

	/**
	 * Live override for how see-through the innermost band is.
	 *
	 * A NEGATIVE VALUE MEANS "USE THE DESIGN", rather than zero, because zero is
	 * a legitimate setting here: it makes the fill invisible and leaves the
	 * three rings, which is a real look worth being able to try.
	 */
	TAutoConsoleVariable<float> CVarTelegraphFillOpacity(
		TEXT("Cataclysm.Telegraph.FillOpacity"),
		-1.0f,
		TEXT("How opaque the telegraph's innermost band is, 0 to 1. Negative "
			 "uses the designed 0.35. Zero leaves only the three rings, which "
			 "is a legitimate look, which is why zero does not mean 'use the "
			 "design' here."),
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
		// The ability's stated wind-up, kept because the sweeping fill needs it.
		// Set before ApplyColours runs, which is what reads it.
		Marker->WindUpSeconds = Seconds;

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

	// THE RINGS ARE SIBLINGS OF THE FILL, NOT CHILDREN OF IT. The fill is scaled
	// to the marker's size, and a child would inherit that scale -- so a ring
	// sized as a fraction of its parent would grow with the marker instead of
	// staying a constant edge, which is what the three widths exist to avoid.
	Edge = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Edge"));
	Edge->SetupAttachment(RootComponent);
	Edge->SetAbsolute(false, false, /*bInAbsoluteScale=*/true);
	Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(RootComponent);
	Ring->SetAbsolute(false, false, /*bInAbsoluteScale=*/true);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Inner = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Inner"));
	Inner->SetupAttachment(RootComponent);
	Inner->SetAbsolute(false, false, /*bInAbsoluteScale=*/true);
	Inner->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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

void ACataclysmTelegraphMarker::BuildCircleBand(UStaticMeshComponent* Component,
											   float BandRadiusCm, int32 StepsDown)
{
	if (!Component)
	{
		return;
	}
	Component->SetStaticMesh(CircleMesh);
	Component->SetWorldScale3D(FVector(
		(BandRadiusCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(BandRadiusCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		MarkerThicknessCm / ACataclysmCharacterBase::BasicShapeSize));
	Component->SetRelativeLocation(
		FVector(0.0f, 0.0f, -MarkerThicknessCm * 0.2f * StepsDown));
}

void ACataclysmTelegraphMarker::BuildLaneBand(UStaticMeshComponent* Component,
											  float LaneLengthCm, float HalfWidthCm,
											  float GrowByCm, int32 StepsDown)
{
	if (!Component)
	{
		return;
	}
	Component->SetStaticMesh(LaneMesh);
	Component->SetWorldScale3D(FVector(
		(LaneLengthCm + GrowByCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(HalfWidthCm * 2.0f + GrowByCm * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		MarkerThicknessCm / ACataclysmCharacterBase::BasicShapeSize));
	Component->SetRelativeLocation(
		FVector(0.0f, 0.0f, -MarkerThicknessCm * 0.2f * StepsDown));
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

	const FLinearColor Ring3020 = ResolveColour(
		CVarTelegraphRingColour.GetValueOnAnyThread(), DesignedRingHex);
	const FLinearColor Outline = ResolveColour(
		CVarTelegraphOutlineColour.GetValueOnAnyThread(), DesignedOutlineHex);
	const FLinearColor InnerLine = ResolveColour(
		CVarTelegraphInnerColour.GetValueOnAnyThread(), DesignedInnerHex);

	const float OpacityOverride = CVarTelegraphFillOpacity.GetValueOnAnyThread();
	const float FillOpacity = OpacityOverride < 0.0f
		? DesignedFillOpacity
		: FMath::Clamp(OpacityOverride, 0.0f, 1.0f);

	// ONE MATERIAL, FOUR INSTANCES. The bands differ only in two parameter
	// values, so they share the asset and each gets its own dynamic instance to
	// set them on.
	//
	// THE THREE RINGS ARE FULLY OPAQUE and the fill is not. That split is the
	// whole arrangement: the rings carry the measured contrast and the fill only
	// tints the ground that hurts.
	auto Paint = [this](UStaticMeshComponent* Component,
						const FLinearColor& Colour,
						float Opacity) -> UMaterialInstanceDynamic*
	{
		if (!Component)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Instance =
			Component->CreateDynamicMaterialInstance(0, MarkerMaterial);
		if (Instance)
		{
			Instance->SetVectorParameterValue(TelegraphColourParameter, Colour);
			Instance->SetScalarParameterValue(TelegraphOpacityParameter, Opacity);
		}
		return Instance;
	};

	// THE THREE BOUNDARY BANDS ARE NOT SWEPT. They mark where the danger is and
	// have to be visible from the first frame. They are hollowed out instead,
	// so each draws as a ring of its own width and the ground inside the
	// marker is left alone. See ApplyRingShapes for why that was not always so.
	ApplyRingShapes(Paint(Edge, Outline, 1.0f),
					RadiusCm + OutlineThicknessCm, RimDarkCm);
	ApplyRingShapes(Paint(Ring, Ring3020, 1.0f),
					RadiusCm + RimBrightCm + RimLightCm, RimBrightCm);
	ApplyRingShapes(Paint(Inner, InnerLine, 1.0f),
					RadiusCm + RimLightCm, RimLightCm);

	ApplySweep(Paint(Patch,
					 ResolveColour(CVarTelegraphRingColour.GetValueOnAnyThread(),
								   DesignedFillHex),
					 FillOpacity));
}

void ACataclysmTelegraphMarker::ApplySweep(UMaterialInstanceDynamic* Fill) const
{
	const UWorld* World = GetWorld();
	if (!Fill || !World || WindUpSeconds <= 0.0f)
	{
		// Nothing to sweep over. Left on the material's all-zero SweepScale,
		// which draws the whole fill from the first frame -- which is the right
		// answer for a marker with no stated wind-up, rather than one that
		// never appears.
		return;
	}

	// THE MESH'S OWN SIZE, NOT THE MARKER'S. Local position is read before the
	// component's scale is applied, so these are the same two vectors whether a
	// marker is drawn at one metre or at six. Both engine basic shapes occupy a
	// BasicShapeSize cube centred on their own origin.
	const float Whole = ACataclysmCharacterBase::BasicShapeSize;
	const float Half = Whole * 0.5f;

	// A LANE SWEEPS FROM THE CASTER'S END, NOT FROM ITS MIDDLE. ShowLine rotates
	// the marker so local +X runs from the caster toward the point that was
	// aimed at, so local -X is the end the shot leaves from. Sweeping that way
	// means the fill travels the way the projectile will.
	//
	// A CIRCLE SWEEPS FROM ITS MIDDLE OUTWARDS, so the Y axis counts too and the
	// distance is radial.
	const FVector Origin = IsLane()
		? FVector(-Half, 0.0f, 0.0f)
		: FVector::ZeroVector;

	// One over the distance from the origin to the edge, per axis. Zero on an
	// axis the sweep ignores: a lane's width does not decide when a pixel is
	// drawn, and neither shape sweeps through its own thickness.
	const FVector Reach = IsLane()
		? FVector(1.0f / Whole, 0.0f, 0.0f)
		: FVector(1.0f / Half, 1.0f / Half, 0.0f);

	Fill->SetVectorParameterValue(TelegraphSweepOriginParameter,
								  FLinearColor(Origin));
	Fill->SetVectorParameterValue(TelegraphSweepScaleParameter,
								  FLinearColor(Reach));

	// The world's clock, because the material's Time node reads the same one.
	Fill->SetScalarParameterValue(TelegraphSweepStartParameter,
								  static_cast<float>(World->GetTimeSeconds()));
	Fill->SetScalarParameterValue(TelegraphSweepDurationParameter,
								  WindUpSeconds);

	// EVERYTHING BEHIND THE LEADING EDGE, so the fill is a disc that grows
	// rather than a band that travels. The project owner compared the two on
	// 2026-08-14 and chose this: a growing disc says how much ground is already
	// committed, where a band only says where its edge is.
	Fill->SetScalarParameterValue(TelegraphSweepBandParameter,
								  FillCoversEverythingBehindTheEdge);
}

void ACataclysmTelegraphMarker::ApplyRingShapes(UMaterialInstanceDynamic* Band,
												float BandRadiusCm,
												float WidthCm) const
{
	// A LANE'S BANDS ARE RECTANGLES and the material measures distance
	// radially, so hollowing one out would cut an oval from a rectangle. Lanes
	// keep the stacked discs they have always had; issue #553 covers it.
	if (!Band || IsLane() || BandRadiusCm <= 0.0f || WidthCm <= 0.0f)
	{
		return;
	}

	const float Half = ACataclysmCharacterBase::BasicShapeSize * 0.5f;

	Band->SetVectorParameterValue(TelegraphSweepOriginParameter,
								  FLinearColor(FVector::ZeroVector));
	Band->SetVectorParameterValue(
		TelegraphSweepScaleParameter,
		FLinearColor(FVector(1.0f / Half, 1.0f / Half, 0.0f)));

	// PROGRESS PINNED AT 1, because a ring is not a sweep -- it is the shape a
	// finished sweep leaves behind. A start time in the past divided by almost
	// no duration lands the material's saturate() on 1 whatever the world clock
	// happens to read, which a duration of zero would not: the clock is near
	// zero at the start of a session.
	Band->SetScalarParameterValue(TelegraphSweepStartParameter, -1.0f);
	Band->SetScalarParameterValue(TelegraphSweepDurationParameter, 0.0001f);

	// What shows is the outermost WidthCm of this band's own radius. Clamped,
	// because a width at or past the radius is a solid disc again, which is the
	// thing this exists to stop.
	Band->SetScalarParameterValue(TelegraphSweepBandParameter,
								  FMath::Min(WidthCm / BandRadiusCm, 1.0f));
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

	// FOUR CONCENTRIC DISCS, EACH SMALLER AND HIGHER THAN THE ONE BELOW, so what
	// shows of each is a ring of its own width. Stacking solid discs draws the
	// bands without the material needing to know the shape's texture
	// coordinates, which the cylinder and the cube map differently.
	Marker->BuildCircleBand(Marker->Edge, RadiusCm + OutlineThicknessCm, 3);
	Marker->BuildCircleBand(Marker->Ring, RadiusCm + RimBrightCm + RimLightCm, 2);
	Marker->BuildCircleBand(Marker->Inner, RadiusCm + RimLightCm, 1);

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

	// The same three rings as a circle gets. A lane grows at both ends as well
	// as both sides, which is right: the rings mark the edge of the danger, and
	// the danger has an end.
	Marker->BuildLaneBand(Marker->Edge, Length, HalfWidthCm, OutlineThicknessCm, 3);
	Marker->BuildLaneBand(Marker->Ring, Length, HalfWidthCm,
						  RimBrightCm + RimLightCm, 2);
	Marker->BuildLaneBand(Marker->Inner, Length, HalfWidthCm, RimLightCm, 1);

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
