// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SphereComponent.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for finding what a skill hits.
 *
 * THE GEOMETRY IS TESTED SEPARATELY FROM THE WORLD QUERY, which is why
 * UCataclysmTargeting splits them. IsInCone and IsInLine take numbers, so every
 * edge of them can be pinned without spawning anything; the Find functions need
 * a world and real actors, so there are fewer of those and they check the parts
 * the arithmetic cannot: that only hostiles come back, that the nearest comes
 * first, and that a cap keeps the nearest rather than an arbitrary set.
 */

namespace CataclysmTargetingTest
{
	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}

	/** Metres, so the tests read like the design document does. */
	constexpr float M = 100.0f;

	/**
	 * An actor that a sphere overlap can actually find.
	 *
	 * A bare AActor has no collision, so OverlapMultiByObjectType returns nothing
	 * for it however close it is. The sphere is on the Pawn object channel
	 * because that is what UCataclysmTargeting::Gather queries -- everything it
	 * looks for is a character.
	 */
	struct FScopedTarget
	{
		FScopedTarget(UWorld* World, const FVector& Where, bool bWithAbilitySystem = true)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			if (bWithAbilitySystem)
			{
				AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
				AbilitySystem->RegisterComponent();

				UCataclysmVitalAttributeSet* Vitals =
					NewObject<UCataclysmVitalAttributeSet>(Actor);
				AbilitySystem->AddAttributeSetSubobject(Vitals);
				AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			}
		}

		~FScopedTarget()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

// --------------------------------------------------------------------------
// The cone
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConeShapeTest,
	"Cataclysm.Targeting.AConeCoversHalfItsWrittenAngleEitherSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConeShapeTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	const FVector Origin = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;

	// Molten Cleave's "wide cone" is written as 120 degrees, which the design
	// means as the full width. So 59 degrees off centre is inside and 61 is out.
	const float Radius = 4 * M;
	const float Angle = 120.0f;

	TestTrue(TEXT("Straight ahead is inside"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(2 * M, 0, 0), Radius, Angle));

	const FVector At59 = FVector(FMath::Cos(FMath::DegreesToRadians(59.0f)),
								 FMath::Sin(FMath::DegreesToRadians(59.0f)), 0) * 2 * M;
	TestTrue(TEXT("59 degrees off centre is inside a 120 degree cone"),
		UCataclysmTargeting::IsInCone(Origin, Forward, At59, Radius, Angle));

	const FVector At61 = FVector(FMath::Cos(FMath::DegreesToRadians(61.0f)),
								 FMath::Sin(FMath::DegreesToRadians(61.0f)), 0) * 2 * M;
	TestFalse(TEXT("61 degrees off centre is outside a 120 degree cone"),
		UCataclysmTargeting::IsInCone(Origin, Forward, At61, Radius, Angle));

	TestFalse(TEXT("Directly behind is outside"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(-2 * M, 0, 0), Radius, Angle));

	TestFalse(TEXT("Beyond the radius is outside even straight ahead"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(5 * M, 0, 0), Radius, Angle));

	// Pyroclasm spins, so its cone is a ring and nothing behind it escapes.
	TestTrue(TEXT("A 360 degree cone is a ring and covers behind"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(-2 * M, 0, 0), Radius, 360.0f));

	TestFalse(TEXT("A ring still respects its radius"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(-5 * M, 0, 0), Radius, 360.0f));

	// An enemy standing on the caster has no direction to compare against, and
	// the wrong answer here makes a point-blank enemy immune.
	TestTrue(TEXT("Standing on the caster is inside"),
		UCataclysmTargeting::IsInCone(Origin, Forward, Origin, Radius, Angle));

	// Height is ignored on purpose: a top-down game aims on the ground plane, so
	// an enemy up a step must not fall out of a cone.
	TestTrue(TEXT("Height is ignored"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(2 * M, 0, 3 * M), Radius, Angle));

	// A radius of zero is what a shape parameter nobody wrote reads as, and it
	// has to hit nothing rather than everything.
	TestFalse(TEXT("A radius of zero hits nothing"),
		UCataclysmTargeting::IsInCone(Origin, Forward,
			FVector(1, 0, 0), 0.0f, Angle));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConeBodyShapeTest,
	"Cataclysm.Targeting.ABodyOverlappingAConesSideIsInsideAndItsReachIsStillMeasuredToItsCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConeBodyShapeTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	// THE WHIP'S BASIC ATTACK, `Weapon_Whip` in game/Data/ItemBases.csv: Radius=3; Angle=45. Issue #2870. A creature's
	// body is a 48 cm capsule. The cone faces +X from the origin, so its edges are 22.5 degrees either side of X.
	const FVector Origin = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;
	const float Radius = 3 * M;
	const float Angle = 45.0f;
	const float Body = 48.0f;

	// THE CASE THE OWNER MET: a metre ahead and half a metre to the side, its centre 26.6 degrees off the aim.
	const FVector Beside(1 * M, 0.5f * M, 0);
	TestFalse(TEXT("control: its centre alone is outside a 45 degree cone"),
		UCataclysmTargeting::IsInCone(Origin, Forward, Beside, Radius, Angle));
	TestTrue(TEXT("its 48 cm body, 8 cm from the cone's edge, is inside"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, Beside, Body, Radius, Angle));
	TestTrue(TEXT("and on the other side too"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(1 * M, -0.5f * M, 0), Body, Radius, Angle));

	// A BODY OF NO SIZE IS THE CENTRE'S OWN TEST.
	TestFalse(TEXT("a body of no size is outside where its centre is"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, Beside, 0.0f, Radius, Angle));

	// THE EDGE STILL HOLDS: a body just clear of it is outside, one just touching it inside. The edge's own direction
	// and the direction square to it, on the +Y side.
	const float Half = FMath::DegreesToRadians(Angle * 0.5f);
	const FVector Edge(FMath::Cos(Half), FMath::Sin(Half), 0);
	const FVector Outward(-FMath::Sin(Half), FMath::Cos(Half), 0);
	TestTrue(TEXT("a body whose centre is 47 cm outside the edge, 2 m along it, touches it and is inside"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, Edge * 2 * M + Outward * 47.0f, Body, Radius, Angle));
	TestFalse(TEXT("one 49 cm outside it is not"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, Edge * 2 * M + Outward * 49.0f, Body, Radius, Angle));

	// THE REACH IS MEASURED TO THE CENTRE (P7-c): a body straight ahead whose centre is 20 cm past the radius, its near
	// side well within it, is outside.
	TestFalse(TEXT("a body straight ahead, its centre 20 cm beyond the 3 m reach, is outside"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(3.2f * M, 0, 0), Body, Radius, Angle));
	TestTrue(TEXT("and one 20 cm within it is inside"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(2.8f * M, 0, 0), Body, Radius, Angle));

	// A BODY THE APEX STANDS INSIDE IS INSIDE, from any side: pressed against the caster's back.
	TestTrue(TEXT("a body 30 cm behind the apex, which stands inside it, is inside"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(-0.3f * M, 0, 0), Body, Radius, Angle));
	TestFalse(TEXT("and one a metre behind is not"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(-1 * M, 0, 0), Body, Radius, Angle));

	// A WIDE CONE AND A RING ARE UNCHANGED WHERE THE CENTRE WAS ALREADY INSIDE, and a ring still has its radius.
	TestTrue(TEXT("a ring covers behind"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(-2 * M, 0, 0), Body, Radius, 360.0f));
	TestFalse(TEXT("a ring still respects its radius"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(-3.5f * M, 0, 0), Body, Radius, 360.0f));

	// A RADIUS OR AN ANGLE OF NOUGHT HITS NOTHING, a body or not.
	TestFalse(TEXT("a radius of nought hits nothing"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(0.2f * M, 0, 0), Body, 0.0f, Angle));
	TestFalse(TEXT("an angle of nought hits nothing"),
		UCataclysmTargeting::IsBodyInCone(Origin, Forward, FVector(1 * M, 0, 0), Body, Radius, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConeBodySearchTest,
	"Cataclysm.Targeting.AConeSearchFindsABodyBesideItsAimNearTheCasterAndStillTakesTheNearestInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConeBodySearchTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	// THE WHIP'S CONE AGAIN, searched in a world. Every target here is a 34 cm sphere on the pawn channel. Issue #2870.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedTarget Caster(World, FVector::ZeroVector);
	// A METRE AHEAD AND 45 CM TO THE SIDE: its centre 24.2 degrees off the aim, outside the 22.5, and its body 3 cm
	// from the cone's edge.
	FScopedTarget Beside(World, FVector(1 * M, 0.45f * M, 0));
	// AND ONE 2.5 M AHEAD, inside by its centre.
	FScopedTarget Ahead(World, FVector(2.5f * M, 0, 0));
	// AND ONE TO THE SIDE, ITS BODY WELL CLEAR OF THE EDGE: half a metre ahead and two metres across, 76 degrees off.
	FScopedTarget Clear(World, FVector(0.5f * M, -2 * M, 0));

	if (!TestFalse(TEXT("set-up: the target beside the aim is outside the cone by its centre"),
			UCataclysmTargeting::IsInCone(FVector::ZeroVector, FVector::ForwardVector,
										  Beside.Actor->GetActorLocation(), 3 * M, 45.0f))
		|| !TestEqual(TEXT("set-up: its body is its 34 cm sphere"),
			   UCataclysmTargeting::BodyRadiusOf(Beside.Actor), 34.0f, 0.01f))
	{
		return false;
	}

	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInCone(
		World, Caster.Actor, FVector::ZeroVector, FVector::ForwardVector, 3 * M, 45.0f);
	TestTrue(TEXT("the target a metre ahead and 45 cm to the side, its body inside, is found"),
		Found.Contains(Beside.Actor));
	TestTrue(TEXT("the one 2.5 m ahead is found"), Found.Contains(Ahead.Actor));
	TestFalse(TEXT("the one whose body is clear of the edge is not"), Found.Contains(Clear.Actor));
	if (Found.Num() == 2)
	{
		TestEqual(TEXT("nearest first: the one beside the aim"), Found[0], Beside.Actor);
	}

	// A LIMIT OF ONE IS THE NEAREST INSIDE, which is the whip's: the target beside the aim, not the one ahead.
	const TArray<AActor*> One = UCataclysmTargeting::FindEnemiesInCone(
		World, Caster.Actor, FVector::ZeroVector, FVector::ForwardVector, 3 * M, 45.0f, /*MaxTargets=*/1);
	TestEqual(TEXT("a limit of one returns one"), One.Num(), 1);
	if (One.Num() == 1)
	{
		TestEqual(TEXT("and it is the nearest inside, the target beside the aim"), One[0], Beside.Actor);
	}

	// AND A LIMIT IS NEVER SPENT ON A TARGET OUTSIDE THE CONE THOUGH NEARER: aimed along -Y, the clear target 2.1 m
	// away is 14 degrees off and inside, and the target beside the old aim, 1.1 m away and nearer, is behind the cone's
	// edge; the cut keeps the clear one.
	const TArray<AActor*> Turned = UCataclysmTargeting::FindEnemiesInCone(
		World, Caster.Actor, FVector::ZeroVector, FVector(0, -1, 0), 3 * M, 45.0f, /*MaxTargets=*/1);
	TestEqual(TEXT("aimed along -Y, a limit of one returns one"), Turned.Num(), 1);
	if (Turned.Num() == 1)
	{
		TestEqual(TEXT("and it is the target inside, not the nearer one outside"), Turned[0], Clear.Actor);
	}
	return true;
}

// --------------------------------------------------------------------------
// The line
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLineShapeTest,
	"Cataclysm.Targeting.ALineStopsAtBothOfItsEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLineShapeTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	// Infernal Lance: "piercing every enemy in a 12 meter line".
	const FVector Start = FVector::ZeroVector;
	const FVector End = FVector(12 * M, 0, 0);
	const float HalfWidth = 1.5f * M;

	TestTrue(TEXT("On the line, part way along"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(6 * M, 0, 0), HalfWidth));

	TestTrue(TEXT("Just off to one side, within the width"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(6 * M, 1 * M, 0), HalfWidth));

	TestFalse(TEXT("Off to one side, beyond the width"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(6 * M, 2 * M, 0), HalfWidth));

	// THE CLAMP IS WHAT THIS PROVES. Without it the nearest point on an infinite
	// line is used, and an enemy well behind the caster is on that line, so a
	// charge would hit somebody it ran away from.
	TestFalse(TEXT("Well behind the start is outside"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(-5 * M, 0, 0), HalfWidth));

	TestFalse(TEXT("Well beyond the end is outside"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(20 * M, 0, 0), HalfWidth));

	// Just past the end but within the width of it is still inside, because the
	// projectile has a thickness at its tip as well as along its length.
	TestTrue(TEXT("Just past the end, within the width, is inside"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(13 * M, 0, 0), HalfWidth));

	// A blink covers no ground, and its two ends are the same point.
	TestTrue(TEXT("A line with no length is a circle at its start"),
		UCataclysmTargeting::IsInLine(Start, Start, FVector(1 * M, 0, 0), HalfWidth));

	TestFalse(TEXT("A width of zero hits nothing"),
		UCataclysmTargeting::IsInLine(Start, End, FVector(6 * M, 0, 0), 0.0f));

	return true;
}

// --------------------------------------------------------------------------
// The world query
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFindEnemiesTest,
	"Cataclysm.Targeting.ASearchReturnsHostilesNearestFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFindEnemiesTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedTarget Caster(World, FVector::ZeroVector);
	FScopedTarget Far(World, FVector(5 * M, 0, 0));
	FScopedTarget Near(World, FVector(2 * M, 0, 0));
	FScopedTarget Outside(World, FVector(20 * M, 0, 0));

	// Something with collision but no ability system: scenery. Nothing a skill
	// can damage, so nothing a skill should find.
	FScopedTarget Scenery(World, FVector(1 * M, 0, 0), /*bWithAbilitySystem=*/false);

	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Caster.Actor, FVector::ZeroVector, 10 * M);

	TestEqual(TEXT("Two hostiles are within ten metres"), Found.Num(), 2);
	if (Found.Num() == 2)
	{
		// NEAREST FIRST, which is what makes a cap of one mean "the closest".
		TestEqual(TEXT("The nearer one comes first"), Found[0], Near.Actor);
		TestEqual(TEXT("The farther one comes second"), Found[1], Far.Actor);
	}

	TestFalse(TEXT("The caster does not find itself"), Found.Contains(Caster.Actor));
	TestFalse(TEXT("Something outside the radius is not found"),
		Found.Contains(Outside.Actor));
	TestFalse(TEXT("Scenery with no ability system is not a target"),
		Found.Contains(Scenery.Actor));

	// Searing Hook deals "massive damage to a single target". The cap has to
	// keep the CLOSEST rather than whichever the physics scene returned first.
	const TArray<AActor*> Capped = UCataclysmTargeting::FindEnemiesInSphere(
		World, Caster.Actor, FVector::ZeroVector, 10 * M, /*MaxTargets=*/1);
	TestEqual(TEXT("A cap of one returns one"), Capped.Num(), 1);
	if (Capped.Num() == 1)
	{
		TestEqual(TEXT("And it is the nearest"), Capped[0], Near.Actor);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOwnMinionsAreNotEnemiesTest,
	"Cataclysm.Targeting.ACharactersOwnSummonsAreNotItsEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOwnMinionsAreNotEnemiesTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedTarget Caster(World, FVector::ZeroVector);
	FScopedTarget Minion(World, FVector(1 * M, 0, 0));
	FScopedTarget Enemy(World, FVector(2 * M, 0, 0));

	// OWNERSHIP ALONE, WITH NO SIDE ANYWHERE. None of these three actors is a
	// character, so none of them has a side, and this is exactly the case
	// UCataclysmTeams::AttitudeBetween checks ownership first for: a thing a
	// caster put into the world is on that caster's side whether or not anything
	// gave it a team. Cataclysm.Teams.ASummonFightsForWhoeverMadeIt covers the
	// real minion, which is given its summoner's side as well.
	Minion.Actor->SetOwner(Caster.Actor);

	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Caster.Actor, FVector::ZeroVector, 10 * M);

	TestFalse(TEXT("A summon of the caster is not a target"),
		Found.Contains(Minion.Actor));
	TestTrue(TEXT("A real enemy still is"), Found.Contains(Enemy.Actor));

	// And the other way: a minion must not attack the character that made it.
	TestFalse(TEXT("A minion does not treat its summoner as an enemy"),
		UCataclysmTargeting::IsHostileTo(Caster.Actor, Minion.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLineSearchOrdersFromTheStartTest,
	"Cataclysm.Targeting.APiercingLineHitsInTheOrderItPassesThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLineSearchOrdersFromTheStartTest::RunTest(const FString&)
{
	using namespace CataclysmTargetingTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedTarget Caster(World, FVector::ZeroVector);
	FScopedTarget First(World, FVector(2 * M, 0, 0));
	FScopedTarget Second(World, FVector(6 * M, 0, 0));
	FScopedTarget Third(World, FVector(10 * M, 0, 0));
	FScopedTarget Beside(World, FVector(6 * M, 4 * M, 0));

	const TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInLine(
		World, Caster.Actor, FVector::ZeroVector, FVector(12 * M, 0, 0), 1.5f * M);

	TestEqual(TEXT("Three enemies stand on the line"), Found.Num(), 3);
	TestFalse(TEXT("One standing beside it is missed"), Found.Contains(Beside.Actor));

	// THE ORDER MATTERS BECAUSE PIERCE IS A COUNT. A lance that pierces two must
	// hit the first two it reaches, not two chosen by the physics scene. The
	// search sphere is centred on the MIDDLE of the line, so without the second
	// sort this order would be by distance from the middle.
	if (Found.Num() == 3)
	{
		TestEqual(TEXT("Nearest to the caster first"), Found[0], First.Actor);
		TestEqual(TEXT("Then the middle one"), Found[1], Second.Actor);
		TestEqual(TEXT("Then the farthest"), Found[2], Third.Actor);
	}

	const TArray<AActor*> TwoOnly = UCataclysmTargeting::FindEnemiesInLine(
		World, Caster.Actor, FVector::ZeroVector, FVector(12 * M, 0, 0), 1.5f * M,
		/*MaxTargets=*/2);
	TestEqual(TEXT("A cap of two returns two"), TwoOnly.Num(), 2);
	if (TwoOnly.Num() == 2)
	{
		TestEqual(TEXT("And they are the first two along the line"),
			TwoOnly[1], Second.Actor);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
