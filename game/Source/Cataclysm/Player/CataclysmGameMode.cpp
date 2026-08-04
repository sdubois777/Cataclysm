// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"

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

		// After spawning, because BeginPlay is what wires the ability system up
		// and the attribute cannot be written before that.
		Dummy->SetHealth(TrainingDummyHealth);

		TrainingDummies.Add(Dummy);
		++Spawned;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d training dummies in a ring of %.0f cm around %s."),
		Spawned, TrainingDummyRingRadius, *Centre.ToCompactString());

	return Spawned;
}
