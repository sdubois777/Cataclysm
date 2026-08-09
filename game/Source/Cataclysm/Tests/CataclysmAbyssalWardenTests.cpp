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
	FCataclysmWardenSwingsLeftThenRightThenRecovers,
	"Cataclysm.Warden.ItSwingsLeftThenRightThenRecovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenSwingsLeftThenRightThenRecovers::RunTest(const FString&)
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

	const bool bDressed = Warden->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Warden->LeftSwingAnimation)
	{
		AddInfo(TEXT("SKIPPED: the Paragon Grux pack is not present, so there "
					 "are no clips to sequence. The behaviour this checks was "
					 "NOT verified on this machine."));
		return true;
	}

	// THREE CLIPS, IN ORDER, AS ONE BASIC ATTACK. Asked for by the project
	// owner: "if he has an attack for both arms include them both so he gives a
	// good left right", and the recovery is what stops the creature snapping
	// back to neutral from a metre and a half of hand travel away.
	const TArray<UAnimSequence*> Clips = Warden->BasicAttackClips();

	if (!TestEqual(TEXT("one basic attack is three clips"), Clips.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("the left swing comes first"),
		Clips[0], Warden->LeftSwingAnimation.Get());
	TestEqual(TEXT("the right swing comes second"),
		Clips[1], Warden->RightSwingAnimation.Get());
	TestEqual(TEXT("and the recovery comes last"),
		Clips[2], Warden->SwingRecoveryAnimation.Get());

	TestNotEqual(TEXT("the two swings are different animations"),
		Clips[0], Clips[1]);

	// AND THE WHOLE THING FITS INSIDE ONE ATTACK INTERVAL AT AUTHORED SPEED,
	// which is the reason the fast swing variants were chosen over the
	// full-speed ones. Measured 2026-08-09: 2.100 s against 2.4.
	float Total = 0.0f;
	for (UAnimSequence* Clip : Clips)
	{
		Total += Clip->GetPlayLength();
	}
	TestTrue(TEXT("the whole combo fits inside one attack interval"),
		Total <= ACataclysmAbyssalWardenCharacter::DesignedAttackIntervalSeconds);

	// THE SEQUENCE REALLY RUNS, one clip at a time, and ends back at rest.
	Warden->AttackTarget(nullptr);

	TestEqual(TEXT("the left swing is playing first"),
		Warden->LastPlayedAnimation.Get(), Clips[0]);

	for (int32 Step = 1; Step < Clips.Num(); ++Step)
	{
		// The clip's end is moved into the past rather than the world being
		// ticked forward, which keeps the test instant and exercises the same
		// branch.
		Warden->OneShotEndsAtSeconds = 0.0f;
		Warden->UpdateLoopingAnimation();
		TestEqual(
			FString::Printf(TEXT("clip %d of the combo follows"), Step + 1),
			Warden->LastPlayedAnimation.Get(), Clips[Step]);
	}

	// AND THEN IT STANDS, rather than holding the recovery's last frame.
	Warden->OneShotEndsAtSeconds = 0.0f;
	Warden->UpdateLoopingAnimation();
	TestEqual(TEXT("and then it stands in its idle again"),
		Warden->CurrentLoopingAnimation.Get(), Warden->IdleAnimation.Get());

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
	FCataclysmWardenHidesItsPlaceholderOnceDressed,
	"Cataclysm.Warden.ItHidesItsPlaceholderOnceDressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenHidesItsPlaceholderOnceDressed::RunTest(const FString&)
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

	if (!Warden->PlaceholderBody)
	{
		AddError(TEXT("the enemy base no longer creates a PlaceholderBody, so "
					  "there is nothing to hide and this check is meaningless"));
		return false;
	}

	// IT STARTS VISIBLE, which is what makes the rest of this mean something. A
	// placeholder that was hidden from birth would pass the check below on a
	// creature that never turns it off.
	TestTrue(TEXT("the placeholder cylinder starts visible"),
		Warden->PlaceholderBody->IsVisible());

	const bool bDressed = Warden->ResolveBody(/*bIncludeAnimation=*/true);

	// THE RELATIONSHIP RATHER THAN AN ABSOLUTE, so this runs the same on a
	// machine with the Paragon Grux pack and on one without. With the art the
	// mesh resolves and the cylinder must go; without it, ResolveBody returns
	// false and the cylinder is all there is, so it must stay.
	//
	// THE PROJECT OWNER SAW THIS FAIL ON 2026-08-09: "the cylinder base is
	// appearing over him". The class set the skeletal mesh and left the
	// placeholder visible on top of it.
	TestEqual(
		TEXT("the placeholder is hidden exactly when the real mesh resolved"),
		Warden->PlaceholderBody->IsVisible(), !bDressed);

	if (!bDressed)
	{
		AddInfo(TEXT("the Paragon Grux pack is not present, so what was checked "
					 "is that the placeholder is KEPT rather than that it is "
					 "hidden. Both directions matter; only one ran here."));
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenReturnsToARestingPoseAfterASwing,
	"Cataclysm.Warden.ItReturnsToARestingPoseAfterASwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenReturnsToARestingPoseAfterASwing::RunTest(const FString&)
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

	const bool bDressed = Warden->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Warden->IdleAnimation || !Warden->LeftSwingAnimation)
	{
		AddInfo(TEXT("SKIPPED: the Paragon Grux pack is not present, so there "
					 "are no clips to move between. The behaviour this checks "
					 "was NOT verified on this machine."));
		return true;
	}

	// STANDING TO BEGIN WITH.
	Warden->UpdateLoopingAnimation();
	TestEqual(TEXT("it stands in its idle before anything happens"),
		Warden->CurrentLoopingAnimation.Get(), Warden->IdleAnimation.Get());

	// A SWING TAKES THE MESH AND RECORDS WHEN IT WILL GIVE IT BACK. Without
	// that the creature held the last frame of the swing until the next one,
	// which the project owner reported on 2026-08-09.
	Warden->AttackTarget(nullptr);

	TestTrue(TEXT("a swing records when it will finish"),
		Warden->OneShotEndsAtSeconds > World->GetTimeSeconds());
	TestNull(TEXT("and nothing is looping while it plays"),
		Warden->CurrentLoopingAnimation.Get());

	// WHILE IT IS STILL PLAYING, NOTHING TAKES THE MESH BACK.
	Warden->UpdateLoopingAnimation();
	TestNull(TEXT("the swing is left alone until it ends"),
		Warden->CurrentLoopingAnimation.Get());

	// AND ONCE THE WHOLE ATTACK HAS ENDED, THE RESTING POSE COMES BACK.
	//
	// DRAINED RATHER THAN STEPPED ONCE, and that is what this test learned on
	// 2026-08-09. It used to clear the end time a single time and expect the
	// idle, which was right when a basic attack was one clip. It is now three --
	// a left swing, a right swing and a recovery -- so a single step just starts
	// the next clip. The loop is bounded so a sequence that never finishes fails
	// here rather than hanging.
	//
	// Each end time is moved into the past rather than the world being ticked
	// forward, which keeps the test instant and exercises the same branch.
	constexpr int32 MostClipsAnAttackMayHave = 8;
	for (int32 Step = 0; Step < MostClipsAnAttackMayHave; ++Step)
	{
		if (Warden->CurrentLoopingAnimation)
		{
			break;
		}
		Warden->OneShotEndsAtSeconds = 0.0f;
		Warden->UpdateLoopingAnimation();
	}

	TestEqual(TEXT("it returns to its idle once the whole attack has finished"),
		Warden->CurrentLoopingAnimation.Get(), Warden->IdleAnimation.Get());

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenWalksRatherThanSlides,
	"Cataclysm.Warden.ItWalksRatherThanSlides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenWalksRatherThanSlides::RunTest(const FString&)
{
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE ARITHMETIC THAT STOPS THE FOOT SLIDING, checked without a world.
	//
	// A planted foot travels backwards at the clip's authored speed while the
	// body travels forwards at the designed speed. Playing the clip at the
	// ratio of the two makes them cancel. `tools/measure_animation_stride.py`
	// measured Jog_Fwd at 281.6 cm/s on 2026-08-09 and the creature is designed
	// at 280, so the rate is 0.994.
	const float Expected = Warden_t::DesignedWalkSpeedCmPerSecond
						 / Warden_t::AuthoredJogSpeedCmPerSecond;

	TestEqual(TEXT("the walk plays at the ratio of designed to authored speed"),
		Warden_t::JogPlayRate(), Expected);

	// AND THE PRODUCT IS THE SPEED IT ACTUALLY MOVES AT, which is the thing
	// that matters and is not the same statement. If either figure were read
	// from the wrong place this would still pass the check above and fail here.
	TestEqual(TEXT("so the foot travels backwards at the body's own speed"),
		Warden_t::JogPlayRate() * Warden_t::AuthoredJogSpeedCmPerSecond,
		Warden_t::DesignedWalkSpeedCmPerSecond);

	// THE RATE IS INSIDE THE CLAMP, so it is not being silently corrected.
	TestTrue(TEXT("the rate is not clamped"),
		Warden_t::JogPlayRate() > Warden_t::MinimumPlayRate
		&& Warden_t::JogPlayRate() < Warden_t::MaximumPlayRate);

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
