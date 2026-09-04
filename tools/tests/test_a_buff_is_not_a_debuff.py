"""No buff falls under a debuff root, so nothing is paid for carrying one.

WHY THIS EXISTS. Issue #1145. Seven Masochist passive nodes are paid per debuff
carried, and `UCataclysmDebuffs::DebuffRootNames` decides what a debuff is by
naming tag branches -- a root counts itself and every child under it. Until
2026-09-04 the status effect tags were one flat branch holding the buffs, the
debuffs and the damage over times together, so the only way to widen those roots
to the twenty-seven named curses was to take the eighteen buffs with them, or to
keep a list of buff names by hand. The project owner chose to split the branch
instead, so that the tag says which kind of effect it is.

WHAT THIS CHECKS. That the split held: `Status.Debuff` is a root, every named
curse is under it, and no buff is under any root. It reads the generated tag list
and the C++ root list and compares them.

WHY IN PYTHON RATHER THAN IN THE AUTOMATION SUITE. Continuous integration runs
`python -m pytest` and nothing else, so nothing under `game/Source/Cataclysm/
Tests/` runs on a pull request and this does. The automation suite has its own
test that a buff on a real character is not counted -- this one is the cheaper
check that the two lists cannot drift apart in the first place.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TAGS_INI = REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini"
DEBUFFS_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
               / "CataclysmDebuffs.cpp")

#: A tag declared in the generated vocabulary.
DECLARED = re.compile(r'GameplayTagList=\(Tag="([A-Za-z0-9_.]+)"')

#: One entry of `UCataclysmDebuffs::DebuffRootNames`.
#:
#: READ OUT OF THE ARRAY'S OWN BRACES rather than by grepping the file for
#: TEXT("..."), because that file names other tags in its comments and in
#: `State.StunImmune`'s explanation of why it is NOT a root. Matching those would
#: make this test assert the opposite of what it is for.
ROOT_ARRAY = re.compile(
    r"UCataclysmDebuffs::DebuffRootNames\[\]\s*=\s*\{(.*?)\n\};",
    re.DOTALL)
ROOT_ENTRY = re.compile(r'^\s*TEXT\("([A-Za-z0-9_.]+)"\),', re.MULTILINE)


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="replace")


def declared_tags() -> list[str]:
    return DECLARED.findall(read(TAGS_INI))


def debuff_roots() -> list[str]:
    body = ROOT_ARRAY.search(read(DEBUFFS_CPP))
    assert body, (
        "could not find the DebuffRootNames array in "
        "game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp. If it was "
        "renamed or reformatted this test is reading nothing and proving "
        "nothing.")
    return ROOT_ENTRY.findall(body.group(1))


def under(tag: str, root: str) -> bool:
    """A root counts itself and its children, which is Unreal's own rule."""
    return tag == root or tag.startswith(root + ".")


def test_the_root_list_was_actually_read():
    """Without this every assertion below passes on an empty list."""
    roots = debuff_roots()
    assert len(roots) >= 3, f"only found these debuff roots: {roots}"
    assert "Keyword.DoT" in roots
    assert "State.Stunned" in roots


def test_the_vocabulary_was_actually_read():
    tags = declared_tags()
    assert len(tags) > 150, f"only found {len(tags)} declared tags"
    assert any(t.startswith("Status.Buff.") for t in tags)
    assert any(t.startswith("Status.Debuff.") for t in tags)


def test_no_buff_is_under_any_debuff_root():
    """The headline. A buff counted as a debuff pays seven Masochist nodes.

    The Commander buff is the one that would be felt: a Succubus grants it to
    every allied creature within 8 metres, so it is live in play rather than
    designed and unbuilt.
    """
    roots = debuff_roots()
    buffs = [t for t in declared_tags() if t.startswith("Status.Buff.")]
    assert buffs, "no buff tags at all, so this test is checking nothing"

    counted = [tag for tag in buffs if any(under(tag, r) for r in roots)]
    assert not counted, (
        f"these buffs are counted as debuffs by "
        f"UCataclysmDebuffs::DebuffRootNames {roots}: {sorted(counted)}")


def test_every_named_curse_is_under_a_debuff_root():
    """The other half. Issue #1145 was that none of them were."""
    roots = debuff_roots()
    curses = [t for t in declared_tags() if t.startswith("Status.Debuff.")]
    assert len(curses) >= 20, f"only {len(curses)} named curses were declared"

    missed = [tag for tag in curses if not any(under(tag, r) for r in roots)]
    assert not missed, (
        f"these named curses count as nothing, so the Masochist nodes paid per "
        f"debuff carried cannot see them: {sorted(missed)}")


def test_the_damage_over_time_branch_is_not_doubled():
    """`Status.DoT` must not be a root beside `Keyword.DoT`.

    The eight damage over time effects have a tag under both branches -- the
    branch says which vocabulary the tag came from rather than which effect it is
    -- and everything that applies one to a character grants the `Keyword.DoT`
    one. Naming both would count a single burn twice the day anything granted
    both tags at once.
    """
    roots = debuff_roots()
    assert "Keyword.DoT" in roots
    assert "Status.DoT" not in roots, (
        "Status.DoT was added as a debuff root beside Keyword.DoT. The two name "
        "the same eight effects, so a character carrying one burn would be "
        "reported as carrying two debuffs.")
