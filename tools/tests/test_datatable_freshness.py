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
    needs_rebuilding,
    rebuild_everything,
)

#: Any two different hashes. The values do not matter, only that they differ.
SAME = "a" * 64
DIFFERENT = "b" * 64


def test_an_unchanged_table_with_its_asset_present_is_left_alone() -> None:
    """The whole point of issue #444.

    THE FAILURE THIS EXISTS FOR is somebody restoring the old behaviour, by
    deleting the check or by making the comparison always true. Thirteen
    unrelated binary files then reappear in every data pull request.
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=True)

    assert not rebuild, (
        f"a table whose CSV has not changed is being rebuilt anyway ({reason}), "
        f"which puts an unrelated binary file into every pull request that "
        f"touches any other table. Issue #444.")
    assert reason, "a decision with no reason cannot be reported"


def test_a_changed_table_is_rebuilt() -> None:
    rebuild, reason = needs_rebuilding(
        current_digest=DIFFERENT, recorded_digest=SAME, asset_exists=True)

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
        current_digest=SAME, recorded_digest=SAME, asset_exists=False)

    assert rebuild, (
        "a table whose asset is missing is not being rebuilt, because its "
        "recorded hash still matches. The run would report success and leave "
        "no asset.")
    assert "does not exist" in reason


def test_a_table_the_record_says_nothing_about_is_rebuilt() -> None:
    """A new table, or a record written before that table existed."""
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=None, asset_exists=True)

    assert rebuild
    assert "nothing recorded" in reason


def test_forcing_rebuilds_a_table_that_is_already_current() -> None:
    """For an asset hand-edited in the editor, whose CSV never moved.

    Nothing in the record can detect that, so there has to be a way to say
    "build them all anyway".
    """
    rebuild, reason = needs_rebuilding(
        current_digest=SAME, recorded_digest=SAME, asset_exists=True, force=True)

    assert rebuild
    assert "everything" in reason


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
