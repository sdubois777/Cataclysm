// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMeshWidth.h"
#include "Tests/CataclysmTestWorld.h"
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
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
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
	// Begun by hand here rather than through the helper, because the world
	// was made a few lines above. Issue #654: World->BeginPlay() alone does
	// nothing without a game mode, which is what the helper settles.
	CataclysmTestWorld::BeginPlayIn(World);

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
	// Begun by hand here rather than through the helper, because the world
	// was made a few lines above. Issue #654: World->BeginPlay() alone does
	// nothing without a game mode, which is what the helper settles.
	CataclysmTestWorld::BeginPlayIn(World);

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	const FVector From(0.0f, 0.0f, 300.0f);
	const FVector To(1000.0f, 0.0f, 0.0f);
	const float FlightSeconds = 1.4f;

	ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
		Caster, From, To, /*InRadiusCm=*/50.0f, /*InSpeed=*/0.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false, /*InBodyMesh=*/nullptr,
		FlightSeconds);
	if (!TestNotNull(TEXT("it fired"), Shot))
	{
		return false;
	}

	// THE APEX IS DERIVED FROM THE FLIGHT TIME since issue #465, and the figure
	// is written out here rather than recomputed from the same constant the
	// code uses, because a guard built from the number it checks cannot fail.
	// 980 * 1.4 * 1.4 / 8 is 240.1 centimetres.
	TestTrue(FString::Printf(
		TEXT("the apex came out of the flight time (%.1f cm, expected 240.1)"),
		Shot->ApexHeightCm),
		FMath::Abs(Shot->ApexHeightCm - 240.1f) < 1.0f);

	double Highest = Shot->GetActorLocation().Z;
	for (int32 Step = 0; Step < 100 && Shot->Step(0.05f); ++Step)
	{
		Highest = FMath::Max(Highest, Shot->GetActorLocation().Z);
	}

	// IT WENT UP. The midpoint of the straight line between 300 and 0 is 150,
	// and the apex adds 240 on top of that, so it should reach about 390 --
	// which is HIGHER than it was launched from. A rock that only ever fell
	// would satisfy "it landed correctly" without ever having been a lob.
	TestTrue(FString::Printf(
		TEXT("it rose above the hand it left (%.0f cm against %.0f)"),
		Highest, From.Z),
		Highest > From.Z);
	TestTrue(FString::Printf(
		TEXT("and reached about the apex the flight time implies "
			 "(%.0f cm, expected 390)"), Highest),
		FMath::Abs(Highest - 390.0f) < 30.0f);

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

	// CALLED DIRECTLY RATHER THAN RELYING ON BeginPlay, without which there is
	// no skeletal mesh and therefore no weapon_r bone, so the launch half of
	// this test skipped itself. It did exactly that from the day it was written
	// until 2026-08-09, on a machine that has the Paragon pack installed.
	Brute->ResolveBody(/*bIncludeAnimation=*/false);

	// THE ARC IS CHECKED WHETHER OR NOT THERE IS ART, because it is a fraction
	// of the distance thrown rather than something read off the skeleton.
	const FVector LandsAt(1000.0f, 0.0f, 0.0f);
	const float Apex = Brute->RockThrowApexCmFor(LandsAt);
	const float ThrownCm =
		FVector::Dist2D(Brute->RockLaunchLocation(), LandsAt);

	TestTrue(FString::Printf(TEXT("the throw lobs at all (apex %.0f cm)"), Apex),
		Apex > 0.0f);

	// A QUARTER OF THE DISTANCE THROWN, which is the designed fraction.
	TestTrue(FString::Printf(
		TEXT("and the apex is the designed fraction of the throw "
			 "(%.0f cm of %.0f)"), Apex, ThrownCm),
		FMath::Abs(Apex - ThrownCm * 0.25f) < 1.0f);

	// AND IT SCALES WITH THE THROW, which is the whole of issue #474. Between
	// #465 and #474 the apex came out of a fixed flight time, so a two metre lob
	// and a ten metre lob both rose 240 cm and the short one was near-vertical.
	// Half the distance must now give half the arc.
	// THE TRUE MIDPOINT BETWEEN THE LAUNCH POINT AND THE LANDING POINT, so the
	// horizontal distance really is half. Stepping half of ThrownCm along X
	// instead is only half the distance when the launch point happens to sit on
	// the same line, and it does not: RockLaunchLocation falls back to the
	// capsule centre here, which the spawn has nudged off the origin.
	const FVector HalfAsFar =
		Brute->RockLaunchLocation() + (LandsAt - Brute->RockLaunchLocation()) * 0.5f;
	const float ShorterApex = Brute->RockThrowApexCmFor(HalfAsFar);
	TestTrue(FString::Printf(
		TEXT("and half the throw gives half the arc (%.0f cm against %.0f)"),
		ShorterApex, Apex),
		FMath::Abs(ShorterApex - Apex * 0.5f) < 2.0f);

	// AND THE FLIGHT TIME FOLLOWS THE ARC RATHER THAN BEING STATED. A parabola
	// sags g * t * t / 8 below its chord, so a 250 cm arc over a 1000 cm throw
	// is sqrt(8 * 250 / 980) = 1.43 seconds in the air.
	const float FlightSeconds = Brute->RockThrowFlightSecondsFor(LandsAt);
	TestTrue(FString::Printf(
		TEXT("and the ten metre throw is in the air about 1.43 s (%.2f)"),
		FlightSeconds),
		FMath::Abs(FlightSeconds - 1.43f) < 0.05f);
	TestTrue(FString::Printf(
		TEXT("while a throw half as far is quicker (%.2f s against %.2f)"),
		Brute->RockThrowFlightSecondsFor(HalfAsFar), FlightSeconds),
		Brute->RockThrowFlightSecondsFor(HalfAsFar) < FlightSeconds);

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

	// THE HAND BONE, WHEREVER THE POSE HAS PUT IT. An earlier version of this
	// asserted the launch point was ABOVE the capsule centre, on the reasoning
	// that the throw animation raises the arm overhead. That is true during the
	// throw and false in the rest pose, where the arm hangs at the creature's
	// side: measured 2026-08-09, weapon_r sits 9 cm BELOW the capsule centre
	// with no animation playing. The claim worth making is pose-independent --
	// the rock leaves the hand rather than the middle of the creature.
	const FVector Hand = Brute->RockLaunchLocation();
	const FVector Bone = Body->GetSocketLocation(
		ACataclysmBruteCharacter::RockHoldBoneName);

	TestTrue(FString::Printf(
		TEXT("the rock leaves the hand bone (%.1f cm from it)"),
		FVector::Dist(Hand, Bone)),
		FVector::Dist(Hand, Bone) < 1.0f);

	// AND IT IS NOT THE CAPSULE CENTRE, which is where it left from before
	// issue #454 and what this whole change was about. Without this the
	// assertion above would pass on a fallback that quietly returned the actor
	// location, since that is what RockLaunchLocation does with no skeleton.
	TestTrue(FString::Printf(
		TEXT("and not the middle of the creature (%.1f cm from it)"),
		FVector::Dist(Hand, Brute->GetActorLocation())),
		FVector::Dist(Hand, Brute->GetActorLocation()) > 10.0f);

	return true;
}

/**
 * A lobbed projectile must be in the air for the time it was given.
 *
 * WHAT THIS GUARDS, AND WHAT IT REPLACED. Issue #465. Until then a lob was
 * given a speed and this test measured it, because issue #462 had shipped a 41%
 * speed defect that every #459 test missed by checking only WHERE the rock went.
 * #465 changed what a lob is given: a flight time, from which the ground speed
 * and the arc height both follow. The measurement it was worth making is the
 * same measurement, of the quantity that is now designed.
 *
 * WHY THE TIME IS THE THING WORTH GUARDING. The marker appears when the wind-up
 * starts and the rock then has to travel, so the player's window to move is the
 * telegraph plus this. A lob that quietly took half as long would give back half
 * the warning, and the rock would still land exactly on the circle, so nothing
 * about where it went would look wrong.
 *
 * MEASURED WITH A STOPWATCH, not by reading a field. The steps are counted until
 * the flight ends.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmLobbedProjectileTakesTheTimeItWasGiven,
	"Cataclysm.Skills.ALobbedProjectileIsInTheAirForTheTimeItWasGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobbedProjectileTakesTheTimeItWasGiven::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	// Begun by hand here rather than through the helper, because the world
	// was made a few lines above. Issue #654: World->BeginPlay() alone does
	// nothing without a game mode, which is what the helper settles.
	CataclysmTestWorld::BeginPlayIn(World);

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	// TWO THROWS OF VERY DIFFERENT LENGTHS, because "the same time whatever the
	// range" is the actual claim and one throw cannot show it. A model that
	// derived the time from a speed -- which is what this class did before
	// issue #465 -- would give these two wildly different flights while still
	// landing both of them exactly on target.
	struct FCase
	{
		float RangeCm;
		const TCHAR* What;
	};
	const FCase Cases[] =
	{
		{200.0f, TEXT("a two metre lob")},
		{1000.0f, TEXT("a ten metre lob")},
	};

	const float FlightSeconds = 1.4f;
	const float StepSeconds = 0.01f;

	for (const FCase& Case : Cases)
	{
		const FVector From(0.0f, 0.0f, 300.0f);
		const FVector To(Case.RangeCm, 0.0f, 0.0f);

		ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
			Caster, From, To, /*InRadiusCm=*/50.0f, /*InSpeed=*/0.0f,
			/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
			FGameplayTagContainer(), /*bInBurns=*/false, /*InBodyMesh=*/nullptr,
			FlightSeconds);
		if (!TestNotNull(TEXT("it fired"), Shot))
		{
			return false;
		}

		float Elapsed = 0.0f;
		for (int32 Step = 0; Step < 1000 && Shot->Step(StepSeconds); ++Step)
		{
			Elapsed += StepSeconds;
		}
		// The step that finished the flight returns false, so its time is added
		// here rather than being left out of the total.
		Elapsed += StepSeconds;

		AddInfo(FString::Printf(
			TEXT("%s was in the air %.2f s against a designed %.2f."),
			Case.What, Elapsed, FlightSeconds));

		// WITHIN A TWENTIETH OF A SECOND. The last step is cut short when the
		// projectile arrives, so a stopwatch over a whole flight cannot be
		// exact; at a hundredth of a second per step the slack is at most one
		// step. That is far tighter than the difference this exists to catch,
		// which for these two ranges under a speed-driven model would be 1.3
		// seconds.
		TestTrue(FString::Printf(
			TEXT("%s took the time it was given (%.2f s against %.2f)"),
			Case.What, Elapsed, FlightSeconds),
			FMath::Abs(Elapsed - FlightSeconds) < 0.05f);

		// AND IT REALLY ARRIVED, so a flight cut short by a bad arrival test
		// cannot pass this by never having gone anywhere.
		TestTrue(FString::Printf(
			TEXT("%s reached where it was aimed (%.0f cm away)"),
			Case.What, FVector::Dist(Shot->GetActorLocation(), To)),
			FVector::Dist(Shot->GetActorLocation(), To) < 20.0f);
	}

	return true;
}

/**
 * A lobbed projectile covers ground at a steady rate and falls faster and
 * faster.
 *
 * WHAT THIS GUARDS. Issue #465, and it is the property NEITHER of the two tests
 * before it checked. #459's tests checked WHERE the rock went. #462's test
 * checked HOW FAST it went overall. Both passed while the speed was distributed
 * along the flight in a way no thrown object has ever moved: the step held the
 * speed THROUGH THE AIR constant, so a steep part of the path bought less ground
 * than a shallow one. Over a ten metre throw from a hand to the floor the
 * horizontal speed was 80% of the designed figure at launch, 97% halfway and 62%
 * at the landing, and over a two metre throw, 97% and 41%. The rock crossed most
 * of the ground early and then sank slowly onto the marker, which is what the
 * project owner reported.
 *
 * THE TWO HALVES OF WHAT MAKES A TRAJECTORY BALLISTIC. Gravity acts downward and
 * nothing acts sideways, so the horizontal speed never changes and the vertical
 * one changes at a constant rate. Both are measured here, from positions, over
 * three windows of the same flight.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmLobbedProjectileHoldsItsGroundSpeed,
	"Cataclysm.Skills.ALobbedProjectileHoldsItsGroundSpeedAndAcceleratesDownward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobbedProjectileHoldsItsGroundSpeed::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	// Begun by hand here rather than through the helper, because the world
	// was made a few lines above. Issue #654: World->BeginPlay() alone does
	// nothing without a game mode, which is what the helper settles.
	CataclysmTestWorld::BeginPlayIn(World);

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	// FROM A HAND TO THE FLOOR, which is the case that produced the defect. A
	// lob whose two ends are at the same height is symmetric and hides it: the
	// old model's error came from the chord being tilted, so launching and
	// landing at the same height would have made the launch and landing slopes
	// equal and opposite and the two speeds equal.
	const FVector From(0.0f, 0.0f, 300.0f);
	const FVector To(1000.0f, 0.0f, 0.0f);
	const float FlightSeconds = 1.4f;
	const float StepSeconds = 0.01f;

	ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
		Caster, From, To, /*InRadiusCm=*/50.0f, /*InSpeed=*/0.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false, /*InBodyMesh=*/nullptr,
		FlightSeconds);
	if (!TestNotNull(TEXT("it fired"), Shot))
	{
		return false;
	}

	// Every position it occupied, so any window of the flight can be measured
	// afterwards rather than deciding in advance which one matters.
	TArray<FVector> Track;
	Track.Add(Shot->GetActorLocation());
	for (int32 Step = 0; Step < 1000 && Shot->Step(StepSeconds); ++Step)
	{
		Track.Add(Shot->GetActorLocation());
	}
	Track.Add(Shot->GetActorLocation());

	if (!TestTrue(FString::Printf(
			TEXT("it flew for enough steps to measure (%d)"), Track.Num()),
			Track.Num() > 30))
	{
		return false;
	}

	// A tenth of the flight at each end and a tenth across the middle. Windows
	// rather than single steps, so one short final step cannot decide the
	// answer.
	const int32 Window = FMath::Max(2, Track.Num() / 10);
	const int32 Middle = Track.Num() / 2;

	auto GroundSpeedOver = [&Track, StepSeconds](int32 First, int32 Last)
	{
		return FVector::Dist2D(Track[First], Track[Last])
			/ (static_cast<float>(Last - First) * StepSeconds);
	};
	auto FallSpeedOver = [&Track, StepSeconds](int32 First, int32 Last)
	{
		return static_cast<float>(Track[First].Z - Track[Last].Z)
			/ (static_cast<float>(Last - First) * StepSeconds);
	};

	// Stops one step short of the end, because the step that arrives is cut off
	// part way through and covers less ground in the time it is charged for.
	const int32 LandingWindowFirst = Track.Num() - 2 - Window;
	const int32 LandingWindowLast = Track.Num() - 2;

	const float GroundAtLaunch = GroundSpeedOver(0, Window);
	const float GroundInMiddle =
		GroundSpeedOver(Middle - Window / 2, Middle + Window / 2);
	const float GroundAtLanding =
		GroundSpeedOver(LandingWindowFirst, LandingWindowLast);

	AddInfo(FString::Printf(
		TEXT("Ground speed launch/middle/landing: %.0f / %.0f / %.0f cm/s. "
			 "The designed figure is %.0f."),
		GroundAtLaunch, GroundInMiddle, GroundAtLanding,
		Shot->SpeedCmPerSecond));

	// WITHIN A TWENTIETH OF EACH OTHER. The old model was out by 18% at launch
	// and 38% at the landing over exactly this throw, so five percent separates
	// a correct flight from that one without being so tight that the step
	// boundaries decide it.
	const float Tolerance = Shot->SpeedCmPerSecond * 0.05f;
	TestTrue(FString::Printf(
		TEXT("it left the hand at its ground speed (%.0f against %.0f)"),
		GroundAtLaunch, Shot->SpeedCmPerSecond),
		FMath::Abs(GroundAtLaunch - Shot->SpeedCmPerSecond) < Tolerance);
	TestTrue(FString::Printf(
		TEXT("and still had it halfway (%.0f against %.0f)"),
		GroundInMiddle, Shot->SpeedCmPerSecond),
		FMath::Abs(GroundInMiddle - Shot->SpeedCmPerSecond) < Tolerance);
	TestTrue(FString::Printf(
		TEXT("and still had it as it landed (%.0f against %.0f)"),
		GroundAtLanding, Shot->SpeedCmPerSecond),
		FMath::Abs(GroundAtLanding - Shot->SpeedCmPerSecond) < Tolerance);

	// AND THE DESCENT SPEEDS UP, which is the other half of projectile motion
	// and the half the player actually sees. The old model did the opposite: it
	// arrived slowly. Measured as a fall rate, so climbing is negative.
	const float FallAtLaunch = FallSpeedOver(0, Window);
	const float FallAtLanding =
		FallSpeedOver(LandingWindowFirst, LandingWindowLast);

	AddInfo(FString::Printf(
		TEXT("Fall rate launch/landing: %.0f / %.0f cm/s."),
		FallAtLaunch, FallAtLanding));

	TestTrue(FString::Printf(
		TEXT("it was still climbing when it left the hand (%.0f cm/s of fall)"),
		FallAtLaunch),
		FallAtLaunch < 0.0f);
	TestTrue(FString::Printf(
		TEXT("and was falling faster at the end than at any point before "
			 "(%.0f cm/s against %.0f)"), FallAtLanding, FallAtLaunch),
		FallAtLanding > FallAtLaunch);

	// AT THE RATE GRAVITY PULLS. Over the whole flight the fall rate goes from
	// its launch value to its landing value, and that change divided by the
	// time it took is the acceleration. 980 centimetres per second squared is
	// what Unreal's own DefaultGravityZ is, and it is written out here rather
	// than read off the class, because a guard built from the number it checks
	// cannot fail.
	//
	// BETWEEN THE MIDDLES OF THE TWO WINDOWS, not between their edges. An
	// average rate over a window is the instantaneous rate at the middle of it,
	// so measuring the span any other way biases the answer by half a window.
	const float Span = (0.5f * static_cast<float>(
			LandingWindowFirst + LandingWindowLast) - 0.5f
		* static_cast<float>(Window)) * StepSeconds;
	const float Measured = (FallAtLanding - FallAtLaunch) / Span;
	AddInfo(FString::Printf(
		TEXT("Measured downward acceleration %.0f cm/s^2 over %.2f s."),
		Measured, Span));
	TestTrue(FString::Printf(
		TEXT("and it fell at gravity (%.0f cm/s^2, expected about 980)"),
		Measured),
		FMath::Abs(Measured - 980.0f) < 120.0f);

	return true;
}

/**
 * The rock in the creature's hand is the same size as the one it throws.
 *
 * WHAT THIS GUARDS. Issue #453. The project owner reported the rock as huge.
 * Measured with tools/measure_rock_sizes.py on 2026-08-09, SM_Rock_To_Hold is
 * authored 206.6 x 180.9 x 512.2 cm. Nothing scaled the component in the
 * creature's hand, so it drew at that authored size -- more than twice the width
 * of the whole Brute, whose capsule is 96 cm across -- while the same asset in
 * the air was scaled to 80 cm across by ACataclysmProjectile::SetBodyMesh.
 *
 * A comment in CataclysmBruteCharacter.cpp said the two "cannot become two
 * different rocks". That was true of the ASSET and said nothing about the SIZE,
 * which is exactly the kind of claim worth turning into a test.
 *
 * IT NEEDS THE ART. The Paragon packs are gitignored, so on a fresh clone there
 * is no rock and nothing to measure. The test says which case it ran rather than
 * passing quietly either way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmHeldRockMatchesThrownRock,
	"Cataclysm.Brute.TheRockInItsHandIsTheSizeOfTheRockItThrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldRockMatchesThrownRock::RunTest(const FString&)
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

	// CALLED DIRECTLY RATHER THAN RELYING ON BeginPlay. This is the trap
	// Cataclysm.Brute.ItsThrowFliesTheRockRatherThanASphere already records
	// having spent a run on: whether BeginPlay fires depends on how the world
	// under test was built, and reading the result afterwards cannot tell "the
	// art is missing" apart from "BeginPlay never ran". On its first run this
	// test reported no rock on a machine that has the pack installed, and passed
	// having compared nothing.
	Brute->ResolveBody(/*bIncludeAnimation=*/false);

	UStaticMeshComponent* Held = Brute->CarriedRock;
	if (!TestNotNull(TEXT("it has a component for the rock it carries"), Held))
	{
		return false;
	}

	const UStaticMesh* Rock = RockOrNull();
	if (!Rock)
	{
		AddInfo(TEXT("The Paragon Rampage pack is not installed, so there is no "
					 "rock to measure and the size is not checked."));
		TestNull(TEXT("and the Brute resolved no rock without the pack"),
			Brute->RockMesh.Get());
		return true;
	}

	if (!TestTrue(TEXT("the carried component is showing the pack's rock"),
		Held->GetStaticMesh() == Rock))
	{
		return false;
	}

	// WHAT THE THROWN ONE WILL BE. ACataclysmProjectile::Fire gives a projectile
	// that does not pierce its DefaultBodyRadiusCm, and the rock does not
	// pierce, so this is the half-width it flies at.
	const float ThrownRadiusCm = ACataclysmProjectile::DefaultBodyRadiusCm;

	// WHAT THE HELD ONE IS. The component's scale applied to the mesh's own
	// authored half-width, which is what the player sees.
	const FVector Extent = Rock->GetBounds().BoxExtent;
	const float AuthoredHalfWidth = FMath::Max(Extent.X, Extent.Y);
	const float HeldRadiusCm =
		AuthoredHalfWidth * Held->GetRelativeScale3D().X;

	AddInfo(FString::Printf(
		TEXT("The rock is authored %.1f cm across. In the hand it draws %.1f cm "
			 "across; in the air it draws %.1f."),
		AuthoredHalfWidth * 2.0f, HeldRadiusCm * 2.0f, ThrownRadiusCm * 2.0f));

	TestTrue(FString::Printf(
		TEXT("the held rock is the size of the thrown one (%.1f cm against "
			 "%.1f)"), HeldRadiusCm * 2.0f, ThrownRadiusCm * 2.0f),
		FMath::Abs(HeldRadiusCm - ThrownRadiusCm) < 1.0f);

	// AND IT IS SMALLER THAN THE CREATURE HOLDING IT, which is the assertion
	// that would have caught the original defect on its own. A rock wider than
	// the whole Brute is wrong whatever the thrown one happens to be doing.
	TestTrue(FString::Printf(
		TEXT("and it is narrower than the creature (%.1f cm against %.1f)"),
		HeldRadiusCm * 2.0f, ACataclysmBruteCharacter::BruteCapsuleRadius * 2.0f),
		HeldRadiusCm < ACataclysmBruteCharacter::BruteCapsuleRadius);

	return true;
}

/**
 * The shared mesh-width helper refuses rather than dividing by nothing.
 *
 * Every caller applies what it returns as a uniform scale, so an answer of zero
 * has to mean "leave it alone" and be returned for each way there is nothing to
 * scale. Applying a zero would collapse the mesh instead of leaving it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmMeshWidthRefusesNothing,
	"Cataclysm.Skills.TheMeshWidthHelperAnswersZeroWhenThereIsNothingToScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMeshWidthRefusesNothing::RunTest(const FString&)
{
	TestEqual(TEXT("a null mesh scales to nothing"),
		CataclysmMeshWidth::ScaleFor(nullptr, 40.0f), 0.0f);

	UStaticMesh* Sphere = Cast<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")).TryLoad());
	if (!Sphere)
	{
		AddError(TEXT("The engine's own sphere could not be loaded."));
		return false;
	}

	TestEqual(TEXT("a radius of zero scales to nothing"),
		CataclysmMeshWidth::ScaleFor(Sphere, 0.0f), 0.0f);
	TestEqual(TEXT("and so does a negative one"),
		CataclysmMeshWidth::ScaleFor(Sphere, -10.0f), 0.0f);

	// THE ENGINE SPHERE IS 100 CM ACROSS, so a 40 cm radius is a scale of 0.8.
	// A real answer as well as the refusals, or this test would pass on a helper
	// that answered zero to everything.
	TestTrue(TEXT("and a real mesh with a real radius scales to something"),
		FMath::Abs(CataclysmMeshWidth::ScaleFor(Sphere, 40.0f) - 0.8f) < 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
