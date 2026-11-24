// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmMerchant.h"

#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"

const double UCataclysmMerchant::SellPiles[UCataclysmMerchant::RarityCount] = {
	0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0,
};

const TCHAR* UCataclysmMerchant::WalkRefusal = TEXT("The merchant trades in the capital, not during a walk.");
const TCHAR* UCataclysmMerchant::NoItemRefusal = TEXT("There is no piece of gear in that cell to sell.");
const TCHAR* UCataclysmMerchant::NoBalanceRefusal = TEXT("There is no gold balance to pay the sale into.");
const TCHAR* UCataclysmMerchant::NoCharacterRefusal = TEXT("No character is playing, so there is nothing to sell.");

int64 UCataclysmMerchant::SellValueOf(const FCataclysmItem& Item, double PileGold)
{
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	if (PileGold <= 0.0 || !UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Rarity))
	{
		return 0;
	}
	const int32 Rung = FMath::Clamp(static_cast<int32>(Rarity), 0, RarityCount - 1);
	const double Price = PileGold * SellPiles[Rung] * (1.0 + UpgradeStep * FMath::Max(0, Item.GearLevel));
	// ROUNDED HALF UP, AND AT LEAST ONE GOLD: an Everyday +0 piece at tier 1 is 0.625 gold, and a sale of nothing
	// would read as a refusal.
	return FMath::Max<int64>(1, static_cast<int64>(FMath::FloorToDouble(Price + 0.5)));
}

double UCataclysmMerchant::PileGoldAt(int32 DifficultyTier)
{
	// FLOOR 1, so no floor's bonus: the owner's "the tier's gold pile size".
	return UCataclysmDropRoll::GoldPileMean(UCataclysmDropRoll::LoadGoldTable(), DifficultyTier, 1);
}

int64 UCataclysmMerchant::SellValueAt(const FCataclysmItem& Item, int32 DifficultyTier)
{
	return SellValueOf(Item, PileGoldAt(DifficultyTier));
}

int32 UCataclysmMerchant::TierIn(const UObject* WorldContext)
{
	const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	return Run != nullptr ? Run->DifficultyTier : ACataclysmGameMode::LowestDifficultyTier;
}

TArray<int32> UCataclysmMerchant::CarriedPiecesOf(const ACataclysmPlayerCharacter* Character)
{
	TArray<int32> Cells;
	const UCataclysmInventoryComponent* Bag = Character != nullptr ? Character->GetInventory() : nullptr;
	if (Bag == nullptr)
	{
		return Cells;
	}
	for (int32 Cell = 0; Cell < UCataclysmInventoryComponent::SlotCount; ++Cell)
	{
		const FCataclysmItem* Item = Bag->ItemAt(Cell);
		if (Item != nullptr && !UCataclysmInventoryComponent::SlotIsEmpty(*Item))
		{
			Cells.Add(Cell);
		}
	}
	return Cells;
}

FString UCataclysmMerchant::RefusalNow(const UObject* WorldContext)
{
	const ACataclysmPlayerCharacter* Character = UCataclysmForgeCraft::CharacterIn(WorldContext);
	if (Character == nullptr || Character->GetInventory() == nullptr)
	{
		return NoCharacterRefusal;
	}
	// NOT DURING A WALK, the forge's rule and for its reason: the merchant is a capital service.
	const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	if (Run != nullptr && Run->IsAWalkUnderWay())
	{
		return WalkRefusal;
	}
	return FString();
}

bool UCataclysmMerchant::Sell(const UObject* WorldContext, int32 CarriedSlot, int64& OutGold, FString& OutMessage)
{
	OutGold = 0;
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return false;
	}
	ACataclysmPlayerCharacter* Character = UCataclysmForgeCraft::CharacterIn(WorldContext);
	UCataclysmInventoryComponent* Bag = Character->GetInventory();
	const FCataclysmItem* Found = Bag->ItemAt(CarriedSlot);
	if (Found == nullptr || UCataclysmInventoryComponent::SlotIsEmpty(*Found))
	{
		OutMessage = NoItemRefusal;
		return false;
	}
	const FCataclysmItem Item = *Found;
	const int64 Price = SellValueAt(Item, TierIn(WorldContext));
	if (Price <= 0)
	{
		OutMessage = NoItemRefusal;
		return false;
	}

	// THE GOLD FIRST, then the piece, so nothing destroys a piece that was not paid for.
	bool bIntoTheCharacter = false;
	if (!UCataclysmGameInstance::AddGoldFor(WorldContext, Price, bIntoTheCharacter))
	{
		OutMessage = NoBalanceRefusal;
		return false;
	}
	if (!Bag->RemoveItemAt(CarriedSlot))
	{
		// CANNOT HAPPEN after the check above, and if it does the gold is taken back: a sale is all or nothing.
		bool bFromTheCharacter = false;
		UCataclysmGameInstance::SpendGoldFor(WorldContext, Price, bFromTheCharacter);
		OutMessage = NoItemRefusal;
		return false;
	}

	const FString Name = UCataclysmForge::NameOf(Item, UCataclysmItemModifiers::LoadBaseTable(),
												 UCataclysmDropRoll::LoadAffixTable());
	int64 Held = 0;
	UCataclysmGameInstance::GoldHeldBy(WorldContext, Held);
	OutGold = Price;
	OutMessage = FString::Printf(TEXT("Sold %s for %s."), *Name, *ACataclysmDroppedItem::GoldName(Price));
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::SoldLine(Name, Item.GearLevel, Price, Held));

	// THE ACCOUNT RECORD when the gold went there; the bag's cell, and a Solo Self-Found character's own balance, are in
	// the character record, which `RemoveItemAt`'s `InventoryChanged` writes.
	if (!bIntoTheCharacter)
	{
		const UWorld* World = GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
	}
	return true;
}
