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
import math
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
                                  "analyse_weakening_ailments.py",
                                  "analyse_margin_tolerance.py",
                                  "analyse_per_tier_rarity.py",
                                  "analyse_experience_curve.py",
                                  "analyse_affix_spread.py",
                                  "analyse_affix_floors.py",
                                  "analyse_siege_dose.py",
                                  "analyse_quest_move_chance.py",
                                  "analyse_explorer_shape.py",
                                  "analyse_surge_cadence.py"])
def test_the_script_runs_and_prints_something(name):
    printed, _ = run(name)
    assert len(printed.splitlines()) > 20, printed


@pytest.fixture(scope="module")
def affix_floor_run():
    return run("analyse_affix_floors.py")


def test_squeezing_the_tier_ladder_breaks_the_guard_past_an_eighth(
        affix_floor_run):
    """The measurement that decided the shape of issue #1230.

    Raising the floor by squeezing the tier ladder is the obvious reading of
    "run the ladder between the floor and the top", and it fails: a floor of a
    sixth of the top needs a tier ladder spanning 1.80, whose adjacent-tier
    ratio of 1.1029 lets a perfect roll two tiers down beat the worst roll here.
    ``Cataclysm.Item.BandsOverlapByExactlyOneTier`` forbids that.

    A FLOOR OF AN EIGHTH IS THE LAST ONE THAT SURVIVES that route, which is
    barely better than the tenth every affix already had. That is why the floor
    is added and the ladder runs across what is left instead.
    """
    _, ns = affix_floor_run
    squeezed = ns["squeezed_tier_ladder"]
    ratio = ns["adjacent_ratio"]
    bound = ns["TWO_TIER_BOUND"]

    assert ratio(squeezed(1.0 / 8)) ** 2 > bound, (
        "a floor of an eighth of the top no longer survives squeezing the tier "
        "ladder. One of the three ladder constants moved.")
    assert ratio(squeezed(1.0 / 6)) ** 2 < bound, (
        "a floor of a sixth of the top now survives squeezing the tier ladder, "
        "which it did not on 2026-09-04. If that is deliberate, the shape "
        "chosen for #1230 may no longer be the only one that works.")


def test_the_shape_chosen_keeps_the_guard_at_every_floor(affix_floor_run):
    """Adding the floor and rescaling is monotone, so it cannot break the guard.

    Asserted at floors far past the point the squeezing route fails, because a
    check that only tried the floors actually in use would not notice the
    property being lost.
    """
    _, ns = affix_floor_run
    holds = ns["guard_holds"]

    for divisor in (10, 6, 5, 4, 3):
        assert holds(100.0, 100.0 / divisor), (
            f"a floor of a {divisor}th of the top now breaks "
            "Cataclysm.Item.BandsOverlapByExactlyOneTier. The map from the "
            "unfloored ladder to the floored one has stopped being monotone.")


def test_every_stated_floor_is_the_worst_roll(affix_floor_run):
    """What the script reports, checked against the affixes it read."""
    from cataclysm_sim import affixes as af

    _, ns = affix_floor_run
    floored = ns["floored_affixes"]()
    # 19 AND NOT THE 20 APPROVED ON 2026-09-04. The flat energy shield
    # regeneration affix gave its floor back with issue #1237: it has to
    # scale exactly as the flat maximum energy shield affix does, and that
    # one states no floor, so a floor on this one alone made the pair
    # refill in two seconds at the bottom of the ladder and five at the top.
    assert len(floored) == 19, (
        f"{len(floored)} affixes state a floor, not 19. If one was added "
        "or removed, say so on #1230.")

    for name, top, floor, _slots in floored:
        worst = af.affix_value(top, af.AFFIX_TIERS[0], 0.0, 0, floor)
        assert worst == pytest.approx(floor), name


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
    # THE SHARE, NOT THE COUNT, and the distinction is load-bearing. This
    # asserted the two absolute counts until issue #1329 added Siege damage to
    # the model. More dungeons then meant more Sieges, the empire fell sooner,
    # and the shorter campaign had FEWER dungeons resolve in total -- 124
    # against 129 -- while a LARGER share of them did, 86% against 80%. The
    # script's sentence claims a share and was printing counts, so it was the
    # evidence that was wrong rather than the claim.
    assert (rows["surges only"]["share_undefeated"]
            > rows["standard"]["share_undefeated"]), (
        "more dungeons no longer means a larger SHARE of them resolving "
        "undefeated, which is the mechanism the script gives for the finding "
        "above.")


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


def test_the_two_rate_constants_really_are_the_class_averages():
    """The invariant `cataclysm_sim/affixes.py` claims, which nothing checked.

    The comment on `ItemBase.attack_speed` says the per-weapon rates "average to
    ONE_HANDED_RATE and TWO_HANDED_RATE in
    sim/analyse_two_handed_multiplier.py, which is what the two-handed
    multiplier of 2.0 was derived against. Changing one without the other moves
    a multiplier that is already shipped."

    **That was true and nothing enforced it.** Editing any single weapon's
    attack speed in the design workbook would silently make the two constants
    stop describing the fleet, and the multiplier the whole dual-wielding
    decision of 2026-08-03 rests on would go on being quoted against rates no
    weapon has. Issue #1185.
    """
    _, ns = run("analyse_two_handed_multiplier.py")
    from cataclysm_sim import affixes as af

    one_handed, two_handed = [], []
    for base in af.WEAPON_BASES:
        if base.attack_speed <= 0.0:
            continue
        (two_handed if base.value_multiplier != 1.0
         else one_handed).append(base.attack_speed)

    assert one_handed and two_handed, (
        "no weapon bases carry an attack speed, so this check read nothing")

    for label, speeds, constant in (
            ("one-handed", one_handed, ns["ONE_HANDED_RATE"]),
            ("two-handed", two_handed, ns["TWO_HANDED_RATE"])):
        mean = sum(speeds) / len(speeds)
        assert abs(mean - constant) < 0.005, (
            f"{label} weapons now average {mean:.4f} attacks a second across "
            f"{len(speeds)} bases, and analyse_two_handed_multiplier.py still "
            f"says {constant}. The two-handed multiplier of 2.0 was derived "
            f"against these rates, so changing one without the other moves a "
            f"multiplier that is already shipped. Either put the weapon speed "
            f"back or re-derive the multiplier.")


def test_the_per_second_figure_is_given_for_the_pair_and_not_only_the_class(
        two_handed_run):
    """Both per-second figures are printed, and the pair's is the larger.

    WHAT WENT WRONG. The script computed its per-hit ratio from the specific
    Greatsword against the specific Axe and Sword, then divided it by the CLASS
    AVERAGE rates to reach a per-second figure -- and the Axe and Sword average
    1.275 a second, not the one-handed class average of 1.35. So it printed a
    margin of 1.225x on a line naming the pair, while a player holding that pair
    experiences more. Issue #1185.

    Neither figure is wrong on its own terms, so both are printed now and this
    checks that both still are. An edit that quietly drops the pair line puts
    the misleading label back.
    """
    printed, ns = two_handed_run
    from cataclysm_sim import affixes as af

    pair_rate = (sum(af.base_named(n).attack_speed for n in ns["ONE_HANDED"])
                 / len(ns["ONE_HANDED"]))
    per_hit = ns["chosen_per_hit_ratio"]()
    by_class = per_hit * (ns["TWO_HANDED_RATE"] / ns["ONE_HANDED_RATE"])
    for_pair = per_hit * (af.base_named(ns["TWO_HANDED"]).attack_speed
                          / pair_rate)

    assert f"{by_class:.3f}x  comparing CLASS AVERAGES" in printed
    assert f"{for_pair:.3f}x  for THIS PAIR" in printed

    assert for_pair > by_class, (
        f"the pair figure {for_pair:.3f}x is no longer larger than the class "
        f"figure {by_class:.3f}x. That is the whole reason both are printed: "
        f"the Axe and Sword are slower than the one-handed class average, so "
        f"the two-hander is further ahead against them than against the class. "
        f"If weapon speeds changed so this is no longer true, the wording "
        f"around these two lines needs rewriting rather than this check "
        f"relaxing.")


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
# analyse_margin_tolerance.py -- issue #328
#
# The measurement that chose how much to smooth a cell's observed win and loss
# rates before its variance is estimated. Section 7 of sim/experiments.py used
# to group empire tree presets with one worst-case tolerance for the whole
# table; it now computes one per pair, and that cannot be done from raw observed
# rates because a cell that won nothing gets a variance of exactly zero.
#
# THE SCRIPT IS A STATISTICS MEASUREMENT, NOT A GAME ONE. It imports nothing
# from cataclysm_sim. Every figure is an exact enumeration over the trinomial
# outcome space, so there is no sample size to argue about and no random seed:
# the same numbers come back every run, which is what makes pinning them here
# reasonable.
#
# THE DIRECTIONS MATTER MORE THAN THE FIGURES. Three of them: that no smoothing
# fails badly, that the chosen constant is above the floor rather than on it,
# and that the pair issue #328 was opened about is STILL reported tied. The
# third is the opposite of what the issue expected and is the thing a future
# reader is most likely to get backwards.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def margin_tolerance_run():
    """About 0.7 seconds. Exact enumeration over a seven-point grid."""
    return run("analyse_margin_tolerance.py")


def test_no_smoothing_separates_identical_cells_far_too_often(
        margin_tolerance_run):
    """THE FIRST DIRECTION. Two cells drawn from the SAME probabilities should
    be called different about 31.7% of the time by a one-standard-error test.
    Unsmoothed, at the rates the high difficulty tiers produce, it is about
    half the time. If this ever falls to the target the smoothing is no longer
    needed and the constant should go."""
    _, ns = margin_tolerance_run
    worst = max(ns["false_separation"](ns["TRIALS"], win, loss, 0.0)[0]
                for win, loss in ns["GRID"])
    assert worst > ns["TARGET_PERCENT"] + 10.0, (
        f"the unsmoothed estimator's worst false-separation rate over the "
        f"grid is now {worst:.1f}% against a target of "
        f"{ns['TARGET_PERCENT']:.1f}%. Issue #328 exists because it was about "
        f"51%. If that has gone away, MARGIN_SMOOTHING is solving a problem "
        f"that no longer exists.")


def test_the_chosen_smoothing_is_above_the_floor_rather_than_on_it(
        margin_tolerance_run):
    """THE SECOND DIRECTION, and the reason the constant is not the smallest
    one that works. The floor is measured over a finite grid, and the candidate
    one step below it already fails, so sitting on the floor would be one grid
    point from the failure the smoothing exists to prevent."""
    from experiments import MARGIN_SMOOTHING

    _, ns = margin_tolerance_run
    holds = [s for s in ns["CANDIDATES"]
             if ns["worst_over_the_grid"](s)[0]
             <= ns["TARGET_PERCENT"] + 1.5]
    assert 0.0 not in holds, "no smoothing must not be reported as holding"
    assert MARGIN_SMOOTHING in holds
    assert MARGIN_SMOOTHING > min(holds), (
        f"MARGIN_SMOOTHING is {MARGIN_SMOOTHING}, which is the smallest value "
        f"that holds on this grid. Section B of the script argues for a value "
        f"above the floor, not on it. Either raise the constant or rewrite "
        f"that argument.")


def test_the_floor_and_the_failing_candidate_are_both_printed(
        margin_tolerance_run):
    """A table that showed every candidate holding would argue nothing. The
    grid includes the point that catches 0.0625 for exactly this reason."""
    printed, ns = margin_tolerance_run
    holds = [s for s in ns["CANDIDATES"]
             if ns["worst_over_the_grid"](s)[0] <= ns["TARGET_PERCENT"] + 1.5]
    fails = [s for s in ns["CANDIDATES"] if s not in holds]
    assert len(fails) >= 2, (
        f"only {fails} fail the calibration on this grid. The table needs a "
        f"failing candidate ABOVE zero, or 'the floor is not zero' is the only "
        f"thing it shows. Add a (win, loss) point that catches one.")
    assert f"THE SMALLEST CANDIDATE THAT HOLDS IS {min(holds):g}" in printed


def test_the_pair_the_issue_was_opened_about_is_still_tied(
        margin_tolerance_run):
    """THE THIRD DIRECTION, and the finding that contradicts the issue.

    Issue #328 quoted that pair as 3.3 points apart needing 2.3. The 2.3 is the
    unsmoothed figure the issue itself rules out. Smoothed it is 4.28, so the
    change does NOT separate the pair it was opened for, and the script says so
    in its own heading.
    """
    from experiments import MARGIN_SMOOTHING, margin_noise_between

    printed, ns = margin_tolerance_run
    a, b = ns["TIER_67_PAIR"]
    smoothed = margin_noise_between(*a, *b, ns["TRIALS"])
    assert ns["TIER_67_GAP"] <= smoothed, (
        f"the tier 6 and 7 pair now separates at smoothing "
        f"{MARGIN_SMOOTHING}: a gap of {ns['TIER_67_GAP']:.1f} against "
        f"{smoothed:.2f}. Section D of the script says it does not.")
    assert "IS STILL REPORTED TIED" in printed


def test_it_recovers_the_issues_own_unsmoothed_figure(margin_tolerance_run):
    """Proof that the transcribed rates are the ones the issue was measured on,
    rather than a pair that merely looks similar. Unsmoothed they give 2.31,
    which is the 2.3 the issue quotes."""
    from experiments import margin_variance

    _, ns = margin_tolerance_run
    a, b = ns["TIER_67_PAIR"]
    unsmoothed = math.sqrt(
        (margin_variance(*a) + margin_variance(*b)) / ns["TRIALS"]) * 100.0
    assert unsmoothed == pytest.approx(2.3, abs=0.05)


def test_the_enumeration_keeps_effectively_all_the_probability(
        margin_tolerance_run):
    """Rare outcomes are dropped before the pairwise sum. If a real tail were
    being dropped, every percentage in section A would be measuring a
    truncated distribution and nothing would say so."""
    _, ns = margin_tolerance_run
    for win, loss in ns["GRID"]:
        _, mass, _ = ns["false_separation"](ns["TRIALS"], win, loss, 0.5)
        assert mass > 0.99999, (
            f"the enumeration at win={win}, loss={loss} keeps only {mass:.6f} "
            f"of the probability. Lower NEGLIGIBLE in "
            f"sim/analyse_margin_tolerance.py.")


def test_it_says_the_grouping_is_not_a_refinement(margin_tolerance_run):
    """CLAUDE.md: say what did not work. Issue #328 assumed a tighter tolerance
    could only split groups. It cannot only split them, and the script must say
    so or the assumption survives."""
    printed, _ = margin_tolerance_run
    assert "WHAT THIS DOES NOT SHOW" in printed
    assert ("is a refinement of the cap's grouping. It is not one."
            in unwrapped(printed))


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


# --------------------------------------------------------------------------
# analyse_experience_curve.py -- issue #50
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def experience_run():
    return run("analyse_experience_curve.py")


def test_a_dungeon_is_summed_floor_by_floor(experience_run):
    """THE CORRECTION THIS SCRIPT EXISTS TO MAKE, checked in both directions.

    The estimate it replaces valued every floor at the rate the LAST floor pays.
    Enemy Score carries a `currentFloor / totalFloors` term, so that is wrong by
    a factor that is largest at difficulty tier 1, where the tier is narrow and
    the depth term dominates. Recomputed here from the script's own two
    functions rather than from its printed table.
    """
    _, ns = experience_run
    weights, population = ns["WEIGHTS"], ns["POPULATION"]
    floors = ns["WHOLE_FLOORS"]

    summed = ns["dungeon_experience"](1, floors, population, weights)
    flat = ns["dungeon_experience_flat"](1, floors, population, weights)
    assert flat > summed * 1.9, (
        f"valuing every floor at the last floor's rate now gives "
        f"{flat / summed:.2f}x the floor-by-floor sum at tier 1, not the ~2x "
        f"the script's docstring reports. The correction it was written to make "
        f"has changed size.")

    # At tier 8 the same error is small, which is why it went unnoticed: the
    # figures that were checked were the deep ones.
    summed_8 = ns["dungeon_experience"](8, floors, population, weights)
    flat_8 = ns["dungeon_experience_flat"](8, floors, population, weights)
    assert flat_8 < summed_8 * 1.2


def test_the_difficulty_gap_is_stated_for_a_whole_dungeon_and_for_one_floor(
        experience_run):
    """Both ratios are true and they differ by nearly two times, so the script
    has to print which is which. 15.5x is the last floor; 27.9x is a dungeon."""
    printed, ns = experience_run
    per_dungeon = ns["PER_DUNGEON"]
    floors, weights = ns["WHOLE_FLOORS"], ns["WEIGHTS"]

    whole = per_dungeon[8] / per_dungeon[1]
    last = (ns["creature_experience"](8, floors, floors, weights)
            / ns["creature_experience"](1, floors, floors, weights))
    assert whole > last, (
        "a whole dungeon no longer spreads the tiers further apart than its "
        "last floor does, which is the reason the earlier 15.5x understated it")
    assert f"tier 8 pays {whole:.1f} times" in printed
    assert f"last floor alone it is {last:.1f} times" in printed


def test_the_module_docstring_quotes_the_ratios_it_computes(experience_run):
    """Issue #6 again: a typed sentence above a computed table goes stale."""
    _, ns = experience_run
    per_dungeon, weights, floors = ns["PER_DUNGEON"], ns["WEIGHTS"], ns["WHOLE_FLOORS"]
    doc = unwrapped(ns["__doc__"])

    whole = per_dungeon[8] / per_dungeon[1]
    last = (ns["creature_experience"](8, floors, floors, weights)
            / ns["creature_experience"](1, floors, floors, weights))
    assert f"{whole:.1f} times over a whole dungeon" in doc
    assert f"not the {last:.1f} times measured on the last floor" in doc


def test_the_size_of_the_climb_is_derived_and_not_chosen(experience_run):
    """THE ANSWER TO THE QUESTION THE SCRIPT WAS ASKED.

    How many dungeons reaching level 100 costs stopped being a matter of taste
    once two facts were put together: `docs/Cataclysm_GDD_v2.md` says "A run is
    played at a fixed tier", and the balance sweep says a campaign is about 26
    dungeons. Eight tiers is eight campaigns. If either input moves, the size
    moves with it, and this is where that gets noticed.
    """
    printed, ns = experience_run
    assert ns["CLIMB_DUNGEONS"] == ns["CAMPAIGN_DUNGEONS"] * len(ns["TIERS"])
    assert f"{len(ns['TIERS'])} difficulty tiers x {ns['CAMPAIGN_DUNGEONS']} dungeons "\
           f"a campaign = {ns['CLIMB_DUNGEONS']} dungeons" in printed

    played = ns["hours"](ns["CLIMB_DUNGEONS"], ns["WHOLE_FLOORS"])
    assert f"= {played:,.0f} hours" in printed
    assert played > 300, (
        f"the whole climb is now {played:,.0f} hours, which is inside the range "
        f"shipped games in the genre report. It was longer than all three, and "
        f"the script says so in a sentence that would now be wrong.")


def test_the_design_source_for_a_run_being_fixed_to_one_tier_is_quoted(
        experience_run):
    """The size rests entirely on this sentence, so it is quoted rather than
    paraphrased, and it has to still be in the design document."""
    printed, _ = experience_run
    claim = "A run is played at a fixed tier."
    assert claim in printed

    design = (SIM_ROOT.parent / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    assert claim in design, (
        "docs/Cataclysm_GDD_v2.md no longer says a run is played at a fixed "
        "tier. That sentence is the whole reason the climb is eight campaigns "
        "rather than a number somebody picked.")


def test_no_rate_gives_both_a_quick_opening_and_a_level_that_tracks_the_tier(
        experience_run):
    """THE FINDING, asserted as the trade-off it is rather than as one number.

    A rate flat enough to keep the character's level in step with the difficulty
    tier makes the first level cost tens of floors. A rate steep enough to open
    quickly leaves the character far ahead of the tier. Both ends are checked,
    because the recommendation is only justified if the middle really is empty.
    """
    _, ns = experience_run
    per_dungeon, population, weights = ns["PER_DUNGEON"], ns["POPULATION"], ns["WEIGHTS"]
    floors = ns["WHOLE_FLOORS"]
    expected_first = ns["reference_levels_at_tier_ends"]()[0]

    def opening_and_first_tier(rate):
        scale = ns["scale_for_level_100_at_the_end"](rate, per_dungeon)
        return (ns["first_level_in_floors"](rate, scale, population, weights, floors),
                ns["levels_at_tier_ends"](rate, per_dungeon)[0])

    # Flat enough to track the tier: the opening is measured in tens of floors.
    slow_opening, slow_level = opening_and_first_tier(0.03)
    assert slow_level <= expected_first
    assert slow_opening > 20, (
        f"a rate slow enough to leave the character at level {slow_level} after "
        f"the first campaign now reaches level 2 in {slow_opening:.1f} floors. "
        f"If that is genuinely quick, the trade-off this script reports has "
        f"gone away and the recommendation should be revisited.")

    # The recommendation: quick opening, character ahead of the tier.
    quick_opening, quick_level = opening_and_first_tier(ns["POE_RATE"])
    assert quick_opening < 6, (
        f"the recommended rate now reaches level 2 in {quick_opening:.1f} "
        f"floors. A quick opening is the only reason it was preferred.")
    assert quick_level > expected_first * 2, (
        "the recommended rate no longer leaves the character well ahead of the "
        "difficulty tier, so the consequence the script warns about is stale")


def test_it_says_what_the_recommendation_breaks_elsewhere(experience_run):
    """A recommendation that silently invalidates `reference_character` and
    makes the early tiers easy is not a finished answer. Both are named, and
    the Power Score arithmetic behind the second is recomputed here."""
    printed, ns = experience_run
    from cataclysm_sim import scoring as sc

    assert "player_power.reference_character's rule" in printed
    assert "Level Weight" in printed

    levels = ns["levels_at_tier_ends"](ns["POE_RATE"], ns["PER_DUNGEON"])
    from_level = ns["LEVEL_WEIGHT"] * levels[0]
    share = from_level / sc.PLAYER_MAX_SCORES[1]
    assert f"level alone would be {share:.0%} of it" in printed
    assert share > 0.5, (
        f"level is now only {share:.0%} of the tier 1 power ceiling, so the "
        f"warning that the early tiers get easier no longer holds")

    # AND THE PART THAT MAKES IT LIVEABLE, which is worth as much as the
    # warning: the lead shrinks every tier, because level stops at 100 while
    # the tier being entered keeps rising. If it ever stopped shrinking, the
    # early-tier problem would be an every-tier problem.
    shares = [ns["LEVEL_WEIGHT"] * levels[tier - 2] / sc.PLAYER_MAX_SCORES[tier - 1]
              for tier in range(2, 9)]
    assert shares == sorted(shares, reverse=True), (
        f"the share of a tier's starting power that the character carries in "
        f"from levels alone no longer falls at every tier: {shares}")
    assert shares[-1] < 0.2, (
        f"entering difficulty tier 8 the character now carries {shares[-1]:.0%} "
        f"of the tier's starting power from levels alone, so out-levelling is "
        f"no longer confined to the early tiers")


def test_the_reference_progression_comes_from_player_power(experience_run):
    """`reference_levels_at_tier_ends` claims to be the rule
    `player_power.reference_character` documents. The comparison is against the
    UNROUNDED rule, because four of the eight boundaries fall on a half level:
    level 37.5 is the top of tier 3 and `reference_character` reports 38."""
    from cataclysm_sim import player_power

    _, ns = experience_run

    stated = ns["reference_levels_at_tier_ends"]()
    assert stated[-1] == ns["MAX_LEVEL"], "the last tier must end at the maximum level"
    for tier, boundary in zip(ns["TIERS"], stated, strict=True):
        rounded = player_power.reference_character(tier).level
        assert abs(rounded - boundary) <= 0.5, (
            f"player_power says a character is level {rounded} at the end of "
            f"difficulty tier {tier}. The rule it documents, level rising "
            f"evenly to 100 at the end of tier 8, puts that at {boundary}. The "
            f"two have drifted apart by more than integer rounding.")


def test_it_says_the_full_clear_assumption_out_loud(experience_run):
    """Every dungeon count assumes the player kills the whole floor, and nobody
    has played a dungeon to find out. An unstated assumption here reads as a
    measurement."""
    printed, ns = experience_run
    doc = unwrapped(ns["__doc__"])
    assert "FULL-CLEAR" in doc
    assert "Issue #925" in doc, (
        "the docstring no longer names the issue tracking the unmeasured clear "
        "share, so a reader takes the dungeon counts as measured")
    assert "HOW MUCH THE UNMEASURED INPUTS MOVE THE ANSWER" in printed
    assert "50% of the floor" in printed, (
        "the sensitivity table no longer shows a half-cleared floor, which is "
        "the case that doubles every hours figure the script prints")


def test_it_reports_hours_at_the_owners_two_minutes_a_floor(experience_run):
    """The project owner asked for dungeons rather than floors, and the genre
    research is all in hours. Both have to be present or the numbers cannot be
    compared with anything shipped. Two minutes is their figure for an endgame
    build, and it is the SHORT end of what the design document states, so the
    script must not present it as a range."""
    printed, ns = experience_run
    assert ns["MINUTES_PER_FLOOR"] == 2.0
    assert f"is {ns['hours'](1, ns['WHOLE_FLOORS']):.1f} hours of play" in printed
    assert "with an endgame build" in printed
    for shipped in ("Last Epoch 60", "Diablo IV about 150", "Path of Exile 150 to 300"):
        assert shipped in printed, shipped


def test_the_recommended_rate_is_fitted_to_path_of_exile_and_not_chosen(
        experience_run):
    """One checkpoint of Path of Exile's published table is fitted and the other
    two then agree without being fitted. That is what makes the rate evidence
    rather than taste, so it is asserted rather than described."""
    _, ns = experience_run
    rate = ns["POE_RATE"]
    whole = ns["total_experience"](rate, 1.0)

    by_90 = ns["total_experience"](rate, 1.0, 90) / whole
    assert abs(by_90 - ns["POE_SHARE_BY_90"]) < 1e-4, "the fitted checkpoint"

    by_50 = ns["total_experience"](rate, 1.0, 50) / whole
    last = ns["level_cost"](ns["MAX_LEVEL"], rate, 1.0) / whole
    assert abs(by_50 - ns["POE_SHARE_BY_50"]) < 0.01, (
        f"the unfitted level 50 checkpoint is now {by_50:.2%} against Path of "
        f"Exile's {ns['POE_SHARE_BY_50']:.2%}. It agreeing was the corroboration; "
        f"without it the rate is just a number that was picked.")
    assert abs(last - ns["POE_SHARE_LAST_LEVEL"]) < 0.01, (
        f"the unfitted last-level checkpoint is now {last:.2%} against Path of "
        f"Exile's {ns['POE_SHARE_LAST_LEVEL']:.2%}")


def test_level_100_lands_exactly_at_the_end_of_the_last_campaign(experience_run):
    """The size is chosen to put level 100 at the end of tier 8, so the model
    that spends the experience has to agree with the model that sized it. These
    are two different code paths and a sign error in either would not show up
    anywhere else."""
    _, ns = experience_run
    levels = ns["levels_at_tier_ends"](ns["POE_RATE"], ns["PER_DUNGEON"])
    assert levels[-1] == ns["MAX_LEVEL"]
    assert levels[-2] < ns["MAX_LEVEL"], (
        "the character now reaches the maximum level before the last campaign, "
        "so the climb is shorter than the eight tiers it was sized for")
    assert levels == sorted(levels), "the level must not go down as tiers pass"


def test_the_decided_rate_is_the_fitted_one_rounded(experience_run):
    """THE DECISION, checked against the measurement it was rounded from.

    The project owner chose 8.2% on 2026-08-24. That is `POE_RATE` rounded, and
    rounding is only safe if it changes nothing that mattered: the Path of Exile
    checkpoints and the level at the end of every tier's campaign. Both are
    asserted, so a future edit to either number has to face them.
    """
    _, ns = experience_run
    rate, fitted = ns["DECIDED_RATE"], ns["POE_RATE"]
    assert abs(rate - fitted) < 0.0005, (
        f"the decided rate {rate:.4%} is no longer the fitted {fitted:.4%} "
        f"rounded, so it is a number somebody picked")

    for top, published in ((50, ns["POE_SHARE_BY_50"]), (90, ns["POE_SHARE_BY_90"])):
        decided = ns["total_experience"](rate, 1.0, top) / ns["total_experience"](rate, 1.0)
        exact = ns["total_experience"](fitted, 1.0, top) / ns["total_experience"](fitted, 1.0)
        # A tenth of a percentage point. The rounding to 8.2% costs 0.052 of
        # one, so this permits that and catches a real change of rate.
        assert abs(decided - exact) < 0.001, (
            f"rounding the rate moved the share of the climb spent by level "
            f"{top} from {exact:.2%} to {decided:.2%}, against Path of Exile's "
            f"{published:.2%}")

    assert (ns["levels_at_tier_ends"](rate, ns["PER_DUNGEON"])
            == ns["levels_at_tier_ends"](fitted, ns["PER_DUNGEON"])), (
        "rounding the rate moved the level the character reaches at the end of "
        "at least one tier's campaign, which is the thing the rate was chosen "
        "to control")


def test_eight_campaigns_pay_for_the_decided_climb(experience_run):
    """The two decided numbers have to agree with the derived size, and they are
    rounded independently, so nothing guarantees it. If they drift apart the
    character stops reaching level 100 when tier 8 ends, which is the whole
    thing the size was derived to achieve."""
    printed, ns = experience_run
    climb = ns["whole_total_to_reach"](ns["MAX_LEVEL"])
    earned = ns["CAMPAIGN_DUNGEONS"] * sum(ns["PER_DUNGEON"].values())

    assert 0.99 < earned / climb < 1.02, (
        f"eight campaigns now pay {earned / climb:.3f} times the decided climb. "
        f"Below 1 the character never reaches level 100; well above it, level "
        f"100 arrives before tier 8 and the climb is shorter than it was sized for.")
    assert f"which is {earned / climb:.3f} times it" in printed
    assert (ns["levels_at_tier_ends"](ns["DECIDED_RATE"],
                                      ns["PER_DUNGEON"])[-1] == ns["MAX_LEVEL"])


def test_the_decided_curve_is_printed_as_a_formula_and_a_table(experience_run):
    """Whoever implements this reads the printed section, not the source. The
    formula and the cost of the first and last level have to be in it."""
    printed, ns = experience_run
    rate, scale = ns["DECIDED_RATE"], ns["DECIDED_LEVEL_2_COST"]
    assert f"cost of level L = {scale:,.0f} x {1 + rate:g} ^ (L - 2)" in printed
    assert f"{ns['level_cost'](ns['MAX_LEVEL'], rate, scale):,.0f}" in printed

    opening = ns["first_level_in_floors"](rate, scale, ns["POPULATION"],
                                          ns["WEIGHTS"], ns["WHOLE_FLOORS"])
    assert f"level 2 takes {opening:.1f} floors" in printed


def test_the_whole_number_curve_is_not_the_float_one_rounded(experience_run):
    """WHY `whole_total_to_reach` EXISTS AT ALL, asserted so it cannot be quietly
    replaced by rounding the closed-form sum.

    A player pays each level's rounded cost, so the climb is the sum of 99
    roundings. Rounding the geometric sum instead gives a different number. The
    gap is 5 over 6.8 billion, which is nothing as a quantity and everything to
    the C++ port, which has to reproduce these integers exactly because a save
    record stores a character's progress into its current level.
    """
    import math

    _, ns = experience_run
    summed = ns["whole_total_to_reach"](ns["MAX_LEVEL"])
    closed = math.floor(ns["total_experience"](ns["DECIDED_RATE"],
                                               ns["DECIDED_LEVEL_2_COST"]) + 0.5)
    assert summed != closed, (
        "the sum of the rounded level costs now equals the rounded closed-form "
        "sum. If that is genuinely true the two functions can be merged, but "
        "check it holds at every level and not just at 100 before doing it.")
    assert abs(summed - closed) < 100, (
        f"the two now differ by {abs(summed - closed)}, which is far more than "
        f"99 roundings of half a unit each can produce. One of them is wrong.")

    # And the per-level cost really is floor(x + 0.5), the rounding
    # `scoring._js_round` uses, rather than Python's round-half-to-even.
    for level in (2, 37, 64, ns["MAX_LEVEL"]):
        raw = ns["level_cost"](level, ns["DECIDED_RATE"], ns["DECIDED_LEVEL_2_COST"])
        assert ns["whole_level_cost"](level) == math.floor(raw + 0.5)


def test_the_whole_number_curve_refuses_a_level_outside_the_range(experience_run):
    """Level 1 costs nothing to reach and there is nothing above 100. The C++
    port has to agree, so the boundary is stated here rather than left to
    whatever each side happens to do."""
    _, ns = experience_run
    assert ns["whole_level_cost"](1) == 0
    assert ns["whole_level_cost"](0) == 0
    assert ns["whole_level_cost"](-5) == 0
    assert ns["whole_level_cost"](ns["MAX_LEVEL"] + 1) == 0
    assert ns["whole_total_to_reach"](1) == 0
    assert ns["whole_total_to_reach"](2) == ns["whole_level_cost"](2)


# --------------------------------------------------------------------------
# analyse_affix_spread.py -- issue #1179
#
# The play report was that affixes "all spawn with the exact same numbers
# instead of randomly within a range". The rolls ARE random; three multipliers
# that were each designed separately squeeze the result until the randomness is
# invisible on the tool tip. The script measures the product, which nobody
# chose directly, and these check its conclusions rather than trusting them.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def affix_spread_run():
    return run("analyse_affix_spread.py")


def test_the_tool_tip_rounding_copy_matches_the_engine():
    """`number_in_words` is a copy of `UCataclysmItemTooltip::NumberInWords`.

    THE ONLY COPIED CODE IN THAT SCRIPT, and the whole measurement rests on it:
    the finding is about what a player READS, not what the value is. The engine
    prints a whole number with no decimal point and everything else to one
    decimal place, so this states the boundary cases that distinguish the two
    branches. A change to the engine's rounding that is not made here would
    make the script report a spread nobody sees.
    """
    _, ns = run("analyse_affix_spread.py")
    words = ns["number_in_words"]

    # The whole-number branch, including the 0.005 tolerance either side.
    assert words(0.0) == "0"
    assert words(120.0) == "120"
    assert words(120.004) == "120"
    assert words(119.997) == "120"

    # The one-decimal branch.
    assert words(0.041) == "0.0"
    assert words(0.152) == "0.2"
    assert words(4.85) == "4.9" or words(4.85) == "4.8"  # float, either is fine
    assert words(0.35) == "0.3" or words(0.35) == "0.4"


def test_no_affix_can_only_ever_show_zero_any_more(affix_spread_run):
    """The sharpest form of the finding, and it is now fixed. Issue #1179.

    Until both affix ladders were eased on 2026-09-03, three affixes could only
    ever display `0.0` on an early drop -- a slot granting a number the player
    reads as nothing. All three now display `0.1`.

    ASSERTED AS "NONE" RATHER THAN AS A LIST OF NAMES, because a list of zero
    names is the same assertion written less clearly, and any affix falling back
    to zero is a regression whichever one it is.
    """
    _, ns = affix_spread_run

    stuck_at_zero = sorted(
        affix.name for affix in ns["every_measurable_affix"]()
        if set(ns["displayed_values"](affix)[0]) <= {"0", "0.0"})

    assert stuck_at_zero == [], (
        f"these affixes can only ever display zero on an early drop: "
        f"{stuck_at_zero}. Before #1179 the three leech affixes did. An affix "
        f"back at zero means either the two ladders were steepened again, or "
        f"its stated top value is too small to survive them.")


def test_one_affix_still_shows_only_one_number(affix_spread_run):
    """WHAT A STATED FLOOR DID NOT FIX, kept visible on purpose.

    It was four before issue #1230 -- the three leeches and mana regeneration --
    and all of them had stated top values of 0.5 to 0.7, so a tenth of that
    could only ever print one number. The three leeches now state a top of 4.0
    and a floor of 1.0 and have left this list.

    MANA REGENERATION IS THE ONE THAT STAYS, and that is a decision rather than
    an oversight. Measured against the reference build's 436 mana, every one of
    its 48 eligible affix slots filled comes to 7.2% of the pool per second,
    against a class line of 2.5% to 6%, so the stat is already in scale and
    raising its top would make one affix worth most of a class line. Only its
    floor moved, from 0.065 to 0.2, so the worst roll prints `0.2` rather than
    `0.1`. Its band at tier 1 is 0.200 to 0.230, which rounds to one number.
    """
    _, ns = affix_spread_run

    one_number = sorted(
        affix.name for affix in ns["every_measurable_affix"]()
        if len(ns["displayed_values"](affix)[0]) == 1)

    assert one_number == ["Flat mana regeneration"], (
        f"the affixes that can show only one number on an early drop have "
        f"changed to {one_number}. If a stated top or floor moved, say so on "
        f"#1230 and update this list; if new ones appeared, that is a "
        f"regression.")


def test_the_squeezed_list_shrank_from_nine_to_one(affix_spread_run):
    """What stating a floor per affix bought, measured rather than asserted.

    Nine affixes could show three different numbers or fewer on an early drop
    after issue #1179 eased the two ladders. One can now. The eight that left
    are the ones a player was handed a worthless number for: evasion,
    penetration, damage reduction, retaliation, health regeneration and the
    three leeches.

    IT WAS TWELVE BEFORE #1179 AND NINE AFTER IT. That change eased the tier and
    gear ladders, which lifted the whole curve; this one states a floor per
    affix and moves seven stated tops, which lifts the bottom of the affixes
    that were still worthless. The two are different work on the same fault.

    WRITTEN OUT RATHER THAN COUNTED, because the interesting failure is a
    DIFFERENT affix joining or leaving the list, which a count would hide.

    The denominator differs from the issue on purpose: the issue counted against
    the non-hybrid rows in `game/Data/Affixes.csv`, and this script measures the
    affixes the model carries.
    """
    _, ns = affix_spread_run

    squeezed = sorted(
        affix.name for affix in ns["every_measurable_affix"]()
        if len(ns["displayed_values"](affix)[0]) <= ns["FEW"])

    assert squeezed == [
        "Flat mana regeneration",
    ], (
        f"the affixes that can show three numbers or fewer on an early drop "
        f"have changed to {squeezed}. Before #1179 there were twelve, and "
        f"before #1227 there were eight. A longer "
        f"list means the ladders were steepened again; a shorter one means "
        f"#858 was acted on and this list needs updating.")


def test_the_headline_numbers_a_player_reads_are_no_longer_fractions(
        affix_spread_run):
    """THE COMPLAINT #1179 STARTED FROM, in the project owner's words: the
    values should be actually useful, and no more of this fraction-of-a-per-cent
    nonsense.

    These four are the affixes a player looks at first. Before the change the
    best a tier 1 drop could show for maximum health was 4.9 and for increased
    damage 5.1; both now start above 12. Stated as a floor rather than a range,
    so a future retune that raises them further does not fail this.
    """
    _, ns = affix_spread_run

    floors = {
        "Flat maximum health": 12.0,
        "Increased damage": 12.0,
        "Flat damage": 2.0,
        "Increased maximum health": 1.0,
    }
    by_name = {a.name: a for a in ns["every_measurable_affix"]()}

    for name, floor in floors.items():
        _, low, high = ns["displayed_values"](by_name[name])
        assert low >= floor, (
            f"the worst {name} an early drop can give is {low:.2f}, below the "
            f"floor of {floor} this test holds. Issue #1179 raised these from "
            f"fractions of a point; something has pushed them back down.")
        assert high > low


def test_the_headline_affixes_do_show_a_spread(affix_spread_run):
    """The play report is not literally true of every affix, and saying so
    matters as much as the finding.

    Flat damage, increased damage and the two health affixes all show several
    different numbers. Overstating the fault would send somebody to fix the
    roll band, which is the one part of this that is working as designed.
    """
    _, ns = affix_spread_run
    from cataclysm_sim import affixes as af

    for affix in (af.FLAT_DAMAGE, af.INCREASED_DAMAGE, af.FLAT_HEALTH):
        shown, _, _ = ns["displayed_values"](affix)
        assert len(shown) > ns["FEW"], (
            f"{affix.name} now shows only {len(shown)} different numbers on an "
            f"early drop, so it has joined the squeezed list and the test "
            f"above needs updating too")


# --------------------------------------------------------------------------
# analyse_siege_dose.py -- issue #1349, the Siege dose-response investigation
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def siege_dose_run():
    """The dose-response curves the project owner ordered on issue #1349.

    A FEW SECONDS AT THE SCRIPT'S DEFAULT `TRIALS`, which is a smoke size, and
    **the campaign shares it prints at that size are noise**. The curves put to
    the owner were taken at `CATACLYSM_SIEGE_DOSE_TRIALS=1000`, which is 2,000
    campaigns a dose over two disjoint seed blocks and 34,000 campaigns in all --
    tens of minutes, and the better part of an hour on a busy machine.

    SO NOTHING BELOW ASSERTS A SHARE. Every check here is either on arithmetic
    that does not move with the sample size, on the instrument itself, or on the
    script stating the conditions its figures were taken under -- which is the
    failure this project has retracted figures to the owner for twice.
    """
    return run("analyse_siege_dose.py")


def test_the_days_to_fall_closed_form_matches_the_day_loop(siege_dose_run):
    """THE LOAD-BEARING CHECK ON AXIS 3.

    `days_to_fall` is the Siege arithmetic written out in closed form, and the
    script's whole third axis -- how much slower a Siege has to be before the
    player can answer one -- is read off it. `_apply_siege_damage` is the
    arithmetic the model actually runs. This stands a Siege on one city of each
    size and advances days until it falls, the way
    `sim/tests/test_siege_subtype.py` does, and compares the two.

    A CLOSED FORM THAT DRIFTS FROM THE LOOP IT DESCRIBES FAILS SILENTLY. It
    would still print a table, still be monotonic, and still look like a
    measurement.
    """
    from dataclasses import replace

    from cataclysm_sim.config import CityTier, DungeonType
    from cataclysm_sim.engine import Simulation

    _, ns = siege_dose_run
    base = ns["base_config"]()

    for scale in (1.0, 0.5, 0.25):
        cfg = replace(
            base,
            siege_defence_bite_per_day=base.siege_defence_bite_per_day * scale,
            siege_population_bite_per_day=(
                base.siege_population_bite_per_day * scale),
            siege_damage_growth_per_day=(
                base.siege_damage_growth_per_day * scale))
        for tier in ns["CITY_SIZES"]:
            sim = Simulation(cfg, seed=0)
            city = next(c for c in sim.empire.cities.values()
                        if c.tier is tier)
            d = sim._make_dungeon(DungeonType.BASIC, city)
            d.subtype = "Siege"
            d.spawned_day = sim.day
            days = 0
            while city.defense > 0 and days < 10_000:
                sim._apply_siege_damage()
                sim.day += 1
                days += 1
            predicted = ns["days_to_fall"](
                base.TIER_STATS[tier].max_defense * base.tree.city_health_mult,
                scale=scale, cfg=base)
            assert days == predicted, (
                f"at {scale}x Siege damage a {tier.value} falls on day {days} "
                f"in the day loop and analyse_siege_dose.days_to_fall says "
                f"{predicted}. The closed form the axis 3 table is read off has "
                "drifted from the model.")
    assert CityTier.OUTPOST in ns["CITY_SIZES"]


def test_it_reproduces_the_four_stated_days_to_fall(siege_dose_run):
    """25, 39, 55 and 70 days for the four city sizes.

    Those are the figures `game/Source/CataclysmEmpire/Empire/
    CataclysmEmpireRun.h` states in prose and `sim/tests/test_siege_subtype.py`
    asserts against the day loop. The script's table is a third statement of
    them, so it has to agree or the table is describing a different game from
    the one the owner is being asked to rule on.

    THEY WERE 14 / 23 / 34 / 47 UNTIL 2026-09-06, when the owner cut the growth
    from 10 points a day to 2.5 on issue #1349. The old four are still on the
    axis-3 ladder as the `10.0` row, and the check below that they are is what
    stops this test quietly becoming a check on one dose.
    """
    from cataclysm_sim.config import CityTier

    printed, ns = siege_dose_run
    stated = {CityTier.OUTPOST: 25, CityTier.BULWARK: 39,
              CityTier.SANCTUARY: 55, CityTier.PILLAR: 70}
    today = ns["axis_3_table"]()["scale"][1.0]
    assert today == stated, (
        f"the script's table says {today} where the game states {stated}")
    assert "   25         39         55         70" in printed

    replaced = {CityTier.OUTPOST: 14, CityTier.BULWARK: 23,
                CityTier.SANCTUARY: 34, CityTier.PILLAR: 47}
    assert ns["axis_3_table"]()["growth"][10.0] == replaced, (
        "the growth the owner ruled against no longer produces the four days "
        "every record of it names, so the before-and-after in the script's "
        "axis 3 is not comparable with what issue #1349 measured")


def test_the_inverse_square_root_sentence_is_computed_not_typed(
        siege_dose_run):
    """The conclusion this whole file exists to guard: a typed sentence under a
    computed table.

    The script's headline claim on axis 3 is that halving the damage does NOT
    double the time a city survives, because the damage dealt after n days is
    quadratic in n. Both ratios are recomputed here from `days_to_fall` and
    looked for in what the script printed, and the DIRECTION is asserted
    separately -- that is the half a future edit is most likely to get
    backwards, and getting it backwards would tell the owner that a smaller
    number buys more time than it does.
    """
    from cataclysm_sim.config import CityTier

    printed, ns = siege_dose_run
    table = ns["axis_3_table"]()["scale"]
    today = table[1.0][CityTier.OUTPOST]
    half = table[0.5][CityTier.OUTPOST]
    quarter = table[0.25][CityTier.OUTPOST]

    assert 1.0 < half / today < 2.0, (
        f"halving the Siege damage now multiplies an Outpost's life by "
        f"{half / today:.2f}. Below 1 the lever is backwards; at or above 2 the "
        "growth term no longer dominates and the script's axis 3 argument is "
        "gone.")
    assert f"from {today} days to {half}, which is {half / today:.2f}x the" \
        in printed
    assert f"Quartering it reaches {quarter} days, {quarter / today:.2f}x." \
        in printed


def test_zeroing_the_growth_gives_every_city_the_same_hundred_days(
        siege_dose_run):
    """The lever that is genuinely axis 3 and not axis 2.

    Dropping the growth term and leaving the 1% share alone keeps the Siege's
    identity -- the owner's deliberate exception, that a Siege does not care how
    thick your walls are -- and makes time-to-fall independent of city size.
    100 days is the same figure
    `game/Source/CataclysmEmpire/Tests/CataclysmEmpireRunTests.cpp` asserts for
    a share-only Siege, so this ties the script to the C++ as well.
    """
    printed, ns = siege_dose_run
    zero_growth = ns["axis_3_table"]()["growth"][0.0]
    assert set(zero_growth.values()) == {100}, (
        f"a Siege with no growth term now empties the four city sizes in "
        f"{sorted(set(zero_growth.values()))} days, not 100 for all of them")
    assert "city size exactly 100 days" in printed


def test_the_spawn_weights_are_the_doses_the_ruling_names(siege_dose_run):
    """Issue #1340 specifies 0, 7.5, 15, 30 and 50, and 7.5 is what
    `development` ships since the owner halved it on issue #1349 on 2026-09-06.
    A silently different ladder would answer a question the owner did not ask.

    THE SHIPPED WEIGHT IS ON THE LADDER AND SO IS THE ONE IT REPLACED, which is
    what lets one run of this script show both the configuration in the tree and
    the configuration the ruling moved away from."""
    _, ns = siege_dose_run
    assert ns["SIEGE_WEIGHTS"] == (0.0, 7.5, 15.0, 30.0, 50.0)
    assert ns["SIEGE_WEIGHT_TODAY"] == 7.5, (
        "development no longer ships Siege at 7.5 in 100, so the curves on "
        "issue #1349 are centred on the wrong dose")
    assert 15.0 in ns["SIEGE_WEIGHTS"], (
        "the weight the owner ruled against is no longer on the ladder, so a "
        "run of this script cannot show what the change bought")


def test_the_growth_doses_carry_the_shipped_value_and_the_one_it_replaced(
        siege_dose_run):
    """Axis 3 has to include both ends of the ruling of 2026-09-06 for the same
    reason axis 1 does: the script is a re-measurement of a change, not only a
    sweep around a point."""
    _, ns = siege_dose_run
    assert ns["GROWTH_TODAY"] == 2.5, (
        "development no longer ships a Siege growth of 2.5 points a day, which "
        "is what the owner ruled on issue #1349")
    assert ns["GROWTH_TODAY"] in ns["GROWTH_DOSES"]
    assert 10.0 in ns["GROWTH_DOSES"], (
        "the growth the owner ruled against is no longer on the ladder")


def test_the_slack_is_taken_up_in_proportion_and_still_totals_a_hundred(
        siege_dose_run):
    """The other six sub-types share what Siege gives up IN PROPORTION, which is
    what issue #1340 specifies. Redistributing it equally would be a second,
    unasked-for change to the spawn table riding along inside the dose."""
    _, ns = siege_dose_run
    today = dict(ns["base_config"]().SUBTYPE_SPAWN_WEIGHTS)
    reference = today["Timed"] / today["Cow Level"]

    for weight in ns["SIEGE_WEIGHTS"]:
        table = ns["weights_with_siege_at"](weight)
        assert sum(table.values()) == pytest.approx(100.0)
        assert table["Siege"] == pytest.approx(weight)
        assert table["Timed"] / table["Cow Level"] == pytest.approx(reference), (
            f"at Siege weight {weight} the other sub-types no longer keep "
            "their proportions to each other")
        assert set(table) == set(today), (
            "a sub-type was dropped from the spawn table rather than reweighted")

    assert ns["weights_with_siege_at"](7.5) == pytest.approx(today), (
        "the 7.5 dose no longer reproduces the shipped table, so the curve's "
        "centre point is not the game. 7.5 is what the owner ruled on issue "
        "#1349 on 2026-09-06; before that this check was on the 15 dose")


def test_the_instrumentation_does_not_change_the_campaign(siege_dose_run):
    """EVERY FIGURE THE SCRIPT PRINTS RESTS ON THIS.

    The per-Siege response counts come from wrapping the policy, and a wrapper
    that drew a random number, or changed what the policy returned, would shift
    every campaign underneath the measurement while still printing a plausible
    table. Compared field by field against the bare policy on the same seeds.
    """
    import dataclasses

    from cataclysm_sim import policies
    from cataclysm_sim.engine import Simulation

    _, ns = siege_dose_run
    cfg = ns["base_config"]()
    plain = policies.ALL["triage"]
    counted = ns["instrumented"](plain)

    for seed in range(12):
        bare = Simulation(cfg, seed=seed).run(plain)
        wrapped = ns["_InstrumentedSimulation"](cfg, seed=seed).run(counted)
        assert dataclasses.asdict(bare) == dataclasses.asdict(wrapped), (
            f"seed {seed} runs differently once the policy is instrumented, so "
            "the response counts describe campaigns other than the ones the "
            "shares were measured over")


def test_sieges_are_counted_when_created_and_not_when_surviving(
        siege_dose_run):
    """A survivor count is not a frequency when the thing counted destroys its
    own host.

    A city that falls absorbs every dungeon standing on it, so counting the
    Sieges left at the end under-counts exactly the Sieges that did their job.
    The script hooks `_make_dungeon`; this checks the hook sees strictly more
    than the end-of-run census over a batch where cities do fall.
    """
    from cataclysm_sim import policies

    _, ns = siege_dose_run
    cfg = ns["base_config"]()
    counted = ns["instrumented"](policies.ALL["triage"])

    created, standing, fell = 0, 0, 0
    for seed in range(25):
        sim = ns["_InstrumentedSimulation"](cfg, seed=seed)
        sim.run(counted)
        created += sim.sieges_created
        standing += sum(1 for d in sim.dungeons.values()
                        if d.subtype == "Siege")
        fell += len(sim.empire.fallen_cities())

    assert fell > 0, "no city fell, so this batch cannot show the difference"
    assert created > standing, (
        f"{created} Sieges were created and {standing} were still standing at "
        "the end, so the two counts agree and the census trap is not being "
        "avoided by construction")


def test_the_two_readings_of_won_per_last_stand_are_not_the_same(
        siege_dose_run):
    """A figure without its denominator is not a figure, and this one has two.

    `RunResult` carries `won` and `last_stand` and nothing joins them, so "won
    per Last Stand reached" reads either as every win in a campaign that reached
    one, or as the Last Stands actually cleared. **The readings differ by
    nearly a factor of two**, because about half the campaigns that open the
    earned boss go on to reach a Last Stand as well and their win is counted by
    the first reading.

    That gap is what made a session's reproduction of the issue #1349 baseline
    look like a disagreement with the model when it was a disagreement about the
    denominator. The script reports the second reading, which is the one issue
    #1286's ruling was made on. This holds both apart so a future edit cannot
    quietly collapse them into one number again.
    """
    from cataclysm_sim import policies

    _, ns = siege_dose_run
    cfg = ns["base_config"]()
    counted = ns["instrumented"](policies.ALL["triage"])

    reached = cleared = won_anything = both_routes = 0
    for seed in range(150):
        sim = ns["_InstrumentedSimulation"](cfg, seed=seed)
        result = sim.run(counted)
        if not result.last_stand:
            continue
        reached += 1
        won_anything += 1 if result.won else 0
        cleared += 1 if sim.won_by == "last stand" else 0
        if result.cataclysm_floors > 0:
            both_routes += 1

    assert reached > 0, "no campaign reached a Last Stand in this batch"
    assert both_routes > 0, (
        "no campaign both opened the earned boss and reached a Last Stand, so "
        "this batch cannot show the two readings apart and the test proves "
        "nothing")
    assert cleared <= won_anything, (
        f"{cleared} Last Stands were cleared but only {won_anything} campaigns "
        "that reached one won at all, which is impossible")
    assert sim.won_by in (None, "earned", "last stand")


def test_it_states_the_settings_beside_the_figures(siege_dose_run):
    """A figure that travels without its conditions is how this project made
    two retractions to the owner.

    In particular `surge_dungeon_count` is 5 and not the `TuningConfig` default
    of 4, which `experiments.exp_calibrate` records as rejected and never
    calibrates; the balance report runs at 5. A reader who assumes 4 is reading
    a different world.
    """
    printed, ns = siege_dose_run
    cfg = ns["base_config"]()
    assert cfg.surge_dungeon_count == 5
    assert "NOT the TuningConfig default of 4" in printed
    for expected in ("difficulty tier                 1",
                     "empire tree preset              No tree",
                     "policy                          triage",
                     "resolve timer days per floor    2.0",
                     "days per craft                  12"):
        assert expected in printed, f"the settings block no longer states: {expected}"
    assert f"campaigns per block             {ns['TRIALS']}" in printed


def test_a_smoke_sized_run_says_its_shares_are_noise(siege_dose_run):
    """A GUARD ON READING RATHER THAN ON CODE, and the one most likely to matter.

    The script's default sample exists so this test file stays fast. At that
    size the campaign shares mean nothing, and a table of them is exactly the
    kind of output that gets quoted. It has to say so itself, in its own output,
    rather than only in a docstring nobody prints.
    """
    printed, ns = siege_dose_run
    if ns["TRIALS"] < 250:
        assert "IS A SMOKE TEST AND THE SHARES BELOW ARE NOISE" in printed
        assert "CATACLYSM_SIEGE_DOSE_TRIALS=1000" in printed
    else:
        assert "SMOKE TEST" not in printed


def test_the_script_does_not_recommend_a_constant(siege_dose_run):
    """The owner ruled, verbatim, "Investigate before changing anything", and
    that no constant changes on the strength of a curve alone. The script prints
    curves; the recommendation belongs on the issue where the owner can see what
    it rests on."""
    printed, _ = siege_dose_run
    assert "THE RECOMMENDATION IS NOT IN THIS FILE" in printed
    assert "no constant changes" in printed


# --------------------------------------------------------------------------
# analyse_surge_cadence.py -- issue #1090
#
# WHAT THIS SECTION IS FOR. The script sweeps dungeons-per-surge against
# days-between and reports how much of a campaign an invested player spends
# with nothing to do. Three things about it can go wrong quietly, and the
# checks below are one per thing.
#
# 1. THE COUNT AXIS CAN STOP MEANING ANYTHING. `Simulation.surge_count` applies
#    `min(n, surge_count_max)` to the BASE count and not only to the escalation
#    growth, and `surge_count_max` is 14. The first version of this script did
#    not lift it, so its knobs of 20, 30 and 40 all measured a knob of 14 and
#    three of its six rows were the same row. Nothing in the output said so.
# 2. THE DAY LEDGER CAN STOP ADDING UP. `_Ledger` classifies every day as
#    walking, at the forge, dead, or free by reading three flags at the top of
#    `Simulation.step`. A fifth branch in the day loop, or a write to one of
#    those flags earlier in the step, would make the classification silently
#    disagree with the branch actually taken.
# 3. THE FAN-OUT CAN STOP MATCHING. The grid is about five core-hours, so it is
#    measured across worker processes. A measurement that changes when it is
#    split is not a measurement.
#
# The script's own campaign shares are NOT checked here. At its default sample
# they are one campaign each and mean nothing, which the run says itself; the
# figures on issue #1090 were taken at a thousand a block.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def cadence_run():
    """About 3.4 seconds -- the second most expensive script in this file.

    Its default narrows the sweep axes to their two ends for exactly this
    reason; see `_axis` in the script. The full 24-cell grid is 192 campaigns
    and thirteen seconds, and at one campaign a cell it measures nothing.
    """
    return run("analyse_surge_cadence.py")


def test_the_count_axis_is_not_silently_capped_at_fourteen(cadence_run):
    """THE DEFECT THIS SCRIPT WAS REWRITTEN TO FIX, held in place.

    `sim/cataclysm_sim/config.py` sets `surge_count_max = 14` and
    `Simulation.surge_count` applies it to the base count. The owner's question
    is about roughly 20 dungeons a surge, so a sweep that does not lift the cap
    cannot answer it -- and does not fail, or warn, or print anything different.
    It just measures 14 three times.

    Both halves are asserted: that the shipped cap really would flatten the
    axis, so this is a live hazard rather than a historical one, and that
    `base_config` lifts it so the knob is realised.
    """
    _, ns = cadence_run
    surge_count_for = ns["surge_count_for"]
    shipped_cap = ns["SHIPPED_COUNT_CAP"]

    assert shipped_cap == 14, (
        f"surge_count_max is now {shipped_cap}, not 14. The figures on issue "
        "#1090 were measured against a cap of 14 and say so; if this moved "
        "deliberately, the grid needs re-running.")

    # Under what ships, everything above the cap is the cap.
    assert surge_count_for(1, 10, shipped_cap) == 10
    assert surge_count_for(1, 20, shipped_cap) == 14
    assert surge_count_for(1, 40, shipped_cap) == 14

    # Under what this script measures, the knob is the knob.
    for knob in (4, 5, 10, 14, 20, 30, 40):
        assert surge_count_for(1, knob) == knob, (
            f"a knob of {knob} fires {surge_count_for(1, knob)} dungeons. "
            "base_config has stopped raising surge_count_max, so every count "
            "above 14 in the grid is secretly a count of 14.")


def test_the_cap_that_ships_is_the_one_the_game_ships(cadence_run):
    """The simulation's `surge_count_max` and the C++ `MostDungeonsPerSurge`
    are the same number, and the script's output names the C++ one. If they
    ever part company the script would be describing a cap the game does not
    have. `tools/tests/test_surge_port.py` is what holds the two together; this
    only checks that the script quotes the right side of it."""
    printed, ns = cadence_run
    assert f"surge_count_max ships at {ns['SHIPPED_COUNT_CAP']}" in printed
    assert "MostDungeonsPerSurge in CataclysmSurge.h" in printed


def test_the_unbound_surge_count_matches_a_real_simulation(cadence_run):
    """`surge_count_for` calls `Simulation.surge_count` on a stand-in carrying
    the two attributes that method reads, because building 96 real empires for
    a table of 96 integers cost four seconds of this suite.

    THAT IS ONLY SAFE WHILE THE STAND-IN IS ENOUGH. If `surge_count` grows a
    read of anything else on the simulation, the stand-in raises
    `AttributeError` or -- worse -- the two answers quietly part. This asks a
    real `Simulation` the same question at every knob on the axis.
    """
    from cataclysm_sim.engine import Simulation

    _, ns = cadence_run
    for knob in (4, 5, 10, 14, 20, 30, 40):
        real = Simulation(ns["base_config"](tier=1, count=knob), seed=0)
        assert ns["surge_count_for"](1, knob) == real.surge_count(), (
            f"the stand-in and a real Simulation disagree at knob {knob}")


def test_the_day_ledger_accounts_for_every_day(cadence_run):
    """Walking, at the forge, dead, or free -- and nothing else.

    This is the check that would notice a fifth branch being added to
    `Simulation.step`, or one of the three flags being written earlier in the
    step than the ledger reads them. Either would make the idle share -- the
    headline of the whole file -- quietly wrong, because `empty%`, `noSafe%`
    and `walk%` are all shares of `survived_days`.

    Run across several worlds and cadences, because a branch that only fires in
    a busy campaign would not show up in a quiet one.
    """
    _, ns = cadence_run
    ledger_policy, Ledger = ns["ledger_policy"], ns["_Ledger"]
    from cataclysm_sim import policies as pol

    policy = ledger_policy(pol.ALL["triage"])
    for world_index in range(len(ns["WORLDS"])):
        _, tree, tier = ns["WORLDS"][world_index]
        for count, interval in ((4, 120), (40, 30)):
            cfg = ns["base_config"](tier=tier, count=count, interval=interval,
                                    tree=tree)
            sim = Ledger(cfg, seed=world_index)
            result = sim.run(policy)
            total = (sim.walk_days + sim.forge_days + sim.dead_days
                     + sim.free_days)
            assert total == result.survived_days, (
                f"world {world_index}, {count} every {interval}d: the ledger "
                f"accounts for {total} days of a {result.survived_days}-day "
                "campaign. Simulation.step has a branch _Ledger does not know "
                "about.")


def test_the_three_kinds_of_idle_day_account_for_every_idle_day(cadence_run):
    """`empty_board_days + no_safe_days + declined_days == idle_days`.

    The split is what separates "the cadence is too slow" from "the player is
    underpowered", and only the first is a question about surge cadence. If the
    three stopped summing, the script would be attributing idle days to a cause
    without covering all of them.
    """
    _, ns = cadence_run
    from cataclysm_sim import policies as pol

    policy = ns["ledger_policy"](pol.ALL["triage"])
    for world_index in range(len(ns["WORLDS"])):
        _, tree, tier = ns["WORLDS"][world_index]
        cfg = ns["base_config"](tier=tier, count=4, interval=120, tree=tree)
        sim = ns["_Ledger"](cfg, seed=world_index)
        result = sim.run(policy)
        split = sim.empty_board_days + sim.no_safe_days + sim.declined_days
        assert split == result.idle_days, (
            f"world {world_index}: {split} classified idle days against "
            f"{result.idle_days} counted by the engine.")


def test_instrumenting_a_campaign_does_not_change_it(cadence_run):
    """A MEASUREMENT THAT MOVES WHAT IT MEASURES IS NOT ONE.

    `_Ledger` counts days and `ledger_policy` inspects the board before letting
    the real policy answer. Neither may draw a random number, or every campaign
    after the first draw would diverge from the campaign a plain `Simulation`
    would have run -- and it would still look plausible, because the shape of
    the output would not change at all.

    `Simulation.death_chance` is pure, which is what makes the wrapper safe;
    this asserts the consequence rather than the reason.
    """
    from cataclysm_sim import policies as pol
    from cataclysm_sim.engine import Simulation

    _, ns = cadence_run
    triage = pol.ALL["triage"]
    for world_index in range(len(ns["WORLDS"])):
        _, tree, tier = ns["WORLDS"][world_index]
        for count, interval in ((4, 120), (40, 30)):
            cfg = ns["base_config"](tier=tier, count=count, interval=interval,
                                    tree=tree)
            for seed in (0, 1):
                plain = Simulation(cfg, seed=seed).run(triage)
                counted = ns["_Ledger"](cfg, seed=seed).run(
                    ns["ledger_policy"](triage))
                assert counted == plain, (
                    f"world {world_index}, {count} every {interval}d, seed "
                    f"{seed}: the instrumented campaign is not the campaign a "
                    "bare Simulation runs. Something in _Ledger or "
                    "ledger_policy draws from the random number generator.")


def test_the_fan_out_measures_what_this_process_measures(cadence_run):
    """The grid is about five core-hours, so it is measured across worker
    processes. This runs two cells both ways and demands they agree exactly.

    IT IS NOT A TEST OF DETERMINISM FOR ITS OWN SAKE. A worker is a fresh
    interpreter reading its own environment, so a cell whose settings came from
    a module-level constant rather than from its own key would come back
    measured under the worker's defaults instead -- silently, and only for the
    fanned-out run, which is the only run anyone quotes.
    """
    _, ns = cadence_run
    keys = [ns["_cell_key"](1, 4, 120, 0), ns["_cell_key"](3, 40, 30, 0)]
    here = ns["measure_cells"](keys, 1)
    split = ns["measure_cells"](keys, 2)
    assert set(here) == set(split)
    for key in here:
        assert set(here[key]) == set(split[key])
        for field, value in here[key].items():
            other = split[key][field]
            # A one-campaign cell has no spread, so its standard error is a
            # NaN -- which is never equal to itself. Comparing the dicts
            # directly would fail here for a reason that has nothing to do
            # with the fan-out.
            if isinstance(value, float) and math.isnan(value):
                assert math.isnan(other), (
                    f"{key}.{field} is NaN in this process and {other} in a "
                    "worker")
                continue
            assert value == other, (
                f"{key}.{field} is {value} in this process and {other} in a "
                "worker. Something in the cell is being read from the "
                "worker's environment rather than from the cell it was asked "
                "for.")


def test_every_section_measures_each_batch_once(cadence_run):
    """Sections 3, 4 and 5 name their batches the same way, so the batch two of
    them want is measured once. What ships -- 4 every 120 days -- is section
    3's whole table and also a cell of the grid; measuring it twice would have
    been about eight thousand redundant campaigns at the size the issue quotes.
    """
    _, ns = cadence_run
    keys = ns["all_cells"]()
    assert len(keys) == len(set(keys)), "all_cells returned a duplicate"
    listed = (ns["grid_cells"]() + ns["shipped_cadence_cells"]()
              + ns["noise_floor_cells"]())
    assert set(keys) == set(listed)
    assert len(keys) < len(listed), (
        "no batch is shared between the three sections any more, so either the "
        "axes no longer contain what ships or the key format changed.")


def test_the_noise_floor_uses_six_disjoint_blocks(cadence_run):
    """ISSUE #1379, WHICH IS THE MEASUREMENT LESSON THIS FILE EXISTS UNDER.

    A gap between two seed blocks is one difference and not a spread; six
    blocks gave a standard deviation of 0.105 where two blocks differed by
    0.04. The script reports the spread over six blocks AND the analytic
    standard error of one block, and says in its own output that the A-to-B gap
    is not a noise floor. The blocks have to actually be disjoint for any of
    that to mean anything.
    """
    printed, ns = cadence_run
    trials = ns["TRIALS"]
    seeds = [int(key.split(",")[3]) for key in ns["noise_floor_cells"]()]
    assert len(seeds) == ns["NOISE_BLOCKS"] >= 6
    assert seeds == sorted(seeds) and len(set(seeds)) == len(seeds)
    for earlier, later in zip(seeds, seeds[1:], strict=False):
        assert later - earlier >= trials, (
            f"blocks starting at {earlier} and {later} overlap at "
            f"{trials} campaigns each.")
    assert "which is NOT a spread" in printed
    assert "issue #1379 records going wrong" in printed


def test_the_settings_block_states_every_condition(cadence_run):
    """A FIGURE WITHOUT ITS CONDITIONS IS NOT A FIGURE, and this project has
    retracted balance numbers for exactly that. The two that get lost are the
    ones that are not obvious from the axes: that the Explorer branch is held
    at the flat 70 days issue #1383 proposes replacing, and that
    `surge_count_max` was raised so the count axis means what it says.
    """
    printed, ns = cadence_run
    for expected in ("policy                          triage",
                     "surge mode                      static",
                     "resolve timer days per floor    2.0",
                     "dungeon power per 100 days      0.10",
                     "days per craft                  12",
                     "tier width gained per craft     0.04",
                     f"campaigns per block             {ns['TRIALS']}"):
        assert expected in printed, (
            f"the settings block no longer states: {expected}")
    for expected in ("Explorer whole branch           run_days_flat=60",
                     "Explorer day nodes only         run_days_flat=60"):
        assert expected in printed, (
            f"the settings block no longer states: {expected}. Issue #1386 "
            "found that the shipped preset is one sub-build of the branch "
            "and not the branch, so a grid measured against either has to "
            "say which.")
    assert "raised to the knob" in printed, (
        "the settings block no longer says surge_count_max was lifted.")


def test_a_smoke_sized_cadence_run_says_its_shares_are_noise(cadence_run):
    """The same guard on reading that `analyse_siege_dose.py` carries. At the
    default sample every share is one campaign, and a table of them is exactly
    the kind of output that gets quoted."""
    printed, ns = cadence_run
    if ns["TRIALS"] < ns["SMOKE_BELOW"]:
        assert "IS A SMOKE TEST AND EVERY SHARE BELOW IS NOISE" in printed
        assert "CATACLYSM_SURGE_CADENCE_TRIALS=1000" in printed
        assert ns["COUNTS"] == (4, 20) and ns["INTERVALS"] == (30, 120), (
            "the smoke run no longer narrows to the ends of the axes, so it "
            "costs the fast suite the full thirteen seconds.")
    else:
        assert "SMOKE TEST" not in printed


def test_the_cadence_script_does_not_recommend_a_constant(cadence_run):
    """No constant changes on the strength of a sweep alone on this project.
    The script prints the grid; issue #1090 carries the single recommendation
    the owner rules on."""
    printed, _ = cadence_run
    assert "THE RECOMMENDATION IS NOT IN THIS FILE" in printed
    assert "no constant changes" in printed.lower()
