// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmInfectionBloomCharacter.generated.h"

/**
 * An infection bloom: what spreads diseased ground, sends waves while it stands, and sends a last surge when the
 * player destroys it. Issues #1820 and #41.
 *
 * "Certain areas of the dungeon are overtaken by massive, living "Infection Blooms,"..." A floor source that does
 * nothing itself, with no brain and no archetype row; `ACataclysmFloorSourceCharacter` says why each. Its patches,
 * waves and surge are the dungeon game mode's. Its own class so the health bar can say "Infection Bloom" under it,
 * which Necrotic Bloom's "Bloom" would otherwise be confused with.
 */
UCLASS()
class CATACLYSM_API ACataclysmInfectionBloomCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
