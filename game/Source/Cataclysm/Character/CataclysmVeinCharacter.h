// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmVeinCharacter.generated.h"

/**
 * An infested vein: what the player destroys to clear its toxic ground for a time. Issues #1820 and #41.
 *
 * "Living tunnels and walls pulsate with veins of infectious growths that create a toxic environment.
 * Players can choose to destroy these veins to temporarily cleanse the area, but destroying too much
 * summons toxic "guardians" from the infection." A floor source that does nothing itself, with no brain and
 * no archetype row; `ACataclysmFloorSourceCharacter` says why each. Its ground, its regrowth and its
 * guardians are the dungeon game mode's. Its own class so the health bar can say "Vein" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmVeinCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
