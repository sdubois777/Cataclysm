// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmCityUpgradePurchase.h"

#include "Cataclysm.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CataclysmDroppedItem.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"

int64 UCataclysmCityUpgradePurchase::PriceIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run, FName RowName,
											 int32 CityId)
{
	const int32 Tier = Run != nullptr ? Run->DifficultyTier : ACataclysmGameMode::LowestDifficultyTier;
	const int64 TiersPrice = UCataclysmCityUpgradeMapping::PriceFor(RowName, Tier);

	// THE TREE'S PRICE NODES ON THIS CITY (slice 6a). A factor of exactly 1 leaves the tier's price as it is.
	const float Factor = UCataclysmEmpireTree::UpgradePriceFactorFor(
		UCataclysmGameInstance::EmpireTreePlayerBonusesFor(WorldContext), Run != nullptr ? Run->Map.Get() : nullptr,
		CityId);
	return Factor == 1.0f ? TiersPrice
						  : static_cast<int64>(FMath::RoundToDouble(static_cast<double>(TiersPrice) * Factor));
}

int32 UCataclysmCityUpgradePurchase::BuildDaysIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run,
												 FName RowName, int32 CityId)
{
	// THE TIER A PURCHASE IS AT: every bought upgrade is at tier 1 (issue #1265; `UCataclysmEmpireRun::BuyCityUpgrade`
	// sets it). The context, the row and the city are what the tree's build nodes will read in the next slice.
	(void)WorldContext;
	(void)Run;
	(void)RowName;
	(void)CityId;
	return UCataclysmCityUpgradeRules::BuildDaysFor(1);
}

int64 UCataclysmCityUpgradePurchase::GoldHeldIn(const UObject* WorldContext)
{
	int64 Held = 0;
	return UCataclysmGameInstance::GoldHeldBy(WorldContext, Held) ? Held : 0;
}

ECataclysmCityUpgradeResult UCataclysmCityUpgradePurchase::Buy(const UObject* WorldContext, UCataclysmEmpireRun* Run,
																int32 CityId, FName RowName)
{
	if (Run == nullptr)
	{
		return ECataclysmCityUpgradeResult::RunHasNotBegun;
	}

	const FCataclysmCityUpgrade Upgrade = UCataclysmCityUpgradeMapping::MakeFromTable(RowName);
	const int64 Price = PriceIn(WorldContext, Run, RowName, CityId);
	const int64 Held = GoldHeldIn(WorldContext);
	const int32 Days = BuildDaysIn(WorldContext, Run, RowName, CityId);

	// THE RUN'S QUESTION FIRST, so nothing is taken for a purchase it would refuse.
	const ECataclysmCityUpgradeResult Asked = Run->WouldBuyCityUpgrade(CityId, Upgrade, Price, Held);
	if (Asked != ECataclysmCityUpgradeResult::Bought)
	{
		return Asked;
	}

	// THE GOLD BEFORE THE UPGRADE, so an upgrade is never held unpaid. All or nothing: a balance that holds less is
	// left as it was, which the question above has just ruled out.
	bool bFromTheCharacter = false;
	if (Price > 0 && !UCataclysmGameInstance::SpendGoldFor(WorldContext, Price, bFromTheCharacter))
	{
		UE_LOG(LogCataclysm, Error,
			   TEXT("City upgrade %s on city %d: the run would sell it for %lld gold against %lld held, and the "
					"balance would not pay it. Nothing was bought."),
			   *RowName.ToString(), CityId, Price, Held);
		return ECataclysmCityUpgradeResult::NotEnoughGold;
	}

	// WITH THE DAYS IT TAKES TO BUILD, since issue #2825: the run puts it on the city's build list.
	const ECataclysmCityUpgradeResult Result = Run->BuyCityUpgrade(CityId, Upgrade, Price, Held, Days);
	if (Result != ECataclysmCityUpgradeResult::Bought)
	{
		// GIVEN BACK, a guard: the run refuses only what it refused a moment ago, above.
		bool bIntoTheCharacter = false;
		if (Price > 0)
		{
			UCataclysmGameInstance::AddGoldFor(WorldContext, Price, bIntoTheCharacter);
		}
		UE_LOG(LogCataclysm, Error, TEXT("City upgrade %s on city %d was refused after its price was taken (%s); "
										 "the %lld gold was given back."),
			   *RowName.ToString(), CityId, *UCataclysmCityUpgradeRules::ResultText(Result), Price);
		return Result;
	}

	// THE SAVES: the run record holds the upgrade and the character record a Solo Self-Found character's own gold
	// (`CityUpgradeBought`); the account record the account's gold, when it paid (`AccountChanged`).
	const UWorld* World = WorldContext != nullptr && GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (Price > 0 && !bFromTheCharacter)
	{
		UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
	}
	UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::CityUpgradeBought);

	int64 After = 0;
	if (!UCataclysmGameInstance::GoldHeldBy(WorldContext, After))
	{
		After = -1;
	}
	const FString Effect = UCataclysmCityUpgradeMapping::EffectTextFor(RowName);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::UpgradeLine(Effect.IsEmpty() ? RowName.ToString() : Effect, Run,
																   CityId, Price, After, Run->BuildReadyDay(CityId)));
	return ECataclysmCityUpgradeResult::Bought;
}

FString UCataclysmCityUpgradePurchase::PriceText(int64 Price)
{
	return ACataclysmDroppedItem::GoldName(FMath::Max<int64>(0, Price));
}

FString UCataclysmCityUpgradePurchase::BuildDaysText(int32 Days)
{
	return Days == 1 ? FString(TEXT("1 day to build")) : FString::Printf(TEXT("%d days to build"), Days);
}

FString UCataclysmCityUpgradePurchase::RefusalText(ECataclysmCityUpgradeResult Result, int64 Price, int64 Held)
{
	if (Result == ECataclysmCityUpgradeResult::Bought)
	{
		return FString();
	}
	if (Result == ECataclysmCityUpgradeResult::NotEnoughGold)
	{
		return FString::Printf(TEXT("Costs %s; %s more needed."), *PriceText(Price),
							   *ACataclysmDroppedItem::GoldAmountText(FMath::Max<int64>(0, Price - Held)));
	}
	return UCataclysmCityUpgradeRules::ResultText(Result);
}
