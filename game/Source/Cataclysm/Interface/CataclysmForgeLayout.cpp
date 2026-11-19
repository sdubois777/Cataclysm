// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmForgeLayout.h"

#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"

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
		TEXT(" Raising it to +%d takes two +%d stones (%d held), adds %d residue (to %d) and costs %d day%s of the run."),
		Plan.ToLevel, Plan.ToLevel, Plan.StonesHeld, FMath::RoundToInt(UCataclysmForge::ResiduePerLevel),
		FMath::RoundToInt(Plan.ResidueAfter), Plan.Days, Plan.Days == 1 ? TEXT("") : TEXT("s"));
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
