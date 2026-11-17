"""Sentences about the Brute must say what the model and the code say.

WHY THIS EXISTS. Issue #397. Two merged pull requests changed the Brute's numbers
and left prose describing the old ones: `docs/Cataclysm_GDD_v2.md` said it had two
abilities one line above a table listing three, and two comments in
`sim/cataclysm_sim/enemy_abilities.py` stated a 2.8 second attack interval as
though it were current when the figure had moved to 1.6 after play testing.

WHY PROSE IS WORTH GUARDING AT ALL. `docs/` is the design in this project and is
authoritative -- `CLAUDE.md` says a design decision is not real until it is there
-- so a wrong sentence in it is a wrong design rather than a stale note. Nothing
compares prose against data, which is how a document came to contradict its own
table one line apart.

WHAT THESE CANNOT DO. They cannot check that a sentence reads well or that its
reasoning is sound. They check the handful of figures in it that also exist as
data, which is the class of error that actually happened.
"""

from __future__ import annotations

import math
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
ABILITIES_PY = REPO_ROOT / "sim" / "cataclysm_sim" / "enemy_abilities.py"

sys.path.insert(0, str(REPO_ROOT / "sim"))


def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def spelled(count: int) -> str:
    """The word the document writes a small count with."""
    words = {1: "one", 2: "two", 3: "three", 4: "four", 5: "five"}
    if count not in words:
        pytest.fail(f"no word for {count}; extend this mapping")
    return words[count]


def test_the_brute_paragraph_counts_its_abilities_correctly() -> None:
    """The sentence above the ability table must agree with the table.

    THE FAILURE THIS EXISTS FOR, exactly as it happened: pull request #394 gave
    the Brute a third ability, the table below the sentence gained a third row,
    and the sentence kept saying two.
    """
    from cataclysm_sim.enemy_abilities import abilities

    real = len(abilities("Brute"))

    sentence = re.search(
        r"\*\*The Brute is the enemy the anti-stun-lock rule was written for\*\*"
        r"[^\n]*", document())
    if sentence is None:
        pytest.fail(
            "docs/Cataclysm_GDD_v2.md no longer has the paragraph introducing "
            "the Brute. If it was reworded, reword this test with it; if it was "
            "deleted, nothing in the document introduces the enemy."
        )

    expected = f"It has {spelled(real)} abilities"
    assert expected in sentence.group(0), (
        f"ABILITIES['Brute'] in sim/cataclysm_sim/enemy_abilities.py holds "
        f"{real} abilities, so the paragraph should say {expected!r}. It says: "
        f"{sentence.group(0)[:200]}"
    )


def test_the_brute_ability_table_has_a_row_for_each_one() -> None:
    """And the table itself, so the sentence is not checked against nothing."""
    from cataclysm_sim.enemy_abilities import abilities

    body = document()
    for ability in abilities("Brute"):
        assert re.search(rf"^\|\s*{re.escape(ability.name)}\s*\|", body, re.M), (
            f"docs/Cataclysm_GDD_v2.md has no table row for the Brute's "
            f"{ability.name}, which sim/cataclysm_sim/enemy_abilities.py "
            f"designs."
        )


def test_the_document_states_the_telegraph_threshold_the_formula_gives() -> None:
    """The slowest attack interval that can carry a marker, in the document.

    WHAT WAS WRONG. The paragraph said "only enemies with an attack interval of 2
    seconds or more can telegraph anything". The real figure falls out of the
    formula the same section states: the smallest useful marker is 1 metre, the
    reaction allowance is 0.4 seconds and the walk-out speed is 3.5 metres per
    second, so the smallest cycle that can carry one is 2 x (0.4 + 1 / 3.5),
    which is 1.371 seconds.

    IT WAS NOT A HARMLESS ROUNDING. At 2 seconds the paragraph excluded the
    Brute, whose interval moved to 1.6 after play testing, and the baseline enemy
    at 1.5. Both can telegraph. Found while re-checking this arithmetic at 1.6
    for issue #397.
    """
    from cataclysm_sim.enemy_abilities import (
        REACTION_ALLOWANCE, SMALLEST_USEFUL_MARKER_METRES, WALK_OUT_SPEED)

    derived = 2.0 * (REACTION_ALLOWANCE
                     + SMALLEST_USEFUL_MARKER_METRES / WALK_OUT_SPEED)

    stated = re.search(
        r"attack interval of ([\d.]+) seconds or more can telegraph anything",
        document())
    if stated is None:
        pytest.fail(
            "docs/Cataclysm_GDD_v2.md no longer states the slowest attack "
            "interval that can telegraph anything. That sentence is what makes "
            "the claim about swarm enemies checkable."
        )

    assert float(stated.group(1)) == pytest.approx(derived, abs=0.01), (
        f"The document says an attack interval of {stated.group(1)} seconds is "
        f"the smallest that can telegraph anything. The formula in the same "
        f"section gives {derived:.3f}. One of the three inputs moved, or the "
        f"figure was rounded to something the formula does not support."
    )


def test_the_swarm_enemies_are_still_below_that_threshold() -> None:
    """The conclusion the paragraph draws, checked against the real intervals.

    The stated threshold being wrong did not change this, which is worth having
    a test say rather than a comment claim: the Imp and the Hellhound are below
    the real 1.371 as well as below the old 2.
    """
    from cataclysm_sim.enemy_abilities import (
        REACTION_ALLOWANCE, SMALLEST_USEFUL_MARKER_METRES, WALK_OUT_SPEED)
    from cataclysm_sim.enemy_stats import ARCHETYPES

    threshold = 2.0 * (REACTION_ALLOWANCE
                       + SMALLEST_USEFUL_MARKER_METRES / WALK_OUT_SPEED)

    for name in ("Imp", "Hellhound"):
        interval = ARCHETYPES[name].attack_interval
        assert interval < threshold, (
            f"The {name} attacks every {interval} s, which is at or above the "
            f"{threshold:.3f} s a marker needs. docs/Cataclysm_GDD_v2.md says "
            f"the swarm enemies cannot telegraph and that a pack therefore "
            f"cannot fill the screen with markers. That is no longer true."
        )


def test_no_comment_states_the_old_attack_interval_as_current() -> None:
    """The two comments issue #397 names, and any that join them.

    HISTORY IS ALLOWED AND IS THE WHOLE DIFFICULTY. The file legitimately
    explains that the interval used to be 2.8 and why it moved, so a search for
    "2.8" would fail on correct text. This looks only for the figure written as a
    present-tense property of the creature.
    """
    if not ABILITIES_PY.is_file():
        pytest.fail("sim/cataclysm_sim/enemy_abilities.py does not exist")

    text = ABILITIES_PY.read_text(encoding="utf-8")

    stale = re.findall(
        r"(?:its|the Brute's)\s+2\.8\s+second\s+attack\s+interval", text)

    assert not stale, (
        f"sim/cataclysm_sim/enemy_abilities.py states a 2.8 second attack "
        f"interval as a current property of the Brute, {len(stale)} time(s). "
        f"ARCHETYPES['Brute'].attack_interval is 1.6. Writing it as history -- "
        f"'the then 2.8 second interval' -- is fine and is deliberately not "
        f"matched here."
    )


def test_the_interval_the_comments_now_quote_is_the_real_one() -> None:
    """And the replacement figures are right, not merely different.

    A comment that swapped 2.8 for some other wrong number would pass the test
    above and say nothing true.
    """
    from cataclysm_sim.enemy_abilities import largest_telegraphed_radius
    from cataclysm_sim.enemy_stats import ARCHETYPES

    text = ABILITIES_PY.read_text(encoding="utf-8")
    interval = ARCHETYPES["Brute"].attack_interval
    allowed = largest_telegraphed_radius(interval)

    quoted = re.findall(
        r"Brute's\s+([\d.]+)\s+second\s+attack\s+interval|"
        r"its\s+([\d.]+)\s+second\s+attack\s+interval", text)
    figures = {float(a or b) for a, b in quoted}

    assert figures, (
        "no comment in sim/cataclysm_sim/enemy_abilities.py quotes the Brute's "
        "attack interval any more. The two that did explain why its ordinary "
        "slam draws no marker, which is not obvious."
    )

    for figure in figures:
        assert figure == pytest.approx(interval), (
            f"a comment quotes a {figure} second attack interval for the Brute "
            f"and ARCHETYPES['Brute'].attack_interval is {interval}."
        )

    # And the marker size those comments now quote alongside it.
    sizes = {float(m) for m in re.findall(
        r"marker of up to ([\d.]+) metres", text)}
    for size in sizes:
        assert size == pytest.approx(allowed, abs=0.01), (
            f"a comment says the Brute's cycle allows a marker of up to {size} "
            f"metres. At a {interval} second interval the formula gives "
            f"{allowed:.2f}."
        )

    assert not math.isclose(allowed, 0.0), (
        "the Brute's cycle allows no marker at all, so the comments explaining "
        "why its slam is the exception no longer describe an exception."
    )
