"""The attack intervals copied into the measuring tool match the model.

WHY THIS FILE EXISTS. Issue #1134. `tools/measure_attack_impact.py` holds a copy
of every creature's designed attack interval, and the comment above that copy has
always said this file holds it to the model:

    THE INTERVALS ARE COPIES OF `ARCHETYPES` in sim/cataclysm_sim/enemy_stats.py,
    which is authoritative. They are repeated here because this script runs
    inside the editor's interpreter and cannot import the simulation package.
    `tools/tests/test_attack_impact_fits_the_telegraph.py` holds them to the
    model, so a drift fails there rather than being reported wrongly here.

This file did not exist. A comment that names a guard reads as evidence the guard
exists, which is worse than saying nothing, so the copies sat unchecked while the
tool said otherwise.

WHY THE TOOL IS READ AS TEXT AND NOT IMPORTED. `tools/measure_attack_impact.py`
does `import unreal` at module level. That module exists only inside the Unreal
editor's own Python interpreter, so importing the tool from the fast suite raises
`ModuleNotFoundError` and nothing here would run. The `CLIPS` list is parsed out
of the file's source instead.

WHY THE PLAYER ROWS ARE SKIPPED. Four `Player` rows were added on 2026-09-01 for
issue #1133. The player is not an enemy archetype and has no entry in
`ARCHETYPES`. Its interval is not one number at all: it comes from the equipped
weapon's attack speed in `game/Data/ItemBases.csv`, and the figure in the tool is
the starting Greataxe's, printed as context only. Checking it against the model
would fail on a correct file. That the rows are skipped **for a stated reason**
is itself checked below, so the skip cannot quietly grow to cover a creature.

THE FIELD IS `attack_interval`. There is no `attack_interval_seconds`. A check
written against that name reads `None` for every creature and, compared with
`!=`, reports all seven as mismatched -- which looks like a drift and is a typo.
"""

from __future__ import annotations

import ast
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = REPO_ROOT / "tools" / "measure_attack_impact.py"

#: The one name in `CLIPS` that is not an enemy archetype. See the module
#: docstring for why its interval cannot be checked against the model.
NOT_AN_ARCHETYPE = "Player"

#: Floating point equality on numbers written out by hand in two files. The
#: intervals are stated to at most four decimal places in both.
CLOSE_ENOUGH = 1e-9


def tool_source() -> str:
    if not TOOL.is_file():
        pytest.skip(f"{TOOL.name} is not present")
    return TOOL.read_text(encoding="utf-8")


def clips_in_the_tool() -> list[tuple[str, float, str]]:
    """The `CLIPS` list, read out of the tool's source rather than imported.

    PARSED WITH `ast` RATHER THAN A REGULAR EXPRESSION. The entries wrap across
    lines and the asset paths are written as adjacent string literals that
    Python joins, so a line-based search would read half a path and a
    handwritten join would have to reimplement that rule. `ast.literal_eval`
    already knows it.
    """
    tree = ast.parse(tool_source(), filename=str(TOOL))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        names = [target.id for target in node.targets
                 if isinstance(target, ast.Name)]
        if "CLIPS" in names:
            return [tuple(entry) for entry in ast.literal_eval(node.value)]
    raise AssertionError(
        f"{TOOL.name} no longer assigns a module-level `CLIPS` list. It is the "
        "list of every clip a creature plays as its ordinary attack, with the "
        "creature's designed attack interval beside it, and this file exists to "
        "hold those intervals to `ARCHETYPES` in "
        "sim/cataclysm_sim/enemy_stats.py. Issue #1134.")


def archetypes() -> dict:
    sim = str(REPO_ROOT / "sim")
    if sim not in sys.path:
        sys.path.insert(0, sim)
    from cataclysm_sim.enemy_stats import ARCHETYPES

    return ARCHETYPES


@pytest.fixture(scope="module")
def clips() -> list[tuple[str, float, str]]:
    return clips_in_the_tool()


@pytest.fixture(scope="module")
def creature_clips(clips) -> list[tuple[str, float, str]]:
    """Every row except the player's."""
    return [row for row in clips if row[0] != NOT_AN_ARCHETYPE]


# ---------------------------------------------------------------------------
# The list is there and it is worth checking
# ---------------------------------------------------------------------------

def test_the_tool_still_states_an_interval_for_every_clip(clips):
    """A check over an empty list passes and proves nothing, so the list is
    measured before anything is asserted about its contents."""
    assert clips, f"{TOOL.name}'s CLIPS list is empty."
    for row in clips:
        assert len(row) == 3, (
            f"a CLIPS entry is {len(row)} items rather than "
            f"(creature, interval, clip path): {row!r}")


def test_more_than_one_creature_is_checked(creature_clips):
    """The skip below removes rows. This is what notices if it ever removed
    nearly all of them and left a guard that compares one number."""
    creatures = {name for name, _, _ in creature_clips}
    assert len(creatures) >= 5, (
        f"only {len(creatures)} creature(s) are checked against the model: "
        f"{sorted(creatures)}. The tool is supposed to carry a row for every "
        "creature whose ordinary attack is measured.")


def test_the_comment_naming_this_file_is_still_there():
    """The comment is the reason this file exists. If somebody deletes the
    claim, this file's name stops meaning anything and the failure should say
    so rather than this file quietly guarding a promise nobody makes."""
    assert "test_attack_impact_fits_the_telegraph.py" in tool_source(), (
        f"{TOOL.name} no longer names this file as what holds its intervals to "
        "the model. Either restore the comment or delete this file with it, "
        "rather than leaving a guard whose stated purpose is gone. Issue #1134.")


# ---------------------------------------------------------------------------
# The intervals match the model
# ---------------------------------------------------------------------------

def test_every_creature_in_the_tool_is_an_archetype(creature_clips):
    """Checked separately from the interval, because a misspelled creature name
    would otherwise raise `KeyError` and report a missing key rather than
    saying which name is wrong."""
    known = archetypes()
    for creature, _, clip in creature_clips:
        assert creature in known, (
            f"{TOOL.name} has a clip for {creature!r} ({clip}), and "
            "sim/cataclysm_sim/enemy_stats.py has no archetype of that name. "
            f"The archetypes are {sorted(known)}.")


def test_every_interval_matches_the_model(creature_clips):
    """The check this file is named for.

    THE MODEL IS AUTHORITATIVE and the tool holds a copy, so a difference is
    the tool being stale, not the model.
    """
    known = archetypes()
    for creature, interval, clip in creature_clips:
        designed = known[creature].attack_interval
        assert interval == pytest.approx(designed, abs=CLOSE_ENOUGH), (
            f"{TOOL.name} states an attack interval of {interval:g}s for the "
            f"{creature} (clip {clip}), and `ARCHETYPES` in "
            f"sim/cataclysm_sim/enemy_stats.py states {designed:g}s. The model "
            "is authoritative, so correct the tool. Issue #1134.")


def test_a_creature_with_several_clips_states_one_interval(creature_clips):
    """The Imp draws from five clips, the Corrupted Sentinel alternates two and
    the Abyssal Warden swings left and right. An interval belongs to the
    creature and not to the clip, so the rows for one creature cannot disagree
    with each other even before the model is consulted."""
    seen: dict[str, float] = {}
    for creature, interval, clip in creature_clips:
        if creature in seen:
            assert interval == pytest.approx(seen[creature], abs=CLOSE_ENOUGH), (
                f"{TOOL.name} states two different attack intervals for the "
                f"{creature}: {seen[creature]:g}s and {interval:g}s (clip "
                f"{clip}). The interval belongs to the creature.")
        else:
            seen[creature] = interval


# ---------------------------------------------------------------------------
# The player rows, and why they are left out
# ---------------------------------------------------------------------------

def test_the_player_rows_are_the_only_ones_left_out(clips, creature_clips):
    """The skip is narrow and stays narrow.

    WITHOUT THIS, a future edit that renamed a creature would silently drop it
    out of every check above, because the rows removed are only those the model
    happens not to know about. Naming the one exception makes that a failure.
    """
    known = archetypes()
    left_out = {name for name, _, _ in clips} - {n for n, _, _ in creature_clips}
    assert left_out <= {NOT_AN_ARCHETYPE}, (
        f"{TOOL.name} has clips for {sorted(left_out)}, which this file skips. "
        f"Only {NOT_AN_ARCHETYPE!r} is meant to be skipped, because it is not "
        "an enemy archetype and its interval comes from the equipped weapon. "
        "Anything else in that list is a creature that has stopped being "
        "checked against the model.")
    assert NOT_AN_ARCHETYPE not in known, (
        f"sim/cataclysm_sim/enemy_stats.py now has an archetype named "
        f"{NOT_AN_ARCHETYPE!r}, so the reason this file skips those rows -- "
        "that the player is not an enemy archetype -- no longer holds. Decide "
        "which is right before removing the skip.")


def test_the_tool_says_why_the_player_rows_are_not_a_designed_interval():
    """The exemption above is only defensible while the tool says what the
    player's figure is. A reader who finds four unchecked rows and no
    explanation has to work it out again."""
    source = tool_source()
    flattened = " ".join(source.split())
    assert "THE PLAYER'S INTERVAL IS NOT ONE NUMBER" in flattened, (
        f"{TOOL.name} no longer explains that the player's attack interval "
        "comes from the equipped weapon's attack speed rather than from a "
        "designed archetype figure. That explanation is why this file leaves "
        f"the {NOT_AN_ARCHETYPE!r} rows out of the comparison. Issue #1134.")
