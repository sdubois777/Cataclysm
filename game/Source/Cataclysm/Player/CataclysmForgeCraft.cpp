// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmForgeCraft.h"

#include "Cataclysm.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"

const TCHAR* UCataclysmForgeCraft::WalkRefusal = TEXT("The forge can be used in the capital, not during a walk.");

namespace
{
	UWorld* ForgeWorldOf(const UObject* WorldContext)
	{
		return WorldContext != nullptr && GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}
}

ACataclysmPlayerCharacter* UCataclysmForgeCraft::CharacterIn(const UObject* WorldContext)
{
	const UWorld* World = ForgeWorldOf(WorldContext);
	return World != nullptr ? FCataclysmSaveGather::CharacterIn(*World) : nullptr;
}

FString UCataclysmForgeCraft::RefusalNow(const UObject* WorldContext)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	if (Character == nullptr || Character->GetInventory() == nullptr || Character->GetEquipment() == nullptr)
	{
		return TEXT("No character is playing, so there is nothing to craft for.");
	}

	// NOT DURING A WALK, the empire upgrade tree's rule (`UCataclysmGameInstance::EmpireTreeChangeRefusal`) and for its
	// reason: days do not pass inside a dungeon, and the forge is a capital service.
	const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	if (Run != nullptr && Run->IsAWalkUnderWay())
	{
		return WalkRefusal;
	}
	return FString();
}

const FCataclysmItem* UCataclysmForgeCraft::ItemAt(const ACataclysmPlayerCharacter& Character,
												   const FCataclysmForgeTarget& Target)
{
	if (Target.bWorn)
	{
		const UCataclysmEquipmentComponent* Equipment = Character.GetEquipment();
		return Equipment != nullptr ? Equipment->EquippedAt(Target.WornSlot) : nullptr;
	}
	const UCataclysmInventoryComponent* Inventory = Character.GetInventory();
	return Inventory != nullptr ? Inventory->ItemAt(Target.CarriedSlot) : nullptr;
}

FCataclysmForgeUpgrade UCataclysmForgeCraft::PlanFor(const UObject* WorldContext, const FCataclysmForgeTarget& Target)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::UpgradeOf(FCataclysmItem(), 0);
	}
	const int32 Held = Character->GetInventory()->CountOfMaterial(UCataclysmForge::StoneFor(Item->GearLevel + 1));
	return UCataclysmForge::UpgradeOf(*Item, Held);
}

ECataclysmForgeResult UCataclysmForgeCraft::Upgrade(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													bool bConfirmedPastThreshold, FCataclysmForgeUpgrade& OutDone,
													FString& OutMessage)
{
	OutDone = FCataclysmForgeUpgrade();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}

	UWorld* World = ForgeWorldOf(WorldContext);
	ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();

	// THE DAYS NEED A RUN TO PASS IN.
	UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	if (Run == nullptr)
	{
		OutMessage = TEXT("There is no run to take the upgrade's days from.");
		return ECataclysmForgeResult::Refused;
	}

	const FCataclysmItem* Held = ItemAt(*Character, Target);
	if (Held == nullptr)
	{
		OutMessage = TEXT("There is nothing there to upgrade.");
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmItem Before = *Held; // A COPY: the pointer is into the component's own array.

	const FCataclysmForgeUpgrade Plan = PlanFor(WorldContext, Target);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}

	const FString Name = UCataclysmForge::NameOf(Before, UCataclysmItemModifiers::LoadBaseTable(),
												 UCataclysmDropRoll::LoadAffixTable());

	// THE THRESHOLD, FOR A WORN PIECE ONLY: a carried piece's residue is not worn. Only the crossing asks, the equip's
	// reading of 2026-11-01 (`FCataclysmThresholdCrossing::Crosses`).
	bool bCrosses = false;
	if (Target.bWorn)
	{
		const float Threshold =
			UCataclysmEquipmentComponent::ConsumptionThresholdFor(ACataclysmGameMode::DifficultyTierIn(World));
		const float Now = Equipment->WornResidue();
		const float After = Now + UCataclysmForge::ResiduePerLevel;
		bCrosses = Now < Threshold && After >= Threshold;
		if (bCrosses && !bConfirmedPastThreshold)
		{
			OutMessage = UCataclysmForge::ThresholdQuestion(Name, After, Threshold);
			return ECataclysmForgeResult::AsksAboutTheThreshold;
		}
	}

	// THE MARK BEFORE THE CHANGE, as `WearPastTheThreshold` sets it, so the record the craft writes holds it.
	ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	const bool bWasMarked = State != nullptr && State->IsMarkedByConsumption();
	if (bCrosses && State != nullptr)
	{
		State->MarkByConsumption();
	}

	// A CRAFT THAT DID NOT HAPPEN TAKES BACK THE MARK IT SET, as an equip that did not happen does.
	const auto TakeBackTheMark = [&]()
	{
		if (bCrosses && State != nullptr && !bWasMarked)
		{
			State->ClearConsumptionMark();
		}
	};

	// THE STONES, THEN THE ITEM. A refusal after the stones are taken puts them back, so nothing is half done.
	if (!Inventory->RemoveMaterial(Plan.Stone, Plan.StonesNeeded))
	{
		TakeBackTheMark();
		OutMessage = TEXT("The stones could not be taken from the bag; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}

	FCataclysmItem Raised = Before;
	Raised.GearLevel = Plan.ToLevel;
	Raised.Residue = Plan.ResidueAfter;
	const bool bReplaced = Target.bWorn ? Equipment->ReplaceEquippedAt(Target.WornSlot, Raised)
										: Inventory->ReplaceItemAt(Target.CarriedSlot, Raised);
	if (!bReplaced)
	{
		TakeBackTheMark();
		Inventory->AddMaterial(Plan.Stone, Plan.StonesNeeded);
		UE_LOG(LogCataclysm, Error,
			   TEXT("The forge took two %s and could not put the raised %s back where it was. The stones are back in "
					"the bag and nothing else changed."),
			   *Plan.Stone.ToString(), *Name);
		OutMessage = TEXT("The item could not be raised; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}

	// THE DAYS, through the run's own day loop, so a surge or a resolve inside them happens on its own day.
	const int32 DayBefore = Run->Day();
	Run->AdvanceDays(Plan.Days);

	// AND THE BUILD THE DOUBLE WEARS, CAPTURED AFTER THE CHANGE, so it holds the raised piece; only when none is held,
	// for `WearPastTheThreshold`'s reason.
	if (bCrosses && State != nullptr && !State->HoldsConsumptionBuild())
	{
		State->SetConsumptionBuild(FCataclysmPlayerBuild::Of(*Character));
	}

	const int32 StonesLeft = Inventory->CountOfMaterial(Plan.Stone);
	UE_LOG(LogCataclysm, Log, TEXT("The forge raised %s from +%d to +%d with two %s (%d left); residue %.0f to %.0f; "
								   "%d day(s), day %d to day %d."),
		   *Name, Plan.FromLevel, Plan.ToLevel, *Plan.Stone.ToString(), StonesLeft, Plan.ResidueBefore,
		   Plan.ResidueAfter, Plan.Days, DayBefore, Run->Day());
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeUpgradeLine(
		Name, Plan.FromLevel, Plan.ToLevel, StonesLeft, FMath::RoundToInt(Plan.ResidueBefore),
		FMath::RoundToInt(Plan.ResidueAfter), Plan.Days, DayBefore, Run->Day()));

	// SAVED: the character record holds the raised item and the stones, the run record the days. `ForgeCrafted` writes
	// both, the run record even after an earlier write in the frame.
	UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::ForgeCrafted);

	OutDone = Plan;
	return ECataclysmForgeResult::Done;
}

bool UCataclysmForgeCraft::Combine(const UObject* WorldContext, int32 Level, FCataclysmForgeCombine& OutDone,
								   FString& OutReason)
{
	OutDone = FCataclysmForgeCombine();
	OutReason = RefusalNow(WorldContext);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	UCataclysmInventoryComponent* Inventory = Character->GetInventory();

	const FCataclysmForgeCombine Plan =
		UCataclysmForge::CombineOf(Level, Inventory->CountOfMaterial(UCataclysmForge::StoneFor(Level)));
	if (!Plan.bPossible)
	{
		OutReason = Plan.Refusal;
		return false;
	}

	// ROOM FOR THE MADE STONE, asked before anything moves: a stack of it to join, a free cell, or the cell the two
	// taken empty.
	const bool bRoom = Inventory->CountOfMaterial(Plan.Into) > 0 || Inventory->NumFreeSlots() > 0
		|| Plan.StonesHeld == Plan.StonesNeeded;
	if (!bRoom)
	{
		OutReason = FString::Printf(TEXT("The bag has no free cell for a +%d stone."), Plan.IntoLevel);
		return false;
	}

	if (!Inventory->RemoveMaterial(Plan.From, Plan.StonesNeeded))
	{
		OutReason = TEXT("The stones could not be taken from the bag.");
		return false;
	}
	if (Inventory->AddMaterial(Plan.Into, 1) == INDEX_NONE)
	{
		Inventory->AddMaterial(Plan.From, Plan.StonesNeeded);
		OutReason = TEXT("The made stone had nowhere to go; nothing changed.");
		return false;
	}

	const int32 FromHeld = Inventory->CountOfMaterial(Plan.From);
	const int32 IntoHeld = Inventory->CountOfMaterial(Plan.Into);
	UE_LOG(LogCataclysm, Log, TEXT("The forge combined two %s into one %s: %d and %d held."), *Plan.From.ToString(),
		   *Plan.Into.ToString(), FromHeld, IntoHeld);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeCombineLine(Plan.FromLevel, Plan.IntoLevel, FromHeld,
																	   IntoHeld));

	// SAVED: the stones are in the character record.
	UCataclysmSaveWriter::NoteTriggerIn(ForgeWorldOf(WorldContext), ECataclysmSaveTrigger::ForgeCrafted);

	OutDone = Plan;
	return true;
}
