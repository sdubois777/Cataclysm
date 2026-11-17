"""The three damage over time levers, and the pricing that ties them together.

WHAT THIS IS FOR. Issue #205 reported that ten affixes apply an ailment and not
one affix makes an ailment hurt more, last longer or tick harder. Issue #258
reported that the one damage over time affix that did exist, increased damage
over time frequency, was priced at 12% on the assumption that ticking faster only
changed when damage arrived rather than how much of it there was.

Both turn on the same rule, decided by the project owner on 2026-08-04 under
issue #220 and written into `docs/Cataclysm_GDD_v2.md`: **a damage over time
effect deals a fixed amount per tick.** So its total is damage per tick x ticks
per second x seconds, all three are scalable, and all three multiply.

WHY THE PRICING NEEDED SOLVING RATHER THAN COPYING. Three affixes at 125% each,
matched to Increased Damage, would multiply damage over time by 8.5 x 8.5 x 8.5
instead of 8.5. The value has to be the one that makes six slots spread over the
three levers reach what six slots of Increased Damage reach. That is why #258 was
blocked on #205: neither could be settled without the other.

WHAT THIS FILE DOES NOT COVER. Whether the shape is right away from six slots.
It is not: the two curves cross exactly once and a damage over time build pulls
ahead above six slots. That is a consequence of the levers multiplying, which the
design intends, and its size is a balance question rather than a correctness one.
`test_the_two_curves_cross_once_and_the_gap_is_recorded` measures it so the
figure is not lost, and does not judge it. Issue #264 carries the question.
"""

from __future__ import annotations

import pytest
from cataclysm_sim import affixes as af
from cataclysm_sim import character as ch

LEVER_STATS = ("dot_damage", "dot_frequency", "dot_duration")


# --------------------------------------------------------------------------
# The stats exist and have the right shape
# --------------------------------------------------------------------------

@pytest.mark.parametrize("stat", LEVER_STATS)
def test_each_lever_is_a_stat_on_the_character_sheet(stat):
    assert stat in ch.ALL_STATS
    assert stat in ch.STAT_GROUPS["Offense"], (
        f"{stat} raises damage dealt, so it belongs to the Offense group with "
        "the other stats that do")


@pytest.mark.parametrize("stat", LEVER_STATS)
def test_each_lever_baselines_at_one_hundred_per_cent(stat):
    """Zero would be silent and total. Each lever is a percentage of what the
    effect itself does, and the three multiply, so a zero on any one of them
    takes the whole product to zero rather than only its own third."""
    assert ch.DEFAULT_STAT_LINE[stat].base == pytest.approx(100.0)


@pytest.mark.parametrize("stat", LEVER_STATS)
def test_a_character_with_no_gear_is_at_the_baseline_on_every_lever(stat):
    """The end-to-end version of the test above: read it off a real sheet
    rather than off the table the sheet is built from."""
    assert ch.Character(ch.GENERIC, level=1).stat(stat) == pytest.approx(100.0)


def test_the_three_levers_multiply_rather_than_adding():
    """The rule the whole pricing rests on. A character with 48% on each of the
    three deals 324% of base, not 148%. The design document states this figure
    and `tools/tests/test_ailment_magnitude_rule.py` checks that it does; this
    checks that the model a sheet is built from agrees."""
    lots = ch.Character(
        ch.GENERIC, level=1,
        gear=ch.Gear(increased={s: 0.48 for s in LEVER_STATS}))
    product = 1.0
    for stat in LEVER_STATS:
        product *= lots.stat(stat) / 100.0
    assert product == pytest.approx(1.48 ** 3)
    assert product == pytest.approx(3.24, abs=0.005)


# --------------------------------------------------------------------------
# The affixes exist and are priced together
# --------------------------------------------------------------------------

def test_there_is_one_affix_for_each_of_the_three_levers():
    assert len(af.DOT_LEVER_AFFIXES) == 3
    assert {a.stat for a in af.DOT_LEVER_AFFIXES} == set(LEVER_STATS)
    for affix in af.DOT_LEVER_AFFIXES:
        assert affix in af.AFFIX_POOL, (
            f"{affix.name} is not in AFFIX_POOL, so nothing can roll it")


@pytest.mark.parametrize("affix", af.DOT_LEVER_AFFIXES, ids=lambda a: a.stat)
def test_every_lever_affix_costs_the_same(affix):
    """A build has to buy all three to get the multiplying total, so a cheaper
    one would be the only one worth rolling."""
    assert affix.top_value == af.DOT_LEVER_TOP_VALUE


@pytest.mark.parametrize("affix", af.DOT_LEVER_AFFIXES, ids=lambda a: a.stat)
def test_every_lever_affix_rolls_where_offensive_affixes_roll(affix):
    assert affix.allowed_slots == af.OFFENSIVE_SLOTS
    assert affix.position == af.SUFFIX
    assert affix.kind == "increased", (
        "each lever is a percentage of what the effect does and baselines at "
        "100, so an increase is the only shape that fits. A flat version would "
        "have nothing to add to.")


def test_the_shipped_value_is_the_solved_value_rounded():
    """`DOT_LEVER_TOP_VALUE` is a whole number because a player reads it off an
    item. This is what stops the rounding drifting away from the solve."""
    exact = af.dot_lever_top_value()
    assert exact == pytest.approx(52.041, abs=0.001)
    assert af.DOT_LEVER_TOP_VALUE == round(exact)
    assert abs(af.DOT_LEVER_TOP_VALUE - exact) < 0.5


def test_six_slots_of_levers_match_six_slots_of_increased_damage():
    """The whole derivation, checked end to end at the reference build.

    `REFERENCE_INCREASED_DAMAGE_AFFIXES` is the number of slots the damage
    numbers elsewhere in `affixes.py` are fitted against, so it is the point the
    two builds have to agree at.
    """
    slots = af.REFERENCE_INCREASED_DAMAGE_AFFIXES
    direct = 1.0 + slots * af.INCREASED_DAMAGE.top_value / 100.0
    over_time = (1.0 + (slots / af.DOT_LEVERS)
                 * af.DOT_LEVER_TOP_VALUE / 100.0) ** af.DOT_LEVERS
    assert direct == pytest.approx(8.5)
    assert over_time == pytest.approx(direct, rel=0.005), (
        f"{slots} affix slots give a direct-hit build x{direct:.3f} and a "
        f"damage over time build x{over_time:.3f}. The two are meant to be "
        "equal at this slot count; that is where the 52% comes from.")
    assert over_time <= direct, (
        "the rounding is meant to leave the damage over time build very "
        "slightly behind rather than ahead, because these levers compound")


def test_the_solve_moves_if_increased_damage_moves(monkeypatch):
    """The value is derived, not typed. Prove that by moving what it derives
    from and watching the answer follow. A constant that ignores its inputs
    is a typed number with a formula next to it."""
    before = af.dot_lever_top_value()
    doubled = af.StatAffix("Increased damage", "attack_damage", "increased",
                           af.INCREASED_DAMAGE.top_value * 2,
                           af.OFFENSIVE_SLOTS, af.PREFIX)
    monkeypatch.setattr(af, "INCREASED_DAMAGE", doubled)
    after = af.dot_lever_top_value()
    assert after > before, (
        "doubling Increased Damage left the damage over time levers where they "
        "were, so dot_lever_top_value() is not reading it")
    # A direct-hit build now reaches 1 + 6 x 2.5 = 16, so each lever has to
    # reach the cube root of that.
    assert (1 + 2 * after / 100) ** 3 == pytest.approx(16.0)


# --------------------------------------------------------------------------
# What the equal-value pricing does NOT promise
# --------------------------------------------------------------------------

def test_the_two_curves_cross_once_and_the_gap_is_recorded():
    """Measured, not judged. An additive bracket and a product of three
    brackets cross exactly once, so equality at six slots means the damage over
    time build is behind below it and ahead above it. These are the figures the
    design document quotes; this is what stops them being repeated from memory
    once a value moves."""
    def direct(slots: float) -> float:
        return 1.0 + slots * af.INCREASED_DAMAGE.top_value / 100.0

    def over_time(slots: float) -> float:
        return (1.0 + (slots / af.DOT_LEVERS)
                * af.DOT_LEVER_TOP_VALUE / 100.0) ** af.DOT_LEVERS

    assert over_time(3) < direct(3), "below the reference build, behind"
    assert over_time(12) / direct(12) == pytest.approx(1.83, abs=0.02)
    assert over_time(18) / direct(18) == pytest.approx(2.98, abs=0.02)


def test_efficacy_drives_one_lever_and_only_one():
    """One attribute point buying three multiplying increases would make
    Efficacy strictly the best attribute for any damage over time build, and no
    other attribute compounds that way."""
    driven = [s for s in LEVER_STATS if s in ch.ATTRIBUTE_EFFECTS["efficacy"]]
    assert driven == ["dot_frequency"], (
        f"Efficacy drives {driven}. It is meant to drive exactly one of the "
        "three damage over time levers; driving two or three would compound "
        "within a single attribute.")
    for name, effects in ch.ATTRIBUTE_EFFECTS.items():
        overlap = set(effects) & set(LEVER_STATS)
        assert len(overlap) <= 1, (
            f"the {name} attribute drives {sorted(overlap)}, which multiply "
            "each other, so one point in it is worth more than one point in "
            "any attribute that does not compound")


def test_efficacy_is_worth_more_to_damage_over_time_than_ferocity_is_to_a_hit():
    """Why Efficacy's 1% per point was left alone when the affix moved from 12%
    to 52%. Scaling the attribute by the same factor would put it at 4.33% per
    point, and it is already ahead of the direct-hit damage attribute at 1%."""
    points = 100
    per_point = ch.ATTRIBUTE_EFFECTS["efficacy"]["dot_frequency"]
    efficacy = 1.0 + per_point * points

    ferocity = ch.ATTRIBUTE_EFFECTS["ferocity"]
    base_chance = ch.DEFAULT_SKILL_CRIT_CHANCE / 100.0
    base_multiplier = ch.DEFAULT_STAT_LINE["crit_multiplier"].base / 100.0

    def expected_hit(pts: int) -> float:
        chance = base_chance * (1 + ferocity["crit_chance"] * pts)
        multiplier = base_multiplier * (1 + ferocity["crit_multiplier"] * pts)
        return (1 - chance) + chance * multiplier

    ferocity_gain = expected_hit(points) / expected_hit(0)

    assert efficacy == pytest.approx(2.0)
    assert ferocity_gain == pytest.approx(1.561, abs=0.005)
    assert efficacy > ferocity_gain, (
        "100 points of Efficacy are worth less to a damage over time build "
        "than 100 points of Ferocity are to a direct-hit build. That was the "
        "reason for leaving Efficacy at 1% per point; if it is no longer true, "
        "the attribute needs re-pricing and issue #258 should be reopened.")
