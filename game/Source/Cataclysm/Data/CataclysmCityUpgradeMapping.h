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
	 * NOTHING REACHES TIER 2 OR TIER 3, so those columns are not read here at
	 * all. Issue #1265.
	 *
	 * @return an upgrade whose `Effect` is `None` when the row is not
	 *         recognised, which `UCataclysmEmpireRun::BuyCityUpgrade` refuses.
	 */
	static FCataclysmCityUpgrade Make(FName RowName,
									  const FCataclysmCityUpgradeRow& Row);

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
};
