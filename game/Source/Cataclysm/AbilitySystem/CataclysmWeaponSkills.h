// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "CataclysmWeaponSkills.generated.h"

class UDataTable;

/**
 * One skill a weapon makes available, and which slot it fills.
 *
 * A thin reading of a row of game/Data/WeaponSkills.csv. It carries the name and
 * description because those are what a player is shown; it carries no numbers,
 * because the workbook has none yet. A skill here is a design that exists, not
 * behaviour that runs.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmWeaponSkill
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::None;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	FString Description;
};

/**
 * Which skills a weapon makes available, read from the weapon skill matrix.
 *
 * WHAT DECIDES A CHARACTER'S SIX ABILITIES. The equipped weapon's TYPE and the
 * DAMAGE TYPE it rolled, together. Nothing here names an ability, and nothing in
 * C++ decides which skill fills which slot -- both come out of
 * game/Data/WeaponSkills.csv, which is generated from the Weapon Skills sheet of
 * docs/All_Things_Cataclysm.xlsx. That is what issue #36 means by the slots
 * being data-driven: changing which skill a Dagger offers is a workbook edit.
 *
 * NOT EVERY COMBINATION EXISTS, AND THAT IS THE DESIGN. The design document
 * gives each damage type its own list of weapon types. War has twelve and
 * includes neither the Wand nor the Staff, so a War Wand has no skills at all
 * and that is correct rather than missing. A caller gets an empty result and
 * must cope with it.
 *
 * THE AURA IS WEAPON-INDEPENDENT. The matrix holds one Aura skill per damage
 * type, on a row whose weapon type is "All" rather than a weapon. So a lookup
 * asks twice: once for the weapon, and once for the weapon-independent rows.
 */
UCLASS()
class CATACLYSM_API UCataclysmWeaponSkills : public UObject
{
	GENERATED_BODY()

public:
	/** The weapon type used by rows that apply whatever weapon is held. */
	static const TCHAR* WeaponIndependent;

	/**
	 * Every skill available with this weapon and damage type, at most one per
	 * slot.
	 *
	 * Rows with no skill name are skipped: the matrix states every combination
	 * so an undesigned one is visible rather than absent, and an undesigned row
	 * is not a skill.
	 *
	 * @param Table       the WeaponSkills DataTable
	 * @param WeaponType  as ItemBases spells it, for example "Greatsword"
	 * @param DamageType  for example "War"
	 */
	static TArray<FCataclysmWeaponSkill> SkillsFor(const UDataTable* Table,
												   const FString& WeaponType,
												   const FString& DamageType);

	/** The slot named by a Slot column value, or None if it names no slot. */
	static ECataclysmAbilitySlot SlotFromName(const FString& SlotName);

	/**
	 * The weapon skill matrix, built from the generated CSV in the project's
	 * Data folder.
	 *
	 * THIS IS AN EDITOR-TIME ARRANGEMENT AND WILL NOT SURVIVE PACKAGING. The
	 * generated tables live in game/Data/ as CSV files and there is no DataTable
	 * asset for any of them in Content, so there is nothing to point a soft
	 * object path at yet. Reading the file works while the project runs from its
	 * source tree, which is every way it is run today, and stops working the
	 * moment it is packaged, because that folder is not cooked. The existing
	 * automation tests in CataclysmDataTableTests.cpp read the same files the
	 * same way for the same reason. Importing the generated tables as real
	 * assets is separate work.
	 *
	 * @return a transient table owned by the caller's root set, or null with the
	 *         reason logged
	 */
	static UDataTable* LoadGeneratedTable(UObject* Outer);
};
