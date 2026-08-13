// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"

#include <utility>

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
	FCataclysmBruteMarksItsThrowWhereItLands,
	"Cataclysm.Telegraph.TheBruteMarksItsRockThrowAsACircleWhereItWillLand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteMarksItsThrowWhereItLands::RunTest(const FString&)
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

	// A CIRCLE, NOT A LANE, BECAUSE THE ROCK IS LOBBED. Issue #459. It was a
	// lane until 2026-08-08, when the throw became an arc. A lobbed rock rises
	// over everything between the creature and its target and endangers only
	// the ground it comes down on, so a lane marked floor on which nothing was
	// going to happen.
	TestFalse(TEXT("it is a circle rather than a lane"), Marker->IsLane());
	TestEqual(TEXT("as wide as the blast where the rock lands"),
		Marker->RadiusCm, ACataclysmBruteCharacter::RockThrowRadiusCm);

	// WHERE IT LANDS, NOT WHERE IT IS THROWN FROM, and this is the assertion
	// that makes the two above worth anything. A circle of exactly the right
	// size drawn at the creature's own feet satisfies both of them and warns
	// the player about entirely the wrong patch of floor.
	//
	// Within a capsule radius of the target, which is as exact as this can be:
	// both characters are pushed apart by their own capsules as they spawn.
	const float ToTarget = FVector::Dist2D(
		Marker->GetActorLocation(), Target->GetActorLocation());
	TestTrue(FString::Printf(
		TEXT("and it is centred on the target (%.0f cm from it)"), ToTarget),
		ToTarget < 100.0f);

	const float ToCreature = FVector::Dist2D(
		Marker->GetActorLocation(), Brute->GetActorLocation());
	TestTrue(FString::Printf(
		TEXT("and nowhere near the creature (%.0f cm of the %.0f cm gap)"),
		ToCreature, Distance),
		ToCreature > Distance * 0.5f);

	return true;
}

/**
 * What the attack is aimed at is the point the marker is drawn at, height and all.
 *
 * WHAT THIS GUARDS. Issue #471. `WindUpAimedAt` is used twice: the marker is drawn
 * at it, and it is handed to the pawn as the point to aim at. It used to be the
 * target's `GetActorLocation()`, which for a character is the CAPSULE CENTRE --
 * 96 cm above the floor for the player. Each marker then flattened it to the
 * floor for its own drawing, and nothing flattened it for the attack. The Brute's
 * lobbed rock therefore ended its flight about a metre above the circle that had
 * been drawn for it, which breaks the rule `docs/DECISIONS.md` states: a
 * telegraphed attack that marks a place must arrive at that place.
 *
 * WHY THE HEIGHT IS CHECKED AGAINST THE CREATURE'S FEET AND NOT AGAINST ZERO.
 * Both characters are spawned at the world origin's height and pushed apart by
 * their own capsules, so the floor in this world is wherever the Brute's feet
 * are. Comparing against zero would pass for the wrong reason.
 *
 * AND WHY IT ALSO CHECKS WHAT IT IS NOT. A test that only asserted "the aim point
 * is at the feet" would pass on a version that aimed at the CREATURE rather than
 * the target. The second half asserts it is horizontally at the target, so both
 * halves have to hold.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheAimPointIsOnTheFloorItMarks,
	"Cataclysm.Telegraph.WhatIsAimedAtIsWhereTheMarkerIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheAimPointIsOnTheFloorItMarks::RunTest(const FString&)
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

	// Outside the stomp's ring and inside the throw's range, so the lobbed rock
	// is the ability that gets chosen and the marker is a circle.
	const float Distance = 7.0f * M;
	ACataclysmEnemyCharacter* Target =
		SpawnTarget(World, FVector(Distance, 0.0f, 0.0f));
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

	const UCapsuleComponent* BruteCapsule = Brute->GetCapsuleComponent();
	const UCapsuleComponent* TargetCapsule = Target->GetCapsuleComponent();
	if (!TestNotNull(TEXT("the Brute has a capsule"), BruteCapsule)
		|| !TestNotNull(TEXT("the target has a capsule"), TargetCapsule))
	{
		return false;
	}

	// DOUBLE, NOT FLOAT. FVector components are double in Unreal 5 and mixing
	// the two makes TestEqual an ambiguous overload.
	const double FloorZ = Brute->GetActorLocation().Z
		- static_cast<double>(BruteCapsule->GetScaledCapsuleHalfHeight());
	const double TargetCentreZ = Target->GetActorLocation().Z;

	// THE CHECK IS ONLY WORTH ANYTHING IF THE TWO HEIGHTS DIFFER. A target whose
	// capsule centre happened to sit on the floor would satisfy everything below
	// without the flattening existing at all.
	if (!TestTrue(FString::Printf(
			TEXT("the target's centre is above the floor, so there is something "
				 "to get wrong (%.0f cm against %.0f)"), TargetCentreZ, FloorZ),
			TargetCentreZ - FloorZ > 10.0))
	{
		return false;
	}

	TestTrue(FString::Printf(
		TEXT("what it aims at is on the floor (%.1f cm against %.1f)"),
		Brain->WindUpAimedAt.Z, FloorZ),
		FMath::Abs(Brain->WindUpAimedAt.Z - FloorZ) < 1.0);

	TestTrue(FString::Printf(
		TEXT("and not at the target's capsule centre (%.1f cm against %.1f)"),
		Brain->WindUpAimedAt.Z, TargetCentreZ),
		FMath::Abs(Brain->WindUpAimedAt.Z - TargetCentreZ) > 10.0);

	// AND IT IS STILL THE TARGET'S PLACE. Flattening the height must not have
	// moved the point sideways onto the creature.
	const float ToTarget = FVector::Dist2D(
		Brain->WindUpAimedAt, Target->GetActorLocation());
	TestTrue(FString::Printf(
		TEXT("and it is still where the target is standing (%.0f cm from it)"),
		ToTarget),
		ToTarget < 100.0f);

	// THE MARKER AGREES, WHICH IS THE WHOLE POINT. The rock is aimed at
	// WindUpAimedAt and the circle is drawn at it, so if the two ever differ
	// again the attack stops arriving where it was advertised.
	ACataclysmTelegraphMarker* Marker = OnlyMarker(World);
	if (!TestNotNull(TEXT("a marker appears"), Marker))
	{
		return false;
	}

	const double MarkerToAim =
		FVector::Dist(Marker->GetActorLocation(), Brain->WindUpAimedAt);
	TestTrue(FString::Printf(
		TEXT("the marker is drawn at the point the attack is aimed at "
			 "(%.1f cm apart, in all three axes)"), MarkerToAim),
		MarkerToAim < 1.0);

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

// --------------------------------------------------------------------------
// What it is drawn with
//
// WHAT THESE GUARD. Issue #539. Until it, neither mesh had a material at all,
// so both took the engine default, which is lit. A warning that takes the
// room's lighting gets darker exactly when the world does, and section XIII of
// docs/Cataclysm_GDD_v2.md states the telegraph's contrast against each of the
// eight Cataclysm themes on the assumption that it does not.
//
// THE ASSERTION THAT MATTERS MOST is that the material is unlit. The colour
// checks below are worth having, but a lit marker with exactly the right colour
// is still the defect #539 describes.
// --------------------------------------------------------------------------

namespace CataclysmTelegraphTest
{
	/** The colour a component's material was actually given, or black with a
	 *  failed flag when there is no dynamic instance to ask. */
	static bool ColourOf(UStaticMeshComponent* Component, FLinearColor& OutColour)
	{
		if (!Component)
		{
			return false;
		}
		UMaterialInstanceDynamic* Instance =
			Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
		if (!Instance)
		{
			return false;
		}
		return Instance->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("Colour")), OutColour);
	}

	/** How opaque a component's material was actually told to be. */
	static bool OpacityOf(UStaticMeshComponent* Component, float& OutOpacity)
	{
		if (!Component)
		{
			return false;
		}
		UMaterialInstanceDynamic* Instance =
			Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
		if (!Instance)
		{
			return false;
		}
		return Instance->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("Opacity")), OutOpacity);
	}

	/** The designed colour for a hex string, the same conversion the marker
	 *  does. Written out here rather than shared, so a change to the marker's
	 *  conversion is caught rather than followed. */
	static FLinearColor Designed(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	static bool NearlyEqual(const FLinearColor& A, const FLinearColor& B)
	{
		return A.Equals(B, /*Tolerance=*/1.0f / 255.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerIsDrawnUnlit,
	"Cataclysm.Telegraph.ItIsDrawnWithAnUnlitMaterialSoTheRoomCannotDimIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerIsDrawnUnlit::RunTest(const FString&)
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
	if (!TestNotNull(TEXT("a circle is drawn"), Circle))
	{
		return false;
	}

	UStaticMeshComponent* Fill = Circle->GetPatch();
	if (!TestNotNull(TEXT("it has a fill"), Fill))
	{
		return false;
	}

	UMaterialInterface* Material = Fill->GetMaterial(0);
	if (!TestNotNull(TEXT("and the fill has a material at all"), Material))
	{
		return false;
	}

	// THE WHOLE POINT OF ISSUE #539. A lit material would make every contrast
	// figure in section XIII a statement about a swatch rather than about the
	// game.
	TestTrue(TEXT("and that material is unlit, so the room's lighting cannot "
				  "change how bright the warning is"),
		Material->GetShadingModels().HasShadingModel(MSM_Unlit));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerHasARimAroundItsFill,
	"Cataclysm.Telegraph.ItHasANearBlackRimWiderThanItsFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerHasARimAroundItsFill::RunTest(const FString&)
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
	if (!TestNotNull(TEXT("a circle is drawn"), Circle))
	{
		return false;
	}

	UStaticMeshComponent* Fill = Circle->GetPatch();
	UStaticMeshComponent* Rim = Circle->GetEdge();
	if (!TestNotNull(TEXT("it has a fill"), Fill)
		|| !TestNotNull(TEXT("and a rim"), Rim))
	{
		return false;
	}

	TestNotNull(TEXT("the rim has a mesh, so it is actually drawn"),
		Rim->GetStaticMesh().Get());

	// WIDER BY EXACTLY THE DESIGNED RIM, on both axes. Measured in world scale
	// because the rim is deliberately not a child of the fill: a child would
	// inherit the fill's scale and the rim would grow with the marker instead
	// of staying a constant edge.
	// DOUBLES, because a component's scale is an FVector and UE5's FVector is
	// double precision. Mixing the two makes TestEqual ambiguous rather than
	// wrong, which is a compile error and not a silent problem.
	const double ShapeSize = ACataclysmCharacterBase::BasicShapeSize;
	const double ExpectedFill = (3.5 * M * 2.0) / ShapeSize;
	const double ExpectedRim =
		((3.5 * M + ACataclysmTelegraphMarker::OutlineThicknessCm) * 2.0) / ShapeSize;

	TestEqual(TEXT("the fill is the ability's own radius across"),
		Fill->GetComponentScale().X, ExpectedFill, /*Tolerance=*/0.001);
	TestEqual(TEXT("and the rim is one rim thickness wider"),
		Rim->GetComponentScale().X, ExpectedRim, /*Tolerance=*/0.001);
	TestTrue(TEXT("so the rim shows all the way around the fill"),
		Rim->GetComponentScale().X > Fill->GetComponentScale().X);

	// UNDERNEATH, so the fill wins where they overlap rather than the two
	// fighting over the same depth.
	TestTrue(TEXT("and it sits below the fill"),
		Rim->GetRelativeLocation().Z < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerUsesTheDesignedColours,
	"Cataclysm.Telegraph.ItsFillAndRimAreTheColoursTheDesignDocumentStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerUsesTheDesignedColours::RunTest(const FString&)
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

	// BOTH SHAPES, because each sets its own meshes and scales and could just
	// as easily forget to colour them.
	ACataclysmTelegraphMarker* Circle = ACataclysmTelegraphMarker::ShowCircle(
		Caster, FVector::ZeroVector, /*RadiusCm=*/3.5f * M, /*Seconds=*/1.4f);
	ACataclysmTelegraphMarker* Lane = ACataclysmTelegraphMarker::ShowLine(
		Caster, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
		/*HalfWidthCm=*/2.1f * M, /*Seconds=*/1.0f);
	if (!TestNotNull(TEXT("a circle is drawn"), Circle)
		|| !TestNotNull(TEXT("and a lane"), Lane))
	{
		return false;
	}

	const FLinearColor ExpectedRing =
		Designed(ACataclysmTelegraphMarker::DesignedRingHex);
	const FLinearColor ExpectedRim =
		Designed(ACataclysmTelegraphMarker::DesignedOutlineHex);
	const FLinearColor ExpectedInner =
		Designed(ACataclysmTelegraphMarker::DesignedInnerHex);

	for (ACataclysmTelegraphMarker* Marker : {Circle, Lane})
	{
		const TCHAR* Which = Marker->IsLane() ? TEXT("lane") : TEXT("circle");

		FLinearColor Rim;
		FLinearColor Ring;
		FLinearColor Inner;
		if (!TestTrue(FString::Printf(TEXT("the %s's outer ring has a colour"), Which),
					  ColourOf(Marker->GetEdge(), Rim))
			|| !TestTrue(FString::Printf(TEXT("and the %s's bright ring does"), Which),
						 ColourOf(Marker->GetRing(), Ring))
			|| !TestTrue(FString::Printf(TEXT("and the %s's inner line does"), Which),
						 ColourOf(Marker->GetInner(), Inner)))
		{
			return false;
		}

		TestTrue(FString::Printf(TEXT("the %s's outer ring is the designed %s"),
			Which, ACataclysmTelegraphMarker::DesignedOutlineHex),
			NearlyEqual(Rim, ExpectedRim));

		TestTrue(FString::Printf(TEXT("the %s's bright ring is the designed %s"),
			Which, ACataclysmTelegraphMarker::DesignedRingHex),
			NearlyEqual(Ring, ExpectedRing));

		TestTrue(FString::Printf(TEXT("the %s's inner line is the designed %s"),
			Which, ACataclysmTelegraphMarker::DesignedInnerHex),
			NearlyEqual(Inner, ExpectedInner));

		// ALL THREE ARE DIFFERENT COLOURS. If the material's parameter were
		// renamed on one side only, every band would silently fall back to the
		// material's default and the rings would stop being rings. Three bands
		// the same colour is a solid disc with extra draw calls.
		TestFalse(FString::Printf(
			TEXT("and the %s's rings are not all the same colour"), Which),
			NearlyEqual(Rim, Ring) || NearlyEqual(Ring, Inner));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerColourCanBeOverriddenLive,
	"Cataclysm.Telegraph.TheFillColourCanBeChangedWithoutARebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerColourCanBeOverriddenLive::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

	// WHY THIS IS TESTED. The colour was argued twice and settled by looking at
	// it in the sandbox rather than by measurement -- cyan first, then FF3020 on
	// 2026-08-13 once the project owner had seen both. The override is what made
	// that possible, so it is a feature rather than a debugging leftover and is
	// worth holding.
	IConsoleVariable* Variable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.Telegraph.RingColour"));
	if (!TestNotNull(TEXT("the ring colour console variable exists"), Variable))
	{
		return false;
	}

	const FString Original = Variable->GetString();
	ON_SCOPE_EXIT { Variable->Set(*Original, ECVF_SetByCode); };

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

	Variable->Set(TEXT("22C9D6"), ECVF_SetByCode);

	ACataclysmTelegraphMarker* Overridden = ACataclysmTelegraphMarker::ShowCircle(
		Caster, FVector::ZeroVector, /*RadiusCm=*/3.5f * M, /*Seconds=*/1.4f);
	if (!TestNotNull(TEXT("a circle is drawn with the override set"), Overridden))
	{
		return false;
	}

	FLinearColor Ring;
	if (!TestTrue(TEXT("and its bright ring has a colour"),
				  ColourOf(Overridden->GetRing(), Ring)))
	{
		return false;
	}
	TestTrue(TEXT("which is the cyan that was asked for"),
		NearlyEqual(Ring, Designed(TEXT("22C9D6"))));

	// A TYPO PUTS THE DESIGN BACK rather than leaving a marker black or
	// refusing to draw the warning at all. This reads a value a person typed.
	Variable->Set(TEXT("nonsense"), ECVF_SetByCode);

	ACataclysmTelegraphMarker* Fallback = ACataclysmTelegraphMarker::ShowCircle(
		Caster, FVector(1000.0f, 0.0f, 0.0f), /*RadiusCm=*/3.5f * M, /*Seconds=*/1.4f);
	if (!TestNotNull(TEXT("a circle is still drawn after a bad override"), Fallback))
	{
		return false;
	}

	FLinearColor FallbackRing;
	if (!TestTrue(TEXT("and its bright ring has a colour"),
				  ColourOf(Fallback->GetRing(), FallbackRing)))
	{
		return false;
	}
	TestTrue(TEXT("which is the designed colour, not the typo"),
		NearlyEqual(FallbackRing, Designed(ACataclysmTelegraphMarker::DesignedRingHex)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTelegraphMarkerFillIsSeeThroughAndRingsAreNot,
	"Cataclysm.Telegraph.OnlyTheInnermostBandIsSeeThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTelegraphMarkerFillIsSeeThroughAndRingsAreNot::RunTest(const FString&)
{
	using namespace CataclysmTelegraphTest;

	// WHAT THIS GUARDS. The project owner asked on 2026-08-13 for the marker to
	// stop reading as a solid plate. The answer was to make only the innermost
	// band see-through and leave the three rings opaque, because a translucent
	// band's contrast against the ground beneath it falls toward 1:1 as it
	// fades. If the rings ever became translucent too, every contrast figure in
	// section XIII of the design document would stop being true and nothing
	// else would notice.
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
	if (!TestNotNull(TEXT("a circle is drawn"), Circle))
	{
		return false;
	}

	float FillOpacity = -1.0f;
	if (!TestTrue(TEXT("the innermost band has an opacity set"),
				  OpacityOf(Circle->GetPatch(), FillOpacity)))
	{
		return false;
	}
	TestEqual(TEXT("and it is the designed 0.35, so the ground reads through it"),
		FillOpacity, ACataclysmTelegraphMarker::DesignedFillOpacity, 0.001f);
	TestTrue(TEXT("which is less than fully opaque"), FillOpacity < 1.0f);

	for (auto Band : {std::make_pair(Circle->GetEdge(), TEXT("outer ring")),
					  std::make_pair(Circle->GetRing(), TEXT("bright ring")),
					  std::make_pair(Circle->GetInner(), TEXT("inner line"))})
	{
		float Opacity = -1.0f;
		if (!TestTrue(FString::Printf(TEXT("the %s has an opacity set"), Band.second),
					  OpacityOf(Band.first, Opacity)))
		{
			return false;
		}
		TestEqual(FString::Printf(
			TEXT("and the %s is fully opaque, because it carries the contrast"),
			Band.second), Opacity, 1.0f, 0.001f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
