// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "CataclysmCityUpgradeMapping.generated.h"

class UDataTable;
struct FCataclysmCityUpgradeRow;

/**
 * Turns a row of `game/Data/CityUpgrades.csv` into something the empire layer
 * can act on.
 *
 * WHY A TRANSLATION EXISTS AT ALL. `CataclysmEmpire` must not depend on this
 * module -- its build file says the dependency runs one way -- so it cannot see
 * `FCataclysmCityUpgradeRow`, which lives beside every other DataTable row here.
 * It names the effects it can act on instead, in
 * `ECataclysmCityUpgradeEffect`, and this is what joins the two.
 *
 * THE JOIN IS BY ROW NAME AND IT IS DELIBERATELY BRITTLE. A row name is built
 * from the branch and the first characters of the effect sentence, so rewording
 * a sentence in the workbook renames the row and this stops recognising it.
 * That is the wanted behaviour: a reworded upgrade should fail loudly in
 * `Cataclysm.CityUpgrade.EveryRowOfTheTableMapsToAnEffect` rather than silently
 * become an upgrade that does nothing.
 *
 * ONE FRAGILITY WORTH KNOWING. Four Explorer rows begin with the same words --
 * "Increases the chance of dungeons on this city being ..." -- so three of them
 * are told apart only by the `_1` and `_2` the generator appends to make the
 * names unique, and the fourth by a spelling mistake in the sheet ("thie"). If
 * those four rows are ever reordered, the suffixes move and Elite and
 * Sacrificial swap places. The test checks each mapped row's sentence actually
 * names the sub-type it was mapped to, which is what catches that.
 */
UCLASS()
class CATACLYSM_API UCataclysmCityUpgradeMapping : public UObject
{
	GENERATED_BODY()

public:
	/** Where the generated table lives. */
	static const TCHAR* TableAssetPath;

	/**
	 * Which effect a row describes, or `None` if this does not recognise it.
	 *
	 * `None` MEANS THE MAPPING IS OUT OF DATE, not that the upgrade does
	 * nothing. An upgrade that is recognised and not yet built is a different
	 * thing, and `UCataclysmCityUpgradeRules::IsBuilt` is what says so.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static ECataclysmCityUpgradeEffect EffectFor(FName RowName);

	/**
	 * Builds the upgrade a city would buy, at tier 1.
	 *
	 * IT READS THE TIER 1 COLUMNS, which is what a newly bought upgrade is
	 * worth. They were only published on 2026-09-05; before that the tier 1
	 * magnitude existed solely inside the effect sentence and nothing could read
	 * it. Issue #1262.
	 *
	 * AT TIER 2 OR 3 IT READS THAT TIER'S COLUMNS, for a raise (issue #1265, ruled 2026-11-22).
	 *
	 * @param Tier 1 to 3; a tier outside that is read as the nearer end.
	 * @return an upgrade whose `Effect` is `None` when the row is not
	 *         recognised, which `UCataclysmEmpireRun::BuyCityUpgrade` refuses,
	 *         or when the row has no such tier (`HasTier`).
	 */
	static FCataclysmCityUpgrade Make(FName RowName,
									  const FCataclysmCityUpgradeRow& Row, int32 Tier = 1);

	/** Whether the row has that tier: tier 1 always, tiers 2 and 3 when its raw cell for them is written. Only the
	 *  cleanse has no tier 2 or 3. Issue #1265. */
	static bool HasTier(const FCataclysmCityUpgradeRow& Row, int32 Tier);

	/** `MakeFromTable` at a tier, for a raise: `None` when the table, the row or the tier is missing. Issue #1265. */
	static FCataclysmCityUpgrade MakeAtTier(FName RowName, int32 Tier);

	/**
	 * Loads the generated table, or logs which script should have produced it
	 * and returns null.
	 */
	static const UDataTable* LoadGeneratedTable();

	/**
	 * Builds the upgrade for one row of the generated table.
	 *
	 * @return an upgrade whose `Effect` is `None` when the table is missing or
	 *         holds no such row.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Empire")
	static FCataclysmCityUpgrade MakeFromTable(FName RowName);

	/** Every row name in the generated table, in table order. */
	static TArray<FName> AllRowNames();

	/**
	 * What the upgrade does, in the words the design workbook uses.
	 *
	 * READ FROM THE TABLE RATHER THAN WRITTEN HERE, so a screen shows the
	 * sentence a designer wrote and this file never becomes a second place where
	 * upgrades are described. Empty when the table is missing or holds no such
	 * row.
	 *
	 * IT IS ALSO TIER 1'S RAW CELL. The sheet's `Tier 1` column is this
	 * sentence; see `FCataclysmCityUpgradeRow::Effect`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString EffectTextFor(FName RowName);

	/** Which branch the upgrade belongs to: Architect, Explorer, Treasurer or
	 *  Artisan. Empty for the one whose branch is undecided. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString BranchFor(FName RowName);

	/**
	 * What the upgrade costs in gold at a difficulty tier. Issue #2742's second slice, ruled 2026-11-18 (G4).
	 *
	 * THE ROW'S `Tier1Gold` SCALED BY THE GOLD SHEET'S PILE COLUMN: times Gold Per Pile at `DifficultyTier` over Gold
	 * Per Pile at tier 1, so 10,000 at tier 1 is 30,000 at tier 2 and 300,000 at tier 8. ONE SCALE FOR BOTH, read from
	 * the pile column rather than a second column kept equal to it, so an upgrade costs the same number of walks at
	 * every tier. That is a judgement, and `docs/DECISIONS.md` records it.
	 *
	 * AT AN UPGRADE TIER, its own column: `Tier1Gold` for a purchase, `Tier2Gold` or `Tier3Gold` for a raise to that
	 * tier (issue #1265, ruled 2026-11-22), each scaled the same way.
	 *
	 * @return 0 for a row the table does not hold or one with no price at that tier (the cleanse above tier 1). The
	 *         row's unscaled price, with an error logged, when the Gold table will not load.
	 */
	static int64 PriceFor(FName RowName, int32 DifficultyTier, int32 UpgradeTier = 1);

	/**
	 * `PriceFor`'s arithmetic without the tables: `TierOneGold` times `GoldPerPileAtTier` over `GoldPerPileAtTierOne`,
	 * rounded to a whole gold. Nought for a price of nought or less; the price unscaled when the tier 1 pile is not
	 * above nought, which the generator refuses.
	 */
	static int64 PriceAt(int32 TierOneGold, float GoldPerPileAtTier, float GoldPerPileAtTierOne);
};
