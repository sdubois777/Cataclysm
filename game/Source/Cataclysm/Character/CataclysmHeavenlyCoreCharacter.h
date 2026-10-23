// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmHeavenlyCoreCharacter.generated.h"

/**
 * An Ascendant's Heavenly Core: what the player destroys on the last floor of
 * the Celestial Cataclysm's Quest dungeon before its stairs will clear it.
 * Since 2026-10-22.
 *
 * `docs/Cataclysm_GDD_v2.md`, Heaven's Wrath: "The dungeon is not cleared, and
 * its quest objective not gained, until the Core is destroyed. While it stands,
 * the Edicts on the Core's floor are 50% stronger." A floor source that does
 * nothing itself, with no brain and no archetype row;
 * `ACataclysmFloorSourceCharacter` says why each, so it never attacks. Its
 * health is the floor boss's, ruled by the coordinating session that day; the
 * dungeon game mode gives it that, places it and seals the stairs while it
 * stands. Its own class so the rules and the tests can tell it from the other
 * floor sources.
 */
UCLASS()
class CATACLYSM_API ACataclysmHeavenlyCoreCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
