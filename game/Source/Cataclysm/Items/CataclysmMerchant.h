// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmItem.h"
#include "CataclysmMerchant.generated.h"

class ACataclysmPlayerCharacter;

/**
 * The merchant at the capital: what a carried piece of gear sells for, and selling it. Issue #48, slice S1, which the
 * Auto-Loot keystone's Auto-Seller then applies (#2846, slice LF3).
 *
 * THE RULINGS OF 2026-11-22 (`.claude/windows/plan_selling_and_dismantling.md`, sections 5 and 6, recorded in
 * docs/DECISIONS.md):
 *   - the owner's O1, "Rarity and upgrade level (Recommended)": the price doubles for each rarity rung, rises 10% for
 *     each upgrade level, and is counted in the tier's gold pile size; affixes and enchantments do not count;
 *   - the owner's O2, "No days (Recommended)": selling costs no day, a departure from #48's "Every service costs time";
 *   - the owner's O5, "Screen from the empire map (Recommended)": the Merchant screen opens from a button beside the
 *     Forge's;
 *   - the coordinating session's N1, a judgement on a simulation sweep: Everyday 0.0625 piles, doubling to Cataclysmic
 *     8. If every gear drop were sold that adds 13%, 27% and 39% to a run's gold at tiers 1, 4 and 8.
 *
 * ONLY A CARRIED PIECE IS SOLD. A worn piece is never offered and cannot be named: `Sell` takes a cell of the bag. A
 * material, a gem or an empty cell is refused.
 *
 * THE BAG IS READ THROUGH THE INVENTORY COMPONENT'S OWN FUNCTIONS (`ItemAt`, `RemoveItemAt`, `SlotCount`), never its
 * slot array, so the inventory tabs being planned (#2849) have one place to change.
 */
UCLASS()
class CATACLYSM_API UCataclysmMerchant : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Gold piles a piece of each rarity sells for, Everyday to Cataclysmic: N1, doubling a rung. */
	static constexpr int32 RarityCount = 8;
	static const double SellPiles[RarityCount];

	/** How much each upgrade level adds: N2, 10% of the rarity's price a level. */
	static constexpr double UpgradeStep = 0.1;

	/** The sentences the merchant says. */
	static const TCHAR* WalkRefusal;
	static const TCHAR* NoItemRefusal;
	static const TCHAR* NoBalanceRefusal;
	static const TCHAR* NoCharacterRefusal;

	/**
	 * What a piece sells for, against one gold pile of `PileGold`: `PileGold x SellPiles[rarity] x (1 + 0.1 x gear
	 * level)`, rounded half up, and at least 1 gold. 0 for a piece that is no rarity (its affixes and enchantments make
	 * no rarity) or a pile of no gold.
	 */
	static int64 SellValueOf(const FCataclysmItem& Item, double PileGold);

	/**
	 * The gold one pile holds at a difficulty tier, with no floor's bonus: the gold table's GoldPerPile (10 at tier 1,
	 * 300 at tier 8, R10's scale). The owner's O1: "counted in the tier's gold pile size". 0 when the table is missing.
	 */
	static double PileGoldAt(int32 DifficultyTier);

	/** `SellValueOf` at a difficulty tier's pile. */
	static int64 SellValueAt(const FCataclysmItem& Item, int32 DifficultyTier);

	/** The difficulty tier the merchant prices at: the empire run's, or the lowest when there is no run. */
	static int32 TierIn(const UObject* WorldContext);

	/** The cells of the bag that hold a piece of gear, in order, read with `ItemAt`. Empty with no character. */
	static TArray<int32> CarriedPiecesOf(const ACataclysmPlayerCharacter* Character);

	/**
	 * Why selling is refused now, or empty when it is not: no character playing, or a walk under way ("The merchant
	 * trades in the capital, not during a walk."), the forge's rule (`UCataclysmForgeCraft::WalkRefusal`).
	 */
	static FString RefusalNow(const UObject* WorldContext);

	/**
	 * Sells the piece in a cell of the bag. The gold is banked FIRST, and only then is the piece taken, so nothing
	 * destroys a piece that was not paid for; if the piece could not be taken after the gold was banked, the gold is
	 * taken back. Says the PLAY LOOP line `FCataclysmPlayLoopLog::SoldLine` and raises the save triggers: the account
	 * record's when the gold went there, and the inventory's (which `RemoveItemAt` raises) for the bag.
	 *
	 * NO DEBT IS REPAID FROM A SALE, a judgement: Global Credit's "Debt repaid from next gold drops" names drops, and a
	 * sale is not one. The sale's gold goes into the balance as it is.
	 *
	 * @param OutGold     what the sale paid, 0 when refused
	 * @param OutMessage  the sentence the screen shows: what was sold, or why not
	 * @return whether the piece was sold
	 */
	static bool Sell(const UObject* WorldContext, int32 CarriedSlot, int64& OutGold, FString& OutMessage);
};
