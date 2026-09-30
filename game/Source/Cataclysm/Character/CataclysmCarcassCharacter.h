// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmCarcassCharacter.generated.h"

/**
 * A Carrion Feast carcass: the body a slain creature leaves, which becomes a carrion feeder unless it is burned.
 * Issues #1820 and #41.
 *
 * "Rotting carcasses attract swarms of carrion feeders that consume the bodies, growing stronger and more numerous
 * with each corpse. Players can prevent this by burning bodies with fire-based abilities." A floor source that does
 * nothing itself, with no brain and no archetype row; `ACataclysmFloorSourceCharacter` says why each. The dungeon
 * game mode makes it one that cannot be hurt, and burning it and what it becomes are the game mode's. Its own class
 * so the health bar can say "Carcass" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmCarcassCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
