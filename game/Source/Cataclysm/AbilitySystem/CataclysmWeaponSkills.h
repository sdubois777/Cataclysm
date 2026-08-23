// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "CataclysmWeaponSkills.generated.h"

class UDataTable;

/**
 * One skill a weapon makes available, which slot it fills, and how it behaves.
 *
 * A thin reading of a row of game/Data/WeaponSkills.csv. The name and
 * description are what a player is shown; the shape says which shared template
 * runs it and the parameters are that template's numbers.
 *
 * A ROW WITH NO SHAPE IS A DESIGN THAT EXISTS AND BEHAVIOUR THAT DOES NOT. That
 * is still true of all 61 War rows, which were written before shapes existed.
 * They are granted the placeholder, which fills the slot and does nothing.
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

	/** Which template runs it. None means the row has no behaviour designed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	ECataclysmSkillShape Shape = ECataclysmSkillShape::None;

	/** That template's numbers, already parsed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	FCataclysmSkillShapeParams Params;

	/**
	 * The row's Tags cell, parsed.
	 *
	 * WHAT THEY DECIDE. Which of the character's stat modifiers reach this
	 * skill. UCataclysmStatPipeline::ModifierApplies asks whether the skill in
	 * hand carries every tag a modifier requires, so an increase scoped to
	 * Element.Demonic applies to Burning Wrath, which carries that tag, and not
	 * to a War skill, which does not.
	 *
	 * They deliberately do NOT decide which template runs the skill; the Shape
	 * column does, for the reason recorded on FCataclysmWeaponSkillRow::Shape.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	FGameplayTagContainer Tags;

	/**
	 * This skill's own base critical strike chance, or -1 to take the default.
	 *
	 * Carried from the row's Crit Chance column. See
	 * `FCataclysmWeaponSkillRow::CritChancePercent` for why -1 rather than 0
	 * means "the row says nothing". Issue #657.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Weapon Skill")
	float CritChancePercent = -1.0f;

	/**
	 * What this skill is worth, waits and costs, or -1 to take its slot's.
	 *
	 * CARRIED UNCHANGED FROM THE TABLE, INCLUDING THE -1. The fallback is
	 * applied where the ability is granted, so the slot's figure lives in one
	 * place. See `FCataclysmWeaponSkillRow::DamagePercent`.
	 */
	float DamagePercent = -1.0f;

	/** Seconds before it may be used again, or -1 to take its slot's. */
	float Cooldown = -1.0f;

	/** Mana one use costs at level 100, or -1 to take its slot's. */
	float ManaCost = -1.0f;
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

	/** The name every basic attack carries. See BasicAttackFor for why it is one name. */
	static const TCHAR* BasicAttackName;

	/**
	 * The basic attack the weapon itself supplies, read from the item base table.
	 *
	 * NOT FROM THE MATRIX, AND THAT IS THE DESIGN. The other six slots name a
	 * skill per weapon type AND damage type; this one does not vary by damage
	 * type, because the design document says the basic attack IS weapon damage.
	 * So it is one entry per weapon on the Item Bases sheet rather than 75
	 * near-identical matrix rows, and the matrix keeps holding one skill per
	 * non-basic slot exactly as the design document states. Issue #524.
	 *
	 * IT IS ALSO WHY THIS TAKES NO DAMAGE TYPE. A weapon its damage type does not
	 * cover -- a War Wand -- has no matrix skills at all and still swings, so it
	 * still gets this.
	 *
	 * ONE NAME FOR ALL THIRTEEN. The design document says the basic attack is
	 * generic and automatic rather than designed per weapon, so inventing 13
	 * names would state a design the document does not have.
	 *
	 * @param BaseTable   the ItemBases DataTable
	 * @param WeaponType  as ItemBases spells it, for example "Greatsword"
	 * @return a skill in the BasicAttack slot, or one whose Slot is None when the
	 *         weapon supplies no basic attack, which today is the Shield
	 */
	static FCataclysmWeaponSkill BasicAttackFor(const UDataTable* BaseTable,
												const FString& WeaponType);

	/** The slot named by a Slot column value, or None if it names no slot. */
	static ECataclysmAbilitySlot SlotFromName(const FString& SlotName);

	/**
	 * The ability class that runs a shape.
	 *
	 * THE ONE PLACE A SHAPE BECOMES CODE. Everything else about a skill travels
	 * as data; this function is the whole of the mapping from the Shape column
	 * to a C++ class, which is what makes adding a skill of an existing shape a
	 * workbook edit.
	 *
	 * @return null for None and for a shape with no template, and the caller
	 *         grants the placeholder instead
	 */
	static TSubclassOf<UCataclysmGameplayAbility> TemplateFor(ECataclysmSkillShape Shape);

	/** Where the imported weapon skill matrix lives. */
	static const TCHAR* TableAssetPath;

	/**
	 * The weapon skill matrix.
	 *
	 * Loads the DataTable ASSET rather than reading the CSV off disk, which is
	 * what makes this work in a packaged build. The CSV files under game/Data/
	 * are the reviewable form the workbook generates; they are not content, and
	 * that folder is not cooked, so a packaged game does not contain them.
	 * tools/generate_datatable_assets.py imports each one into /Game/Data/.
	 * Issue #150.
	 *
	 * @return the table, or null with the reason logged
	 */
	static const UDataTable* LoadGeneratedTable();
};
