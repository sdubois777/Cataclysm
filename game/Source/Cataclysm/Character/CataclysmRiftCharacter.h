// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmRiftCharacter.generated.h"

/**
 * An Abyssal Rift: what sends a hunter every three seconds from the floor's start until the player kills it. Issues
 * #1820, #41 and #2864.
 *
 * Since the owner's redesign of 2026-11-23 (#2864) a rift is a Boss the player can target and kill: the dungeon game
 * mode gives it the Boss rung and much more health, and its hunters, their strength and its death are the game mode's.
 * It is still a floor source that does nothing itself, with no brain and no archetype row;
 * `ACataclysmFloorSourceCharacter` says why each. Its own class so the health bar can say "Rift".
 */
UCLASS()
class CATACLYSM_API ACataclysmRiftCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
