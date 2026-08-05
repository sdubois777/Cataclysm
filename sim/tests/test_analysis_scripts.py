"""The standalone analysis scripts state conclusions the model still supports.

WHY THIS EXISTS. Issue #6. `sim/analyse_scoring.py`, `sim/analyse_dungeons.py`
and `sim/analyse_penetration.py` each print a table and then a sentence or two
saying what the table means. Those sentences were typed. When issue #2 replaced
the player power anchors in `sim/cataclysm_sim/scoring.py`, every table moved and
every sentence stayed, so all three scripts spent five months printing tables
that disagreed with the prose directly underneath them. Nothing noticed, because
a wrong sentence inside a print statement raises nothing.

It did not stay inside the scripts. Four of the figures were copied out of
`sim/analyse_penetration.py` into the module docstring of
`sim/cataclysm_sim/combat.py`, where they are the stated reason Overwhelm is
rated against tier width instead of a flat point step -- a live design decision
justified by numbers that had gone wrong. `sim/cataclysm_sim/scoring.py`'s own
docstring carried four more.

WHAT THIS FILE CHECKS.

1. All three scripts still run.
2. Every conclusion in them is computed from the model, checked by recomputing
   it here and finding that exact string in what the script printed.
3. The retired figures are absent from the sources, by the exact phrase they
   appeared in.
4. The two module docstrings agree with the model.

WHY IT RUNS THE SCRIPTS RATHER THAN TESTING HELPERS. Testing a helper is not
testing what a report prints -- see the same lesson recorded in
`sim/tests/test_power_threshold.py`. These three are scripts with no helpers to
test: the whole file is the report. All three together take under a tenth of a
second, so running them is affordable in a way `sim/experiments.py` is not.
"""

from __future__ import annotations

import ast
import contextlib
import io
import pathlib
import runpy

import pytest

from cataclysm_sim import combat, scoring

SIM_ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The dungeon `analyse_penetration.py` measures every tier against.
DTYPE, SUBTYPE, BOSS, FLOORS = "Cataclysm", "None", "Cataclysm Boss", 125


def run(name: str) -> tuple[str, dict]:
    """Execute one analysis script; return what it printed and its globals.

    The globals matter: they let a test compare the script's own computed value
    against one derived here, rather than re-implementing the script to check it.
    """
    path = SIM_ROOT / name
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        namespace = runpy.run_path(str(path))
    return out.getvalue(), namespace


def source(name: str) -> str:
    return (SIM_ROOT / name).read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def scoring_run():
    return run("analyse_scoring.py")


@pytest.fixture(scope="module")
def dungeons_run():
    return run("analyse_dungeons.py")


@pytest.fixture(scope="module")
def penetration_run():
    return run("analyse_penetration.py")


# --------------------------------------------------------------------------
# They run at all
# --------------------------------------------------------------------------

@pytest.mark.parametrize("name", ["analyse_scoring.py", "analyse_dungeons.py",
                                  "analyse_penetration.py"])
def test_the_script_runs_and_prints_something(name):
    printed, _ = run(name)
    assert len(printed.splitlines()) > 20, printed


# --------------------------------------------------------------------------
# analyse_scoring.py
# --------------------------------------------------------------------------

def test_the_depth_conclusion_is_the_measured_one(scoring_run):
    """"Depth is length, not difficulty" is the headline finding of the whole
    power model. The size of the effect has to be the current size."""
    printed, ns = scoring_run
    shallow, deep = ns["DEPTHS"][0], ns["DEPTHS"][-1]
    lo = scoring.dungeon_score(ns["EXAMPLE_TIER"], "Basic", "None", shallow)
    hi = scoring.dungeon_score(ns["EXAMPLE_TIER"], "Basic", "None", deep)
    assert (f"{deep / shallow:.1f}x the depth buys {hi / lo - 1:.0%} more "
            f"difficulty") in printed


def test_the_retired_depth_conclusion_is_not_back():
    """It read "7.5x the depth buys 22% more difficulty". Both halves were
    wrong: the table samples 8 to 150 floors, not 20 to 150, and the spread
    against the current anchors is smaller than 22%."""
    text = source("analyse_scoring.py")
    assert "7.5x the depth" not in text
    assert "22% more difficulty" not in text


def test_the_weighted_terms_line_adds_up_the_model_weights(scoring_run):
    """The claim that a Cataclysm Boss is worth more tier widths than a player
    can gain in a tier. It is the sum of three weights the model owns."""
    printed, _ = scoring_run
    total = (scoring.BASELINE_WEIGHT + scoring.TYPE_WEIGHTS["Cataclysm"]
             + scoring.RARITY_WEIGHTS["Cataclysm Boss"])
    assert f"sum to {total:.2f} x tier width" in printed
    assert total > 1.0, (
        "the weighted terms no longer exceed one tier width, so the paragraph "
        "under section B claiming the gap is structural is now false")


def test_the_first_floor_claim_follows_from_the_first_floor_score(scoring_run):
    """Section F says floor 1 enemies score below zero. Written so the sentence
    changes with the number rather than contradicting it."""
    printed, ns = scoring_run
    first = scoring.enemy_scores(ns["EXAMPLE_TIER"], "Basic", "None",
                                 ns["SHALLOW_FLOORS"], 1)["Common"]
    assert f"scores {first}, which is" in printed
    assert ("below zero" in printed) is (first < 0)


def test_the_transcribed_design_document_ranges_match_the_model():
    """Section D compares a hand transcription of the design document's "Power
    Score Ranges by Tier" table against the anchors. Nothing checked the
    transcription itself, so a stale copy would have printed a false "NO".

    Read out of the source with `ast` rather than from the run, so this states
    what is written in the file.
    """
    tree = ast.parse(source("analyse_scoring.py"))
    literal = next(
        ast.literal_eval(node.value) for node in ast.walk(tree)
        if isinstance(node, ast.Assign)
        and any(getattr(t, "id", None) == "GDD" for t in node.targets))
    expected = {t: (scoring.PLAYER_MAX_SCORES[t - 1] + 1 if t > 1 else 0,
                    scoring.PLAYER_MAX_SCORES[t]) for t in range(1, 9)}
    assert literal == expected, (
        "the transcription of the design document's Power Score Ranges table in "
        "sim/analyse_scoring.py no longer matches sim/cataclysm_sim/scoring.py. "
        "Check which one moved. Issue #253.")


# --------------------------------------------------------------------------
# analyse_dungeons.py
# --------------------------------------------------------------------------

def test_the_modifier_pool_conclusion_matches_its_own_table(dungeons_run):
    printed, ns = dungeons_run
    top = ns["TOP_TIER"]
    assert (f"wants {top * 2} modifiers against a pool of "
            f"{len(ns['pool_for'](top))}.") in printed


def test_the_modifier_share_conclusion_matches_its_own_table(dungeons_run):
    """The sentence under section B quotes a percentage the table above it also
    prints. They said different things for five months -- 4% against 2.9%."""
    printed, _ = dungeons_run
    row = next(line for line in printed.splitlines()
               if line.startswith("modifiers (1 rolled)"))
    share = row.split()[-1]
    assert f"({share} of tier width)" in printed


def test_the_retired_modifier_share_is_not_back():
    assert "~4% of tier width" not in source("analyse_dungeons.py")


# --------------------------------------------------------------------------
# analyse_penetration.py
# --------------------------------------------------------------------------

def flat_penetration(tier: int, ns: dict) -> float:
    gap = (scoring.final_boss_score(tier, DTYPE, SUBTYPE, FLOORS, rarity=BOSS)
           - scoring.PLAYER_MAX_SCORES[tier])
    return min(combat.BASE_MITIGATION, (gap / ns["FLAT_PER"]) * ns["FLAT_RATE"])


def test_the_flat_rule_conclusion_is_the_measured_one(penetration_run):
    """The three sentences under section C are the argument the shipped
    mechanic rests on: a flat point step punishes high tiers far harder than low
    ones. The direction still holds; every figure in it changed."""
    printed, ns = penetration_run
    low, high = flat_penetration(1, ns), flat_penetration(8, ns)
    assert f"A maxed T1 player eats {low:.0%} pen" in printed
    assert f"A maxed T8 player eats {high:.0%}" in printed
    assert f"{high / low:.1f} times as much" in printed
    assert high > low, (
        "a flat point step no longer punishes tier 8 harder than tier 1, so the "
        "reason Overwhelm is rated against tier width has gone away")


def test_the_flat_step_share_of_a_tier_is_the_measured_one(penetration_run):
    printed, ns = penetration_run
    step = ns["FLAT_PER"]
    assert (f"is worth {step / scoring.tier_width(1):.0%} of a T1 tier but only "
            f"{step / scoring.tier_width(8):.0%} of a T8 one.") in printed


def test_the_retired_penetration_figures_are_not_back():
    """These four went into `cataclysm_sim/combat.py` as the stated reason for a
    live design decision. Kept out by the exact phrasing they appeared in."""
    text = source("analyse_penetration.py")
    for phrase in ("eats 13% pen", "eats 47%",
                   "worth 17% of a T1 tier", "only 6% of a T8 one",
                   "instead of 13% -> 47%"):
        assert phrase not in text, phrase


def test_it_says_up_front_that_the_proposal_was_rejected(penetration_run):
    """The mechanic this file analyses does not exist. `docs/DECISIONS.md`
    2026-08-03 records that enemies carry no Penetration stat because Overwhelm
    already does the job. A reader arriving at the tables must be told."""
    text = source("analyse_penetration.py")
    head = text[:text.index('"""', 3)]
    assert "REJECTED PROPOSAL" in head
    assert "combat.py" in head
    assert "DECISIONS.md" in head


def test_it_compares_the_proposal_against_the_rule_that_shipped(penetration_run):
    """Section F. Without it the file measures two rules, neither of which is
    the one the game uses."""
    printed, ns = penetration_run
    for tier in range(1, 9):
        boss = scoring.final_boss_score(tier, DTYPE, SUBTYPE, FLOORS,
                                        rarity=BOSS)
        expected = combat.overwhelm(scoring.PLAYER_MAX_SCORES[tier], boss,
                                    scoring.tier_width(tier))
        assert ns["shipped"][tier] == pytest.approx(expected)
    assert f"{combat.OVERWHELM_RATE:.0%} per 1.0x of shortfall" in printed
    assert f"capped at {combat.OVERWHELM_CAP:.0%}" in printed


def test_the_ordinary_content_sentence_reports_what_the_table_shows(
        penetration_run):
    """It said routine content "barely triggers" the rule. Against the current
    anchors a player at 70% of their tier out-powers a mid-floor Basic at every
    tier, so it does not trigger at all."""
    printed, ns = penetration_run
    worst = max(ns["ordinary"].values())
    assert f"Routine content reaches {worst:.0%} penetration at worst" in printed


def test_it_reads_the_mitigation_cap_from_the_model(penetration_run):
    """It carried its own copy of 70%. The resistance cap is one number in one
    place -- see tools/tests/test_the_resistance_cap_is_one_number.py."""
    _, ns = penetration_run
    assert ns["RESIST_CAP"] == combat.BASE_MITIGATION
    assert ns["dmg_multiplier"] is combat.damage_multiplier


# --------------------------------------------------------------------------
# The two module docstrings that quote the same measurements
# --------------------------------------------------------------------------

DOCSTRING_DEPTHS = (20, 50, 100, 150)

#: The rejected proposal's constants, as `analyse_penetration.py` states them.
#: Repeated here because `combat.py`'s docstring quotes figures derived from them
#: and importing the script to fetch them would run the whole report.
PROPOSAL = {"FLAT_PER": 50.0, "FLAT_RATE": 0.02}


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


def test_the_proposal_constants_here_match_the_script():
    """`PROPOSAL` above is a copy. If the script's numbers change, the two
    docstring tests below are measuring something the argument no longer uses."""
    _, ns = run("analyse_penetration.py")
    assert {k: ns[k] for k in PROPOSAL} == PROPOSAL


def test_the_scoring_docstring_quotes_the_current_depth_scores():
    """Its opening paragraph is where "depth is length, not difficulty" is
    stated for anyone reading the model. It quoted 151/159/171/184, which were
    the pre-#2 numbers."""
    doc = unwrapped(scoring.__doc__)
    for floors in DOCSTRING_DEPTHS:
        want = scoring.dungeon_score(1, "Basic", "None", floors)
        assert f"{want} ({floors} floors)" in doc or f"{want} ({floors})" in doc, (
            f"sim/cataclysm_sim/scoring.py's docstring does not state {want} "
            f"for {floors} floors. Recompute the paragraph. Issue #6.")
    lo = scoring.dungeon_score(1, "Basic", "None", DOCSTRING_DEPTHS[0])
    hi = scoring.dungeon_score(1, "Basic", "None", DOCSTRING_DEPTHS[-1])
    assert f"a {hi / lo - 1:.0%} spread" in doc


def test_the_retired_scoring_docstring_figures_are_not_back():
    doc = unwrapped(scoring.__doc__)
    assert "151 (20 floors)" not in doc
    assert "a 22% spread" not in doc


def test_the_combat_docstring_quotes_the_current_flat_step_figures():
    """The four numbers that say why Overwhelm is rated against tier width."""
    doc = unwrapped(combat.__doc__)
    step = PROPOSAL["FLAT_PER"]
    assert (f"worth {step / scoring.tier_width(1):.0%} of a T1 tier "
            f"but only {step / scoring.tier_width(8):.0%} of a T8 "
            f"one") in doc
    assert f"eat {flat_penetration(1, PROPOSAL):.0%} penetration" in doc
    assert f"ate {flat_penetration(8, PROPOSAL):.0%}" in doc


def test_the_retired_combat_docstring_figures_are_not_back():
    """They read 17%, 6%, 13% and 47%. Kept out by their phrasing, because
    those percentages are ordinary numbers that could legitimately return."""
    doc = unwrapped(combat.__doc__)
    for phrase in ("worth 17% of a T1 tier", "only 6% of a T8 one",
                   "eat 13% penetration", "ate 47%"):
        assert phrase not in doc, phrase
