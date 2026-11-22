// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmLootFilter.h"
#include "CataclysmLootFilterLayout.generated.h"

class UDataTable;

/** One button the rule editor shows: what a click sends, what it says, and whether it shows as chosen. */
struct CATACLYSM_API FCataclysmLootEditorButton
{
	/** What a click sends to `UCataclysmLootFilterLayout::EditRule`, e.g. "Rarity.2" or "GearMin+". */
	FString Command;

	/** What the button says, e.g. "Superb" or "Lowest gear level +". */
	FString Label;

	/** Whether the button shows as chosen: a rarity, slot, affix or damage type the rule names, or a cycle's setting. */
	bool bChosen = false;
};

/**
 * The loot filter screen's words and its rule editor, as plain functions. Issue #2846, slice LF2.
 *
 * WHY THE EDITOR IS HERE AND NOT IN THE WIDGET: a headless test has no Widget Blueprint, so the screen's panels and
 * buttons never exist there. Every edit a button makes is `EditRule` on a command string, and every button the
 * editor shows is `EditorButtonsFor`, so a test can press any button and read any label.
 *
 * A FIRST VERSION, as the coordinating session asked (the owner's rule that every system has a basic screen): a list
 * of filters, a list of rules, and an editor of buttons, each condition and action a toggle, a cycle or a step.
 */
UCLASS()
class CATACLYSM_API UCataclysmLootFilterLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The base slots a rule can name, in the equipment's order: "Head" ... "Weapon". */
	static TArray<FString> SlotNames();

	/** A rarity's name as the game shows it: "Everyday" ... "Cataclysmic". */
	static FString RarityName(ECataclysmRarity Rarity);

	/** The filter list's line for a filter: its name, "(built in)" for a built-in, "(active)" for the active one. */
	static FString FilterLine(const FString& Name, bool bBuiltIn, bool bActive, int32 RuleCount);

	/**
	 * A rule as one line in the rule list: "1. Hide gear: Everyday or Quality; below what is worn". Each condition the
	 * rule uses, in the order the editor shows them; "everything" when it uses none.
	 */
	static FString RuleLine(const FCataclysmLootFilterRule& Rule, int32 Index, const UDataTable* AffixTable);

	/**
	 * Changes one field of a rule as the editor's button `Command` says. Returns whether `Command` was one the editor
	 * knows; a step past a field's end leaves it where it is and still answers true.
	 *
	 *   "Action"              cycles Show, Hide, Restyle
	 *   "Kind"                cycles Any, Gear, Crafting material, Gold, Gems
	 *   "Rarity.N"            adds or removes rarity N (0 Everyday ... 7 Cataclysmic)
	 *   "BelowWorn"           turns "below what is worn" on or off
	 *   "TierRungs+" / "-"    "at least N rungs below the tier's rarity", 0 (off) to 7
	 *   "Slot.NAME"           adds or removes a slot
	 *   "GearMin+" / "-", "GearMax+" / "-"        gear level, 0 to 10, the lowest never above the highest
	 *   "Affix.ROW"           adds or removes an affix row
	 *   "AffixTier+" / "-"    1 to 7;  "AffixCount+" / "-"   1 to 4
	 *   "Enchant"             cycles Either, Yes, No
	 *   "SocketsMin+" / "-", "SocketsMax+" / "-"  0 to 6, the lowest never above the highest
	 *   "Damage.NAME"         adds or removes a damage type
	 *   "MatMin+" / "-", "MatMax+" / "-"          a material's tier, 1 to 5, the lowest never above the highest
	 *   "Text+" / "-", "Back+" / "-"              a palette entry, or none, cycling through the 12
	 *   "Size+" / "-"         0 to 2
	 */
	static bool EditRule(FCataclysmLootFilterRule& Rule, const FString& Command);

	/**
	 * Every button the editor shows for a rule, in order: the action, the kind, the eight rarities, below-worn, the
	 * tier rungs, the eleven slots, gear level, the affix chooser (the affix at `AffixPick` of `AffixRows`, and the
	 * rule's own affixes), affix tier and count, enchantment, sockets, the damage types, material tier, and, for a
	 * restyle, the text colour, background and size.
	 */
	static TArray<FCataclysmLootEditorButton> EditorButtonsFor(const FCataclysmLootFilterRule& Rule,
															   const TArray<FName>& AffixRows, int32 AffixPick,
															   const UDataTable* AffixTable);

	/** A palette entry's name: "Everyday" ... "Cataclysmic", "Yellow", "Cyan", "Pink", "Black", or "None". */
	static FString PaletteName(int32 Index);
};
