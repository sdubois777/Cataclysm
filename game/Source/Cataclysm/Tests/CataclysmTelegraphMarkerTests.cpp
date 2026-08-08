// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the ground marker an enemy draws while it winds up.
 *
 * WHAT THESE GUARD. Issue #396. Nothing in the project drew a ground marker of
 * any kind, so the design's wind-up rule -- 0.4 seconds plus radius over 3.5
 * metres per second, which exists so the player can see an area and walk out of
 * it -- was being kept on the timing side and broken on the seeing side.
 *
 * THE ASSERTION THAT MATTERS MOST is that the drawn radius is the ability's own
 * radius. A marker showing a different circle from the one that hurts is worse
 * than no marker at all, because the player would have learnt to trust it. Every
 * size check below compares against the constant the ability's damage uses, not
 * against a number written here.
 */

namespace CataclysmTelegraphTest
{
	/** A world that has begun play, so spawned characters are initialised and
	 *  possessed. Same reason the behaviour and sandbox tests need one. */
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

	/** Move the world clock without ticking anything. A world built by
	 *  UWorld::CreateWorld is never ticked, so nothing that waits can finish. */
	static void AdvanceWorldClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/**
	 * Something on the player's side for a Brute to attack.
	 *
	 * AN ENEMY CHARACTER RE-TEAMED, NOT ACataclysmPlayerCharacter, and that is
	 * the stand-in CataclysmEnemyBehaviourTests already uses. The player pawn's
	 * ability system lives on its player state, which a synthetic world has no
	 * controller to create, so UCataclysmTargeting cannot see one at all -- a
	 * Brute spawned beside one finds nothing to attack and never winds up.
	 * An enemy character owns its own component and is visible immediately.
	 */
	static ACataclysmEnemyCharacter* SpawnTarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target = World->SpawnActor<ACataclysmEnemyCharacter>(
			Where, FRotator::ZeroRotator);
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Target->SetHealth(1000.0f);
			Target->SetAttackDamage(0.0f);
		}
		return Target;
	}

	/** How many telegraph markers exist in the world right now. */
	static int32 CountMarkers(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmTelegraphMarker> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}

	/** The single marker in the world, or null when there is not exactly one. */
	static ACataclysmTelegraphMarker* OnlyMarker(UWorld* World)
	{
		ACataclysmTelegraphMarker* Found = nullptr;
		for (TActorIterator<ACataclysmTelegraphMarker> It(World); It; ++It)
		{
			if (!IsValid(*It))
			{
				continue;
			}
			if (Found)
			{
				return nullptr;
			}
			Found = *It;
		}
		return Found;
	}
}

// --------------------------------------------------------------------------
// The marker itself
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerDrawsBothShapes,
	"Cataclysm.Telegraph.ItDrawsACircleAndALaneAtTheSizesItIsGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerDrawsBothShapes::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

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

	ACataclysmTelegraphMarker* Circle = ACataclysmTelegraphMarker::ShowCircle(
		Caster, FVector::ZeroVector, /*RadiusCm=*/3.5f * M, /*Seconds=*/1.4f);
	if (!TestNotNull(TEXT("a three and a half metre circle is drawn"), Circle))
	{
		return false;
	}

	TestEqual(TEXT("at the radius it was asked for"), Circle->RadiusCm, 3.5f * M);
	TestFalse(TEXT("and it is a circle, not a lane"), Circle->IsLane());

	ACataclysmTelegraphMarker* Lane = ACataclysmTelegraphMarker::ShowLine(
		Caster, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
		/*HalfWidthCm=*/2.1f * M, /*Seconds=*/1.0f);
	if (!TestNotNull(TEXT("a lane is drawn"), Lane))
	{
		return false;
	}

	TestEqual(TEXT("at the half width it was asked for"), Lane->RadiusCm, 2.1f * M);
	TestEqual(TEXT("and reaching exactly as far as it was aimed"),
		Lane->LengthCm, 10.0f * M);
	TestTrue(TEXT("and it is a lane, not a circle"), Lane->IsLane());

	// MEASURED IN THE PLANE. A caster's centre and the point it aimed at sit at
	// different heights, and an unflattened length would be the hypotenuse
	// rather than the ground the lane covers.
	ACataclysmTelegraphMarker* Sloped = ACataclysmTelegraphMarker::ShowLine(
		Caster, FVector(0.0f, 0.0f, 130.0f), FVector(10.0f * M, 0.0f, 0.0f),
		/*HalfWidthCm=*/2.1f * M, /*Seconds=*/1.0f);
	if (!TestNotNull(TEXT("a lane aimed at a different height is drawn"), Sloped))
	{
		return false;
	}
	TestEqual(TEXT("and is still exactly as long across the ground"),
		Sloped->LengthCm, 10.0f * M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerRefusesWhatIsNotATelegraph,
	"Cataclysm.Telegraph.NothingIsDrawnBelowTheOneMetreFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerRefusesWhatIsNotATelegraph::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

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

	// THE BRUTE'S ORDINARY SLAM IS THE CASE THIS EXISTS FOR. It reaches 0.90
	// metres, which is under the design's one metre floor: a marker smaller than
	// the creature standing in it is not a telegraph, because there is nowhere
	// to walk. So it draws nothing and is read off the creature instead.
	TestNull(TEXT("a slam-sized circle draws nothing"),
		ACataclysmTelegraphMarker::ShowCircle(
			Caster, FVector::ZeroVector,
			ACataclysmBruteCharacter::DesignedMeleeReachCm, 1.0f));

	TestNull(TEXT("and neither does a lane that narrow"),
		ACataclysmTelegraphMarker::ShowLine(
			Caster, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
			ACataclysmBruteCharacter::DesignedMeleeReachCm, 1.0f));

	// ONE CENTIMETRE EITHER SIDE OF THE FLOOR, so the boundary itself is
	// checked rather than only a value far below it.
	TestNull(TEXT("a hair under the floor draws nothing"),
		ACataclysmTelegraphMarker::ShowCircle(
			Caster, FVector::ZeroVector,
			ACataclysmTelegraphMarker::SmallestUsefulRadiusCm - 1.0f, 1.0f));

	TestNotNull(TEXT("and exactly at the floor draws"),
		ACataclysmTelegraphMarker::ShowCircle(
			Caster, FVector::ZeroVector,
			ACataclysmTelegraphMarker::SmallestUsefulRadiusCm, 1.0f));

	// A wind-up of no length has nothing to warn about, and a lane of no length
	// is a square patch that says nothing about a direction.
	TestNull(TEXT("a wind-up of no length draws nothing"),
		ACataclysmTelegraphMarker::ShowCircle(
			Caster, FVector::ZeroVector, 3.5f * M, /*Seconds=*/0.0f));

	TestNull(TEXT("a lane aimed at its own feet draws nothing"),
		ACataclysmTelegraphMarker::ShowLine(
			Caster, FVector::ZeroVector, FVector::ZeroVector, 2.1f * M, 1.0f));

	return true;
}

// --------------------------------------------------------------------------
// The Brute, through its controller
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteMarksItsStompAndClearsIt,
	"Cataclysm.Telegraph.TheBruteMarksItsStompRingAndTakesItAwayWhenItLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteMarksItsStompAndClearsIt::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

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

	// Well inside the stomp's ring, so the stomp is what it chooses. The rock
	// throw has a minimum range of the melee reach and so is not available here.
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(1.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("something for it to attack"), Target))
	{
		return false;
	}

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Brute->GetController());
	if (!TestNotNull(TEXT("the Brute has a controller"), Brain))
	{
		return false;
	}

	TestEqual(TEXT("nothing is drawn before it decides to do anything"),
		CountMarkers(World), 0);

	Brain->Think();

	if (!TestEqual(TEXT("it starts winding up the stomp"),
		Brain->WindingUpAbility,
		static_cast<int32>(ACataclysmBruteCharacter::StompAbility)))
	{
		return false;
	}

	ACataclysmTelegraphMarker* Marker = OnlyMarker(World);
	if (!TestNotNull(TEXT("and exactly one marker appears"), Marker))
	{
		return false;
	}

	// THE ASSERTION THIS FILE EXISTS FOR. Compared against the constant the
	// stomp's own damage sweep uses, not against a number written here, so the
	// two cannot drift into showing one circle and hurting another.
	TestEqual(TEXT("drawn at the stomp's own radius"),
		Marker->RadiusCm, ACataclysmBruteCharacter::StompRadiusCm);
	TestFalse(TEXT("and it is a circle"), Marker->IsLane());

	// ON THE GROUND, NOT AT THE CREATURE'S MIDDLE. An actor's location is its
	// capsule centre; a marker left there floats at chest height.
	const float HalfHeight = Brute->GetCapsuleComponent()
		? Brute->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	TestEqual(TEXT("and it sits at the creature's feet"),
		static_cast<float>(Marker->GetActorLocation().Z),
		static_cast<float>(Brute->GetActorLocation().Z) - HalfHeight);

	// A SECOND PASS MID-WIND-UP MUST NOT DRAW A SECOND ONE. Think runs four
	// times a second and the stomp winds up for 1.4, so this happens five times
	// in every real telegraph.
	Brain->Think();
	TestEqual(TEXT("a second pass part way through leaves one marker, not two"),
		CountMarkers(World), 1);

	// Past the moment it lands.
	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds + 0.1);
	Brain->Think();

	TestEqual(TEXT("the stomp landed"), Brain->AbilitiesUsed, 1);
	TestEqual(TEXT("and its marker was taken away with it"),
		CountMarkers(World), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteMarksItsThrowAsALane,
	"Cataclysm.Telegraph.TheBruteMarksItsRockThrowAsALaneToWhereItAimed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteMarksItsThrowAsALane::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

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

	// Outside the stomp's ring and inside the throw's range, so the throw is
	// the only ability available.
	const float Distance = 7.0f * M;
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(Distance, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("something for it to attack"), Target))
	{
		return false;
	}

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Brute->GetController());
	if (!TestNotNull(TEXT("the Brute has a controller"), Brain))
	{
		return false;
	}

	Brain->Think();

	if (!TestEqual(TEXT("it starts winding up the rock throw"),
		Brain->WindingUpAbility,
		static_cast<int32>(ACataclysmBruteCharacter::RockThrowAbility)))
	{
		return false;
	}

	ACataclysmTelegraphMarker* Marker = OnlyMarker(World);
	if (!TestNotNull(TEXT("a marker appears"), Marker))
	{
		return false;
	}

	TestTrue(TEXT("and it is a lane rather than a circle"), Marker->IsLane());
	TestEqual(TEXT("as wide as the rock that will travel down it"),
		Marker->RadiusCm, ACataclysmBruteCharacter::RockThrowRadiusCm);

	// TO WHERE IT AIMED, NOT TO THE ABILITY'S MAXIMUM RANGE. The projectile
	// stops where it was aimed -- ACataclysmProjectile::Fire takes its range
	// from the distance between the two points it is given -- so a lane drawn
	// out to the full 10 metres would cover ground nothing happens on.
	TestTrue(FString::Printf(
		TEXT("and reaches the target rather than the ability's full range "
			 "(%.0f cm of %.0f)"),
		Marker->LengthCm, ACataclysmBruteCharacter::RockThrowRangeCm),
		Marker->LengthCm < ACataclysmBruteCharacter::RockThrowRangeCm);

	// Within a capsule radius of the gap between the two, which is as exact as
	// this can be: both characters are pushed apart by their own capsules as
	// they spawn.
	TestTrue(FString::Printf(
		TEXT("and it is as long as the gap it crosses (%.0f cm against %.0f)"),
		Marker->LengthCm, Distance),
		FMath::Abs(Marker->LengthCm - Distance) < 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAnInterruptedWindUpTakesItsMarkerWithIt,
	"Cataclysm.Telegraph.AnInterruptedWindUpTakesItsMarkerWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAnInterruptedWindUpTakesItsMarkerWithIt::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(1.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("brute"), Brute)
		|| !TestNotNull(TEXT("something for it to attack"), Target))
	{
		return false;
	}

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Brute->GetController());
	if (!TestNotNull(TEXT("the Brute has a controller"), Brain))
	{
		return false;
	}

	Brain->Think();
	if (!TestEqual(TEXT("a marker is drawn for the wind-up"),
		CountMarkers(World), 1))
	{
		return false;
	}

	// STUNNING IT IS THE ONE THING THAT ABANDONS A COMMITTED WIND-UP. The
	// attack did not happen, so its warning must not stay on the floor telling
	// the player to leave ground where nothing is going to land. Do that a few
	// times and they stop reading the next one.
	UCataclysmSkillEffects::ApplyStun(Target, Brute, /*Seconds=*/1.0f,
									  /*DamageDealt=*/0.0f,
									  /*bStunIsDesigned=*/true);

	Brain->Think();

	TestEqual(TEXT("the wind-up was abandoned"), Brain->WindingUpAbility, -1);
	TestEqual(TEXT("and its marker went with it"), CountMarkers(World), 0);
	TestEqual(TEXT("and no ability landed"), Brain->AbilitiesUsed, 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
