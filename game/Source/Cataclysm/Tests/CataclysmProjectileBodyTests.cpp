// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmProjectile.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "UObject/SoftObjectPath.h"

/**
 * Tests for what a projectile looks like in the air.
 *
 * WHAT THESE GUARD. Issue #404. Every projectile in the game flew a grey engine
 * sphere, including the Brute's thrown rock, while the Paragon pack ships the
 * actual rock. The mesh could not simply be swapped in: `ACataclysmProjectile` is
 * what all 398 rows of `game/Data/WeaponSkills.csv` fire through, so a rock in
 * its constructor would have armed every player fire bolt with one.
 *
 * THE TWO THINGS THAT CAN GO WRONG. A caster that passes nothing losing its
 * placeholder, which would make every player skill invisible again; and a mesh
 * arriving at the wrong size, because the engine's basic shapes occupy a 100
 * centimetre cube and an art-pack mesh does not, so a scale worked out for one is
 * meaningless for the other.
 *
 * ART-DEPENDENT ASSERTIONS SAY SO WHEN THEY SKIP. The Paragon packs are
 * gitignored, so on a fresh clone the rock is absent and the sphere is correct
 * behaviour rather than a failure.
 */

namespace CataclysmProjectileBodyTest
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

	/**
	 * The scale the old formula produced for a body of DefaultBodyRadiusCm.
	 *
	 * WHY IT IS WRITTEN DOWN. Sizing used to be
	 * `(BodyRadiusCm * 2) / BasicShapeSize`, which assumed the mesh occupied a
	 * 100 centimetre cube -- true of the engine's sphere and of nothing out of an
	 * art pack. It is now taken from the mesh's own bounds. That is only a safe
	 * rewrite if the sphere still comes out at the size it always did, so this is
	 * the number the old arithmetic gave and the test below insists on it.
	 */
	constexpr float SphereScaleUnderTheOldFormula = 0.8f;

	/** What a projectile is drawing, as a plain pointer.
	 *
	 * UStaticMeshComponent::GetStaticMesh returns a TObjectPtr in Unreal 5.8,
	 * which the automation test macros have no overload for. */
	static UStaticMesh* MeshOf(const ACataclysmProjectile* Projectile)
	{
		if (!Projectile || !Projectile->PlaceholderBody)
		{
			return nullptr;
		}
		return Projectile->PlaceholderBody->GetStaticMesh().Get();
	}

	/** How wide the projectile's drawn body actually is in the world, in
	 *  centimetres, measured across the ground. */
	static float DrawnHalfWidthCm(const ACataclysmProjectile* Projectile)
	{
		const UStaticMesh* Shown = MeshOf(Projectile);
		if (!Shown)
		{
			return 0.0f;
		}
		const FVector Extent = Shown->GetBounds().BoxExtent;
		const FVector Scale = Projectile->PlaceholderBody->GetRelativeScale3D();
		return FMath::Max(Extent.X * Scale.X, Extent.Y * Scale.Y);
	}

	/** The Brute's rock, or null when the Paragon pack is not installed. */
	static UStaticMesh* RockOrNull()
	{
		return Cast<UStaticMesh>(
			FSoftObjectPath(ACataclysmBruteCharacter::RockMeshPath).TryLoad());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmProjectileKeepsItsPlaceholder,
	"Cataclysm.Projectile.ACasterThatPassesNoMeshKeepsThePlaceholderSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileKeepsItsPlaceholder::RunTest(const FString&)
{
	using namespace CataclysmProjectileBodyTest;

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

	// Every player skill fires exactly like this: no mesh argument at all.
	ACataclysmProjectile* Bolt = ACataclysmProjectile::Fire(
		Caster, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
		/*InRadiusCm=*/150.0f, /*InSpeed=*/1200.0f, /*InPierce=*/0,
		/*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false);
	if (!TestNotNull(TEXT("a projectile is fired"), Bolt))
	{
		return false;
	}

	if (!TestNotNull(TEXT("and it has a body to draw"), Bolt->PlaceholderBody.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("with a mesh on it"), MeshOf(Bolt)))
	{
		return false;
	}

	// THE ENGINE SPHERE, BY NAME. A player fire bolt must not become a rock
	// because the Brute needed one.
	TestEqual(TEXT("which is the engine sphere"),
		MeshOf(Bolt)->GetName(), FString(TEXT("Sphere")));

	// AT EXACTLY THE SIZE THE OLD ARITHMETIC GAVE. Sizing is now taken from the
	// mesh's own bounds instead of assuming a 100 centimetre cube, and this is
	// the assertion that says the rewrite changed nothing for the sphere.
	TestEqual(TEXT("at the scale the previous formula produced"),
		static_cast<float>(Bolt->PlaceholderBody->GetRelativeScale3D().X),
		SphereScaleUnderTheOldFormula);

	// AND THAT SCALE MEANS THE RIGHT THING. What the player sees and what the
	// sweep uses have to be the same width.
	TestEqual(TEXT("so what is drawn is as wide as what the sweep uses"),
		DrawnHalfWidthCm(Bolt), Bolt->BodyRadiusCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmProjectileUsesAGivenMesh,
	"Cataclysm.Projectile.AGivenMeshIsFlownAndSizedToTheBodyRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileUsesAGivenMesh::RunTest(const FString&)
{
	using namespace CataclysmProjectileBodyTest;

	UStaticMesh* Rock = RockOrNull();
	if (!Rock)
	{
		AddInfo(TEXT("The Paragon Rampage pack is not installed, so there is no "
					 "rock to fly and this test has nothing to check. That is "
					 "the expected state on a fresh clone."));
		return true;
	}

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

	ACataclysmProjectile* Thrown = ACataclysmProjectile::Fire(
		Caster, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
		/*InRadiusCm=*/210.0f, /*InSpeed=*/1200.0f, /*InPierce=*/0,
		/*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false, Rock);
	if (!TestNotNull(TEXT("a projectile is fired"), Thrown))
	{
		return false;
	}

	TestTrue(TEXT("it flies the mesh it was given"), MeshOf(Thrown) == Rock);

	// THE ROCK IS NOT A 100 CENTIMETRE CUBE, which is the whole reason sizing
	// had to move to the mesh's own bounds. If it happened to be, this assertion
	// would still hold but would prove less, so the difference is asserted too.
	const float RockHalfWidth = FMath::Max(
		Rock->GetBounds().BoxExtent.X, Rock->GetBounds().BoxExtent.Y);
	TestTrue(FString::Printf(
		TEXT("and the rock's own half-width (%.1f cm) is not the engine "
			 "primitive's 50, so the bounds really are being read"),
		RockHalfWidth),
		FMath::Abs(RockHalfWidth - 50.0f) > 1.0f);

	TestEqual(TEXT("and it is drawn as wide as what the sweep uses"),
		DrawnHalfWidthCm(Thrown), Thrown->BodyRadiusCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteThrowsTheRock,
	"Cataclysm.Brute.ItsThrowFliesTheRockRatherThanASphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteThrowsTheRock::RunTest(const FString&)
{
	using namespace CataclysmProjectileBodyTest;

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

	// CALLED DIRECTLY RATHER THAN RELYING ON BeginPlay, for the reason
	// CataclysmBruteTests states on its own mesh test: whether BeginPlay
	// fires depends on how the world under test was built, and reading the
	// result afterwards cannot tell "the art failed to load" apart from
	// "BeginPlay never ran". This test spent a run finding that out again.
	//
	// WITHOUT THE ANIMATIONS, because the rock is resolved before that half
	// and loading two montages here would be work this test does not use.
	Brute->ResolveBody(/*bIncludeAnimation=*/false);

	UStaticMesh* Rock = RockOrNull();
	if (!Rock)
	{
		// WITHOUT THE PACK IT MUST STILL WORK, and that half is checked here
		// rather than skipped. A fresh clone throws a sphere and the throw is
		// otherwise unaffected.
		AddInfo(TEXT("The Paragon Rampage pack is not installed, so only the "
					 "no-art path is checked."));
		TestNull(TEXT("the Brute resolves no rock without the pack"),
			Brute->RockMesh.Get());
	}
	else
	{
		TestTrue(TEXT("the Brute resolved the pack's rock at BeginPlay"),
			Brute->RockMesh.Get() == Rock);
	}

	// Throw it. Index 1 is the rock throw; see the enumeration on the class.
	Brute->UseEnemyAbility(ACataclysmBruteCharacter::RockThrowAbility,
						   /*Target=*/nullptr, FVector(7.0f * M, 0.0f, 0.0f));

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
	if (!TestNotNull(TEXT("with something to draw"), MeshOf(Thrown)))
	{
		return false;
	}

	if (Rock)
	{
		TestTrue(TEXT("and what it flies is the rock"), MeshOf(Thrown) == Rock);
	}
	else
	{
		TestEqual(TEXT("and without the pack it falls back to the engine sphere"),
			MeshOf(Thrown)->GetName(), FString(TEXT("Sphere")));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
