// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmSpireCharacter.generated.h"

/**
 * A Golden Spire: what the player destroys to stop it healing and strengthening the creatures near
 * it. Issues #1820 and #41.
 *
 * "Floors feature radiant towers that heal enemies and buff their damage. These spires must be
 * destroyed to progress effectively." A floor source that does nothing itself, with no brain and no
 * archetype row; `ACataclysmFloorSourceCharacter` says why each. Its heal is Field Medic's, switched on
 * by the dungeon game mode with `bHealsAlliesForTheFloorRule`, and its damage buff is the game mode's.
 * Its own class so the health bar can say "Spire" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmSpireCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
