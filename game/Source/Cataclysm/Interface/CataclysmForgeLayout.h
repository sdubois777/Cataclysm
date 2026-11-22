// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmForge.h"
#include "Player/CataclysmForgeCraft.h"
#include "CataclysmForgeLayout.generated.h"

class UCataclysmInventoryComponent;
class UCataclysmEquipmentComponent;

/** One piece of gear the forge lists: where it is, the name of its button, and its words. */
struct FCataclysmForgeEntry
{
	FCataclysmForgeTarget Target;
	FName ButtonName;
	FString Label;
};

/**
 * One socket action the forge offers for the piece chosen: put a carried gem into an empty socket, or take a gem out.
 * Issue #46, slice G4.
 */
struct FCataclysmForgeSocketAction
{
	FName ButtonName;
	FString Label;
	bool bUnsocket = false;
	int32 Socket = INDEX_NONE;
	FName Gem;
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
};

/**
 * One socket the forge offers to add, or one socket's type it offers to change, for the piece chosen. Issue #46, slice
 * G9.
 */
struct FCataclysmForgeSocketWorkAction
{
	FName ButtonName;
	FString Label;
	bool bAdd = false;
	int32 Socket = INDEX_NONE;
	ECataclysmSocketType NewType = ECataclysmSocketType::Any;
};

/** One affix craft the forge offers for the piece chosen: reroll, perfect or raise the tier of one affix. #47, F1. */
struct FCataclysmForgeAffixAction
{
	FName ButtonName;
	FString Label;
	ECataclysmForgeAffixCraft Craft = ECataclysmForgeAffixCraft::RerollValue;
	int32 Affix = INDEX_NONE;
};

/**
 * One affix shift, or one step of choosing an affix to add, that the forge offers for the piece chosen. Issue #47, slice
 * F2. A shift names its affix; the chooser's steps name what they change; only `AddAffixButtonName` adds.
 */
struct FCataclysmForgeAffixChangeAction
{
	FName ButtonName;
	FString Label;
	bool bShift = false;
	int32 Affix = INDEX_NONE;
};

/** One damage-type craft the forge offers for the weapon chosen. Issue #47, slice F4. */
struct FCataclysmForgeDamageTypeAction
{
	FName ButtonName;
	FString Label;
	ECataclysmForgeDamageTypeCraft Craft = ECataclysmForgeDamageTypeCraft::InfuseRandom;

	/** The type a specific infusion adds, or the type a change replaces; None for a random infusion. */
	FName Type;
};

/**
 * One imprint the forge offers, giving up one affix, or one enchantment whose drawback it offers to change, for the piece
 * chosen. Issue #47, slice F3.
 */
struct FCataclysmForgeEnchantmentAction
{
	FName ButtonName;
	FString Label;
	bool bImprint = false;

	/** An imprint's affix given up, from 0, or a change's enchantment pair, from 0. */
	int32 Index = INDEX_NONE;

	/**
	 * What the craft's question names: "affix 2", numbered as slice F1's affix list in the screen's detail
	 * (`AffixesDetailFor`), or the enchantment's benefit as the tool tip reads it.
	 */
	FString Named;
};

/**
 * One gem combine the forge offers: two carried gems of a kind and rarity into one of the next rarity. Issue #46, slice
 * G6.
 */
struct FCataclysmForgeGemCombineAction
{
	FName ButtonName;
	FString Label;
	FName Gem;
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
};

/**
 * Every word and every list the forge's screen shows, as pure functions of the character's gear and stones, so a test
 * reads them with no widget at all. The screen, `UCataclysmForgeWidget`, lays them out. The owner's ruling of
 * 2026-11-19.
 */
UCLASS()
class CATACLYSM_API UCataclysmForgeLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The screen's title. */
	static FString Title();

	/**
	 * Every piece the forge can raise, in the order the screen lists them: what is worn, in gear slot order, then what
	 * is carried, in cell order. A material's cell and an empty one are not pieces.
	 */
	static TArray<FCataclysmForgeEntry> EntriesFor(const UCataclysmEquipmentComponent* Equipment,
												   const UCataclysmInventoryComponent* Inventory);

	/** A piece's button: "Worn, Necklace: Quality Torc of Warding +2" or "Carried: Everyday Gloves +0". */
	static FString LabelFor(const FCataclysmItem& Item, const FCataclysmForgeTarget& Target);

	/** The button name that stands for a target, "Worn_9" or "Carried_14", and back. */
	static FName ButtonNameFor(const FCataclysmForgeTarget& Target);
	static bool TargetForButton(FName ButtonName, FCataclysmForgeTarget& OutTarget);

	/**
	 * What the screen says about the piece chosen: its name and level, its residue, and what raising it takes or why
	 * it cannot be raised. "Quality Torc of Warding, +2, residue 87. Raising it to +3 takes two +3 stones (5 held), adds
	 * 25 residue (to 112) and costs 1 day of the run."
	 */
	static FString DetailFor(const FCataclysmItem& Item, const FCataclysmForgeUpgrade& Plan);

	/** The upgrade button's words: "Upgrade to +3", or "Upgrade" for a piece that cannot be raised. */
	static FString UpgradeButtonText(const FCataclysmForgeUpgrade& Plan);

	/** The stones held, every level that has any: "Upgrade stones held: +1 x16, +2 x19, +3 x5", or that none are. */
	static FString StonesLine(const UCataclysmInventoryComponent* Inventory);

	/** A combine button's words: "Combine two +2 stones into one +3". */
	static FString CombineButtonText(int32 Level);

	/** The name of the upgrade button, and of a combine button for a level, and back. */
	static FName UpgradeButtonName();
	static FName CombineButtonNameFor(int32 Level);
	static int32 CombineLevelForButton(FName ButtonName);

	/** The levels a combine button is shown for: every level from 1 to 9 with at least one stone held. */
	static TArray<int32> CombineLevelsFor(const UCataclysmInventoryComponent* Inventory);

	//~ Gems in sockets. Issue #46, slice G4.

	/**
	 * The socket actions for a piece, socket by socket: a filled socket offers to take its gem out, "Unsocket Of The
	 * Viper (Superb) from socket 2"; an empty one offers each carried gem stack that fits it, "Socket Of The Titan
	 * (Superb) into socket 1 (Defense)". Sockets are numbered from 1 to the player. A gem that fits no socket of the
	 * piece is not offered.
	 */
	static TArray<FCataclysmForgeSocketAction> SocketActionsFor(const FCataclysmItem& Item,
																const UCataclysmInventoryComponent* Inventory);

	/** The action a button stands for, among those shown. False for a button that is no socket action. */
	static bool SocketActionForButton(FName ButtonName, const TArray<FCataclysmForgeSocketAction>& Shown,
									  FCataclysmForgeSocketAction& OutAction);

	/** The piece's sockets for the screen's detail, "Sockets: Attack socket: empty; Defense socket: ...", or empty. */
	static FString SocketsDetailFor(const FCataclysmItem& Item);

	//~ Adding a socket and changing a socket's type. Issue #46, slice G9.

	/**
	 * "Add a socket (3 of 6)" for a piece below its slot's maximum `MaxSockets`, then, socket by socket, "Make socket 1
	 * Defense" and "Make socket 1 Utility" for each EMPTY socket of a type, the two types it is not. A socket holding a gem
	 * and a socket for any gem are offered no change, as `UCataclysmForge::ChangeSocketTypeOf` refuses them.
	 */
	static TArray<FCataclysmForgeSocketWorkAction> SocketWorkActionsFor(const FCataclysmItem& Item, int32 MaxSockets);

	/** The action a button stands for, among those shown. False for a button that is none of them. */
	static bool SocketWorkActionForButton(FName ButtonName, const TArray<FCataclysmForgeSocketWorkAction>& Shown,
										  FCataclysmForgeSocketWorkAction& OutAction);

	//~ The three affix crafts. Issue #47, slice F1.

	/**
	 * The piece's affixes for the screen's detail, numbered from 1 as the buttons are: "Affixes: 1. +12 to armor (tier
	 * 2), roll 50 percent; 2. ...", each read as the item's tool tip reads it; empty for a piece with none.
	 */
	static FString AffixesDetailFor(const FCataclysmItem& Item);

	/**
	 * The affix crafts for a piece, affix by affix: "Reroll affix 1", then "Perfect affix 1" while its roll is below the
	 * top, then "Raise affix 1 to tier 3" while its tier is below 7. A perfecting and an upgrade that
	 * `UCataclysmForge::AffixWorkOf` would refuse for the affix itself are not offered; one refused for the material or
	 * the gold is, so pressing it says why.
	 */
	static TArray<FCataclysmForgeAffixAction> AffixActionsFor(const FCataclysmItem& Item);

	/** The action a button stands for, among those shown. False for a button that is none of them. */
	static bool AffixActionForButton(FName ButtonName, const TArray<FCataclysmForgeAffixAction>& Shown,
									 FCataclysmForgeAffixAction& OutAction);

	//~ Adding a chosen affix, and shifting an affix. Issue #47, slice F2.

	/** The chooser's buttons: step through the affixes, lower and raise the tier, and add the one shown. */
	static FName AddAffixPreviousButtonName();
	static FName AddAffixNextButtonName();
	static FName AddAffixTierDownButtonName();
	static FName AddAffixTierUpButtonName();
	static FName AddAffixButtonName();

	/** "Shift affix 1", and so on, one for each affix of the piece. */
	static TArray<FCataclysmForgeAffixChangeAction> ShiftActionsFor(const FCataclysmItem& Item);

	/**
	 * The chooser for adding an affix, when the piece has a free slot of the four and `Addable` is not empty: "Previous
	 * affix", "Next affix", "Lower tier", "Raise tier", then "Add Flat armor at tier 3". The affix shown is
	 * `Addable[Chosen]`; `Chosen` and `Tier` are the screen's, kept within the list and 1 to 7 by the screen.
	 */
	static TArray<FCataclysmForgeAffixChangeAction> AddAffixActionsFor(const FCataclysmItem& Item,
																		const TArray<FName>& Addable, int32 Chosen,
																		int32 Tier, const UDataTable* AffixTable);

	/**
	 * The chooser's line for the screen's detail: "Affix to add: Flat armor (prefix), 3 of 32, at tier 2", or that the
	 * piece's four slots are full, or empty for a piece with no base or nothing it could take.
	 */
	static FString AddAffixDetailFor(const FCataclysmItem& Item, const TArray<FName>& Addable, int32 Chosen, int32 Tier,
									 const UDataTable* AffixTable);

	/** The action a button stands for, among those shown. False for a button that is none of them. */
	static bool AffixChangeActionForButton(FName ButtonName, const TArray<FCataclysmForgeAffixChangeAction>& Shown,
										   FCataclysmForgeAffixChangeAction& OutAction);

	//~ The three damage-type crafts. Issue #47, slice F4.

	/**
	 * A weapon's damage types for the screen's detail: "Damage types: War, Death; 2 of at most 4.", or, for the weapon
	 * made at creation, that the forge does not change them. Empty for an item that is no weapon (`MaxTypes` 0).
	 */
	static FString DamageTypesDetailFor(const FCataclysmItem& Item, int32 MaxTypes);

	/**
	 * The damage-type crafts for a weapon: below its maximum, "Infuse a random damage type" and "Infuse Death" for each
	 * type in `Open`, the types designed for it that it lacks; and, while `Open` is not empty, "Change War" for each type
	 * it carries. None for an item that is no weapon (`MaxTypes` 0) or the weapon made at creation (issue #2845).
	 */
	static TArray<FCataclysmForgeDamageTypeAction> DamageTypeActionsFor(const FCataclysmItem& Item,
																		 const TArray<FName>& Open, int32 MaxTypes);

	/** The action a button stands for, among those shown. False for a button that is none of them. */
	static bool DamageTypeActionForButton(FName ButtonName, const TArray<FCataclysmForgeDamageTypeAction>& Shown,
										  FCataclysmForgeDamageTypeAction& OutAction);

	//~ Imprinting an enchantment and changing an enchantment's drawback. Issue #47, slice F3.

	/**
	 * For a piece with every slot filled, Masterful and up, "Imprint, giving up affix 2" for each affix it holds, the
	 * player choosing which goes (the survey's Q15), numbered as slice F1's affix list in the screen's detail
	 * (`AffixesDetailFor`), which reads each affix as the tool tip does; then "Change the drawback of 10% increased attack
	 * speed" for each enchantment that is not a set's, as `UCataclysmForge::ChangeNegativeOf` refuses a set's (ruled
	 * 2026-11-22, F3-Q1). A piece below the gear level the imprint needs is still offered it, so that pressing it says
	 * what it needs, as a stone level with one stone is offered a combine.
	 */
	static TArray<FCataclysmForgeEnchantmentAction> EnchantmentActionsFor(const FCataclysmItem& Item,
																		  const UDataTable* PositiveTable,
																		  const UDataTable* NegativeTable);

	/** The action a button stands for, among those shown. False for a button that is none of them. */
	static bool EnchantmentActionForButton(FName ButtonName, const TArray<FCataclysmForgeEnchantmentAction>& Shown,
										   FCataclysmForgeEnchantmentAction& OutAction);

	//~ Combining gems. Issue #46, slice G6.

	/**
	 * A combine for each carried gem stack below Cataclysmic, in cell order: "Combine two Of Life (Superb) into one Of
	 * Life (Masterful), 3 held". A stack of one is still offered, as a stone level with one stone is, so that pressing it
	 * says why it cannot be done.
	 */
	static TArray<FCataclysmForgeGemCombineAction> GemCombineActionsFor(const UCataclysmInventoryComponent* Inventory);

	/** The gem combine a button stands for, among those shown. False for a button that is no gem combine. */
	static bool GemCombineActionForButton(FName ButtonName, const TArray<FCataclysmForgeGemCombineAction>& Shown,
										  FCataclysmForgeGemCombineAction& OutAction);
};
