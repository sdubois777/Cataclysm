// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmDropRoll.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

namespace
{
	/** The slot name the Item Sockets sheet uses for a weapon. */
	const TCHAR* WeaponSlot = TEXT("Weapon");

	/**
	 * One rarity as an index into the ladder, 0 for Everyday and 7 for
	 * Cataclysmic, clamped so an out-of-range enum cannot read past the table.
	 */
	int32 LadderIndex(ECataclysmRarity Rarity)
	{
		return FMath::Clamp(static_cast<int32>(Rarity), 0,
							UCataclysmDropRoll::RarityCount - 1);
	}

	ECataclysmRarity RarityAt(int32 Index)
	{
		return static_cast<ECataclysmRarity>(
			FMath::Clamp(Index, 0, UCataclysmDropRoll::RarityCount - 1));
	}

	/**
	 * The table this project's tables are loaded with: by path, with the reason
	 * spelt out when it is not there.
	 *
	 * NAMES BOTH SCRIPTS, because the two failures look the same from here: the
	 * workbook never produced the CSV, or the CSV was never imported as an
	 * asset. UCataclysmItemModifiers::LoadBaseTable says the same thing.
	 */
	const UDataTable* LoadTableAt(const TCHAR* Path, const TCHAR* CsvName,
								  const TCHAR* SheetName)
	{
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, Path);
		if (!Table)
		{
			UE_LOG(LogCataclysm, Error,
				TEXT("Could not load %s. It is produced by "
					 "tools/generate_datatable_assets.py from game/Data/%s, "
					 "which tools/generate_datatables.py produces from the %s "
					 "sheet of docs/All_Things_Cataclysm.xlsx."),
				Path, CsvName, SheetName);
		}
		return Table;
	}

	/**
	 * The weight of every rung from Everyday up to and including this one.
	 *
	 * Returns 0 when nothing on the way up has a weight, which the caller has to
	 * treat as "there is nothing to choose between" rather than dividing by it.
	 */
	float WeightAtOrBelow(const UDataTable* GearRarityTable,
						  ECataclysmRarity Rarity)
	{
		float Total = 0.0f;
		for (int32 Rung = 0; Rung <= LadderIndex(Rarity); ++Rung)
		{
			if (const FCataclysmGearRarityRow* Row =
					UCataclysmDropRoll::RarityRow(GearRarityTable, RarityAt(Rung)))
			{
				Total += Row->DropWeight;
			}
		}
		return Total;
	}
}

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------

const TCHAR* UCataclysmDropRoll::GearRarityTableAssetPath =
	TEXT("/Game/Data/DT_GearRarity.DT_GearRarity");
const TCHAR* UCataclysmDropRoll::ItemSocketTableAssetPath =
	TEXT("/Game/Data/DT_ItemSockets.DT_ItemSockets");
const TCHAR* UCataclysmDropRoll::AffixTierTableAssetPath =
	TEXT("/Game/Data/DT_AffixTiers.DT_AffixTiers");

const UDataTable* UCataclysmDropRoll::LoadGearRarityTable()
{
	return LoadTableAt(GearRarityTableAssetPath, TEXT("GearRarity.csv"),
					   TEXT("Gear Rarity"));
}

const UDataTable* UCataclysmDropRoll::LoadItemSocketTable()
{
	return LoadTableAt(ItemSocketTableAssetPath, TEXT("ItemSockets.csv"),
					   TEXT("Item Sockets"));
}

const UDataTable* UCataclysmDropRoll::LoadAffixTierTable()
{
	return LoadTableAt(AffixTierTableAssetPath, TEXT("AffixTiers.csv"),
					   TEXT("Affix Tiers"));
}

FName UCataclysmDropRoll::RowNameFor(ECataclysmRarity Rarity)
{
	// THE ENUM'S OWN ENTRY NAME, not its UMETA display name. The generator keys
	// each row on the rarity as the workbook spells it, and the enum entries are
	// spelt the same way; a Python test compares the two lists so this cannot
	// drift. GetNameStringByValue returns the entry name rather than the
	// display name, which is what makes the two agree by construction on the
	// C++ side even if a display name is ever changed for the interface.
	const UEnum* Enum = StaticEnum<ECataclysmRarity>();
	if (!Enum)
	{
		return NAME_None;
	}
	return FName(*Enum->GetNameStringByValue(
		static_cast<int64>(LadderIndex(Rarity))));
}

const FCataclysmGearRarityRow* UCataclysmDropRoll::RarityRow(
	const UDataTable* GearRarityTable, ECataclysmRarity Rarity)
{
	if (!GearRarityTable)
	{
		return nullptr;
	}
	return GearRarityTable->FindRow<FCataclysmGearRarityRow>(
		RowNameFor(Rarity), TEXT("UCataclysmDropRoll::RarityRow"),
		/*bWarnIfMissing=*/false);
}

// ---------------------------------------------------------------------------
// Which rarity a drop rolls
// ---------------------------------------------------------------------------

ECataclysmRarity UCataclysmDropRoll::BestRarityOnADrop(int32 DifficultyTier)
{
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, DifficultyTiers);

	// The tier is one-based and the ladder is zero-based, so tier 1 alone
	// reaches Everyday and Quality: index (1 - 1) + 1.
	return RarityAt(Tier - 1 + RaritiesAboveDifficulty);
}

int32 UCataclysmDropRoll::GearLevelGateFor(const UDataTable* GearRarityTable,
										   ECataclysmRarity Rarity)
{
	const FCataclysmGearRarityRow* Row = RarityRow(GearRarityTable, Rarity);
	return Row ? Row->GearLevelGate : 0;
}

bool UCataclysmDropRoll::ResidueBandFor(const UDataTable* GearRarityTable,
										ECataclysmRarity Rarity,
										float& OutLowest, float& OutHighest)
{
	const FCataclysmGearRarityRow* Row = RarityRow(GearRarityTable, Rarity);
	if (!Row)
	{
		OutLowest = 0.0f;
		OutHighest = 0.0f;
		return false;
	}
	OutLowest = Row->ResidueOnDropLowest;
	OutHighest = Row->ResidueOnDropHighest;
	return true;
}

float UCataclysmDropRoll::RollResidue(const UDataTable* GearRarityTable,
									  ECataclysmRarity Rarity,
									  FRandomStream& Stream)
{
	float Lowest = 0.0f;
	float Highest = 0.0f;
	if (!ResidueBandFor(GearRarityTable, Rarity, Lowest, Highest))
	{
		return 0.0f;
	}

	// RandRange is inclusive at both ends, which is what randint is.
	return static_cast<float>(Stream.RandRange(FMath::TruncToInt(Lowest),
											   FMath::TruncToInt(Highest)));
}

float UCataclysmDropRoll::RarityStepChance(const UDataTable* GearRarityTable,
										   ECataclysmRarity Rarity,
										   float MagicFind)
{
	const FCataclysmGearRarityRow* Row = RarityRow(GearRarityTable, Rarity);
	if (!Row)
	{
		return 0.0f;
	}

	const float AtOrBelow = WeightAtOrBelow(GearRarityTable, Rarity);
	if (AtOrBelow <= 0.0f)
	{
		// Every rung up to here weighs nothing, so there is no share to take.
		// Reported rather than silently zero, because a table that loaded and
		// weighs nothing is a data fault and every drop would be Everyday.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Every gear rarity up to %s has a drop weight of zero, so the "
				 "cascade has nothing to choose between and every drop will be "
				 "the floor. Check game/Data/GearRarity.csv."),
			*RowNameFor(Rarity).ToString());
		return 0.0f;
	}

	const float Share = Row->DropWeight / AtOrBelow;
	return FMath::Min(1.0f, Share * (1.0f + FMath::Max(0.0f, MagicFind) / 100.0f));
}

void UCataclysmDropRoll::RarityDistribution(const UDataTable* GearRarityTable,
											int32 DifficultyTier, float MagicFind,
											TArray<float>& OutShares)
{
	OutShares.Init(0.0f, RarityCount);

	const int32 Best = LadderIndex(BestRarityOnADrop(DifficultyTier));
	float Left = 1.0f;
	for (int32 Rung = Best; Rung >= 1; --Rung)
	{
		const float Chance =
			RarityStepChance(GearRarityTable, RarityAt(Rung), MagicFind);
		OutShares[Rung] = Left * Chance;
		Left *= 1.0f - Chance;
	}

	// WHATEVER FELL THROUGH EVERY RUNG IS THE FLOOR, which is what makes this a
	// cascade rather than a table of weights that has to sum to one by hand.
	OutShares[0] = Left;
}

ECataclysmRarity UCataclysmDropRoll::RollRarity(const UDataTable* GearRarityTable,
												int32 DifficultyTier,
												float MagicFind,
												FRandomStream& Stream)
{
	const int32 Best = LadderIndex(BestRarityOnADrop(DifficultyTier));
	for (int32 Rung = Best; Rung >= 1; --Rung)
	{
		if (Stream.FRand() < RarityStepChance(GearRarityTable, RarityAt(Rung),
											  MagicFind))
		{
			return RarityAt(Rung);
		}
	}
	return ECataclysmRarity::Everyday;
}

// ---------------------------------------------------------------------------
// How many sockets it has
// ---------------------------------------------------------------------------

int32 UCataclysmDropRoll::MaxSocketsFor(const UDataTable* SocketTable,
										const FCataclysmItemBaseRow& Base)
{
	if (!SocketTable)
	{
		return -1;
	}

	// A WEAPON IS MATCHED ON ITS HAND COUNT TOO, because that is what decides
	// its maximum: three for a one-hander and six for a two-hander, so two
	// one-handed weapons match one two-hander. Everything else is one row with
	// a hand count of 0, which is also how the item base table writes it.
	const bool bIsWeapon = Base.Slot.Equals(WeaponSlot, ESearchCase::IgnoreCase);

	int32 Found = -1;
	SocketTable->ForeachRow<FCataclysmItemSocketRow>(
		TEXT("UCataclysmDropRoll::MaxSocketsFor"),
		[&](const FName&, const FCataclysmItemSocketRow& Row)
		{
			if (Found >= 0
				|| !Row.Slot.Equals(Base.Slot, ESearchCase::IgnoreCase))
			{
				return;
			}
			if (bIsWeapon && Row.Hands != Base.Hands)
			{
				return;
			}
			Found = Row.MaxSockets;
		});

	if (Found < 0)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("The %s base %s is in slot '%s' with %d hand(s), and "
				 "game/Data/ItemSockets.csv states no socket maximum for it, so "
				 "it will drop with none."),
			*Base.Slot, *Base.BaseName, *Base.Slot, Base.Hands);
	}
	return Found;
}

int32 UCataclysmDropRoll::RollSockets(const UDataTable* SocketTable,
									  const FCataclysmItemBaseRow& Base,
									  FRandomStream& Stream)
{
	const int32 Maximum = MaxSocketsFor(SocketTable, Base);
	if (Maximum <= 0)
	{
		// A base with no stated maximum drops plain rather than crashing. The
		// warning is in MaxSocketsFor, and the generator refuses a maximum
		// below one, so reaching here means the table itself is missing.
		return 0;
	}
	return Stream.RandRange(0, Maximum);
}

// ---------------------------------------------------------------------------
// What tier its affixes roll at
// ---------------------------------------------------------------------------

int32 UCataclysmDropRoll::MaxAffixTierOnADrop(int32 DifficultyTier)
{
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, DifficultyTiers);
	return FMath::Min(UCataclysmItemValues::MaxAffixTier,
					  Tier + AffixTiersAboveDifficulty);
}

int32 UCataclysmDropRoll::RollAffixTier(const UDataTable* AffixTierTable,
										int32 DifficultyTier,
										FRandomStream& Stream)
{
	const int32 Cap = MaxAffixTierOnADrop(DifficultyTier);
	if (!AffixTierTable)
	{
		return 1;
	}

	// THE WEIGHT OF EVERY TIER AT OR BELOW THE CAP, read into a small array
	// first so the draw is a single pass over a running total rather than a
	// second walk of the table.
	TArray<float> Weights;
	Weights.Init(0.0f, Cap);
	float Total = 0.0f;
	AffixTierTable->ForeachRow<FCataclysmAffixTierRow>(
		TEXT("UCataclysmDropRoll::RollAffixTier"),
		[&](const FName&, const FCataclysmAffixTierRow& Row)
		{
			if (Row.Tier < 1 || Row.Tier > Cap)
			{
				return;
			}
			Weights[Row.Tier - 1] = Row.DropWeight;
			Total += Row.DropWeight;
		});

	if (Total <= 0.0f)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("No affix tier at or below T%d has a drop weight, so every "
				 "affix will roll at T1. Check game/Data/AffixTiers.csv."), Cap);
		return 1;
	}

	float Drawn = Stream.FRand() * Total;
	for (int32 Index = 0; Index < Cap; ++Index)
	{
		Drawn -= Weights[Index];
		if (Drawn < 0.0f)
		{
			return Index + 1;
		}
	}

	// Only reachable when floating point rounding leaves a sliver at the top.
	return Cap;
}

// ---------------------------------------------------------------------------
// What it is called
// ---------------------------------------------------------------------------

FString UCataclysmItemName::RarityWord(ECataclysmRarity Rarity)
{
	return UCataclysmDropRoll::RowNameFor(Rarity).ToString();
}

FString UCataclysmItemName::WordFor(const UDataTable* AffixTable, FName Affix)
{
	if (!AffixTable)
	{
		return FString();
	}
	const FCataclysmAffixRow* Row = AffixTable->FindRow<FCataclysmAffixRow>(
		Affix, TEXT("UCataclysmItemName::WordFor"), /*bWarnIfMissing=*/false);
	return Row ? Row->NameWord : FString();
}

bool UCataclysmItemName::StrongestSuffix(const FCataclysmItem& Item,
										 const UDataTable* AffixTable,
										 FCataclysmRolledAffix& OutAffix)
{
	bool bFound = false;
	for (const FCataclysmRolledAffix& Rolled : Item.Affixes)
	{
		// A PREFIX IS SKIPPED BY HAVING NO WORD rather than by reading its
		// Position column. The two say the same thing -- the generator refuses
		// a prefix that carries a word -- and asking for the word is asking the
		// question the name actually has.
		if (WordFor(AffixTable, Rolled.Affix).IsEmpty())
		{
			continue;
		}

		// STRICTLY GREATER, so the first of two equals wins and the same item is
		// called the same thing on every run.
		const bool bBetter = !bFound
			|| Rolled.Tier > OutAffix.Tier
			|| (Rolled.Tier == OutAffix.Tier && Rolled.Roll > OutAffix.Roll);
		if (bBetter)
		{
			OutAffix = Rolled;
			bFound = true;
		}
	}
	return bFound;
}

FString UCataclysmItemName::NameOf(const FCataclysmItem& Item,
								   const UDataTable* BaseTable,
								   const UDataTable* AffixTable)
{
	const FCataclysmItemBaseRow* Base = BaseTable
		? BaseTable->FindRow<FCataclysmItemBaseRow>(
			Item.Base, TEXT("UCataclysmItemName::NameOf"),
			/*bWarnIfMissing=*/false)
		: nullptr;
	if (!Base)
	{
		return FString();
	}

	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
	if (!UCataclysmItemModifiers::RarityOfItem(Item, Rarity))
	{
		// The contents are not any rarity, which means the item is malformed
		// rather than plain: RarityOf accepts eight combinations and rejects
		// every other. Naming it Everyday would hide that.
		return FString();
	}

	const FString Stem = FString::Printf(TEXT("%s %s"),
										 *RarityWord(Rarity), *Base->BaseName);

	FCataclysmRolledAffix Strongest;
	if (!StrongestSuffix(Item, AffixTable, Strongest))
	{
		return Stem;
	}
	return FString::Printf(TEXT("%s of %s"), *Stem,
						   *WordFor(AffixTable, Strongest.Affix));
}
