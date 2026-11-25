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
		// EACH CARRIED STACK THAT FITS THIS SOCKET, in the gems tab's order (by gem, then rarity). Read from the gems tab
		// since #2849; `Cell` is the stack's place in it, as it was a bag cell before.
		const TArray<FCataclysmCarriedSlot>& Slots = Inventory->GetGemStacks();
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

FName UCataclysmForgeLayout::AddAffixPreviousButtonName()
{
	return FName(TEXT("AddAffixPrevious"));
}

FName UCataclysmForgeLayout::AddAffixNextButtonName()
{
	return FName(TEXT("AddAffixNext"));
}

FName UCataclysmForgeLayout::AddAffixTierDownButtonName()
{
	return FName(TEXT("AddAffixTierDown"));
}

FName UCataclysmForgeLayout::AddAffixTierUpButtonName()
{
	return FName(TEXT("AddAffixTierUp"));
}

FName UCataclysmForgeLayout::AddAffixButtonName()
{
	return FName(TEXT("AddAffix"));
}

TArray<FCataclysmForgeAffixChangeAction> UCataclysmForgeLayout::ShiftActionsFor(const FCataclysmItem& Item)
{
	TArray<FCataclysmForgeAffixChangeAction> Out;
	for (int32 Index = 0; !Item.Base.IsNone() && Index < Item.Affixes.Num(); ++Index)
	{
		FCataclysmForgeAffixChangeAction& Shift = Out.AddDefaulted_GetRef();
		Shift.bShift = true;
		Shift.Affix = Index;
		Shift.ButtonName = FName(*FString::Printf(TEXT("ShiftAffix_%d"), Index));
		Shift.Label = FString::Printf(TEXT("Shift affix %d"), Index + 1);
	}
	return Out;
}

TArray<FCataclysmForgeAffixChangeAction> UCataclysmForgeLayout::AddAffixActionsFor(const FCataclysmItem& Item,
																					const TArray<FName>& Addable,
																					int32 Chosen, int32 Tier,
																					const UDataTable* AffixTable)
{
	TArray<FCataclysmForgeAffixChangeAction> Out;
	// ONLY WITH A FREE SLOT OF THE FOUR AND SOMETHING TO ADD, as `UCataclysmForge::DeterministicAffixOf` refuses the rest.
	if (Item.Base.IsNone() || !Addable.IsValidIndex(Chosen)
		|| Item.Affixes.Num() + Item.EnchantmentCount >= UCataclysmItemValues::SlotsPerPiece)
	{
		return Out;
	}
	const auto Add = [&Out](FName ButtonName, const FString& Label)
	{
		FCataclysmForgeAffixChangeAction& Action = Out.AddDefaulted_GetRef();
		Action.ButtonName = ButtonName;
		Action.Label = Label;
		return &Action;
	};
	Add(AddAffixPreviousButtonName(), TEXT("Previous affix"));
	Add(AddAffixNextButtonName(), TEXT("Next affix"));
	Add(AddAffixTierDownButtonName(), TEXT("Lower tier"));
	Add(AddAffixTierUpButtonName(), TEXT("Raise tier"));
	FCataclysmForgeAffixChangeAction* Adding = Add(
		AddAffixButtonName(),
		FString::Printf(TEXT("Add %s at tier %d"), *UCataclysmForge::AffixNameOf(Addable[Chosen], AffixTable), Tier));
	Adding->Affix = Item.Affixes.Num();
	return Out;
}

FString UCataclysmForgeLayout::AddAffixDetailFor(const FCataclysmItem& Item, const TArray<FName>& Addable, int32 Chosen,
												 int32 Tier, const UDataTable* AffixTable)
{
	if (Item.Base.IsNone())
	{
		return FString();
	}
	if (Item.Affixes.Num() + Item.EnchantmentCount >= UCataclysmItemValues::SlotsPerPiece)
	{
		return TEXT("No affix can be added: that piece's four slots are full.");
	}
	if (!Addable.IsValidIndex(Chosen))
	{
		return FString();
	}
	const FCataclysmAffixRow* Row = AffixTable != nullptr
		? AffixTable->FindRow<FCataclysmAffixRow>(Addable[Chosen], TEXT("UCataclysmForgeLayout::AddAffixDetailFor"),
												  /*bWarnIfMissing=*/false)
		: nullptr;
	return FString::Printf(TEXT("Affix to add: %s (%s), %d of %d, at tier %d"),
						   *UCataclysmForge::AffixNameOf(Addable[Chosen], AffixTable),
						   Row != nullptr ? *Row->Position : TEXT("?"), Chosen + 1, Addable.Num(), Tier);
}

bool UCataclysmForgeLayout::AffixChangeActionForButton(FName ButtonName,
													   const TArray<FCataclysmForgeAffixChangeAction>& Shown,
													   FCataclysmForgeAffixChangeAction& OutAction)
{
	for (const FCataclysmForgeAffixChangeAction& Action : Shown)
	{
		if (Action.ButtonName == ButtonName)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

FName UCataclysmForgeLayout::StabilizerButtonName()
{
	return FName(TEXT("OptionStabilizer"));
}

FName UCataclysmForgeLayout::ShardsButtonName()
{
	return FName(TEXT("OptionShards"));
}

FName UCataclysmForgeLayout::CleanseButtonName()
{
	return FName(TEXT("Cleanse"));
}

TArray<FCataclysmForgeOptionAction> UCataclysmForgeLayout::OptionActionsFor(const FCataclysmItem& Item,
																			 const FCataclysmForgeCraftOptions& Options,
																			 int32 StabilizersHeld, int32 ShardsHeld,
																			 int32 EssenceHeld)
{
	TArray<FCataclysmForgeOptionAction> Out;
	if (Item.Base.IsNone())
	{
		return Out;
	}
	if (StabilizersHeld > 0 || Options.bChaosStabilizer)
	{
		Out.Add({StabilizerButtonName(), FString::Printf(TEXT("Chaos Stabilizer: %s (%d held)"),
														 Options.bChaosStabilizer ? TEXT("on") : TEXT("off"),
														 FMath::Max(0, StabilizersHeld))});
	}
	if (ShardsHeld > 0 || Options.TaintedShards > 0)
	{
		Out.Add({ShardsButtonName(), FString::Printf(TEXT("Tainted Shards: %d of %d held"), FMath::Max(0, Options.TaintedShards),
													 FMath::Max(0, ShardsHeld))});
	}
	if (Item.Residue > 0.0f)
	{
		Out.Add({CleanseButtonName(), FString::Printf(TEXT("Cleanse half the residue (%d Purified Essence held)"),
													  FMath::Max(0, EssenceHeld))});
	}
	return Out;
}

FName UCataclysmForgeLayout::DismantleButtonName()
{
	return FName(TEXT("Dismantle"));
}

FString UCataclysmForgeLayout::DismantleLabelFor(const FCataclysmItem& Item, bool bWorn)
{
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	if (bWorn || Item.Base.IsNone() || Item.bMadeAtCreation
		|| !UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Rarity))
	{
		return FString();
	}
	return TEXT("Dismantle this piece (it is destroyed)");
}

FString UCataclysmForgeLayout::DamageTypesDetailFor(const FCataclysmItem& Item, int32 MaxTypes)
{
	if (Item.Base.IsNone() || MaxTypes <= 0)
	{
		return FString();
	}
	const FString Types = Item.DamageTypes.IsEmpty()
		? FString(TEXT("none"))
		: FString::JoinBy(Item.DamageTypes, TEXT(", "), [](const FName& Type) { return Type.ToString(); });
	if (Item.bMadeAtCreation)
	{
		return FString::Printf(TEXT("Damage types: %s. %s"), *Types, UCataclysmForge::CreationWeaponRefusal);
	}
	return FString::Printf(TEXT("Damage types: %s; %d of at most %d."), *Types, Item.DamageTypes.Num(), MaxTypes);
}

TArray<FCataclysmForgeDamageTypeAction> UCataclysmForgeLayout::DamageTypeActionsFor(const FCataclysmItem& Item,
																					 const TArray<FName>& Open,
																					 int32 MaxTypes)
{
	TArray<FCataclysmForgeDamageTypeAction> Out;
	// NONE FOR AN ITEM THAT IS NO WEAPON, AND NONE FOR THE WEAPON MADE AT CREATION (issue #2845), as the rule refuses them.
	if (Item.Base.IsNone() || MaxTypes <= 0 || Item.bMadeAtCreation)
	{
		return Out;
	}
	// THE INFUSIONS BELOW THE MAXIMUM ONLY (Q20, Q21), and only with a type left to add.
	if (Item.DamageTypes.Num() < MaxTypes && !Open.IsEmpty())
	{
		FCataclysmForgeDamageTypeAction& Random = Out.AddDefaulted_GetRef();
		Random.Craft = ECataclysmForgeDamageTypeCraft::InfuseRandom;
		Random.ButtonName = FName(TEXT("InfuseRandomType"));
		Random.Label = TEXT("Infuse a random damage type");
		for (const FName& Type : Open)
		{
			FCataclysmForgeDamageTypeAction& Specific = Out.AddDefaulted_GetRef();
			Specific.Craft = ECataclysmForgeDamageTypeCraft::InfuseSpecific;
			Specific.Type = Type;
			Specific.ButtonName = FName(*FString::Printf(TEXT("InfuseType_%s"), *Type.ToString()));
			Specific.Label = FString::Printf(TEXT("Infuse %s"), *Type.ToString());
		}
	}
	// A CHANGE FOR EACH TYPE CARRIED, at the maximum too, while there is a type it lacks to draw.
	if (!Open.IsEmpty())
	{
		for (const FName& Type : Item.DamageTypes)
		{
			FCataclysmForgeDamageTypeAction& Change = Out.AddDefaulted_GetRef();
			Change.Craft = ECataclysmForgeDamageTypeCraft::Change;
			Change.Type = Type;
			Change.ButtonName = FName(*FString::Printf(TEXT("ChangeType_%s"), *Type.ToString()));
			Change.Label = FString::Printf(TEXT("Change %s"), *Type.ToString());
		}
	}
	return Out;
}

bool UCataclysmForgeLayout::DamageTypeActionForButton(FName ButtonName,
													  const TArray<FCataclysmForgeDamageTypeAction>& Shown,
													  FCataclysmForgeDamageTypeAction& OutAction)
{
	for (const FCataclysmForgeDamageTypeAction& Action : Shown)
	{
		if (Action.ButtonName == ButtonName)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

TArray<FCataclysmForgeEnchantmentAction> UCataclysmForgeLayout::EnchantmentActionsFor(const FCataclysmItem& Item,
																					  const UDataTable* PositiveTable,
																					  const UDataTable* NegativeTable)
{
	TArray<FCataclysmForgeEnchantmentAction> Out;
	if (Item.Base.IsNone())
	{
		return Out;
	}
	// AN IMPRINT FOR EACH AFFIX, ON A PIECE WITH EVERY SLOT FILLED (Q16), the player choosing which affix goes (Q15). Each
	// is named by its number in slice F1's affix list, `AffixesDetailFor`, which the screen's detail shows.
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	if (UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Rarity)
		&& Item.EnchantmentCount + Item.Affixes.Num() >= UCataclysmItemValues::SlotsPerPiece)
	{
		for (int32 Index = 0; Index < Item.Affixes.Num(); ++Index)
		{
			FCataclysmForgeEnchantmentAction& Imprint = Out.AddDefaulted_GetRef();
			Imprint.bImprint = true;
			Imprint.Index = Index;
			Imprint.Named = FString::Printf(TEXT("affix %d"), Index + 1);
			Imprint.ButtonName = FName(*FString::Printf(TEXT("Imprint_%d"), Index));
			Imprint.Label = FString::Printf(TEXT("Imprint, giving up %s"), *Imprint.Named);
		}
	}
	// A DRAWBACK TO CHANGE FOR EACH ENCHANTMENT THAT IS NOT A SET'S: a set's drawback is the set's own.
	for (int32 Index = 0; Index < Item.Enchantments.Num(); ++Index)
	{
		const FCataclysmRolledEnchantment& Pair = Item.Enchantments[Index];
		const FCataclysmEnchantmentRow* Positive = PositiveTable != nullptr
			? PositiveTable->FindRow<FCataclysmEnchantmentRow>(Pair.Positive, TEXT("EnchantmentActionsFor"),
															   /*bWarnIfMissing=*/false)
			: nullptr;
		if (Positive == nullptr || UCataclysmDropRoll::EnchantmentSetId(*Positive) > 0)
		{
			continue;
		}
		const TArray<FString> Lines = UCataclysmItemTooltip::EnchantmentLines(Pair, PositiveTable, NegativeTable);
		FCataclysmForgeEnchantmentAction& Change = Out.AddDefaulted_GetRef();
		Change.Index = Index;
		Change.Named = Lines.IsValidIndex(0) ? Lines[0] : Pair.Positive.ToString();
		Change.ButtonName = FName(*FString::Printf(TEXT("ChangeDrawback_%d"), Index));
		Change.Label = FString::Printf(TEXT("Change the drawback of %s"), *Change.Named);
	}
	return Out;
}

bool UCataclysmForgeLayout::EnchantmentActionForButton(FName ButtonName,
													   const TArray<FCataclysmForgeEnchantmentAction>& Shown,
													   FCataclysmForgeEnchantmentAction& OutAction)
{
	for (const FCataclysmForgeEnchantmentAction& Action : Shown)
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
	// THE GEMS TAB, in its order (by gem, then rarity), since #2849; `Cell` is the stack's place in it.
	const TArray<FCataclysmCarriedSlot>& Slots = Inventory->GetGemStacks();
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
