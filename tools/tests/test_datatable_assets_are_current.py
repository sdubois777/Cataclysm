"""A generated CSV must not change without its DataTable asset being rebuilt.

WHY THIS EXISTS. Issue #226. Two pull requests changed a table under
`game/Data/` and did not run `tools/generate_datatable_assets.py`, which is what
turns those CSV files into the DataTable assets under `game/Content/Data/`. Both
merged. `game/Data/*.csv` is the reviewable input; `game/Content/Data/DT_*.uasset`
is what a packaged build actually loads, because `game/Data/` is not cooked. So a
stale asset means the game runs on the old numbers while every document, every
test and every review reads the new ones, and nothing about that is visible from
the source tree.

WHY NOTHING CAUGHT IT. The check exists and works:
`Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt`, in
`game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp`, loads every asset and
compares its rows against the CSV. It needs the Unreal editor. Continuous
integration runs `python -m pytest` and `python -m ruff check .` and nothing
else, so that test only ever runs when somebody runs the Unreal suite by hand.

WHAT THIS FILE CHECKS INSTEAD, and it is deliberately narrower. It does not read
a `.uasset`. A DataTable asset is a binary Unreal package whose row data is
stored in the engine's serialised form, and parsing that without the engine means
writing a parser for an undocumented format that changes between engine versions.

What it reads is `game/Data/datatable_asset_sources.json`, which
`tools/generate_datatable_assets.py` writes at the end of a successful run. That
file records the SHA-256 of each CSV at the moment its asset was built. If a CSV
has changed since, the asset was not rebuilt, and this fails.

    it proves          the generator was run after the CSV last changed
    it does not prove  the asset's contents are right

The second is what the automation test is for. This is the part a Python test can
do with no engine, and it catches the failure that actually happened.

THE ASSET'S OWN BYTES ARE NOT RECORDED, deliberately. A `.uasset` carries
generated identifiers that differ between runs, so two runs over unchanged input
do not produce identical bytes. An asset hash would fail on every regeneration
and teach people to ignore this file.

HOW TO FIX A FAILURE HERE. Run the DataTable asset generator. The command is in
`game/README.md`. It rewrites all fourteen assets even when one CSV changed, so
restore the ones whose CSV did not change before committing, or every run adds
Git LFS storage for nothing.
"""

from __future__ import annotations

import ast
import hashlib
import json
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "game" / "Data"
CONTENT_DIR = REPO_ROOT / "game" / "Content" / "Data"
GENERATOR = REPO_ROOT / "tools" / "generate_datatable_assets.py"
RECORD = DATA_DIR / "datatable_asset_sources.json"

#: The command that fixes every failure in this file. Repeated in each message
#: rather than left to the reader, because the whole point is that the person who
#: hits this did not know the step existed.
FIX = ("Run `python tools/run_editor_python.py "
       "tools/generate_datatable_assets.py`. It rewrites all fourteen assets, so "
       "restore the ones whose CSV did not change before committing. From a git "
       "worktree it refuses to start and says why; game/README.md gives the way "
       "round that.")


def generator_tables() -> list[tuple[str, str, str]]:
    """The TABLES list out of the generator, read without importing it.

    `tools/generate_datatable_assets.py` imports `unreal` at module level and
    runs inside the editor's Python interpreter, so the system Python running
    these tests cannot import it. The list is parsed out of the source instead,
    which is exact and needs no second copy here.
    """
    if not GENERATOR.is_file():
        pytest.skip("the DataTable asset generator is not present")
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        names = [t.id for t in node.targets if isinstance(t, ast.Name)]
        if "TABLES" in names:
            return [tuple(ast.literal_eval(element))
                    for element in node.value.elts]
    pytest.fail(f"{GENERATOR.name} no longer has a TABLES list")


@pytest.fixture(scope="module")
def tables() -> list[tuple[str, str, str]]:
    return generator_tables()


@pytest.fixture(scope="module")
def record() -> dict:
    if not RECORD.is_file():
        pytest.fail(
            f"{RECORD.name} does not exist in game/Data/. It is written by "
            f"tools/generate_datatable_assets.py. {FIX}")
    return json.loads(RECORD.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def recorded(record) -> dict[str, dict]:
    return {entry["csv"]: entry for entry in record["tables"]}


def digest(path: pathlib.Path) -> str:
    """The same hash the generator records: LF line endings, then SHA-256.

    NORMALISED, AND IT HAS TO BE. `.gitattributes` sets `* text=auto`, so these
    CSV files are stored with LF and checked out with CRLF on Windows, where the
    asset generator runs, and with LF on the Linux runner that runs these tests
    in continuous integration. Hashing the bytes as they sit on disk would make
    every recorded hash machine-specific and this file would fail on every pull
    request. `csv_digest` in the generator does the same thing for the same
    reason, and `test_the_two_hashes_are_computed_the_same_way` holds them
    together.
    """
    return hashlib.sha256(
        path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


class TestTheRecordCoversEveryTable:
    """A table missing from the record is one this file does not check."""

    def test_there_are_tables_to_check(self, tables):
        """A check over an empty list passes and proves nothing."""
        assert len(tables) >= 14

    def test_every_table_the_generator_builds_is_recorded(self, tables, recorded):
        missing = sorted(csv for _, csv, _ in tables if csv not in recorded)
        assert not missing, (
            f"{RECORD.name} has no entry for {missing}, which "
            f"{GENERATOR.name} builds an asset from. {FIX}")

    def test_the_record_names_nothing_the_generator_does_not_build(self, tables,
                                                                   recorded):
        built = {csv for _, csv, _ in tables}
        extra = sorted(set(recorded) - built)
        assert not extra, (
            f"{RECORD.name} has entries for {extra}, which {GENERATOR.name} no "
            "longer builds. Regenerate the record.")

    def test_every_recorded_asset_name_matches_the_generator(self, tables, recorded):
        for asset, csv, _ in tables:
            assert recorded[csv]["asset"] == asset, csv


class TestEveryCsvIsUnchangedSinceItsAssetWasBuilt:
    """The failure this file exists for."""

    def test_every_csv_still_hashes_to_what_was_recorded(self, tables, recorded):
        stale = []
        for _, csv, _ in tables:
            path = DATA_DIR / csv
            assert path.is_file(), f"{csv} does not exist in game/Data/"
            if digest(path) != recorded[csv]["csv_sha256"]:
                stale.append(csv)

        assert not stale, (
            f"{stale} changed since their DataTable assets were built, so the "
            f"assets under game/Content/Data/ hold the previous numbers. That "
            f"is what a packaged build loads. {FIX}")

    def test_every_asset_named_in_the_record_exists(self, tables):
        missing = [asset for asset, _, _ in tables
                   if not (CONTENT_DIR / f"{asset}.uasset").is_file()]
        assert not missing, (
            f"no asset file in game/Content/Data/ for {missing}. {FIX}")


class TestTheRecordIsSelfDescribing:
    """A generated file nobody can interpret gets deleted by the next person."""

    def test_it_says_what_it_is(self, record):
        assert "what_this_is" in record
        assert "generate_datatable_assets.py" in record["what_this_is"]

    def test_it_names_this_test_file(self, record):
        assert pathlib.Path(__file__).name in record["what_this_is"], (
            "the record does not say which test reads it, so a failure here "
            "gives no way back to this file")

    def test_every_entry_has_the_four_fields(self, record):
        for entry in record["tables"]:
            assert set(entry) == {"asset", "csv", "rows", "csv_sha256"}, entry

    def test_every_row_count_is_positive(self, record):
        """A table imported with no rows means the row struct did not match the
        CSV's columns. The generator already fails on that; this records it."""
        for entry in record["tables"]:
            assert entry["rows"] > 0, entry["asset"]


class TestTheGeneratorStillWritesIt:
    """Nothing else would notice the generator quietly losing this step."""

    def test_the_generator_writes_the_record_file(self):
        source = GENERATOR.read_text(encoding="utf-8")
        assert "def write_record(" in source, (
            f"{GENERATOR.name} no longer has a write_record function, so "
            f"{RECORD.name} will go stale without anything saying so")
        assert "write_record(" in source.split("def main(")[-1], (
            "write_record is defined but main() does not call it")

    def test_the_generator_names_the_same_file(self):
        source = GENERATOR.read_text(encoding="utf-8")
        assert f'RECORD_FILE = "{RECORD.name}"' in source, (
            f"{GENERATOR.name} writes its record somewhere other than "
            f"{RECORD.name}, which is what this file reads")

    def test_it_checks_the_bytes_on_disk_before_recording_an_asset(self):
        """Issue #587. The engine's own report of a save is not the last word.

        The editor can fail to write a `.uasset` and carry on: an open
        interactive editor holds the file, Windows refuses the rename with error
        code 32, the failure reaches the log only as a warning, and the
        commandlet exits normally. The generator recorded the new hash anyway,
        which made every check in this file pass over an asset six days old.

        The check that would have caught it regardless of what the engine said is
        the file's own modification time and size, compared either side of the
        import. This is a source check because the generator imports `unreal` and
        cannot be imported here.
        """
        source = GENERATOR.read_text(encoding="utf-8")
        for name in ("def stat_of(", "def save_problem("):
            assert name in source, (
                f"{GENERATOR.name} no longer has {name.split('(')[0][4:]}, so "
                f"nothing compares the asset file on disk before and after the "
                f"import and {RECORD.name} can record an asset the editor "
                f"failed to write. Issue #587.")

    def test_a_run_that_could_not_write_an_asset_stops_and_says_which(self):
        """Reporting success for work that did not happen is the fault here.

        The run that produced issue #587 printed "rebuilt 1 DataTable assets",
        exited 0, and left the asset untouched. `main` must call `fail`, which
        logs an error and raises `SystemExit(1)`, with the message that names the
        assets it could not write.
        """
        tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))

        main = next((node for node in ast.walk(tree)
                     if isinstance(node, ast.FunctionDef) and node.name == "main"),
                    None)
        assert main is not None, f"{GENERATOR.name} no longer has a main()"

        def calls_named(node) -> set[str]:
            return {inner.func.id for inner in ast.walk(node)
                    if isinstance(inner, ast.Call)
                    and isinstance(inner.func, ast.Name)}

        failing = [node for node in ast.walk(main)
                   if isinstance(node, ast.Call)
                   and isinstance(node.func, ast.Name) and node.func.id == "fail"
                   and any("unwritten_assets_message" in calls_named(argument)
                           for argument in node.args)]

        assert failing, (
            "main() in {} does not call fail(unwritten_assets_message(...)), so "
            "a run that could not write an asset reports success and exits 0. "
            "That is what happened in issue #587.".format(GENERATOR.name))

    def test_the_two_hashes_are_computed_the_same_way(self):
        """Both sides must normalise line endings before hashing.

        If one does and the other does not, every hash disagrees on Windows and
        agrees on Linux, so the failure appears only for whoever runs the asset
        generator. `.gitattributes` sets `* text=auto`, which is what makes the
        two platforms differ.
        """
        source = GENERATOR.read_text(encoding="utf-8")
        assert 'replace(b"\\r\\n", b"\\n")' in source, (
            f"{GENERATOR.name} no longer normalises line endings before "
            "hashing, so the hashes it records cannot be reproduced on Linux")
