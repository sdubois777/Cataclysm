// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmProjectile.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Components/SkeletalMeshComponent.h"
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

/**
 * A projectile nobody asked to arc must fly exactly as it always did.
 *
 * WHY THIS IS THE FIRST OF THESE TESTS. `ACataclysmProjectile` is fired by all
 * 398 rows of `game/Data/WeaponSkills.csv`. Issue #459 added an arc for the
 * Brute's rock, and the danger in that change is not that the arc is wrong: it
 * is that every player skill quietly becomes a mortar. A projectile given no
 * apex must keep the height it was fired at, for the whole flight.
 *
 * IT ALSO GUARDS THE DISTANCE BOOKKEEPING. The step loop now counts down the
 * range by the horizontal distance covered rather than the distance flown,
 * because a range is a distance across the ground. For a straight shot those are
 * the same number, and this is what says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmStraightProjectileIsUnchanged,
	"Cataclysm.Skills.AStraightProjectileStillTravelsItsWholeRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStraightProjectileIsUnchanged::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FURL URL;
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	// Fired from 300 cm up at a point on the floor 1000 cm away. A straight
	// shot ignores the height difference entirely and flies level, which is
	// what it has always done.
	const FVector From(0.0f, 0.0f, 300.0f);
	const FVector To(1000.0f, 0.0f, 0.0f);

	ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
		Caster, From, To, /*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false);
	if (!TestNotNull(TEXT("it fired"), Shot))
	{
		return false;
	}

	TestEqual(TEXT("nothing asked it to arc"), Shot->ApexHeightCm, 0.0f);

	double Highest = Shot->GetActorLocation().Z;
	for (int32 Step = 0; Step < 100 && Shot->Step(0.05f); ++Step)
	{
		Highest = FMath::Max(Highest, Shot->GetActorLocation().Z);
	}

	TestEqual(TEXT("it stayed at the height it was fired from"),
		Shot->GetActorLocation().Z, From.Z);
	TestEqual(TEXT("and never rose above it at any point"), Highest, From.Z);

	// AND IT COVERED THE WHOLE GROUND DISTANCE. Counting the flown distance
	// instead of the horizontal one would land a straight shot in the same
	// place, so this passes either way for a level shot -- which is exactly why
	// it is here: it says the change to that bookkeeping did no harm.
	const float Covered = FVector::Dist2D(From, Shot->GetActorLocation());
	TestTrue(FString::Printf(
		TEXT("and travelled its whole range (%.0f cm of 1000)"), Covered),
		FMath::Abs(Covered - 1000.0f) < 10.0f);

	return true;
}

/**
 * A projectile asked to arc rises, comes back down, and lands where it was
 * aimed.
 *
 * WHAT THIS GUARDS. Issue #459. The rock is thrown from the creature's hand,
 * well above the ground, at a target on the floor. The parabola is built as the
 * straight line between those two heights plus a bump, so it is possible to get
 * the two halves right separately and still have the rock finish its flight in
 * mid-air, or start it underground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmArcingProjectileLandsWhereAimed,
	"Cataclysm.Skills.AnArcingProjectileRisesAndLandsWhereItWasAimed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmArcingProjectileLandsWhereAimed::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FURL URL;
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	const FVector From(0.0f, 0.0f, 300.0f);
	const FVector To(1000.0f, 0.0f, 0.0f);
	const float Apex = 250.0f;

	ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
		Caster, From, To, /*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false, /*InBodyMesh=*/nullptr,
		Apex);
	if (!TestNotNull(TEXT("it fired"), Shot))
	{
		return false;
	}

	double Highest = Shot->GetActorLocation().Z;
	for (int32 Step = 0; Step < 100 && Shot->Step(0.05f); ++Step)
	{
		Highest = FMath::Max(Highest, Shot->GetActorLocation().Z);
	}

	// IT WENT UP. The midpoint of the straight line between 300 and 0 is 150,
	// and the apex adds 250 on top of that, so it should reach about 400 --
	// which is HIGHER than it was launched from. A rock that only ever fell
	// would satisfy "it landed correctly" without ever having been a lob.
	TestTrue(FString::Printf(
		TEXT("it rose above the hand it left (%.0f cm against %.0f)"),
		Highest, From.Z),
		Highest > From.Z);
	TestTrue(FString::Printf(
		TEXT("and reached about the apex it was given (%.0f cm, expected 400)"),
		Highest),
		FMath::Abs(Highest - 400.0f) < 30.0f);

	// AND IT CAME BACK DOWN TO WHERE IT WAS AIMED, in all three axes.
	const FVector Landed = Shot->GetActorLocation();
	TestTrue(FString::Printf(
		TEXT("and landed at the height it was aimed at (%.0f cm, expected 0)"),
		Landed.Z),
		FMath::Abs(Landed.Z - To.Z) < 20.0f);
	TestTrue(FString::Printf(
		TEXT("and at the place it was aimed at (%.0f cm away)"),
		FVector::Dist2D(Landed, To)),
		FVector::Dist2D(Landed, To) < 20.0f);

	return true;
}

/**
 * The Brute throws its rock from its hand, and the rock arcs.
 *
 * WHY THE TWO ARE ONE TEST. They are one change. Issue #454 asked for the rock
 * to leave the hand rather than the creature's waist; doing that alone would
 * have been worse than leaving it, because the projectile flew level, so a rock
 * launched from a hand above 250 cm would have sailed over the head of a player
 * whose own is about 192. The launch point is only correct with a trajectory
 * that comes back down.
 *
 * WITHOUT THE PARAGON PACK THERE IS NO SKELETON, so RockLaunchLocation falls
 * back to the capsule centre and the height half of this cannot be checked. The
 * test says which path it took rather than passing quietly either way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteLobsFromItsHand,
	"Cataclysm.Brute.ItLobsTheRockFromItsHandRatherThanItsWaist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteLobsFromItsHand::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FURL URL;
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Brute))
	{
		return false;
	}

	// THE ARC IS CHECKED WHETHER OR NOT THERE IS ART, because it is computed
	// from the distance thrown and not from the skeleton.
	const FVector LandsAt(1000.0f, 0.0f, 0.0f);
	const float Apex = Brute->RockThrowApexCmFor(LandsAt);
	const float Thrown = FVector::Dist2D(Brute->RockLaunchLocation(), LandsAt);

	TestTrue(FString::Printf(TEXT("the throw arcs at all (apex %.0f cm)"), Apex),
		Apex > 0.0f);
	TestTrue(FString::Printf(
		TEXT("and the apex is the designed fraction of the throw "
			 "(%.0f cm of %.0f)"), Apex, Thrown),
		FMath::Abs(Apex
			- Thrown * ACataclysmBruteCharacter::RockThrowApexFraction) < 1.0f);

	// THE LAUNCH POINT NEEDS THE SKELETON, so say which case ran.
	const USkeletalMeshComponent* Body = Brute->GetMesh();
	const bool bHasBone =
		Body && Body->DoesSocketExist(ACataclysmBruteCharacter::RockHoldBoneName);
	if (!bHasBone)
	{
		AddInfo(TEXT("No skeleton with a weapon_r bone, which is expected "
					 "without the Paragon Rampage pack. The launch height is "
					 "not checked; the arc above is."));
		return true;
	}

	const FVector Hand = Brute->RockLaunchLocation();
	TestTrue(FString::Printf(
		TEXT("the rock leaves the hand, above the capsule centre "
			 "(%.0f cm against %.0f)"),
		Hand.Z, Brute->GetActorLocation().Z),
		Hand.Z > Brute->GetActorLocation().Z);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
