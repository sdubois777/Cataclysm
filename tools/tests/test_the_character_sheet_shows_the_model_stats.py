"""The character sheet screen shows the model's 46 stats, in the model's groups.

WHY THIS EXISTS. Issue #1233. Nothing in the game showed a player a single stat,
so `UCataclysmCharacterSheetLayout` was written to show them all -- and the list
of which stats there are was already settled twice over. `STAT_GROUPS` in
`sim/cataclysm_sim/character.py` holds the 46 that make up this project's
character sheet. (The automation test `Cataclysm.Attributes.CharacterSheetIsComplete`
also counted the attribute sets against that 46 until issue #2928 removed the
counts.)

So the screen's list is a second copy, and this is what stops it drifting from
the model's. The failure it catches is quiet: a stat added to the model and not to
the screen is a stat the player can never see, and a stat on the screen that the
model does not carry is a row that reads zero for ever because nothing tunes it.

WHAT IT CHECKS AND WHERE.

    sim/cataclysm_sim/character.py                          the model's groups
    game/Source/.../Interface/CataclysmCharacterSheetLayout.cpp   the screen's

NOTHING ON A PULL REQUEST COMPILES THE C++, so the screen's list is read out of
the source text rather than run, the same way
`test_stat_baselines_match_the_attribute_set.py` reads the attribute set's
initialisers.

THE ORDER IS CHECKED AS WELL AS THE MEMBERSHIP. A sheet is a list a player reads
top to bottom, and the model's order is the design document's order: the eight
resistances follow the six other defences, and the eight damage bonuses follow
the ten other offences. Two lists holding the same stats in a different order
would pass a set comparison and still put the resistances in the wrong place.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
LAYOUT_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
              / "CataclysmCharacterSheetLayout.cpp")

sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim.character import STAT_GROUPS  # noqa: E402

#: The C++ variable holding each group against the model's name for that group.
#:
#: THE TWO SPELLINGS DIFFER ON PURPOSE. The model is written in American spelling
#: throughout and this project's prose is not, so the C++ says `DefenceStats`
#: where the model says `Defense`. `UCataclysmCharacterSheetLayout::ModelGroupName`
#: carries the same pairing in the C++ and exists for the same reason.
GROUP_VARIABLES: tuple[tuple[str, str], ...] = (
    ("ResourceStats", "Resource"),
    ("RecoveryStats", "Recovery"),
    ("DefenceStats", "Defense"),
    ("OffenceStats", "Offense"),
    ("UtilityStats", "Utility"),
)


def read_group(variable: str) -> list[str]:
    """The stat names in one `static const TArray<FString>` block."""
    source = LAYOUT_CPP.read_text(encoding="utf-8")

    opening = re.search(
        r"static const TArray<FString>\s+" + re.escape(variable) + r"\s*=\s*\{",
        source)
    if opening is None:
        pytest.fail(
            "{} declares no {}. The screen's group lists are read out of that "
            "file by name, so renaming one silently stops it being checked."
            .format(LAYOUT_CPP.name, variable))

    closing = source.index("};", opening.end())
    return re.findall(r'TEXT\("([a-z_]+)"\)', source[opening.end():closing])


def test_the_layout_file_is_where_it_is_expected():
    """A missing file must fail loudly rather than skipping every check below."""
    assert LAYOUT_CPP.is_file(), (
        "{} does not exist. Every check in this file reads it, so a move that "
        "was not followed here would turn the whole file green.".format(
            LAYOUT_CPP))


@pytest.mark.parametrize("variable,group", GROUP_VARIABLES)
def test_each_group_holds_the_model_stats_in_the_model_order(variable, group):
    """One group of the screen against the same group of the model."""
    assert group in STAT_GROUPS, (
        "The model has no group called {}. Its groups are {}.".format(
            group, ", ".join(STAT_GROUPS)))

    assert read_group(variable) == list(STAT_GROUPS[group]), (
        "{} in {} does not match STAT_GROUPS[{!r}] in "
        "sim/cataclysm_sim/character.py. A stat in the model and not on the "
        "screen is one no player can see; a stat on the screen and not in the "
        "model is a row that reads zero for ever.".format(
            variable, LAYOUT_CPP.name, group))


def test_the_screen_covers_every_group_the_model_has():
    """Neither list may grow a group the other does not know about."""
    named = {group for _, group in GROUP_VARIABLES}

    assert named == set(STAT_GROUPS), (
        "The screen groups {} and the model groups {}. A group added to one "
        "and not the other is a whole section of the sheet missing.".format(
            ", ".join(sorted(named)), ", ".join(sorted(STAT_GROUPS))))


def test_the_sheet_is_forty_six_stats():
    """The screen shows as many stats as the model's groups hold.

    READ FROM THE MODEL, NOT TYPED (issue #2928). This used to assert 46, a number
    also pinned in the C++ sheet test and reached a third way by counting the
    attribute sets; a new sheet stat meant three edits and a failing Unreal run. The
    model's `STAT_GROUPS` is the list the balance is tuned against, so the screen
    must show exactly as many, each once (below). Which stats they are is the test
    above.
    """
    on_screen = [stat for variable, _ in GROUP_VARIABLES
                 for stat in read_group(variable)]
    in_the_model = sum(len(stats) for stats in STAT_GROUPS.values())

    assert in_the_model > 0, "CONTROL: the model's stat groups hold stats"
    assert len(on_screen) == in_the_model, (
        "The screen shows {} stats and the model's STAT_GROUPS hold {}.".format(
            len(on_screen), in_the_model))

    assert len(set(on_screen)) == len(on_screen), (
        "A stat is listed in two groups on the screen. That keeps the count "
        "right while leaving a different stat off the sheet entirely.")
