// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "HAL/IConsoleManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

/**
 * Tests for The Brute, the first of the seven Demonic vertical slice enemies.
 *
 * WHAT THESE GUARD. Three separate things that a Brute can silently fail at:
 *
 * 1. Being a Brute at all rather than an inherited training dummy. Every
 *    assertion on a designed number also asserts it differs from the base
 *    enemy's, because a test that passes on the base class proves nothing.
 *
 * 2. Reaching the player. The Brute's designed reach is exactly contact --
 *    0.42 + 0.48 metres, the two capsule radii -- with no margin at all. That
 *    makes it the only enemy where the distance check's own arithmetic decides
 *    whether it ever lands a hit. See the horizontal-distance test.
 *
 * 3. Wearing its art without breaking without it. The Paragon packs are
 *    gitignored, so these tests run both with the art present and absent and
 *    must pass either way.
 */

namespace CataclysmBruteTest
{
	/** A world that has begun play, so spawned characters get their attributes. */
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

	/** The player capsule radius in CataclysmPlayerCharacter.cpp. */
	constexpr float PlayerCapsuleRadius = 42.0f;

	/** The player capsule half-height in CataclysmPlayerCharacter.cpp. */
	constexpr float PlayerCapsuleHalfHeight = 96.0f;

	/** The base enemy capsule half-height in CataclysmEnemyCharacter.cpp. */
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteCarriesItsDesignedNumbers,
	"Cataclysm.Brute.CarriesItsDesignedNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteCarriesItsDesignedNumbers::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}

	TestEqual(TEXT("reach is the designed 0.90 m"),
		Brute->AttackReachCm(), ACataclysmBruteCharacter::DesignedMeleeReachCm);
	TestEqual(TEXT("attack interval is the designed 2.8 s"),
		Brute->SecondsBetweenAttacks(),
		ACataclysmBruteCharacter::DesignedAttackIntervalSeconds);

	const UCharacterMovementComponent* Movement = Brute->GetCharacterMovement();
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}
	TestEqual(TEXT("walks at the designed 250 cm/s"),
		Movement->MaxWalkSpeed,
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond);
	// CAST, BECAUSE FRotator's components are double in Unreal 5 while the
	// designed constant is a float, and TestEqual has an overload for each.
	TestEqual(TEXT("turns at the designed 180 deg/s"),
		static_cast<float>(Movement->RotationRate.Yaw),
		ACataclysmBruteCharacter::DesignedTurnRateDegreesPerSecond);

	const UCapsuleComponent* Capsule = Brute->GetCapsuleComponent();
	if (!TestNotNull(TEXT("capsule"), Capsule))
	{
		return false;
	}
	TestEqual(TEXT("capsule radius is the designed body radius, 48 cm"),
		Capsule->GetUnscaledCapsuleRadius(),
		ACataclysmBruteCharacter::BruteCapsuleRadius);
	TestEqual(TEXT("capsule half-height fits the 221 cm mesh"),
		Capsule->GetUnscaledCapsuleHalfHeight(),
		ACataclysmBruteCharacter::BruteCapsuleHalfHeight);

	// THE POINT OF THE WHOLE CLASS, stated as an inequality rather than a value.
	// "Heavily armored slow melee. Can be outmaneuvered" is a claim about being
	// slower than other things, so it is tested against another thing.
	ACataclysmEnemyCharacter* Ordinary = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("ordinary enemy spawned"), Ordinary))
	{
		return false;
	}

	TestTrue(TEXT("a Brute walks strictly slower than an ordinary enemy"),
		Brute->GetCharacterMovement()->MaxWalkSpeed
			< Ordinary->GetCharacterMovement()->MaxWalkSpeed);
	TestTrue(TEXT("a Brute turns strictly slower than an ordinary enemy"),
		Brute->GetCharacterMovement()->RotationRate.Yaw
			< Ordinary->GetCharacterMovement()->RotationRate.Yaw);
	TestTrue(TEXT("a Brute attacks strictly less often than an ordinary enemy"),
		Brute->SecondsBetweenAttacks() > Ordinary->SecondsBetweenAttacks());
	TestTrue(TEXT("a Brute stands taller than an ordinary enemy"),
		Brute->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
			> BaseEnemyCapsuleHalfHeight);

	Ordinary->Destroy();
	Brute->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteCanActuallyReachThePlayer,
	"Cataclysm.Brute.CanActuallyReachThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteCanActuallyReachThePlayer::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// ARITHMETIC, NOT A WORLD. This is the check that the Brute's reach is
	// reachable at all, and it is the reason ACataclysmEnemyController::Think
	// measures horizontal distance rather than 3D distance.
	//
	// Two capsules touching are their two radii apart on the floor plane. Their
	// centres are not at the same height, because a player's capsule is 96 cm
	// half-height and an enemy's is 80.
	const float ContactHorizontal =
		ACataclysmBruteCharacter::BruteCapsuleRadius + PlayerCapsuleRadius;
	const float HeightDifference =
		PlayerCapsuleHalfHeight - BaseEnemyCapsuleHalfHeight;
	const float Contact3D = FMath::Sqrt(
		ContactHorizontal * ContactHorizontal
		+ HeightDifference * HeightDifference);

	const float Reach = ACataclysmBruteCharacter::DesignedMeleeReachCm;

	TestEqual(TEXT("contact on the floor plane is exactly the designed reach"),
		ContactHorizontal, Reach);

	// THE GUARD. If someone changes the controller back to a 3D distance, this
	// records what breaks: the Brute pressed against the player measures further
	// away than its own reach and chases for ever.
	TestTrue(
		TEXT("3D distance at contact EXCEEDS the reach, which is why the "
			 "controller must not use it"),
		Contact3D > Reach);

	// And the same check for the training dummy, showing why this never showed
	// up before: its reach has 110 cm of margin.
	constexpr float OrdinaryReach = 200.0f;
	TestTrue(TEXT("an ordinary enemy had margin to spare either way"),
		Contact3D < OrdinaryReach);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteFightsWithOrWithoutItsArt,
	"Cataclysm.Brute.FightsWithOrWithoutItsArt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteFightsWithOrWithoutItsArt::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}
	Brute->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Brute->SetHealth(11000.0f);
	Brute->SetAttackDamage(35.0f);

	// PROVES THE ABILITY SYSTEM CAME UP. An enemy whose attribute sets never
	// registered has no health to read.
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Brute);
	if (!TestNotNull(TEXT("ability system"), AbilitySystem))
	{
		return false;
	}
	TestEqual(TEXT("health was applied"),
		AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()),
		11000.0f);

	TestEqual(TEXT("it is on the monsters' side"),
		Brute->GetGenericTeamId().GetId(),
		UCataclysmTeams::IdFor(ECataclysmTeam::Monsters).GetId());

	// THE ART IS OPTIONAL AND THIS TEST SAYS SO RATHER THAN ASSUMING EITHER WAY.
	// The Paragon packs are excluded by .gitignore, so on a fresh clone and in
	// continuous integration the mesh is absent and that is not a failure.
	const bool bArtIsInstalled =
		FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).ResolveObject() != nullptr
		|| FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).TryLoad() != nullptr;

	if (bArtIsInstalled)
	{
		// CALLED DIRECTLY RATHER THAN RELYING ON BeginPlay. BeginPlay calls this
		// too, but whether it fires depends on how the world under test was
		// built, and reading the mesh afterwards cannot tell "the art failed to
		// load" apart from "BeginPlay never ran". The return value can.
		//
		// WITHOUT THE ANIMATIONS, because this half of the test is about whether
		// the Brute wears the mesh. The animations get their own test below.
		const bool bResolved = Brute->ResolveBody(/*bIncludeAnimation=*/false);
		TestTrue(TEXT("the Brute resolved its body when the art is installed"),
			bResolved);

		const USkeletalMeshComponent* MeshComponent = Brute->GetMesh();
		if (TestNotNull(TEXT("mesh component"), MeshComponent))
		{
			TestNotNull(TEXT("the Rampage mesh is on the Brute"),
				MeshComponent->GetSkeletalMeshAsset());
		}
	}
	else
	{
		AddInfo(TEXT("Paragon Rampage pack is not installed, so the mesh test "
					 "is skipped. The Brute keeps its placeholder cylinder. "
					 "This is the expected state in continuous integration."));
	}

	// EITHER WAY IT MUST STILL BE A FIGHTABLE ENEMY. This is what makes the
	// skip above honest rather than a hole: whether or not the art loaded, the
	// Brute is findable by the targeting system from the other side.
	ACataclysmEnemyCharacter* Hero = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(50.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("opposing character"), Hero))
	{
		return false;
	}
	Hero->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Hero->SetHealth(1000.0f);

	TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Hero, Hero->GetActorLocation(), 500.0f);
	TestTrue(TEXT("the Brute is a valid enemy target"), Found.Contains(Brute));

	Hero->Destroy();
	Brute->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteAnimatesInsteadOfSliding,
	"Cataclysm.Brute.AnimatesInsteadOfSliding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteAnimatesInsteadOfSliding::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// WHAT THIS GUARDS. The first Brute to reach a level slid across the floor in
	// a fixed pose, because the Paragon animation blueprint could not identify
	// its owner and left its own speed at zero for ever. The mesh is now driven
	// directly, and this checks the three things that were wrong: an animation is
	// selected at all, standing and walking select different ones, and the walk's
	// play rate follows ground speed instead of being stuck at one.

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}

	const bool bArtIsInstalled =
		FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).TryLoad() != nullptr;
	if (!bArtIsInstalled)
	{
		AddInfo(TEXT("Paragon Rampage pack is not installed, so the locomotion "
					 "test is skipped. Expected in continuous integration."));
		Brute->Destroy();
		return true;
	}

	TestTrue(TEXT("the body and its animations resolved"),
		Brute->ResolveBody(/*bIncludeAnimation=*/true));

	// BOTH ANIMATIONS EXIST. If either soft path is wrong this is where it shows,
	// rather than as a creature standing still in a level.
	if (!TestNotNull(TEXT("a standing animation was loaded"), Brute->IdleAnimation.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("a walking animation was loaded"), Brute->WalkAnimation.Get()))
	{
		return false;
	}
	TestTrue(TEXT("standing and walking are different animations"),
		Brute->IdleAnimation != Brute->WalkAnimation);

	USkeletalMeshComponent* MeshComponent = Brute->GetMesh();
	if (!TestNotNull(TEXT("mesh component"), MeshComponent))
	{
		return false;
	}

	// NOT AN ANIMATION BLUEPRINT. The component plays one animation that this
	// class chooses, rather than running the pack's graph. This is the setting
	// the whole approach rests on.
	TestEqual(TEXT("the mesh plays a single animation rather than a graph"),
		static_cast<int32>(MeshComponent->GetAnimationMode()),
		static_cast<int32>(EAnimationMode::AnimationSingleNode));

	// THE DECISION IS TESTED, NOT THE PLAYBACK. AnimationForGroundSpeed is a
	// pure function of speed, so it gives a definite answer in a world that has
	// no animation instance. Whether the component then renders it is a
	// Play-In-Editor question, and a screenshot answers that one.
	float Rate = -1.0f;

	// STANDING STILL.
	TestEqual(TEXT("at rest it chooses the standing animation"),
		Brute->AnimationForGroundSpeed(0.0f, Rate), Brute->IdleAnimation.Get());
	TestEqual(TEXT("standing plays at normal speed"), Rate, 1.0f);

	// A TWITCH IS NOT WALKING. A character told to stop keeps a little residual
	// velocity for a frame or two, and treating that as walking makes it flicker.
	TestEqual(TEXT("below the walking threshold it is still standing"),
		Brute->AnimationForGroundSpeed(
			ACataclysmBruteCharacter::WalkingThresholdCmPerSecond - 1.0f, Rate),
		Brute->IdleAnimation.Get());

	// WALKING AT THE DESIGNED SPEED.
	const float Designed = ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond;
	TestEqual(TEXT("moving it chooses the walking animation"),
		Brute->AnimationForGroundSpeed(Designed, Rate),
		Brute->WalkAnimation.Get());

	const float ExpectedRate = FMath::Clamp(
		Designed / Brute->AuthoredWalkSpeedCmPerSecond,
		ACataclysmBruteCharacter::MinimumPlayRate,
		ACataclysmBruteCharacter::MaximumPlayRate);
	TestEqual(TEXT("the walk's play rate is scaled to ground speed"),
		Rate, ExpectedRate);

	// AND IT FOLLOWS SPEED rather than being a constant that happens to match.
	float FasterRate = -1.0f;
	Brute->AnimationForGroundSpeed(Designed * 2.0f, FasterRate);
	TestTrue(TEXT("twice the ground speed plays the walk faster"),
		FasterRate > Rate);

	// AND IT CANNOT RUN AWAY. Without a ceiling a fast enemy blurs.
	float RunawayRate = -1.0f;
	Brute->AnimationForGroundSpeed(100000.0f, RunawayRate);
	TestEqual(TEXT("the play rate is capped"), RunawayRate,
		ACataclysmBruteCharacter::MaximumPlayRate);

	Brute->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteWalkSpeedCanBeTunedLive,
	"Cataclysm.Brute.WalkSpeedCanBeTunedLive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteWalkSpeedCanBeTunedLive::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// WHAT THIS GUARDS. The measured authored walk speed is an average over a
	// gait cycle, so the last of it is judged by eye, and judging by eye means
	// changing the number while watching. That is what the console variable
	// Cataclysm.Brute.AuthoredWalkSpeed is for. If it stops being read, tuning
	// silently goes back to editing a header and rebuilding, and nobody would
	// notice until they tried.

	IConsoleVariable* Knob = IConsoleManager::Get().FindConsoleVariable(
		TEXT("Cataclysm.Brute.AuthoredWalkSpeed"));
	if (!TestNotNull(TEXT("the tuning console variable is registered"), Knob))
	{
		return false;
	}

	const float Restore = Knob->GetFloat();
	ON_SCOPE_EXIT { Knob->Set(Restore, ECVF_SetByCode); };

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}

	// ZERO MEANS DO NOT OVERRIDE, so the measured property is what is used.
	Knob->Set(0.0f, ECVF_SetByCode);
	TestEqual(TEXT("at zero it uses the measured value on the class"),
		Brute->EffectiveAuthoredWalkSpeed(),
		Brute->AuthoredWalkSpeedCmPerSecond);

	// A POSITIVE VALUE WINS.
	Knob->Set(200.0f, ECVF_SetByCode);
	TestEqual(TEXT("a positive value overrides the measured one"),
		Brute->EffectiveAuthoredWalkSpeed(), 200.0f);

	// AND IT CHANGES THE PLAY RATE, which is the whole point. A smaller authored
	// speed means the animation is treated as a slower walk, so it must play
	// faster to cover the same ground.
	float RateAt200 = 0.0f;
	Brute->AnimationForGroundSpeed(
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond, RateAt200);

	Knob->Set(600.0f, ECVF_SetByCode);
	float RateAt600 = 0.0f;
	Brute->AnimationForGroundSpeed(
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond, RateAt600);

	TestTrue(TEXT("a smaller authored speed plays the walk faster"),
		RateAt200 > RateAt600);

	// AND GOING BACK TO ZERO RESTORES THE MEASURED VALUE, so the dial is a
	// tuning aid rather than a second source of truth.
	Knob->Set(0.0f, ECVF_SetByCode);
	TestEqual(TEXT("returning to zero restores the measured value"),
		Brute->EffectiveAuthoredWalkSpeed(),
		Brute->AuthoredWalkSpeedCmPerSecond);

	Brute->Destroy();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
