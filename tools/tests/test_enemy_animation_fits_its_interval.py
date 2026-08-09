"""Every enemy's attack animation must finish before it attacks again.

WHY THIS EXISTS. `game/docs/enemy-source-assets.md` carries a table of each
enemy's attack interval, the shortest attack animation its Paragon model has, and
a verdict on whether the one fits inside the other. Nothing checked it, and it had
already drifted: the table gave the Brute a 2.8 second interval when
`sim/cataclysm_sim/enemy_stats.py` says 1.6. The figure was 2.8 until play testing
on 2026-08-07 changed it, and the document was not updated with it.

That particular drift was harmless -- the Brute's attack is 0.97 seconds and
passes at either figure -- which is exactly why nobody noticed. The next one need
not be.

WHAT THIS CHECKS. That the table's intervals are the model's, that each verdict
is the arithmetic rather than an opinion, that every designed enemy appears, and
that a recorded failure cites an issue so it is tracked rather than tolerated.

WHAT IT DOES NOT CHECK. The animation lengths themselves. Those were read from
the assets through the editor, which continuous integration does not have, so
they are trusted here. Re-measuring them is
`tools/measure_animation_impact.py`-shaped work against the Paragon packs.

WIND-UP IS A DIFFERENT AND STRICTER RULE. The design document caps a telegraphed
wind-up at half the attack interval, and the whole-animation length checked here
is not that. The document says plainly that wind-up durations have not been
measured for any enemy.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: One row of the timing table. The verdict is free text so that a failing row
#: can carry its issue number.
ROW = re.compile(
    r"^\|\s*([^|]+?)\s*\|\s*([\d.]+)\s*s\s*\|\s*`([^`]+)`\s*\|"
    r"\s*([\d.]+)\s*s\s*\|\s*(.+?)\s*\|$",
    re.MULTILINE)

#: The archetype that is not a creature. It has no model and no animation.
NOT_A_CREATURE = "Baseline"


def archetypes() -> dict:
    from cataclysm_sim.enemy_stats import ARCHETYPES

    return ARCHETYPES


@pytest.fixture(scope="module")
def rows() -> list[tuple[str, float, str, float, str]]:
    """The timing table, parsed.

    FAILS RATHER THAN SKIPS when the table is gone. A skip here would take the
    only check on these figures with it and still report green, which is the
    fault issue #406 was closed on.
    """
    if not ASSET_RECORD.is_file():
        pytest.fail(f"{ASSET_RECORD.relative_to(REPO_ROOT)} does not exist")

    text = ASSET_RECORD.read_text(encoding="utf-8")
    section = text.split("## The timing constraint")
    if len(section) < 2:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer has a 'The timing "
            "constraint' section, so the attack interval each enemy's animation "
            "was chosen against is recorded nowhere.")

    body = section[1].split("\n## ")[0]
    found = [(name, float(interval), clip, float(length), verdict)
             for name, interval, clip, length, verdict in ROW.findall(body)]

    if not found:
        pytest.fail(
            "the timing table in game/docs/enemy-source-assets.md parsed to no "
            "rows at all, so every test in this file would pass without "
            "checking anything.")
    return found


def test_every_designed_enemy_appears(rows) -> None:
    """A creature missing from the table is a creature nobody checked."""
    listed = {name for name, _, _, _, _ in rows}
    expected = {name for name in archetypes() if name != NOT_A_CREATURE}

    missing = sorted(expected - listed)
    assert not missing, (
        f"these enemies have no row in the timing table in "
        f"game/docs/enemy-source-assets.md: {missing}. Each needs an attack "
        f"animation short enough for its attack interval, and an enemy that is "
        f"not in the table is one nobody has checked.")

    unknown = sorted(listed - expected)
    assert not unknown, (
        f"the timing table lists {unknown}, which are not archetypes in "
        f"sim/cataclysm_sim/enemy_stats.py. Either the name is misspelled or "
        f"the creature was removed from the design.")


def test_the_intervals_are_the_ones_the_model_designs(rows) -> None:
    """The drift this file was written for.

    THE FAILURE THIS EXISTS FOR, and it had already happened: somebody retunes
    an attack interval in the model and the document keeps the old figure. The
    document is then advice about a creature that no longer exists, and the next
    person choosing an animation chooses against the wrong number.
    """
    designed = archetypes()

    for name, interval, _, _, _ in rows:
        assert name in designed, f"{name} is not a designed archetype"
        assert interval == pytest.approx(designed[name].attack_interval), (
            f"the timing table gives {name} a {interval} s attack interval and "
            f"sim/cataclysm_sim/enemy_stats.py designs {designed[name].attack_interval} s. "
            f"The model is authoritative; update the table, and check the "
            f"verdict still holds at the real figure.")


def test_each_verdict_is_the_arithmetic_rather_than_an_opinion(rows) -> None:
    """Recomputed from the two numbers in the same row.

    NOT A REPEAT OF THE DOCUMENT'S OWN CLAIM. The verdict word is compared
    against what the interval and the length actually give, so a row that says
    Passes while its animation is longer than its interval fails here.
    """
    for name, interval, clip, length, verdict in rows:
        fits = length <= interval
        says_passes = verdict.startswith("Passes")

        assert says_passes == fits, (
            f"the timing table says {name} '{verdict}' with {clip} at "
            f"{length} s against a {interval} s interval, which "
            f"{'fits' if fits else 'does not fit'}. The verdict and the two "
            f"numbers disagree.")


def test_a_recorded_failure_names_the_issue_tracking_it(rows) -> None:
    """So a known problem is tracked rather than tolerated.

    The Corrupted Sentinel's only firing animations run 2.40 s against a 2.0 s
    interval and there is no shorter one in the pack. That is issue #369 and it
    needs a decision: play it faster, cut it, or lengthen the interval. What must
    not happen is the row sitting there as a permanent shrug.
    """
    for name, _, _, _, verdict in rows:
        if verdict.startswith("Passes"):
            continue
        assert re.search(r"#\d+", verdict), (
            f"{name}'s row records a failure without naming an issue: "
            f"{verdict!r}. A known-broken enemy with nothing tracking it is a "
            f"creature that ships broken.")


def test_the_table_is_in_interval_order(rows) -> None:
    """Because it presents itself that way, and a table sorted by nothing is
    harder to read than one sorted by anything.

    This is the check that catches a row edited in place without being moved,
    which is how the Brute's stale 2.8 s figure sat at the bottom of the table
    looking like the slowest creature in the game.
    """
    intervals = [interval for _, interval, _, _, _ in rows]
    assert intervals == sorted(intervals), (
        f"the timing table is no longer in attack interval order: {intervals}. "
        f"Move the row rather than only changing the number.")
