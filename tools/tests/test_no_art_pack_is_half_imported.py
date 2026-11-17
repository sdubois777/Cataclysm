"""Some art packs present and others gone is a fault. None present is normal.

WHY THIS EXISTS. Issue #1461. `game/Content/ParagonRampage/` was emptied on
2026-09-05 and the Brute rendered as a plain cylinder for two days. Nothing said
so, and nothing could have: **every test that would have noticed is built to
treat that folder's absence as normal, because in a git worktree it is.**

`CataclysmTestSkip::ReportSkippedHalf` exists so a test that cannot reach part of
its subject says so rather than passing silently, and `python
tools/unreal_build.py tests` names those tests after the pass count. In a
worktree that line is routine news and every session correctly ignores it. In the
main checkout the same line means a fault. **The two print identically.**

WHAT THIS CHECKS, AND IT IS THE ONE THING THE TWO CASES DO NOT SHARE. A worktree
has NONE of the packs. A main checkout that has lost one has SOME. So:

    no pack folder present     -> a worktree. Skip; this is not the place to look
    every pack folder present  -> pass
    some present, some not     -> FAIL, naming which are missing

That distinction needs no art to state and no engine to evaluate, which is why
this is a Python test that runs in continuous integration rather than an Unreal
one that does not.

WHAT IT DELIBERATELY DOES NOT CHECK: THE RECORDED ASSET COUNTS. The manifest
records 2,105 assets for `ParagonMinions/` and so on. **This does not compare
those against files on disk**, because the relationship between "assets" as the
manifest counts them and files as the filesystem counts them has not been
measured, and cannot be measured from a worktree, which has none of them.
Asserting a relationship nobody has measured would produce a check that fails in
the main checkout for a reason nobody could explain.

**So a pack that is PARTLY deleted, with some files left, passes here.** That is
a real gap and it is stated rather than hidden. What is caught is a pack folder
that is gone or empty, which is what happened.

WHERE THE LIST OF PACKS COMES FROM. `game/docs/enemy-source-assets.md`, the
manifest that records what was imported. It is read rather than repeated, so a
seventh pack added to the table is checked without touching this file.

WHICH FOLDERS COUNT AS VENDOR PACKS is `tools/third_party_content.py`'s answer,
derived from `.gitignore`, and this cross-checks the manifest against it: a pack
in the manifest that `.gitignore` would not exclude is itself a fault, because it
would be committed.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
CONTENT = REPO_ROOT / "game" / "Content"

sys.path.insert(0, str(REPO_ROOT / "tools"))

#: The heading above the manifest's table of imported packs.
PACKS_HEADING = "## The packs"

#: A row of that table: `| `ParagonMinions/` | 4.76 GB | 2,105 |`
#:
#: THE FOLDER NAME IS NOT REQUIRED TO START WITH "Paragon", AND THAT IS THE WHOLE
#: POINT OF THE CROSS-CHECK BELOW. The first version of this pattern demanded it,
#: which made `test_every_recorded_pack_would_be_kept_out_of_git` a test that
#: could not fail: renaming a row to something `.gitignore` does not exclude did
#: not produce a pack that failed the check, it produced a line that stopped
#: being a row at all. A break-and-run on 2026-09-12 reported "expected to fire:
#: True, actually fired: False", which is how it was found.
#:
#: `.gitignore` excludes six prefixes and only one of them is `Paragon` --
#: `_SplineVFX`, `SplineEffect2`, `Vefects`, `Knife_light` and
#: `Medieval_Weapons` are the others -- so a non-Paragon pack in this table is a
#: real possibility rather than a hypothetical one.
PACK_ROW = re.compile(
    r"^\|\s*`(?P<folder>[A-Za-z_][\w\-.]*)/`\s*\|"
    r"\s*(?P<size>[\d.]+\s*[KMGT]B)\s*\|"
    r"\s*(?P<assets>[\d,]+)\s*\|", re.MULTILINE)


def third_party():
    import third_party_content

    return third_party_content


def recorded_packs() -> dict[str, int]:
    """Each pack folder the manifest records, and the asset count beside it.

    READ FROM ONE SECTION AND NOT THE WHOLE FILE. The manifest is about 60,000
    bytes and holds several tables. Now that the row pattern accepts any folder
    name rather than only `Paragon*`, searching the whole file could pick up a
    row from another table that happens to share the shape. The section heading
    bounds it.
    """
    if not MANIFEST.is_file():
        pytest.skip(f"{MANIFEST.name} is not present")
    text = MANIFEST.read_text(encoding="utf-8", errors="replace")

    start = text.find(PACKS_HEADING)
    assert start != -1, (
        f"{MANIFEST.name} no longer has a {PACKS_HEADING!r} section, which is "
        "where it records what art was imported. Issue #1461.")
    end = text.find("\n## ", start + len(PACKS_HEADING))
    section = text[start:end if end != -1 else len(text)]

    return {m.group("folder"): int(m.group("assets").replace(",", ""))
            for m in PACK_ROW.finditer(section)}


def packs_on_disk(content_root: pathlib.Path,
                  wanted: dict[str, int]) -> dict[str, int]:
    """How many files each named folder holds under `content_root`.

    A folder that does not exist is absent from the result. A folder that exists
    and holds no file at all maps to 0, which is the state issue #1461 is about:
    the directory survived and its contents did not.

    TAKES THE ROOT AS AN ARGUMENT so the comparison below can be proved against a
    directory built for the purpose. A worktree has none of these folders, so a
    check written only against the real one could never be shown to fire.
    """
    found = {}
    for name in wanted:
        folder = content_root / name
        if not folder.is_dir():
            continue
        found[name] = sum(1 for path in folder.rglob("*") if path.is_file())
    return found


def half_imported(recorded: dict[str, int],
                  found: dict[str, int]) -> list[str]:
    """The packs that are missing when at least one other pack is there.

    Returns an empty list both when every pack is present and when none is, and
    those two are NOT the same answer -- the caller has to tell them apart. See
    `test_the_two_empty_answers_are_different` below, which is the check on that.
    """
    usable = {name for name, files in found.items() if files > 0}
    if not usable:
        return []
    return sorted(set(recorded) - usable)


# ---------------------------------------------------------------------------
# The manifest is readable and says what this depends on
# ---------------------------------------------------------------------------

def test_the_manifest_records_some_packs():
    """A check over an empty table passes and proves nothing."""
    packs = recorded_packs()
    assert packs, (
        f"{MANIFEST.name} no longer has a table of imported art packs that this "
        "can read. It is the list of what should be on disk; if the table was "
        "reformatted, follow it here rather than deleting this file. "
        "Issue #1461.")
    assert len(packs) >= 2, (
        f"{MANIFEST.name} records only {len(packs)} pack(s), so 'some present "
        f"and some missing' is barely a state this can be in: {sorted(packs)}")


def test_every_recorded_pack_has_a_positive_asset_count():
    """A row whose count failed to parse would read as zero and quietly weaken
    every statement made from the table."""
    for name, assets in sorted(recorded_packs().items()):
        assert assets > 0, (
            f"{MANIFEST.name} records {assets} assets for {name}/, which is "
            "either a typo or a row this file is misreading.")


def test_every_recorded_pack_would_be_kept_out_of_git():
    """A pack in the manifest that `.gitignore` does not exclude is a fault in
    its own right: it would be committed.

    THE TWO LISTS ARE DERIVED SEPARATELY, which is what makes this worth
    checking. The manifest is written by hand; the exclusion patterns come from
    `.gitignore`. Nothing keeps them in step.
    """
    prefixes = third_party().folder_prefixes()
    assert prefixes, (
        ".gitignore no longer yields any third-party folder prefix, so this "
        "cannot tell a vendor pack from authored content.")

    for name in sorted(recorded_packs()):
        assert any(name.startswith(prefix) for prefix in prefixes), (
            f"{MANIFEST.name} records {name}/ as an imported pack and "
            f".gitignore would not exclude it -- its prefixes are {prefixes}. "
            "Either the pack is about to be committed or the manifest names a "
            "folder that is not a vendor pack.")


# ---------------------------------------------------------------------------
# The check itself
# ---------------------------------------------------------------------------

def test_no_recorded_pack_is_missing_while_others_are_present():
    """The check issue #1461 was opened for.

    IT SKIPS IN A WORKTREE AND THAT IS THE POINT, not a weakness: a worktree has
    none of these folders, so there is nothing here to be half of. It does its
    work in the main checkout, where having some and not others means a pack was
    lost.
    """
    recorded = recorded_packs()
    found = packs_on_disk(CONTENT, recorded)
    usable = {name for name, files in found.items() if files > 0}

    if not usable:
        pytest.skip(
            "no imported art pack is present under game/Content/, which is "
            "normal in a git worktree -- the packs are gitignored and exist "
            "only in the main checkout. Nothing to compare.")

    missing = half_imported(recorded, found)
    assert not missing, (
        f"{len(usable)} of {len(recorded)} imported art packs are present under "
        f"game/Content/ and these are not: {', '.join(missing)}. A pack that is "
        "gone or empty makes every creature it dresses render as a plain "
        "cylinder, and every test that would notice treats its absence as "
        "normal because in a worktree it is. Re-import it from Fab, or from the "
        f"offline archive named in {MANIFEST.name}. Issue #1461.")


# ---------------------------------------------------------------------------
# Proving the comparison, which cannot be done against this worktree
# ---------------------------------------------------------------------------

def build(root: pathlib.Path, folders: dict[str, int]) -> pathlib.Path:
    """A content directory holding each named folder with that many files.

    A count of 0 makes the folder and leaves it empty, which is the state issue
    #1461 is about.
    """
    root.mkdir(parents=True, exist_ok=True)
    for name, count in folders.items():
        folder = root / name
        folder.mkdir()
        for index in range(count):
            (folder / f"Asset{index}.uasset").write_bytes(b"")
    return root


def test_every_pack_present_is_clean(tmp_path):
    recorded = {"ParagonOne": 100, "ParagonTwo": 200}
    root = build(tmp_path / "content", {"ParagonOne": 3, "ParagonTwo": 4})
    assert half_imported(recorded, packs_on_disk(root, recorded)) == []


def test_a_deleted_pack_is_named(tmp_path):
    """The exact shape of issue #1461: one folder gone, the rest there."""
    recorded = {"ParagonOne": 100, "ParagonTwo": 200, "ParagonThree": 300}
    root = build(tmp_path / "content", {"ParagonOne": 3, "ParagonThree": 5})
    assert half_imported(recorded, packs_on_disk(root, recorded)) == ["ParagonTwo"]


def test_an_emptied_pack_is_named(tmp_path):
    """The folder survives and its contents do not, which is what happened on
    2026-09-05. A check that only asked whether the directory existed would pass
    on this."""
    recorded = {"ParagonOne": 100, "ParagonTwo": 200}
    root = build(tmp_path / "content", {"ParagonOne": 3, "ParagonTwo": 0})
    assert half_imported(recorded, packs_on_disk(root, recorded)) == ["ParagonTwo"]


def test_a_pack_emptied_but_for_a_stray_directory_is_still_named(tmp_path):
    """Deleting a pack's contents can leave empty subdirectories behind.
    `packs_on_disk` counts files and not entries, so this still reads as empty.
    """
    recorded = {"ParagonOne": 100, "ParagonTwo": 200}
    root = build(tmp_path / "content", {"ParagonOne": 3, "ParagonTwo": 0})
    (root / "ParagonTwo" / "Characters" / "Heroes").mkdir(parents=True)
    assert half_imported(recorded, packs_on_disk(root, recorded)) == ["ParagonTwo"]


def test_no_pack_present_is_not_reported_as_missing(tmp_path):
    """A worktree. Every pack is absent and that is not a fault.

    THIS IS THE CASE THE WHOLE DESIGN TURNS ON. If it returned the full list,
    the check would fail in every worktree and be disabled within a day.
    """
    recorded = {"ParagonOne": 100, "ParagonTwo": 200}
    root = build(tmp_path / "content", {})
    assert half_imported(recorded, packs_on_disk(root, recorded)) == []


def test_the_two_empty_answers_are_different(tmp_path):
    """`half_imported` returns an empty list for both "all present" and "none
    present", so the caller must tell them apart and this is the check that it
    can. Reading the empty list alone would make a worktree look complete."""
    recorded = {"ParagonOne": 100, "ParagonTwo": 200}

    everything = packs_on_disk(build(tmp_path / "all", recorded), recorded)
    nothing = packs_on_disk(build(tmp_path / "none", {}), recorded)

    assert half_imported(recorded, everything) == []
    assert half_imported(recorded, nothing) == []
    assert {n for n, files in everything.items() if files > 0} == set(recorded)
    assert {n for n, files in nothing.items() if files > 0} == set()
