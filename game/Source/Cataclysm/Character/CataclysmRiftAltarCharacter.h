// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "CataclysmRiftAltarCharacter.generated.h"

/**
 * One of the four altars powering a Rift, the Demonic Cataclysm's Quest dungeon: what the player destroys to seal it.
 * Issue #51 slice B1, since 2026-11-24.
 *
 * `docs/Cataclysm_GDD_v2.md` section XI, Hell on Earth: "Players must assault Rifts, destroy the four altars powering
 * each one, and seal 10 Rifts to challenge the enemy capital." The owner, 2026-11-24 (issue #51, Q2): "An altar does
 * not attack, has the floor boss's health, and seals its floor's stairs while it stands, as the Heavenly Core does."
 *
 * A FLOOR SOURCE, AS THE HEAVENLY CORE IS, and for its reasons: `ACataclysmFloorSourceCharacter` says why it has no
 * brain and no archetype row, so it never attacks, and it wears the base enemy's placeholder cylinder, the Core's
 * placeholder, which is engine content and not art. The dungeon game mode gives it the floor boss's health, places it
 * and seals the stairs while it stands (`ACataclysmDungeonGameMode::PlaceTheRiftAltars`). Its own class so the rules,
 * the health bar's word and the tests can tell it from the other floor sources.
 *
 * NOT `ACataclysmRiftCharacter`, which is the Void's Abyssal Rifts modifier's rift and has nothing to do with this.
 */
UCLASS()
class CATACLYSM_API ACataclysmRiftAltarCharacter : public ACataclysmFloorSourceCharacter
{
	GENERATED_BODY()
};
