// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmItemTooltip.h"

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"

namespace
{
	// EVERY HELPER IN THIS BLOCK CARRIES A NAME NO OTHER FILE IN THE MODULE
	// USES. Unreal merges a module's .cpp files into one translation unit, so
	// two files defining the same private helper collide the moment both land
	// in the same unity blob -- and not before, because a modified file is kept
	// out of the blob. That failure first appears on `development`, after a
	// clean build every time. tools/tests/test_no_two_files_share_an_anonymous_helper.py
	// is what catches it now, and these names are chosen to pass it.

	/** Whether a carried slot holds a piece of gear rather than a material. */
	bool TooltipSlotHoldsGear(const FCataclysmCarriedSlot& Slot)
	{
		return !Slot.Item.Base.IsNone();
	}

	/** The affix table row for a rolled affix, or null. */
	const FCataclysmAffixRow* TooltipAffixRow(const UDataTable* AffixTable,
											  FName Affix)
	{
		if (!AffixTable || Affix.IsNone())
		{
			return nullptr;
		}
		return AffixTable->FindRow<FCataclysmAffixRow>(
			Affix, TEXT("UCataclysmItemTooltip"), /*bWarnIfMissing=*/false);
	}

	/**
	 * A phrase with a leading word removed, and the rest left exactly as it is.
	 *
	 * `Flat maximum health` with `Flat` gives `maximum health`. The comparison
	 * ignores case; the remainder does not have its case touched, because
	 * `Fire` in a resistance name is a proper noun and lowercasing it would be
	 * wrong.
	 */
	FString TooltipWithoutLeadingWord(const FString& Phrase, const TCHAR* Word)
	{
		const FString Prefix = FString(Word) + TEXT(" ");
		if (Phrase.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return Phrase.RightChop(Prefix.Len());
		}
		return Phrase;
	}

	/** The damage types a resistance affix rolled, as `Fire and Cold`. */
	FString TooltipDamageTypeList(const TArray<FName>& Types)
	{
		TArray<FString> Words;
		for (const FName& Type : Types)
		{
			Words.Add(Type.ToString());
		}

		if (Words.Num() == 0)
		{
			return FString();
		}
		if (Words.Num() == 1)
		{
			return Words[0];
		}

		const FString Last = Words.Pop();
		return FString::Join(Words, TEXT(", ")) + TEXT(" and ") + Last;
	}
}

// ---------------------------------------------------------------------------
// Words and numbers
// ---------------------------------------------------------------------------

FString UCataclysmItemTooltip::NumberInWords(float Value)
{
	// WHOLE NUMBERS LOSE THE DECIMAL POINT, which most affix values are not and
	// a few are. `120` reads as a quantity; `120.000000` reads as a bug.
	if (FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value), 0.005f))
	{
		return FString::FromInt(FMath::RoundToInt(Value));
	}
	return FString::Printf(TEXT("%.1f"), Value);
}

FString UCataclysmItemTooltip::StatInWords(const FString& Stat)
{
	if (Stat.IsEmpty())
	{
		return FString();
	}

	FString Words = Stat.Replace(TEXT("_"), TEXT(" "));

	// FOUR EXPANSIONS COVER EVERY STAT IN THE DATA. They are applied to whole
	// words rather than as substrings, so `max_health` becomes `maximum health`
	// while a stat that merely contains those letters is left alone.
	// Cataclysm.Tooltip.EveryStatInTheDataReadsAsWords is what checks that
	// claim against the tables rather than leaving it asserted here.
	static const TMap<FString, FString> Expansions = {
		{TEXT("max"), TEXT("maximum")},
		{TEXT("crit"), TEXT("critical strike")},
		{TEXT("regen"), TEXT("regeneration")},
		{TEXT("dot"), TEXT("damage over time")},
	};

	TArray<FString> Parts;
	Words.ParseIntoArray(Parts, TEXT(" "), /*InCullEmpty=*/true);
	for (FString& Part : Parts)
	{
		if (const FString* Expanded = Expansions.Find(Part))
		{
			Part = *Expanded;
		}
	}

	return FString::Join(Parts, TEXT(" "));
}

// ---------------------------------------------------------------------------
// One line at a time
// ---------------------------------------------------------------------------

FString UCataclysmItemTooltip::ImplicitLine(const FString& Stat,
											const FString& Kind,
											float StatedValue,
											int32 GearLevel,
											bool bTwoHanded)
{
	// AN EMPTY STAT IS HOW A BASE SAYS IT HAS ONLY ONE IMPLICIT, not an error.
	// Every armour base fills Implicit1 and leaves Implicit2 blank.
	if (Stat.IsEmpty())
	{
		return FString();
	}

	const float Value =
		UCataclysmItemValues::ImplicitValue(StatedValue, GearLevel, bTwoHanded);
	const FString Subject = StatInWords(Stat);

	if (Kind.Equals(TEXT("increased"), ESearchCase::IgnoreCase))
	{
		return FString::Printf(TEXT("%s%% increased %s"),
							   *NumberInWords(Value), *Subject);
	}
	return FString::Printf(TEXT("+%s to %s"), *NumberInWords(Value), *Subject);
}

FString UCataclysmItemTooltip::AffixLine(const FCataclysmRolledAffix& Rolled,
										 const FCataclysmItem& Item,
										 const UDataTable* BaseTable,
										 const UDataTable* AffixTable)
{
	const FCataclysmAffixRow* Affix = TooltipAffixRow(AffixTable, Rolled.Affix);
	if (!Affix)
	{
		return FString();
	}

	const bool bTwoHanded = UCataclysmItemModifiers::IsTwoHanded(Item, BaseTable);
	const float Value = UCataclysmItemValues::AffixValue(
		Affix->TopValue, Rolled.Tier, Rolled.Roll, Item.GearLevel, bTwoHanded);

	const FString Tier = FString::Printf(TEXT(" (tier %d)"), Rolled.Tier);

	// -- a resistance names which damage types it covers --------------------
	if (Affix->AffixKind.Equals(TEXT("Resistance"), ESearchCase::IgnoreCase))
	{
		// THE AFFIX SAYS HOW MANY TYPES IT COVERS AND THE ITEM SAYS WHICH, which
		// is the same split UCataclysmItemModifiers::AccumulateInto relies on.
		// A family covering all eight leaves the list empty, because there was
		// no choice to make when it rolled.
		if (Rolled.DamageTypes.Num() == 0)
		{
			return FString::Printf(TEXT("+%s to all resistances%s"),
								   *NumberInWords(Value), *Tier);
		}
		return FString::Printf(TEXT("+%s to %s resistance%s%s"),
							   *NumberInWords(Value),
							   *TooltipDamageTypeList(Rolled.DamageTypes),
							   Rolled.DamageTypes.Num() > 1 ? TEXT("s") : TEXT(""),
							   *Tier);
	}

	// -- an ordinary stat, flat or increased --------------------------------
	if (Affix->ValueKind.Equals(TEXT("flat"), ESearchCase::IgnoreCase))
	{
		return FString::Printf(TEXT("+%s to %s%s"), *NumberInWords(Value),
							   *TooltipWithoutLeadingWord(Affix->AffixName, TEXT("Flat")),
							   *Tier);
	}
	if (Affix->ValueKind.Equals(TEXT("increased"), ESearchCase::IgnoreCase))
	{
		return FString::Printf(TEXT("%s%% increased %s%s"), *NumberInWords(Value),
							   *TooltipWithoutLeadingWord(Affix->AffixName, TEXT("Increased")),
							   *Tier);
	}

	// -- an ailment or a hybrid, which have no such shape -------------------
	// NOT A PLACEHOLDER. An Ailment affix carries no value kind and a Hybrid one
	// grants two stats named in other columns, so neither can be put into
	// "+N to X". Both name themselves clearly in the sheet -- "Chance to bleed",
	// "Health and armor" -- so the sheet's phrase and the number is the honest
	// line, and it is the one 24 of the 85 rows take.
	if (!Affix->AffixName.IsEmpty())
	{
		return FString::Printf(TEXT("%s: %s%s"), *Affix->AffixName,
							   *NumberInWords(Value), *Tier);
	}

	return FString();
}

// ---------------------------------------------------------------------------
// The whole panel
// ---------------------------------------------------------------------------

TArray<FString> UCataclysmItemTooltip::LinesFor(
	const FCataclysmCarriedSlot& Slot, const UDataTable* BaseTable,
	const UDataTable* AffixTable, const UDataTable* CraftingMaterialTable)
{
	TArray<FString> Lines;

	if (UCataclysmInventoryComponent::SlotIsEmpty(Slot))
	{
		// AN EMPTY SLOT SAYS NOTHING RATHER THAN SAYING "EMPTY". A tool tip with
		// no text does not appear at all, which is what hovering over an empty
		// cell should do.
		return Lines;
	}

	// -- a crafting material ------------------------------------------------
	if (!TooltipSlotHoldsGear(Slot))
	{
		const FString Name =
			UCataclysmDropRoll::MaterialNameOf(CraftingMaterialTable, Slot.Material);
		Lines.Add(Name.IsEmpty() ? Slot.Material.ToString() : Name);

		if (Slot.Quantity > 1)
		{
			Lines.Add(FString::Printf(TEXT("%d carried"), Slot.Quantity));
		}

		if (CraftingMaterialTable)
		{
			if (const FCataclysmCraftingMaterialRow* Row =
					CraftingMaterialTable->FindRow<FCataclysmCraftingMaterialRow>(
						Slot.Material, TEXT("UCataclysmItemTooltip"),
						/*bWarnIfMissing=*/false))
			{
				// WHAT IT IS FOR, which is the only reason a player keeps one.
				// Seven of the eighteen materials have no stated source, which
				// is issue #531; that is about where they come from rather than
				// what they do, so PrimaryUse is filled for all of them.
				if (!Row->PrimaryUse.IsEmpty())
				{
					Lines.Add(Row->PrimaryUse);
				}
			}
		}

		return Lines;
	}

	// -- a piece of gear ----------------------------------------------------
	const FCataclysmItem& Item = Slot.Item;

	const FString WholeName = UCataclysmItemName::NameOf(Item, BaseTable, AffixTable);
	if (!WholeName.IsEmpty())
	{
		Lines.Add(WholeName);
	}

	if (Item.GearLevel > 0)
	{
		Lines.Add(FString::Printf(TEXT("+%d"), Item.GearLevel));
	}

	const bool bTwoHanded = UCataclysmItemModifiers::IsTwoHanded(Item, BaseTable);

	const FCataclysmItemBaseRow* Base =
		BaseTable ? BaseTable->FindRow<FCataclysmItemBaseRow>(
						Item.Base, TEXT("UCataclysmItemTooltip"),
						/*bWarnIfMissing=*/false)
				  : nullptr;
	if (Base)
	{
		for (const FString& Line : {
				 ImplicitLine(Base->Implicit1Stat, Base->Implicit1Kind,
							  Base->Implicit1Value, Item.GearLevel, bTwoHanded),
				 ImplicitLine(Base->Implicit2Stat, Base->Implicit2Kind,
							  Base->Implicit2Value, Item.GearLevel, bTwoHanded)})
		{
			if (!Line.IsEmpty())
			{
				Lines.Add(Line);
			}
		}
	}

	for (const FCataclysmRolledAffix& Rolled : Item.Affixes)
	{
		const FString Line = AffixLine(Rolled, Item, BaseTable, AffixTable);
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
	}

	if (Item.Sockets > 0)
	{
		Lines.Add(FString::Printf(TEXT("%d socket%s"), Item.Sockets,
								  Item.Sockets == 1 ? TEXT("") : TEXT("s")));
	}

	if (Item.Residue > 0.0f)
	{
		// A COST AND NEVER A BENEFIT, which is why it is stated last and plainly.
		// It raises what crafting the piece charges and counts toward the Worn
		// Residue that can get a character hunted by a corrupted copy of itself.
		Lines.Add(FString::Printf(TEXT("Cataclysmic Residue %s"),
								  *NumberInWords(Item.Residue)));
	}

	return Lines;
}

FString UCataclysmItemTooltip::TextFor(const FCataclysmCarriedSlot& Slot,
									   const UDataTable* BaseTable,
									   const UDataTable* AffixTable,
									   const UDataTable* CraftingMaterialTable)
{
	return FString::Join(
		LinesFor(Slot, BaseTable, AffixTable, CraftingMaterialTable),
		TEXT("\n"));
}
