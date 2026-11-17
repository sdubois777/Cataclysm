"""A migration is one version wide, needs nothing outside the record, and has a
committed example file for every version it can be asked to read.

WHY THIS EXISTS. Issue #529. `docs/Save_System_Design.md` section 5 sets four
rules that decide whether a player's progress survives a patch, and every one of
them fails quietly:

    rule 1   the schema version is the first field, so it can be read without
             parsing the rest
    rule 3   a step is always exactly one version wide, never a jump
    rule 4   a step reads only what is in the record and constants frozen into
             itself -- never `game/Data/`
    the test the fixtures ARE the test: one committed example save file per
             historical schema version

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmSaveMigrationTests.cpp`
never run on a pull request. Reading the source as text does.

WHAT BREAKING EACH RULE ACTUALLY COSTS, because none of them announces itself:

    A JUMP FROM VERSION 1 TO VERSION 4 skips versions 2 and 3, so a file at
    version 2 or 3 has no step at all and the load is refused. The player is
    told their save is broken, and it is not.

    A STEP THAT READS A DATA TABLE breaks the moment that table changes, which
    is the thing most likely to change. The failure is a corrupt save rather
    than an error, because the step succeeds while reading the wrong numbers.

    A MISSING EXAMPLE FILE means the version it covers is never loaded by any
    test again. The migration that reads it can be broken for a year and
    nothing says so until a player with an old file tries to start the game.

WHAT IS DELIBERATELY NOT SCANNED. Files whose names end in `Tests.cpp`. They
build chains with gaps, chains with steps that always fail, and chains declared
backwards, on purpose, to prove the machinery refuses them. Scanning those would
report the deliberate breakage as real breakage.
"""

from __future__ import annotations

import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
SAVE_DIR = SOURCE / "Save"
TESTS_DIR = SOURCE / "Tests"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE_DESIGN = REPO_ROOT / "docs" / "Save_System_Design.md"

#: `{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },`
STEP = re.compile(
    r"\{\s*(?P<version>\d+)\s*,\s*TEXT\(\"(?P<label>[^\"]+)\"\)\s*,\s*&(?P<function>\w+)\s*\}")

#: `Migrate_3_to_4`
STEP_NAME = re.compile(r"^Migrate_(\d+)_to_(\d+)$")

#: The same shape wherever it appears in the design document's prose.
STEP_NAME_IN_PROSE = re.compile(r"Migrate_(\d+)_to_(\d+)")

#: `static constexpr int32 SchemaVersionNow = 3;`
VERSION_NOW = re.compile(r"class\s+CATACLYSM_API\s+(?P<klass>U\w+)\s*:"
                         r".*?static\s+constexpr\s+int32\s+SchemaVersionNow\s*=\s*(?P<version>\d+)\s*;",
                         re.DOTALL)

#: `const FName UCataclysmAccountSave::TypeName = FName(TEXT("Account"));`
TYPE_NAME = re.compile(r"const\s+FName\s+(?P<klass>U\w+)::TypeName\s*=\s*FName\(TEXT\(\"(?P<name>\w+)\"\)\)")

#: `Example_v2.json`
FIXTURE_NAME = re.compile(r"^(?P<type>\w+)_v(?P<version>\d+)\.json$")

#: What a step must never reach for. Section 5, rule 4.
FORBIDDEN_IN_A_STEP = (
    "DataTable",
    "game/Data",
    ".csv",
    "FindRow",
    "LoadObject",
    "StaticLoadObject",
)


def without_comments(text: str) -> str:
    """The code with its comments removed.

    NEEDED FOR EVERY "MUST NOT CONTAIN" CHECK IN THIS PROJECT, because every
    rule here is written down beside the code in a comment naming the very
    thing being searched for. `FCataclysmSaveMigrationStep`'s own comment says
    a step "must not read `game/Data/`", which is a match for two of the
    forbidden strings below. This project has recorded that trap five times.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. The save "
            f"system is issue #529; if a file was renamed, rename it here too.")
    return path.read_text(encoding="utf-8")


def files_that_may_register_steps() -> list[pathlib.Path]:
    """Every save source file, minus the ones that break chains on purpose."""
    found = sorted(SAVE_DIR.glob("*.cpp")) + sorted(TESTS_DIR.glob("CataclysmSave*.cpp"))
    return [path for path in found if not path.name.endswith("Tests.cpp")]


def registered_steps() -> list[tuple[pathlib.Path, int, str, str]]:
    """Every step the game registers: its file, version, label and function."""
    steps: list[tuple[pathlib.Path, int, str, str]] = []
    for path in files_that_may_register_steps():
        code = without_comments(read(path))
        for match in STEP.finditer(code):
            steps.append((path, int(match["version"]), match["label"], match["function"]))
    return steps


def record_versions() -> dict[str, int]:
    """Every save record class and the schema version it is at today."""
    versions: dict[str, int] = {}
    for path in sorted(SAVE_DIR.glob("*.h")) + sorted(TESTS_DIR.glob("CataclysmSave*.h")):
        for match in VERSION_NOW.finditer(read(path)):
            versions[match["klass"]] = int(match["version"])
    return versions


def record_type_names() -> dict[str, str]:
    """Every save record class and the name its files are called after."""
    names: dict[str, str] = {}
    for path in sorted(SAVE_DIR.glob("*.cpp")) + sorted(TESTS_DIR.glob("CataclysmSave*.cpp")):
        for match in TYPE_NAME.finditer(read(path)):
            names[match["klass"]] = match["name"]
    return names


def fixture_files() -> list[pathlib.Path]:
    if not FIXTURES.is_dir():
        pytest.fail(
            f"{FIXTURES.relative_to(REPO_ROOT).as_posix()} does not exist. The "
            f"committed example save files are the save system's test, per "
            f"docs/Save_System_Design.md section 5.")
    return sorted(FIXTURES.glob("*.json"))


def test_the_design_document_asks_for_single_step_migrations() -> None:
    """The rule this file enforces is read from the design, not restated here."""
    design = read(SAVE_DESIGN)

    assert "Never write a migration that jumps versions" in design, (
        "docs/Save_System_Design.md section 5 no longer forbids a migration "
        "that jumps versions. If the design changed, this whole test file is "
        "enforcing a rule that no longer exists and should be deleted rather "
        "than made to pass.")

    examples = STEP_NAME_IN_PROSE.findall(design)
    assert examples, (
        "the design document no longer gives an example of a migration step's "
        "name. The naming convention the code follows is read from there.")

    for first, second in examples:
        assert int(second) == int(first) + 1, (
            f"the design document's own example Migrate_{first}_to_{second} "
            f"jumps {int(second) - int(first)} versions, and section 5 rule 3 "
            f"says a step is always one.")


def test_every_migration_step_is_exactly_one_version_wide() -> None:
    """Rule 3. A step named for a jump is refused."""
    steps = registered_steps()
    assert steps, (
        "no migration steps were found at all. Either every record is still at "
        "schema version 1 and the example record was removed, or the shape "
        "this file parses has changed. Both are worth looking at: with no steps "
        "found, every check below passes for free.")

    for path, version, label, function in steps:
        where = path.relative_to(REPO_ROOT).as_posix()

        named = STEP_NAME.match(label)
        assert named, (
            f"{where} registers a step called {label!r}. Section 5 rule 3 names "
            f"them Migrate_N_to_N+1, so that a reader can see at a glance which "
            f"version a step leads out of.")

        first, second = int(named[1]), int(named[2])
        assert second == first + 1, (
            f"{where}: {label} jumps {second - first} versions. Section 5 rule "
            f"3: 'Never write a migration that jumps versions; a chain of small "
            f"steps is testable and a jump is not.'")

        assert first == version, (
            f"{where}: the step called {label} is registered against version "
            f"{version}. The name says it leads out of version {first}. One of "
            f"the two is wrong, and the chain believes the number rather than "
            f"the name, so this would run the wrong step.")

        assert function == label, (
            f"{where}: the step called {label} runs the function {function}. A "
            f"label that does not match the function it names makes a failure "
            f"message point at the wrong step.")


def test_no_migration_step_reads_the_games_data() -> None:
    """Rule 4. A step uses only the record and constants frozen into itself."""
    for path in files_that_may_register_steps():
        code = without_comments(read(path))
        if not STEP.search(code):
            continue

        where = path.relative_to(REPO_ROOT).as_posix()
        for forbidden in FORBIDDEN_IN_A_STEP:
            assert forbidden not in code, (
                f"{where} registers migration steps and mentions {forbidden!r}. "
                f"Section 5 rule 4: a migration 'transforms one schema into the "
                f"next using only what is in the record and constants frozen "
                f"into the migration itself. A migration that reads game/Data/ "
                f"breaks the moment that data changes, which is the thing most "
                f"likely to change.' The break is silent: the step succeeds "
                f"while reading numbers that no longer mean what they did.")


def test_every_example_save_file_says_its_version_first() -> None:
    """Rule 1, checked on the files rather than on the code that writes them."""
    files = fixture_files()
    assert files, "there are no committed example save files at all"

    for path in files:
        text = path.read_text(encoding="utf-8")
        record = json.loads(text)

        assert isinstance(record, dict), f"{path.name} is not a JSON object"

        first = next(iter(record))
        assert first == "SchemaVersion", (
            f"{path.name} starts with {first!r}. Section 5 rule 1 puts the "
            f"schema version first 'so it can be read without parsing the "
            f"rest, which is what lets a migration run before the record is "
            f"interpreted'.")

        version = record["SchemaVersion"]
        assert isinstance(version, int) and not isinstance(version, bool), (
            f"{path.name} has a schema version of {version!r}, which is not a "
            f"whole number.")
        assert version >= 1, (
            f"{path.name} says it is version {version}. The first real version "
            f"is 1; anything below it means nothing wrote the field.")


def test_every_example_save_file_is_named_for_the_version_it_holds() -> None:
    """`Example_v2.json` holds a version 2 record, or the name is a lie."""
    for path in fixture_files():
        named = FIXTURE_NAME.match(path.name)
        assert named, (
            f"{path.name} is not named <RecordType>_v<Version>.json. The name "
            f"is how a reader knows which version a file covers without opening "
            f"it, and how the check below finds a missing one.")

        record = json.loads(path.read_text(encoding="utf-8"))
        assert record["SchemaVersion"] == int(named["version"]), (
            f"{path.name} is named for version {named['version']} and says it "
            f"is version {record['SchemaVersion']}.")


def test_every_schema_version_has_a_committed_example_file() -> None:
    """The fixtures ARE the test, so a version with no file is not tested.

    THIS IS THE CHECK THAT FIRES ON THE DAY SOMEBODY BUMPS A VERSION. Adding
    `SchemaVersionNow = 2` without committing the version 1 example file leaves
    the migration that reads a version 1 file covered by nothing.
    """
    versions = record_versions()
    names = record_type_names()

    assert versions, (
        "no save record class declares a SchemaVersionNow. Either the records "
        "were renamed or the shape this file parses has changed.")

    present = set()
    for path in fixture_files():
        named = FIXTURE_NAME.match(path.name)
        if named:
            present.add((named["type"], int(named["version"])))

    for klass, version in sorted(versions.items()):
        assert klass in names, (
            f"{klass} declares a schema version but no TypeName, so nothing "
            f"says what its save files are called.")

        record_type = names[klass]
        for expected in range(1, version + 1):
            assert (record_type, expected) in present, (
                f"{record_type} is at schema version {version} and there is no "
                f"{record_type}_v{expected}.json in "
                f"{FIXTURES.relative_to(REPO_ROOT).as_posix()}. Section 5: "
                f"'Commit example save files, one per historical schema "
                f"version, as test fixtures.' Without one, nothing ever loads a "
                f"version {expected} record again.")


def test_every_record_type_has_a_distinct_name() -> None:
    """Two records sharing a name would share a set of example files."""
    names = record_type_names()
    assert len(set(names.values())) == len(names), (
        f"two save record classes answer to the same name: {sorted(names.items())}. "
        f"The name decides which example files belong to which record.")
