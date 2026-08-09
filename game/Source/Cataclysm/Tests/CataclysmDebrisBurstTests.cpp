// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDebrisBurst.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for `ACataclysmDebrisBurst`, which places meshes at a point and clears
 * them up after itself.
 *
 * WHAT USES IT NOW. Only the Brute's rip crater, which passes a single mesh with
 * no spread. It was written for the five pieces a thrown rock broke into, and
 * the project owner had those removed on 2026-08-08 as unwanted, which is issue
 * #455. The class stays because the crater needs it and because it is generic:
 * it knows nothing about rocks.
 *
 * THE TRAP THAT IS NOT OBVIOUS, and the reason a material is a separate
 * argument. Measured 2026-08-08: the Paragon pack's debris meshes have
 * `/Engine/EngineMaterials/WorldGridMaterial` assigned -- the engine's grey
 * checkerboard placeholder. Spawning one as it comes would put a large checkered
 * lump on the floor, which is worse than nothing at all. The material has to be
 * supplied by whoever scatters them, and the first test below is what stops that
 * being forgotten.
 */

namespace CataclysmDebrisTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;

	/** An engine shape, so the placement and sizing can be checked without art. */
	static UStaticMesh* EngineCube()
	{
		return Cast<UStaticMesh>(
			FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")).TryLoad());
	}

	/**
	 * A material that is NOT what any of these meshes already wear.
	 *
	 * THE FIRST VERSION OF THIS TEST USED THE CHECKERBOARD, and that could
	 * not fail: the engine cube's own default material IS the checkerboard,
	 * so asking whether a piece wears it answered yes whether or not the
	 * material was ever applied. Proved by deleting the SetMaterial call and
	 * watching the test pass.
	 */
	static UMaterialInterface* SomethingOtherThanThePlaceholder()
	{
		return Cast<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"))
			.TryLoad());
	}

	/** The engine's placeholder, which is what the pack's fragments carry
	 *  and what must never be what a piece ends up wearing. */
	static UMaterialInterface* CheckerboardPlaceholder()
	{
		return Cast<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"))
			.TryLoad());
	}

	static int32 CountBursts(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmDebrisBurst> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmDebrisIsPlacedSizedAndDressed,
	"Cataclysm.Debris.PiecesArePlacedAroundThePointSizedAndGivenTheirMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebrisIsPlacedSizedAndDressed::RunTest(const FString&)
{
	using namespace CataclysmDebrisTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Caster = World->SpawnActor<AActor>();
	UStaticMesh* Cube = EngineCube();
	UMaterialInterface* Dressing = SomethingOtherThanThePlaceholder();
	if (!TestNotNull(TEXT("caster"), Caster)
		|| !TestNotNull(TEXT("an engine mesh to scatter"), Cube))
	{
		return false;
	}

	const FVector At(3.0f * M, 0.0f, 0.0f);
	const float SpreadCm = 26.0f;
	const float PieceRadiusCm = 13.0f;

	TArray<UStaticMesh*> Pieces;
	Pieces.Add(Cube);
	Pieces.Add(Cube);
	Pieces.Add(Cube);
	Pieces.Add(Cube);

	ACataclysmDebrisBurst* Burst = ACataclysmDebrisBurst::Scatter(
		Caster, At, Pieces, Dressing, SpreadCm, PieceRadiusCm, /*Seconds=*/2.0f);
	if (!TestNotNull(TEXT("a burst of debris appears"), Burst))
	{
		return false;
	}

	TestEqual(TEXT("with one piece per mesh given"), Burst->PiecesPlaced, 4);
	TestEqual(TEXT("and it is where the impact was"), Burst->GetActorLocation(), At);

	for (int32 Index = 0; Index < Burst->Pieces.Num(); ++Index)
	{
		UStaticMeshComponent* Piece = Burst->Pieces[Index].Get();
		if (!TestNotNull(TEXT("a piece"), Piece))
		{
			return false;
		}

		// PLACED AROUND THE POINT, NOT ON IT. Five meshes at one location is one
		// mesh as far as anybody looking can tell.
		const float Distance = static_cast<float>(FVector2D(
			Piece->GetRelativeLocation().X, Piece->GetRelativeLocation().Y).Size());
		TestEqual(FString::Printf(
			TEXT("piece %d sits the spread distance from the impact"), Index),
			Distance, SpreadCm);

		// SIZED FROM THE MESH'S OWN BOUNDS. The pack's five fragments range from
		// 56 to 96 cm of half-width, so five pieces at scale 1 would be five
		// different sizes.
		const FVector Extent = Piece->GetStaticMesh()->GetBounds().BoxExtent;
		const FVector Scale = Piece->GetRelativeScale3D();
		TestEqual(FString::Printf(TEXT("piece %d is the size asked for"), Index),
			static_cast<float>(FMath::Max(Extent.X * Scale.X, Extent.Y * Scale.Y)),
			PieceRadiusCm);

		// DRESSED IN WHAT IT WAS GIVEN. This is the assertion the whole file
		// exists for; see the note at the top.
		TestEqual(FString::Printf(TEXT("piece %d wears the material given"), Index),
			Piece->GetMaterial(0), Dressing);

		// AND SAID THE OTHER WAY ROUND, because the failure that matters is
		// not "wrong material" but "still wearing the pack's checkerboard".
		TestTrue(FString::Printf(
			TEXT("piece %d is not left in the engine's checkerboard"), Index),
			Piece->GetMaterial(0) != CheckerboardPlaceholder());

		// NO COLLISION. Debris that blocked movement would let a thrown rock
		// wall a doorway.
		TestEqual(FString::Printf(TEXT("piece %d cannot block or hit anything"), Index),
			static_cast<int32>(Piece->GetCollisionEnabled()),
			static_cast<int32>(ECollisionEnabled::NoCollision));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmDebrisRefusesWhatIsNotAnEffect,
	"Cataclysm.Debris.NothingIsSpawnedWhenThereIsNothingToPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebrisRefusesWhatIsNotAnEffect::RunTest(const FString&)
{
	using namespace CataclysmDebrisTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	// THE CASE THIS EXISTS FOR IS A FRESH CLONE. The Paragon packs are
	// gitignored, so a caller's mesh list may be empty and every entry in it may
	// be null. An empty actor sitting in the world for two seconds is something
	// to clean up rather than an effect.
	TArray<UStaticMesh*> Nothing;
	TestNull(TEXT("an empty list places nothing"),
		ACataclysmDebrisBurst::Scatter(
			Caster, FVector::ZeroVector, Nothing, nullptr, 26.0f, 13.0f, 2.0f));

	TArray<UStaticMesh*> AllNull;
	AllNull.Add(nullptr);
	AllNull.Add(nullptr);
	TestNull(TEXT("and a list of nulls places nothing either"),
		ACataclysmDebrisBurst::Scatter(
			Caster, FVector::ZeroVector, AllNull, nullptr, 26.0f, 13.0f, 2.0f));

	TestEqual(TEXT("so no burst was left in the world"), CountBursts(World), 0);

	// A life of no length is not an effect either.
	TArray<UStaticMesh*> Real;
	if (UStaticMesh* Cube = EngineCube())
	{
		Real.Add(Cube);
		TestNull(TEXT("and neither is a burst that lasts no time"),
			ACataclysmDebrisBurst::Scatter(
				Caster, FVector::ZeroVector, Real, nullptr, 26.0f, 13.0f,
				/*Seconds=*/0.0f));
	}

	// A LIST WITH SOME ART IS STILL AN EFFECT. A caller whose pack is partly
	// installed gets what it has rather than nothing.
	if (!Real.IsEmpty())
	{
		TArray<UStaticMesh*> Partial;
		Partial.Add(nullptr);
		Partial.Add(Real[0]);
		Partial.Add(nullptr);

		ACataclysmDebrisBurst* Burst = ACataclysmDebrisBurst::Scatter(
			Caster, FVector::ZeroVector, Partial, nullptr, 26.0f, 13.0f, 2.0f);
		if (TestNotNull(TEXT("a partly filled list still places what it has"), Burst))
		{
			TestEqual(TEXT("and places only the real ones"),
				Burst->PiecesPlaced, 1);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
