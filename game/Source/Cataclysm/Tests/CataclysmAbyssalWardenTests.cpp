// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the Abyssal Warden, the mini-boss of the Demonic vertical slice.
 *
 * WHAT THESE GUARD, and each is something this creature can fail at silently:
 *
 * 1. Being an Abyssal Warden rather than an inherited training dummy. Every
 *    assertion on a designed figure also asserts it differs from the base
 *    enemy's, because a test that would pass on the base class proves nothing.
 *
 * 2. Offering exactly one ability. Its designed charge cannot be executed by
 *    the current brain and is deliberately absent -- issue #491 -- and the
 *    failure mode is somebody adding it back, which would take priority over
 *    the ring everywhere inside eight metres and then do nothing at all.
 *
 * 3. Never being able to catch the player. That is designed, and one line in
 *    the constructor would undo it.
 *
 * 4. Wearing its art without breaking without it. The Paragon packs are
 *    gitignored, so these run both ways and must pass either way.
 */

namespace CataclysmWardenTest
{
	/** A world that has begun play, so spawned characters get their attributes. */
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (!World)
		{
			return nullptr;
		}

		FWorldContext& Context =
			GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	static void TearDown(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	static ACataclysmAbyssalWardenCharacter* SpawnWarden(UWorld* World,
														 const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmAbyssalWardenCharacter>(
			ACataclysmAbyssalWardenCharacter::StaticClass(), Where,
			FRotator::ZeroRotator, Params);
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenCarriesItsDesignedProfile,
	"Cataclysm.Warden.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	TestEqual(TEXT("it reaches exactly contact distance"),
		Warden->AttackReachCm(), Warden_t::DesignedMeleeReachCm);
	TestEqual(TEXT("it swings on its designed interval"),
		Warden->SecondsBetweenAttacks(), Warden_t::DesignedAttackIntervalSeconds);
	TestEqual(TEXT("it wanders its designed distance"),
		Warden->RoamRadiusCm(), Warden_t::WardenRoamRadiusCm);
	TestEqual(TEXT("it notices at its designed distance"),
		Warden->SightRadiusCm(), Warden_t::WardenNoticeRadiusCm);

	// AND EVERY ONE OF THOSE DIFFERS FROM THE BASE ENEMY'S, which is what makes
	// the four above mean something. A creature that inherited the base's
	// figures would pass them all.
	TestNotEqual(TEXT("its reach is not the base enemy's"),
		Warden->AttackReachCm(), 200.0f);
	TestNotEqual(TEXT("its interval is not the base enemy's"),
		Warden->SecondsBetweenAttacks(), 1.5f);
	TestTrue(TEXT("it roams, where the base enemy does not"),
		Warden->RoamRadiusCm() > 0.0f);

	const UCapsuleComponent* Capsule = Warden->GetCapsuleComponent();
	TestEqual(TEXT("its capsule is its designed radius"),
		Capsule->GetScaledCapsuleRadius(), Warden_t::WardenCapsuleRadius);
	TestEqual(TEXT("its capsule is its designed half-height"),
		Capsule->GetScaledCapsuleHalfHeight(), Warden_t::WardenCapsuleHalfHeight);

	const UCharacterMovementComponent* Movement = Warden->GetCharacterMovement();
	TestEqual(TEXT("it walks at its designed speed"),
		Movement->MaxWalkSpeed, Warden_t::DesignedWalkSpeedCmPerSecond);
	// CAST BECAUSE FRotator's COMPONENTS ARE DOUBLES IN UNREAL 5. Comparing one
	// against a float constant is an ambiguous overload, error C2666, and the
	// message names eight candidates rather than saying so.
	TestEqual(TEXT("it turns at its designed rate"),
		Movement->RotationRate.Yaw,
		static_cast<double>(Warden_t::DesignedTurnRateDegreesPerSecond));

	// THE ONE THAT WOULD OTHERWISE BE SILENT. ACataclysmEnemyCharacter never
	// sets MaxWalkSpeed, so a creature that forgot to would move at Unreal's
	// default 600 -- faster than the player, which is exactly what this one is
	// designed not to be.
	TestTrue(TEXT("and it is not Unreal's default walk speed"),
		Movement->MaxWalkSpeed < 600.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenOffersOnlyMoltenRoar,
	"Cataclysm.Warden.ItOffersOnlyMoltenRoar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenOffersOnlyMoltenRoar::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	const TArray<FCataclysmEnemyAbility> Abilities = Warden->EnemyAbilities();

	// ONE, AND THE COUNT IS THE POINT. Its designed charge is a Movement shape
	// and nothing can execute one: ChooseAbility picks by range and cooldown
	// without looking at the shape and takes the first entry that fits, so a
	// charge spanning 0 to 800 cm would be chosen ahead of this ring everywhere
	// inside eight metres, draw no marker, and do nothing. Issue #491.
	if (!TestEqual(TEXT("it offers exactly one ability"), Abilities.Num(), 1))
	{
		return false;
	}

	const FCataclysmEnemyAbility& Roar = Abilities[0];

	TestEqual(TEXT("it is Molten Roar"), Roar.Name, FName(TEXT("Molten Roar")));
	TestEqual(TEXT("it is a Strike, which the brain can draw a marker for"),
		static_cast<int32>(Roar.Shape),
		static_cast<int32>(ECataclysmSkillShape::Strike));
	TestEqual(TEXT("it reaches its designed radius"),
		Roar.MaxRangeCm, Warden_t::MoltenRoarRadiusCm);
	TestEqual(TEXT("its marker is the same radius the damage sweeps"),
		Roar.MarkerRadiusCm, Warden_t::MoltenRoarRadiusCm);
	TestEqual(TEXT("it comes round on its designed cooldown"),
		Roar.CooldownSeconds, Warden_t::MoltenRoarCooldownSeconds);
	TestEqual(TEXT("it warns for its designed wind-up"),
		Roar.WindUpSeconds, Warden_t::MoltenRoarWindUpSeconds);

	// FROM ITS OWN FEET OUT. A target pressed against the creature is inside
	// the ring and must be hit by it, so a minimum range would be wrong here in
	// a way it is not wrong for a lobbed attack.
	TestEqual(TEXT("it has no minimum range"), Roar.MinRangeCm, 0.0f);

	TestFalse(TEXT("it is not lobbed, so its marker is a ring at the feet"),
		Roar.bArcsOntoItsTarget);

	// AND THE MARKER CLEARS THE ONE METRE FLOOR, below which the design document
	// says a marker is smaller than the creature standing in it.
	TestTrue(TEXT("its marker is larger than the smallest useful one"),
		Roar.MarkerRadiusCm >= 100.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenAlternatesItsTwoSwings,
	"Cataclysm.Warden.ItAlternatesItsTwoSwings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenAlternatesItsTwoSwings::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	// CALLED DIRECTLY RATHER THAN INFERRED FROM BeginPlay, because whether
	// BeginPlay ran at all depends on how the world was made, and a test that
	// spawns into a synthetic world cannot otherwise tell "the art is missing"
	// from "BeginPlay did not fire".
	Warden->ResolveBody(/*bIncludeAnimation=*/true);

	const bool bStartsLeft = Warden->bSwingLeftNext;

	// THE ALTERNATION IS CHECKED WITHOUT THE ART, and that is deliberate. The
	// Paragon Grux pack is gitignored, so on a machine without it both clips are
	// null. The flag still has to flip, because it is the sequence that is being
	// tested rather than the clip.
	Warden->AttackTarget(nullptr);
	TestNotEqual(TEXT("the next swing is the other arm"),
		Warden->bSwingLeftNext, bStartsLeft);

	Warden->AttackTarget(nullptr);
	TestEqual(TEXT("and the one after that is the first arm again"),
		Warden->bSwingLeftNext, bStartsLeft);

	// WITH THE ART, the two clips must actually differ. Without it, both are
	// null and there is nothing to compare, which is reported rather than
	// skipped silently.
	if (Warden->LeftSwingAnimation && Warden->RightSwingAnimation)
	{
		TestNotEqual(TEXT("the two swing clips are different animations"),
			Warden->LeftSwingAnimation.Get(), Warden->RightSwingAnimation.Get());

		// AND BOTH FIT INSIDE ONE ATTACK INTERVAL TOGETHER, which is the claim
		// the design makes about this creature and no other.
		const float Pair = Warden->LeftSwingAnimation->GetPlayLength()
						 + Warden->RightSwingAnimation->GetPlayLength();
		TestTrue(TEXT("both swings together fit inside one attack interval"),
			Pair <= ACataclysmAbyssalWardenCharacter::DesignedAttackIntervalSeconds);
	}
	else
	{
		AddInfo(TEXT("SKIPPED the clip comparison: the Paragon Grux pack is not "
					 "present, so both swing animations are null. The "
					 "alternation itself was still checked."));
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenRingReachesItsWholeMarker,
	"Cataclysm.Warden.MoltenRoarHitsEverythingInsideItsMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenRingReachesItsWholeMarker::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE SWEEP THE ABILITY REALLY USES, asked at the same radius the marker is
	// drawn from. What this proves is that the circle promised and the circle
	// swept are the same number rather than two that can disagree -- which is
	// the defect the Brute's rock throw shipped four times.
	const FCataclysmEnemyAbility Roar = Warden->EnemyAbilities()[0];

	TestEqual(TEXT("the marker and the ability's reach are one number"),
		Roar.MarkerRadiusCm, Roar.MaxRangeCm);
	TestEqual(TEXT("and both are the constant the damage sweeps at"),
		Roar.MarkerRadiusCm, Warden_t::MoltenRoarRadiusCm);

	// SOMETHING STANDING AT THE EDGE IS INSIDE IT. Spawned far off and then
	// moved, because two capsules created at contact distance push each other
	// apart before anything can be measured.
	//
	// AND PUT ON THE PLAYERS' TEAM, WHICH IS NOT OPTIONAL. FindEnemiesInSphere
	// finds actors hostile to the actor asking, and a second creature spawned
	// as an enemy is the Warden's ALLY. The first version of this test left it
	// on the default team, and the sweep correctly returned nothing -- which
	// read as the ring not working rather than as the target being on the wrong
	// side.
	ACataclysmEnemyCharacter* AtTheEdge =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(10000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!AtTheEdge)
	{
		AddError(TEXT("could not spawn something to stand in the ring"));
		return false;
	}

	AtTheEdge->SetGenericTeamId(
		UCataclysmTeams::IdFor(ECataclysmTeam::Players));

	AtTheEdge->SetActorLocation(
		FVector(Warden_t::MoltenRoarRadiusCm - 1.0f, 0.0f, 0.0f));

	const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
		World, Warden, Warden->GetActorLocation(),
		Warden_t::MoltenRoarRadiusCm);

	TestTrue(TEXT("something just inside the ring is caught by it"),
		Caught.Contains(AtTheEdge));

	// AND SOMETHING JUST OUTSIDE IT IS NOT, which is what makes the check above
	// mean something rather than proving the sweep catches everything.
	AtTheEdge->SetActorLocation(
		FVector(Warden_t::MoltenRoarRadiusCm + 200.0f, 0.0f, 0.0f));

	const TArray<AActor*> Missed = UCataclysmTargeting::FindEnemiesInSphere(
		World, Warden, Warden->GetActorLocation(),
		Warden_t::MoltenRoarRadiusCm);

	TestFalse(TEXT("something outside the ring is not caught by it"),
		Missed.Contains(AtTheEdge));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenCanRegisterContactDespiteItsHeight,
	"Cataclysm.Warden.ItCanRegisterContactDespiteItsHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenCanRegisterContactDespiteItsHeight::RunTest(const FString&)
{
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE BOUND THAT IS NOT OBVIOUS, checked in the engine as well as in Python.
	// The brain compares reach on the floor plane, but the two capsule centres
	// do not sit at the same height and ContactToleranceCm is the only slack
	// there is. This creature's capsule is 34 cm taller than the base enemy's,
	// which is what makes it the one where the arithmetic is tight.
	constexpr float PlayerCapsuleRadius = 42.0f;
	constexpr float PlayerCapsuleHalfHeight = 96.0f;

	const float Horizontal = PlayerCapsuleRadius + Warden_t::WardenCapsuleRadius;
	const float HeightGap =
		Warden_t::WardenCapsuleHalfHeight - PlayerCapsuleHalfHeight;
	const double Contact3D = FMath::Sqrt(
		static_cast<double>(Horizontal) * Horizontal
		+ static_cast<double>(HeightGap) * HeightGap);

	const double Allowed = static_cast<double>(Warden_t::DesignedMeleeReachCm)
						 + ACataclysmEnemyController::ContactToleranceCm;

	TestTrue(TEXT("the contact tolerance absorbs its height, so it can reach "
				  "the player at all"),
		Contact3D <= Allowed);

	// AND THE HORIZONTAL DISTANCE IS EXACTLY ITS REACH, which is what the
	// creature's designed 0.90 m means: the two bodies touch and no further.
	TestEqual(TEXT("its reach is exactly the two capsule radii"),
		Horizontal, Warden_t::DesignedMeleeReachCm);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
