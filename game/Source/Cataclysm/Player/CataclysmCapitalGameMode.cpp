// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmCapitalGameMode.h"

#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
// For `DungeonMap`, the level the capital's Walk opens. Issue #2733 slice 3.
#include "Interface/CataclysmCharacterSelectWidget.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPlayLoopLog.h"
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

	// THE PLAY LOOP: the capital, with the days away when a walk has just ended -- the dungeon level's account,
	// carried on the game instance and said once -- then every dungeon standing, with what no screen shows: its kind
	// and its modifiers. The owner checks the map against these. Issue #2733 slice 4.
	const ACataclysmPlayerState* State = Players[0]->GetPlayerState<ACataclysmPlayerState>();
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::CapitalLine(Run, State ? State->GetCharacterLevel() : 0,
																  State ? State->GetExperienceIntoLevel() : 0,
																  Instance->PlayLoopAwayAccount));
	Instance->PlayLoopAwayAccount.Empty(); // A capital's: the days away said once.
	for (const FString& Line : FCataclysmPlayLoopLog::StandingLines(Run))
	{
		FCataclysmPlayLoopLog::Say(Line);
	}

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

FString ACataclysmCapitalGameMode::TravelToWalk(int32 DungeonId)
{
	// THE DUNGEON LEVEL, WITH THE DUNGEON TO WALK: the character's records written and carried, as `TravelTo` says.
	if (!TravelTo(UCataclysmCharacterSelectWidget::DungeonMap, DungeonId)) // A trip's: to walk this dungeon.
	{
		return FString::Printf(TEXT("The walk of dungeon %d could not begin: the character's records could not be "
									"written, so it stays in the capital."), DungeonId);
	}
	// THE PLAY LOOP: the dungeon chosen, named as the arrival line will list it. Issue #2733 slice 4.
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::WalkLine(
		UCataclysmGameInstance::EmpireRunFor(this, /*bStartIfNone*/ false), DungeonId));
	return FString();
}

ACataclysmCapitalGameMode* ACataclysmCapitalGameMode::InWorld(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACataclysmCapitalGameMode> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
