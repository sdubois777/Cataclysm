// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPassiveTree.h"
#include "Items/CataclysmItem.h"
#include "Save/CataclysmSaveRecords.h"
#include "CataclysmPlayerBuild.generated.h"

class ACataclysmPlayerCharacter;
class ACataclysmPlayerState;

/**
 * A player character's build, as values: what it wears and every number of its own that its stat line is made from.
 * Issue #2688, slice 2a, for the corrupted double that hunts a character past the Consumption Threshold.
 *
 * THE PLAYER'S OWN NUMBERS, ruled by the coordinating session on 2026-11-02 (R3): the worn items exactly as they are,
 * with their rolled values and gear levels, and the character's own level, class, attribute points, passive points
 * and skill choices. Nothing is rescaled to a tier and nothing is rolled again.
 *
 * SAVE-READY, the owner's decision of 2026-11-02: the double is the character as it was at the crossing, so a later
 * slice captures this in `ACataclysmPlayerCharacter::WearPastTheThreshold` and keeps it in the character record. Every
 * field is `SaveGame`, the items as the record already writes them (`FCataclysmWornItem`), the rest plain values.
 * Slice 2a neither captures it nor saves it.
 *
 * THE WHOLE PASSIVE ALLOCATION, NOT A REDUCED COPY, with the points earned apart from gear beside it. What counts of the
 * allocation is decided by `UCataclysmEquipmentComponent::RefreshAttributesAs`, the one place that gathers every stat
 * source (issue #1054), from the class points the worn rows grant there; the player's own refresh decides it the same
 * way from the same figures.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmPlayerBuild
{
	GENERATED_BODY()

	/** Every worn item with the slot it is worn in. A two-handed weapon is one entry, in the first weapon slot. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	TArray<FCataclysmWornItem> Worn;

	/** The character's level. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	int32 Level = 1;

	/** A `ClassStats` class line: `UCataclysmPlayerClassStats::ChosenClass`, which is what a player's refresh applies. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	FString ClassName;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	FCataclysmAttributePoints SpentAttributePoints;

	/** The damage type chosen at creation: what a weapon with no damage types counts as carrying, and the fill order's first. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	FName CreationDamageType;

	/** The player's chosen skills, as `ACataclysmPlayerState::GetSkillAssignment` holds them. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	TArray<FCataclysmSkillChoice> SkillAssignment;

	/** Every point spent in the passive trees, as the player state holds it. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	FCataclysmPassiveAllocation PassiveAllocation;

	/**
	 * The passive points earned apart from gear: `ACataclysmPlayerState::PassivePointsEarnedWith(0)`, the level's, the
	 * bosses' and those an allocation was handed with. The class points the worn rows grant are added where the build
	 * is applied.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Build")
	int32 PassivePointsBesidesGear = 0;

	/**
	 * A live player character's build: its worn items, its state's numbers and its skill choices. What the capture at
	 * the crossing will call (a later slice). With no player state, the worn items alone, at
	 * `UCataclysmPlayerClassStats::ChosenLevel` and `ChosenClass`, as a refresh without a state applies them.
	 */
	static FCataclysmPlayerBuild Of(const ACataclysmPlayerCharacter& Player);

	/**
	 * The numbers a player state holds, and no items and no skill choices: what
	 * `UCataclysmEquipmentComponent::RefreshAttributes` reads of the player before it gathers. One function, so the
	 * player's refresh and a build taken from the player read the state in the same place.
	 */
	static FCataclysmPlayerBuild NumbersOf(const ACataclysmPlayerState& State);

	/** `Worn` in the shape `UCataclysmEquipmentComponent::RestoreWornGear` takes. */
	TArray<TPair<ECataclysmGearSlot, FCataclysmItem>> WornPairs() const;
};
