// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CataclysmGameMode.generated.h"

/**
 * Names the pawn, controller and player state the game runs with.
 *
 * GameModeBase rather than GameMode: the match state machine GameMode adds --
 * waiting to start, in progress, waiting post match -- describes a session with
 * rounds. This game has a run that ends when the capital falls or the boss dies,
 * and the empire layer owns that. There is no round to wait for.
 *
 * Set as the project default in Config/DefaultEngine.ini rather than per level,
 * so a level opened directly still gets the real player pawn.
 */
UCLASS()
class CATACLYSM_API ACataclysmGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACataclysmGameMode();
};
