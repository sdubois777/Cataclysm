"""Import the generated CSV tables as DataTable assets in the project's content.

Runs inside the Unreal editor's Python interpreter, not the system Python. Start
it with the runner, which checks the preconditions and checks afterwards that it
actually ran:

    python tools/run_editor_python.py tools/generate_datatable_assets.py

Started by hand instead, neither is checked, and a git worktree gets an editor
that writes nothing and exits without saying why. That is issue #279.

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

IT ONLY REBUILDS WHAT MOVED, as of issue #444. A table whose CSV hashes to what
the record says it was built from, and whose asset is present, is left alone.
Rebuilding every asset whatever changed left a reviewer one modified binary file
per table, all but one of them with no content change and no way to tell which
one carried the change. Set the environment variable named by
`REBUILD_ALL_VARIABLE` in `tools/datatable_freshness.py` to rebuild every asset
regardless.

AND A RECORD OF WHAT EACH ASSET WAS BUILT FROM, written at the end of the run.
See RECORD_FILE. That automation test is correct and complete and needs the
engine to run, so nothing on a pull request runs it: continuous integration runs
the Python tests only. Two pull requests changed a CSV without running this
script, and both merged. Issue #226 is that gap; the record is the part of it a
Python test can check.

ONLY ASSETS THAT REACHED DISK GO INTO THAT RECORD, as of issue #587. The editor
can fail to write a `.uasset` and carry on: an open interactive editor holds the
file, Windows refuses the rename with error code 32, the failure is logged as a
warning, and the commandlet exits normally. This script used to record the new
hash anyway, which made the record lie, made the Python test pass over a stale
asset, and made the NEXT run skip the table as already current. Each import is
now checked against the bytes on disk, a table that did not get written keeps
whatever the record said about it before, and the run exits non-zero naming the
asset.
"""

import hashlib
import json
import os
import sys

import unreal

# THIS FILE'S OWN FOLDER, so `datatable_freshness` can be imported. The editor's
# Python interpreter runs this script by path and does not put its directory on
# the module search path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from datatable_freshness import (  # noqa: E402
    REBUILD_ALL_VARIABLE,
    entry_for_table,
    needs_rebuilding,
    rebuild_everything,
)

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
    ("DT_ElementVisuals", "ElementVisuals.csv", "CataclysmElementVisualRow"),
    ("DT_EnchantmentsNegative", "EnchantmentsNegative.csv", "CataclysmEnchantmentRow"),
    ("DT_EnchantmentsPositive", "EnchantmentsPositive.csv", "CataclysmEnchantmentRow"),
    ("DT_EnemyArchetypes", "EnemyArchetypes.csv", "CataclysmEnemyArchetypeRow"),
    ("DT_EnemyModifiers", "EnemyModifiers.csv", "CataclysmEnemyModifierRow"),
    ("DT_EnemyRarities", "EnemyRarities.csv", "CataclysmEnemyRarityRow"),
    ("DT_Gems", "Gems.csv", "CataclysmGemRow"),
    ("DT_ItemBases", "ItemBases.csv", "CataclysmItemBaseRow"),
    ("DT_SkillSlots", "SkillSlots.csv", "CataclysmSkillSlotRow"),
    ("DT_StatusEffects", "StatusEffects.csv", "CataclysmStatusEffectRow"),
    ("DT_WeaponSkills", "WeaponSkills.csv", "CataclysmWeaponSkillRow"),
]

#: Where the record of what each asset was built from is written, relative to
#: the Data folder beside the .uproject.
#:
#: WHAT IT IS FOR. A DataTable asset is a binary Unreal package, so a Python test
#: with no engine cannot read the rows out of it and compare them with the CSV.
#: What it can do is check whether this script has been run since the CSV last
#: changed, which is the failure that actually happens: someone edits the design
#: workbook, runs tools/generate_datatables.py to rewrite the CSV, and stops
#: there. The asset then holds the previous numbers, and a packaged build reads
#: the asset.
#:
#: So the record holds one SHA-256 per CSV, taken at the moment its asset was
#: built. tools/tests/test_datatable_assets_are_current.py recomputes them.
#:
#: IT DOES NOT PROVE THE ASSET IS CORRECT, only that it was rebuilt after the CSV
#: last changed. Proving the contents match is what the automation test
#: Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt does, and that needs
#: the engine.
#:
#: THE ASSET'S OWN BYTES ARE NOT RECORDED, deliberately. Two runs over unchanged
#: input produce different bytes, for the reason above, so an asset hash would
#: fail on every regeneration and teach people to ignore it.
RECORD_FILE = "datatable_asset_sources.json"

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


def asset_file(asset_name):
    """Where one table's `.uasset` sits on disk.

    `/Game/` is the project's `Content/` folder. This project has no plugin
    content, so that mapping is the whole of it.

    NEEDED BECAUSE THE ENGINE'S OWN REPORT OF A SAVE IS NOT THE LAST WORD. See
    `stat_of`.
    """
    content = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_content_dir())
    inside_content = DATA_DIR[len("/Game/"):]
    return os.path.join(content, inside_content, asset_name + ".uasset")


def stat_of(path):
    """A file's modification time and size, or None when it is not there.

    WHAT THIS IS FOR. Issue #587. Comparing this before and after an import is
    direct evidence about the bytes on disk, and it is independent of everything
    the engine reports. In the run that produced that issue the editor logged the
    save failure only as a warning, the commandlet exited normally, and the file
    was left untouched: still 17,240 bytes, still dated six days earlier. A pair
    that has not moved means the write did not happen, whatever anything else
    says.
    """
    try:
        info = os.stat(path)
    except OSError:
        return None
    return (info.st_mtime, info.st_size)


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
    """Build one DataTable asset from one CSV, replacing any existing one.

    Returns (asset path, row count, problem). `problem` is None when the asset
    reached disk, and otherwise says in words why it did not. It is not raised
    here: a save that failed for one table says nothing about the next one, and
    the run is more useful if it tries them all and then reports every table it
    could not write.
    """
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

    # BEFORE THE IMPORT, so that "the file did not change" can be told from "the
    # file was already like that". Issue #587.
    on_disk = asset_file(asset_name)
    was = stat_of(on_disk)

    asset_tools.import_asset_tasks([task])

    full = "{}/{}.{}".format(DATA_DIR, asset_name, asset_name)
    table = editor_assets.load_asset(full)
    if table is None:
        fail("Importing {} produced no asset at {}".format(csv_file, full))

    rows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))
    if rows == 0:
        fail("{} imported with no rows at all. The row struct {} probably does "
             "not match the CSV's columns.".format(csv_file, struct_name))

    return full, rows, save_problem(asset_name, on_disk, was)


def save_problem(asset_name, on_disk, was):
    """Why the asset did not reach disk, or None when it did. Issue #587.

    TWO INDEPENDENT CHECKS, because either one alone has been enough to be
    fooled.

    The first is the engine's own answer. `task.save = True` above has already
    tried once; asking the subsystem to save again costs nothing when that
    worked, because a package that is not dirty is not written a second time,
    and it retries and reports honestly when it did not.

    The second is the file itself. That is the check that would have caught the
    run in issue #587 whatever the engine had said, because in that run the
    failure reached the log only as a warning under `LogFileManager` and
    `LogSavePackage`, the commandlet exited normally, and the bytes on disk never
    moved.
    """
    if not editor_assets.save_asset("{}/{}".format(DATA_DIR, asset_name),
                                    only_if_is_dirty=True):
        return ("the editor reported that it could not save {}"
                .format(os.path.basename(on_disk)))

    now = stat_of(on_disk)
    if now is None:
        return "{} is not on disk after the import".format(on_disk)
    if now == was:
        return ("{} was not written: it is still {} bytes with the same "
                "modification time it had before the import"
                .format(os.path.basename(on_disk), now[1]))

    return None


def csv_digest(csv_file):
    """The SHA-256 of one CSV, with line endings normalised to LF first.

    NORMALISED, AND IT HAS TO BE. `.gitattributes` sets `* text=auto`, so these
    CSV files are stored in the repository with LF and checked out with CRLF on
    Windows, where this script runs. Continuous integration runs on Linux and
    checks them out with LF. Hashing the bytes as they sit on disk would record
    a Windows hash that no Linux runner could ever reproduce, and the test
    reading this record would fail on every pull request.

    Normalising loses nothing this record is for. A line ending change is not a
    content change, and the DataTable asset built from the file would be
    identical either way.
    """
    with open(os.path.join(csv_dir(), csv_file), "rb") as handle:
        return hashlib.sha256(
            handle.read().replace(b"\r\n", b"\n")).hexdigest()


def write_record(entries):
    """Record what each asset was built from. See RECORD_FILE.

    @param entries  one entry per table whose asset is on disk, already in the
        order of TABLES. A table whose asset could not be written is not in this
        list, so the record keeps whatever it said about that table before, or
        says nothing about it if it never had an entry. `entry_for_table` in
        `tools/datatable_freshness.py` makes that decision and explains it.

    WRITTEN EVEN WHEN A SAVE FAILED, which is a change from issue #587. Not
    writing it at all would leave the record still naming the OLD hashes for the
    tables this run did rebuild successfully, so the next run would rebuild those
    again for nothing. Writing it with the failed table's previous entry left
    alone records the truth about every table: the ones that were written, and
    the one that was not.
    """
    record = {
        "what_this_is": (
            "Written by tools/generate_datatable_assets.py. One entry per "
            "DataTable asset, holding the SHA-256 of the CSV it was built from "
            "at the time it was built. tools/tests/"
            "test_datatable_assets_are_current.py fails when a CSV has changed "
            "since. Regenerate with the command in game/README.md."),
        "tables": entries,
    }

    path = os.path.join(csv_dir(), RECORD_FILE)
    # Explicit newline so the file is identical on every platform. Written with
    # a trailing newline because every other text file here has one.
    with open(path, "w", newline="\n") as handle:
        json.dump(record, handle, indent=2, sort_keys=False)
        handle.write("\n")
    log("wrote {}".format(path))


def read_record():
    """What the last successful run recorded, as {asset: entry}, or empty.

    A missing, unreadable or malformed record is not an error: it means nothing
    is known about what the assets were built from, and everything is rebuilt.
    """
    path = os.path.join(csv_dir(), RECORD_FILE)
    if not os.path.isfile(path):
        return {}

    try:
        with open(path) as handle:
            record = json.load(handle)
        return {entry["asset"]: entry for entry in record.get("tables", [])}
    except (ValueError, KeyError, TypeError, OSError) as error:
        log("could not read {}, so every asset will be rebuilt: {}"
            .format(RECORD_FILE, error))
        return {}


def main():
    """Rebuild the assets whose CSV moved, and leave the rest alone.

    WHY NOT ALL OF THEM EVERY TIME, which is what this did until issue #444. A
    .uasset is binary and tracked with git LFS, so a pull request cannot show a
    diff of one. Rebuilding every table when one CSV changed left a reviewer one
    modified binary file per table, all but one of them with no content change
    and no way to tell which one mattered.
    """
    force = rebuild_everything()
    if force:
        log("{} is set, so every asset will be rebuilt."
            .format(REBUILD_ALL_VARIABLE))

    previous = read_record()

    total = 0
    built = 0
    skipped = 0
    entries = []
    problems = []

    for asset_name, csv_file, struct_name in TABLES:
        entry = previous.get(asset_name) or {}
        full = "{}/{}.{}".format(DATA_DIR, asset_name, asset_name)

        # THE ROW COUNT HAS TO COME FROM SOMEWHERE FOR A SKIPPED TABLE, because
        # the record holds one per asset and importing is what counts them. It is
        # passed on rather than folded into `force`, which is what made a
        # brand-new table report `asked to rebuild everything`. Issue #450.
        recorded_rows = entry.get("rows")
        digest = csv_digest(csv_file)

        rebuild, reason = needs_rebuilding(
            current_digest=digest,
            recorded_digest=entry.get("csv_sha256"),
            asset_exists=editor_assets.does_asset_exist(full),
            recorded_rows=recorded_rows,
            force=force)

        if not rebuild:
            log("skipped {} -- {} ({} rows)"
                .format(asset_name, reason, recorded_rows))
            entries.append(entry)
            total += recorded_rows
            skipped += 1
            continue

        full, rows, problem = import_table(asset_name, csv_file, struct_name)
        if problem is None:
            log("imported {} from {} with {} rows -- {}"
                .format(full, csv_file, rows, reason))
            total += rows
            built += 1
        else:
            unreal.log_error("FAILED TO SAVE {} -- {}".format(asset_name, problem))
            problems.append((asset_name, problem))

        recorded = entry_for_table(
            asset=asset_name, csv=csv_file, rows=rows, current_digest=digest,
            previous_entry=previous.get(asset_name), saved=problem is None)
        if recorded is not None:
            entries.append(recorded)

    editor_assets.save_directory(DATA_DIR, recursive=True)
    write_record(entries)

    # BOTH COUNTS, ALWAYS. "skipped 14" and "did nothing because it crashed"
    # look identical unless the run says which it was.
    log("rebuilt {} DataTable assets and left {} already current, {} rows in "
        "total across {}".format(built, skipped, total, DATA_DIR))

    if problems:
        fail(unwritten_assets_message(problems))

    log("done")


def unwritten_assets_message(problems):
    """What to say when the editor could not write an asset. Issue #587.

    IT NAMES THE CAUSE, because in this project there is one common cause and
    the reader has no way to guess it from a Windows error number. The
    interactive editor holds a `.uasset` open, usually because that data table is
    open in an editor window, and Windows then refuses to rename the file. It is
    the same class of problem as `Build.bat` refusing to run while the editor is
    open, which `CLAUDE.md` and `game/README.md` already record.
    """
    lines = ["{} DataTable asset(s) were NOT written:".format(len(problems))]
    lines += ["  {} -- {}".format(asset, why) for asset, why in problems]
    lines += [
        "",
        "The record in game/Data/{} was left saying what those assets were "
        "really built from, so they are still marked as needing a rebuild and "
        "tools/tests/test_datatable_assets_are_current.py will fail until they "
        "are.".format(RECORD_FILE),
        "",
        "CLOSE THE UNREAL EDITOR AND RUN THIS AGAIN. An open editor holds the "
        "asset file, and Windows refuses the rename with error code 32, 'the "
        "process cannot access the file because it is being used by another "
        "process'. Whether it happens depends on which assets the editor has "
        "loaded, so a run that worked yesterday is not evidence.",
    ]
    return "\n".join(lines)


main()
