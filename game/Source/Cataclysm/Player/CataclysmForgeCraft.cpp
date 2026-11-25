// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmForgeCraft.h"

#include "Cataclysm.h"
// The damage types designed for each weapon type, issue #47 slice F4.
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
// The player's magic find, which a dismantle's base stone reads (#2848, slice S2).
#include "Items/CataclysmDroppedItem.h"
// A gem's type, name and rarity name, issue #46 slice G4.
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
// The threshold's consequence in the design's words, for a socketing that crosses it.
#include "Items/CataclysmWearing.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
// Salvage Protocol and Disassembly Expert, which a dismantle reads (#2848, slice S2).
#include "Player/CataclysmEmpireTree.h"
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

	/**
	 * The difficulty tier a forge price is read at: the run's, as a city upgrade's price is
	 * (`UCataclysmCityUpgradePurchase::PriceIn`), and the lowest with no run. Issue #2830.
	 */
	int32 GoldTierOf(const UObject* WorldContext)
	{
		const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
		return Run != nullptr ? Run->DifficultyTier : ACataclysmGameMode::LowestDifficultyTier;
	}

	/** The gold the character playing holds (`UCataclysmGameInstance::GoldHeldBy`), or none. */
	int64 GoldHeldIn(const UObject* WorldContext)
	{
		int64 Held = 0;
		return UCataclysmGameInstance::GoldHeldBy(WorldContext, Held) ? Held : 0;
	}

	/**
	 * Pays a craft's gold from the balance city upgrades pay from, all or nothing. False when it would not pay, with
	 * nothing taken. Issue #2830.
	 */
	bool PayForTheCraft(const UObject* WorldContext, int64 Gold, bool& bOutFromTheCharacter)
	{
		bOutFromTheCharacter = false;
		return Gold <= 0 || UCataclysmGameInstance::SpendGoldFor(WorldContext, Gold, bOutFromTheCharacter);
	}

	/** Gives a craft's gold back when a later step failed, so nothing is half done. */
	void GiveTheGoldBack(const UObject* WorldContext, int64 Gold)
	{
		bool bIntoTheCharacter = false;
		if (Gold > 0)
		{
			UCataclysmGameInstance::AddGoldFor(WorldContext, Gold, bIntoTheCharacter);
		}
	}

	/** The account record is written when the account paid; a Solo Self-Found character's gold is in its own record. */
	void SaveWhereTheGoldCameFrom(UWorld* World, int64 Gold, bool bFromTheCharacter)
	{
		if (Gold > 0 && !bFromTheCharacter)
		{
			UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
		}
	}
}

namespace
{
	/**
	 * An item craft's step from its plan: its own material and count, then the options' materials; one gold payment, the
	 * craft's and the options'; the residue and the days after the options. Issue #47, slice F5.
	 */
	FCataclysmForgeCraftStep ForgeStepOf(const FCataclysmForgeTarget& Target, FName Material, int32 Count, int64 CraftGold,
										 const FCataclysmForgeOptionEffect& Effect, const FString& ThresholdWords)
	{
		FCataclysmForgeCraftStep Step;
		Step.Target = Target;
		Step.Materials.Add({Material, Count});
		Step.Materials.Append(Effect.Materials);
		Step.Gold = CraftGold + Effect.Gold;
		Step.ResidueAfter = Effect.ResidueAfter;
		Step.Days = Effect.Days;
		Step.Options = Effect;
		Step.ThresholdWords = ThresholdWords;
		return Step;
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
		return UCataclysmForge::UpgradeOf(FCataclysmItem(), 0, GoldTierOf(WorldContext), 0);
	}
	const int32 Held = Character->GetInventory()->CountOfMaterial(UCataclysmForge::StoneFor(Item->GearLevel + 1));
	return UCataclysmForge::UpgradeOf(*Item, Held, GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::Upgrade(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													bool bConfirmedPastThreshold, FCataclysmForgeUpgrade& OutDone,
													FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeUpgrade();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeUpgrade Plan = PlanFor(WorldContext, Target);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}

	// TWO STONES OF THE LEVEL IT GOES TO, and the level raised; everything else is `DoItemCraft`'s (slice F5).
	FCataclysmForgeCraftStep Step = ForgeStepOf(Target, Plan.Stone, Plan.StonesNeeded, Plan.Gold, Effect, TEXT("Upgrading"));
	Step.NoRunRefusal = TEXT("There is no run to take the upgrade's days from.");
	Step.NothingThereRefusal = TEXT("There is nothing there to upgrade.");
	FCataclysmForgeUpgrade Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	UCataclysmInventoryComponent* Inventory = CharacterIn(WorldContext)->GetInventory();
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&Plan](const FCataclysmItem&, FCataclysmItem& Changed, const UCataclysmEmpireRun&, FString&)
		{
			Changed.GearLevel = Plan.ToLevel;
			return true;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			const int32 StonesLeft = Inventory->CountOfMaterial(Plan.Stone);
			UE_LOG(LogCataclysm, Log, TEXT("The forge raised %s from +%d to +%d with two %s (%d left); residue %.0f to %.0f; "
										   "%d day(s), day %d to day %d."),
				   *Name, Plan.FromLevel, Plan.ToLevel, *Plan.Stone.ToString(), StonesLeft, Plan.ResidueBefore,
				   Done.ResidueAfter, Done.Days, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeUpgradeLine(
				Name, Plan.FromLevel, Plan.ToLevel, StonesLeft, Done.Gold, FMath::RoundToInt(Plan.ResidueBefore),
				FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
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

	const FCataclysmForgeCombine Plan = UCataclysmForge::CombineOf(
		Level, Inventory->CountOfMaterial(UCataclysmForge::StoneFor(Level)), GoldTierOf(WorldContext),
		GoldHeldIn(WorldContext));
	if (!Plan.bPossible)
	{
		OutReason = Plan.Refusal;
		return false;
	}

	// NO ROOM IS ASKED FOR THE MADE STONE: since #2849 the materials tab holds one stack per material with no limit, so
	// a stone always has room, whatever the gear tab holds. Until then this refused when the bag had no free cell.

	// THE GOLD FIRST, given back if a later step fails (#2830).
	bool bGoldFromTheCharacter = false;
	if (!PayForTheCraft(WorldContext, Plan.Gold, bGoldFromTheCharacter))
	{
		OutReason = TEXT("The gold could not be taken; nothing changed.");
		return false;
	}
	if (!Inventory->RemoveMaterial(Plan.From, Plan.StonesNeeded))
	{
		GiveTheGoldBack(WorldContext, Plan.Gold);
		OutReason = TEXT("The stones could not be taken from the bag.");
		return false;
	}
	if (!Inventory->AddMaterial(Plan.Into, 1))
	{
		Inventory->AddMaterial(Plan.From, Plan.StonesNeeded);
		GiveTheGoldBack(WorldContext, Plan.Gold);
		OutReason = TEXT("The made stone had nowhere to go; nothing changed.");
		return false;
	}

	const int32 FromHeld = Inventory->CountOfMaterial(Plan.From);
	const int32 IntoHeld = Inventory->CountOfMaterial(Plan.Into);
	UE_LOG(LogCataclysm, Log, TEXT("The forge combined two %s into one %s: %d and %d held."), *Plan.From.ToString(),
		   *Plan.Into.ToString(), FromHeld, IntoHeld);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeCombineLine(Plan.FromLevel, Plan.IntoLevel, FromHeld,
																	   IntoHeld, Plan.Gold));

	// SAVED: the stones are in the character record; the gold in the account record when it paid.
	SaveWhereTheGoldCameFrom(ForgeWorldOf(WorldContext), Plan.Gold, bGoldFromTheCharacter);
	UCataclysmSaveWriter::NoteTriggerIn(ForgeWorldOf(WorldContext), ECataclysmSaveTrigger::ForgeCrafted);

	OutDone = Plan;
	return true;
}

bool UCataclysmForgeCraft::CombineGems(const UObject* WorldContext, FName Gem, ECataclysmRarity Rarity,
									   FCataclysmForgeGemCombine& OutDone, FString& OutReason)
{
	OutDone = FCataclysmForgeGemCombine();
	OutReason = RefusalNow(WorldContext);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	UCataclysmInventoryComponent* Inventory = Character->GetInventory();

	const FCataclysmForgeGemCombine Plan = UCataclysmForge::GemCombineOf(
		Gem, Rarity, Inventory->CountOfGems(Gem, Rarity), GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
	if (!Plan.bPossible)
	{
		OutReason = Plan.Refusal;
		return false;
	}

	// NO ROOM IS ASKED FOR THE MADE GEM: since #2849 the gems tab holds one stack per gem and rarity with no limit.

	// THE GOLD FIRST, given back if a later step fails (#2830).
	bool bGoldFromTheCharacter = false;
	if (!PayForTheCraft(WorldContext, Plan.Gold, bGoldFromTheCharacter))
	{
		OutReason = TEXT("The gold could not be taken; nothing changed.");
		return false;
	}
	if (!Inventory->RemoveGems(Gem, Plan.From, Plan.GemsNeeded))
	{
		GiveTheGoldBack(WorldContext, Plan.Gold);
		OutReason = TEXT("The gems could not be taken from the bag.");
		return false;
	}
	if (!Inventory->AddGems(Gem, Plan.Into, 1))
	{
		Inventory->AddGems(Gem, Plan.From, Plan.GemsNeeded);
		GiveTheGoldBack(WorldContext, Plan.Gold);
		OutReason = TEXT("The made gem had nowhere to go; nothing changed.");
		return false;
	}

	const int32 FromHeld = Inventory->CountOfGems(Gem, Plan.From);
	const int32 IntoHeld = Inventory->CountOfGems(Gem, Plan.Into);
	UE_LOG(LogCataclysm, Log, TEXT("The forge combined two %s at rarity %d into one at %d: %d and %d held."),
		   *Gem.ToString(), static_cast<int32>(Plan.From), static_cast<int32>(Plan.Into), FromHeld, IntoHeld);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeGemCombineLine(
		UCataclysmGems::NameOf(Gem), UCataclysmGems::RarityName(Plan.From), UCataclysmGems::RarityName(Plan.Into),
		FromHeld, IntoHeld, Plan.Gold));

	// SAVED: the gems are in the character record; the gold in the account record when it paid.
	SaveWhereTheGoldCameFrom(ForgeWorldOf(WorldContext), Plan.Gold, bGoldFromTheCharacter);
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
		return UCataclysmForge::SocketGemOf(FCataclysmItem(), Socket, Gem, Rarity, FString(), 0, 0,
											GoldTierOf(WorldContext), 0);
	}
	const UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	const FCataclysmGemRow* Row = UCataclysmGems::RowOf(UCataclysmGems::LoadGeneratedTable(), Gem);
	return UCataclysmForge::SocketGemOf(*Item, Socket, Gem, Rarity, Row ? Row->GemType : FString(),
										Inventory->CountOfGems(Gem, Rarity),
										Inventory->CountOfMaterial(FName(UCataclysmForge::SettingAgentRow)),
										GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

FCataclysmForgeSocketing UCataclysmForgeCraft::UnsocketPlanFor(const UObject* WorldContext,
															   const FCataclysmForgeTarget& Target, int32 Socket)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::UnsocketGemOf(FCataclysmItem(), Socket, 0, false, GoldTierOf(WorldContext), 0);
	}
	const UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	const int32 Dust = Inventory->CountOfMaterial(FName(UCataclysmForge::DismantlingDustRow));
	// ROOM FOR THE GEM IS ALWAYS THERE since #2849: the gems tab holds one stack per gem and rarity with no limit,
	// whatever the gear tab holds. Until then this asked for a stack to join, a free cell, or the cell the last
	// Dismantling Dust emptied, which stopped being true once the Dust and the gem were carried apart.
	const FCataclysmSocket* From = Item->Sockets.IsValidIndex(Socket) ? &Item->Sockets[Socket] : nullptr;
	const bool bRoom = From != nullptr;
	return UCataclysmForge::UnsocketGemOf(*Item, Socket, Dust, bRoom, GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::SocketGem(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													  int32 Socket, FName Gem, ECataclysmRarity Rarity,
													  bool bConfirmedPastThreshold, FCataclysmForgeSocketing& OutDone,
													  FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
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
	return DoSocketing(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::UnsocketGem(const UObject* WorldContext,
														const FCataclysmForgeTarget& Target, int32 Socket,
														bool bConfirmedPastThreshold, FCataclysmForgeSocketing& OutDone,
														FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
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
	return DoSocketing(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::DoSocketing(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														const FCataclysmForgeSocketing& Plan,
														bool bConfirmedPastThreshold,
														FCataclysmForgeSocketing& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}

	// ONE MATERIAL, AND FOR A SOCKETING THE GEM, TAKEN; FOR AN UNSOCKETING THE GEM GIVEN BACK AFTER THE CHANGE. Everything
	// else is `DoItemCraft`'s (slice F5).
	FCataclysmForgeCraftStep Step =
		ForgeStepOf(Target, Plan.Material, 1, Plan.Gold, Effect,
					Plan.bUnsocket ? TEXT("Unsocketing a gem from") : TEXT("Socketing a gem into"));
	Step.NothingThereRefusal =
		Plan.bUnsocket ? TEXT("There is nothing there to unsocket.") : TEXT("There is nothing there to socket.");
	UCataclysmInventoryComponent* Inventory = CharacterIn(WorldContext)->GetInventory();
	FCataclysmForgeCraftHooks Hooks;
	if (Plan.bUnsocket)
	{
		Hooks.AfterTheChange = [Inventory, Plan]() { return Inventory->AddGems(Plan.Gem, Plan.Rarity, 1); };
		Hooks.AfterRefusal = TEXT("The gem had nowhere to go in the bag; nothing changed.");
	}
	else
	{
		Hooks.TakeBeforeTheChange = [Inventory, Plan]() { return Inventory->RemoveGems(Plan.Gem, Plan.Rarity, 1); };
		Hooks.TakeRefusal = TEXT("The gem could not be taken from the bag; nothing changed.");
		Hooks.GiveBackWhatWasTaken = [Inventory, Plan]() { Inventory->AddGems(Plan.Gem, Plan.Rarity, 1); };
	}
	FCataclysmForgeSocketing Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&Plan](const FCataclysmItem&, FCataclysmItem& Changed, const UCataclysmEmpireRun&, FString&)
		{
			FCataclysmSocket& Socket = Changed.Sockets[Plan.Socket];
			Socket.Gem = Plan.bUnsocket ? NAME_None : Plan.Gem;
			Socket.GemRarity = Plan.bUnsocket ? ECataclysmRarity::Everyday : Plan.Rarity;
			return true;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			const FString GemName = UCataclysmGems::NameOf(Plan.Gem);
			const FString RarityName = UCataclysmGems::RarityName(Plan.Rarity);
			UE_LOG(LogCataclysm, Log, TEXT("The forge %s %s (%s) %s %s; residue %.0f to %.0f; %d day(s), day %d to day %d."),
				   Plan.bUnsocket ? TEXT("unsocketed") : TEXT("socketed"), *GemName, *RarityName,
				   Plan.bUnsocket ? TEXT("from") : TEXT("into"), *Name, Plan.ResidueBefore, Done.ResidueAfter, Done.Days,
				   DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeSocketLine(
				Plan.bUnsocket, GemName, RarityName, UCataclysmGems::SocketTypeName(Plan.SocketType), Name, Done.Gold,
				FMath::RoundToInt(Plan.ResidueBefore), FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		Hooks, OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}

int32 UCataclysmForgeCraft::MaxSocketsOf(const FCataclysmItem& Item)
{
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const FCataclysmItemBaseRow* BaseRow = BaseTable != nullptr && !Item.Base.IsNone()
		? BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("UCataclysmForgeCraft::MaxSocketsOf"),
													/*bWarnIfMissing=*/false)
		: nullptr;
	return BaseRow != nullptr ? UCataclysmDropRoll::MaxSocketsFor(UCataclysmDropRoll::LoadItemSocketTable(), *BaseRow)
							  : -1;
}

FCataclysmForgeSocketWork UCataclysmForgeCraft::AddSocketPlanFor(const UObject* WorldContext,
																 const FCataclysmForgeTarget& Target)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::AddSocketOf(FCataclysmItem(), 0, 0, GoldTierOf(WorldContext), 0);
	}
	return UCataclysmForge::AddSocketOf(
		*Item, MaxSocketsOf(*Item),
		Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::ShatteredCoreRow)), GoldTierOf(WorldContext),
		GoldHeldIn(WorldContext));
}

FCataclysmForgeSocketWork UCataclysmForgeCraft::ChangeSocketTypePlanFor(const UObject* WorldContext,
																		const FCataclysmForgeTarget& Target,
																		int32 Socket, ECataclysmSocketType NewType)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::ChangeSocketTypeOf(FCataclysmItem(), Socket, NewType, 0, GoldTierOf(WorldContext), 0);
	}
	return UCataclysmForge::ChangeSocketTypeOf(
		*Item, Socket, NewType, Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::FocusingLensRow)),
		GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::AddSocket(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													  bool bConfirmedPastThreshold, FCataclysmForgeSocketWork& OutDone,
													  FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeSocketWork();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeSocketWork Plan = AddSocketPlanFor(WorldContext, Target);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoSocketWork(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::ChangeSocketType(const UObject* WorldContext,
															 const FCataclysmForgeTarget& Target, int32 Socket,
															 ECataclysmSocketType NewType, bool bConfirmedPastThreshold,
															 FCataclysmForgeSocketWork& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeSocketWork();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeSocketWork Plan = ChangeSocketTypePlanFor(WorldContext, Target, Socket, NewType);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoSocketWork(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::DoSocketWork(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														 const FCataclysmForgeSocketWork& Plan,
														 bool bConfirmedPastThreshold,
														 FCataclysmForgeSocketWork& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	FCataclysmForgeCraftStep Step = ForgeStepOf(Target, Plan.Material, 1, Plan.Gold, Effect,
												Plan.bAdd ? TEXT("Adding a socket to") : TEXT("Changing a socket of"));
	Step.NothingThereRefusal =
		Plan.bAdd ? TEXT("There is nothing there to add a socket to.") : TEXT("There is nothing there to change.");
	FCataclysmForgeSocketWork Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&](const FCataclysmItem& Before, FCataclysmItem& Changed, const UCataclysmEmpireRun& Run, FString&)
		{
			// AN ADDED SOCKET'S TYPE IS ROLLED FROM THE ITEM AS IT STOOD, THE RUN'S MASK AND THE DAY BEFORE THE CRAFT'S DAYS
			// PASS, so the same item on the same day of the same run rolls the same type (ruled 2026-11-22).
			if (Plan.bAdd)
			{
				Done.ToType = UCataclysmForge::AddedSocketType(Before, Run.WalkSeedMask, Run.Day());
				FCataclysmSocket& NewSocket = Changed.Sockets.AddDefaulted_GetRef();
				NewSocket.Type = Done.ToType;
			}
			else
			{
				Changed.Sockets[Plan.Socket].Type = Plan.ToType;
			}
			return true;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			const FString ToType = UCataclysmGems::SocketTypeName(Done.ToType);
			UE_LOG(LogCataclysm, Log, TEXT("The forge %s socket %d of %s, %s; residue %.0f to %.0f; %d day(s), day %d to day %d."),
				   Plan.bAdd ? TEXT("added") : TEXT("changed"), Plan.Socket + 1, *Name, *ToType, Plan.ResidueBefore,
				   Done.ResidueAfter, Done.Days, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeSocketWorkLine(
				Plan.bAdd, Plan.Socket + 1, UCataclysmGems::SocketTypeName(Plan.FromType), ToType, Name, Done.Gold,
				FMath::RoundToInt(Plan.ResidueBefore), FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}

FCataclysmForgeAffixWork UCataclysmForgeCraft::AffixWorkPlanFor(const UObject* WorldContext,
																const FCataclysmForgeTarget& Target,
																ECataclysmForgeAffixCraft Craft, int32 Affix)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::AffixWorkOf(FCataclysmItem(), Craft, Affix, 0, GoldTierOf(WorldContext), 0);
	}
	return UCataclysmForge::AffixWorkOf(*Item, Craft, Affix,
										Character->GetInventory()->CountOfMaterial(UCataclysmForge::MaterialFor(Craft)),
										GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::CraftAffix(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														ECataclysmForgeAffixCraft Craft, int32 Affix,
														bool bConfirmedPastThreshold, FCataclysmForgeAffixWork& OutDone,
														FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeAffixWork();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeAffixWork Plan = AffixWorkPlanFor(WorldContext, Target, Craft, Affix);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	const TCHAR* Doing = Craft == ECataclysmForgeAffixCraft::PerfectRoll ? TEXT("Perfecting an affix of")
		: Craft == ECataclysmForgeAffixCraft::UpgradeTier				  ? TEXT("Raising an affix's tier on")
																		  : TEXT("Rerolling an affix of");
	const FCataclysmForgeCraftStep Step = ForgeStepOf(Target, Plan.Material, 1, Plan.Gold, Effect, Doing);
	FCataclysmForgeAffixWork Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&](const FCataclysmItem& Before, FCataclysmItem& Changed, const UCataclysmEmpireRun& Run, FString&)
		{
			// THE AFFIX CHANGED, AND NOTHING ELSE OF IT. A reroll's roll is drawn from the item as it stood, the run's mask
			// and the day before the craft's days pass, so the same item on the same day of the same run draws the same.
			if (Craft == ECataclysmForgeAffixCraft::RerollValue)
			{
				Done.ToRoll = UCataclysmForge::RerolledAffixRoll(Before, Plan.Affix, Run.WalkSeedMask, Run.Day());
			}
			FCataclysmRolledAffix& Affixed = Changed.Affixes[Plan.Affix];
			Affixed.Tier = Done.ToTier;
			Affixed.Roll = Done.ToRoll;
			return true;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			UE_LOG(LogCataclysm, Log, TEXT("The forge changed affix %d (%s) of %s: tier %d to %d, roll %.4f to %.4f; residue %.0f "
										   "to %.0f; %d day(s), day %d to day %d."),
				   Plan.Affix + 1, *Plan.AffixRow.ToString(), *Name, Done.FromTier, Done.ToTier, Done.FromRoll, Done.ToRoll,
				   Plan.ResidueBefore, Done.ResidueAfter, Done.Days, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeAffixLine(
				Craft, Plan.Affix + 1, Name, Done.Gold, Done.FromTier, Done.ToTier, UCataclysmForge::RollPercent(Done.FromRoll),
				UCataclysmForge::RollPercent(Done.ToRoll), FMath::RoundToInt(Plan.ResidueBefore),
				FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}

namespace
{
	/** The run's mask and day an affix craft's draw is seeded from: day 0 of no mask with no run. */
	void ForgeSeedOfTheRun(const UObject* WorldContext, int32& OutMask, int32& OutDay)
	{
		const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
		OutMask = Run != nullptr ? Run->WalkSeedMask : 0;
		OutDay = Run != nullptr ? Run->Day() : 0;
	}
}

FCataclysmForgeAffixChange UCataclysmForgeCraft::AddAffixPlanFor(const UObject* WorldContext,
																 const FCataclysmForgeTarget& Target, FName AffixRow,
																 int32 Tier)
{
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::DeterministicAffixOf(FCataclysmItem(), AffixRow, Tier, 1, AffixTable, BaseTable, 0,
													 GoldTierOf(WorldContext), 0);
	}
	int32 Mask = 0;
	int32 Day = 0;
	ForgeSeedOfTheRun(WorldContext, Mask, Day);
	const int32 Seed = UCataclysmForge::AffixCraftSeed(*Item, UCataclysmForge::DeterministicAffixSalt,
													   Item->Affixes.Num(), Mask, Day);
	return UCataclysmForge::DeterministicAffixOf(
		*Item, AffixRow, Tier, Seed, AffixTable, BaseTable,
		Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::SchematicFragmentsRow)), GoldTierOf(WorldContext),
		GoldHeldIn(WorldContext));
}

FCataclysmForgeAffixChange UCataclysmForgeCraft::ShiftAffixPlanFor(const UObject* WorldContext,
																   const FCataclysmForgeTarget& Target, int32 Affix)
{
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::AffixShiftOf(FCataclysmItem(), Affix, 1, AffixTable, BaseTable, 0, GoldTierOf(WorldContext),
											 0);
	}
	int32 Mask = 0;
	int32 Day = 0;
	ForgeSeedOfTheRun(WorldContext, Mask, Day);
	const int32 Seed = UCataclysmForge::AffixCraftSeed(*Item, UCataclysmForge::ShiftedAffixSalt, Affix, Mask, Day);
	return UCataclysmForge::AffixShiftOf(
		*Item, Affix, Seed, AffixTable, BaseTable,
		Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::CrystalOfInstabilityRow)),
		GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::AddAffix(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													  FName AffixRow, int32 Tier, bool bConfirmedPastThreshold,
													  FCataclysmForgeAffixChange& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeAffixChange();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeAffixChange Plan = AddAffixPlanFor(WorldContext, Target, AffixRow, Tier);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoAffixChange(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::ShiftAffix(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
														int32 Affix, bool bConfirmedPastThreshold,
														FCataclysmForgeAffixChange& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeAffixChange();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeAffixChange Plan = ShiftAffixPlanFor(WorldContext, Target, Affix);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoAffixChange(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::DoAffixChange(const UObject* WorldContext,
														   const FCataclysmForgeTarget& Target,
														   const FCataclysmForgeAffixChange& Plan,
														   bool bConfirmedPastThreshold,
														   FCataclysmForgeAffixChange& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeCraftStep Step = ForgeStepOf(Target, Plan.Material, 1, Plan.Gold, Effect,
													  Plan.bShift ? TEXT("Shifting an affix of") : TEXT("Adding an affix to"));
	FCataclysmForgeAffixChange Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&Plan](const FCataclysmItem&, FCataclysmItem& Changed, const UCataclysmEmpireRun&, FString&)
		{
			// THE ITEM CHANGED AS THE PLAN DREW IT, for the run's mask and the day before the craft's days pass. A shift
			// writes the new row and its damage types over the affix and keeps its tier and roll; an addition puts the new
			// affix last.
			FCataclysmRolledAffix NewAffix;
			NewAffix.Affix = Plan.ToRow;
			NewAffix.Tier = Plan.Tier;
			NewAffix.Roll = Plan.Roll;
			NewAffix.DamageTypes = Plan.ToDamageTypes;
			if (Plan.bShift)
			{
				Changed.Affixes[Plan.Affix] = NewAffix;
			}
			else
			{
				Changed.Affixes.Add(NewAffix);
			}
			return true;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
			UE_LOG(LogCataclysm, Log, TEXT("The forge %s affix %d (%s to %s, tier %d, roll %.4f) of %s; residue %.0f to %.0f; %d "
										   "day(s), day %d to day %d."),
				   Plan.bShift ? TEXT("shifted") : TEXT("added"), Plan.Affix + 1, *Plan.FromRow.ToString(),
				   *Plan.ToRow.ToString(), Plan.Tier, Plan.Roll, *Name, Plan.ResidueBefore, Done.ResidueAfter, Done.Days,
				   DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeAffixChangeLine(
				Plan.bShift, Plan.Affix + 1, UCataclysmForge::AffixNameOf(Plan.FromRow, AffixTable),
				UCataclysmForge::AffixNameOf(Plan.ToRow, AffixTable), Name, Done.Gold, Plan.Tier,
				UCataclysmGems::RarityName(Plan.ToRarity), FMath::RoundToInt(Plan.ResidueBefore),
				FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}

FCataclysmForgeDamageTypeWork UCataclysmForgeCraft::DamageTypeWorkPlanFor(const UObject* WorldContext,
																		  const FCataclysmForgeTarget& Target,
																		  ECataclysmForgeDamageTypeCraft Craft, FName Chosen)
{
	const UDataTable* SkillTable = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::DamageTypeWorkOf(FCataclysmItem(), Craft, Chosen, 1, SkillTable, BaseTable, 0,
												 GoldTierOf(WorldContext), 0);
	}
	int32 Mask = 0;
	int32 Day = 0;
	ForgeSeedOfTheRun(WorldContext, Mask, Day);
	// A CHANGE'S DRAW IS SEEDED BY THE PLACE OF THE TYPE IT REPLACES, AN INFUSION'S BY THE PLACE THE NEW TYPE TAKES.
	const bool bChange = Craft == ECataclysmForgeDamageTypeCraft::Change;
	const int32 Seed = UCataclysmForge::AffixCraftSeed(
		*Item, bChange ? UCataclysmForge::ChangedDamageTypeSalt : UCataclysmForge::InfusedDamageTypeSalt,
		bChange ? Item->DamageTypes.IndexOfByKey(Chosen) : Item->DamageTypes.Num(), Mask, Day);
	return UCataclysmForge::DamageTypeWorkOf(
		*Item, Craft, Chosen, Seed, SkillTable, BaseTable,
		Character->GetInventory()->CountOfMaterial(UCataclysmForge::MaterialFor(Craft)), GoldTierOf(WorldContext),
		GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::CraftDamageType(const UObject* WorldContext,
															 const FCataclysmForgeTarget& Target,
															 ECataclysmForgeDamageTypeCraft Craft, FName Chosen,
															 bool bConfirmedPastThreshold,
															 FCataclysmForgeDamageTypeWork& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeDamageTypeWork();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeDamageTypeWork Plan = DamageTypeWorkPlanFor(WorldContext, Target, Craft, Chosen);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeCraftStep Step =
		ForgeStepOf(Target, Plan.Material, 1, Plan.Gold, Effect,
					Craft == ECataclysmForgeDamageTypeCraft::Change ? TEXT("Changing a damage type of")
																	: TEXT("Infusing a damage type into"));
	FCataclysmForgeDamageTypeWork Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	int32 TypesAfter = Plan.TypesHeld;
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&](const FCataclysmItem&, FCataclysmItem& Changed, const UCataclysmEmpireRun&, FString&)
		{
			// THE WEAPON'S LIST CHANGED AS THE PLAN SAYS: a change puts the new type in the chosen one's place, keeping the
			// order; an infusion adds it last.
			const int32 Replaced = Changed.DamageTypes.IndexOfByKey(Plan.FromType);
			if (Craft == ECataclysmForgeDamageTypeCraft::Change && Replaced != INDEX_NONE)
			{
				Changed.DamageTypes[Replaced] = Plan.ToType;
			}
			else
			{
				Changed.DamageTypes.Add(Plan.ToType);
			}
			TypesAfter = Changed.DamageTypes.Num();
			return true;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			UE_LOG(LogCataclysm, Log, TEXT("The forge %s %s on %s (%d to %d of at most %d); residue %.0f to %.0f; %d day(s), day "
										   "%d to day %d."),
				   Craft == ECataclysmForgeDamageTypeCraft::Change ? TEXT("changed a damage type to") : TEXT("infused"),
				   *Plan.ToType.ToString(), *Name, Plan.TypesHeld, TypesAfter, Plan.MaxTypes, Plan.ResidueBefore,
				   Done.ResidueAfter, Done.Days, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeDamageTypeLine(
				Craft, Name, Plan.FromType.ToString(), Plan.ToType.ToString(), Done.Gold,
				FMath::RoundToInt(Plan.ResidueBefore), FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}

namespace
{
	/**
	 * An enchantment pair as the tool tip reads it: its positive's line, then its drawback's without the tool tip's
	 * "Drawback: " word, which the play loop's line says itself. A row the tables do not hold reads as its row name.
	 */
	TPair<FString, FString> EnchantmentWords(const FCataclysmRolledEnchantment& Pair)
	{
		const TArray<FString> Lines = UCataclysmItemTooltip::EnchantmentLines(
			Pair, UCataclysmDropRoll::LoadPositiveEnchantmentTable(), UCataclysmDropRoll::LoadNegativeEnchantmentTable());
		FString Drawback = Lines.IsValidIndex(1) ? Lines[1] : Pair.Negative.ToString();
		Drawback.RemoveFromStart(UCataclysmItemTooltip::DrawbackPrefix);
		return TPair<FString, FString>(Lines.IsValidIndex(0) ? Lines[0] : Pair.Positive.ToString(), Drawback);
	}
}

FCataclysmForgeEnchantmentWork UCataclysmForgeCraft::ImprintPlanFor(const UObject* WorldContext,
																	const FCataclysmForgeTarget& Target, int32 Affix)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::ImprintOf(FCataclysmItem(), Affix, 0, UCataclysmDropRoll::LoadGearRarityTable(),
										  GoldTierOf(WorldContext), 0);
	}
	return UCataclysmForge::ImprintOf(
		*Item, Affix, Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::AetherialShardRow)),
		UCataclysmDropRoll::LoadGearRarityTable(), GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

FCataclysmForgeEnchantmentWork UCataclysmForgeCraft::ChangeNegativePlanFor(const UObject* WorldContext,
																		   const FCataclysmForgeTarget& Target,
																		   int32 Pair)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::ChangeNegativeOf(FCataclysmItem(), Pair, 0,
												 UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
												 GoldTierOf(WorldContext), 0);
	}
	return UCataclysmForge::ChangeNegativeOf(
		*Item, Pair, Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::RegretEssenceRow)),
		UCataclysmDropRoll::LoadPositiveEnchantmentTable(), GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::Imprint(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													int32 Affix, bool bConfirmedPastThreshold,
													FCataclysmForgeEnchantmentWork& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeEnchantmentWork();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeEnchantmentWork Plan = ImprintPlanFor(WorldContext, Target, Affix);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoEnchantmentWork(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::ChangeNegative(const UObject* WorldContext,
														   const FCataclysmForgeTarget& Target, int32 Pair,
														   bool bConfirmedPastThreshold,
														   FCataclysmForgeEnchantmentWork& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	OutDone = FCataclysmForgeEnchantmentWork();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeEnchantmentWork Plan = ChangeNegativePlanFor(WorldContext, Target, Pair);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	return DoEnchantmentWork(WorldContext, Target, Plan, bConfirmedPastThreshold, OutDone, OutMessage, Options);
}

ECataclysmForgeResult UCataclysmForgeCraft::DoEnchantmentWork(const UObject* WorldContext,
															  const FCataclysmForgeTarget& Target,
															  const FCataclysmForgeEnchantmentWork& Plan,
															  bool bConfirmedPastThreshold,
															  FCataclysmForgeEnchantmentWork& OutDone, FString& OutMessage, const FCataclysmForgeCraftOptions& Options)
{
	const FCataclysmForgeOptionEffect Effect =
		OptionsFor(WorldContext, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (!Effect.bPossible)
	{
		OutMessage = Effect.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	FCataclysmForgeCraftStep Step = ForgeStepOf(Target, Plan.Material, 1, Plan.Gold, Effect,
												Plan.bImprint ? TEXT("Imprinting") : TEXT("Changing a drawback of"));
	Step.NothingThereRefusal =
		Plan.bImprint ? TEXT("There is nothing there to imprint.") : TEXT("There is nothing there to change.");
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	FCataclysmForgeEnchantmentWork Done = Plan;
	Done.ResidueAfter = Step.ResidueAfter;
	Done.Days = Step.Days;
	Done.Gold = Step.Gold;
	FCataclysmItem AsItStood;
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, bConfirmedPastThreshold,
		[&](const FCataclysmItem& Before, FCataclysmItem& Changed, const UCataclysmEmpireRun& Run, FString& OutRefusal)
		{
			// THE ITEM CHANGED, IN SLICE F1'S ORDER (`CraftAffix`): after the gold and the material, before the days. The
			// pair is drawn from the item as it stood, the run's mask and the day before the craft's days pass, so the same
			// item on the same day of the same run draws the same, and an imprint draws the same pair whichever affix the
			// player gives up.
			AsItStood = Before;
			const UDataTable* Positives = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
			const UDataTable* Negatives = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
			bool bDrawn = false;
			if (Plan.bImprint)
			{
				FRandomStream AfterTheDraw;
				bDrawn = UCataclysmForge::DrawImprint(Before, BaseTable, Positives, Negatives, Run.WalkSeedMask, Run.Day(),
													  Done.Drawn, AfterTheDraw)
					&& UCataclysmForge::ApplyImprint(Before, Plan.Affix, Done.Drawn, BaseTable,
													 UCataclysmWeaponSkills::LoadGeneratedTable(),
													 UCataclysmItemModifiers::LoadEnchantmentEffectTable(), AfterTheDraw,
													 Changed);
			}
			else
			{
				bDrawn = UCataclysmForge::DrawChangedNegative(Before, Plan.Pair, BaseTable, Positives, Negatives,
															  Run.WalkSeedMask, Run.Day(), Done.Drawn);
				if (bDrawn)
				{
					Changed.Enchantments[Plan.Pair] = Done.Drawn;
				}
			}
			if (!bDrawn)
			{
				// WHAT IT TOOK GOES BACK, as when the piece cannot be replaced: `DoItemCraft` gives back the material, the
				// gold and the mark.
				UE_LOG(LogCataclysm, Error, TEXT("The forge could not draw an enchantment; what it took is back in the bag."));
				OutRefusal = TEXT("No enchantment could be drawn for that piece; nothing changed.");
			}
			return bDrawn;
		},
		[&](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			// THE LINE: what was drawn, and what was given up or replaced, as the tool tip reads them.
			const TPair<FString, FString> Drawn = EnchantmentWords(Done.Drawn);
			const FString GivenUpOrReplaced = Plan.bImprint
				? UCataclysmItemTooltip::AffixLine(AsItStood.Affixes[Plan.Affix], AsItStood, BaseTable, AffixTable)
				: EnchantmentWords(Plan.Before).Value;
			UE_LOG(LogCataclysm, Log, TEXT("The forge %s %s: %s / %s, for %s; residue %.0f to %.0f; %d day(s), day %d to day %d."),
				   Plan.bImprint ? TEXT("imprinted") : TEXT("changed a drawback of"), *Name, *Drawn.Key, *Drawn.Value,
				   *GivenUpOrReplaced, Plan.ResidueBefore, Done.ResidueAfter, Done.Days, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeEnchantmentWorkLine(
				Plan.bImprint, Name, Drawn.Key, Drawn.Value, GivenUpOrReplaced, Done.Gold,
				FMath::RoundToInt(Plan.ResidueBefore), FMath::RoundToInt(Done.ResidueAfter), Done.Days, DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}

int32 UCataclysmForgeCraft::MaterialTierOf(FName Material)
{
	const UDataTable* Table = UCataclysmDropRoll::LoadCraftingMaterialTable();
	const FCataclysmCraftingMaterialRow* Row = Table != nullptr && !Material.IsNone()
		? Table->FindRow<FCataclysmCraftingMaterialRow>(Material, TEXT("UCataclysmForgeCraft::MaterialTierOf"),
														/*bWarnIfMissing=*/false)
		: nullptr;
	return Row != nullptr ? Row->Tier : 0;
}

bool UCataclysmForgeCraft::TakeMaterials(UCataclysmInventoryComponent* Inventory,
										 const TArray<FCataclysmForgeMaterialTake>& Materials,
										 TArray<FCataclysmForgeMaterialTake>& OutTaken)
{
	OutTaken.Reset();
	for (const FCataclysmForgeMaterialTake& Take : Materials)
	{
		if (Take.Material.IsNone() || Take.Count <= 0)
		{
			continue;
		}
		// EVERY MATERIAL IS TAKEN IN SLICE F5. THE GOLD SESSION'S F6b DECIDES HERE, BY `MaterialTierOf(Take.Material)`,
		// whether Material Conservation, Resource Management or Tempting Fate keeps one (Q17); the option materials are
		// in this list like any other.
		if (!Inventory->RemoveMaterial(Take.Material, Take.Count))
		{
			GiveMaterialsBack(Inventory, OutTaken);
			OutTaken.Reset();
			return false;
		}
		OutTaken.Add(Take);
	}
	return true;
}

void UCataclysmForgeCraft::GiveMaterialsBack(UCataclysmInventoryComponent* Inventory,
											 const TArray<FCataclysmForgeMaterialTake>& Taken)
{
	for (const FCataclysmForgeMaterialTake& Take : Taken)
	{
		Inventory->AddMaterial(Take.Material, Take.Count);
	}
}

ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(
	const UObject* WorldContext, const FCataclysmForgeCraftStep& Step, bool bConfirmedPastThreshold,
	TFunctionRef<bool(const FCataclysmItem& Before, FCataclysmItem& Changed, const UCataclysmEmpireRun& Run,
					  FString& OutRefusal)> ChangeTheItem,
	TFunctionRef<void(const FString& ItemName, int32 DayFrom, int32 DayTo)> SayTheLine,
	const FCataclysmForgeCraftHooks& Hooks, FString& OutMessage)
{
	UWorld* World = ForgeWorldOf(WorldContext);
	ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	if (Character == nullptr || Character->GetInventory() == nullptr || Character->GetEquipment() == nullptr)
	{
		OutMessage = TEXT("No character is playing, so there is nothing to craft for.");
		return ECataclysmForgeResult::Refused;
	}
	UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();

	// THE DAYS NEED A RUN TO PASS IN, and a drawn result reads the run's mask and day.
	UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone*/ false);
	if (Run == nullptr)
	{
		OutMessage = Step.NoRunRefusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmItem* Held = ItemAt(*Character, Step.Target);
	if (Held == nullptr)
	{
		OutMessage = Step.NothingThereRefusal;
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmItem Before = *Held; // A COPY: the pointer is into the component's own array.
	const FString Name = UCataclysmForge::NameOf(Before, UCataclysmItemModifiers::LoadBaseTable(),
												 UCataclysmDropRoll::LoadAffixTable());
	const float Added = Step.ResidueAfter - Before.Residue;

	// THE THRESHOLD, FOR A WORN PIECE ONLY: a carried piece's residue is not worn. Only the crossing asks, the equip's
	// reading of 2026-11-01 (`FCataclysmThresholdCrossing::Crosses`).
	bool bCrosses = false;
	if (Step.Target.bWorn)
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
				*Step.ThresholdWords, Name.IsEmpty() ? TEXT("this") : *Name, FMath::RoundToInt(After),
				FMath::RoundToInt(Threshold), UCataclysmWearing::ThresholdConsequence);
			return ECataclysmForgeResult::AsksAboutTheThreshold;
		}
	}

	// THE MARK BEFORE THE CHANGE, as `WearPastTheThreshold` sets it, so the record the craft writes holds it. A craft that
	// did not happen takes back the mark it set, and only that one: a cleanse, which lowers residue, never crosses, so
	// it never clears a mark already made (Q26).
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

	// THE GOLD, ONCE, THE CRAFT'S AND ITS OPTIONS'; THEN THE MATERIALS; THEN WHAT THE CRAFT TAKES BESIDES. A refusal after
	// anything is taken puts it back, so nothing is half done.
	bool bGoldFromTheCharacter = false;
	if (!PayForTheCraft(WorldContext, Step.Gold, bGoldFromTheCharacter))
	{
		TakeBackTheMark();
		OutMessage = TEXT("The gold could not be taken; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}
	TArray<FCataclysmForgeMaterialTake> Taken;
	if (!TakeMaterials(Inventory, Step.Materials, Taken))
	{
		GiveTheGoldBack(WorldContext, Step.Gold);
		TakeBackTheMark();
		OutMessage = TEXT("The material could not be taken from the bag; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}
	if (Hooks.TakeBeforeTheChange && !Hooks.TakeBeforeTheChange())
	{
		GiveMaterialsBack(Inventory, Taken);
		GiveTheGoldBack(WorldContext, Step.Gold);
		TakeBackTheMark();
		OutMessage = Hooks.TakeRefusal;
		return ECataclysmForgeResult::Refused;
	}
	const auto GiveBackTheTake = [&]()
	{
		if (Hooks.GiveBackWhatWasTaken)
		{
			Hooks.GiveBackWhatWasTaken();
		}
	};

	// THE ITEM CHANGED, from a copy of it as it stood and the run before the days pass, so a drawn result reads the day
	// the craft began; then its residue, the step's, after the options.
	FCataclysmItem Changed = Before;
	FString Refusal;
	if (!ChangeTheItem(Before, Changed, *Run, Refusal))
	{
		GiveBackTheTake();
		GiveMaterialsBack(Inventory, Taken);
		GiveTheGoldBack(WorldContext, Step.Gold);
		TakeBackTheMark();
		OutMessage = Refusal.IsEmpty() ? FString(TEXT("The item could not be changed; nothing changed.")) : Refusal;
		return ECataclysmForgeResult::Refused;
	}
	Changed.Residue = Step.ResidueAfter;
	const auto PutTheItem = [&](const FCataclysmItem& Item)
	{
		return Step.Target.bWorn ? Equipment->ReplaceEquippedAt(Step.Target.WornSlot, Item)
								 : Inventory->ReplaceItemAt(Step.Target.CarriedSlot, Item);
	};
	if (!PutTheItem(Changed))
	{
		GiveBackTheTake();
		GiveMaterialsBack(Inventory, Taken);
		GiveTheGoldBack(WorldContext, Step.Gold);
		TakeBackTheMark();
		UE_LOG(LogCataclysm, Error, TEXT("The forge could not put %s back where it was; what it took is back in the bag."),
			   *Name);
		OutMessage = TEXT("The item could not be changed; nothing changed.");
		return ECataclysmForgeResult::Refused;
	}
	if (Hooks.AfterTheChange && !Hooks.AfterTheChange())
	{
		PutTheItem(Before);
		GiveBackTheTake();
		GiveMaterialsBack(Inventory, Taken);
		GiveTheGoldBack(WorldContext, Step.Gold);
		TakeBackTheMark();
		OutMessage = Hooks.AfterRefusal;
		return ECataclysmForgeResult::Refused;
	}

	// THE DAYS, through the run's own day loop, so a surge or a resolve inside them happens on its own day.
	const int32 DayBefore = Run->Day();
	Run->AdvanceDays(Step.Days);

	// AND THE BUILD THE DOUBLE WEARS, CAPTURED AFTER THE CHANGE, so it holds the changed piece; only when none is held.
	if (bCrosses && State != nullptr && !State->HoldsConsumptionBuild())
	{
		State->SetConsumptionBuild(FCataclysmPlayerBuild::Of(*Character));
	}

	// THE CRAFT'S OWN LINE, THEN THE OPTIONS', each said once the days have passed.
	SayTheLine(Name, DayBefore, Run->Day());
	if (Step.Options.bStabilizerUsed || Step.Options.ShardsUsed > 0)
	{
		FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeOptionsLine(
			Name, Step.Options.bStabilizerUsed, Step.Options.ShardsUsed, Step.Options.ResidueSaved, Step.Options.DaysSaved,
			Step.Options.Gold));
	}

	// SAVED: the character record holds the item and the materials, the run record the days, the account record the
	// gold when it paid.
	SaveWhereTheGoldCameFrom(World, Step.Gold, bGoldFromTheCharacter);
	UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::ForgeCrafted);
	return ECataclysmForgeResult::Done;
}

FCataclysmForgeOptionEffect UCataclysmForgeCraft::OptionsFor(const UObject* WorldContext, float ResidueBefore,
															 float ResidueAfter, int32 Days, int64 CraftGold,
															 const FCataclysmForgeCraftOptions& Options)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const UCataclysmInventoryComponent* Inventory = Character != nullptr ? Character->GetInventory() : nullptr;
	return UCataclysmForge::OptionsOn(
		ResidueBefore, ResidueAfter, Days, CraftGold, Options,
		Inventory != nullptr ? Inventory->CountOfMaterial(FName(UCataclysmForge::ChaosStabilizerRow)) : 0,
		Inventory != nullptr ? Inventory->CountOfMaterial(FName(UCataclysmForge::TaintedShardRow)) : 0,
		GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

FCataclysmForgeCleanse UCataclysmForgeCraft::CleansePlanFor(const UObject* WorldContext,
															const FCataclysmForgeTarget& Target)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Item == nullptr)
	{
		return UCataclysmForge::CleanseOf(FCataclysmItem(), 0, GoldTierOf(WorldContext), 0);
	}
	return UCataclysmForge::CleanseOf(
		*Item, Character->GetInventory()->CountOfMaterial(FName(UCataclysmForge::PurifiedEssenceRow)),
		GoldTierOf(WorldContext), GoldHeldIn(WorldContext));
}

ECataclysmForgeResult UCataclysmForgeCraft::Cleanse(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													FCataclysmForgeCleanse& OutDone, FString& OutMessage)
{
	OutDone = FCataclysmForgeCleanse();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeCleanse Plan = CleansePlanFor(WorldContext, Target);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}

	// ONE PURIFIED ESSENCE, THE RESIDUE HALVED, THE ROW'S DAYS (Q25); no option, and no threshold question, since the
	// residue falls. Everything else is `DoItemCraft`'s.
	FCataclysmForgeCraftStep Step;
	Step.Target = Target;
	Step.Materials.Add({Plan.Material, 1});
	Step.Gold = Plan.Gold;
	Step.ResidueAfter = Plan.ResidueAfter;
	Step.Days = Plan.Days;
	Step.ThresholdWords = TEXT("Cleansing");
	Step.NothingThereRefusal = TEXT("There is nothing there to cleanse.");
	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, /*bConfirmedPastThreshold=*/false,
		[](const FCataclysmItem&, FCataclysmItem&, const UCataclysmEmpireRun&, FString&) { return true; },
		[&Plan](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			UE_LOG(LogCataclysm, Log, TEXT("The forge cleansed %s: residue %.0f to %.0f; %d day(s), day %d to day %d."), *Name,
				   Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeCleanseLine(
				Name, Plan.Gold, FMath::RoundToInt(Plan.ResidueBefore), FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
				DayFrom, DayTo));
		},
		FCataclysmForgeCraftHooks(), OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Plan;
	}
	return Result;
}

FCataclysmForgeDismantle UCataclysmForgeCraft::DismantlePlanFor(const UObject* WorldContext,
																const FCataclysmForgeTarget& Target)
{
	const ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	const FCataclysmItem* Item = Character != nullptr ? ItemAt(*Character, Target) : nullptr;
	if (Target.bWorn && Item != nullptr)
	{
		FCataclysmForgeDismantle Worn;
		Worn.Refusal = TEXT("Take the piece off before dismantling it.");
		return Worn;
	}
	// THE TWO NODES, read where the yield is read, as the dungeon reads the tree at every floor (#2741 slice 5a).
	const FCataclysmEmpireTreePlayerBonuses Tree = UCataclysmGameInstance::EmpireTreePlayerBonusesFor(WorldContext);
	return UCataclysmForge::DismantleOf(Item != nullptr ? *Item : FCataclysmItem(), Tree.DismantleMaterialsIncreased,
										Tree.DismantleMaterialsIncreasedSuperbAndAbove,
										UCataclysmDropRoll::LoadGearRarityTable());
}

ECataclysmForgeResult UCataclysmForgeCraft::Dismantle(const UObject* WorldContext, const FCataclysmForgeTarget& Target,
													  FCataclysmForgeDismantle& OutDone, FString& OutMessage)
{
	OutDone = FCataclysmForgeDismantle();
	OutMessage = RefusalNow(WorldContext);
	if (!OutMessage.IsEmpty())
	{
		return ECataclysmForgeResult::Refused;
	}
	const FCataclysmForgeDismantle Plan = DismantlePlanFor(WorldContext, Target);
	if (!Plan.bPossible)
	{
		OutMessage = Plan.Refusal;
		return ECataclysmForgeResult::Refused;
	}
	ACataclysmPlayerCharacter* Character = CharacterIn(WorldContext);
	UCataclysmInventoryComponent* Inventory = Character != nullptr ? Character->GetInventory() : nullptr;
	if (Inventory == nullptr)
	{
		OutMessage = TEXT("No character is playing, so there is nothing to dismantle.");
		return ECataclysmForgeResult::Refused;
	}
	// D3: THE PLAYER'S OWN MAGIC FIND AT THE DISMANTLE, a judgement; Auto-Shatter will pass the kill's.
	float MagicFind = 0.0f;
	float LootQuantity = 0.0f;
	UCataclysmDropSpawner::LootStatsOf(Character, MagicFind, LootQuantity);

	// INSTANT (D2): no material, no gold, no days, and the residue the piece already carries, which goes with it.
	FCataclysmForgeCraftStep Step;
	Step.Target = Target;
	Step.Gold = 0;
	Step.Days = 0;
	if (const FCataclysmItem* Held = ItemAt(*Character, Target))
	{
		Step.ResidueAfter = Held->Residue;
	}
	Step.ThresholdWords = TEXT("Dismantling");
	Step.NothingThereRefusal = TEXT("There is nothing there to dismantle.");

	FCataclysmForgeDismantle Done = Plan;
	FCataclysmItem Dismantled;
	FCataclysmForgeCraftHooks Hooks;
	Hooks.AfterRefusal = TEXT("The piece could not be dismantled; nothing changed.");
	Hooks.AfterTheChange = [&Done, &Dismantled, Inventory, &Target]() -> bool
	{
		// NO ROOM IS ASKED (issue #2849): the materials and gems a dismantle gives back go to stores that never refuse.
		// Until #2849 a full bag refused here first, "Make room in the bag first." (D4).

		// THE PIECE LEAVES THE BAG, THEN WHAT IT GIVES JOINS IT. Anything that cannot be added puts back what was added
		// and the piece itself, so nothing is half done.
		if (!Inventory->RemoveItemAt(Target.CarriedSlot))
		{
			return false;
		}
		TArray<FName> AddedMaterials;
		TArray<TPair<FName, ECataclysmRarity>> AddedGems;
		const auto PutEverythingBack = [&]()
		{
			for (const FName& Material : AddedMaterials)
			{
				Inventory->RemoveMaterial(Material, 1);
			}
			for (const TPair<FName, ECataclysmRarity>& Gem : AddedGems)
			{
				Inventory->RemoveGems(Gem.Key, Gem.Value, 1);
			}
			Inventory->AddItem(Dismantled);
		};
		const auto AddOne = [&](FName Material) -> bool
		{
			if (!Inventory->AddMaterial(Material, 1))
			{
				return false;
			}
			AddedMaterials.Add(Material);
			return true;
		};
		bool bAdded = true;
		for (int32 Each = 0; bAdded && Each < Done.Fragments; ++Each)
		{
			bAdded = AddOne(FName(UCataclysmForge::SchematicFragmentsRow));
		}
		for (int32 Each = 0; bAdded && Each < Done.Dust; ++Each)
		{
			bAdded = AddOne(FName(UCataclysmForge::DismantlingDustRow));
		}
		for (int32 Each = 0; bAdded && Each < Done.RefundStones.Num(); ++Each)
		{
			bAdded = AddOne(Done.RefundStones[Each]);
		}
		for (int32 Each = 0; bAdded && Each < Done.BaseStones.Num(); ++Each)
		{
			bAdded = AddOne(Done.BaseStones[Each]);
		}
		for (int32 Each = 0; bAdded && Each < Done.Gems.Num(); ++Each)
		{
			bAdded = Inventory->AddGems(Done.Gems[Each].Key, Done.Gems[Each].Value, 1);
			if (bAdded)
			{
				AddedGems.Add(Done.Gems[Each]);
			}
		}
		if (!bAdded)
		{
			PutEverythingBack();
			UE_LOG(LogCataclysm, Error, TEXT("A dismantle could not add what it gives after the room check passed; the piece "
											 "and the bag are as they were."));
			return false;
		}
		return true;
	};

	const ECataclysmForgeResult Result = DoItemCraft(
		WorldContext, Step, /*bConfirmedPastThreshold=*/false,
		[&Done, &Dismantled, MagicFind](const FCataclysmItem& Before, FCataclysmItem& Changed, const UCataclysmEmpireRun& Run,
										 FString& OutRefusal) -> bool
		{
			// THE DRAW, FROM THE ITEM AS IT STOOD AND THE DAY BEFORE ANYTHING MOVES (D5), so a reload draws the same.
			FRandomStream Stream(UCataclysmForge::AffixCraftSeed(Before, UCataclysmForge::DismantleSalt, 0,
																 Run.WalkSeedMask, Run.Day()));
			UCataclysmForge::DrawDismantle(Done, UCataclysmDropRoll::LoadMaterialTierTable(), MagicFind,
										   UCataclysmForge::DismantleStoneTier(Before, Run.DifficultyTier), Stream);
			Dismantled = Before;
			Changed = Before;
			return true;
		},
		[&Done](const FString& Name, int32 DayFrom, int32 DayTo)
		{
			const FString Given = UCataclysmForge::DismantleGiven(Done);
			UE_LOG(LogCataclysm, Log, TEXT("The forge dismantled %s into %s; day %d to day %d."), *Name, *Given, DayFrom, DayTo);
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeDismantleLine(Name, Given, DayFrom));
		},
		Hooks, OutMessage);
	if (Result == ECataclysmForgeResult::Done)
	{
		OutDone = Done;
	}
	return Result;
}
