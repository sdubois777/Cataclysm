// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmRiftCharacter.generated.h"

/**
 * An Abyssal Rift: what opens when the player comes near, sends waves, and closes when they are killed in time.
 * Issues #1820 and #41.
 *
 * "Portals to the Abyss open up, unleashing waves of demonic creatures. Players must close these rifts by defeating
 * waves of enemies within a given time limit." A floor source that does nothing itself, with no brain and no
 * archetype row; `ACataclysmFloorSourceCharacter` says why each. The dungeon game mode makes it one that cannot be
 * hurt, and its opening, waves and closing are the game mode's. Its own class so the health bar can say "Rift".
 */
UCLASS()
class CATACLYSM_API ACataclysmRiftCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
