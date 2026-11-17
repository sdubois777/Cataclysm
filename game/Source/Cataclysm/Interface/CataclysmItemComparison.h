// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"

#include "CataclysmItemComparison.generated.h"

class ACataclysmComparisonStandIn;
class UAbilitySystemComponent;
class UDataTable;
struct FCataclysmCarriedSlot;

/**
 * The character's totals for one set of worn gear, as the comparison reads them: one figure per character sheet
 * stat, in `UCataclysmCharacterSheetLayout::SheetStats` order, and the basic attack's hit.
 *
 * A PLAIN STRUCT AND NOT A USTRUCT, for `FCataclysmEquipPreview`'s reason: read at once by C++, never saved.
 */
struct FCataclysmComparisonReading
{
	/** Whether anything was read. False with no stand-in ability system to gather onto. */
	bool bRead = false;

	/** One figure per sheet stat, `UCataclysmCharacterSheetLayout::FigureFor` each. */
	TArray<float> Figures;

	/** `UCataclysmItemComparison::BasicHitOf` for this gear. Nought with no weapon that swings. */
	float BasicHit = 0.0f;
};

/** What comparing a carried item with what it would replace found. A plain struct, for the same reason. */
struct FCataclysmComparisonResult
{
	/** Whether a comparison was made. False for an item `Equip` would refuse, or with nothing to gather onto. */
	bool bCompared = false;

	/** Where the item would go, as `Equip` would choose it. */
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;

	/** What would come off, in `Equip`'s order. Empty when the slot is free: "compared with nothing". */
	TArray<FCataclysmItem> Replaced;

	/** One line per total that would change, in the sheet's order, then the basic attack's hit when it would. */
	TArray<FString> ChangeLines;

	/** The basic attack's hit now and with the item worn, the figures behind its line. */
	float BasicHitNow = 0.0f;
	float BasicHitWith = 0.0f;
};

/**
 * A carried item compared with what wearing it would replace. Issue #832.
 *
 * THE QUESTION A PLAYER ASKS IS "IF I PUT THIS ON, WHAT CHANGES?", and the answer here is the character's own totals,
 * before and after, rather than the two items' lines set against each other. Two helms of one base rarely carry the
 * same stats -- one has flat health, the other increased armour -- so a stat-by-stat table of the items has nothing to
 * line up, while the character's health and armour always do. The worn piece's own lines are still shown beside, so
 * a player can read what it is.
 *
 * WHAT IS COMPARED WITH WHAT, ruled by the coordinating session on 2026-11-02 and recorded in `docs/DECISIONS.md`:
 * the piece `UCataclysmEquipmentComponent::Equip` would actually replace (`PreviewEquip`), so a ring is compared with
 * the ring the equip action would take off, a free slot is "compared with nothing", and a two-handed weapon over two
 * one-handed weapons is compared with both.
 *
 * HOW THE TOTALS ARE FOUND: the worn set and the set with the item swapped in are each restored onto an equipment
 * component that belongs to no actor and gathered by `RefreshAttributesAs`, the one gatherer, onto
 * `ACataclysmComparisonStandIn`, with the player's numbers and the floor's lines. Nothing is written to the player.
 *
 * WHY IT IS SEPARATE FROM THE WIDGET, for `UCataclysmItemTooltip`'s reason: the automation run passes `-nullrhi`, so
 * what a widget draws cannot be watched, and everything here can be.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemComparison : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The name of the one line that is not a sheet stat: the basic attack's hit. */
	static const TCHAR* BasicHitLineName;

	/** What the comparison says when no total would move. */
	static const TCHAR* NothingChangesLine;

	/**
	 * One sheet stat's change, "Health: 606 to 726, up 120", or an empty string when it would not change.
	 *
	 * WHETHER IT CHANGES IS DECIDED BY THE FIGURES AS SHOWN, `UCataclysmCharacterSheetLayout::FigureText`, not by the
	 * two numbers: a movement too small to change what the sheet prints is not listed, so the comparison never says
	 * "606 to 606".
	 */
	static FString ChangeLine(const FString& Stat, float Now, float With);

	/** `ChangeLine` for a line with its own name, written as a percentage or as a plain figure. */
	static FString ChangeLineNamed(const FString& Name, bool bPercentage, float Now, float With);

	/** Every line that changes between two readings: the sheet's stats in order, then the basic attack's hit. */
	static TArray<FString> ChangeLines(const FCataclysmComparisonReading& Now, const FCataclysmComparisonReading& With);

	/**
	 * What one basic attack of this gear is worth before the target's defences: ruling (e) of 2026-11-02.
	 *
	 * THE GAME'S OWN NO-TARGET PRICE, `UCataclysmSkillEffects::SkillHitWithNoTarget`: `ApplyHit`'s bracket with every
	 * fact about an enemy left at "not known", which is what a self hit, a copied blow and a ground patch are priced
	 * by. Handed the basic attack the first worn weapon supplies, with the element tag it would be granted with
	 * (`UCataclysmWeaponSlotsComponent::BasicAttackCarrying`) and its damage percent, its own or else its slot's, as
	 * `UCataclysmSkillTemplate::GetDamagePercent` takes it.
	 *
	 * NOT COUNTED, because a hit with no enemy in it cannot know them: a row about the enemy struck, increased damage
	 * against its type, a critical strike, and what the moment holds -- a stored blow, a cut on the attacker, a charge
	 * bought.
	 *
	 * @param AbilitySystem       an ability system already refreshed for this gear
	 * @param Wearing             the gear, for its weapons
	 * @param CreationDamageType  the player's, which a weapon with no damage types counts as carrying
	 * @return nought when no worn weapon supplies a basic attack
	 */
	static float BasicHitOf(const UAbilitySystemComponent* AbilitySystem, const UCataclysmEquipmentComponent& Wearing,
							FName CreationDamageType);

	/**
	 * The totals for one set of worn slots, gathered onto the stand-in: the slots restored as they are listed onto an
	 * equipment component that belongs to no actor, with the live component's switched-off slot, the player's numbers
	 * (`UCataclysmEquipmentComponent::PlayerNumbers`) and the floor's stat lines (the live ability system's
	 * `GetDungeonStatModifiers`).
	 */
	static FCataclysmComparisonReading ReadingOf(const TArray<FCataclysmItem>& WornSlots,
												 const UCataclysmEquipmentComponent& Live,
												 const UAbilitySystemComponent* LiveAbilities,
												 ACataclysmComparisonStandIn& StandIn);

	/**
	 * Compares a carried item with what wearing it would replace.
	 *
	 * @param Live           the character's equipment. NOT CONST because `PreviewEquip` runs `Equip` on it and puts
	 *                       everything back; nothing worn changes and nothing is announced.
	 * @param LiveAbilities  the character's ability system, for the floor's lines; may be null
	 * @param Now            the reading of what is worn now, when the caller has already taken it for another item.
	 *                       Null reads it here. A screen comparing every carried item reads it once.
	 */
	static FCataclysmComparisonResult Compare(const FCataclysmItem& Hovered, UCataclysmEquipmentComponent& Live,
											  const UAbilitySystemComponent* LiveAbilities,
											  ACataclysmComparisonStandIn& StandIn,
											  const FCataclysmComparisonReading* Now = nullptr);

	/**
	 * The line above the change lines: "If worn, in place of Iron Helm:", "If worn, in place of Sword and Dagger:", or
	 * "If worn in Ring 4:" for a free slot.
	 *
	 * EACH PIECE BY THE NAME THE PLAYER SEES FOR IT: its whole name (`UCataclysmItemName::NameOf`), or, for a piece
	 * with no rarity and so no whole name -- no affix and no enchantment, as the starting weapon is -- its base's
	 * `BaseName`, as its inventory cell calls it. Never the row key while the base is in the table.
	 */
	static FString HeadingFor(const FCataclysmComparisonResult& Result, const UDataTable* BaseTable,
							  const UDataTable* AffixTable);

	/**
	 * The tooltip's two columns.
	 *
	 * LEFT: the hovered item's own lines (`UCataclysmItemTooltip::LinesFor`), then `HeadingFor` and the change lines,
	 * or `NothingChangesLine`. RIGHT: "Worn now:" and the lines of each piece that would come off, or "Compared with
	 * nothing: Ring 4 is empty." With no comparison, the left is the item's lines alone and the right is empty.
	 */
	static void Columns(const FCataclysmCarriedSlot& Hovered, const FCataclysmComparisonResult& Result,
						const UDataTable* BaseTable, const UDataTable* AffixTable,
						const UDataTable* CraftingMaterialTable, const UDataTable* PositiveEnchantmentTable,
						const UDataTable* NegativeEnchantmentTable, FString& OutLeft, FString& OutRight);
};
