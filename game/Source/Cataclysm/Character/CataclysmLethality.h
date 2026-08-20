// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmLethality.generated.h"

/**
 * Which lethality mode a character was created in.
 *
 * CHOSEN AT CREATION AND NEVER CHANGED, in either direction, and dying does not
 * change it either. That is what makes a mode worth anything: a character
 * carried its rules for its whole life and could not switch them off at the
 * moment they became inconvenient. Section "Difficulty Options" of
 * `docs/Cataclysm_GDD_v2.md`.
 *
 * **HARDCORE IS NOT PERMADEATH IN THIS DESIGN**, and that is worth saying
 * because the word means permadeath in most of this genre. Issue #315 settled
 * that nothing in this design destroys a character. Dying costs days and some of
 * what the character is wearing: 5 days in Standard, 10 in Hardcore, 15 in
 * Heretic, with each of the 18 equipped pieces dropping at 0%, 10% and 20%
 * respectively. The design document's table is the authority on those figures
 * and **none of them is built yet**.
 *
 * WHAT THIS ENUM IS FOR TODAY, AND IT IS ONE THING: deciding which account
 * record a character reads and writes. Every other consequence of the mode --
 * the day cost, what drops, whether the heads-up display is shown, how many
 * upgrade slots a city has -- is unbuilt. See `UCataclysmSavePartition`.
 *
 * THE ORDER IS THE DESIGN DOCUMENT'S ORDER, least lethal first, and the numbers
 * are written out rather than left implicit because they are persisted: a
 * character record stores this value, so renumbering the enum would silently
 * change what every existing save says its mode is.
 */
UENUM(BlueprintType)
enum class ECataclysmLethality : uint8
{
	Standard = 0	UMETA(DisplayName = "Standard"),
	Hardcore = 1	UMETA(DisplayName = "Hardcore"),
	Heretic = 2		UMETA(DisplayName = "Heretic"),
};

/**
 * Whether a character was created offline or online.
 *
 * SET AT CREATION AND NEVER CHANGED, decided by the project owner on 2026-08-14
 * against issue #528 and recorded in `docs/DECISIONS.md`: the two populations
 * are separate and nothing moves between them. It is the same shape Last Epoch
 * uses, which issue #505 adopted by name.
 *
 * IT PARTITIONS SAVES EXACTLY AS THE LETHALITY MODE DOES, so an offline Standard
 * character and an online Standard character share nothing at all -- not an
 * empire upgrade tree, not a stash.
 *
 * NUMBERED FOR THE SAME REASON THE MODES ARE. It is persisted.
 */
UENUM(BlueprintType)
enum class ECataclysmPopulation : uint8
{
	Offline = 0		UMETA(DisplayName = "Offline"),
	Online = 1		UMETA(DisplayName = "Online"),
};
