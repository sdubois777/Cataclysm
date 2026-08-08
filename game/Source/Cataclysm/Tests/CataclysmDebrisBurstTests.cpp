// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDebrisBurst.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the broken pieces left where something hit.
 *
 * WHAT THESE GUARD. Issue #422. `ACataclysmProjectile` stopped, dealt its damage
 * and destroyed itself, so a thrown rock was gone from one frame to the next.
 * Nothing in the project had an impact effect of any kind.
 *
 * THE TRAP THAT IS NOT OBVIOUS. Measured 2026-08-08: all five
 * `SM_Rampage_Rock_Frag` meshes in the Paragon pack have
 * `/Engine/EngineMaterials/WorldGridMaterial` assigned -- the engine's grey
 * checkerboard placeholder. Spawning them as they come would put five large
 * checkered lumps on the floor, which is worse than the rock vanishing. The
 * material has to be supplied by whoever scatters them, and the test below is
 * what stops that being forgotten.
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
	// gitignored, so the Brute's fragment array is empty and every entry a
	// caller passes may be null. An empty actor sitting in the world for two
	// seconds is something to clean up rather than an effect.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheBrutesRockBreaksWhereItStops,
	"Cataclysm.Brute.ItsThrownRockLeavesDebrisWhereItStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheBrutesRockBreaksWhereItStops::RunTest(const FString&)
{
	using namespace CataclysmDebrisTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Brute))
	{
		return false;
	}
	Brute->ResolveBody(/*bIncludeAnimation=*/false);

	if (Brute->RockFragments.IsEmpty())
	{
		AddInfo(TEXT("The Paragon Rampage pack is not installed, so there are no "
					 "fragments and the throw leaves nothing behind. That is the "
					 "expected state on a fresh clone."));
	}
	else
	{
		TestEqual(TEXT("the Brute resolved all five fragments"),
			Brute->RockFragments.Num(),
			ACataclysmBruteCharacter::RockFragmentCount);

		// THE MATERIAL COMES FROM THE ROCK, NOT FROM THE FRAGMENTS, because the
		// fragments carry the engine's checkerboard placeholder.
		if (TestNotNull(TEXT("and a material to dress them in"),
			Brute->RockMaterial.Get()))
		{
			TestTrue(TEXT("which is not the engine's placeholder"),
				Brute->RockMaterial.Get() != CheckerboardPlaceholder());
		}
	}

	TestEqual(TEXT("nothing is broken before the throw"), CountBursts(World), 0);

	// Throw it, then run the projectile to its end so OnFinished fires.
	Brute->UseEnemyAbility(ACataclysmBruteCharacter::RockThrowAbility,
						   /*Target=*/nullptr, FVector(5.0f * M, 0.0f, 0.0f));

	ACataclysmProjectile* Thrown = nullptr;
	for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			Thrown = *It;
			break;
		}
	}
	if (!TestNotNull(TEXT("the throw put a projectile in the world"), Thrown))
	{
		return false;
	}

	// Stepped by hand: a world made by UWorld::CreateWorld is never ticked, so
	// the projectile is driven the way every other test in this project drives
	// one.
	for (int32 Step = 0; Step < 200 && Thrown->Step(0.05f); ++Step)
	{
	}

	if (Brute->RockFragments.IsEmpty())
	{
		TestEqual(TEXT("without the pack the throw still leaves nothing"),
			CountBursts(World), 0);
		return true;
	}

	TestEqual(TEXT("the rock left one burst of debris"), CountBursts(World), 1);

	ACataclysmDebrisBurst* Burst = nullptr;
	for (TActorIterator<ACataclysmDebrisBurst> It(World); It; ++It)
	{
		Burst = *It;
		break;
	}
	if (!TestNotNull(TEXT("the burst"), Burst))
	{
		return false;
	}

	TestEqual(TEXT("with all five pieces in it"),
		Burst->PiecesPlaced, ACataclysmBruteCharacter::RockFragmentCount);

	// WHERE IT GOT TO, NOT WHERE IT WAS AIMED. The two differ for a rock stopped
	// early by an enemy or a wall.
	TestEqual(TEXT("and it is where the rock actually stopped"),
		Burst->GetActorLocation(), Thrown->FurthestReached);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
