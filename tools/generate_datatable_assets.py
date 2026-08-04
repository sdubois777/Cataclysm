"""Import the generated CSV tables as DataTable assets in the project's content.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
      "$PWD/Cataclysm.uproject" -run=pythonscript \
      -script="../tools/generate_datatable_assets.py" -unattended -nopause -nosplash

WHY THIS EXISTS. tools/generate_datatables.py turns the design workbook into CSV
files under game/Data/. Those are text, so they review well and the generator can
compare them, but they are not content: nothing in the engine can reference one,
and game/Data/ is not cooked, so a packaged build does not contain them at all.
Anything reading a CSV off disk works in the editor and then finds nothing once
the game is packaged. That is issue #150.

This turns each of those CSV files into a DataTable asset under /Game/Data/,
which is what the rest of the engine can reference and what a packaged build
carries.

THE CSV FILES REMAIN THE REVIEWABLE FORM. They are still what the workbook
generates, still what `--check` compares, and still what a pull request shows a
diff of. The assets are built from them, so the CSV stays the thing a person
reads and the asset is the thing the game loads.

WHAT THIS SCRIPT CANNOT DO, the same limitation tools/generate_input_assets.py
has. There is no --check mode, because a .uasset carries generated identifiers
that differ between runs, so two runs over unchanged input do not produce
identical bytes. The guarantee comes instead from the automation test
Cataclysm.Data.EveryGeneratedTableHasAnAsset, which loads every asset and
compares its contents against the CSV it came from.
"""

import os

import unreal

# --- what to build ----------------------------------------------------------

DATA_DIR = "/Game/Data"

#: (asset name, CSV file, row struct). The struct names match the USTRUCTs in
#: game/Source/Cataclysm/Data/CataclysmDataRows.h with the leading F dropped,
#: which is how Unreal exposes them to Python.
#:
#: The two enchantment tables share a struct: the sheet holds positives and
#: negatives side by side in one block of columns, and they differ by which
#: columns they came from rather than by shape.
TABLES = [
    ("DT_Affixes", "Affixes.csv", "CataclysmAffixRow"),
    ("DT_Attributes", "Attributes.csv", "CataclysmAttributeEffectRow"),
    ("DT_CityUpgrades", "CityUpgrades.csv", "CataclysmCityUpgradeRow"),
    ("DT_ClassStats", "ClassStats.csv", "CataclysmClassStatRow"),
    ("DT_CraftingMaterials", "CraftingMaterials.csv", "CataclysmCraftingMaterialRow"),
    ("DT_DungeonModifiers", "DungeonModifiers.csv", "CataclysmDungeonModifierRow"),
    ("DT_EnchantmentsNegative", "EnchantmentsNegative.csv", "CataclysmEnchantmentRow"),
    ("DT_EnchantmentsPositive", "EnchantmentsPositive.csv", "CataclysmEnchantmentRow"),
    ("DT_EnemyModifiers", "EnemyModifiers.csv", "CataclysmEnemyModifierRow"),
    ("DT_Gems", "Gems.csv", "CataclysmGemRow"),
    ("DT_ItemBases", "ItemBases.csv", "CataclysmItemBaseRow"),
    ("DT_SkillSlots", "SkillSlots.csv", "CataclysmSkillSlotRow"),
    ("DT_StatusEffects", "StatusEffects.csv", "CataclysmStatusEffectRow"),
    ("DT_WeaponSkills", "WeaponSkills.csv", "CataclysmWeaponSkillRow"),
]

log = unreal.log
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def fail(message):
    """Stop with a message the caller will actually see in the log."""
    unreal.log_error(message)
    raise SystemExit(1)


def csv_dir():
    """The Data folder beside the .uproject, where the CSV files are written."""
    return os.path.join(unreal.Paths.project_dir(), "Data")


def row_struct(name):
    """The ScriptStruct for a row type, checked rather than assumed.

    A misspelled struct name would otherwise import every row as the wrong shape
    or as nothing at all, and the failure would look like missing content.
    """
    struct = getattr(unreal, name, None)
    if struct is None:
        fail("There is no struct named {} exposed to Python. It must be a "
             "USTRUCT in game/Source/Cataclysm/Data/CataclysmDataRows.h with "
             "the leading F dropped.".format(name))
    return struct.static_struct()


def import_table(asset_name, csv_file, struct_name):
    """Builds one DataTable asset from one CSV, replacing any existing one."""
    path = os.path.join(csv_dir(), csv_file)
    if not os.path.isfile(path):
        fail("{} does not exist. Run tools/generate_datatables.py first."
             .format(path))

    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = row_struct(struct_name)

    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DATA_DIR
    task.destination_name = asset_name
    task.factory = factory
    task.automated = True
    # Replaced rather than merged. The CSV is the whole truth about the table,
    # so a row that has left the CSV must leave the asset with it.
    task.replace_existing = True
    task.save = True

    asset_tools.import_asset_tasks([task])

    full = "{}/{}.{}".format(DATA_DIR, asset_name, asset_name)
    table = editor_assets.load_asset(full)
    if table is None:
        fail("Importing {} produced no asset at {}".format(csv_file, full))

    rows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))
    if rows == 0:
        fail("{} imported with no rows at all. The row struct {} probably does "
             "not match the CSV's columns.".format(csv_file, struct_name))

    return full, rows


def main():
    total = 0
    for asset_name, csv_file, struct_name in TABLES:
        full, rows = import_table(asset_name, csv_file, struct_name)
        log("imported {} from {} with {} rows".format(full, csv_file, rows))
        total += rows

    editor_assets.save_directory(DATA_DIR, recursive=True)
    log("wrote {} DataTable assets to {}, {} rows in total"
        .format(len(TABLES), DATA_DIR, total))
    log("done")


main()
