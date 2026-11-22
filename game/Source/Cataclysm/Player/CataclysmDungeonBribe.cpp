// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmDungeonBribe.h"

#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"

namespace CataclysmDungeonBribe
{
	/** What every Bribe button's name begins with; the dungeon's id follows. */
	const TCHAR* const ButtonPrefix = TEXT("Bribe_");
}

int64 FCataclysmDungeonBribe::FeeIn(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr)
	{
		return 0;
	}
	return static_cast<int64>(FMath::RoundToDouble(static_cast<double>(UCataclysmEmpireTree::BribeFeeAtTierOne)
												   * UCataclysmDropRoll::GoldScaleFor(Run->DifficultyTier)));
}

FString FCataclysmDungeonBribe::RefusalFor(const UObject* WorldContext, const UCataclysmEmpireRun* Run,
										   int32 DungeonId)
{
	if (Run == nullptr)
	{
		return TEXT("No run has begun.");
	}
	if (UCataclysmGameInstance::EmpireTreePlayerBonusesFor(WorldContext).DungeonBribePoints <= 0)
	{
		return TEXT("Bribery is not held in the empire upgrade tree.");
	}
	const FString Refused = Run->RefusalForBribing(DungeonId);
	if (!Refused.IsEmpty())
	{
		return Refused;
	}

	// IN FULL FROM THE BALANCE, WITH NO CREDIT (V13, as V9): the credit nodes are for city upgrades.
	int64 Held = 0;
	if (!UCataclysmGameInstance::GoldHeldBy(WorldContext, Held))
	{
		return TEXT("There is no gold balance to pay the fee from.");
	}
	const int64 Fee = FeeIn(Run);
	if (Held < Fee)
	{
		return FString::Printf(TEXT("Costs %s gold; %s more needed."), *ACataclysmDroppedItem::GoldAmountText(Fee),
							   *ACataclysmDroppedItem::GoldAmountText(Fee - Held));
	}
	return FString();
}

FString FCataclysmDungeonBribe::Bribe(const UObject* WorldContext, UCataclysmEmpireRun* Run, int32 DungeonId)
{
	const FString Refused = RefusalFor(WorldContext, Run, DungeonId);
	if (!Refused.IsEmpty())
	{
		FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::BribeRefusedLine(DungeonId, Refused));
		return Refused;
	}

	// THE GOLD BEFORE THE TIMER, so a reset is never had unpaid, and given back if the run refuses after all.
	const int64 Fee = FeeIn(Run);
	bool bFromTheCharacter = false;
	if (Fee > 0 && !UCataclysmGameInstance::SpendGoldFor(WorldContext, Fee, bFromTheCharacter))
	{
		return TEXT("The fee could not be taken from the balance.");
	}
	if (!Run->BribeDungeon(DungeonId))
	{
		bool bIntoTheCharacter = false;
		if (Fee > 0)
		{
			UCataclysmGameInstance::AddGoldFor(WorldContext, Fee, bIntoTheCharacter);
		}
		return Run->RefusalForBribing(DungeonId);
	}

	// THE SAVES A GOLD SPEND RAISES: the account record when it paid, and the run and character records.
	const UWorld* World = WorldContext != nullptr && GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (Fee > 0 && !bFromTheCharacter)
	{
		UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
	}
	UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::InventoryChanged);

	int64 After = 0;
	UCataclysmGameInstance::GoldHeldBy(WorldContext, After);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::BribeLine(DungeonId, Fee, After));
	return FString();
}

FString FCataclysmDungeonBribe::ButtonText(const UCataclysmEmpireRun* Run)
{
	return FString::Printf(TEXT("Bribe: reset its timer for %s gold"),
						   *ACataclysmDroppedItem::GoldAmountText(FeeIn(Run)));
}

FName FCataclysmDungeonBribe::ButtonNameFor(int32 DungeonId)
{
	return FName(*FString::Printf(TEXT("%s%d"), CataclysmDungeonBribe::ButtonPrefix, DungeonId));
}

int32 FCataclysmDungeonBribe::DungeonForButton(FName Value)
{
	const FString Name = Value.ToString();
	const FString Prefix = CataclysmDungeonBribe::ButtonPrefix;
	if (!Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
	{
		return INDEX_NONE;
	}
	const FString Digits = Name.RightChop(Prefix.Len());
	if (Digits.IsEmpty() || !Digits.IsNumeric() || Digits.Contains(TEXT("-")) || Digits.Contains(TEXT(".")))
	{
		return INDEX_NONE;
	}
	return FCString::Atoi(*Digits);
}
