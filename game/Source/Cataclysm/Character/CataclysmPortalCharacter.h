// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmPortalCharacter.generated.h"

/**
 * A Portal Unleashing portal: what keeps sending creatures for as long as the floor lasts. Issues #1820 and #41.
 *
 * "The dungeon is riddled with unstable portals that periodically spawn twisted abominations from the void.
 * Players must swiftly dispatch these creatures before they overwhelm the party." A floor source that does
 * nothing itself, with no brain and no archetype row; `ACataclysmFloorSourceCharacter` says why each. The
 * dungeon game mode makes it one that cannot be hurt, and what it sends is the game mode's. Its own class so
 * the health bar can say "Portal" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmPortalCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
