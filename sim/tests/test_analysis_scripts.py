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

`sim/analyse_lethality_modes.py` was added for issue #289 and
`sim/analyse_two_handed_multiplier.py` for issue #117. The second had no
conclusion checks at all until issue #319, and by then two of its figures had
already drifted -- the same failure this file was built for, in the one script it
did not cover. Both are documented in the section that checks it. The drift
matters more there than elsewhere: that script's conclusion is what the project
owner's answer on issue #117 was given against.
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
                                  "analyse_two_handed_multiplier.py",
                                  "analyse_weakening_ailments.py"])
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
# analyse_two_handed_multiplier.py -- issues #117 and #319
#
# This script had NO conclusion checks at all until issue #319, and two of its
# stated figures had already drifted by the time they were written:
#
#   "the five two-handed bases average 1.03 times two one-handed ones" was true
#   when written and is now six bases averaging 1.00, because issue #146 gave
#   the Wand and the Staff flat attack damage after this file was written.
#
#   "reaching a damage edge through the affix half alone needs a multiplier near
#   2.75" was never computed from anything. Solved, it is 3.63.
#
# Both are now printed by the script and checked below. This matters more than
# for the other scripts: this one's conclusion fed an answer the project owner
# gave on issue #117, that dual wielding is balanced by two-handed affixes being
# worth more rather than by equalising slot counts.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def two_handed_run():
    return run("analyse_two_handed_multiplier.py")


def test_the_weapon_bases_it_names_are_the_ones_in_the_model(two_handed_run):
    """The three bases the whole report is built on. Read out of the model here
    rather than copied, so a change to a weapon base moves both."""
    printed, ns = two_handed_run
    for name in ns["ONE_HANDED"]:
        assert f"{name:<14} one-handed   {ns['base_damage'](name):>7.1f}" in printed
    assert (f"{ns['TWO_HANDED']:<14} two-handed   "
            f"{ns['base_damage'](ns['TWO_HANDED']):>7.1f}") in printed


def test_two_one_handed_bases_still_beat_the_best_two_handed_one(two_handed_run):
    """THE FINDING THE WHOLE FILE RESTS ON, as a direction rather than a figure.

    If a two-handed base ever exceeds two one-handed ones, the argument for
    applying the multiplier to implicits goes away, because the two-hander no
    longer starts behind.
    """
    _, ns = two_handed_run
    summed = sum(ns["base_damage"](n) for n in ns["ONE_HANDED"])
    assert summed > ns["base_damage"](ns["TWO_HANDED"]), (
        "the two best one-handed bases no longer sum to more than the best "
        "two-handed one. That reverses the premise of this whole report and of "
        "the comment on TWO_HANDED_MULTIPLIER in cataclysm_sim/affixes.py.")


def test_the_two_base_damage_ratios_are_the_computed_ones(two_handed_run):
    printed, ns = two_handed_run
    two = ns["base_damage"](ns["TWO_HANDED"])
    summed = sum(ns["base_damage"](n) for n in ns["ONE_HANDED"])
    assert (f"A two-hander is {two / (summed / 2):.2f}x the average one-hander "
            f"and {two / summed:.2f}x the two together.") in printed


def test_the_fleet_counts_are_read_from_the_model(two_handed_run):
    """The count was written out as "five" and went stale when a sixth
    two-handed base gained attack damage. Both counts are now printed."""
    printed, ns = two_handed_run
    one_handed, two_handed = ns["damage_weapon_bases"]()
    mean_one = sum(v for _, v in one_handed) / len(one_handed)
    mean_two = sum(v for _, v in two_handed) / len(two_handed)
    assert f"{len(one_handed)} one-handed bases, mean {mean_one:>5.1f}" in printed
    assert f"{len(two_handed)} two-handed bases, mean {mean_two:>5.1f}" in printed


def test_the_fleet_ratio_conclusion_is_the_computed_one(two_handed_run):
    """The figure that drifted. 1.03 when written, 1.00 now."""
    printed, ns = two_handed_run
    assert f"THE FLEET RATIO IS {ns['fleet_ratio']():.2f}" in printed


def test_the_fleet_ratio_is_parity_rather_than_an_advantage(two_handed_run):
    """The DIRECTION, which is what the argument uses. A ratio at or below one
    means a two-handed weapon brings no base damage advantage over two
    one-handers, which is why the multiplier has to cover implicits."""
    _, ns = two_handed_run
    ratio = ns["fleet_ratio"]()
    assert ratio < 1.1, (
        f"the mean two-handed base is now {ratio:.2f}x two mean one-handed "
        f"ones, so a two-handed weapon has a real base damage advantage again. "
        f"The case for applying TWO_HANDED_MULTIPLIER to implicits rests on "
        f"there being none. Recheck cataclysm_sim/affixes.py's comment on that "
        f"constant and section VI of the design document.")


def test_the_retired_fleet_figures_are_not_back():
    """Kept out by their exact phrasing. 1.03 and "five" are ordinary values
    that could legitimately return if the base list changes again; what must
    not return is the pair asserted with nothing computing them."""
    text = source("analyse_two_handed_multiplier.py")
    body = text[text.index('"""', 3):]
    assert "five two-handed bases average 1.03" not in body, (
        "the stale fleet-ratio sentence is back in the body of the script. It "
        "belongs only in the docstring's record of what it used to say.")


def test_the_affix_budget_verdicts_match_the_slot_counts(two_handed_run):
    """The budget table is the reason 2.0 was chosen. Every row recomputed."""
    printed, ns = two_handed_run
    from cataclysm_sim import affixes as af
    for multiplier in ns["CANDIDATES"]:
        two_slots = af.AFFIX_SLOTS_PER_PIECE * multiplier
        dual_slots = af.AFFIX_SLOTS_PER_PIECE * 2
        assert (f"{multiplier:>10.2f}  {two_slots:>16.1f}  {dual_slots:>11.0f}"
                in printed)


def test_the_budget_table_covers_the_chosen_multiplier(two_handed_run):
    """The table must contain the row for the value that shipped, or the report
    argues for a number it never shows.

    IT DOES NOT ALSO ASSERT THAT 2.0 EQUALISES THE BUDGET.
    `_check_the_two_loadouts_have_equal_affix_value` in
    `sim/cataclysm_sim/affixes.py` already raises at import time if it does not,
    so a test here would be a second copy of a guard that already fires first --
    proved by breaking TWO_HANDED_MULTIPLIER to 2.5, which raised
    `ValueError: a two-handed weapon is worth 10.0 one-handed affix slots and
    two one-handed weapons are worth 8` before any test in this file ran.
    """
    printed, ns = two_handed_run
    from cataclysm_sim import affixes as af
    assert af.TWO_HANDED_MULTIPLIER in ns["CANDIDATES"], (
        f"the candidate list in analyse_two_handed_multiplier.py does not "
        f"include {af.TWO_HANDED_MULTIPLIER}, the multiplier that shipped, so "
        f"the report has no row for the value it is about.")
    slots = af.AFFIX_SLOTS_PER_PIECE * af.TWO_HANDED_MULTIPLIER
    dual = af.AFFIX_SLOTS_PER_PIECE * 2
    assert (f"{af.TWO_HANDED_MULTIPLIER:>10.2f}  {slots:>16.1f}  "
            f"{dual:>11.0f}  {68 + slots:>9.1f}  {68 + dual:>9.1f}  "
            f"{'equal':<28}") in printed, (
        "the affix budget table's row for the chosen multiplier does not read "
        "'equal'. Section VII of the design document requires the two "
        "loadouts to be worth the same in affixes.")


def test_the_two_crossover_multipliers_are_the_solved_ones(two_handed_run):
    """One per reading of what dual wielding does to base damage. The summed
    reading is the one the project owner settled; the averaged one is printed
    because it is what the current base damages were set against."""
    printed, ns = two_handed_run
    for sum_bases in (False, True):
        crossing = ns["solve_multiplier"](sum_bases)
        assert crossing is not None, "the two loadouts no longer cross at all"
        assert f"EQUAL DAMAGE AT A MULTIPLIER OF {crossing:.3f}" in printed


def test_the_crossover_is_higher_when_the_bases_are_summed(two_handed_run):
    """THE DIRECTION, and the one a future edit is most likely to reverse.
    Summing gives the dual wielder more base damage, so the two-hander needs a
    larger multiplier to catch up. If that ever flips, the paragraph explaining
    why summing is the harder case is wrong."""
    _, ns = two_handed_run
    averaged = ns["solve_multiplier"](False)
    summed = ns["solve_multiplier"](True)
    assert summed > averaged, (
        f"the crossover with bases summed ({summed:.3f}) is no longer above "
        f"the crossover with them averaged ({averaged:.3f}). Summing is "
        f"supposed to be the harder case for the two-hander.")


def test_the_crit_crossovers_are_the_solved_ones(two_handed_run):
    """The dual wielder holds four more suffix slots as well as four more
    prefixes. Leaving them out overstates the two-hander, so these are the
    figures that matter."""
    printed, ns = two_handed_run
    for sum_bases in (False, True):
        crossing = ns["solve_with_crit"](sum_bases)
        assert crossing is not None
        assert f"EQUAL AT A MULTIPLIER OF {crossing:.3f}" in printed


def test_the_crit_factors_are_identical_at_the_chosen_multiplier(two_handed_run):
    """The section headed WHAT A MULTIPLIER OF 2.0 DOES, EXACTLY claims the two
    loadouts' critical strike factors are not merely close but identical, and
    that this is arithmetic rather than luck. Checked, because "identical" is a
    claim that stops being true the moment anything is added to either side."""
    printed, ns = two_handed_run
    from cataclysm_sim import affixes as af
    two = ns["crit_factor"](ns["SUFFIXES_PER_WEAPON"] * 2,
                            af.TWO_HANDED_MULTIPLIER)
    dual = ns["crit_factor"](ns["SUFFIXES_PER_WEAPON"] * 4, 1.0)
    assert two == pytest.approx(dual), (
        f"the critical strike factors are no longer identical: {two} against "
        f"{dual}. The section claiming they are is wrong, and so is the reason "
        f"it gives for TWO_HANDED_MULTIPLIER being exactly 2.0.")
    assert f"critical strike factor, two-hander at {af.TWO_HANDED_MULTIPLIER:.1f} : {two:.6f}" in printed
    assert f"critical strike factor, dual wield        : {dual:.6f}" in printed


def test_the_two_ratios_at_the_chosen_multiplier_are_the_computed_ones(
        two_handed_run):
    """The pair the section headed WHY THE ANSWER IS NOT SIMPLY 2.0 turns on:
    the same multiplier that equalises the affix budget does not equalise
    damage, and which way it lands depends on the base damage reading."""
    printed, ns = two_handed_run
    from cataclysm_sim import affixes as af
    for sum_bases, label in ((True, "summed"), (False, "averaged")):
        ratio = (ns["two_handed_damage"](af.TWO_HANDED_MULTIPLIER)
                 / ns["dual_wield_damage"](sum_bases))
        assert (f"at 2.0 with bases {label:<9}: the two-hander deals "
                f"{ratio:.2f}x") in printed


def test_the_summed_reading_still_leaves_the_two_hander_behind_on_damage(
        two_handed_run):
    """THE FINDING THAT MADE THE IMPLICIT HALF NECESSARY, as a direction.

    Section VI of the design document says a two-handed weapon stays ahead on
    raw damage. With the affix multiplier alone and bases summed it does not,
    and that gap is the entire reason the multiplier was extended to implicits.
    """
    _, ns = two_handed_run
    from cataclysm_sim import affixes as af
    affix_only = ns["two_handed_damage"](af.TWO_HANDED_MULTIPLIER)
    dual = ns["dual_wield_damage"](sum_bases=True)
    assert affix_only < dual, (
        f"with the affix multiplier alone the two-hander now deals "
        f"{affix_only / dual:.3f}x the dual wielder with bases summed, which "
        f"is no longer behind. The case for applying the multiplier to "
        f"implicits as well rests on it being behind.")


def test_the_chosen_design_ratios_are_the_computed_ones(two_handed_run):
    """The answer's own two numbers. Both are quoted in the comment on
    TWO_HANDED_MULTIPLIER in cataclysm_sim/affixes.py, checked below."""
    printed, ns = two_handed_run
    per_hit = ns["chosen_per_hit_ratio"]()
    per_second = per_hit * (ns["TWO_HANDED_RATE"] / ns["ONE_HANDED_RATE"])
    assert f"damage per hit          {per_hit:.3f}x" in printed
    assert f"damage per second       {per_second:.3f}x" in printed


def test_the_chosen_design_puts_the_two_hander_ahead_on_both(two_handed_run):
    """THE DIRECTION the whole decision delivers. Per swing and per second."""
    _, ns = two_handed_run
    per_hit = ns["chosen_per_hit_ratio"]()
    per_second = per_hit * (ns["TWO_HANDED_RATE"] / ns["ONE_HANDED_RATE"])
    assert per_hit > 1.0, (
        f"the chosen design no longer puts the two-hander ahead per swing "
        f"({per_hit:.3f}x). Section VI of the design document says it stays "
        f"ahead on raw damage.")
    assert per_second > 1.0, (
        f"the chosen design no longer puts the two-hander ahead per second "
        f"({per_second:.3f}x).")


def test_the_affix_only_multiplier_is_solved_rather_than_asserted(
        two_handed_run):
    """WHAT THIS REPLACES. The script used to print "needs about 2.75" with
    nothing computing it, and cataclysm_sim/affixes.py repeated the figure as
    the reason the multiplier covers implicits."""
    printed, ns = two_handed_run
    from cataclysm_sim import affixes as af
    solved = ns["solve_affix_only_for_the_same_edge"]()
    assert solved is not None
    assert f"WOULD NEED A MULTIPLIER OF {solved:.2f}" in printed
    slots = af.AFFIX_SLOTS_PER_PIECE * solved
    assert f"{slots:.1f} affix slots-worth" in printed


def test_the_affix_only_route_hands_over_slots_the_dual_wielder_lacks(
        two_handed_run):
    """THE DIRECTION, which is the argument. Reaching the same edge through
    affixes alone must cost more affix slots than the dual wielder holds, or
    section VII's free-power rule would not be broken by it and there would be
    no reason to prefer the implicit route."""
    _, ns = two_handed_run
    from cataclysm_sim import affixes as af
    solved = ns["solve_affix_only_for_the_same_edge"]()
    assert af.AFFIX_SLOTS_PER_PIECE * solved > af.AFFIX_SLOTS_PER_PIECE * 2, (
        "reaching the same damage edge through the affix half alone no longer "
        "costs more affix slots than the dual wielder holds. That is the whole "
        "argument for applying TWO_HANDED_MULTIPLIER to implicits instead.")


def test_the_retired_affix_only_figure_is_not_back():
    """It read "needs about 2.75, which hands the two-hander three affix
    slots". Kept out of both files by the phrasing it appeared in."""
    for text in (source("analyse_two_handed_multiplier.py"),
                 (SIM_ROOT / "cataclysm_sim" / "affixes.py").read_text(
                     encoding="utf-8")):
        assert "affix half alone needs a multiplier near 2.75" not in text
        assert "hands the two-hander three affix slots" not in text


def test_the_affixes_comment_quotes_the_current_figures():
    """The comment on TWO_HANDED_MULTIPLIER in cataclysm_sim/affixes.py is
    where this measurement is read by anyone using the model, and it carried
    two figures this script produces. That is the same failure issue #6 found
    for cataclysm_sim/combat.py."""
    from cataclysm_sim import affixes as af

    _, ns = run("analyse_two_handed_multiplier.py")
    source_text = (SIM_ROOT / "cataclysm_sim" / "affixes.py").read_text(
        encoding="utf-8")
    # Drop the `#:` marker before collapsing whitespace. Without this a `#:`
    # lands in the middle of every sentence that wraps, and nothing in a
    # comment longer than one line can be matched.
    comment = unwrapped("\n".join(
        line.lstrip()[2:] if line.lstrip().startswith("#:") else line
        for line in source_text.splitlines()))

    per_hit = ns["chosen_per_hit_ratio"]()
    per_second = per_hit * (ns["TWO_HANDED_RATE"] / ns["ONE_HANDED_RATE"])
    assert f"deals {per_hit:.2f}x per hit" in comment, (
        f"the comment on TWO_HANDED_MULTIPLIER does not state the current "
        f"per-hit figure of {per_hit:.2f}x. Issue #319.")
    assert f"about {per_second:.2f}x per second" in comment

    solved = ns["solve_affix_only_for_the_same_edge"]()
    slots = af.AFFIX_SLOTS_PER_PIECE * solved
    assert f"needs a multiplier of {solved:.2f}" in comment, (
        f"the comment on TWO_HANDED_MULTIPLIER does not state the current "
        f"affix-only multiplier of {solved:.2f}. Issue #319.")
    assert f"{slots:.1f} affix slots-worth" in comment

    summed = sum(ns["base_damage"](n) for n in ns["ONE_HANDED"])
    assert (f"an Axe and a Sword give {summed:.0f} against a Greatsword's "
            f"{ns['base_damage'](ns['TWO_HANDED']):.0f}") in comment


def test_the_script_docstring_quotes_the_current_fleet_figures():
    """The docstring is the first thing anyone reads and it is where the stale
    1.03 lived for five months."""
    _, ns = run("analyse_two_handed_multiplier.py")
    doc = unwrapped(source("analyse_two_handed_multiplier.py"))
    _, two_handed = ns["damage_weapon_bases"]()
    count = {5: "five", 6: "six", 7: "seven", 8: "eight"}.get(len(two_handed))
    assert count is not None, (
        f"there are now {len(two_handed)} two-handed damage bases and the "
        f"docstring spells the count as a word. Add it to the map above.")
    assert (f"The {count} two-handed bases average "
            f"{ns['fleet_ratio']():.2f} times two one-handed ones") in doc, (
        "the docstring of sim/analyse_two_handed_multiplier.py no longer "
        "states the current base count and fleet ratio. That sentence went "
        "stale once already, which is issue #319.")


def test_the_docstring_records_that_the_figure_drifted():
    """CLAUDE.md: say what did not work. A reader comparing this file with the
    decision on issue #117 should find the drift already written down rather
    than discover it."""
    doc = unwrapped(source("analyse_two_handed_multiplier.py"))
    assert "THAT SENTENCE USED TO READ" in doc
    assert "#319" in doc



# --------------------------------------------------------------------------
# analyse_weakening_ailments.py -- issue #300
#
# The measurement behind the decision that Cripple, Weaken, Shred and Madness
# scale by chance to apply and nothing else. Every figure it prints is read from
# game/Data/StatusEffects.csv, game/Data/Gems.csv or cataclysm_sim.affixes, so
# the checks below recompute from the same places rather than pinning numbers.
#
# THE DIRECTIONS MATTER MORE THAN THE FIGURES HERE. The decision rests on two:
# that the caps are NOT filled by affixes alone, and that they ARE filled with a
# handful of sockets. Either one reversing would change the answer, and neither
# is visible from a figure on its own.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def weakening_run():
    return run("analyse_weakening_ailments.py")


def test_it_reads_the_base_strengths_from_the_data(weakening_run):
    """Not typed in. The four descriptions in game/Data/StatusEffects.csv are
    the only statement of what these effects do."""
    printed, ns = weakening_run
    rows = ns["status_effect_rows"]()
    assert set(rows) == {"Cripple", "Weaken", "Shred", "Madness"}
    for name, description in rows.items():
        base = ns["base_strength"](description)
        shown = "none" if base is None else ns["format_base"](base)
        assert f"{name:<10}{shown:>10}" in printed


def test_it_distinguishes_a_resistance_point_from_a_percentage(weakening_run):
    """Shred takes 10 POINTS off a resistance; Cripple and Weaken take a
    percentage off a speed and a damage number. An earlier version of the
    script printed all three as percentages, which made Shred read as a small
    version of the other two."""
    printed, ns = weakening_run
    rows = ns["status_effect_rows"]()
    assert ns["base_strength"](rows["Shred"])[1] is False, (
        "Shred's reduction now parses as a percentage. It is 10 resistance "
        "points, and the script prints a unit so the two cannot be confused.")
    assert ns["base_strength"](rows["Cripple"])[1] is True
    assert "10pts" in printed


def test_only_the_two_capped_effects_have_a_cap(weakening_run):
    """Cripple and Weaken cap at a percentage. Shred stops when the resistance
    reaches zero, which depends on the enemy, and Madness has no strength at
    all. A magnitude affix would have nothing to push against on either."""
    _, ns = weakening_run
    rows = ns["status_effect_rows"]()
    capped = {name for name, text in rows.items()
              if ns["magnitude_cap"](text) is not None}
    assert capped == {"Cripple", "Weaken"}, (
        f"the effects with a percentage magnitude cap are now {sorted(capped)}. "
        f"Issue #300's answer treats Cripple and Weaken as the two that have "
        f"one and Shred and Madness as the two that do not.")


def test_all_four_roll_their_magnitude_into_duration(weakening_run):
    """The second half of the argument. Without this the chance affix is two
    levers rather than three, and the case for a duration affix comes back."""
    _, ns = weakening_run
    rows = ns["status_effect_rows"]()
    for name, description in rows.items():
        assert ns["rolls_over_into_duration"](description), (
            f"{name} no longer rolls its magnitude into duration. Issue #300 "
            f"decided no duration affix is needed BECAUSE it does. If this is "
            f"deliberate, the paragraphs in docs/Cataclysm_GDD_v2.md and the "
            f"docs/DECISIONS.md entry both need revisiting.")


def test_the_affix_ceiling_is_the_computed_one(weakening_run):
    """Eleven pieces at fifteen per cent. Recomputed from GEAR_SLOTS rather
    than pinned, so adding a ring slot moves both."""
    printed, ns = weakening_run
    from cataclysm_sim import affixes as af
    for name in ("Cripple", "Weaken", "Shred", "Madness"):
        pieces, total = ns["affix_ceiling"](ns["WEAKENING"][name])
        expected = sum(count for slot, count in af.GEAR_SLOTS.items()
                       if slot in ns["WEAKENING"][name].allowed_slots)
        assert pieces == expected
        assert f"{name:<10}{pieces:>8}" in printed
        assert f"{total:>9.0f}%" in printed


def test_the_chance_needed_to_fill_a_cap_is_the_solved_one(weakening_run):
    """267% for Cripple and 400% for Weaken. Derived from the overflow rule --
    magnitude is total chance over 100 -- rather than measured, so this checks
    the printed figure against the formula."""
    printed, ns = weakening_run
    rows = ns["status_effect_rows"]()
    for name in ("Cripple", "Weaken"):
        base, _ = ns["base_strength"](rows[name])
        cap = ns["magnitude_cap"](rows[name])
        needed = ns["chance_needed_to_fill_the_cap"](base, cap)
        assert f"{needed:>14.0f}%" in printed
        assert needed == pytest.approx(100.0 * cap / base)


def test_the_caps_are_not_filled_by_affixes_alone(weakening_run):
    """THE FIRST DIRECTION THE DECISION RESTS ON. If affixes alone filled the
    caps, chance to apply would stop paying long before a build ran out of
    them, and the argument that it keeps scaling would fail."""
    _, ns = weakening_run
    rows = ns["status_effect_rows"]()
    for name in ("Cripple", "Weaken"):
        base, _ = ns["base_strength"](rows[name])
        cap = ns["magnitude_cap"](rows[name])
        needed = ns["chance_needed_to_fill_the_cap"](base, cap)
        _, from_affixes = ns["affix_ceiling"](ns["WEAKENING"][name])
        assert from_affixes < needed, (
            f"{name}'s magnitude cap is now filled by affixes alone "
            f"({from_affixes:.0f}% against {needed:.0f}% needed). Issue #300's "
            f"answer assumes a build has to spend on gems as well, which is "
            f"what makes the chance affix keep paying.")


def test_the_caps_are_filled_with_a_handful_of_sockets(weakening_run):
    """THE SECOND DIRECTION. If the caps needed most of the forty-five sockets,
    reaching them would be a build-defining cost rather than an ordinary one,
    and a magnitude affix would be doing real work."""
    _, ns = weakening_run
    from cataclysm_sim import player_power
    rows = ns["status_effect_rows"]()
    for name in ("Cripple", "Weaken"):
        base, _ = ns["base_strength"](rows[name])
        cap = ns["magnitude_cap"](rows[name])
        needed = ns["chance_needed_to_fill_the_cap"](base, cap)
        _, from_affixes = ns["affix_ceiling"](ns["WEAKENING"][name])
        per_gem = ns["gem_chance"](ns["WEAKENING"][name].gem)
        sockets = (needed - from_affixes) / per_gem
        assert sockets < player_power.TOTAL_SOCKETS / 4, (
            f"{name}'s magnitude cap now needs {sockets:.0f} of "
            f"{player_power.TOTAL_SOCKETS} sockets. Issue #300's answer "
            f"assumes a handful. At this cost a magnitude affix would be "
            f"buying something real.")


def test_it_says_what_the_measurement_does_not_show(weakening_run):
    """CLAUDE.md: say what did not work. The measurement shows a second affix
    would add no lever. It does not show one lever is worth as much as three,
    and the script must not let a reader take it that way."""
    printed, _ = weakening_run
    assert "WHAT THAT DOES NOT SAY" in printed
    assert "It does not say the four are as strong as" in printed, (
        "analyse_weakening_ailments.py no longer says what its measurement "
        "does not cover. Issue #300.")


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
