"""Which DataTable assets get rebuilt, and which are left alone.

WHY THIS EXISTS. Issue #444. `tools/generate_datatable_assets.py` rebuilt all
fourteen assets whenever it ran, whatever had changed. Renaming one enemy
modifier for issue #358 left fourteen modified `.uasset` files, thirteen of them
with no content change at all. Those are binary and tracked with git LFS, so a
pull request cannot show a diff of one, and a reviewer had no way to tell which
of the fourteen carried the change.

WHY THE DECISION IS IN ITS OWN MODULE. The generator imports `unreal` at module
level and runs inside the editor's Python interpreter, so no test here can
import it -- `test_datatable_assets_are_current.py` parses its source with `ast`
for the same reason. `tools/datatable_freshness.py` holds the part that is plain
arithmetic on hashes, so these tests call it rather than reading it.

WHAT IS NOT CHECKED HERE. That the generator actually calls it, and that a real
run leaves the other thirteen assets untouched. Both need the editor. The pull
request for issue #444 carries the measured before and after.
"""

from __future__ import annotations

import ast
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_datatable_assets.py"

sys.path.insert(0, str(REPO_ROOT / "tools"))

from datatable_freshness import (  # noqa: E402
    REBUILD_ALL_VARIABLE,
    entry_for_table,
    needs_rebuilding,
    rebuild_everything,
)

#: Any two different hashes. The values do not matter, only that they differ.
SAME = "a" * 64
DIFFERENT = "b" * 64

#: A row count the record does hold. Any positive number will do; only whether
#: there is one at all changes what `needs_rebuilding` answers.
ROWS = 8


def test_an_unchanged_table_with_its_asset_present_is_left_alone() -> None:
    """The whole point of issue #444.

    THE FAILURE THIS EXISTS FOR is somebody restoring the old behaviour, by
    deleting the check or by making the comparison always true. Thirteen
    unrelated binary files then reappear in every data pull request.
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=True,
        recorded_rows=ROWS)

    assert not rebuild, (
        f"a table whose CSV has not changed is being rebuilt anyway ({reason}), "
        f"which puts an unrelated binary file into every pull request that "
        f"touches any other table. Issue #444.")
    assert reason, "a decision with no reason cannot be reported"


def test_a_changed_table_is_rebuilt() -> None:
    rebuild, reason = needs_rebuilding(
        current_digest=DIFFERENT, recorded_digest=SAME, asset_exists=True,
        recorded_rows=ROWS)

    assert rebuild, (
        "a table whose CSV changed is not being rebuilt, so the asset keeps the "
        "previous numbers and a packaged build reads the asset.")
    assert "changed" in reason


def test_a_missing_asset_is_rebuilt_even_when_the_hash_matches() -> None:
    """The order of the two checks is the whole of this test.

    A record can name a hash for an asset that was deleted, or never committed
    in the first place. Comparing hashes first and stopping there would leave
    nothing on disk while reporting success -- which is the shape of fault issue
    #436 was about: work reported as done that was never done.
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=False,
        recorded_rows=ROWS)

    assert rebuild, (
        "a table whose asset is missing is not being rebuilt, because its "
        "recorded hash still matches. The run would report success and leave "
        "no asset.")
    assert "does not exist" in reason


def test_a_table_the_record_says_nothing_about_is_rebuilt() -> None:
    """A new table, or a record written before that table existed."""
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=None, asset_exists=True,
        recorded_rows=None)

    assert rebuild
    assert "nothing recorded" in reason


def test_forcing_rebuilds_a_table_that_is_already_current() -> None:
    """For an asset hand-edited in the editor, whose CSV never moved.

    Nothing in the record can detect that, so there has to be a way to say
    "build them all anyway".
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=True,
        recorded_rows=ROWS, force=True)

    assert rebuild
    assert "everything" in reason


#: The reason a run gives when the operator set the environment variable. Issue
#: #450 is that this was also the reason given when they had not.
ASKED_FOR = "asked to rebuild everything"


def test_a_table_that_has_never_been_built_does_not_blame_the_operator() -> None:
    """Issue #450. A brand-new table was reported as `asked to rebuild everything`.

    WHY THE WORDS MATTER MORE THAN THE ANSWER HERE. The boolean was already
    right: a table with no asset and no record does need building. The reason
    string is the only account of why a binary file changed, because a `.uasset`
    is stored in git LFS and cannot be reviewed by reading it. Naming an
    operator action that did not happen sends a reviewer looking for a
    `CATACLYSM_REBUILD_ALL_DATATABLES` nobody set.
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=None, asset_exists=False,
        recorded_rows=None, force=False)

    assert rebuild
    assert reason != ASKED_FOR, (
        "a table that has never been built is reported as though somebody asked "
        "for every asset to be rebuilt. Nobody did: force is False here. "
        "Issue #450.")
    assert "does not exist" in reason, reason


def test_a_record_with_no_row_count_says_that_rather_than_blaming_the_operator() -> None:
    """The other way the row count used to reach `force`.

    The asset is there and the record names a hash for it, but no row count. The
    generator cannot skip a table it has no row count for, because the record it
    writes needs one per asset and importing is the only thing that counts them.
    So it rebuilds -- and must say that is why.
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=True,
        recorded_rows=None, force=False)

    assert rebuild
    assert reason != ASKED_FOR, (
        "a record with no row count is reported as though somebody asked for "
        "every asset to be rebuilt. Issue #450.")
    assert "how many rows" in reason, reason


def test_only_the_force_argument_produces_the_asked_for_everything_reason() -> None:
    """The reason exists for exactly one cause, so nothing else may claim it."""
    causes = [
        dict(current_digest=SAME, recorded_digest=None, asset_exists=False,
             recorded_rows=None),
        dict(current_digest=SAME, recorded_digest=SAME, asset_exists=True,
             recorded_rows=None),
        dict(current_digest=SAME, recorded_digest=None, asset_exists=True,
             recorded_rows=ROWS),
        dict(current_digest=DIFFERENT, recorded_digest=SAME, asset_exists=True,
             recorded_rows=ROWS),
        dict(current_digest=SAME, recorded_digest=SAME, asset_exists=True,
             recorded_rows=ROWS),
    ]
    for arguments in causes:
        _, reason = needs_rebuilding(force=False, **arguments)
        assert reason != ASKED_FOR, arguments

    _, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=True,
        recorded_rows=ROWS, force=True)
    assert reason == ASKED_FOR


def test_the_generator_passes_the_row_count_instead_of_folding_it_into_force() -> None:
    """Otherwise the three tests above pass against a call site that lies.

    The defect in issue #450 was entirely at the call site: `needs_rebuilding`
    already had accurate branches for both cases, and the generator made them
    unreachable by passing `force=force or recorded_rows is None`. Since the
    generator imports `unreal` and cannot be imported here, this reads the call
    out of the source.
    """
    if not GENERATOR.is_file():
        pytest.fail(f"{GENERATOR.name} does not exist")

    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))

    calls = [node for node in ast.walk(tree)
             if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
             and node.func.id == "needs_rebuilding"]
    assert calls, f"{GENERATOR.name} no longer calls needs_rebuilding"

    for call in calls:
        keywords = {keyword.arg: keyword.value for keyword in call.keywords}
        assert "recorded_rows" in keywords, (
            "the generator does not pass recorded_rows to needs_rebuilding, so "
            "a missing row count has to reach it some other way -- which is how "
            "a brand-new table came to be reported as `asked to rebuild "
            "everything`. Issue #450.")

        force = keywords.get("force")
        assert isinstance(force, ast.Name), (
            "the generator passes something other than the force flag itself as "
            f"`force` ({ast.dump(force) if force else 'nothing'}). Folding "
            "another condition into it makes that condition report the wrong "
            "reason, because force is checked first. Issue #450.")


@pytest.mark.parametrize("value,expected", [
    ("1", True),
    ("yes", True),
    ("0", True),
    ("", False),
    ("   ", False),
])
def test_any_non_empty_value_turns_the_rebuild_switch_on(value, expected) -> None:
    """`0` counts as on, deliberately.

    Somebody setting it to `0` almost certainly means "on", and a variable that
    has to be spelled exactly right to do anything is a variable that silently
    does nothing.
    """
    assert rebuild_everything({REBUILD_ALL_VARIABLE: value}) is expected


def test_the_switch_is_off_when_the_variable_is_absent() -> None:
    assert rebuild_everything({}) is False


#: What the record held about one table before the run. The hash is the old one,
#: so a test can tell a preserved entry from a rewritten one by looking at it.
PREVIOUS = {"asset": "DT_StatusEffects", "csv": "StatusEffects.csv",
            "rows": 50, "csv_sha256": SAME}


def test_an_asset_that_reached_disk_is_recorded_against_the_current_csv() -> None:
    """The ordinary case: this is what the record is for."""
    entry = entry_for_table(
        asset="DT_StatusEffects", csv="StatusEffects.csv", rows=52,
        current_digest=DIFFERENT, previous_entry=PREVIOUS, saved=True)

    assert entry == {"asset": "DT_StatusEffects", "csv": "StatusEffects.csv",
                     "rows": 52, "csv_sha256": DIFFERENT}


def test_an_asset_the_editor_could_not_write_keeps_its_previous_entry() -> None:
    """The failure issue #587 exists for, and the reason this function exists.

    The editor failed to write `DT_StatusEffects.uasset` -- the interactive
    editor was open and held the file -- and the generator recorded the new row
    count and the new hash regardless. `test_datatable_assets_are_current.py`
    then passed, because the record matched the CSV, while the asset on disk was
    six days old. The next run read that record, decided the table was already
    current and skipped it, which made the staleness permanent.

    KEEPING THE PREVIOUS ENTRY IS WHAT MAKES THE NEXT RUN REBUILD IT. The old
    hash no longer matches the current CSV, so `needs_rebuilding` answers "its
    source changed".
    """
    entry = entry_for_table(
        asset="DT_StatusEffects", csv="StatusEffects.csv", rows=52,
        current_digest=DIFFERENT, previous_entry=PREVIOUS, saved=False)

    assert entry == PREVIOUS, (
        "an asset the editor could not write is being recorded as though it "
        "was written. The record then says the asset was built from the current "
        "CSV when it holds the previous numbers, the Python test passes over it, "
        "and the next run skips the table as already current. Issue #587.")

    rebuild, reason = needs_rebuilding(
        current_digest=DIFFERENT, recorded_digest=entry["csv_sha256"],
        asset_exists=True, recorded_rows=entry["rows"])
    assert rebuild, (
        "the entry kept for a failed save does not make the next run rebuild "
        f"the table ({reason}), which is the whole point of keeping it")


def test_a_new_table_whose_first_save_failed_is_recorded_not_at_all() -> None:
    """There is no previous entry to keep, so nothing truthful can be said.

    A table missing from the record is one the next run rebuilds, and
    `test_every_table_the_generator_builds_is_recorded` fails and names it. Both
    are the wanted behaviour; the wrong answer would be an entry claiming a
    build that did not happen.
    """
    entry = entry_for_table(
        asset="DT_EnemyRarities", csv="EnemyRarities.csv", rows=6,
        current_digest=DIFFERENT, previous_entry=None, saved=False)

    assert entry is None


def test_the_generator_records_only_what_it_managed_to_save() -> None:
    """Otherwise the three tests above pass against code nothing calls.

    READ FROM THE SOURCE, for the same reason as the check below it: the
    generator imports `unreal` and cannot be imported here.
    """
    if not GENERATOR.is_file():
        pytest.fail(f"{GENERATOR.name} does not exist")

    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))

    imported = {
        alias.name
        for node in ast.walk(tree)
        if isinstance(node, ast.ImportFrom) and node.module == "datatable_freshness"
        for alias in node.names
    }
    assert "entry_for_table" in imported, (
        "the DataTable asset generator no longer imports entry_for_table, so "
        "nothing stops it recording an asset the editor failed to write. "
        "Issue #587.")

    called = {
        node.func.id
        for node in ast.walk(tree)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "entry_for_table" in called, (
        "the DataTable asset generator imports entry_for_table but never calls "
        "it, so every table is recorded whether or not it was written.")


def test_the_generator_actually_uses_the_decision() -> None:
    """Otherwise every test above passes against code nothing calls.

    READ FROM THE SOURCE, because the generator imports `unreal` and cannot be
    imported here. This is the weakest check in the file and it is the one that
    ties the rest to anything real, so it is written to fail loudly rather than
    to be thorough.
    """
    if not GENERATOR.is_file():
        pytest.fail(f"{GENERATOR.name} does not exist")

    source = GENERATOR.read_text(encoding="utf-8")
    tree = ast.parse(source)

    imported = {
        alias.name
        for node in ast.walk(tree)
        if isinstance(node, ast.ImportFrom) and node.module == "datatable_freshness"
        for alias in node.names
    }
    assert "needs_rebuilding" in imported, (
        "the DataTable asset generator no longer imports needs_rebuilding, so "
        "it rebuilds every asset whatever changed. Issue #444.")

    called = {
        node.func.id
        for node in ast.walk(tree)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "needs_rebuilding" in called, (
        "the DataTable asset generator imports needs_rebuilding but never calls "
        "it, so nothing is skipped.")
