// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmBloomCharacter.generated.h"

/**
 * A Necrotic Bloom's cursed flower: what the player destroys to stop its waves. Issues #1820 and #41.
 *
 * "Cursed flowers sprout in random areas; if not destroyed, they spawn waves of undead every 20s." A
 * floor source that does nothing, with no brain and no archetype row; `ACataclysmFloorSourceCharacter`
 * says why each. The waves are the dungeon game mode's, not the flower's. Its own class so the health
 * bar can say "Bloom" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmBloomCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
