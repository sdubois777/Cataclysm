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
	 */
	static int64 PriceIn(const UCataclysmEmpireRun* Run, FName RowName);

	/**
	 * The gold the character playing holds (`UCataclysmGameInstance::GoldHeldBy`), or 0 when it has no balance. What
	 * the city screen passes `UCataclysmCityScreenLayout::OffersFor`, which asks the run as `Buy` does.
	 */
	static int64 GoldHeldIn(const UObject* WorldContext);

	/**
	 * Buys the upgrade on a city for the character playing, taking its price from the balance `GoldHeldBy` reads: the
	 * account's for the character's lethality mode, or a Solo Self-Found character's own.
	 *
	 * ON `Bought` IT RAISES THE SAVES: `CityUpgradeBought`, which writes the run record and the character record, and
	 * `AccountChanged` as well when the gold came from the account. And it says the PLAY LOOP line
	 * (`FCataclysmPlayLoopLog::UpgradeLine`).
	 *
	 * @return the run's answer; `NotEnoughGold` when the balance holds less than the price, or there is none.
	 */
	static ECataclysmCityUpgradeResult Buy(const UObject* WorldContext, UCataclysmEmpireRun* Run, int32 CityId,
										   FName RowName);

	/** "10,000 gold". */
	static FString PriceText(int64 Price);

	/**
	 * Why an upgrade was refused, in words a screen can show: the run's own words, and for `NotEnoughGold` the price
	 * and how much more is needed, "Costs 30,000 gold; 2,500 more needed." Empty for `Bought`.
	 */
	static FString RefusalText(ECataclysmCityUpgradeResult Result, int64 Price, int64 Held);
};
