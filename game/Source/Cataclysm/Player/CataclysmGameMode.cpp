// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Cataclysm.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

/**
 * Play at a difficulty tier without editing the game mode's default.
 *
 * ZERO MEANS "USE THE GAME MODE'S", the same shape as the Brute's two cooldown
 * overrides in `CataclysmBruteCharacter.cpp`. A sentinel rather than a second
 * copy of the default, so the real figure has one home.
 *
 * WHAT IT IS FOR. The tier decides what armour is worth and nothing else, and
 * the difference is large: the Abyssal Warden's 5,954 armour removes 75% of a
 * hit at tier 1 against 48.19% at tier 8. Switching while standing in the
 * sandbox is how that gets judged by play rather than by arithmetic.
 */
static TAutoConsoleVariable<int32> CVarDifficultyTier(
	TEXT("Cataclysm.DifficultyTier"),
	0,
	TEXT("The difficulty tier every hit resolves at, 1 to 8. 0 uses the game "
		 "mode's own DifficultyTier. It decides what armour is worth and "
		 "nothing else: armour removes armor / (armor + 800 x tier) of a hit, "
		 "capped at 75 percent, so the same armour is worth far more at tier 1."),
	ECVF_Default);

int32 ACataclysmGameMode::DifficultyTierFor(const ACataclysmGameMode* Mode)
{
	// THE CONSOLE VARIABLE WINS, so a tier chosen while playing takes effect on
	// the next hit rather than on the next launch.
	const int32 Override = CVarDifficultyTier.GetValueOnAnyThread();
	if (Override > 0)
	{
		return FMath::Clamp(Override, LowestDifficultyTier,
							HighestDifficultyTier);
	}

	if (Mode)
	{
		return FMath::Clamp(Mode->DifficultyTier, LowestDifficultyTier,
							HighestDifficultyTier);
	}

	// No game mode. Every automation test builds a bare world, and a hit
	// resolved there still has to produce a number.
	return LowestDifficultyTier;
}

int32 ACataclysmGameMode::DifficultyTierIn(const UObject* WorldContext)
{
	const ACataclysmGameMode* Mode = nullptr;
	if (WorldContext && GEngine)
	{
		if (const UWorld* World = GEngine->GetWorldFromContextObject(
				WorldContext, EGetWorldErrorMode::ReturnNull))
		{
			Mode = World->GetAuthGameMode<ACataclysmGameMode>();
		}
	}

	return DifficultyTierFor(Mode);
}

ACataclysmGameMode::ACataclysmGameMode()
{
	DefaultPawnClass = ACataclysmPlayerCharacter::StaticClass();
	PlayerControllerClass = ACataclysmPlayerController::StaticClass();

	// The player state is where the ability system component lives, so naming it
	// here is not optional dressing: with the default APlayerState the pawn finds
	// no ability system, InitAbilityActorInfo returns early, and the character
	// spawns with no attributes and no abilities and no error.
	PlayerStateClass = ACataclysmPlayerState::StaticClass();
}

void ACataclysmGameMode::StartPlay()
{
	Super::StartPlay();

	// After the player start exists and before anything can be pressed. Spawning
	// from here rather than from the level means the sandbox's contents are
	// reviewable text rather than bytes inside L_Sandbox.umap.
	SpawnTrainingDummies();
	SpawnBrutes();
	SpawnAbyssalWardens();
}

int32 ACataclysmGameMode::SpawnAbyssalWardens()
{
	UWorld* World = GetWorld();
	if (!World || AbyssalWardenCount <= 0)
	{
		return 0;
	}

	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY THE DIFFERENCE IN CAPSULE HALF-HEIGHT, the same correction
	// SpawnBrutes makes. The player start sits at the height a default capsule
	// needs, and this creature's is 34 cm taller than the base enemy's, so
	// spawning at the same height buries its feet until the movement component
	// pushes it out.
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
	const float RiseCm =
		ACataclysmAbyssalWardenCharacter::WardenCapsuleHalfHeight
		- BaseEnemyCapsuleHalfHeight;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < AbyssalWardenCount; ++Index)
	{
		// Spread around the same centre when there is more than one, and
		// directly in front when there is one.
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(FMath::Max(AbyssalWardenCount, 1));
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * AbyssalWardenDistanceCm,
			FMath::Sin(Angle) * AbyssalWardenDistanceCm,
			RiseCm);

		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmAbyssalWardenCharacter* Warden =
			World->SpawnActor<ACataclysmAbyssalWardenCharacter>(
				ACataclysmAbyssalWardenCharacter::StaticClass(), Where, Facing,
				SpawnParams);
		if (!Warden)
		{
			continue;
		}

		Warden->SetHealth(AbyssalWardenHealth);
		Warden->SetAttackDamage(AbyssalWardenAttackDamage);

		AbyssalWardens.Add(Warden);
		++Spawned;
	}

	// THE LOG SAYS IT CANNOT CHASE, because that is the first thing anybody
	// watching it will notice and it is designed rather than broken.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d Abyssal Wardens %.0f cm from %s. Each has %.0f health, "
			 "hits for %.0f every %.1f s, walks at %.0f cm/s and never runs. "
			 "Its Molten Roar marks a %.0f cm ring every %.0f s. It has no "
			 "charge and cannot close on a player who walks away: issue #491."),
		Spawned, AbyssalWardenDistanceCm, *Centre.ToCompactString(),
		AbyssalWardenHealth, AbyssalWardenAttackDamage,
		ACataclysmAbyssalWardenCharacter::DesignedAttackIntervalSeconds,
		ACataclysmAbyssalWardenCharacter::DesignedWalkSpeedCmPerSecond,
		ACataclysmAbyssalWardenCharacter::MoltenRoarRadiusCm,
		ACataclysmAbyssalWardenCharacter::MoltenRoarCooldownSeconds);

	return Spawned;
}

int32 ACataclysmGameMode::SpawnBrutes()
{
	UWorld* World = GetWorld();
	if (!World || BruteCount <= 0)
	{
		return 0;
	}

	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY THE DIFFERENCE IN CAPSULE HALF-HEIGHT. The player start sits at
	// the height a default capsule needs, and the Brute's is 30 cm taller than
	// the base enemy's, so spawning at the same Z buries its feet in the floor
	// until the movement component pushes it out.
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;
	const float RiseCm =
		ACataclysmBruteCharacter::BruteCapsuleHalfHeight - BaseEnemyCapsuleHalfHeight;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < BruteCount; ++Index)
	{
		// Spread around the same centre when there is more than one, and
		// directly in front when there is one.
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(FMath::Max(BruteCount, 1));
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * BruteDistanceCm,
			FMath::Sin(Angle) * BruteDistanceCm,
			RiseCm);

		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
			ACataclysmBruteCharacter::StaticClass(), Where, Facing, SpawnParams);
		if (!Brute)
		{
			continue;
		}

		Brute->SetHealth(BruteHealth);
		Brute->SetAttackDamage(BruteAttackDamage);

		Brutes.Add(Brute);
		++Spawned;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d Brutes %.0f cm from %s. Each has %.0f health, hits for "
			 "%.0f every %.1f s, walks at %.0f cm/s and turns at %.0f deg/s."),
		Spawned, BruteDistanceCm, *Centre.ToCompactString(),
		BruteHealth, BruteAttackDamage,
		ACataclysmBruteCharacter::DesignedAttackIntervalSeconds,
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond,
		ACataclysmBruteCharacter::DesignedTurnRateDegreesPerSecond);

	return Spawned;
}

int32 ACataclysmGameMode::SpawnTrainingDummies()
{
	UWorld* World = GetWorld();
	if (!World || TrainingDummyCount <= 0)
	{
		return 0;
	}

	// Around the player start rather than around the world origin, so the ring
	// is where the player actually appears whatever the level looks like. The
	// origin is the fallback when a level has no player start at all.
	FVector Centre = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Centre = It->GetActorLocation();
		break;
	}

	FActorSpawnParameters SpawnParams;

	// AlwaysSpawn, because a dummy that overlaps the floor or another dummy
	// should still exist. The alternative silently produces fewer than asked
	// for, and a sandbox that sometimes has four enemies and sometimes five is
	// worse than one that always has five.
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < TrainingDummyCount; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index)
						  / static_cast<float>(TrainingDummyCount);
		const FVector Where = Centre + FVector(
			FMath::Cos(Angle) * TrainingDummyRingRadius,
			FMath::Sin(Angle) * TrainingDummyRingRadius,
			0.0f);

		// Facing the centre, so a ring of them looks deliberate rather than
		// scattered.
		const FRotator Facing = (Centre - Where).Rotation();

		ACataclysmEnemyCharacter* Dummy = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), Where, Facing, SpawnParams);
		if (!Dummy)
		{
			continue;
		}

		// Both remembered and applied, so it does not matter whether the ability
		// system has been wired up yet. See ACataclysmEnemyCharacter::SetHealth.
		Dummy->SetHealth(TrainingDummyHealth);
		Dummy->SetAttackDamage(TrainingDummyAttackDamage);

		TrainingDummies.Add(Dummy);
		++Spawned;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d training dummies in a ring of %.0f cm around %s. "
			 "Each has %.0f health and hits for %.0f."),
		Spawned, TrainingDummyRingRadius, *Centre.ToCompactString(),
		TrainingDummyHealth, TrainingDummyAttackDamage);

	return Spawned;
}
