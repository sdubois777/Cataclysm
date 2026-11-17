"""Decide which DataTable assets need rebuilding, and what to record about them.

WHY THIS IS ITS OWN MODULE. `tools/generate_datatable_assets.py` imports
`unreal` at module level and runs inside the editor's Python interpreter, so no
ordinary test can import it -- `tools/tests/test_datatable_assets_are_current.py`
parses its source with `ast` for exactly that reason. The two decisions below are
plain arithmetic on hashes and have nothing to do with the engine, so they live
here where a test can call them directly instead of reading them.

WHY THE DECISION EXISTS AT ALL. Issue #444. The generator rebuilt all fourteen
assets whenever it ran, whatever had changed. Renaming one enemy modifier for
issue #358 left fourteen modified `.uasset` files, thirteen of them with no
content change. Those are binary and tracked with git LFS, so a pull request
cannot show a diff of one, and **a reviewer seeing fourteen changed binary files
has no way to tell which one carries the change.**

It is not ordinary serialisation noise that a byte comparison would filter out.
The generator's own docstring records why: a `.uasset` carries generated
identifiers that differ between runs, so two runs over unchanged input never
produce identical bytes. The only way to leave an asset alone is not to rebuild
it.
"""

from __future__ import annotations

import os

#: Set this to rebuild every asset whether or not its source moved.
#:
#: AN ENVIRONMENT VARIABLE RATHER THAN A FLAG, because the generator runs inside
#: the editor: `tools/run_editor_python.py` takes a script path and a timeout and
#: forwards nothing else to it. An environment variable reaches the child process
#: without changing the runner.
#:
#: WHAT IT IS FOR. An asset hand-edited in the editor, or one whose import
#: settings changed, has bytes that no longer match its CSV while the CSV's hash
#: is unchanged. Nothing here can detect that, so there has to be a way to say
#: "build them all anyway".
REBUILD_ALL_VARIABLE = "CATACLYSM_REBUILD_ALL_DATATABLES"


def rebuild_everything(environment: dict[str, str] | None = None) -> bool:
    """Whether the caller asked for every asset to be rebuilt.

    ANY NON-EMPTY VALUE COUNTS. Somebody setting it to `0` almost certainly
    means "on", and a variable that has to be spelled exactly right to work is a
    variable that silently does nothing.
    """
    values = os.environ if environment is None else environment
    return bool(values.get(REBUILD_ALL_VARIABLE, "").strip())


def needs_rebuilding(current_digest: str,
                     recorded_digest: str | None,
                     asset_exists: bool,
                     recorded_rows: int | None,
                     force: bool = False) -> tuple[bool, str]:
    """Whether one table must be rebuilt, and why, in words.

    @param current_digest   the SHA-256 of the CSV as it is now
    @param recorded_digest  what the record says it was when the asset was
                            built, or None if the record has no entry for it
    @param asset_exists     whether the asset is actually there
    @param recorded_rows    how many rows the record says the asset has, or None
                            if it does not say
    @param force            the caller asked for everything

    THE REASON IS RETURNED, NOT LOGGED HERE, so that the generator prints one
    line per table saying what it did and why. A run that quietly skips
    everything looks exactly like a run that did nothing because it was broken,
    which is the fault issue #436 was about in a different file.

    THE REASON IS ALSO THE ONLY ACCOUNT OF WHY A BINARY FILE CHANGED. Issue #450.
    A `.uasset` is stored in git LFS and cannot be reviewed by reading it, so a
    reviewer asking why one changed has this line and nothing else. It named the
    wrong cause for a table that had never been built: the generator folded "the
    record says nothing about its row count" into `force`, and `force` is checked
    first, so a brand-new table was reported as `asked to rebuild everything`
    with no environment variable set. `recorded_rows` is its own argument now so
    that each case says what actually happened.
    """
    if force:
        return True, "asked to rebuild everything"

    if not asset_exists:
        # BEFORE THE HASH COMPARISON. A record can name a hash for an asset that
        # was deleted or never committed, and skipping then would leave nothing
        # on disk while reporting success.
        return True, "the asset does not exist"

    if recorded_digest is None:
        return True, "nothing recorded about what it was built from"

    if recorded_rows is None:
        # A SEPARATE CASE FROM THE ONE ABOVE, because something IS recorded. The
        # row count matters because the generator has to put one in the record
        # for a table it skips, and importing is the only thing that counts them.
        return True, "the record does not say how many rows it has"

    if recorded_digest != current_digest:
        return True, "its source changed"

    return False, "already current"


def entry_for_table(asset: str,
                    csv: str,
                    rows: int | None,
                    current_digest: str,
                    previous_entry: dict | None,
                    saved: bool) -> dict | None:
    """What to record about one table, or None to record nothing about it.

    @param asset           the DataTable asset's name, e.g. `DT_StatusEffects`
    @param csv             the CSV it is built from, e.g. `StatusEffects.csv`
    @param rows            how many rows it has now, or None if unknown
    @param current_digest  the SHA-256 of the CSV as it is now
    @param previous_entry  what the record already said about this table
    @param saved           whether the asset was actually written to disk

    THE POINT OF THIS FUNCTION IS THE `saved` ARGUMENT. Issue #587. The editor
    can fail to write a `.uasset` -- the usual cause is the interactive editor
    being open and holding the file, which makes Windows refuse the rename with
    error code 32 -- and the generator recorded the new hash anyway. The record
    then said the asset had been built from the current CSV when the asset on
    disk was six days old, `tools/tests/test_datatable_assets_are_current.py`
    passed over it, and the next run read the record, decided the table was
    already current, and skipped it. The staleness became permanent and nothing
    reported it.

    SO A FAILED SAVE KEEPS THE PREVIOUS ENTRY RATHER THAN THE NEW ONE, and that
    is deliberate rather than merely conservative. The previous entry is the
    truthful account of what the asset on disk was built from. Keeping it means
    the next run compares the current CSV against the OLD hash, sees they differ,
    and rebuilds -- and means the Python test fails with the accurate message,
    that this CSV changed since its asset was built, rather than with a vaguer
    one about a missing entry.

    A table that has never been built has no previous entry, so a first build
    that fails to save records nothing at all. That is also correct: a table
    missing from the record is one the next run rebuilds, and the test that
    every table is recorded fails and names it.
    """
    if not saved:
        return previous_entry

    return {"asset": asset, "csv": csv, "rows": rows, "csv_sha256": current_digest}
