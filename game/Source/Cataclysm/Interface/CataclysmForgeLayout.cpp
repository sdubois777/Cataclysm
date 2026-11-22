// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmForgeLayout.h"

#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmDropRoll.h"
// A gold amount as a player reads it, issue #2830.
#include "Items/CataclysmDroppedItem.h"
// The gems a socket fits and their names, issue #46 slice G4.
#include "Items/CataclysmGems.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
// An affix read as the tool tip reads it, issue #47 slice F1.
#include "Interface/CataclysmItemTooltip.h"

namespace
{
	const TCHAR* const ForgeWornPrefix = TEXT("Worn_");
	const TCHAR* const ForgeCarriedPrefix = TEXT("Carried_");
	const TCHAR* const ForgeCombinePrefix = TEXT("Combine_");
}

FString UCataclysmForgeLayout::Title()
{
	return TEXT("Cataclysmic Forge");
}

TArray<FCataclysmForgeEntry> UCataclysmForgeLayout::EntriesFor(const UCataclysmEquipmentComponent* Equipment,
															   const UCataclysmInventoryComponent* Inventory)
{
	TArray<FCataclysmForgeEntry> Out;
	if (Equipment != nullptr)
	{
		for (int32 Index = 0; Index < UCataclysmEquipmentComponent::SlotCount; ++Index)
		{
			const ECataclysmGearSlot Slot = static_cast<ECataclysmGearSlot>(Index);
			if (const FCataclysmItem* Worn = Equipment->EquippedAt(Slot))
			{
				FCataclysmForgeEntry Entry;
				Entry.Target = FCataclysmForgeTarget::Worn(Slot);
				Entry.ButtonName = ButtonNameFor(Entry.Target);
				Entry.Label = LabelFor(*Worn, Entry.Target);
				Out.Add(MoveTemp(Entry));
			}
		}
	}
	if (Inventory != nullptr)
	{
		for (int32 Cell = 0; Cell < Inventory->GetSlots().Num(); ++Cell)
		{
			if (const FCataclysmItem* Carried = Inventory->ItemAt(Cell))
			{
				FCataclysmForgeEntry Entry;
				Entry.Target = FCataclysmForgeTarget::Carried(Cell);
				Entry.ButtonName = ButtonNameFor(Entry.Target);
				Entry.Label = LabelFor(*Carried, Entry.Target);
				Out.Add(MoveTemp(Entry));
			}
		}
	}
	return Out;
}

FString UCataclysmForgeLayout::LabelFor(const FCataclysmItem& Item, const FCataclysmForgeTarget& Target)
{
	const FString Name = UCataclysmForge::NameOf(Item, UCataclysmItemModifiers::LoadBaseTable(),
												 UCataclysmDropRoll::LoadAffixTable());
	return Target.bWorn
		? FString::Printf(TEXT("Worn, %s: %s +%d"), *UCataclysmGearSlots::DisplayName(Target.WornSlot), *Name,
						  Item.GearLevel)
		: FString::Printf(TEXT("Carried: %s +%d"), *Name, Item.GearLevel);
}

FName UCataclysmForgeLayout::ButtonNameFor(const FCataclysmForgeTarget& Target)
{
	return Target.bWorn
		? FName(*FString::Printf(TEXT("%s%d"), ForgeWornPrefix, static_cast<int32>(Target.WornSlot)))
		: FName(*FString::Printf(TEXT("%s%d"), ForgeCarriedPrefix, Target.CarriedSlot));
}

bool UCataclysmForgeLayout::TargetForButton(FName ButtonName, FCataclysmForgeTarget& OutTarget)
{
	const FString Text = ButtonName.ToString();
	if (Text.StartsWith(ForgeWornPrefix))
	{
		const int32 Index = FCString::Atoi(*Text.RightChop(FCString::Strlen(ForgeWornPrefix)));
		if (Index < 0 || Index >= UCataclysmEquipmentComponent::SlotCount)
		{
			return false;
		}
		OutTarget = FCataclysmForgeTarget::Worn(static_cast<ECataclysmGearSlot>(Index));
		return true;
	}
	if (Text.StartsWith(ForgeCarriedPrefix))
	{
		OutTarget = FCataclysmForgeTarget::Carried(FCString::Atoi(*Text.RightChop(FCString::Strlen(ForgeCarriedPrefix))));
		return true;
	}
	return false;
}

FString UCataclysmForgeLayout::DetailFor(const FCataclysmItem& Item, const FCataclysmForgeUpgrade& Plan)
{
	const FString Name = UCataclysmForge::NameOf(Item, UCataclysmItemModifiers::LoadBaseTable(),
												 UCataclysmDropRoll::LoadAffixTable());
	const FString Head = FString::Printf(TEXT("%s, +%d, residue %d."), *Name, Item.GearLevel,
										 FMath::RoundToInt(Item.Residue));
	if (Plan.Stone.IsNone())
	{
		return Head + TEXT(" ") + Plan.Refusal;
	}
	const FString Cost = FString::Printf(
		TEXT(" Raising it to +%d takes two +%d stones (%d held) and %s, adds %d residue (to %d) and costs %d day%s of the run."),
		Plan.ToLevel, Plan.ToLevel, Plan.StonesHeld, *ACataclysmDroppedItem::GoldName(Plan.Gold),
		FMath::RoundToInt(UCataclysmForge::ResiduePerLevel), FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
		Plan.Days == 1 ? TEXT("") : TEXT("s"));
	return Head + Cost;
}

FString UCataclysmForgeLayout::UpgradeButtonText(const FCataclysmForgeUpgrade& Plan)
{
	return Plan.Stone.IsNone() ? FString(TEXT("Upgrade")) : FString::Printf(TEXT("Upgrade to +%d"), Plan.ToLevel);
}

FString UCataclysmForgeLayout::StonesLine(const UCataclysmInventoryComponent* Inventory)
{
	TArray<FString> Held;
	for (int32 Level = 1; Inventory != nullptr && Level <= UCataclysmForge::HighestStone; ++Level)
	{
		const int32 Count = Inventory->CountOfMaterial(UCataclysmForge::StoneFor(Level));
		if (Count > 0)
		{
			Held.Add(FString::Printf(TEXT("+%d x%d"), Level, Count));
		}
	}
	return Held.IsEmpty() ? FString(TEXT("No upgrade stones held."))
						  : TEXT("Upgrade stones held: ") + FString::Join(Held, TEXT(", "));
}

FString UCataclysmForgeLayout::CombineButtonText(int32 Level)
{
	return FString::Printf(TEXT("Combine two +%d stones into one +%d"), Level, Level + 1);
}

FName UCataclysmForgeLayout::UpgradeButtonName()
{
	return FName(TEXT("Upgrade"));
}

FName UCataclysmForgeLayout::CombineButtonNameFor(int32 Level)
{
	return FName(*FString::Printf(TEXT("%s%d"), ForgeCombinePrefix, Level));
}

int32 UCataclysmForgeLayout::CombineLevelForButton(FName ButtonName)
{
	const FString Text = ButtonName.ToString();
	return Text.StartsWith(ForgeCombinePrefix) ? FCString::Atoi(*Text.RightChop(FCString::Strlen(ForgeCombinePrefix)))
											   : INDEX_NONE;
}

TArray<int32> UCataclysmForgeLayout::CombineLevelsFor(const UCataclysmInventoryComponent* Inventory)
{
	TArray<int32> Levels;
	for (int32 Level = 1; Inventory != nullptr && Level < UCataclysmForge::HighestStone; ++Level)
	{
		if (Inventory->CountOfMaterial(UCataclysmForge::StoneFor(Level)) > 0)
		{
			Levels.Add(Level);
		}
	}
	return Levels;
}

TArray<FCataclysmForgeSocketAction> UCataclysmForgeLayout::SocketActionsFor(const FCataclysmItem& Item,
																			const UCataclysmInventoryComponent* Inventory)
{
	TArray<FCataclysmForgeSocketAction> Out;
	const UDataTable* GemTable = UCataclysmGems::LoadGeneratedTable();
	// THE PIECE'S OWN "FIT ANY GEM TYPE" ENCHANTMENT, read once for all its sockets (slice G10).
	const UDataTable* EffectTable = UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	const bool bFitsAnyGem = UCataclysmGems::FitsAnyGem(Item, EffectTable);
	for (int32 Index = 0; Index < Item.Sockets.Num(); ++Index)
	{
		const FCataclysmSocket& Socket = Item.Sockets[Index];
		if (!Socket.Gem.IsNone())
		{
			FCataclysmForgeSocketAction& Action = Out.AddDefaulted_GetRef();
			Action.bUnsocket = true;
			Action.Socket = Index;
			Action.Gem = Socket.Gem;
			Action.Rarity = Socket.GemRarity;
			Action.ButtonName = FName(*FString::Printf(TEXT("Unsocket_%d"), Index));
			Action.Label = FString::Printf(TEXT("Unsocket %s (%s) from socket %d"), *UCataclysmGems::NameOf(Socket.Gem),
										   *UCataclysmGems::RarityName(Socket.GemRarity), Index + 1);
			continue;
		}
		if (Inventory == nullptr)
		{
			continue;
		}
		// EACH CARRIED STACK THAT FITS THIS SOCKET, in cell order.
		const TArray<FCataclysmCarriedSlot>& Slots = Inventory->GetSlots();
		for (int32 Cell = 0; Cell < Slots.Num(); ++Cell)
		{
			const FCataclysmCarriedSlot& Carried = Slots[Cell];
			const FCataclysmGemRow* Row = Carried.Gem.IsNone() || Carried.Quantity <= 0
				? nullptr
				: UCataclysmGems::RowOf(GemTable, Carried.Gem);
			if (Row == nullptr || !(bFitsAnyGem || UCataclysmGems::GemFits(Socket.Type, Row->GemType)))
			{
				continue;
			}
			FCataclysmForgeSocketAction& Action = Out.AddDefaulted_GetRef();
			Action.Socket = Index;
			Action.Gem = Carried.Gem;
			Action.Rarity = Carried.GemRarity;
			Action.ButtonName = FName(*FString::Printf(TEXT("Socket_%d_Cell_%d"), Index, Cell));
			const FString Type = bFitsAnyGem || Socket.Type == ECataclysmSocketType::Any
				? FString(TEXT("any gem"))
				: UCataclysmGems::SocketTypeName(Socket.Type);
			Action.Label = FString::Printf(TEXT("Socket %s (%s) into socket %d (%s)"), *UCataclysmGems::NameOf(Carried.Gem),
										   *UCataclysmGems::RarityName(Carried.GemRarity), Index + 1, *Type);
		}
	}
	return Out;
}

bool UCataclysmForgeLayout::SocketActionForButton(FName ButtonName, const TArray<FCataclysmForgeSocketAction>& Shown,
												  FCataclysmForgeSocketAction& OutAction)
{
	for (const FCataclysmForgeSocketAction& Action : Shown)
	{
		if (Action.ButtonName == ButtonName)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

FString UCataclysmForgeLayout::SocketsDetailFor(const FCataclysmItem& Item)
{
	const TArray<FString> Lines = UCataclysmGems::SocketLinesFor(Item);
	return Lines.IsEmpty() ? FString() : FString::Printf(TEXT("Sockets: %s"), *FString::Join(Lines, TEXT("; ")));
}

TArray<FCataclysmForgeSocketWorkAction> UCataclysmForgeLayout::SocketWorkActionsFor(const FCataclysmItem& Item,
																					int32 MaxSockets)
{
	TArray<FCataclysmForgeSocketWorkAction> Out;
	if (Item.Base.IsNone())
	{
		return Out;
	}
	if (Item.Sockets.Num() < MaxSockets)
	{
		FCataclysmForgeSocketWorkAction& Add = Out.AddDefaulted_GetRef();
		Add.bAdd = true;
		Add.Socket = Item.Sockets.Num();
		Add.ButtonName = FName(TEXT("AddSocket"));
		Add.Label = FString::Printf(TEXT("Add a socket (%d of %d)"), Item.Sockets.Num() + 1, MaxSockets);
	}
	const ECataclysmSocketType Types[] = { ECataclysmSocketType::Attack, ECataclysmSocketType::Defense,
										   ECataclysmSocketType::Utility };
	for (int32 Index = 0; Index < Item.Sockets.Num(); ++Index)
	{
		const FCataclysmSocket& Socket = Item.Sockets[Index];
		// NO CHANGE FOR A FULL SOCKET OR ONE FOR ANY GEM, ruled 2026-11-22 (Q4, Q5).
		if (!Socket.Gem.IsNone() || Socket.Type == ECataclysmSocketType::Any)
		{
			continue;
		}
		for (const ECataclysmSocketType Type : Types)
		{
			if (Type == Socket.Type)
			{
				continue;
			}
			FCataclysmForgeSocketWorkAction& Change = Out.AddDefaulted_GetRef();
			Change.Socket = Index;
			Change.NewType = Type;
			const FString TypeName = UCataclysmGems::SocketTypeName(Type);
			Change.ButtonName = FName(*FString::Printf(TEXT("ChangeSocket_%d_%s"), Index, *TypeName));
			Change.Label = FString::Printf(TEXT("Make socket %d %s"), Index + 1, *TypeName);
		}
	}
	return Out;
}

bool UCataclysmForgeLayout::SocketWorkActionForButton(FName ButtonName,
													  const TArray<FCataclysmForgeSocketWorkAction>& Shown,
													  FCataclysmForgeSocketWorkAction& OutAction)
{
	for (const FCataclysmForgeSocketWorkAction& Action : Shown)
	{
		if (Action.ButtonName == ButtonName)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

FString UCataclysmForgeLayout::AffixesDetailFor(const FCataclysmItem& Item)
{
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	TArray<FString> Lines;
	for (int32 Index = 0; Index < Item.Affixes.Num(); ++Index)
	{
		const FCataclysmRolledAffix& Rolled = Item.Affixes[Index];
		// THE TOOL TIP'S OWN LINE, so the forge and the tool tip cannot read one affix two ways; the row key for an affix
		// the table does not hold, which the tool tip leaves out.
		FString Line = UCataclysmItemTooltip::AffixLine(Rolled, Item, BaseTable, AffixTable);
		if (Line.IsEmpty())
		{
			Line = FString::Printf(TEXT("%s (tier %d)"), *Rolled.Affix.ToString(), Rolled.Tier);
		}
		Lines.Add(FString::Printf(TEXT("%d. %s, roll %d percent"), Index + 1, *Line,
								  UCataclysmForge::RollPercent(Rolled.Roll)));
	}
	return Lines.IsEmpty() ? FString() : FString::Printf(TEXT("Affixes: %s"), *FString::Join(Lines, TEXT("; ")));
}

TArray<FCataclysmForgeAffixAction> UCataclysmForgeLayout::AffixActionsFor(const FCataclysmItem& Item)
{
	TArray<FCataclysmForgeAffixAction> Out;
	if (Item.Base.IsNone())
	{
		return Out;
	}
	for (int32 Index = 0; Index < Item.Affixes.Num(); ++Index)
	{
		const FCataclysmRolledAffix& Rolled = Item.Affixes[Index];
		FCataclysmForgeAffixAction& Reroll = Out.AddDefaulted_GetRef();
		Reroll.Craft = ECataclysmForgeAffixCraft::RerollValue;
		Reroll.Affix = Index;
		Reroll.ButtonName = FName(*FString::Printf(TEXT("RerollAffix_%d"), Index));
		Reroll.Label = FString::Printf(TEXT("Reroll affix %d"), Index + 1);
		// NO PERFECTING OF A ROLL AT THE TOP, AND NO UPGRADE AT TIER 7, as the rule refuses them.
		if (Rolled.Roll < 1.0f)
		{
			FCataclysmForgeAffixAction& Perfect = Out.AddDefaulted_GetRef();
			Perfect.Craft = ECataclysmForgeAffixCraft::PerfectRoll;
			Perfect.Affix = Index;
			Perfect.ButtonName = FName(*FString::Printf(TEXT("PerfectAffix_%d"), Index));
			Perfect.Label = FString::Printf(TEXT("Perfect affix %d"), Index + 1);
		}
		if (Rolled.Tier < UCataclysmItemValues::MaxAffixTier)
		{
			FCataclysmForgeAffixAction& Raise = Out.AddDefaulted_GetRef();
			Raise.Craft = ECataclysmForgeAffixCraft::UpgradeTier;
			Raise.Affix = Index;
			Raise.ButtonName = FName(*FString::Printf(TEXT("RaiseAffix_%d"), Index));
			Raise.Label = FString::Printf(TEXT("Raise affix %d to tier %d"), Index + 1, Rolled.Tier + 1);
		}
	}
	return Out;
}

bool UCataclysmForgeLayout::AffixActionForButton(FName ButtonName, const TArray<FCataclysmForgeAffixAction>& Shown,
												 FCataclysmForgeAffixAction& OutAction)
{
	for (const FCataclysmForgeAffixAction& Action : Shown)
	{
		if (Action.ButtonName == ButtonName)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

TArray<FCataclysmForgeGemCombineAction> UCataclysmForgeLayout::GemCombineActionsFor(
	const UCataclysmInventoryComponent* Inventory)
{
	TArray<FCataclysmForgeGemCombineAction> Out;
	if (Inventory == nullptr)
	{
		return Out;
	}
	const TArray<FCataclysmCarriedSlot>& Slots = Inventory->GetSlots();
	for (int32 Cell = 0; Cell < Slots.Num(); ++Cell)
	{
		const FCataclysmCarriedSlot& Carried = Slots[Cell];
		if (Carried.Gem.IsNone() || Carried.Quantity <= 0 || Carried.GemRarity == ECataclysmRarity::Cataclysmic)
		{
			continue;
		}
		const ECataclysmRarity Next = static_cast<ECataclysmRarity>(static_cast<uint8>(Carried.GemRarity) + 1);
		const FString Name = UCataclysmGems::NameOf(Carried.Gem);
		FCataclysmForgeGemCombineAction& Action = Out.AddDefaulted_GetRef();
		Action.Gem = Carried.Gem;
		Action.Rarity = Carried.GemRarity;
		Action.ButtonName = FName(*FString::Printf(TEXT("CombineGem_Cell_%d"), Cell));
		Action.Label = FString::Printf(TEXT("Combine two %s (%s) into one %s (%s), %d held"), *Name,
									   *UCataclysmGems::RarityName(Carried.GemRarity), *Name,
									   *UCataclysmGems::RarityName(Next), Carried.Quantity);
	}
	return Out;
}

bool UCataclysmForgeLayout::GemCombineActionForButton(FName ButtonName,
													  const TArray<FCataclysmForgeGemCombineAction>& Shown,
													  FCataclysmForgeGemCombineAction& OutAction)
{
	for (const FCataclysmForgeGemCombineAction& Action : Shown)
	{
		if (Action.ButtonName == ButtonName)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}
