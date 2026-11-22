// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "CataclysmCityUpgradePurchase.generated.h"

class UCataclysmEmpireRun;

/**
 * Buying a city upgrade with gold: its price, whether the character playing can pay it, and the purchase. Issue
 * #2742's second slice, ruled by the owner on 2026-11-18 ("city upgrades are the first thing it buys") and priced by
 * the coordinating session's G4.
 *
 * WHY THIS IS HERE AND NOT ON THE RUN. The rules of a purchase are `UCataclysmEmpireRun::WouldBuyCityUpgrade`'s, in
 * `CataclysmEmpire`, which may not depend on this module; and the price (a DataTable) and the gold (the account record
 * or a Solo Self-Found character's player state) are both this module's. So this reads them, asks the run with both
 * numbers, and on `Bought` takes the gold and raises the saves. The city screen and the `Cataclysm.BuyCityUpgrade`
 * console command both buy through `Buy`; nothing else charges.
 *
 * THE GOLD IS TAKEN BEFORE THE RUN RECORDS THE UPGRADE, and given back if the run then refuses, so no upgrade is ever
 * held unpaid. The run refuses only what `WouldBuyCityUpgrade` refused a moment before, so the giving back is a guard.
 */
UCLASS()
class CATACLYSM_API UCataclysmCityUpgradePurchase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * What the upgrade costs in this run: `UCataclysmCityUpgradeMapping::PriceFor` at the run's difficulty tier, the
	 * tier every level of the run copies (`ACataclysmGameMode::TakeTheRunsTier`), so a price follows the same tier the
	 * gold drops follow. Tier 1 with no run.
	 *
	 * AND ON THIS CITY, WITH THE EMPIRE UPGRADE TREE'S PRICE NODES, issue #2741 slice 6a (P1 to P3): the price times
	 * `UCataclysmEmpireTree::UpgradePriceFactorFor`, from the tree the character playing holds
	 * (`UCataclysmGameInstance::EmpireTreePlayerBonusesFor`), rounded to whole gold. THE ONE PRICE: the city screen's
	 * offers, `Buy` and the console command all ask this, so the price shown is the price paid. No context, no tree
	 * held, or no such city: the tier's price.
	 */
	static int64 PriceIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run, FName RowName, int32 CityId,
						 int32 UpgradeTier = 1);

	/**
	 * The days the upgrade takes to build in this run, on this city: `UCataclysmCityUpgradeRules::BuildDaysFor` at the
	 * upgrade tier it is bought at, 1, or raised to, 2 or 3 (issue #1265). Issue #2825, ruled 2026-11-21.
	 *
	 * THE ONE FIGURE, as `PriceIn` is the one price: the city screen's offers, `Buy` and the console command all ask
	 * this, so the days shown are the days counted.
	 *
	 * AND WITH THE EMPIRE UPGRADE TREE'S THREE BUILD NODES, issue #2832: `UCataclysmEmpireTree::UpgradeBuildDaysFor`,
	 * from the tree the character playing holds -- Scaffolding's doubling, Rapid Renovation's days off and, when
	 * `bRush` and Rapid Funding is held, Rapid Funding's 2, to at least a day. The days are fixed at purchase, so a node
	 * bought later does not shorten a build under way.
	 */
	static int32 BuildDaysIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run, FName RowName, int32 CityId,
							 bool bRush = false, int32 UpgradeTier = 1);

	/** Whether the character playing holds Rapid Funding, so each offer has a Rush. Issue #2832. */
	static bool CanRushIn(const UObject* WorldContext);

	/**
	 * What a rush of the upgrade costs: `PriceIn`, after every discount, times Rapid Funding's 150%, rounded to whole
	 * gold. Issue #2832, ruled 2026-11-21. `PriceIn` when Rapid Funding is not held, which `Buy` refuses to rush.
	 */
	static int64 RushPriceIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run, FName RowName, int32 CityId,
							 int32 UpgradeTier = 1);

	/**
	 * The gold the character playing holds (`UCataclysmGameInstance::GoldHeldBy`), or 0 when it has no balance. What
	 * the city screen passes `UCataclysmCityScreenLayout::OffersFor`, which asks the run as `Buy` does.
	 */
	static int64 GoldHeldIn(const UObject* WorldContext);

	/**
	 * The credit the empire upgrade tree gives the character playing on this price (issue #2741 slice 6e, D2):
	 * `UCataclysmEmpireTree::UpgradeCreditFor` from the tree it holds, and nought while anything is owed, where there is
	 * no balance, or with neither Global Credit nor Sovereign Loans. The run is asked with the gold held plus this, so
	 * the screen and `Buy` agree.
	 */
	static int64 CreditIn(const UObject* WorldContext, int64 Price);

	/** What the character playing owes (`UCataclysmGameInstance::GoldOwedBy`), or 0 where there is no balance. */
	static int64 GoldOwedIn(const UObject* WorldContext);

	/**
	 * Buys the upgrade on a city for the character playing, taking its price from the balance `GoldHeldBy` reads: the
	 * account's for the character's lethality mode, or a Solo Self-Found character's own.
	 *
	 * ON `Bought` IT RAISES THE SAVES: `CityUpgradeBought`, which writes the run record and the character record, and
	 * `AccountChanged` as well when the gold came from the account. And it says the PLAY LOOP line
	 * (`FCataclysmPlayLoopLog::UpgradeLine`).
	 *
	 * IT TAKES DAYS TO BUILD, since issue #2825: the run is told `BuildDaysIn`, puts the upgrade on the city's build
	 * list and finishes it when those days have passed. The gold is taken now; a build cannot be cancelled.
	 *
	 * ON CREDIT, issue #2741 slice 6e (D2): a price the balance cannot cover but the balance and `CreditIn` can takes the
	 * whole balance and owes the rest (`UCataclysmGameInstance::SpendGoldOnCreditFor`), repaid by day when Sovereign
	 * Loans is held (D4).
	 *
	 * A RUSH, `bRush`, since issue #2832: `RushPriceIn` is taken and `BuildDaysIn` with the rush is told the run.
	 * Refused with `RushNotHeld`, and nothing taken, when Rapid Funding is not held.
	 * A rush may be bought on credit as a plain purchase may, the credit a share of the rush's price.
	 *
	 * @return the run's answer; `NotEnoughGold` when the balance and the credit hold less than the price, or there is no
	 *         balance.
	 */
	static ECataclysmCityUpgradeResult Buy(const UObject* WorldContext, UCataclysmEmpireRun* Run, int32 CityId,
										   FName RowName, bool bRush = false);

	/**
	 * The tier a raise of that row on that city would reach: one above the tier the city holds it at, or `INDEX_NONE`
	 * when the city does not hold it. Issue #1265.
	 */
	static int32 RaiseTierIn(const UCataclysmEmpireRun* Run, int32 CityId, FName RowName);

	/**
	 * Raises an upgrade the city holds by one tier for the character playing, as `Buy` buys one: the same payment, on
	 * credit as a purchase may be, the same rush, Reinvestment's count, the saves and a play loop line. Issue #1265,
	 * the coordinating session's rulings of 2026-11-22: in the same slot, for the next tier's gold (`PriceIn` at that
	 * tier) and its build days (`BuildDaysIn` at that tier), the run told through `RaiseCityUpgrade`.
	 *
	 * @return `NotHeld` when the city does not hold it, `NoTierToRaiseTo` when its row has no next tier, `RushNotHeld`
	 *         for a rush without Rapid Funding; otherwise the run's answer, as `Buy`.
	 */
	static ECataclysmCityUpgradeResult Raise(const UObject* WorldContext, UCataclysmEmpireRun* Run, int32 CityId,
											 FName RowName, bool bRush = false);

	/** "10,000 gold". */
	static FString PriceText(int64 Price);

	/** "6 days to build", or "1 day to build". Issue #2825. */
	static FString BuildDaysText(int32 Days);

	/**
	 * Why an upgrade was refused, in words a screen can show: the run's own words, and for `NotEnoughGold` the price
	 * and how much more is needed, "Costs 30,000 gold; 2,500 more needed." Empty for `Bought`.
	 */
	static FString RefusalText(ECataclysmCityUpgradeResult Result, int64 Price, int64 Held);

	/**
	 * `RefusalText` for the character playing (issue #2741 slice 6e, D6): what is missing counted beyond the credit the
	 * tree gives, and an open debt named when it is what stops the credit, "Costs 10,000 gold; 7,500 more needed, and no
	 * more credit until the 2,500 owed is paid." What the city screen, its button and the console show.
	 */
	static FString RefusalTextIn(const UObject* WorldContext, ECataclysmCityUpgradeResult Result, int64 Price,
								 int64 Held);
};
