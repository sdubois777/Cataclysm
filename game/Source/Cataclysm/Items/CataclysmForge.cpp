// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmForge.h"

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmWearing.h"

FName UCataclysmForge::StoneFor(int32 Level)
{
	// THE ROW KEYS OF `game/Data/CraftingMaterials.csv`: a stone is named for the level it takes an item to.
	if (Level < 1 || Level > HighestStone)
	{
		return NAME_None;
	}
	return FName(*FString::Printf(TEXT("Material_Upgrade_Stone_%d"), Level));
}

int32 UCataclysmForge::DaysFor(float Residue)
{
	// THE DESIGN'S GLOBAL TIME PENALTY, `CR / 100` rounded down, on the residue the item carries as the craft begins.
	// A residue below nothing is read as nothing, so no craft costs less than its base day.
	const int32 Penalty = FMath::FloorToInt(FMath::Max(0.0f, Residue) / ResiduePerPenaltyDay);
	return BaseDaysPerLevel + Penalty;
}

FCataclysmForgeUpgrade UCataclysmForge::UpgradeOf(const FCataclysmItem& Item, int32 StonesOfTheNextLevelHeld)
{
	FCataclysmForgeUpgrade Out;
	Out.FromLevel = Item.GearLevel;
	Out.ToLevel = Item.GearLevel + 1;
	Out.StonesHeld = FMath::Max(0, StonesOfTheNextLevelHeld);
	Out.ResidueBefore = Item.Residue;

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to upgrade.");
		return Out;
	}

	// SEQUENTIAL, AND +10 IS THE TOP: "Upgrades are sequential -- you cannot skip levels".
	if (Item.GearLevel >= UCataclysmItemValues::MaxGearLevel)
	{
		Out.ToLevel = Item.GearLevel;
		Out.Refusal = FString::Printf(TEXT("This is +%d, the highest an item can be raised to."), Item.GearLevel);
		return Out;
	}

	Out.Stone = StoneFor(Out.ToLevel);
	Out.StonesNeeded = StonesPerUpgrade;
	Out.ResidueAfter = Item.Residue + ResiduePerLevel;
	Out.Days = DaysFor(Item.Residue);

	if (Out.StonesHeld < Out.StonesNeeded)
	{
		Out.Refusal = FString::Printf(TEXT("Raising it to +%d takes two +%d upgrade stones; %d held."), Out.ToLevel,
									  Out.ToLevel, Out.StonesHeld);
		return Out;
	}

	Out.bPossible = true;
	return Out;
}

FCataclysmForgeCombine UCataclysmForge::CombineOf(int32 Level, int32 StonesOfThisLevelHeld)
{
	FCataclysmForgeCombine Out;
	Out.FromLevel = Level;
	Out.IntoLevel = Level + 1;
	Out.StonesHeld = FMath::Max(0, StonesOfThisLevelHeld);
	Out.StonesNeeded = StonesPerCombine;

	if (Level < 1 || Level >= HighestStone)
	{
		Out.Refusal = Level == HighestStone
			? FString::Printf(TEXT("A +%d stone is the highest; there is none above it to make."), HighestStone)
			: FString::Printf(TEXT("There is no +%d upgrade stone."), Level);
		return Out;
	}

	Out.From = StoneFor(Level);
	Out.Into = StoneFor(Level + 1);
	if (Out.StonesHeld < Out.StonesNeeded)
	{
		Out.Refusal = FString::Printf(TEXT("Combining takes two +%d upgrade stones; %d held."), Level, Out.StonesHeld);
		return Out;
	}

	Out.bPossible = true;
	return Out;
}

FString UCataclysmForge::UpgradeQuestion(const FCataclysmForgeUpgrade& Upgrade)
{
	return FString::Printf(TEXT("Press again to upgrade to +%d: two +%d stones, residue %d to %d, %d day%s of the run."),
						   Upgrade.ToLevel, Upgrade.ToLevel, FMath::RoundToInt(Upgrade.ResidueBefore),
						   FMath::RoundToInt(Upgrade.ResidueAfter), Upgrade.Days, Upgrade.Days == 1 ? TEXT("") : TEXT("s"));
}

FString UCataclysmForge::ThresholdQuestion(const FString& ItemName, float WornResidueAfter, float Threshold)
{
	// THE EQUIP'S WORDS, with the verb of this action, and its consequence in the design's words
	// (`UCataclysmWearing::ThresholdConsequence`).
	return FString::Printf(
		TEXT("Upgrading %s brings your Worn Residue to %d, past this tier's Consumption Threshold of %d. %s"),
		ItemName.IsEmpty() ? TEXT("this") : *ItemName, FMath::RoundToInt(WornResidueAfter),
		FMath::RoundToInt(Threshold), UCataclysmWearing::ThresholdConsequence);
}

FString UCataclysmForge::NameOf(const FCataclysmItem& Item, const UDataTable* BaseTable, const UDataTable* AffixTable)
{
	// ITS WHOLE NAME, AND ELSE ITS BASE'S NAME: `UCataclysmItemName::NameOf` names only an item with a rarity, and a
	// piece with no affix and no enchantment -- the starting weapon -- has none, so its inventory cell calls it by its
	// base's `BaseName`, "Greataxe", and so does this. The row key only for a base the table does not hold.
	FString Name = UCataclysmItemName::NameOf(Item, BaseTable, AffixTable);
	if (Name.IsEmpty() && BaseTable)
	{
		if (const FCataclysmItemBaseRow* Row = BaseTable->FindRow<FCataclysmItemBaseRow>(
				Item.Base, TEXT("UCataclysmForge::NameOf"), /*bWarnIfMissing=*/false))
		{
			Name = Row->BaseName;
		}
	}
	return Name.IsEmpty() ? Item.Base.ToString() : Name;
}
