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

int64 UCataclysmCityUpgradePurchase::GoldHeldIn(const UObject* WorldContext)
{
	int64 Held = 0;
	return UCataclysmGameInstance::GoldHeldBy(WorldContext, Held) ? Held : 0;
}

int64 UCataclysmCityUpgradePurchase::CreditIn(const UObject* WorldContext, int64 Price)
{
	// NOTHING MORE ON CREDIT UNTIL THE DEBT IS PAID (the owner's ruling of 2026-11-20, relayed).
	int64 Owed = 0;
	int64 EachDay = 0;
	if (Price <= 0 || !UCataclysmGameInstance::GoldOwedBy(WorldContext, Owed, EachDay) || Owed > 0)
	{
		return 0;
	}
	return UCataclysmEmpireTree::UpgradeCreditFor(UCataclysmGameInstance::EmpireTreePlayerBonusesFor(WorldContext),
												  Price);
}

int64 UCataclysmCityUpgradePurchase::GoldOwedIn(const UObject* WorldContext)
{
	int64 Owed = 0;
	int64 EachDay = 0;
	return UCataclysmGameInstance::GoldOwedBy(WorldContext, Owed, EachDay) ? Owed : 0;
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
	const bool bOnCredit = Price > Held;
	const int64 Credit = bOnCredit ? CreditIn(WorldContext, Price) : 0;

	// THE RUN'S QUESTION FIRST, so nothing is taken for a purchase it would refuse. The credit the tree gives counts as
	// gold held for it (slice 6e, D2), as the city screen asks it.
	const ECataclysmCityUpgradeResult Asked = Run->WouldBuyCityUpgrade(CityId, Upgrade, Price, Held + Credit);
	if (Asked != ECataclysmCityUpgradeResult::Bought)
	{
		return Asked;
	}

	// THE GOLD BEFORE THE UPGRADE, so an upgrade is never held unpaid. All or nothing: a balance that holds less is
	// left as it was, which the question above has just ruled out. ON CREDIT the whole balance is taken and the rest
	// owed (D2), repaid by day when Sovereign Loans is held (D4).
	bool bFromTheCharacter = false;
	int64 Owed = 0;
	const bool bPaid = Price <= 0
		|| (bOnCredit ? UCataclysmGameInstance::SpendGoldOnCreditFor(
							WorldContext, Price, Credit,
							UCataclysmEmpireTree::RepaidDailyFor(
								UCataclysmGameInstance::EmpireTreePlayerBonusesFor(WorldContext)),
							bFromTheCharacter, Owed)
					  : UCataclysmGameInstance::SpendGoldFor(WorldContext, Price, bFromTheCharacter));
	if (!bPaid)
	{
		UE_LOG(LogCataclysm, Error,
			   TEXT("City upgrade %s on city %d: the run would sell it for %lld gold against %lld held, and the "
					"balance would not pay it. Nothing was bought."),
			   *RowName.ToString(), CityId, Price, Held);
		return ECataclysmCityUpgradeResult::NotEnoughGold;
	}

	const ECataclysmCityUpgradeResult Result = Run->BuyCityUpgrade(CityId, Upgrade, Price, Held + Credit);
	if (Result != ECataclysmCityUpgradeResult::Bought)
	{
		// GIVEN BACK, a guard: the run refuses only what it refused a moment ago, above. On credit, the balance taken is
		// given back and the debt made is forgiven.
		bool bIntoTheCharacter = false;
		if (Price > 0 && Owed > 0)
		{
			UCataclysmGameInstance::UndoCreditFor(WorldContext, Price - Owed, Owed);
		}
		else if (Price > 0)
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
																   CityId, Price, After));
	return ECataclysmCityUpgradeResult::Bought;
}

FString UCataclysmCityUpgradePurchase::PriceText(int64 Price)
{
	return ACataclysmDroppedItem::GoldName(FMath::Max<int64>(0, Price));
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

FString UCataclysmCityUpgradePurchase::RefusalTextIn(const UObject* WorldContext, ECataclysmCityUpgradeResult Result,
													 int64 Price, int64 Held)
{
	if (Result != ECataclysmCityUpgradeResult::NotEnoughGold)
	{
		return RefusalText(Result, Price, Held);
	}

	// WHAT IS MISSING BEYOND THE CREDIT, and A DEBT NAMED when it is what stops the credit (D6). A debt with neither
	// credit node held -- refunded since (D7) -- stops nothing a node would give, so it is not named.
	const FString Short = RefusalText(Result, Price, Held + CreditIn(WorldContext, Price));
	const int64 Owed = GoldOwedIn(WorldContext);
	if (Owed <= 0
		|| !UCataclysmEmpireTree::AllowsUpgradeCredit(UCataclysmGameInstance::EmpireTreePlayerBonusesFor(WorldContext)))
	{
		return Short;
	}
	return FString::Printf(TEXT("%s, and no more credit until the %s owed is paid."), *Short.LeftChop(1),
						   *ACataclysmDroppedItem::GoldAmountText(Owed));
}
