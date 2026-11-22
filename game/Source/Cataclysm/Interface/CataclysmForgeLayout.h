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

	//~ Combining gems. Issue #46, slice G6.

	/**
	 * A combine for each carried gem stack below Cataclysmic, in cell order: "Combine two Of Life (Superb) into one Of
	 * Life (Masterful), 3 held". A stack of one, or one whose next rarity is above the tier's ceiling, is still offered,
	 * as a stone level with one stone is, so that pressing it says why it cannot be done.
	 */
	static TArray<FCataclysmForgeGemCombineAction> GemCombineActionsFor(const UCataclysmInventoryComponent* Inventory);

	/** The gem combine a button stands for, among those shown. False for a button that is no gem combine. */
	static bool GemCombineActionForButton(FName ButtonName, const TArray<FCataclysmForgeGemCombineAction>& Shown,
										  FCataclysmForgeGemCombineAction& OutAction);
};
