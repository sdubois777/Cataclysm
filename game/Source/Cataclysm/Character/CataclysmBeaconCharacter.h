// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmBeaconCharacter.generated.h"

/**
 * A plague beacon: what the player destroys so that it does not strengthen the creatures of later
 * floors. Issues #1820 and #41.
 *
 * "Players must find and destroy the plague beacons on each floor if they want to lower the power of
 * enemies on later floors." A floor source that does nothing itself, with no brain and no archetype row;
 * `ACataclysmFloorSourceCharacter` says why each. What a beacon left standing does is the dungeon game
 * mode's. Its own class so the health bar can say "Beacon" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmBeaconCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
