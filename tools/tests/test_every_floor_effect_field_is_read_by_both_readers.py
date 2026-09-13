"""Every field of the player's floor-effects struct is handled by both functions that
walk it field by field.

WHAT THE STRUCT IS. `FCataclysmPlayerFloorEffects` in
`game/Source/Cataclysm/Dungeon/CataclysmDungeonModifierEffects.h` holds what a dungeon
floor's modifiers do to the player, as a float per effect. Three fields are filled once
per floor and four are recomputed on the game mode's quarter-second beat.

WHY THIS TEST EXISTS. **Three separate functions walk that struct naming each field, and
all three are hand-written.** `StatModifiersFor` turns the fields into stat modifiers and
is the one that makes a rule bite; `IsEmpty()` answers whether the floor does anything at
all; `Describe()` builds the sentence the floor panel and the per-floor log show a player.
Adding a field and forgetting one of the others produces:

    StatModifiersFor missing it   the rule silently does nothing
    IsEmpty missing it            a floor that IS doing something reports as empty
    Describe missing it           the rule works and the player is never told why

**None of those three is a compile error and none is caught by any other test.** The
middle two are the quiet ones: the game behaves correctly and only the reporting lies.

THIS FOUND ONE THE DAY IT WAS WRITTEN. `MovementSpeedLessPercent`, added for the
Singularity Wells dungeon modifier, was in `IsEmpty()` and in `StatModifiersFor` and
missing from `Describe()` — so a player slowed by 40% on a floor carrying that modifier
was told nothing about it by the floor panel. Issue #41.

WHY IT READS THE TEXT RATHER THAN RUNNING THE ENGINE. The property is about which names
appear in which function, which the source states. A C++ automation test could assert the
output of `Describe` for a given struct, but only for the fields somebody remembered to
write a case for — **which is the same omission this is trying to catch.** Reading the
declarations is the only form that cannot miss a field by being written to miss it.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DUNGEON = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
HEADER = DUNGEON / "CataclysmDungeonModifierEffects.h"
SOURCE = DUNGEON / "CataclysmDungeonModifierEffects.cpp"

#: Below this, assume the parse failed rather than that the struct shrank. The struct has
#: had at least three fields since it was introduced and only ever grows; a run finding
#: one or none has matched the wrong thing, and every assertion here would then pass
#: against an empty set. A positive control, in the shape
#: `tools/tests/test_the_decisions_log_names_real_files.py` uses.
FEWEST_FIELDS_A_WORKING_PARSER_FINDS = 5


def body_of(text: str, opening: str) -> str:
    """The braced body that follows `opening`, by counting braces.

    A regex cannot do this: every one of these functions contains nested braces, and
    `.*?\\}` stops at the first inner one, which silently returns a fragment. A fragment
    is the dangerous answer here, because a field mentioned after the cut reads as absent.
    """
    start = text.index(opening)
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"braces never balanced after {opening!r}")


@pytest.fixture(scope="module")
def fields() -> list[str]:
    """Every float field of `FCataclysmPlayerFloorEffects`, in declaration order."""
    struct = body_of(HEADER.read_text(encoding="utf-8"),
                     "struct CATACLYSM_API FCataclysmPlayerFloorEffects")
    return re.findall(r"^\tfloat (\w+) = ", struct, flags=re.MULTILINE)


def test_the_struct_was_found_and_holds_the_fields(fields: list[str]) -> None:
    """The positive control, and the reason a later pass is believable.

    Every other test here asks whether a name appears in a function. If `fields` were
    empty — a renamed struct, a moved file, a changed declaration style — all of them
    would pass having compared nothing.
    """
    assert len(fields) >= FEWEST_FIELDS_A_WORKING_PARSER_FINDS, (
        f"found {len(fields)} float field(s) in FCataclysmPlayerFloorEffects: {fields}. "
        f"That is too few to be the real struct, so the parse in {HEADER.name} has "
        "broken rather than the struct having shrunk. Check the declaration style: this "
        "expects one tab, `float `, a name, and ` = `.")
    assert all(f.endswith("Percent") for f in fields), (
        f"every field of this struct has been a percentage so far and {fields} is not. "
        "If that is deliberate the naming assertion here should go; if it is a typo, it "
        "is the kind `StatModifiersFor` would apply as the wrong kind of number.")


@pytest.mark.parametrize("function, where", [("bool IsEmpty() const", "the header"),
                                             ("FString UCataclysmDungeonModifierEffects"
                                              "::Describe", "the source file")])
def test_every_field_is_named_by_the_function(fields: list[str], function: str,
                                              where: str) -> None:
    """The check itself, for the two functions that walk the struct field by field.

    `StatModifiersFor` is deliberately NOT checked here. It is the third walker, but it
    is allowed to skip a field: a field could be reported to the player and applied by
    some route other than a stat modifier. `IsEmpty` and `Describe` have no such
    exemption -- a field that does something the floor panel cannot say, or that leaves
    the floor reporting itself as empty, is a defect in every case.
    """
    text = (HEADER if where == "the header" else SOURCE).read_text(encoding="utf-8")
    missing = [f for f in fields if f not in body_of(text, function)]

    assert not missing, (
        f"{len(missing)} field(s) of FCataclysmPlayerFloorEffects are not named by "
        f"`{function}` in {where}: {', '.join(missing)}.\n"
        "That function is hand-written field by field and nothing else reports the "
        "omission. `IsEmpty` missing a field makes a floor that IS doing something "
        "report as empty; `Describe` missing one makes a rule take effect with the "
        "floor panel and the per-floor log saying nothing about it.\n"
        f"Add a clause for each, in {HEADER.name} and {SOURCE.name}.")
