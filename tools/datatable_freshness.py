"""Decide which DataTable assets actually need rebuilding.

WHY THIS IS ITS OWN MODULE. `tools/generate_datatable_assets.py` imports
`unreal` at module level and runs inside the editor's Python interpreter, so no
ordinary test can import it -- `tools/tests/test_datatable_assets_are_current.py`
parses its source with `ast` for exactly that reason. The decision below is
plain arithmetic on hashes and has nothing to do with the engine, so it lives
here where a test can call it directly instead of reading it.

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
                     force: bool = False) -> tuple[bool, str]:
    """Whether one table must be rebuilt, and why, in words.

    @param current_digest   the SHA-256 of the CSV as it is now
    @param recorded_digest  what the record says it was when the asset was
                            built, or None if the record has no entry for it
    @param asset_exists     whether the asset is actually there
    @param force            the caller asked for everything

    THE REASON IS RETURNED, NOT LOGGED HERE, so that the generator prints one
    line per table saying what it did and why. A run that quietly skips
    everything looks exactly like a run that did nothing because it was broken,
    which is the fault issue #436 was about in a different file.
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

    if recorded_digest != current_digest:
        return True, "its source changed"

    return False, "already current"
