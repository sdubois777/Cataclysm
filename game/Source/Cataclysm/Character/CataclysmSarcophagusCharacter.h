// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmSarcophagusCharacter.generated.h"

/**
 * An obsidian sarcophagus: a coffin that strengthens the creatures near it and, once enough of them have died
 * beside it, lets out a Vampire Lord. Issues #1820 and #41.
 *
 * "Indestructible coffins pulse with death magic, granting enemies in range bonus damage and resistance. Once
 * enough nearby enemies have been slain, a Vampire Lord erupts from the coffin to kill the player." A floor
 * source that does nothing itself, with no brain and no archetype row; `ACataclysmFloorSourceCharacter` says
 * why each. The dungeon game mode makes it one that cannot be hurt, and its bonuses, its count and its lord are
 * the game mode's. Its own class so the health bar can say "Sarcophagus" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmSarcophagusCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
