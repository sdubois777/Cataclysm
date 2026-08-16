"""Stun scales by chance to stun and nothing else. Issue #299.

WHAT WAS DECIDED, 2026-08-16. There is no affix that scales a stun's duration.
The chance to stun added by issue #298 is the only lever, and it covers both of
the things a stun has: chance up to 100%, and duration above that through
`stun_application` in `sim/cataclysm_sim/damage.py`, capped at 3 seconds.

TWO ARGUMENTS, AND THE SECOND IS THE ONE THAT WOULD SURVIVE THE FIRST BEING
WRONG.

    A duration affix would be a second lever on a number that already has one.
    That is the same reasoning that gave Cripple, Weaken, Shred and Madness one
    affix each rather than three, recorded under issue #300.

    A duration affix would have a CLIFF. A stunned target is immune for 5
    seconds, so duration past that is worth exactly nothing -- nothing was going
    to stun it again in that window anyway. A player who rolled past the cliff
    would have wasted the roll with nothing in the interface to say so.

THE GENRE ARGUES THE OTHER WAY AND THE DIFFERENCE IS NAMED. Path of Exile sells
increased stun duration as a common modifier. It can, because only unique bosses
there become immune while stunned and for 4 seconds after; ordinary monsters have
no such window, so there is no cliff to run into. Here the window applies to
everything that can be stunned at all. That difference is the whole argument,
which is why the tests below check the document still states it rather than only
the conclusion it supports.

WHAT IS ASSERTED HERE. That no affix scales stun duration in the model or in the
generated data, that the design document states the decision and both arguments,
and that the cap the decision relies on is still below the immunity window.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
AFFIXES = REPO_ROOT / "game" / "Data" / "Affixes.csv"


def affix_rows() -> list[dict[str, str]]:
    if not AFFIXES.is_file():
        pytest.skip("game/Data/Affixes.csv is not present")
    with AFFIXES.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def design_document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return " ".join(GDD.read_text(encoding="utf-8").split())


# --------------------------------------------------------------------------
# No such affix exists
# --------------------------------------------------------------------------

def test_no_affix_in_the_model_scales_a_stuns_duration() -> None:
    from cataclysm_sim import affixes as af

    named = [a.name for a in af.AFFIX_POOL
             if "stun" in a.name.lower() and "duration" in a.name.lower()]
    assert not named, (
        f"{named} scale a stun's duration. Issue #299 decided on 2026-08-16 that "
        f"chance to stun is the only lever, because it already lengthens the "
        f"stun past 100% and a duration affix would have a cliff at the 5 second "
        f"immunity window.")


def test_no_generated_affix_row_scales_a_stuns_duration() -> None:
    """The model and the workbook are separate copies, so both are checked. The
    workbook is authoritative."""
    named = [row["AffixName"] for row in affix_rows()
             if "stun" in row.get("AffixName", "").lower()
             and "duration" in row.get("AffixName", "").lower()]
    assert not named, (
        f"{named} in game/Data/Affixes.csv scale a stun's duration, which issue "
        f"#299 decided against. If that decision has been reversed, "
        f"docs/DECISIONS.md should say so and this file should go.")


def test_the_one_stun_affix_is_a_chance_to_apply() -> None:
    """Guards the two tests above against passing vacuously. If the chance to
    stun is ever removed, they would both pass over an empty list and say
    nothing."""
    stun_affixes = [row for row in affix_rows()
                    if row.get("Ailment", "").strip().lower() == "stun"]
    assert len(stun_affixes) == 1, (
        f"there are {len(stun_affixes)} affixes applying stun rather than one. "
        f"Issue #298 added exactly one, a chance to apply.")
    assert stun_affixes[0]["AffixKind"] == "Ailment"


# --------------------------------------------------------------------------
# The document states the decision and why
# --------------------------------------------------------------------------

def test_the_document_says_there_is_no_duration_affix() -> None:
    assert "no affix that scales a stun's duration" in design_document(), (
        "the design document no longer states that no affix scales a stun's "
        "duration. That was decided on 2026-08-16 under issue #299, and a "
        "decision nobody can find is one that gets re-asked.")


def test_the_document_states_the_cliff_argument() -> None:
    """The argument that would survive the other one being wrong. If chance ever
    stopped lengthening a stun, this reason to have no duration affix would
    still hold."""
    text = design_document()

    # THE CLAIM, NOT THE WORD. Checking for "cliff" alone passed while the
    # argument had been reworded away, because the word also appears in the
    # sentence about a player rolling past one. Measured on 2026-08-16.
    assert "A duration affix would also have a cliff" in text, (
        "the design document no longer explains that a duration affix would "
        "have a cliff at the immunity window. That is the argument that does "
        "not depend on chance already covering duration, so losing it loses "
        "half the reasoning.")
    assert "worth exactly nothing" in text, (
        "the design document states that a duration affix would have a cliff "
        "without saying what makes it one: duration past the immunity window is "
        "worth nothing, because nothing was going to stun the target again "
        "inside it.")


def test_the_document_names_where_the_genre_disagrees() -> None:
    """The repository's rule is to record what argues against a decision, not
    only what argues for it."""
    text = design_document()
    assert "Path of Exile sells increased stun duration" in text, (
        "the design document no longer records that Path of Exile sells the "
        "affix this decision refuses. Without it the decision reads as though "
        "no shipped game disagreed, which is not true.")
    assert "unique bosses" in text, (
        "the design document states that Path of Exile sells increased stun "
        "duration without stating the difference that lets it: only unique "
        "bosses there have an immunity window, so there is no cliff. That "
        "difference is the whole argument.")


# --------------------------------------------------------------------------
# The decision rests on the cap, so the cap is checked here too
# --------------------------------------------------------------------------

def test_chance_still_reaches_the_duration_cap_and_stops_below_the_window() -> None:
    """The decision assumes two things about `stun_application`: that chance
    lengthens a stun at all, and that it stops short of the immunity window. If
    either stopped being true the decision would need revisiting rather than
    quietly becoming wrong."""
    from cataclysm_sim import damage as dm

    _, at_certainty = dm.stun_application(100.0)
    _, at_double = dm.stun_application(200.0)
    assert at_double > at_certainty, (
        "chance past 100% no longer lengthens a stun, so chance is no longer "
        "the second lever and issue #299 should be reopened.")

    _, at_cap = dm.stun_application(100_000.0)
    assert at_cap == dm.LONGEST_STUN_SECONDS
    assert at_cap < dm.STUN_IMMUNITY_SECONDS, (
        "a stun can now last as long as the immunity window, which means a "
        "target can be held for ever. The cap exists to stop exactly that.")


def test_the_document_states_the_cap_the_decision_relies_on() -> None:
    from cataclysm_sim import damage as dm

    text = design_document()
    stated = re.search(r"cap of ([\d.]+) seconds", text)
    assert stated, (
        "the design document no longer states the cap on a stun's duration in "
        "the crowd control gear paragraph. The decision to have no duration "
        "affix rests on chance reaching that cap on its own.")
    assert float(stated.group(1)) == dm.LONGEST_STUN_SECONDS, (
        f"the design document says the cap is {stated.group(1)} seconds and "
        f"LONGEST_STUN_SECONDS in sim/cataclysm_sim/damage.py is "
        f"{dm.LONGEST_STUN_SECONDS}.")
