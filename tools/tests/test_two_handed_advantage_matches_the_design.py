"""The two two-handed figures in `docs/Cataclysm_GDD_v2.md`, each against the
thing that computes it.

WHY THIS EXISTS. Issue #1381. The design document states a two-handed advantage
in two places, and they are different numbers computed from different things:

    "The Damage Target"                     "about 1.32 times the target"
    "A Two-Handed Weapon Is Worth Double,   "about **1.29 times** the damage per
     Per Implicit and Per Affix"             hit and about **1.22 times** the
                                             damage per second"

**Neither is wrong and they are not in conflict.** 1.32 is the composed damage
per hit divided by the 1,860 damage target, which `player_damage` computes. 1.29
is the ratio of the weapon brackets alone -- a Greatsword's 310 against an Axe
and Sword's 240 -- which `analyse_two_handed_multiplier.py` solves, and 1.22 is
that per-hit ratio carried to a per-second one using the two weapon CLASSES'
average attack rates.

WHAT WENT WRONG WITHOUT THIS FILE. Commit `dbe4ca1` moved both sentences on
2026-08-15, from 1.33 and 1.33/1.26 to 1.32 and 1.29/1.22. Three comments in
`sim/` still said 1.33 three weeks later, and two of them claimed 1.33 was
"exactly" what the two-handed section states -- a claim that was true while both
sentences said 1.33 and false from that commit onward. **Nothing noticed,
because no test compared any model figure to either sentence.** The one test that
came close asserted `approx(1.33, abs=0.05)`, a band spanning 1.28 to 1.38, which
passed for the model's 1.3211 and would have passed for 1.29 too.

HOW IT CHECKS. The figures are **read out of the design document** rather than
written here, so a change to a sentence moves the expected value and the check
still has to hold. Only the sentence's shape is hard-coded, and a sentence that
stops matching fails loudly rather than silently checking nothing.

WHAT THIS DOES NOT CHECK. The per-second pair figure of 1.266, and both
per-second figures being printed at all. `sim/tests/test_analysis_scripts.py`
already owns those under issue #1185, which is a different failure: the script
labelled a class-average figure as if it were the pair's.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import re
import runpy

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SIM = REPO_ROOT / "sim"

#: How close a stated figure has to be to what the model computes. The document
#: quotes two decimal places, so half of the last place is the tightest band
#: that a correctly rounded figure always satisfies.
TOLERANCE = 0.005


@pytest.fixture(scope="module")
def gdd() -> str:
    """The design document, flattened.

    **FLATTENED BECAUSE EVERY FILE IN `docs/` IS HARD-WRAPPED**, so a sentence
    longer than about eighty characters is split across lines and a raw search
    for it reports a clean document that is not clean.
    """
    assert GDD.is_file(), f"{GDD} is missing"
    return re.sub(r"\s+", " ", GDD.read_text(encoding="utf-8"))


def stated(text: str, pattern: str, label: str) -> float:
    """One figure read out of the design document by the sentence around it."""
    found = re.findall(pattern, text)
    assert len(found) == 1, (
        f"expected exactly one sentence in {GDD.name} matching {label}, found "
        f"{len(found)}: {found}. Either the sentence was reworded -- in which "
        f"case update the pattern here -- or the figure is now stated twice, "
        f"which is how issue #1381 happened.")
    return float(found[0])


@pytest.fixture(scope="module")
def two_handed_script() -> dict:
    """`analyse_two_handed_multiplier.py`'s globals, by running it once."""
    import sys
    if str(SIM) not in sys.path:
        sys.path.insert(0, str(SIM))
    with contextlib.redirect_stdout(io.StringIO()):
        return runpy.run_path(str(SIM / "analyse_two_handed_multiplier.py"))


# --------------------------------------------------------------------------
# 1.32 -- the composed damage per hit over the damage target
# --------------------------------------------------------------------------

def test_the_damage_target_section_states_what_player_damage_computes(gdd):
    """"A two-handed weapon deals about 1.32 times the target"."""
    import sys
    if str(SIM) not in sys.path:
        sys.path.insert(0, str(SIM))
    from cataclysm_sim import player_damage as pd

    figure = stated(
        gdd,
        r"A two-handed weapon deals about ([\d.]+) times the target",
        "the two-handed figure in \"The Damage Target\"")
    measured = pd.gap_against_target(8, ("Greatsword",))
    assert measured == pytest.approx(figure, abs=TOLERANCE), (
        f'"The Damage Target" in {GDD.name} states {figure} and '
        f"player_damage.gap_against_target(8, ('Greatsword',)) computes "
        f"{measured:.4f}. One of the two has moved. Do NOT reconcile this "
        f"against the 1.29 in the two-handed section: that is the "
        f"weapon-bracket ratio, a different quantity, and conflating the two "
        f"is issue #1381.")


def test_the_lone_one_hander_figure_in_the_same_sentence_also_holds(gdd):
    """The same sentence states 0.81 for a single one-hander, and a figure
    beside a checked one is exactly where the next stale number hides."""
    import sys
    if str(SIM) not in sys.path:
        sys.path.insert(0, str(SIM))
    from cataclysm_sim import player_damage as pd

    figure = stated(
        gdd,
        r"A single one-handed weapon deals about ([\d.]+) times it",
        "the one-handed figure in \"The Damage Target\"")
    for name in ("Axe", "Sword"):
        measured = pd.gap_against_target(8, name)
        assert measured == pytest.approx(figure, abs=0.05), (
            f"{name} alone reaches {measured:.4f} of the target and the "
            f"document states about {figure}")


# --------------------------------------------------------------------------
# 1.29 and 1.22 -- the weapon-bracket ratio and its per-second form
# --------------------------------------------------------------------------

def test_the_two_handed_section_states_what_the_script_solves(
        gdd, two_handed_script):
    """"a two-handed weapon deals about **1.29 times** the damage per hit"."""
    figure = stated(
        gdd,
        r"a two-handed weapon deals about \*\*([\d.]+) times\*\* the damage per hit",
        "the per-hit figure in \"A Two-Handed Weapon Is Worth Double\"")
    solved = two_handed_script["chosen_per_hit_ratio"]()
    assert solved == pytest.approx(figure, abs=TOLERANCE), (
        f'"A Two-Handed Weapon Is Worth Double" in {GDD.name} states {figure} '
        f"per hit and analyse_two_handed_multiplier.py solves {solved:.4f}.")


def test_the_per_second_figure_is_the_class_average_one(gdd, two_handed_script):
    """"about **1.22 times** the damage per second".

    **THE DOCUMENT QUOTES THE CLASS-AVERAGE FIGURE AND NOT THE PAIR'S**, and
    that is deliberate: the script prints both, and its own note says the class
    figure "is what the multiplier was derived against and is kept so that
    derivation stays checkable". Checking it against the pair's 1.266 would
    fail, which is the point of naming which one this is.
    """
    from cataclysm_sim import affixes as af

    figure = stated(
        gdd,
        r"about \*\*([\d.]+) times\*\* the damage per second",
        "the per-second figure in \"A Two-Handed Weapon Is Worth Double\"")
    ns = two_handed_script
    by_class = (ns["chosen_per_hit_ratio"]()
                * ns["TWO_HANDED_RATE"] / ns["ONE_HANDED_RATE"])
    assert by_class == pytest.approx(figure, abs=TOLERANCE), (
        f"the document states {figure} per second and the class-average "
        f"figure is {by_class:.4f}")

    pair_rate = (sum(af.base_named(n).attack_speed for n in ns["ONE_HANDED"])
                 / len(ns["ONE_HANDED"]))
    for_pair = (ns["chosen_per_hit_ratio"]()
                * af.base_named(ns["TWO_HANDED"]).attack_speed / pair_rate)
    assert not (for_pair == pytest.approx(figure, abs=TOLERANCE)), (
        f"the pair figure {for_pair:.4f} now also matches the document's "
        f"{figure}, so this test can no longer tell which of the two the "
        f"document quotes. Rewrite it rather than leaving a check that cannot "
        f"distinguish them.")


# --------------------------------------------------------------------------
# The two figures are different, and the document no longer says otherwise
# --------------------------------------------------------------------------

def test_the_two_figures_are_different_and_the_document_says_so(gdd):
    """Until issue #1381 "The Damage Target" said its figure "is the two-handed
    advantage Dual Wielding states". That was true while both sentences said
    1.33 and false from commit `dbe4ca1` onward, and it is what sent three
    comments in `sim/` to the wrong number.

    This checks the claim of identity is gone and that the document says the two
    are different, rather than only checking the numbers -- because the numbers
    were already different while the sentence claimed they were the same.
    """
    per_target = stated(
        gdd, r"A two-handed weapon deals about ([\d.]+) times the target",
        "the target figure")
    per_hit = stated(
        gdd,
        r"a two-handed weapon deals about \*\*([\d.]+) times\*\* the damage per hit",
        "the per-hit figure")
    assert per_target != per_hit, (
        "the two sentences now state the same figure. If that is a real "
        "re-derivation rather than an edit to one of them, this file and the "
        "sentence about them being different quantities both need rewriting.")

    assert "which is the two-handed advantage Dual Wielding states" not in gdd, (
        "the claim that the damage-target figure IS the figure the two-handed "
        "section states is back in the design document. It is not: one is "
        "composed damage over the 1,860 target, the other is the ratio of the "
        "weapon brackets alone. Issue #1381.")
    assert "and the two do not disagree" in gdd, (
        "the sentence explaining that the two figures are different quantities "
        "has been removed from \"The Damage Target\". Without it the next "
        "reader reconciles them, which is what issue #1381 was.")


def test_no_source_file_still_quotes_the_pre_2026_08_15_figure():
    """1.33 was the target ratio before issue #633 re-derived the flat damage
    affix. Three comments in `sim/` kept quoting it for three weeks.

    Checked across the simulation package and its tests rather than at the three
    known sites, because the point is to catch a fourth.
    """
    stale = []
    for path in sorted((REPO_ROOT / "sim").rglob("*.py")):
        if "__pycache__" in path.parts:
            continue
        for number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1):
            if re.search(r"1\.33\s*(x|\s+times)", line) and "two-hand" in line.lower():
                stale.append(f"{path.relative_to(REPO_ROOT)}:{number}: {line.strip()}")
    assert not stale, (
        "these lines still quote 1.33 as the two-handed advantage. It has been "
        "1.32 against the damage target and 1.29 for the weapon brackets since "
        "commit dbe4ca1 on 2026-08-15:\n  " + "\n  ".join(stale))
