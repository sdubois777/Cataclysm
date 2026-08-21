// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonGameMode.h"

#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/**
	 * The dungeon seed to walk, when set above zero.
	 *
	 * TYPED AT THE CONSOLE RATHER THAN EDITED, so a person can look at another
	 * dungeon without changing a default and rebuilding. Zero means "use the
	 * game mode's own setting", which is why it is not simply the seed.
	 *
	 * NAMED FOR THIS FILE. Unreal merges a module's `.cpp` files into one
	 * translation unit, so two files declaring the same file-scope name collide,
	 * and only once both are committed. The project names console variables after
	 * what owns them for exactly that reason.
	 */
	static int32 GCataclysmDungeonSeedOverride = 0;
	static FAutoConsoleVariableRef CVarCataclysmDungeonSeed(
		TEXT("Cataclysm.DungeonSeed"),
		GCataclysmDungeonSeedOverride,
		TEXT("Which dungeon to generate. 0 uses the game mode's own setting."),
		ECVF_Default);

	/** How far above the walking surface a pawn's capsule middle has to sit. */
	float DungeonGameModeStandingHeightOf(const APawn* Pawn)
	{
		if (const UCapsuleComponent* Capsule =
				Pawn ? Pawn->FindComponentByClass<UCapsuleComponent>() : nullptr)
		{
			return Capsule->GetScaledCapsuleHalfHeight();
		}

		// A pawn with no capsule is not a character. Placing it exactly on the
		// surface is the honest answer: there is no half height to raise it by.
		return 0.0f;
	}
}

ACataclysmDungeonGameMode::ACataclysmDungeonGameMode()
{
	// The one thing this game mode turns off. See the class comment.
	bSpawnsSandboxCreatures = false;
}

void ACataclysmDungeonGameMode::StartPlay()
{
	// The floor is built BEFORE `Super::StartPlay`, and the order matters. The
	// parent starts the save writer, which records the floor being stood on, and
	// a floor that does not exist yet is one the record cannot describe.
	BuildFloor();

	Super::StartPlay();

	// AND THE PLAYER IS MOVED AFTER, because the pawn is created during login,
	// which the parent's `StartPlay` is downstream of. Moved rather than spawned
	// there: the player start is wherever the map put it, and where the player
	// arrives is decided by the generator and is different every floor.
	if (const APlayerController* Controller =
			GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PlaceAtEntrance(Controller->GetPawn());
	}
}

ACataclysmDungeonFloor* ACataclysmDungeonGameMode::BuildFloor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (!CurrentFloor)
	{
		CurrentFloor = World->SpawnActor<ACataclysmDungeonFloor>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	}
	if (!CurrentFloor)
	{
		return nullptr;
	}

	FCataclysmFloorRequest Request;
	Request.DungeonSeed = (GCataclysmDungeonSeedOverride > 0)
		? GCataclysmDungeonSeedOverride : DungeonSeed;
	Request.FloorNumber = FMath::Max(1, FloorNumber);
	Request.Layout = Layout;

	if (!CurrentFloor->Build(FCataclysmFloorGenerator::Generate(Request)))
	{
		return nullptr;
	}

	return CurrentFloor;
}

bool ACataclysmDungeonGameMode::PlaceAtEntrance(APawn* Pawn)
{
	if (!Pawn || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return false;
	}

	const FVector Standing = CurrentFloor->EntranceWorld()
		+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOf(Pawn));

	// SWEEP OFF. The pawn is being put somewhere it is not, across a floor that
	// may be a hundred metres away, and a swept move would stop at the first wall
	// between here and there and leave the player inside it.
	return Pawn->TeleportTo(Standing, Pawn->GetActorRotation(),
						    /*bIsATest=*/false, /*bNoCheck=*/true);
}
