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
	 * AT A DIFFICULTY TIER, since issue #886, because the weights themselves
	 * move with it. Reads through UCataclysmDropRoll::DropWeightAt rather than
	 * the row's own DropWeight, which is the tier 1 figure.
	 *
	 * Returns 0 when nothing on the way up has a weight, which the caller has to
	 * treat as "there is nothing to choose between" rather than dividing by it.
	 */
	float WeightAtOrBelow(const UDataTable* GearRarityTable,
						  ECataclysmRarity Rarity, int32 DifficultyTier,
						  float MagicFind)
	{
		float Total = 0.0f;
		for (int32 Rung = 0; Rung <= LadderIndex(Rarity); ++Rung)
		{
			Total += UCataclysmDropRoll::DropWeightAt(
				GearRarityTable, RarityAt(Rung), DifficultyTier, MagicFind);
		}
		return Total;
	}

	/**
	 * The ordinary segment's weights added up at a difficulty tier.
	 *
	 * THE ARITHMETIC IS WRITTEN OUT HERE RATHER THAN GOING THROUGH DropWeightAt,
	 * and that is not duplication for its own sake: DropWeightAt asks this
	 * function for the enchanted rungs, so calling it back would recurse.
	 */
	float OrdinaryWeightTotal(const UDataTable* GearRarityTable,
							  int32 DifficultyTier)
	{
		const float Change = UCataclysmDropRoll::OrdinaryFallAt(DifficultyTier)
			/ UCataclysmDropRoll::OrdinaryFallAtTierOne;

		float Total = 0.0f;
		for (int32 Rung = 0; Rung < UCataclysmDropRoll::OrdinaryRarities; ++Rung)
		{
			if (const FCataclysmGearRarityRow* Row =
					UCataclysmDropRoll::RarityRow(GearRarityTable, RarityAt(Rung)))
			{
				const float Below = static_cast<float>(
					UCataclysmDropRoll::OrdinaryRarities - 1 - Rung);
				Total += Row->DropWeight * FMath::Pow(Change, Below);
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

const TCHAR* UCataclysmDropRoll::PositiveEnchantmentTableAssetPath =
	TEXT("/Game/Data/DT_EnchantmentsPositive.DT_EnchantmentsPositive");
const TCHAR* UCataclysmDropRoll::NegativeEnchantmentTableAssetPath =
	TEXT("/Game/Data/DT_EnchantmentsNegative.DT_EnchantmentsNegative");

const UDataTable* UCataclysmDropRoll::LoadPositiveEnchantmentTable()
{
	return LoadTableAt(PositiveEnchantmentTableAssetPath,
					   TEXT("EnchantmentsPositive.csv"), TEXT("Enchantments"));
}

const UDataTable* UCataclysmDropRoll::LoadNegativeEnchantmentTable()
{
	return LoadTableAt(NegativeEnchantmentTableAssetPath,
					   TEXT("EnchantmentsNegative.csv"), TEXT("Enchantments"));
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

ECataclysmRarity UCataclysmDropRoll::HighestUnpenalisedRarity(
	int32 DifficultyTier)
{
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, DifficultyTiers);

	// The tier is one-based and the ladder is zero-based, so tier 1 alone
	// reaches Everyday and Quality unpenalised: index (1 - 1) + 1.
	return RarityAt(Tier - 1 + RaritiesAboveDifficulty);
}

float UCataclysmDropRoll::PenaltyAboveTheTier(ECataclysmRarity Rarity,
											  int32 DifficultyTier)
{
	const int32 Above = LadderIndex(Rarity)
		- LadderIndex(HighestUnpenalisedRarity(DifficultyTier));
	if (Above <= 0)
	{
		return 1.0f;
	}

	return FMath::Pow(RarityPenaltyAboveTheTier,
					  static_cast<float>(Above));
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

float UCataclysmDropRoll::OrdinaryFallAt(int32 DifficultyTier)
{
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, DifficultyTiers);
	const float Span = static_cast<float>(DifficultyTiers - 1);

	return OrdinaryFallAtTierOne
		* FMath::Pow(OrdinaryFallAtDeepest / OrdinaryFallAtTierOne,
					 static_cast<float>(Tier - 1) / Span);
}

float UCataclysmDropRoll::EffectiveMagicFind(float MagicFind)
{
	const float Carried = FMath::Max(0.0f, MagicFind);
	if (Carried <= 0.0f)
	{
		return 0.0f;
	}

	return Carried * MagicFindCeiling / (Carried + MagicFindCeiling);
}

float UCataclysmDropRoll::MagicFindMultiplier(ECataclysmRarity Rarity,
											  float MagicFind)
{
	const float Effective = EffectiveMagicFind(MagicFind);
	if (Effective <= 0.0f)
	{
		return 1.0f;
	}

	const float Rungs = static_cast<float>(RarityCount - 1);
	const float Share =
		static_cast<float>(LadderIndex(Rarity)) / Rungs * MagicFindReach;
	return FMath::Pow(1.0f + Effective / 100.0f, Share);
}

float UCataclysmDropRoll::DropWeightAt(const UDataTable* GearRarityTable,
									   ECataclysmRarity Rarity,
									   int32 DifficultyTier,
									   float MagicFind)
{
	const FCataclysmGearRarityRow* Row = RarityRow(GearRarityTable, Rarity);
	if (!Row)
	{
		return 0.0f;
	}

	const int32 Rung = LadderIndex(Rarity);
	const float Change = OrdinaryFallAt(DifficultyTier) / OrdinaryFallAtTierOne;
	// MAGIC FIND MULTIPLIES AND THE ABOVE-THE-TIER PENALTY DIVIDES. Neither
	// touches OrdinaryWeightTotal below, which is the shaped weight only, so
	// the enchanted rungs cannot receive either of them twice.
	const float Lucky = MagicFindMultiplier(Rarity, MagicFind)
		/ PenaltyAboveTheTier(Rarity, DifficultyTier);

	if (Rung < OrdinaryRarities)
	{
		// MASTERFUL IS THE ANCHOR AND DOES NOT MOVE. It sits at rung
		// OrdinaryRarities - 1, so its exponent is zero and everything below it
		// is multiplied by the change once per rung of distance.
		const float Below = static_cast<float>(OrdinaryRarities - 1 - Rung);
		return Row->DropWeight * Lucky * FMath::Pow(Change, Below);
	}

	// THE ENCHANTED FOUR FOLLOW THE ORDINARY SEGMENT'S TOTAL, which is what
	// keeps their share of the ladder fixed at every difficulty tier. At tier 1
	// the two totals are equal and this is a multiplication by one, so the
	// table's own figures come straight through.
	//
	// THAT TOTAL IS TAKEN WITHOUT MAGIC FIND ON PURPOSE. It exists to undo the
	// per-tier flattening, which magic find has nothing to do with; folding
	// magic find in here as well would apply it to these rungs twice.
	const float AtTierOne = OrdinaryWeightTotal(GearRarityTable, 1);
	if (AtTierOne <= 0.0f)
	{
		return 0.0f;
	}

	return Row->DropWeight * Lucky
		* OrdinaryWeightTotal(GearRarityTable, DifficultyTier) / AtTierOne;
}

float UCataclysmDropRoll::RarityStepChance(const UDataTable* GearRarityTable,
										   ECataclysmRarity Rarity,
										   int32 DifficultyTier,
										   float MagicFind)
{
	const FCataclysmGearRarityRow* Row = RarityRow(GearRarityTable, Rarity);
	if (!Row)
	{
		return 0.0f;
	}

	const float AtOrBelow =
		WeightAtOrBelow(GearRarityTable, Rarity, DifficultyTier, MagicFind);
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

	// MAGIC FIND IS INSIDE THE WEIGHTS AND IS NOT APPLIED AGAIN HERE. Issue
	// #890, and the absence of a multiplier on this line is the whole of the
	// fix rather than an omission. Multiplying this chance is what let a rung
	// reach 1 and wipe out every rarity below it; a weight cannot, because a
	// rung's share of the weight at or below it is always under one.
	return FMath::Min(1.0f,
		DropWeightAt(GearRarityTable, Rarity, DifficultyTier, MagicFind)
			/ AtOrBelow);
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

int32 UCataclysmDropRoll::MaxUpgradeStoneOnADrop(int32 DifficultyTier)
{
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, DifficultyTiers);
	return FMath::Min(UCataclysmItemValues::MaxGearLevel,
					  Tier + UpgradeLevelsAboveDifficulty);
}

FName UCataclysmDropRoll::RollMaterial(const UDataTable* CraftingMaterialTable,
									   int32 MaterialTier, int32 DifficultyTier,
									   FRandomStream& Stream)
{
	if (!CraftingMaterialTable || MaterialTier <= 0)
	{
		return NAME_None;
	}

	const int32 BestStone = MaxUpgradeStoneOnADrop(DifficultyTier);

	// GATHERED RATHER THAN COUNTED THEN INDEXED, because a DataTable is a map
	// and walking it twice is not guaranteed to walk it in the same order.
	TArray<FName> InTier;
	int32 StonesTooGood = 0;
	CraftingMaterialTable->ForeachRow<FCataclysmCraftingMaterialRow>(
		TEXT("UCataclysmDropRoll::RollMaterial"),
		[&](const FName& Key, const FCataclysmCraftingMaterialRow& Row)
		{
			if (Row.Tier != MaterialTier)
			{
				return;
			}

			// AN UPGRADE STONE IS CAPPED BY THE DIFFICULTY TIER, issue #863, and
			// UpgradeLevel is 0 for everything that is not one, so this leaves
			// every other material alone.
			if (Row.UpgradeLevel > BestStone)
			{
				++StonesTooGood;
				return;
			}

			InTier.Add(Key);
		});

	if (InTier.Num() == 0)
	{
		// TWO DIFFERENT FAULTS, SAID DIFFERENTLY. An empty band is a data fault;
		// a band emptied by the cap would be a design fault, and no band can be,
		// because each keeps at least three materials that are not stones.
		if (StonesTooGood > 0)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Material tier %d holds nothing but upgrade stones above "
					 "+%d, which is the most difficulty tier %d may drop, so "
					 "the roll produced nothing. A tier needs a material that "
					 "is not an upgrade stone. Check "
					 "game/Data/CraftingMaterials.csv."),
				MaterialTier, BestStone, DifficultyTier);
			return NAME_None;
		}

		UE_LOG(LogCataclysm, Warning,
			TEXT("No crafting material is in tier %d, so a material that "
				 "rolled that tier cannot become anything. Check "
				 "game/Data/CraftingMaterials.csv."), MaterialTier);
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

int32 UCataclysmDropRoll::MaterialTierOf(
	const UDataTable* CraftingMaterialTable, FName Material)
{
	if (!CraftingMaterialTable || Material.IsNone())
	{
		return 0;
	}

	const FCataclysmCraftingMaterialRow* Row =
		CraftingMaterialTable->FindRow<FCataclysmCraftingMaterialRow>(
			Material, TEXT("MaterialTierOf"), /*bWarnIfMissing=*/false);
	return Row ? Row->Tier : 0;
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

	// THE WHOLE LADDER, since the cap became a penalty on 2026-08-23.
	float Left = 1.0f;
	for (int32 Rung = RarityCount - 1; Rung >= 1; --Rung)
	{
		const float Chance = RarityStepChance(GearRarityTable, RarityAt(Rung),
											  DifficultyTier, MagicFind);
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
	// THE WHOLE LADDER, since the cap became a penalty on 2026-08-23.
	for (int32 Rung = RarityCount - 1; Rung >= 1; --Rung)
	{
		if (Stream.FRand() < RarityStepChance(GearRarityTable, RarityAt(Rung),
											  DifficultyTier, MagicFind))
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
			const FCataclysmAffixRow* Part = UCataclysmItemModifiers::AffixNamed(AffixTable, PartName);
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

// ---------------------------------------------------------------------------
// What damage types a dropped weapon carries
// ---------------------------------------------------------------------------

TArray<FName> UCataclysmDropRoll::DamageTypesAvailableTo(
	const UDataTable* WeaponSkillTable, const FString& WeaponType)
{
	TArray<FName> Available;
	if (!WeaponSkillTable || WeaponType.IsEmpty())
	{
		return Available;
	}

	// ROWS NAMING UCataclysmWeaponSkills::WeaponIndependent ARE NOT COUNTED.
	// Those are the auras, whose WeaponType is "All" because they apply
	// whatever is held. Counting them would make every damage type available
	// to every weapon and erase the table this function exists to read.
	TSet<FString> Present;
	for (const TPair<FName, uint8*>& Each : WeaponSkillTable->GetRowMap())
	{
		const FCataclysmWeaponSkillRow* Row =
			reinterpret_cast<const FCataclysmWeaponSkillRow*>(Each.Value);
		if (Row && Row->WeaponType == WeaponType)
		{
			Present.Add(Row->DamageType);
		}
	}

	for (const FName& Candidate : UCataclysmItemModifiers::DamageTypeNames())
	{
		if (Present.Contains(Candidate.ToString()))
		{
			Available.Add(Candidate);
		}
	}
	return Available;
}

int32 UCataclysmDropRoll::MaxDamageTypesFor(const FCataclysmItemBaseRow& BaseRow,
											int32 DifficultyTier,
											int32 AvailableCount)
{
	return FMath::Max(0, FMath::Min3(BaseRow.MaxDamageTypes, DifficultyTier,
									 AvailableCount));
}

TArray<FName> UCataclysmDropRoll::RollDamageTypes(
	const UDataTable* WeaponSkillTable, const FCataclysmItemBaseRow& BaseRow,
	int32 DifficultyTier, FRandomStream& Stream)
{
	TArray<FName> Rolled;
	if (BaseRow.MaxDamageTypes <= 0)
	{
		// Everything that is not a weapon. game/Data/ItemBases.csv gives every
		// non-weapon base a MaxDamageTypes of 0, so this is the ordinary path
		// for ten of the eleven slots rather than a failure.
		return Rolled;
	}

	TArray<FName> Pool = DamageTypesAvailableTo(WeaponSkillTable,
												BaseRow.WeaponType);
	const int32 Most = MaxDamageTypesFor(BaseRow, DifficultyTier, Pool.Num());
	if (Most <= 0)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("A '%s' can hold at most %d damage types and %d are designed "
				 "for it, so a drop at tier %d carries none."),
			*BaseRow.WeaponType, BaseRow.MaxDamageTypes, Pool.Num(),
			DifficultyTier);
		return Rolled;
	}

	// FROM ONE, NOT FROM ZERO. The design says a weapon rolls "from one damage
	// type up to" its cap, so a weapon carrying none is not a drop that can
	// happen.
	const int32 Count = Stream.RandRange(1, Most);

	// WHICH TYPES IS UNIFORM, AND THE DESIGN SAYS IT SHOULD NOT BE. Section IV
	// says loot is biased toward the Cataclysm being fought. Nothing in this
	// module knows which Cataclysm that is and no other part of a drop is
	// biased either, so uniform is the honest stand-in rather than a bias
	// invented here.
	for (int32 Taken = 0; Taken < Count; ++Taken)
	{
		const int32 Index = Stream.RandRange(0, Pool.Num() - 1);
		Rolled.Add(Pool[Index]);
		Pool.RemoveAt(Index);
	}
	return Rolled;
}

float UCataclysmDropRoll::EnchantmentDrawWeight(float SheetWeight)
{
	// THE SHEET'S NUMBER MUST BE A WHOLE ONE FROM 1 TO 4. Anything else is a row
	// this draw cannot price, which today means one of the 55 set rows carrying
	// a set identifier where a weight belongs -- issue #1443. Those are already
	// excluded by EnchantmentSuitsSlot, so reaching here with one is a fault in
	// the data rather than an unlucky roll, and a zero takes the row out of the
	// draw instead of pricing it wrongly.
	const float Rounded = FMath::RoundToFloat(SheetWeight);
	if (!FMath::IsNearlyEqual(Rounded, SheetWeight)
		|| Rounded < LowestEnchantmentWeight
		|| Rounded > HighestEnchantmentWeight)
	{
		return 0.0f;
	}

	// INVERTED: weight 1 is the rarest, so it gets the smallest frequency.
	// Weight 1 -> step^0 = 1, weight 4 -> step^3 = 64. This is the frequency of
	// the whole BAND rather than of one row in it; RollEnchantments picks a band
	// with these and then draws inside it uniformly.
	return FMath::Pow(EnchantmentWeightStep, Rounded - LowestEnchantmentWeight);
}

bool UCataclysmDropRoll::EnchantmentSuitsSlot(const FCataclysmEnchantmentRow& Row,
											  const FString& Slot)
{
	// A SET ROW USED TO BE REFUSED HERE AND IS NOT ANY MORE. The refusal read
	// "paired and guaranteed" as meaning some other mechanism hands out a whole
	// set, and no such mechanism was ever written, so 55 authored rows could not
	// appear in the game. The project owner ruled on 2026-09-08 that a set IS an
	// enchantment. A set row still never reaches the ordinary pool -- its Weight
	// holds a set identifier of 5 to 18, which EnchantmentDrawWeight prices at
	// zero -- but it now honours a slot tag the same way every other row does,
	// so that a set written for weapons only would bind. None carries one today.

	// A SLOT TAG BINDS; EVERY OTHER TAG DESCRIBES WHAT THE ENCHANTMENT AFFECTS.
	// Three rows of 574 carry one. A row with none may appear anywhere, which is
	// the other 571.
	static const FString SlotTagPrefix = TEXT("Item.Slot.");

	TArray<FString> Tags;
	Row.Tags.ParseIntoArray(Tags, TEXT(","), true);

	bool bHasSlotTag = false;
	for (FString Each : Tags)
	{
		Each.TrimStartAndEndInline();
		if (!Each.StartsWith(SlotTagPrefix, ESearchCase::IgnoreCase))
		{
			continue;
		}
		bHasSlotTag = true;
		if (Each.RightChop(SlotTagPrefix.Len()).Equals(Slot,
													   ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return !bHasSlotTag;
}

void UCataclysmDropRoll::EnchantmentCandidatesFor(const UDataTable* Table,
												  const FString& Slot,
												  TArray<FName>& OutCandidates)
{
	OutCandidates.Reset();
	if (!Table)
	{
		return;
	}

	Table->ForeachRow<FCataclysmEnchantmentRow>(TEXT("EnchantmentCandidatesFor"),
		[&](const FName& Key, const FCataclysmEnchantmentRow& Row)
		{
			if (EnchantmentSuitsSlot(Row, Slot)
				&& EnchantmentDrawWeight(Row.Weight) > 0.0f)
			{
				OutCandidates.Add(Key);
			}
		});

	// SORTED SO THE ROLL IS REPRODUCIBLE FROM ITS SEED. Without this the same
	// seed could give a different enchantment between runs, because the order
	// the table hands its rows over is not part of the data. RollBase and
	// RollMaterialTier have said exactly this since they were written; this
	// function was missing it, so two of the three draws in this file were
	// reproducible and the third was not.
	OutCandidates.Sort(FNameLexicalLess());
}

int32 UCataclysmDropRoll::EnchantmentSetId(const FCataclysmEnchantmentRow& Row)
{
	if (!Row.EnchantmentType.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
	{
		return 0;
	}

	// A WHOLE NUMBER ABOVE THE HIGHEST WEIGHT. The Weight column does double
	// duty on set rows and holds the identifier -- issue #1443. Requiring it
	// above HighestEnchantmentWeight is what keeps the two meanings apart: a
	// set row carrying 3 is a fault in the data rather than a set numbered 3,
	// and is refused here so it cannot silently become one.
	const float Rounded = FMath::RoundToFloat(Row.Weight);
	if (!FMath::IsNearlyEqual(Rounded, Row.Weight)
		|| Rounded <= HighestEnchantmentWeight)
	{
		return 0;
	}
	return static_cast<int32>(Rounded);
}

namespace
{
	/**
	 * How many pieces a set row's own text says it needs, or MAX_int32.
	 *
	 * READ FROM THE EFFECT because that is where it is written: every one of
	 * the 42 set positive rows states its threshold as "(N-Piece Bonus)", and
	 * all 42 parse, measured 2026-09-08. It is only used to order a set's rows
	 * so the lowest threshold can be the one an item records, so a row that
	 * does not parse sorts last rather than breaking the draw.
	 */
	int32 SetPieceThreshold(const FString& Effect)
	{
		int32 Open = INDEX_NONE;
		if (!Effect.FindChar(TEXT('('), Open))
		{
			return MAX_int32;
		}

		// EVERY '(' IN THE ROW, not just the first, because a set's text can
		// carry more than one -- "(10s cd)" follows the threshold on several.
		for (int32 At = Open; At != INDEX_NONE;
			 At = Effect.Find(TEXT("("), ESearchCase::CaseSensitive,
							  ESearchDir::FromStart, At + 1))
		{
			int32 Digits = At + 1;
			while (Digits < Effect.Len() && FChar::IsDigit(Effect[Digits]))
			{
				++Digits;
			}
			if (Digits > At + 1
				&& Effect.Mid(Digits).StartsWith(TEXT("-Piece Bonus"),
												 ESearchCase::IgnoreCase))
			{
				return FCString::Atoi(*Effect.Mid(At + 1, Digits - At - 1));
			}
		}
		return MAX_int32;
	}
}

void UCataclysmDropRoll::EnchantmentSetsFor(
	const UDataTable* PositiveTable, const UDataTable* NegativeTable,
	const FString& Slot, TArray<FCataclysmEnchantmentSet>& OutSets)
{
	OutSets.Reset();
	if (!PositiveTable || !NegativeTable)
	{
		return;
	}

	// GATHERED BY IDENTIFIER FIRST AND JUDGED COMPLETE AFTERWARDS, so that a
	// set missing a half is reported for what it is rather than silently
	// half-offered.
	TMap<int32, TArray<TPair<int32, FName>>> PositivesBySet;
	TMap<int32, TArray<FName>> NegativesBySet;

	PositiveTable->ForeachRow<FCataclysmEnchantmentRow>(TEXT("EnchantmentSetsFor"),
		[&](const FName& Key, const FCataclysmEnchantmentRow& Row)
		{
			const int32 SetId = EnchantmentSetId(Row);
			if (SetId > 0 && EnchantmentSuitsSlot(Row, Slot))
			{
				PositivesBySet.FindOrAdd(SetId).Add(
					TPair<int32, FName>(SetPieceThreshold(Row.Effect), Key));
			}
		});

	NegativeTable->ForeachRow<FCataclysmEnchantmentRow>(TEXT("EnchantmentSetsFor"),
		[&](const FName& Key, const FCataclysmEnchantmentRow& Row)
		{
			const int32 SetId = EnchantmentSetId(Row);
			if (SetId > 0 && EnchantmentSuitsSlot(Row, Slot))
			{
				NegativesBySet.FindOrAdd(SetId).Add(Key);
			}
		});

	TArray<int32> SetIds;
	PositivesBySet.GetKeys(SetIds);
	SetIds.Sort();

	for (const int32 SetId : SetIds)
	{
		TArray<TPair<int32, FName>>& Rows = PositivesBySet[SetId];
		TArray<FName>* Negatives = NegativesBySet.Find(SetId);

		if (!Negatives || Negatives->Num() == 0)
		{
			// NAMED BY IDENTIFIER RATHER THAN BY SET NAME, because the name is
			// only inside the effect text. Set 15, Shard of Anarchy, is in this
			// state today and issue #1494 is the drawback being written; adding
			// that one row is all it takes for this branch to stop firing.
			//
			// ONCE PER SET PER RUN, NOT ONCE PER DROP. This function runs for
			// every item that rolls enchantments. Unguarded, it wrote 214,442
			// identical lines during one automation run and would write one per
			// drop in the game, which buries the log it is trying to inform.
			// A duplicate line if two threads ever reach this together is a
			// better outcome than a lock on a drop roll.
			static TSet<int32> AlreadyWarnedMissing;
			if (!AlreadyWarnedMissing.Contains(SetId))
			{
				AlreadyWarnedMissing.Add(SetId);
				UE_LOG(LogCataclysm, Warning,
					TEXT("Set %d has %d positive rows and no negative row, so "
						 "it cannot be paired and guaranteed and is left out of "
						 "the draw. Write one negative row carrying that set "
						 "identifier in game/Data/EnchantmentsNegative.csv. "
						 "This is said once per run."),
					SetId, Rows.Num());
			}
			continue;
		}
		if (Negatives->Num() > 1)
		{
			Negatives->Sort(FNameLexicalLess());

			// ONCE PER SET PER RUN, for the reason above. No set has a second
			// negative row today, so this has never fired; it is guarded so
			// that the day one does, the log says so rather than drowning.
			static TSet<int32> AlreadyWarnedExtra;
			if (!AlreadyWarnedExtra.Contains(SetId))
			{
				AlreadyWarnedExtra.Add(SetId);
				UE_LOG(LogCataclysm, Warning,
					TEXT("Set %d has %d negative rows and a set carries one, so "
						 "'%s' is used and the rest are ignored. This is said "
						 "once per run."),
					SetId, Negatives->Num(), *(*Negatives)[0].ToString());
			}
		}

		// BY THRESHOLD, THEN BY NAME. The threshold is what the player meets
		// them in; the name breaks a tie so two rows that failed to parse still
		// come out in the same order on every run.
		Rows.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B)
		{
			return A.Key != B.Key ? A.Key < B.Key
								  : FNameLexicalLess()(A.Value, B.Value);
		});

		FCataclysmEnchantmentSet Set;
		Set.SetId = SetId;
		Set.Negative = (*Negatives)[0];
		for (const TPair<int32, FName>& Each : Rows)
		{
			Set.Positives.Add(Each.Value);
		}
		Set.Representative = Set.Positives[0];
		OutSets.Add(MoveTemp(Set));
	}
}

void UCataclysmDropRoll::EnchantmentCandidatesByWeight(
	const UDataTable* Table, const FString& Slot,
	TArray<TArray<FName>>& OutByWeight)
{
	// ALWAYS FOUR BANDS, EVEN WHEN A BAND IS EMPTY. A caller that has to check
	// whether a band exists as well as whether it holds anything gets the check
	// wrong; an empty array answers both questions at once.
	OutByWeight.Reset();
	OutByWeight.SetNum(EnchantmentWeightCount);
	if (!Table)
	{
		return;
	}

	TArray<FName> Candidates;
	EnchantmentCandidatesFor(Table, Slot, Candidates);

	for (const FName& Candidate : Candidates)
	{
		const FCataclysmEnchantmentRow* Row =
			Table->FindRow<FCataclysmEnchantmentRow>(
				Candidate, TEXT("EnchantmentCandidatesByWeight"),
				/*bWarnIfMissing=*/false);
		if (!Row)
		{
			continue;
		}

		// EnchantmentCandidatesFor has already dropped anything that does not
		// price, so the weight here is a whole 1 to 4 and the index is in range.
		const int32 Band = FMath::RoundToInt(Row->Weight)
			- static_cast<int32>(LowestEnchantmentWeight);
		if (OutByWeight.IsValidIndex(Band))
		{
			OutByWeight[Band].Add(Candidate);
		}
	}
}

int32 UCataclysmDropRoll::DrawEnchantmentWeight(
	const TArray<bool>& bBandCanSupply, FRandomStream& Stream)
{
	// ONE PASS TO TOTAL, A SECOND TO WALK. Four entries, so the cost of the two
	// passes is nothing and the alternative is a table that has to be kept in
	// step with EnchantmentDrawWeight by hand.
	float Total = 0.0f;
	for (int32 Band = 0; Band < bBandCanSupply.Num(); ++Band)
	{
		if (bBandCanSupply[Band])
		{
			Total += EnchantmentDrawWeight(
				LowestEnchantmentWeight + static_cast<float>(Band));
		}
	}

	if (Total <= 0.0f)
	{
		return 0;
	}

	float Landed = Stream.FRand() * Total;
	for (int32 Band = 0; Band < bBandCanSupply.Num(); ++Band)
	{
		if (!bBandCanSupply[Band])
		{
			continue;
		}
		Landed -= EnchantmentDrawWeight(
			LowestEnchantmentWeight + static_cast<float>(Band));
		if (Landed <= 0.0f)
		{
			return Band + static_cast<int32>(LowestEnchantmentWeight);
		}
	}

	// FLOATING POINT LANDED PAST THE END. The running total and the sum are the
	// same additions in the same order, so this is rare rather than impossible;
	// the last band that can supply is the honest answer.
	for (int32 Band = bBandCanSupply.Num() - 1; Band >= 0; --Band)
	{
		if (bBandCanSupply[Band])
		{
			return Band + static_cast<int32>(LowestEnchantmentWeight);
		}
	}
	return 0;
}

FName UCataclysmDropRoll::DrawEnchantmentInBand(
	const TArray<FName>& BandCandidates, const TSet<FName>& Taken,
	FRandomStream& Stream)
{
	// UNIFORM, so the untaken rows are gathered and one index is drawn. Walking
	// a weighted total the way DrawEnchantmentWeight does would be the same
	// arithmetic with every term equal, which is a longer way to write this.
	TArray<FName> Available;
	Available.Reserve(BandCandidates.Num());
	for (const FName& Candidate : BandCandidates)
	{
		if (!Taken.Contains(Candidate))
		{
			Available.Add(Candidate);
		}
	}

	if (Available.Num() == 0)
	{
		return NAME_None;
	}
	return Available[Stream.RandRange(0, Available.Num() - 1)];
}

bool UCataclysmDropRoll::RollEnchantments(
	const UDataTable* PositiveTable, const UDataTable* NegativeTable,
	const FString& Slot, int32 Count, FRandomStream& Stream,
	TArray<FCataclysmRolledEnchantment>& OutRolled)
{
	OutRolled.Reset();
	if (Count <= 0)
	{
		return Count == 0;
	}
	if (!PositiveTable || !NegativeTable)
	{
		return false;
	}

	TArray<TArray<FName>> PositivesByWeight;
	TArray<TArray<FName>> NegativesByWeight;
	EnchantmentCandidatesByWeight(PositiveTable, Slot, PositivesByWeight);
	EnchantmentCandidatesByWeight(NegativeTable, Slot, NegativesByWeight);

	// A SET IS ONE MORE OPTION IN THE WEIGHT 1 BAND, NOT A FIFTH BAND.
	//
	// The project owner ruled on 2026-09-08 that sets "should all probably be in
	// the same bucket as t1 enchantments as they're pretty strong". Two readings
	// were open: put the sets INTO the weight 1 band, or give sets a fifth band
	// priced like weight 1. This is the first, for two reasons.
	//
	// IT KEEPS THE PUBLISHED LADDER EXACTLY TRUE. `docs/Cataclysm_GDD_v2.md`
	// states the four shares as 1.2%, 4.7%, 18.8% and 75.3%, and the owner ruled
	// separately that the ladder does not move. A fifth band renormalises all
	// four and makes every one of those four numbers slightly wrong; putting
	// sets inside band 1 leaves all four untouched.
	//
	// AND IT IS THE LITERAL READING of "in the same bucket as t1".
	//
	// WHAT IT COSTS: the weight 1 band is now shared, so an ordinary weight 1
	// enchantment is drawn about half as often as before. Measured for a chest
	// on 2026-09-08: 11 ordinary weight 1 rows against 13 offered sets, so 11 of
	// 24 rather than 11 of 11. All sets together take about 0.64% of draws and
	// one named set about 0.049%.
	//
	// THE COUNTS ARE PER SLOT AND ARE NOT 12 AND 14. Twelve weight 1 positives
	// are written, but one carries `Item.Slot.Weapon` and a chest never sees it.
	// Fourteen sets are written and thirteen are offered, because Shard of
	// Anarchy has no negative row yet -- issue #1494.
	//
	// TO REVERSE IT, give sets their own band: add a fifth entry to the band
	// arrays priced with EnchantmentDrawWeight(LowestEnchantmentWeight). The
	// draw below reads `SetBand` rather than assuming the lowest band, so the
	// change is local.
	static constexpr int32 SetBand = 0;

	TArray<FCataclysmEnchantmentSet> Sets;
	EnchantmentSetsFor(PositiveTable, NegativeTable, Slot, Sets);

	// NEITHER HALF REPEATS ON ONE PIECE, tracked separately because a positive
	// and a negative are drawn from different tables and cannot collide.
	TSet<FName> TakenPositives;
	TSet<FName> TakenNegatives;

	// ASKED WITHOUT DRAWING. DrawEnchantmentInBand would answer this too, but it
	// takes a number off the stream to do it, and a test that moves the stream
	// changes what the item rolls next.
	auto HasUntaken = [](const TArray<FName>& CandidateRows, const TSet<FName>& Taken)
	{
		for (const FName& Row : CandidateRows)
		{
			if (!Taken.Contains(Row))
			{
				return true;
			}
		}
		return false;
	};

	// THE SAME QUESTION FOR SETS, asked separately because a set does not need
	// a drawback left in the pool the way an ordinary benefit does. It carries
	// its own, which is what "paired and guaranteed" means.
	auto HasUntakenSet = [&Sets](const TSet<FName>& Taken)
	{
		for (const FCataclysmEnchantmentSet& Each : Sets)
		{
			if (!Taken.Contains(Each.Representative))
			{
				return true;
			}
		}
		return false;
	};

	for (int32 Filled = 0; Filled < Count; ++Filled)
	{
		// ASKED PAIR BY PAIR RATHER THAN ONCE, because the four pairs on a
		// Cataclysmic item draw from the same pools and empty them as they go.
		TArray<bool> bDrawbackBandHasRow;
		bDrawbackBandHasRow.Reserve(EnchantmentWeightCount);
		for (int32 Band = 0; Band < EnchantmentWeightCount; ++Band)
		{
			bDrawbackBandHasRow.Add(
				HasUntaken(NegativesByWeight[Band], TakenNegatives));
		}

		// A BENEFIT'S WEIGHT CAN BE DRAWN WHEN IT HAS AN UNTAKEN ROW AND SOME
		// DRAWBACK AT OR BELOW IT IS LEFT TO PAY FOR IT. "At or below" is the
		// floor: a weight 2 benefit may be bought with a weight 1 or a weight 2
		// drawback and nothing milder, so a weight 2 benefit with only weight 3
		// and 4 drawbacks left cannot be offered at all.
		TArray<bool> bBenefitBandCanSupply;
		bBenefitBandCanSupply.Reserve(EnchantmentWeightCount);
		bool bAnyDrawbackAtOrBelow = false;
		for (int32 Band = 0; Band < EnchantmentWeightCount; ++Band)
		{
			bAnyDrawbackAtOrBelow =
				bAnyDrawbackAtOrBelow || bDrawbackBandHasRow[Band];

			// A SET MAKES ITS BAND SUPPLIABLE ON ITS OWN. Every other benefit
			// needs a drawback still left at or below its weight to pay for it;
			// a set brings its own negative row, so it can be offered when the
			// weight 1 drawbacks are all taken and an ordinary weight 1 benefit
			// could not be.
			const bool bSetCanSupply =
				Band == SetBand && HasUntakenSet(TakenPositives);
			bBenefitBandCanSupply.Add(
				(HasUntaken(PositivesByWeight[Band], TakenPositives)
				 && bAnyDrawbackAtOrBelow)
				|| bSetCanSupply);

			if (!bBenefitBandCanSupply[Band])
			{
				// WORTH A LINE IN THE LOG EVEN THOUGH IT CANNOT HAPPEN TODAY.
				// Every band holds at least 22 rows a side for every slot and
				// an item draws at most four pairs, so reaching here means the
				// sheet changed, and the weights the remaining bands are then
				// drawn at are not the designed ones.
				UE_LOG(LogCataclysm, Warning,
					TEXT("A weight %d benefit cannot be paired on a '%s' (%d "
						 "benefit and %d drawback rows written at that weight, "
						 "%d and %d already on this piece), so this drop draws "
						 "the other weights more often than designed."),
					Band + static_cast<int32>(LowestEnchantmentWeight), *Slot,
					PositivesByWeight[Band].Num(),
					NegativesByWeight[Band].Num(),
					TakenPositives.Num(), TakenNegatives.Num());
			}
		}

		// THE BENEFIT'S WEIGHT FIRST. It is what the pair is worth, and it is
		// what the drawback's range is then measured against.
		const int32 BenefitWeight =
			DrawEnchantmentWeight(bBenefitBandCanSupply, Stream);
		if (BenefitWeight <= 0)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Asked for %d enchantments on a '%s' and no weight can "
					 "supply a benefit with a drawback at or below it, so only "
					 "%d could be drawn. That is a fault in the enchantment "
					 "pool rather than an unlucky roll."),
				Count, *Slot, Filled);
			OutRolled.Reset();
			return false;
		}

		const int32 BenefitBand =
			BenefitWeight - static_cast<int32>(LowestEnchantmentWeight);

		FCataclysmRolledEnchantment Rolled;

		// A SET AND AN ORDINARY ROW ARE ONE DRAW, NOT TWO. Inside the set band
		// the options are the ordinary rows that can still be paired plus every
		// set not already on this piece, and one index is taken across all of
		// them. So a set is exactly as likely as any one weight 1 enchantment,
		// which is what "the same bucket as t1" was asked for.
		const FCataclysmEnchantmentSet* DrawnSet = nullptr;
		if (BenefitBand == SetBand)
		{
			TArray<FName> Options;
			if (bDrawbackBandHasRow[SetBand])
			{
				for (const FName& Row : PositivesByWeight[SetBand])
				{
					if (!TakenPositives.Contains(Row))
					{
						Options.Add(Row);
					}
				}
			}
			const int32 OrdinaryOptions = Options.Num();

			TArray<const FCataclysmEnchantmentSet*> SetOptions;
			for (const FCataclysmEnchantmentSet& Each : Sets)
			{
				if (!TakenPositives.Contains(Each.Representative))
				{
					SetOptions.Add(&Each);
				}
			}

			const int32 Total = OrdinaryOptions + SetOptions.Num();
			if (Total > 0)
			{
				const int32 Picked = Stream.RandRange(0, Total - 1);
				if (Picked < OrdinaryOptions)
				{
					Rolled.Positive = Options[Picked];
				}
				else
				{
					DrawnSet = SetOptions[Picked - OrdinaryOptions];
					Rolled.Positive = DrawnSet->Representative;
				}
			}
		}
		else
		{
			Rolled.Positive = DrawEnchantmentInBand(
				PositivesByWeight[BenefitBand], TakenPositives, Stream);
		}

		if (DrawnSet)
		{
			// GUARANTEED, NOT DRAWN. A set carries its own drawback, so it
			// skips the draw below entirely rather than being priced against
			// the negative pool. This is the whole of "paired and guaranteed":
			// the two halves of a set arrive together and are never a random
			// pairing. The set's other threshold rows are not handed out here
			// -- they are 6-piece and 10-piece bonuses and belong to whatever
			// counts equipped pieces, which is not written yet.
			Rolled.Negative = DrawnSet->Negative;
		}
		else
		{
			// THE DRAWBACK FROM WEIGHT 1 UP TO THE BENEFIT'S WEIGHT, AND NO
			// FURTHER. This one line is the floor the project owner chose on
			// 2026-09-07: bands above the benefit's are never offered, so the
			// drawback matches it or is harsher and can never be milder.
			TArray<bool> bDrawbackBandCanSupply;
			bDrawbackBandCanSupply.Reserve(EnchantmentWeightCount);
			for (int32 Band = 0; Band < EnchantmentWeightCount; ++Band)
			{
				bDrawbackBandCanSupply.Add(Band <= BenefitBand
										   && bDrawbackBandHasRow[Band]);
			}

			const int32 DrawbackWeight =
				DrawEnchantmentWeight(bDrawbackBandCanSupply, Stream);
			if (DrawbackWeight > 0)
			{
				Rolled.Negative = DrawEnchantmentInBand(
					NegativesByWeight[DrawbackWeight
						- static_cast<int32>(LowestEnchantmentWeight)],
					TakenNegatives, Stream);
			}
		}

		// BOTH ARE SET, because a benefit weight was only offered once a
		// drawback at or below it was left. Checked anyway: a future change
		// that breaks that would otherwise write NAME_None onto an item and the
		// player would meet it as a blank line on a tool tip.
		if (Rolled.Positive.IsNone() || Rolled.Negative.IsNone())
		{
			UE_LOG(LogCataclysm, Error,
				TEXT("A weight %d benefit was chosen for a '%s' and then could "
					 "not be paired. The check that offered that weight and the "
					 "draw that followed it disagree about what the bands "
					 "hold."),
				BenefitWeight, *Slot);
			OutRolled.Reset();
			return false;
		}

		TakenPositives.Add(Rolled.Positive);
		TakenNegatives.Add(Rolled.Negative);
		OutRolled.Add(MoveTemp(Rolled));
	}

	return true;
}

bool UCataclysmDropRoll::RollItem(const UDataTable* BaseTable,
								  const UDataTable* AffixTable,
								  const UDataTable* GearRarityTable,
								  const UDataTable* SocketTable,
								  const UDataTable* AffixTierTable,
								  const UDataTable* WeaponSkillTable,
								  const UDataTable* PositiveEnchantmentTable,
								  const UDataTable* NegativeEnchantmentTable,
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

	// LAST, AFTER THE AFFIXES, AND THE ORDER IS DELIBERATE. Every draw moves
	// the stream on, so rolling this earlier would shift every affix draw that
	// follows it and change what an existing seed produces. Adding it at the
	// end leaves every seeded expectation in the tests describing the same
	// item it described before.
	OutItem.DamageTypes = RollDamageTypes(WeaponSkillTable, *BaseRow,
										  DifficultyTier, Stream);

	// AFTER THE DAMAGE TYPES, FOR THE SAME REASON THEY COME AFTER THE AFFIXES.
	// This was the newest draw when it was added, so it goes on the end.
	if (!RollEnchantments(PositiveEnchantmentTable, NegativeEnchantmentTable,
						  Slot, OutItem.EnchantmentCount, Stream,
						  OutItem.Enchantments))
	{
		return false;
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
