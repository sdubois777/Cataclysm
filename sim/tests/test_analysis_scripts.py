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
`sim/tests/test_power_threshold.py`. These are scripts with no helpers worth
testing on their own: the whole file is the report. All of them together take
under a tenth of a second, so running them is affordable in a way
`sim/experiments.py` is not.

A FOURTH SCRIPT, `sim/analyse_damage_vs_type.py`, was added for issue #234 and is
covered here too. It is the measurement that issue asked for and could not make:
what four prefix slots of "increased damage against X enemies" are worth against
four of the generic affix. Its two headline findings are checked against
recomputed values, and one of them -- that the crossover measured on time to kill
is LOWER than the ratio of the two affix values, not higher -- is checked by
direction as well as by figure, because that is the claim a future edit is most
likely to get backwards.
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
                                  "analyse_penetration.py",
                                  "analyse_damage_vs_type.py",
                                  "analyse_lethality_modes.py",
                                  "analyse_two_handed_multiplier.py"])
def test_the_script_runs_and_prints_something(name):
    printed, _ = run(name)
    assert len(printed.splitlines()) > 20, printed


def test_every_analysis_script_is_covered_here():
    """The list above is written out, so a new sim/analyse_*.py could be added
    and never run by anything. That is how a script goes stale unnoticed, which
    is the whole reason this file exists."""
    on_disk = {path.name for path in SIM_ROOT.glob("analyse_*.py")}
    listed = set(test_the_script_runs_and_prints_something.pytestmark[0].args[1])
    assert on_disk == listed, (
        f"these analysis scripts are not run by any test: "
        f"{sorted(on_disk - listed)}. Add them to the parametrize list above, "
        f"and check their stated conclusions the way the sections below do.")


# --------------------------------------------------------------------------
# analyse_lethality_modes.py -- issue #289
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def lethality_run():
    """About 4.4 seconds, which is far more than the other four scripts put
    together. It runs 125 campaigns; the others are analytical. Module-scoped so
    it runs once for every test below."""
    return run("analyse_lethality_modes.py")


def test_it_reports_a_row_for_every_mode_and_both_controls(lethality_run):
    _, ns = lethality_run
    assert set(ns["ROWS"]) == {"standard", "hardcore", "heretic",
                               "surges only", "death cost only"}


def test_the_headline_ratio_is_computed_from_its_own_rows(lethality_run):
    """The trap this file exists for: a typed sentence under a computed table.
    Recomputed here from the run's own numbers."""
    printed, ns = lethality_run
    rows = ns["ROWS"]
    ratio = rows["heretic"]["rate"] / rows["standard"]["rate"]
    assert f"at {ratio:.2f}x the Standard rate" in printed


def test_the_control_row_ratio_is_computed_from_its_own_rows(lethality_run):
    """The control is the row that answers issue #289: Heretic's extra dungeons
    with nothing else changed."""
    printed, ns = lethality_run
    rows = ns["ROWS"]
    ratio = rows["surges only"]["rate"] / rows["standard"]["rate"]
    assert f"ON THEIR OWN give {ratio:.2f}x" in printed


def test_the_extra_dungeons_do_not_buy_proportionally_more_points(
        lethality_run):
    """THE FINDING, asserted as a direction rather than a figure.

    Issue #289 asked whether Heretic's 25% extra dungeons over-compensate for
    starting its empire tree from nothing. They do not: the fill rate rises by
    far less than 25%, because more dungeons against an unchanged day budget
    means more of them resolve undefeated, and an undefeated dungeon pays
    nothing. Measured at five sample sizes from 15 to 80 campaigns on
    2026-08-05, the control row ran 1.02x to 1.09x and never approached 1.25x.

    The figure moves with the sample size. The direction is the answer.
    """
    from cataclysm_sim.config import LETHALITY_RULES, LethalityMode

    _, ns = lethality_run
    rows = ns["ROWS"]
    multiplier = LETHALITY_RULES[
        LethalityMode.HERETIC].surge_dungeon_multiplier
    ratio = rows["surges only"]["rate"] / rows["standard"]["rate"]
    assert ratio < multiplier, (
        f"the empire tree fill rate now rises by {ratio:.2f}x when the surge "
        f"dungeon count rises by {multiplier:.2f}x, so extra dungeons DO buy "
        f"proportionally more empire points. That reverses the answer issue "
        f"#289 was given and the paragraph the script prints under its table.")
    assert rows["surges only"]["resolved"] > rows["standard"]["resolved"], (
        "more dungeons no longer means more of them resolving undefeated, "
        "which is the mechanism the script gives for the finding above.")


def test_it_says_which_half_of_the_question_it_cannot_answer(lethality_run):
    """The measurement is one-sided by construction and must say so. Heretic's
    2 city upgrade slots instead of 3 is the effect that would cost Heretic,
    and there is no city upgrade system to reduce."""
    printed, _ = lethality_run
    assert "WHAT THIS DOES NOT ANSWER" in printed
    assert "Issue #318" in printed, (
        "the script no longer names the issue holding the half it cannot "
        "measure. Without it a reader takes the number as the whole answer.")
    assert "UPPER BOUND" in printed, (
        "the script does not say its number is an upper bound. Every effect it "
        "leaves out costs the harder modes, so the true figure is lower, and "
        "that is what makes an incomplete measurement usable.")


# --------------------------------------------------------------------------
# analyse_damage_vs_type.py -- issue #234
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def damage_vs_type_run():
    return run("analyse_damage_vs_type.py")


def expected_brackets(other_slots: int) -> tuple[float, float, float]:
    """(generalist, specialist against its type, specialist against the rest).

    Recomputed here from `affixes` rather than read out of the script, so this
    checks the script rather than agreeing with it.
    """
    from cataclysm_sim import affixes as A

    generic = A.INCREASED_DAMAGE.value_at(7) / 100.0
    specific = A.DAMAGE_VS_TOP_VALUE / 100.0
    shared = 1.0 + other_slots * generic
    slots = 4                       # the comparison issue #234 sets up
    return (shared + slots * generic, shared + slots * specific, shared)


def test_the_swing_at_the_bare_build_is_the_computed_one(damage_vs_type_run):
    """The figure issue #234 quotes, recomputed. It is the four slots against
    nothing else, which is the largest the swing ever gets."""
    printed, _ = damage_vs_type_run
    g, m, _ = expected_brackets(0)
    assert f"The bare case swings {m / g:.2f}x" in printed


def test_the_swing_at_the_reference_build_is_smaller(damage_vs_type_run):
    """The finding issue #234 did not have. Increases are additive, so the same
    four slots matter less once the build carries others. If this ever stops
    being smaller, the additive bracket has become something else."""
    from cataclysm_sim import affixes as A

    printed, _ = damage_vs_type_run
    bare_g, bare_m, _ = expected_brackets(0)
    ref_g, ref_m, _ = expected_brackets(A.REFERENCE_INCREASED_DAMAGE_AFFIXES)
    assert ref_m / ref_g < bare_m / bare_g, (
        "the swing at the reference build is no longer smaller than at the bare "
        "build. That was the whole point of section A.")
    assert f"swings {ref_m / ref_g:.2f}x from the same four slots" in printed


def test_the_crossover_on_damage_is_the_ratio_of_the_two_affixes(
        damage_vs_type_run):
    """Where issue #234's 3.2 comes from. It falls out of the arithmetic and
    does not depend on how many other increases the build has, which is worth
    pinning because it is the figure everyone will reach for."""
    from cataclysm_sim import affixes as A

    printed, ns = damage_vs_type_run
    expected = A.DAMAGE_VS_TOP_VALUE / A.INCREASED_DAMAGE.value_at(7)
    for other in (0, A.REFERENCE_INCREASED_DAMAGE_AFFIXES, 8):
        assert ns["crossover_on_damage"](other) == pytest.approx(expected)
    assert f"not {expected:.2f}." in printed


def test_the_crossover_on_time_is_lower_than_the_crossover_on_damage(
        damage_vs_type_run):
    """THE DIRECTION IS THE FINDING, and it is the opposite of the one the issue
    was filed expecting. Averaging time to kill rather than damage moves the
    crossover DOWN, so the specialist stops paying sooner than the ratio of the
    two affix values suggests. A future edit that flips this sentence back would
    reverse the conclusion, so the direction is asserted, not only the figure."""
    from cataclysm_sim import affixes as A

    printed, ns = damage_vs_type_run
    other = A.REFERENCE_INCREASED_DAMAGE_AFFIXES
    g, m, s = expected_brackets(other)
    expected = (1.0 / m - 1.0 / s) / (1.0 / g - 1.0 / s)

    assert expected < A.DAMAGE_VS_TOP_VALUE / A.INCREASED_DAMAGE.value_at(7), (
        "the crossover on time is no longer below the crossover on damage. "
        "Section C of analyse_damage_vs_type.py says it is lower and explains "
        "why; if that has changed, the explanation has to change with it.")
    assert f"the crossover is {expected:.2f} active" in printed
    assert "It is LOWER, so the specialist stops" in printed


def test_the_crossover_does_not_depend_on_health_or_the_base_bracket(
        damage_vs_type_run):
    """They cancel in the solve, and the docstring says so. Checked because a
    crossover that quietly started depending on enemy health would make every
    figure in section C tier-specific without anything saying so."""
    _, ns = damage_vs_type_run
    one = ns["crossover_cataclysms"](6, base=100.0, health=1000.0)
    other = ns["crossover_cataclysms"](6, base=7.0, health=999_999.0)
    assert one == pytest.approx(other)


def test_it_does_not_claim_to_settle_the_question(damage_vs_type_run):
    """Issue #234 exists because the magnitude is a feel question that needs
    play. This script measures; it must not start recommending, or the issue
    will be closed on the strength of a number that cannot answer it."""
    printed, _ = damage_vs_type_run
    assert "Nothing here says 400% is too high or too low" in printed


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
