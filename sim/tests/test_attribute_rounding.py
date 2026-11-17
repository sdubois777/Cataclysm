"""An attribute is a whole number of points, rounded to the nearest.

WHAT THIS IS FOR. Issue #225. Each of the eight primary attributes has one
affix and it is a percentage increase, so a character with 33 Spirit and a
top-tier +12% Spirit affix arrives at 36.96. Nothing said whether that was 36,
37 or 36.96.

The project owner answered on 2026-08-05: round to the nearest whole number,
because players of this genre check the arithmetic and a displayed number that
is not the number the maths used sends them looking for a bug.

THE ROUNDING IS IN THE MATHS, NOT ONLY THE DISPLAY, and that is the part worth
guarding. A screen showing 37 while the calculation keeps 36.96 is exactly the
failure the answer was given to prevent, so the tests below check the value the
stats are computed from and not a formatted string.

WHAT ALSO HAD TO BE BUILT FOR THE RULE TO HAVE ANYWHERE TO LIVE. Before this,
`sim/cataclysm_sim/character.py` could not represent an attribute affix at all:
`Gear(increased={"agility": 0.12})` raised, because the validation admitted only
character-sheet stats, and the eight attributes are not stats. So the eight
affixes added by pull request #224 existed in the affix pool and reached no
character. `test_the_affix_reaches_the_stats_the_attribute_drives` is the check
that they now do.
"""

from __future__ import annotations

import pytest
from cataclysm_sim import character as ch


# --------------------------------------------------------------------------
# The rounding rule itself
# --------------------------------------------------------------------------

@pytest.mark.parametrize("raw,expected", [
    (36.96, 37),
    (36.4, 36),
    (4.48, 4),
    (0.0, 0),
    (33.0, 33),
])
def test_it_rounds_to_the_nearest_whole_number(raw, expected):
    assert ch.attribute_points(raw) == expected


@pytest.mark.parametrize("raw,expected", [
    (0.5, 1),
    (4.5, 5),
    (36.5, 37),
    (37.5, 38),
])
def test_a_half_rounds_up_rather_than_to_even(raw, expected):
    """Python's built-in `round` rounds a half to the nearest EVEN number, so it
    gives 4 for 4.5 and 36 for 36.5. A player reads "nearest whole number" as
    4.5 becoming 5. Halves are reachable: 10% of 5 is 5.5."""
    assert ch.attribute_points(raw) == expected


def test_the_built_in_round_really_would_disagree():
    """Proof that spelling the rule out is not redundant. If this ever stops
    disagreeing, `attribute_points` could be replaced by `round` and this file
    would be the place that says so."""
    disagreeing = [raw for raw in (0.5, 4.5, 36.5)
                   if round(raw) != ch.attribute_points(raw)]
    assert disagreeing == [0.5, 4.5, 36.5], (
        "the built-in round now agrees with attribute_points on every half "
        "tested, so the reason for a separate function has changed")


def test_the_result_is_an_integer_and_not_a_float_that_looks_like_one():
    value = ch.attribute_points(36.96)
    assert isinstance(value, int)
    assert not isinstance(value, float)


def test_a_negative_attribute_is_refused():
    """Nothing in the design reduces an attribute, so a negative one means a
    caller has made an error rather than a character being weakened."""
    with pytest.raises(ValueError, match="cannot be negative"):
        ch.attribute_points(-1.0)


# --------------------------------------------------------------------------
# The rule applied through a character
# --------------------------------------------------------------------------

def test_an_attribute_affix_raises_the_attribute_and_the_result_is_whole():
    """The worked example from the issue: 33 Spirit, +12%, 36.96, so 37."""
    character = ch.Character(
        ch.GENERIC, level=50, attributes=ch.Attributes(spirit=33),
        gear=ch.Gear(increased={"spirit": 0.12}))
    assert 33 * 1.12 == pytest.approx(36.96)
    assert character.attribute("spirit") == 37


def test_a_character_with_no_attribute_affix_has_exactly_what_it_allocated():
    character = ch.Character(ch.GENERIC, level=50,
                             attributes=ch.Attributes(spirit=33))
    assert character.attribute("spirit") == 33


def test_every_attribute_is_whole_on_the_whole_line():
    character = ch.Character(
        ch.GENERIC, level=100,
        attributes=ch.Attributes(agility=11, ferocity=13, constitution=7,
                                 vitality=17, mind=19, spirit=23, efficacy=5,
                                 luck=3),
        gear=ch.Gear(increased={name: 0.12 for name in ch.ATTRIBUTE_NAMES}))
    line = character.attribute_line()
    assert set(line) == set(ch.ATTRIBUTE_NAMES)
    for name, value in line.items():
        assert isinstance(value, int), f"{name} is {value!r}, not a whole number"


def test_the_affix_reaches_the_stats_the_attribute_drives():
    """Not the attribute in isolation: the point of raising Vitality is more
    health. This is the end-to-end check that an attribute affix does anything
    at all, which before this change it could not, because gear could not name
    an attribute."""
    plain = ch.Character(ch.GENERIC, level=60,
                         attributes=ch.Attributes(vitality=33))
    geared = ch.Character(ch.GENERIC, level=60,
                          attributes=ch.Attributes(vitality=33),
                          gear=ch.Gear(increased={"vitality": 0.12}))

    assert geared.attribute("vitality") == 37
    assert plain.attribute("vitality") == 33
    assert geared.stat("max_health") > plain.stat("max_health")

    # Vitality grants 2% increased health per point, so the gap is exactly the
    # four extra points' worth and nothing else.
    per_point = ch.ATTRIBUTE_EFFECTS["vitality"]["max_health"]
    base = plain.base("max_health")
    assert geared.stat("max_health") - plain.stat("max_health") == pytest.approx(
        base * per_point * 4)


def test_the_stat_is_computed_from_the_rounded_attribute_not_the_fraction():
    """The whole reason the rule is in the maths. If the model kept 36.96 and
    only the character screen rounded, health here would sit between the two
    whole-numbered characters instead of matching one of them exactly."""
    geared = ch.Character(ch.GENERIC, level=60,
                          attributes=ch.Attributes(vitality=33),
                          gear=ch.Gear(increased={"vitality": 0.12}))
    as_if_whole = ch.Character(ch.GENERIC, level=60,
                               attributes=ch.Attributes(vitality=37))
    assert geared.stat("max_health") == pytest.approx(
        as_if_whole.stat("max_health")), (
        "a character showing 37 Vitality does not have the health of a "
        "character with 37 Vitality, which is the mismatch issue #225 was "
        "filed to prevent")


def test_an_affix_on_a_lightly_invested_attribute_is_weak_but_not_zero():
    """Why flooring was rejected. +12% of 4 is 4.48; flooring gives 4 and the
    affix is worth exactly nothing. Rounding gives 4 here too -- but 5 points
    reach 6, so the affix starts paying at a low investment rather than being
    dead below a threshold."""
    def spirit(points: int) -> int:
        return ch.Character(ch.GENERIC, level=50,
                            attributes=ch.Attributes(spirit=points),
                            gear=ch.Gear(increased={"spirit": 0.12})
                            ).attribute("spirit")

    assert spirit(4) == 4
    assert spirit(5) == 6, (
        "5 Spirit with a +12% affix is 5.6, which rounds to 6. If this is 5, "
        "the model is flooring, which the design rejected")


# --------------------------------------------------------------------------
# What gear may name
# --------------------------------------------------------------------------

def test_gear_may_increase_an_attribute():
    gear = ch.Gear(increased={"agility": 0.12})
    assert gear.attribute_increases() == {"agility": 0.12}


def test_attribute_increases_separates_attributes_from_stats():
    gear = ch.Gear(increased={"agility": 0.12, "max_health": 0.12})
    assert gear.attribute_increases() == {"agility": 0.12}


@pytest.mark.parametrize("table", ["flat", "weapon_base"])
def test_gear_may_not_grant_an_attribute_flat_or_from_a_weapon(table):
    """There is no flat attribute affix -- the project owner decided that -- and
    a weapon supplies a base for the stats it owns, which no attribute is."""
    with pytest.raises(ValueError, match="not on the character sheet"):
        ch.Gear(**{table: {"agility": 5.0}})


def test_gear_still_refuses_a_name_that_is_neither_a_stat_nor_an_attribute():
    """Widening the check to admit attributes must not have widened it to admit
    anything. A typo has to stay an error."""
    with pytest.raises(ValueError, match="not on the character sheet"):
        ch.Gear(increased={"agilty": 0.12})


def test_no_attribute_shares_a_name_with_a_stat():
    """`Gear.increased` holds both, so a collision would make one silently
    become the other."""
    assert not set(ch.ATTRIBUTE_NAMES) & set(ch.ALL_STATS)


def test_asking_for_an_attribute_that_does_not_exist_is_refused():
    character = ch.Character(ch.GENERIC, level=50)
    with pytest.raises(KeyError, match="not one of the eight attributes"):
        character.attribute("max_health")


# --------------------------------------------------------------------------
# The C++ applies the same rule
# --------------------------------------------------------------------------

def test_the_cpp_rounds_attributes_too():
    """Nothing on a pull request compiles the C++, so the rule is read out of
    the source text. That catches the two ways it goes wrong: the rounding being
    removed, and it never being called."""
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    source = (root / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmPrimaryAttributeSet.cpp")
    if not source.is_file():
        pytest.skip(f"{source.name} is not present")
    text = source.read_text(encoding="utf-8")

    assert "float UCataclysmPrimaryAttributeSet::RoundedPoints(float Raw)" in text, (
        "the C++ no longer has a named rounding function for attribute points. "
        "The model rounds in character.attribute_points; if the game does not, "
        "the two disagree about every attribute-derived number. Issue #225.")
    assert "FMath::RoundToFloat" in text, (
        "RoundedPoints no longer rounds. RoundToFloat rounds a half away from "
        "zero, which is the round-half-up the design states.")
    assert "NewValue = RoundedPoints(NewValue);" in text, (
        "PreAttributeChange no longer calls RoundedPoints, so nothing applies "
        "the rounding to an attribute the game actually uses.")


def test_the_design_document_states_the_rule():
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    gdd = root / "docs" / "Cataclysm_GDD_v2.md"
    if not gdd.is_file():
        pytest.skip("the design document is not present")
    body = " ".join(gdd.read_text(encoding="utf-8").split())

    assert ("An attribute is always a whole number of points, rounded to the "
            "nearest.") in body, (
        "the design document no longer states the attribute rounding rule. It "
        "is the only place a reader who is not reading the code can find it.")
    assert "A half rounds up: 36.5 becomes 37." in body, (
        "the design document no longer says which way a half goes, which is "
        "the only part of 'nearest whole number' that is ambiguous.")
    assert "**The rounded number is the only number.**" in body, (
        "the design document no longer says the rounding is in the maths "
        "rather than only on the character screen. That distinction is the "
        "whole reason the project owner gave the answer they did.")
