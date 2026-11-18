// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmCapitalGameMode.h"

#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"
#include "Save/CataclysmSaveWriter.h"

ACataclysmCapitalGameMode::ACataclysmCapitalGameMode()
{
	// THE ONE THING THIS LEVEL TURNS OFF, as `ACataclysmDungeonGameMode` does: the capital is the map's home, and the
	// sandbox's ring of training dummies and its creatures are for watching a fight.
	bSpawnsSandboxCreatures = false;
}

void ACataclysmCapitalGameMode::StartPlay()
{
	// THE SANDBOX'S ARRIVAL FIRST: the save writer begun, and the chosen character made or loaded when the player
	// already stands. Then the map, if that settled it.
	Super::StartPlay();
	OpenTheEmpireMapIfReady();
}

void ACataclysmCapitalGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	OpenTheEmpireMapIfReady();
}

bool ACataclysmCapitalGameMode::OpenTheEmpireMapIfReady()
{
	if (bOpenedTheEmpireMap)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UCataclysmGameInstance* Instance = World ? Cast<UCataclysmGameInstance>(World->GetGameInstance()) : nullptr;
	if (!Instance)
	{
		return false;
	}

	// NOT WHILE A LOAD IS STILL TO COME. `LoadThePendingCharacter` clears the pending creation and the pending
	// identifier before it makes or loads, so either still set means it has not run yet.
	if (Instance->PendingCreation.bPending || Instance->PendingCharacterId.IsValid())
	{
		return false;
	}

	// NOT AFTER A REFUSAL, which has asked for the front end and is leaving this level.
	if (!FrontEndRequested().IsEmpty())
	{
		return false;
	}

	// AND NOT BEFORE THE WRITER WRITES AND THE PLAYER STANDS, the load's own two conditions, so the map never opens
	// on a level the load has yet to reach.
	const UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(World);
	if (!Writer || !Writer->IsWriting())
	{
		return false;
	}
	TArray<APlayerController*> Players;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (Player && Cast<ACataclysmPlayerCharacter>(Player->GetPawn()))
		{
			Players.Add(Player);
		}
	}
	if (Players.IsEmpty())
	{
		return false;
	}

	// THE RUN THE MAP SHOWS: the made or loaded character's, or one begun now when there is none (Q2), which stands its
	// opening wave (`UCataclysmEmpireRun::FireOpeningSurge`).
	const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(this, /*bStartIfNone*/ true);

	bOpenedTheEmpireMap = true;
	UE_LOG(LogCataclysm, Log, TEXT("The capital opened the empire map: day %d, %d dungeons standing."),
		   Run ? Run->Day() : -1, Run ? Run->DungeonCount() : -1);

	// NOT IN AN AUTOMATION TEST, which has no viewport; it reads `HasOpenedTheEmpireMap`.
	if (!GIsAutomationTesting)
	{
		for (APlayerController* Player : Players)
		{
			if (ACataclysmPlayerController* Showing = Cast<ACataclysmPlayerController>(Player))
			{
				Showing->OpenEmpireMap();
			}
		}
	}
	return true;
}
