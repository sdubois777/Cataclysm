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
	 * One rung's chance in a weighted cascade, given that the cascade reached it.
	 *
	 * THE RUNG'S WEIGHT AS A SHARE OF EVERYTHING AT OR BELOW IT, multiplied by
	 * magic find and capped at certainty. Shared by the gear rarity roll and the
	 * crafting material tier roll, because they are the same cascade over
	 * different ladders; `_cascade_step_chance` in `sim/cataclysm_sim/loot.py`
	 * is the same function on the other side.
	 *
	 * @param Index  ONE-BASED, so it lines up with a difficulty tier.
	 */
	float CascadeStepChance(const TArray<float>& Weights, int32 Index,
							float MagicFind)
	{
		if (Index < 1 || Index > Weights.Num())
		{
			return 0.0f;
		}

		float AtOrBelow = 0.0f;
		for (int32 Rung = 0; Rung < Index; ++Rung)
		{
			AtOrBelow += Weights[Rung];
		}
		if (AtOrBelow <= 0.0f)
		{
			return 0.0f;
		}

		return FMath::Min(1.0f, Weights[Index - 1] / AtOrBelow
			* (1.0f + FMath::Max(0.0f, MagicFind) / 100.0f));
	}

	/** The material tier weights, weakest first, or empty. */
	TArray<float> MaterialTierWeights(const UDataTable* MaterialTierTable)
	{
		TArray<float> Weights;
		if (!MaterialTierTable)
		{
			return Weights;
		}

		MaterialTierTable->ForeachRow<FCataclysmMaterialTierRow>(
			TEXT("MaterialTierWeights"),
			[&](const FName&, const FCataclysmMaterialTierRow& Row)
			{
				if (Row.Tier < 1)
				{
					return;
				}
				if (Row.Tier > Weights.Num())
				{
					Weights.SetNumZeroed(Row.Tier);
				}
				Weights[Row.Tier - 1] = Row.DropWeight;
			});
		return Weights;
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

const UDataTable* UCataclysmDropRoll::LoadAffixTable()
{
	return LoadTableAt(TEXT("/Game/Data/DT_Affixes.DT_Affixes"),
					   TEXT("Affixes.csv"), TEXT("Affixes"));
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

// ---------------------------------------------------------------------------
// What a kill drops
// ---------------------------------------------------------------------------

const TCHAR* UCataclysmDropRoll::EnemyDropTableAssetPath =
	TEXT("/Game/Data/DT_EnemyDrops.DT_EnemyDrops");
const TCHAR* UCataclysmDropRoll::MaterialTierTableAssetPath =
	TEXT("/Game/Data/DT_MaterialTiers.DT_MaterialTiers");
const TCHAR* UCataclysmDropRoll::CraftingMaterialTableAssetPath =
	TEXT("/Game/Data/DT_CraftingMaterials.DT_CraftingMaterials");

const UDataTable* UCataclysmDropRoll::LoadEnemyDropTable()
{
	return LoadTableAt(EnemyDropTableAssetPath, TEXT("EnemyDrops.csv"),
					   TEXT("Enemy Drops"));
}

const UDataTable* UCataclysmDropRoll::LoadMaterialTierTable()
{
	return LoadTableAt(MaterialTierTableAssetPath, TEXT("MaterialTiers.csv"),
					   TEXT("Material Tiers"));
}

const FCataclysmEnemyDropRow* UCataclysmDropRoll::EnemyDropRow(
	const UDataTable* EnemyDropTable, FName EnemyRarity)
{
	if (!EnemyDropTable)
	{
		return nullptr;
	}
	return EnemyDropTable->FindRow<FCataclysmEnemyDropRow>(
		EnemyRarity, TEXT("UCataclysmDropRoll::EnemyDropRow"),
		/*bWarnIfMissing=*/false);
}

namespace
{
	/** Loot quantity applied to a rate. Shared by the two Expected* functions
	 *  so neither can forget the baseline of 100. */
	float ScaledByLootQuantity(float Rate, float LootQuantity)
	{
		return Rate * FMath::Max(0.0f, LootQuantity)
			/ UCataclysmDropRoll::BaselineLootQuantity;
	}
}

float UCataclysmDropRoll::ExpectedGearDrops(const UDataTable* EnemyDropTable,
											FName EnemyRarity,
											float LootQuantity)
{
	const FCataclysmEnemyDropRow* Row = EnemyDropRow(EnemyDropTable, EnemyRarity);
	return Row ? ScaledByLootQuantity(Row->GearDrops, LootQuantity) : 0.0f;
}

float UCataclysmDropRoll::ExpectedMaterialDrops(const UDataTable* EnemyDropTable,
												FName EnemyRarity,
												float LootQuantity)
{
	const FCataclysmEnemyDropRow* Row = EnemyDropRow(EnemyDropTable, EnemyRarity);
	return Row ? ScaledByLootQuantity(Row->MaterialDrops, LootQuantity) : 0.0f;
}

float UCataclysmDropRoll::MagicFindFrom(const UDataTable* EnemyDropTable,
										FName EnemyRarity)
{
	const FCataclysmEnemyDropRow* Row = EnemyDropRow(EnemyDropTable, EnemyRarity);
	return Row ? Row->MagicFind : 0.0f;
}

int32 UCataclysmDropRoll::RollDropCount(float Expected, FRandomStream& Stream)
{
	if (Expected <= 0.0f)
	{
		return 0;
	}

	// KNUTH'S POISSON METHOD. See the header for why the count is drawn from a
	// distribution rather than being the whole part plus a fractional chance.
	const double Limit = FMath::Exp(-static_cast<double>(Expected));

	int32 Count = 0;
	double Product = 1.0;
	while (true)
	{
		++Count;
		Product *= static_cast<double>(Stream.FRand());
		if (Product <= Limit)
		{
			return Count - 1;
		}
	}
}

void UCataclysmDropRoll::MaterialTierDistribution(
	const UDataTable* MaterialTierTable, float MagicFind,
	TArray<float>& OutShares)
{
	const TArray<float> Weights = MaterialTierWeights(MaterialTierTable);
	OutShares.Init(0.0f, Weights.Num());
	if (Weights.Num() == 0)
	{
		return;
	}

	float Left = 1.0f;
	for (int32 Rung = Weights.Num(); Rung >= 2; --Rung)
	{
		const float Chance = CascadeStepChance(Weights, Rung, MagicFind);
		OutShares[Rung - 1] = Left * Chance;
		Left *= 1.0f - Chance;
	}

	// WHATEVER FELL THROUGH EVERY RUNG IS THE COMMONEST TIER, which is what
	// makes this a cascade rather than a table of weights that has to sum to
	// one by hand.
	OutShares[0] = Left;
}

const UDataTable* UCataclysmDropRoll::LoadCraftingMaterialTable()
{
	return LoadTableAt(CraftingMaterialTableAssetPath,
					   TEXT("CraftingMaterials.csv"), TEXT("Crafting"));
}

FName UCataclysmDropRoll::RollMaterial(const UDataTable* CraftingMaterialTable,
									   int32 Tier, FRandomStream& Stream)
{
	if (!CraftingMaterialTable || Tier <= 0)
	{
		return NAME_None;
	}

	// GATHERED RATHER THAN COUNTED THEN INDEXED, because a DataTable is a map
	// and walking it twice is not guaranteed to walk it in the same order.
	TArray<FName> InTier;
	CraftingMaterialTable->ForeachRow<FCataclysmCraftingMaterialRow>(
		TEXT("UCataclysmDropRoll::RollMaterial"),
		[&](const FName& Key, const FCataclysmCraftingMaterialRow& Row)
		{
			if (Row.Tier == Tier)
			{
				InTier.Add(Key);
			}
		});

	if (InTier.Num() == 0)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("No crafting material is in tier %d, so a material that "
				 "rolled that tier cannot become anything. Check "
				 "game/Data/CraftingMaterials.csv."), Tier);
		return NAME_None;
	}

	// SORTED SO THE ROLL IS REPRODUCIBLE FROM ITS SEED. Without this the same
	// seed could give a different material between runs, because the order the
	// table hands its rows over is not part of the data.
	InTier.Sort(FNameLexicalLess());

	return InTier[Stream.RandRange(0, InTier.Num() - 1)];
}

FString UCataclysmDropRoll::MaterialNameOf(
	const UDataTable* CraftingMaterialTable, FName Material)
{
	if (!CraftingMaterialTable || Material.IsNone())
	{
		return FString();
	}

	const FCataclysmCraftingMaterialRow* Row =
		CraftingMaterialTable->FindRow<FCataclysmCraftingMaterialRow>(
			Material, TEXT("MaterialNameOf"), /*bWarnIfMissing=*/false);
	return Row ? Row->MaterialName : FString();
}

FLinearColor UCataclysmDropRoll::MaterialColourFor(
	const UDataTable* MaterialTierTable, int32 Tier)
{
	if (!MaterialTierTable)
	{
		return FLinearColor::White;
	}

	FLinearColor Found = FLinearColor::White;
	MaterialTierTable->ForeachRow<FCataclysmMaterialTierRow>(
		TEXT("UCataclysmDropRoll::MaterialColourFor"),
		[&](const FName& Key, const FCataclysmMaterialTierRow& Row)
		{
			if (Row.Tier == Tier)
			{
				Found = Row.Colour;
			}
		});
	return Found;
}

int32 UCataclysmDropRoll::RollMaterialTier(const UDataTable* MaterialTierTable,
										   float MagicFind,
										   FRandomStream& Stream)
{
	const TArray<float> Weights = MaterialTierWeights(MaterialTierTable);
	if (Weights.Num() == 0)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("No crafting material tier has a drop weight, so no material "
				 "can roll a tier. Check game/Data/MaterialTiers.csv."));
		return 0;
	}

	for (int32 Rung = Weights.Num(); Rung >= 2; --Rung)
	{
		if (Stream.FRand() < CascadeStepChance(Weights, Rung, MagicFind))
		{
			return Rung;
		}
	}
	return 1;
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
// Rolling a whole item
// ---------------------------------------------------------------------------

namespace
{
	/** The Affixes table row whose AffixName is this, or null.
	 *
	 * BY NAME RATHER THAN BY ROW KEY, because a hybrid names its two parts as
	 * the affix names they are ("Flat magic find") and the row key is decorated
	 * with the kind ("Stat_Flat_magic_find").
	 */
	const FCataclysmAffixRow* AffixNamed(const UDataTable* AffixTable,
										 const FString& AffixName)
	{
		if (!AffixTable || AffixName.IsEmpty())
		{
			return nullptr;
		}

		const FCataclysmAffixRow* Found = nullptr;
		AffixTable->ForeachRow<FCataclysmAffixRow>(TEXT("AffixNamed"),
			[&](const FName&, const FCataclysmAffixRow& Row)
			{
				if (!Found && Row.AffixName.Equals(AffixName,
												   ESearchCase::IgnoreCase))
				{
					Found = &Row;
				}
			});
		return Found;
	}

	/** The group one stat affix occupies: "<stat>.<flat or increased>". */
	FString StatGroup(const FString& Stat, const FString& ValueKind)
	{
		return FString::Printf(TEXT("%s.%s"), *Stat, *ValueKind);
	}

	/** Whether this affix row is a resistance family. */
	bool IsResistanceFamily(const FCataclysmAffixRow& Row)
	{
		return Row.AffixKind.Equals(TEXT("Resistance"), ESearchCase::IgnoreCase);
	}
}

void UCataclysmDropRoll::GroupsOf(const UDataTable* AffixTable,
								  const FCataclysmAffixRow& Affix,
								  const TArray<FName>& DamageTypes,
								  TSet<FString>& OutGroups)
{
	OutGroups.Reset();

	if (IsResistanceFamily(Affix))
	{
		// AN EMPTY LIST MEANS ALL EIGHT, which is the convention
		// FCataclysmRolledAffix already uses: a family covering every damage
		// type has no choice to make, so nothing is stored.
		const TArray<FName>& Covered = DamageTypes.Num() > 0
			? DamageTypes : UCataclysmItemModifiers::DamageTypeNames();
		for (const FName& Type : Covered)
		{
			OutGroups.Add(StatGroup(
				UCataclysmItemModifiers::ResistanceStatFor(Type).ToString(),
				TEXT("flat")));
		}
		return;
	}

	if (Affix.AffixKind.Equals(TEXT("Ailment"), ESearchCase::IgnoreCase))
	{
		// AN AILMENT AFFIX GRANTS NO STAT, so it cannot use a stat group. What
		// it grants is a chance at one named effect, and two rolls of the same
		// chance on one piece is the duplicate the rule exists to stop.
		OutGroups.Add(FString::Printf(TEXT("ailment.%s"), *Affix.Ailment));
		return;
	}

	if (Affix.AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
	{
		for (const FString& PartName : { Affix.HybridPart1, Affix.HybridPart2 })
		{
			const FCataclysmAffixRow* Part = AffixNamed(AffixTable, PartName);
			if (Part)
			{
				OutGroups.Add(StatGroup(Part->Stat, Part->ValueKind));
			}
			else if (!PartName.IsEmpty())
			{
				UE_LOG(LogCataclysm, Warning,
					TEXT("The hybrid affix '%s' names a part '%s' that is not "
						 "in the Affixes table, so the group rule cannot see "
						 "it and two affixes granting that stat could land on "
						 "one item."), *Affix.AffixName, *PartName);
			}
		}
		return;
	}

	OutGroups.Add(StatGroup(Affix.Stat, Affix.ValueKind));
}

void UCataclysmDropRoll::CandidatesFor(
	const UDataTable* AffixTable, const FString& Slot, const FString& Position,
	FRandomStream& Stream, TArray<FCataclysmAffixCandidate>& OutCandidates)
{
	OutCandidates.Reset();
	if (!AffixTable)
	{
		return;
	}

	const TArray<FName>& AllTypes = UCataclysmItemModifiers::DamageTypeNames();

	AffixTable->ForeachRow<FCataclysmAffixRow>(TEXT("CandidatesFor"),
		[&](const FName& Key, const FCataclysmAffixRow& Row)
		{
			if (!Row.Position.Equals(Position, ESearchCase::IgnoreCase))
			{
				return;
			}

			// THE SLOT LIST IS COMMA SEPARATED and every entry is checked at
			// generation time against the slots the item bases occupy, so a
			// misspelling here would already have failed the build.
			TArray<FString> Allowed;
			Row.AllowedSlots.ParseIntoArray(Allowed, TEXT(","), true);
			bool bAllowed = false;
			for (FString Each : Allowed)
			{
				if (Each.TrimStartAndEnd().Equals(Slot, ESearchCase::IgnoreCase))
				{
					bAllowed = true;
					break;
				}
			}
			if (!bAllowed)
			{
				return;
			}

			FCataclysmAffixCandidate Candidate;
			Candidate.Affix = Key;

			// A RESISTANCE FAMILY DRAWS ITS DAMAGE TYPES NOW, before the affix
			// draw, because the draw has to know which groups it would occupy.
			// A family covering all eight stores none, by the convention above.
			if (IsResistanceFamily(Row) && Row.Breadth > 0
				&& Row.Breadth < AllTypes.Num())
			{
				TArray<FName> Pool = AllTypes;
				for (int32 Taken = 0; Taken < Row.Breadth; ++Taken)
				{
					const int32 Index = Stream.RandRange(Taken, Pool.Num() - 1);
					Pool.Swap(Taken, Index);
					Candidate.DamageTypes.Add(Pool[Taken]);
				}
			}

			OutCandidates.Add(MoveTemp(Candidate));
		});
}

bool UCataclysmDropRoll::DrawWithoutRepeatingAGroup(
	const UDataTable* AffixTable,
	const TArray<FCataclysmAffixCandidate>& Candidates, int32 Count,
	FRandomStream& Stream, TArray<FCataclysmAffixCandidate>& OutDrawn)
{
	OutDrawn.Reset();
	if (Count <= 0)
	{
		return Count == 0;
	}
	if (!AffixTable)
	{
		return false;
	}

	// SHUFFLED AND THEN TAKEN IN ORDER, which is draw-without-replacement with
	// a whole group treated as drawn once any of its members is.
	TArray<FCataclysmAffixCandidate> Order = Candidates;
	for (int32 Index = Order.Num() - 1; Index > 0; --Index)
	{
		Order.Swap(Index, Stream.RandRange(0, Index));
	}

	TSet<FString> Taken;
	TSet<FString> Groups;
	for (const FCataclysmAffixCandidate& Candidate : Order)
	{
		if (OutDrawn.Num() == Count)
		{
			break;
		}

		const FCataclysmAffixRow* Row = AffixTable->FindRow<FCataclysmAffixRow>(
			Candidate.Affix, TEXT("DrawWithoutRepeatingAGroup"),
			/*bWarnIfMissing=*/false);
		if (!Row)
		{
			continue;
		}

		GroupsOf(AffixTable, *Row, Candidate.DamageTypes, Groups);
		if (Groups.Intersect(Taken).Num() > 0)
		{
			continue;
		}
		Taken.Append(Groups);
		OutDrawn.Add(Candidate);
	}

	if (OutDrawn.Num() < Count)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Asked for %d affixes and the %d candidates supply only %d "
				 "distinct stat groups. That is a fault in the affix pool "
				 "rather than an unlucky roll."),
			Count, Order.Num(), OutDrawn.Num());
		return false;
	}
	return true;
}

void UCataclysmDropRoll::SplitForADrop(int32 Slots, FRandomStream& Stream,
									   int32& OutPrefixes, int32& OutSuffixes)
{
	UCataclysmItemValues::PrefixSuffixSplit(Slots, OutPrefixes, OutSuffixes);
	if (OutPrefixes != OutSuffixes && Stream.FRand() < 0.5f)
	{
		Swap(OutPrefixes, OutSuffixes);
	}
}

FString UCataclysmDropRoll::RollSlot(const UDataTable* BaseTable,
									 FRandomStream& Stream)
{
	if (!BaseTable)
	{
		return FString();
	}

	// READ OFF THE ITEM BASES rather than held as a list here, so adding a slot
	// to the design needs no change in this file.
	TArray<FString> Slots;
	BaseTable->ForeachRow<FCataclysmItemBaseRow>(TEXT("RollSlot"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (!Row.Slot.IsEmpty())
			{
				Slots.AddUnique(Row.Slot);
			}
		});
	if (Slots.Num() == 0)
	{
		return FString();
	}

	// SORTED SO THE DRAW IS REPRODUCIBLE. A DataTable is a map and its row
	// order is not guaranteed, so an unsorted list would make one seed give
	// different slots on different runs.
	Slots.Sort();
	return Slots[Stream.RandRange(0, Slots.Num() - 1)];
}

FName UCataclysmDropRoll::RollBase(const UDataTable* BaseTable,
								   const FString& Slot, FRandomStream& Stream)
{
	if (!BaseTable || Slot.IsEmpty())
	{
		return NAME_None;
	}

	// EVERY BASE IN THE SLOT IS EQUALLY LIKELY, decided by the project owner on
	// 2026-08-18. The bases in a slot are alternatives rather than a ladder --
	// one grants armour, another evasion, another energy shield -- so none of
	// them is the good one to hold out for.
	TArray<FName> Bases;
	BaseTable->ForeachRow<FCataclysmItemBaseRow>(TEXT("RollBase"),
		[&](const FName& Key, const FCataclysmItemBaseRow& Row)
		{
			if (Row.Slot.Equals(Slot, ESearchCase::IgnoreCase))
			{
				Bases.Add(Key);
			}
		});
	if (Bases.Num() == 0)
	{
		return NAME_None;
	}

	Bases.Sort(FNameLexicalLess());
	return Bases[Stream.RandRange(0, Bases.Num() - 1)];
}

bool UCataclysmDropRoll::RollItem(const UDataTable* BaseTable,
								  const UDataTable* AffixTable,
								  const UDataTable* GearRarityTable,
								  const UDataTable* SocketTable,
								  const UDataTable* AffixTierTable,
								  const FString& Slot, int32 DifficultyTier,
								  float MagicFind, FRandomStream& Stream,
								  FCataclysmItem& OutItem)
{
	OutItem = FCataclysmItem();

	const FName Base = RollBase(BaseTable, Slot, Stream);
	if (Base.IsNone())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("No item base is in slot '%s', so nothing can drop for it."),
			*Slot);
		return false;
	}
	const FCataclysmItemBaseRow* BaseRow =
		BaseTable->FindRow<FCataclysmItemBaseRow>(
			Base, TEXT("RollItem"), /*bWarnIfMissing=*/false);
	if (!BaseRow)
	{
		return false;
	}

	// THE RARITY IS ROLLED FIRST AND THE CONTENTS FOLLOW FROM IT.
	const ECataclysmRarity Rarity =
		RollRarity(GearRarityTable, DifficultyTier, MagicFind, Stream);

	OutItem.Base = Base;
	OutItem.GearLevel = GearLevelGateFor(GearRarityTable, Rarity);
	OutItem.EnchantmentCount = UCataclysmItemValues::EnchantmentsFor(Rarity);
	OutItem.Sockets = RollSockets(SocketTable, *BaseRow, Stream);
	OutItem.Residue = RollResidue(GearRarityTable, Rarity, Stream);

	int32 Prefixes = 0;
	int32 Suffixes = 0;
	SplitForADrop(UCataclysmItemValues::AffixSlotsFor(Rarity), Stream,
				  Prefixes, Suffixes);

	const TPair<const TCHAR*, int32> Positions[] = {
		{ TEXT("prefix"), Prefixes }, { TEXT("suffix"), Suffixes } };

	for (const TPair<const TCHAR*, int32>& Each : Positions)
	{
		if (Each.Value == 0)
		{
			continue;
		}

		TArray<FCataclysmAffixCandidate> Candidates;
		CandidatesFor(AffixTable, Slot, Each.Key, Stream, Candidates);

		TArray<FCataclysmAffixCandidate> Drawn;
		if (!DrawWithoutRepeatingAGroup(AffixTable, Candidates, Each.Value,
										Stream, Drawn))
		{
			return false;
		}

		for (const FCataclysmAffixCandidate& Candidate : Drawn)
		{
			FCataclysmRolledAffix Rolled;
			Rolled.Affix = Candidate.Affix;
			Rolled.Tier = RollAffixTier(AffixTierTable, DifficultyTier, Stream);
			Rolled.Roll = Stream.FRand();
			Rolled.DamageTypes = Candidate.DamageTypes;
			OutItem.Affixes.Add(MoveTemp(Rolled));
		}
	}

	return true;
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
