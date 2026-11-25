// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDebrisBurst.h"
#include "AbilitySystem/CataclysmEffectPlacement.h"
#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/SoftObjectPath.h"

/**
 * Effects drawn on the ground are drawn on the ground. Issue #2863.
 *
 * THE OWNER, PLAYTESTING ON 2026-11-23: "There seems to be skill fx going off
 * under the floor, I only noticed because I got close to an edge and saw
 * something." Every effect that is drawn on the ground -- a ground zone's ring,
 * a telegraph marker, the debris an attack leaves -- went exactly where its
 * caller pointed, and nothing asked where the floor was. The callers point at a
 * capsule centre, a creature's capsule bottom under a target's X and Y, or a
 * cell's walking surface; `docs/DECISIONS.md`, 2026-11-24, lists every one.
 *
 * WHAT THESE BUILD, AND WHY. A test world made by `UWorld::CreateWorld` holds no
 * geometry, so every other test of these effects runs over nothing at all and
 * could not see the fault. These lay a solid floor first, built as the dungeon's
 * ground is -- world-static and blocking -- with its top at 500 cm rather than at
 * the origin, so nothing that assumes the floor is at zero can pass by accident.
 *
 * WHAT THEY CANNOT SEE. A Niagara component is never created under `-nullrhi`, so
 * a ground zone's drawing is read through `UCataclysmGroundEffect::LastDrawnStart`
 * and `LastDrawnFarEnd`, which are recorded from the same places the spawns use.
 * A telegraph marker and a debris burst are actors, and their own location is
 * read.
 */

namespace CataclysmEffectPlacementTest
{
	/** The walking surface's height in these tests. Not zero; see the file comment. */
	constexpr float SurfaceZ = 500.0f;

	/** Where a ground drawing goes on that floor: just above it. */
	constexpr float DrawnZ = SurfaceZ + CataclysmEffectPlacement::LiftCm;

	/** As high as a player's capsule centre stands over the floor. */
	constexpr float CapsuleCentreZ = SurfaceZ + 96.0f;

	/** Inside and under the floor, as deep as the exit stairs' top tier is high. */
	constexpr float UnderTheSurfaceZ = SurfaceZ - 60.0f;

	/** A place with no floor under it: the floor below is 40 metres square. */
	constexpr float PastTheEdgeX = 5000.0f;

	/**
	 * A solid floor 40 metres square and 40 cm thick, the dungeon's ground block's
	 * thickness, with its top at `SurfaceZ`.
	 *
	 * BUILT AS THE ABYSSAL WARDEN'S TESTS BUILD THEIR SLOPES: a box component set to
	 * the world-static object type and blocking everything, because the search for
	 * the floor asks for world-static objects and the dungeon's ground is one.
	 */
	AActor* LayTheEffectPlacementFloor(UWorld* World)
	{
		AActor* Floor = World->SpawnActor<AActor>(
			FVector(0.0f, 0.0f, SurfaceZ - 20.0f), FRotator::ZeroRotator);
		if (!Floor)
		{
			return nullptr;
		}

		UBoxComponent* Body = NewObject<UBoxComponent>(Floor);
		Body->InitBoxExtent(FVector(2000.0f, 2000.0f, 20.0f));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetCollisionObjectType(ECC_WorldStatic);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Floor->SetRootComponent(Body);
		Body->RegisterComponent();
		Floor->SetActorLocationAndRotation(FVector(0.0f, 0.0f, SurfaceZ - 20.0f),
										   FRotator::ZeroRotator);
		return Floor;
	}

	/** A world that has begun play, with that floor in it, or null. */
	UWorld* MakeTheEffectPlacementWorld()
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (World && !LayTheEffectPlacementFloor(World))
		{
			World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			return nullptr;
		}
		return World;
	}
}

#define CATACLYSM_PLACEMENT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_PLACEMENT_TEST(FCataclysmEffectPlacementOnTheFloorTest,
	"Cataclysm.EffectPlacement.APointIsPutOnTheFloorUnderItFromAboveOrBelow")
{
	using namespace CataclysmEffectPlacementTest;

	UWorld* World = MakeTheEffectPlacementWorld();
	if (!TestNotNull(TEXT("set-up: a world with a floor in it"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE FLOOR IS WHERE THE TEST BUILT IT, measured by the search itself, so the
	// figures below are about the search and not about the box.
	float Surface = 0.0f;
	if (!TestTrue(TEXT("set-up: the search finds the floor under its middle"),
				  CataclysmEffectPlacement::FloorSurfaceUnder(
					  World, FVector(0.0f, 0.0f, CapsuleCentreZ), Surface)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: at the height the test built it"), Surface, SurfaceZ, 0.01f);

	// FROM ABOVE: a capsule centre, where most ground zones are placed.
	const FVector FromAbove =
		CataclysmEffectPlacement::OnTheFloor(World, FVector(100.0f, 200.0f, CapsuleCentreZ));
	TestEqual(TEXT("a point at a capsule centre is brought down to just above the floor"),
			  static_cast<float>(FromAbove.Z), DrawnZ, 0.01f);
	TestEqual(TEXT("and only its height moves: X"), static_cast<float>(FromAbove.X), 100.0f, 0.001f);
	TestEqual(TEXT("and only its height moves: Y"), static_cast<float>(FromAbove.Y), 200.0f, 0.001f);

	// FROM BELOW: a point under the surface, the case the owner saw.
	const FVector FromBelow =
		CataclysmEffectPlacement::OnTheFloor(World, FVector(0.0f, 0.0f, UnderTheSurfaceZ));
	TestEqual(TEXT("a point 60 cm under the surface is lifted to just above it"),
			  static_cast<float>(FromBelow.Z), DrawnZ, 0.01f);

	// AND THE THREE PLACES IT LEAVES ALONE. No floor under it: out past the edge.
	const FVector PastTheEdge(PastTheEdgeX, 0.0f, CapsuleCentreZ);
	TestEqual(TEXT("a point with no floor under it is left where it was asked"),
			  CataclysmEffectPlacement::OnTheFloor(World, PastTheEdge), PastTheEdge);

	// FAR BELOW, out of the window: a body that fell far is not dragged back up.
	const FVector FarBelow(0.0f, 0.0f,
		SurfaceZ - CataclysmEffectPlacement::LookAboveCm - 500.0f);
	TestEqual(TEXT("a point further under the floor than the search looks is left where it was asked"),
			  CataclysmEffectPlacement::OnTheFloor(World, FarBelow), FarBelow);

	// AND NO WORLD.
	TestEqual(TEXT("with no world to search, a point is left where it was asked"),
			  CataclysmEffectPlacement::OnTheFloor(nullptr, FromAbove), FromAbove);
	return true;
}

CATACLYSM_PLACEMENT_TEST(FCataclysmEffectPlacementGroundZoneTest,
	"Cataclysm.EffectPlacement.AGroundZoneIsDrawnOnTheFloorUnderIt")
{
	using namespace CataclysmEffectPlacementTest;

	UWorld* World = MakeTheEffectPlacementWorld();
	if (!TestNotNull(TEXT("set-up: a world with a floor in it"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: something to own the zones"), Owner))
	{
		return false;
	}

	// A ROUND ZONE AT A CAPSULE CENTRE, where most zones in the game are placed.
	{
		const int32 Before = UCataclysmGroundEffect::TimesAsked;
		const FVector Asked(0.0f, 0.0f, CapsuleCentreZ);
		const ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
			Owner, Asked, /*RadiusCm=*/300.0f, /*Duration=*/5.0f, /*DamagePerTick=*/0.0f);
		if (!TestNotNull(TEXT("set-up: a zone at a capsule centre"), Zone)
			|| !TestEqual(TEXT("set-up: and it asked to be drawn"),
						  UCataclysmGroundEffect::TimesAsked, Before + 1))
		{
			return false;
		}
		TestEqual(TEXT("the zone itself stays where it was put"),
				  static_cast<float>(Zone->GetActorLocation().Z), CapsuleCentreZ, 0.01f);
		TestEqual(TEXT("and asked to be drawn there"),
				  static_cast<float>(UCataclysmGroundEffect::LastStart.Z), CapsuleCentreZ, 0.01f);
		TestEqual(TEXT("but its ring is drawn just above the floor, not at waist height"),
				  static_cast<float>(UCataclysmGroundEffect::LastDrawnStart.Z), DrawnZ, 0.01f);
	}

	// A ROUND ZONE UNDER THE SURFACE, the case the owner saw.
	{
		const int32 Before = UCataclysmGroundEffect::TimesAsked;
		ACataclysmGroundZone::Spawn(Owner, FVector(0.0f, 0.0f, UnderTheSurfaceZ), 300.0f, 5.0f, 0.0f);
		if (!TestEqual(TEXT("set-up: a zone under the surface asked to be drawn"),
					   UCataclysmGroundEffect::TimesAsked, Before + 1))
		{
			return false;
		}
		TestEqual(TEXT("a zone placed 60 cm under the surface has its ring drawn just above it"),
				  static_cast<float>(UCataclysmGroundEffect::LastDrawnStart.Z), DrawnZ, 0.01f);
	}

	// A LONG ZONE, ONE END UNDER THE SURFACE AND ONE ABOVE: every copy is put on
	// the floor under it, so both ends are.
	{
		const int32 Before = UCataclysmGroundEffect::TimesAsked;
		ACataclysmGroundZone::SpawnAlong(Owner, FVector(-600.0f, 0.0f, UnderTheSurfaceZ),
										 FVector(600.0f, 0.0f, CapsuleCentreZ),
										 /*HalfWidthCm=*/100.0f, 5.0f, 0.0f);
		if (!TestEqual(TEXT("set-up: a long zone asked to be drawn"),
					   UCataclysmGroundEffect::TimesAsked, Before + 1))
		{
			return false;
		}
		TestEqual(TEXT("a long zone's near end, under the surface, is drawn just above it"),
				  static_cast<float>(UCataclysmGroundEffect::LastDrawnStart.Z), DrawnZ, 0.01f);
		TestEqual(TEXT("and its far end, at a capsule centre, is drawn just above it too"),
				  static_cast<float>(UCataclysmGroundEffect::LastDrawnFarEnd.Z), DrawnZ, 0.01f);
		TestEqual(TEXT("and the far end is still the far end"),
				  static_cast<float>(UCataclysmGroundEffect::LastDrawnFarEnd.X), 600.0f, 0.01f);
	}

	// THE CONTROL: no floor under it, so it is drawn where it was asked.
	{
		const FVector Asked(PastTheEdgeX, 0.0f, CapsuleCentreZ);
		ACataclysmGroundZone::Spawn(Owner, Asked, 300.0f, 5.0f, 0.0f);
		TestEqual(TEXT("a zone with no floor under it is drawn where it was asked"),
				  UCataclysmGroundEffect::LastDrawnStart, Asked);
	}
	return true;
}

CATACLYSM_PLACEMENT_TEST(FCataclysmEffectPlacementTelegraphTest,
	"Cataclysm.EffectPlacement.ATelegraphMarkerIsDrawnOnTheFloorUnderItsCentre")
{
	using namespace CataclysmEffectPlacementTest;

	UWorld* World = MakeTheEffectPlacementWorld();
	if (!TestNotNull(TEXT("set-up: a world with a floor in it"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: something winding up an attack"), Caster))
	{
		return false;
	}

	// A CIRCLE CENTRED UNDER THE SURFACE: what `FloorUnder` hands over when the
	// target stands on the exit stairs and the creature does not.
	const ACataclysmTelegraphMarker* Circle = ACataclysmTelegraphMarker::ShowCircle(
		Caster, FVector(0.0f, 0.0f, UnderTheSurfaceZ), /*RadiusCm=*/200.0f, /*Seconds=*/2.0f);
	if (TestNotNull(TEXT("set-up: a circle is drawn"), Circle))
	{
		TestEqual(TEXT("a circle centred 60 cm under the surface is drawn just above it"),
				  static_cast<float>(Circle->GetActorLocation().Z), DrawnZ, 0.01f);
	}

	// A LANE WHOSE ENDS ARE AT A CAPSULE CENTRE: drawn on the floor, at its middle.
	const ACataclysmTelegraphMarker* Lane = ACataclysmTelegraphMarker::ShowLine(
		Caster, FVector(-500.0f, 0.0f, CapsuleCentreZ), FVector(500.0f, 0.0f, CapsuleCentreZ),
		/*HalfWidthCm=*/100.0f, /*Seconds=*/2.0f);
	if (TestNotNull(TEXT("set-up: a lane is drawn"), Lane))
	{
		TestEqual(TEXT("a lane at a capsule centre's height is drawn just above the floor"),
				  static_cast<float>(Lane->GetActorLocation().Z), DrawnZ, 0.01f);
		TestEqual(TEXT("and still at its middle"),
				  static_cast<float>(Lane->GetActorLocation().X), 0.0f, 0.01f);
	}

	// THE CONTROL: no floor under it, so it is drawn where it was asked.
	const FVector PastTheEdge(PastTheEdgeX, 0.0f, UnderTheSurfaceZ);
	const ACataclysmTelegraphMarker* Unplaced =
		ACataclysmTelegraphMarker::ShowCircle(Caster, PastTheEdge, 200.0f, 2.0f);
	if (TestNotNull(TEXT("set-up: a circle with no floor under it is drawn"), Unplaced))
	{
		TestEqual(TEXT("a circle with no floor under it is drawn where it was asked"),
				  static_cast<float>(Unplaced->GetActorLocation().Z), UnderTheSurfaceZ, 0.01f);
	}
	return true;
}

CATACLYSM_PLACEMENT_TEST(FCataclysmEffectPlacementDebrisTest,
	"Cataclysm.EffectPlacement.DebrisIsLeftOnTheFloorUnderTheImpact")
{
	using namespace CataclysmEffectPlacementTest;

	UWorld* World = MakeTheEffectPlacementWorld();
	if (!TestNotNull(TEXT("set-up: a world with a floor in it"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Thrower = World->SpawnActor<AActor>();
	UStaticMesh* Cube = Cast<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")).TryLoad());
	if (!TestNotNull(TEXT("set-up: something that broke the ground"), Thrower)
		|| !TestNotNull(TEXT("set-up: the engine's cube, to scatter"), Cube))
	{
		return false;
	}

	const ACataclysmDebrisBurst* Burst = ACataclysmDebrisBurst::Scatter(
		Thrower, FVector(0.0f, 0.0f, UnderTheSurfaceZ), {Cube}, /*Material=*/nullptr,
		/*SpreadCm=*/0.0f, /*PieceRadiusCm=*/50.0f, /*Seconds=*/2.0f);
	if (TestNotNull(TEXT("set-up: the debris is left"), Burst))
	{
		TestEqual(TEXT("debris left 60 cm under the surface is put just above it"),
				  static_cast<float>(Burst->GetActorLocation().Z), DrawnZ, 0.01f);
	}

	// THE CONTROL: no floor under it, so it is left where it was asked.
	const ACataclysmDebrisBurst* Unplaced = ACataclysmDebrisBurst::Scatter(
		Thrower, FVector(PastTheEdgeX, 0.0f, UnderTheSurfaceZ), {Cube}, nullptr, 0.0f, 50.0f, 2.0f);
	if (TestNotNull(TEXT("set-up: debris with no floor under it is left"), Unplaced))
	{
		TestEqual(TEXT("debris with no floor under it is left where it was asked"),
				  static_cast<float>(Unplaced->GetActorLocation().Z), UnderTheSurfaceZ, 0.01f);
	}
	return true;
}

#undef CATACLYSM_PLACEMENT_TEST

#endif // WITH_AUTOMATION_TESTS
