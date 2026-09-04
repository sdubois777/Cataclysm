// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmItemTooltip.h"

#include "AbilitySystem/CataclysmWeaponSkills.h"
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

	/**
	 * Whether a stat is measured in percentage points, read out of the
	 * affix rows.
	 *
	 * THE AFFIX TABLE IS THE ONLY TABLE THAT RECORDS IT, so an implicit
	 * asks the affixes about its own stat rather than carrying a second
	 * answer of its own. `validate_implicit_stats_have_an_affix` in
	 * tools/generate_datatables.py refuses a workbook where an implicit
	 * names a stat no affix grants, so this lookup cannot come back
	 * empty and quietly say "not a percentage". Issue #1224.
	 */
	bool TooltipStatIsPercent(const UDataTable* AffixTable, const FString& Stat)
	{
		if (!AffixTable || Stat.IsEmpty())
		{
			return false;
		}

		bool bPercent = false;
		AffixTable->ForeachRow<FCataclysmAffixRow>(
			TEXT("UCataclysmItemTooltip"),
			[&Stat, &bPercent](const FName&, const FCataclysmAffixRow& Row)
			{
				if (Row.Stat.Equals(Stat, ESearchCase::IgnoreCase) && Row.Percent)
				{
					bPercent = true;
				}
			});
		return bPercent;
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

	/**
	 * One ordinary stat affix in words, with the value already worked out.
	 *
	 * THE TIER IS NOT PART OF IT, because a hybrid grants two of these and has
	 * one tier between them. The caller appends it once.
	 *
	 * SHARED BY THE ORDINARY PATH AND THE HYBRID PATH so the two cannot word the
	 * same stat differently. Before issue #1177 there was only one path.
	 *
	 * @return an empty string for a row that is neither flat nor increased,
	 *         which is every Ailment row and every Hybrid row. Those have no
	 *         "+N to X" shape and are not put into one.
	 */
	FString TooltipStatPhrase(const FCataclysmAffixRow& Row, float Value)
	{
		if (Row.ValueKind.Equals(TEXT("flat"), ESearchCase::IgnoreCase))
		{
			// A FLAT ROW WHOSE NAME BEGINS "Increased" GRANTS AN INCREASE,
			// and the two columns are not in conflict. ValueKind says the
			// affix ADDS to the stat rather than scaling it; the stat itself
			// is a bucket of increases, which is why its name says so.
			// Putting such a row into "+N to X" printed "+160 to Increased
			// damage against War enemies", a third sentence shape this
			// project does not use. Issue #1223. The eight rows for damage
			// against one Cataclysm are the only ones in this state, and
			// Cataclysm.Tooltip.EveryAffixInTheDataReads is what holds that.
			if (Row.AffixName.StartsWith(TEXT("Increased "),
										 ESearchCase::IgnoreCase))
			{
				return FString::Printf(TEXT("%s%% increased %s"),
					*UCataclysmItemTooltip::NumberInWords(Value),
					*TooltipWithoutLeadingWord(Row.AffixName, TEXT("Increased")));
			}

			// THE PERCENT SIGN COMES FROM THE STAT, NOT FROM THE VALUE KIND.
			// A flat addition to a stat that is itself measured in percentage
			// points needs the sign as much as an increase does, and eleven
			// rows printed without it -- "+0.2 to life leech" did not say
			// whether that was 0.2 per cent or 0.2 health. Issue #1224.
			return FString::Printf(TEXT("+%s%s to %s"),
				*UCataclysmItemTooltip::NumberInWords(Value),
				Row.Percent ? TEXT("%") : TEXT(""),
				*TooltipWithoutLeadingWord(Row.AffixName, TEXT("Flat")));
		}
		if (Row.ValueKind.Equals(TEXT("increased"), ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT("%s%% increased %s"),
				*UCataclysmItemTooltip::NumberInWords(Value),
				*TooltipWithoutLeadingWord(Row.AffixName, TEXT("Increased")));
		}
		return FString();
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
											bool bTwoHanded,
											const UDataTable* AffixTable)
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

	// AN IMPLICIT ON A PERCENTAGE STAT NEEDS THE SIGN TOO. A helmet's
	// implicit evasion and an evasion affix on the same helmet sit two
	// lines apart, so one of them printing "+4 to evasion" while the other
	// printed "+1.2% to evasion" would read as two different stats.
	// Issue #1224.
	return FString::Printf(TEXT("+%s%s to %s"), *NumberInWords(Value),
						   TooltipStatIsPercent(AffixTable, Stat) ? TEXT("%")
																  : TEXT(""),
						   *Subject);
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
		Affix->TopValue, Affix->Floor, Rolled.Tier, Rolled.Roll,
		Item.GearLevel, bTwoHanded);

	const FString Tier = FString::Printf(TEXT(" (tier %d)"), Rolled.Tier);

	// -- a resistance names which damage types it covers --------------------
	if (Affix->AffixKind.Equals(TEXT("Resistance"), ESearchCase::IgnoreCase))
	{
		// THE AFFIX SAYS HOW MANY TYPES IT COVERS AND THE ITEM SAYS WHICH, which
		// is the same split UCataclysmItemModifiers::AccumulateInto relies on.
		// A family covering all eight leaves the list empty, because there was
		// no choice to make when it rolled.
		//
		// A RESISTANCE IS A PERCENTAGE, and `Damage *= 1 - Resist / 100` is
		// where it is applied. The sign comes off the row like every other
		// one so there is a single answer rather than a rule written twice.
		const TCHAR* const ResistSign = Affix->Percent ? TEXT("%") : TEXT("");
		if (Rolled.DamageTypes.Num() == 0)
		{
			return FString::Printf(TEXT("+%s%s to all resistances%s"),
								   *NumberInWords(Value), ResistSign, *Tier);
		}
		return FString::Printf(TEXT("+%s%s to %s resistance%s%s"),
							   *NumberInWords(Value), ResistSign,
							   *TooltipDamageTypeList(Rolled.DamageTypes),
							   Rolled.DamageTypes.Num() > 1 ? TEXT("s") : TEXT(""),
							   *Tier);
	}

	// -- a hybrid names its two halves in other columns ---------------------
	// ITS OWN COLUMNS ARE EMPTY. All thirteen hybrid rows carry no stat, no
	// value kind and a top value of 0.0, because what they grant is written in
	// HybridPart1 and HybridPart2 instead. So `Value` above is zero for one, and
	// until issue #1177 this fell through to the sheet's-phrase branch below and
	// printed "Health and armor: 0 (tier 3)" on an item that was granting both
	// halves perfectly well. Issue #847 fixed what a hybrid is WORTH and left
	// what it SAYS.
	//
	// THE SAME LOOKUP AND THE SAME SHARE `UCataclysmItemModifiers::AccumulateInto`
	// USES, read from the same constant, so the line and the modifier cannot
	// drift. A part is found by its AffixName rather than by a row name, which is
	// what the columns hold.
	if (Affix->AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
	{
		TArray<FString> Halves;
		for (const FString& PartName : { Affix->HybridPart1, Affix->HybridPart2 })
		{
			const FCataclysmAffixRow* Part =
				UCataclysmItemModifiers::AffixNamed(AffixTable, PartName);
			if (!Part)
			{
				continue;
			}

			const float PartValue = UCataclysmItemValues::AffixValue(
										Part->TopValue, Part->Floor, Rolled.Tier,
										Rolled.Roll, Item.GearLevel, bTwoHanded)
								  * UCataclysmItemValues::HybridFraction;

			const FString Phrase = TooltipStatPhrase(*Part, PartValue);
			if (!Phrase.IsEmpty())
			{
				Halves.Add(Phrase);
			}
		}

		// BOTH HALVES ON ONE LINE, joined, because they came from one affix and
		// occupy one of the item's four slots. Two lines would read as two
		// affixes and make the item look richer than it is.
		//
		// AN EMPTY LIST FALLS THROUGH rather than returning nothing, so a hybrid
		// whose parts are missing from the table still says its own name instead
		// of vanishing off the item.
		if (Halves.Num() > 0)
		{
			return FString::Join(Halves, TEXT(", ")) + Tier;
		}
	}

	// -- an ordinary stat, flat or increased --------------------------------
	const FString Phrase = TooltipStatPhrase(*Affix, Value);
	if (!Phrase.IsEmpty())
	{
		return Phrase + Tier;
	}

	// -- an ailment, which has no such shape --------------------------------
	// NOT A PLACEHOLDER. An Ailment affix carries no value kind, so it cannot be
	// put into "+N to X". It names itself clearly in the sheet -- "Chance to
	// bleed" -- so the sheet's phrase and the number is the honest line, and it
	// is the one the eleven Ailment rows take. A hybrid whose parts could not be
	// looked up reaches here too, which is a data fault rather than a shape.
	if (!Affix->AffixName.IsEmpty())
	{
		// AN AILMENT CHANCE IS A PERCENTAGE. It is capped at 100 and
		// anything past that raises the effect's magnitude instead, so
		// "Chance to bleed: 15" was a percentage printed as a bare number.
		return FString::Printf(TEXT("%s: %s%s%s"), *Affix->AffixName,
							   *NumberInWords(Value),
							   Affix->Percent ? TEXT("%") : TEXT(""), *Tier);
	}

	return FString();
}

// ---------------------------------------------------------------------------
// The whole panel
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// What a weapon is
// ---------------------------------------------------------------------------

TArray<FString> UCataclysmItemTooltip::WeaponLines(const FCataclysmItem& Item,
												   const UDataTable* BaseTable)
{
	TArray<FString> Lines;

	const FCataclysmItemBaseRow* Base =
		BaseTable ? BaseTable->FindRow<FCataclysmItemBaseRow>(
						Item.Base, TEXT("UCataclysmItemTooltip::WeaponLines"),
						/*bWarnIfMissing=*/false)
				  : nullptr;
	if (!Base || Base->WeaponType.IsEmpty())
	{
		// NOT A WEAPON, which game/Data/ItemBases.csv says by leaving the
		// WeaponType column blank. None of the lines below means anything on a
		// helm, and forty-one of the fifty-five bases are not weapons.
		return Lines;
	}

	// WHAT IT IS. The sub type is not trivia: a hit's sub type is the one every
	// weapon swung agrees on and a mixed pair carries none, so a player picking
	// a second weapon cannot otherwise see what that choice costs them.
	FString What = FString::Printf(TEXT("%s %s"),
		Base->Hands == 2 ? TEXT("Two-handed") : TEXT("One-handed"),
		*Base->WeaponType);
	if (!Base->SubType.IsEmpty())
	{
		What.Append(TEXT(", ")).Append(Base->SubType);
	}
	Lines.Add(What);

	// HOW FAST IT SWINGS, AND ONLY FOR A WEAPON THAT IS SWUNG. A Shield is a
	// one-handed weapon carrying no attack damage implicit, so it contributes
	// neither damage nor swing rate to a hit -- UCataclysmItemModifiers::
	// BlendedAttackSpeed averages over the weapons that arm and leaves it out.
	// Its base row still carries an AttackSpeed of 1.2, and printing that would
	// tell a player a shield is swung.
	//
	// TWO DECIMALS RATHER THAN NumberInWords. The designed rates are 1.20, 1.25
	// and 1.28, and one decimal place makes three different weapons read alike.
	if (Base->AttackSpeed > 0.0f
		&& UCataclysmItemModifiers::WeaponDamageForItem(Item, BaseTable) > 0.0f)
	{
		Lines.Add(FString::Printf(TEXT("%.2f attacks per second"),
								  Base->AttackSpeed));
	}

	// WHAT DAMAGE TYPES IT CARRIES, rolled when it dropped. Issue #857.
	if (Item.DamageTypes.Num() > 0)
	{
		TArray<FString> Named;
		for (const FName& Each : Item.DamageTypes)
		{
			Named.Add(Each.ToString());
		}
		Lines.Add(FString::Join(Named, TEXT(", ")));
	}

	// AND HOW MANY IT COULD EVER CARRY, which is the lower of its base's own
	// limit and how many damage types are designed for its weapon type.
	//
	// THE BASE'S FIGURE ALONE WOULD BE WRONG FOR HALF THE WEAPON TYPES. Every
	// two-handed base states 8 and not one has 8 designed for it: a Staff has
	// 7, a Greatsword 6, a Greataxe 4, a 2H Crossbow 3. A Shield states 4 and
	// has 3. Whether the design intends that is issue #875; either way a number
	// shown to a player has to be one they can reach.
	const int32 Designed = UCataclysmDropRoll::DamageTypesAvailableTo(
		UCataclysmWeaponSkills::LoadGeneratedTable(), Base->WeaponType).Num();
	const int32 Ceiling = FMath::Min(Base->MaxDamageTypes, Designed);
	if (Ceiling > 0)
	{
		Lines.Add(FString::Printf(TEXT("Holds up to %d damage type%s"), Ceiling,
								  Ceiling == 1 ? TEXT("") : TEXT("s")));
	}

	return Lines;
}

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

	// WHAT A WEAPON IS, BEFORE WHAT IT GRANTS. Its type, its sub type, its
	// swing rate and its damage types describe the item itself; the implicits
	// and affixes below describe what carrying it does to the character.
	Lines.Append(WeaponLines(Item, BaseTable));

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
							  Base->Implicit1Value, Item.GearLevel, bTwoHanded,
							  AffixTable),
				 ImplicitLine(Base->Implicit2Stat, Base->Implicit2Kind,
							  Base->Implicit2Value, Item.GearLevel, bTwoHanded,
							  AffixTable)})
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
