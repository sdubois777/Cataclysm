// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmDungeonModifier.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDungeonModifierTable.generated.h"

class UDataTable;
struct FCataclysmDungeonModifierRow;

/**
 * Reads the dungeon modifier table and hands it to the empire layer.
 *
 * WHY THIS EXISTS AND WHY IT IS ON THIS SIDE OF THE MODULE LINE.
 * `game/Data/DungeonModifiers.csv` has held 117 rows since the design workbook
 * was imported and nothing in the game had ever read one. The thing that needs
 * them is `UCataclysmEmpireRun`, over in `CataclysmEmpire` -- but the table's row
 * type `FCataclysmDungeonModifierRow` is declared in this module, and
 * `CataclysmEmpire` must not depend on this one. So the pool is built here and
 * handed over. Issue #41.
 *
 * MOVING THE ROW STRUCT DOWN WAS THE OTHER OPTION AND WAS REJECTED, unlike
 * `ECataclysmDungeonType` in issue #1083. A `.uasset` DataTable stores the path
 * of its row struct inside itself, so moving the struct means regenerating
 * `game/Content/Data/DT_DungeonModifiers.uasset` -- which needs the editor, and
 * `game/README.md` records that the asset generator cannot run from a git
 * worktree at all. The enum that moved carried no asset with it.
 *
 * IT DRAWS NOTHING AND DECIDES NOTHING. Every rule about which modifiers a
 * dungeon carries lives in `UCataclysmDungeonModifierRules` in the empire layer,
 * where it can be tested against a pool built by hand. This reads rows.
 */
UCLASS()
class CATACLYSM_API UCataclysmDungeonModifierTable
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The Cataclysm Type meaning "no single Cataclysm owns this row".
	 *
	 * ONE ROW CARRIES IT: the Corrupted Stalker, added by issue #504. The project
	 * owner ruled on 2026-09-05 that it is "granted separately" and does not
	 * compete for one of a dungeon's modifier slots, so it maps to
	 * `ECataclysmType::None`, which is never in a run's active set and therefore
	 * never in a pool. Nothing grants it; that gap is issue
	 * [#1308](https://github.com/sdubois777/Cataclysm/issues/1308).
	 */
	static const TCHAR* GenericCataclysm;

	/** The dungeon modifier table, loaded once. Null when it cannot be read. */
	static const UDataTable* LoadDungeonModifierTable();

	/**
	 * Which Cataclysm a row's `CataclysmType` column names.
	 *
	 * THE SPELLING IS `UCataclysmRoster::NameFor`'s, which is the design
	 * document's rather than the display name -- so `Void` and not "The Void".
	 * The CSV is generated from the same design workbook, so the two agree by
	 * construction and a test checks that they still do.
	 *
	 * @return `None` for the `Generic` column and for anything unrecognised.
	 *         `bOutRecognised` separates those two, because they mean opposite
	 *         things: one is a design decision and the other is a broken row
	 */
	static ECataclysmType CataclysmFor(const FString& ColumnValue,
									   bool& bOutRecognised);

	/**
	 * Every row of the table, as the empire layer's plain struct.
	 *
	 * NOT FILTERED AND NOT SORTED. `UCataclysmDungeonModifierRules::PoolFor`
	 * narrows it to the Cataclysms a run faces and sorts what is left, which is
	 * where both rules belong. This hands over the whole table so that the run
	 * can be re-pooled without reloading anything.
	 *
	 * A ROW WITH AN UNRECOGNISED `CataclysmType` IS KEPT AND LOGGED, not dropped.
	 * Dropping it would make a broken row look like a row that does not exist,
	 * and the pool rule already excludes it -- it maps to `None`, which is never
	 * active. `Cataclysm.DungeonModifiers.EveryRowNamesACataclysmOrGeneric` is
	 * what fails when one appears.
	 */
	static TArray<FCataclysmDungeonModifier> PoolFromTable(
		const UDataTable* DungeonModifierTable);

	/** The whole table as the empire layer's struct, loading it first. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Dungeon")
	static TArray<FCataclysmDungeonModifier> LoadPool();

	/** One row by key, or null when the table or the key is missing. */
	static const FCataclysmDungeonModifierRow* FindRow(
		const UDataTable* DungeonModifierTable, FName Key);

	/**
	 * What the design calls the modifier this key names, or the key itself.
	 *
	 * FOR SHOWING A PLAYER. `FCataclysmDungeon::Modifiers` holds row keys, which
	 * are machine names like `Celestial_Edict_of_Silence`; this is what turns one
	 * back into `Edict of Silence`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Dungeon")
	static FString NameOf(FName RowKey);
};
