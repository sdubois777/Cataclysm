"""An ailment chance rolled on a two-handed weapon is doubled, as in the game.

ISSUE #1575. The design document, in "A Two-Handed Weapon Is Worth Double, Per
Implicit and Per Affix": "A two-handed weapon multiplies both its implicit
values and every affix rolled on it by 2." The game does that for the ailment
affixes too: `UCataclysmItemValues::AffixValue` takes `bTwoHanded`, and the code
that turns an item's affixes into stats values an ailment chance with it.
`StatAffix.value_at` took `two_handed` and `AilmentAffix.chance_at` did not, so
the simulation gave a two-handed weapon the one-handed chance.

DOUBLED INSIDE THE ROUNDING since issue #2883, the whole-number affix ranges:
each end of the tier's +0 range is multiplied by the upgrade and by 2 together,
then rounded once (`affix_ranges.upgraded`). So a two-handed chance is not
exactly twice the one-handed one: a tier 1 bleed at +10 and a middle roll is 7
one-handed (5 to 8) and 13 two-handed (10 to 15), because the two-handed range
holds more whole numbers for the roll to land among. What holds is that each END of the range is
exactly doubled at +0, and the value is the same rule with two hands.

THE CONTROL is the one-handed figure, which must be the shared rule at that
tier, roll and gear level, with one hand.
"""
from __future__ import annotations

import pytest

from cataclysm_sim import affix_ranges
from cataclysm_sim import affixes as af

AILMENT_AFFIXES = [v for v in vars(af).values() if isinstance(v, af.AilmentAffix)]


def test_there_are_eleven_ailment_affixes_to_check():
    """Guards the tests below from passing on an empty list."""
    assert len(AILMENT_AFFIXES) == 11


@pytest.mark.parametrize("affix", AILMENT_AFFIXES, ids=lambda a: a.name)
def test_a_two_handed_weapon_doubles_the_chance(affix):
    for tier in range(1, 8):
        one_low, one_high = (affix.chance_at(tier, 0.0, 0), affix.chance_at(tier, 1.0, 0))
        two_low, two_high = (affix.chance_at(tier, 0.0, 0, two_handed=True),
                             affix.chance_at(tier, 1.0, 0, two_handed=True))
        assert (two_low, two_high) == (2 * one_low, 2 * one_high), (
            f"{affix.name} at T{tier}: the two-handed range at +0 is {two_low}-{two_high} "
            f"against {one_low}-{one_high} one-handed. Issue #1575.")
        for roll in (0.0, 0.5, 1.0):
            for gear_level in (0, 5, af.MAX_GEAR_LEVEL):
                assert affix.chance_at(tier, roll, gear_level, two_handed=True) == (
                    affix_ranges.value(affix.name, tier, roll, gear_level, two_handed=True))
                assert (affix.chance_at(tier, roll, gear_level, two_handed=True)
                        > affix.chance_at(tier, roll, gear_level))


@pytest.mark.parametrize("affix", AILMENT_AFFIXES, ids=lambda a: a.name)
def test_control_a_one_handed_chance_is_the_rule_unchanged(affix):
    for tier in range(1, 8):
        for roll in (0.0, 0.5, 1.0):
            for gear_level in (0, af.MAX_GEAR_LEVEL):
                expected = affix_ranges.value(affix.name, tier, roll, gear_level)
                assert affix.chance_at(tier, roll, gear_level) == expected
                assert affix.chance_at(tier, roll, gear_level,
                                       two_handed=False) == expected


def test_a_stat_affix_and_an_ailment_affix_read_one_constant(monkeypatch):
    """One multiplier for every affix on a two-handed weapon. Moving the
    constant moves both, so the two cannot be given different factors by
    accident. `affix_ranges.TWO_HANDED_FACTOR` is the one the rule reads, and
    `affixes.py` refuses to import if it differs from TWO_HANDED_MULTIPLIER."""
    stat = af.FLAT_DAMAGE
    ailment = af.BLEED
    for factor in (affix_ranges.TWO_HANDED_FACTOR, 3):
        monkeypatch.setattr(affix_ranges, "TWO_HANDED_FACTOR", factor)
        assert stat.value_at(7, 1.0, 0, two_handed=True) == factor * stat.value_at(7, 1.0, 0)
        assert ailment.chance_at(7, 1.0, 0, two_handed=True) == factor * ailment.chance_at(7, 1.0, 0)
