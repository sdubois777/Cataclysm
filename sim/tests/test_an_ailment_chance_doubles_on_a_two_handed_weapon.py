"""An ailment chance rolled on a two-handed weapon is doubled, as in the game.

ISSUE #1575. The design document, in "A Two-Handed Weapon Is Worth Double, Per
Implicit and Per Affix": "A two-handed weapon multiplies both its implicit
values and every affix rolled on it by 2." The game does that for the ailment
affixes too: `UCataclysmItemValues::AffixValue` takes `bTwoHanded`, and the code
that turns an item's affixes into stats values an ailment chance with it.
`StatAffix.value_at` took `two_handed` and `AilmentAffix.chance_at` did not, so
the simulation gave a two-handed weapon the one-handed chance.

THE CONTROL is the one-handed figure, which must be exactly what it was: the
shared affix curve at that tier, roll and gear level, multiplied by nothing.
"""
from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af

AILMENT_AFFIXES = [v for v in vars(af).values() if isinstance(v, af.AilmentAffix)]


def test_there_are_eleven_ailment_affixes_to_check():
    """Guards the tests below from passing on an empty list."""
    assert len(AILMENT_AFFIXES) == 11


@pytest.mark.parametrize("affix", AILMENT_AFFIXES, ids=lambda a: a.name)
def test_a_two_handed_weapon_doubles_the_chance(affix):
    for tier in range(1, 8):
        for roll in (0.0, 0.5, 1.0):
            one = affix.chance_at(tier, roll)
            two = affix.chance_at(tier, roll, two_handed=True)
            assert two == pytest.approx(2.0 * one), (
                f"{affix.name} at T{tier}, roll {roll}: {two} on a two-handed "
                f"weapon against {one} on a one-handed one. Issue #1575.")


@pytest.mark.parametrize("affix", AILMENT_AFFIXES, ids=lambda a: a.name)
def test_control_a_one_handed_chance_is_the_curve_unchanged(affix):
    for tier in range(1, 8):
        for roll in (0.0, 0.5, 1.0):
            for gear_level in (0, af.MAX_GEAR_LEVEL):
                expected = af.affix_value(affix.top_chance, tier, roll, gear_level)
                assert affix.chance_at(tier, roll, gear_level) == expected
                assert affix.chance_at(tier, roll, gear_level,
                                       two_handed=False) == expected


def test_a_stat_affix_and_an_ailment_affix_read_one_constant(monkeypatch):
    """One multiplier for every affix on a two-handed weapon, read when the
    value is asked for. Moving the constant moves both, so the two cannot be
    given different factors by accident."""
    stat = next(v for v in vars(af).values() if isinstance(v, af.StatAffix))
    ailment = af.BLEED
    for factor in (af.TWO_HANDED_MULTIPLIER, 3.0):
        monkeypatch.setattr(af, "TWO_HANDED_MULTIPLIER", factor)
        assert stat.value_at(7, two_handed=True) == pytest.approx(
            factor * stat.value_at(7))
        assert ailment.chance_at(7, two_handed=True) == pytest.approx(
            factor * ailment.chance_at(7))
