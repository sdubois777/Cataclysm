// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmForgeCraft.h"

#include "Cataclysm.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CataclysmDropRoll.h"
// A gem's type, name and rarity name, issue #46 slice G4.
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
// The threshold's consequence in the design's words, for a socketing that crosses it.
#include "Items/CataclysmWearing.h"
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

FCataclysmForgeSocketing UCataclysmForgeCraft::SocketPlanFor(const UObject* WorldContext,
															 const FCataclysmForgeTarget& Target, int32 Socket,
															 FName Gem, ECataclysmRarity Rarity)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::SocketGemOf(FCataclysmItem(), Socket, Gem, Rarity, FString(), 0, 0);
	}
	const UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	const FCataclysmGemRow* Row = UCataclysmGems::RowOf(UCataclysmGems::LoadGeneratedTable(), Gem);
	return UCataclysmForge::SocketGemOf(*Item, Socket, Gem, Rarity, Row ? Row->GemType : FString(),
										Inventory->CountOfGems(Gem, Rarity),
										Inventory->CountOfMaterial(FName(UCataclysmForge::SettingAgentRow)));
}

FCataclysmForgeSocketing UCataclysmForgeCraft::UnsocketPlanFor(const UObject* WorldContext,
															   const FCataclysmForgeTarget& Target, int32 Socket)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::UnsocketGemOf(FCataclysmItem(), Socket, 0, false);
	}
	const UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	const int32 Dust = Inventory->CountOfMaterial(FName(UCataclysmForge::DismantlingDustRow));
	// ROOM FOR THE GEM: a stack of it to join, a free cell, or the cell the last Dust taken empties.
	const FCataclysmSocket* From = Item->Sockets.IsValidIndex(Socket) ? &Item->Sockets[Socket] : nullptr;
	const bool bRoom = From != nullptr
		&& (Inventory->SlotOfGems(From->Gem, From->GemRarity) != INDEX_NONE || Inventory->NumFreeSlots() > 0
			|| Dust == 1);
	return UCataclysmForge::UnsocketGemOf(*Item, Socket, Dust, bRoom);
}

ECataclysmForgeResult UCataclysmForgeCraft::SocketGem(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													  int32 Socket, FName Gem, ECataclysmRarity Rarity,
													  bool bConfirmedPastThreshold, FCataclysmForgeSocketing& OutDone,
													  FString& OutMessage)
{
	OutDone = FCataclysmForgeSocketing();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeSocketing Plan = SocketPlanFor(WorldContext, Target, Socket, Gem, Rarity);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoSocketing(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage);
}

ECataclysmForgeResult UCataclysmForgeCraft::UnsocketGem(const UObject* WorldContext,
														const FCataclysmForgeTarget& Target, int32 Socket,
														bool bConfirmedPastThreshold, FCataclysmForgeSocketing& OutDone,
														FString& OutMessage)
{
	OutDone = FCataclysmForgeSocketing();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeSocketing Plan = UnsocketPlanFor(WorldContext, Target, Socket);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoSocketing(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage);
}

ECataclysmForgeResult UCataclysmForgeCraft::DoSocketing(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														const FCataclysmForgeSocketing& Plan,
														bool bConfirmedPastThreshold,
														FCataclysmForgeSocketing& OutDone, FString& OutMessage)
{
	UWorld* World = ForgeWorldOf(WorldContext);
	ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();

	// THE DAYS NEED A RUN TO PASS IN, as an upgrade's do.
	UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	if (Run == nullptr)
	{
		OutMessage = TEXT("There is no run to take the craft's days from.");
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmItem* Held = ItemAt(*Character, Target);
	if (Held == nullptr)
	{
		OutMessage = Plan.bUnsocket ? TEXT("There is nothing there to unsocket.") : TEXT("There is nothing there to socket.");
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmItem Before = *Held; // A COPY: the pointer is into the component's own array.
	const FString Name = UCataclysmForge::NameOf(Before, UCataclysmItemModifiers::LoadBaseTable(),
												 UCataclysmDropRoll::LoadAffixTable());
	const float Added = Plan.ResidueAfter - Plan.ResidueBefore;

	// THE THRESHOLD, FOR A WORN PIECE ONLY, as an upgrade asks it.
	bool bCrosses = false;
	if (Target.bWorn)
	{
		const float Threshold =
			UCataclysmEquipmentComponent::ConsumptionThresholdFor(ACataclysmGameMode::DifficultyTierIn(World));
		const float Now = Equipment->WornResidue();
		const float After = Now + Added;
		bCrosses = Now < Threshold && After >= Threshold;
		if (bCrosses && !bConfirmedPastThreshold)
		{
			OutMessage = FString::Printf(
				TEXT("%s %s brings your Worn Residue to %d, past this tier's Consumption Threshold of %d. %s"),
				Plan.bUnsocket ? TEXT("Unsocketing a gem from") : TEXT("Socketing a gem into"), *Name,
				FMath::RoundToInt(After), FMath::RoundToInt(Threshold), UCataclysmWearing::ThresholdConsequence);
			return ECataclysmForgeResult::AsksAboutTheThreshold;
		}
	}
	ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	const bool bWasMarked = State != nullptr && State->IsMarkedByConsumption();
	if (bCrosses && State != nullptr)
	{
		State->MarkByConsumption();
	}
	const auto TakeBackTheMark = [&]()
	{
		if (bCrosses && State != nullptr && !bWasMarked)
		{
			State->ClearConsumptionMark();
		}
	};

	// THE MATERIAL, AND FOR A SOCKETING THE GEM, THEN THE ITEM. A refusal after anything is taken puts it back.
	if (!Inventory->RemoveMaterial(Plan.Material, 1))
	{
		TakeBackTheMark();
		OutMessage = TEXT("The material could not be taken from the bag; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}
	if (!Plan.bUnsocket && !Inventory->RemoveGems(Plan.Gem, Plan.Rarity, 1))
	{
		Inventory->AddMaterial(Plan.Material, 1);
		TakeBackTheMark();
		OutMessage = TEXT("The gem could not be taken from the bag; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}

	FCataclysmItem Changed = Before;
	FCataclysmSocket& Socket = Changed.Sockets[Plan.Socket];
	Socket.Gem = Plan.bUnsocket ? NAME_None : Plan.Gem;
	Socket.GemRarity = Plan.bUnsocket ? ECataclysmRarity::Everyday : Plan.Rarity;
	Changed.Residue = Plan.ResidueAfter;
	const auto PutTheItemBack = [&]()
	{
		return Target.bWorn ? Equipment->ReplaceEquippedAt(Target.WornSlot, Before)
							: Inventory->ReplaceItemAt(Target.CarriedSlot, Before);
	};
	const bool bReplaced = Target.bWorn ? Equipment->ReplaceEquippedAt(Target.WornSlot, Changed)
										: Inventory->ReplaceItemAt(Target.CarriedSlot, Changed);
	if (!bReplaced)
	{
		Inventory->AddMaterial(Plan.Material, 1);
		if (!Plan.bUnsocket)
		{
			Inventory->AddGems(Plan.Gem, Plan.Rarity, 1);
		}
		TakeBackTheMark();
		UE_LOG(LogCataclysm, Error, TEXT("The forge could not put %s back where it was; what it took is back in the bag."),
			   *Name);
		OutMessage = TEXT("The item could not be changed; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}

	// AN UNSOCKETED GEM GOES BACK TO THE BAG; if it cannot, the item and the Dust are put back as they were.
	if (Plan.bUnsocket && Inventory->AddGems(Plan.Gem, Plan.Rarity, 1) == INDEX_NONE)
	{
		PutTheItemBack();
		Inventory->AddMaterial(Plan.Material, 1);
		TakeBackTheMark();
		OutMessage = TEXT("The gem had nowhere to go in the bag; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}

	const int32 DayBefore = Run->Day();
	Run->AdvanceDays(Plan.Days);
	if (bCrosses && State != nullptr && !State->HoldsConsumptionBuild())
	{
		State->SetConsumptionBuild(FCataclysmPlayerBuild::Of(*Character));
	}

	const FString GemName = UCataclysmGems::NameOf(Plan.Gem);
	const FString RarityName = UCataclysmGems::RarityName(Plan.Rarity);
	UE_LOG(LogCataclysm, Log, TEXT("The forge %s %s (%s) %s %s; residue %.0f to %.0f; %d day(s), day %d to day %d."),
		   Plan.bUnsocket ? TEXT("unsocketed") : TEXT("socketed"), *GemName, *RarityName,
		   Plan.bUnsocket ? TEXT("from") : TEXT("into"), *Name, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days,
		   DayBefore, Run->Day());
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeSocketLine(
		Plan.bUnsocket, GemName, RarityName, UCataclysmGems::SocketTypeName(Plan.SocketType), Name,
		FMath::RoundToInt(Plan.ResidueBefore), FMath::RoundToInt(Plan.ResidueAfter), Plan.Days, DayBefore, Run->Day()));

	// SAVED: the character record holds the item, the gem and the material, the run record the days.
	UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::ForgeCrafted);

	OutDone = Plan;
	return ECataclysmForgeResult::Done;
}
