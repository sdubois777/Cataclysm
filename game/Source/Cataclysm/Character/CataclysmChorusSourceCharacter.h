// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmChorusSourceCharacter.generated.h"

/**
 * The source of an Eternal Chorus: what the player destroys to silence it. Issues #1820 and #41.
 *
 * "Players must destroy the source of the hymn to silence it." A floor source that does nothing, with
 * no brain and no archetype row; `ACataclysmFloorSourceCharacter` says why each. Its own class so the
 * health bar can say "Chorus" under it.
 */
UCLASS()
class CATACLYSM_API ACataclysmChorusSourceCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
