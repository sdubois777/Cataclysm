"""The shape keys the Death weapon skills' second slice added, and where the generator refuses them. Issue #2885.

Slice DS2 built the Death draft's N1 (`Permanent=1`, a raise that stands until it is destroyed), N3 (`Detonates=1`, the
caster's minions in the skill's area explode), N4 (`CommandStrike=1`, until then a projectile's alone, on a Strike) and
N11 (`MinionsLayEffect=1`, a summon's minions lay its `Effect` with their blows). N2 has no key: a Summon row stating a
Damage Percent of its own strikes as it rises, and lays its `Effect` with that blow. N16 widened `SKILL_ROW_AILMENTS`,
which `test_stalker_kit_shape_keys.py` holds.

EACH KEY IS SHOWN BOTH WAYS: accepted where the engine reads it, and refused where a row stating it would run and
quietly do something else -- which is the failure `parse_shape_params` exists to prevent. The C++ parser reading each
key is `test_the_cpp_reads_every_shape_parameter.py`'s to check.
"""

from __future__ import annotations

import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

WHERE = "Weapon Skills row 9 (Death Greatsword Ultimate)"


def parse(text: str, shape: str) -> dict[str, str]:
    return gen.parse_shape_params(text, shape, WHERE)


def refused(text: str, shape: str) -> str:
    with pytest.raises(gen.DataError) as raised:
        parse(text, shape)
    return str(raised.value)


# --------------------------------------------------------------------------
# N1 Permanent


def test_a_permanent_raise_is_accepted_on_a_summon():
    """Barrow Guard's cell, word for word."""
    assert parse("Count=2; Radius=5; Minions=BarrowGuard:2; FervourReserve=10; Permanent=1; Effect=Cripple; "
                 "EffectMagnitude=30; EffectDuration=4; Taunts=1", "Summon")["Permanent"] == "1"


@pytest.mark.parametrize("clash", ["Duration=8", "Interval=0.5; Duration=4", "Possess=1"])
def test_a_permanent_raise_with_a_lifespan_or_a_possession_is_refused(clash):
    """A lifespan the key would take away, a rift's interval, or a thrall Subjugate already holds for good."""
    said = refused(f"Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; Permanent=1; {clash}", "Summon")
    assert "Permanent=1" in said


def test_a_permanent_key_switched_off_beside_a_duration_passes():
    """THE CONTROL for the refusal above: nought is the key switched off, and a summon may last its Duration."""
    assert parse("Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; Permanent=0; Duration=8", "Summon")


@pytest.mark.parametrize("shape, text", [
    ("Deployable", "Count=1; Duration=5; Minions=BoltTurret:1; FervourReserve=5; Permanent=1"),
    ("Strike", "Radius=3; Angle=90; Permanent=1"),
])
def test_permanent_is_a_summons_alone(shape, text):
    assert "has no parameter 'Permanent'" in refused(text, shape)


# --------------------------------------------------------------------------
# N2: a Summon's Effect needs something to lay it


@pytest.mark.parametrize("params, damage", [
    ({"Effect": "Cripple"}, "300"),
    ({}, ""),
])
def test_a_summons_effect_is_laid_by_its_rising_blow_or_its_minions(params, damage):
    """Barrow Guard (its 300% rising blow), and a summon naming no effect at all."""
    gen.check_a_summon_effect_has_a_carrier(params, "Summon", damage, WHERE)


@pytest.mark.parametrize("params, damage", [
    ({"Effect": "Cripple"}, ""),
    ({"Effect": "Cripple"}, "0"),
    ({"Effect": "Cripple", "Possess": "1"}, "300"),
])
def test_a_summons_effect_that_nothing_lays_is_refused(params, damage):
    with pytest.raises(gen.DataError, match="nothing lays it"):
        gen.check_a_summon_effect_has_a_carrier(params, "Summon", damage, WHERE)


def test_an_effect_on_any_other_shape_is_not_this_checks_to_judge():
    """THE CONTROL: a Strike naming an effect and no damage of its own is the slot's figure and lays it with its swing."""
    gen.check_a_summon_effect_has_a_carrier({"Effect": "Cripple"}, "Strike", "", WHERE)
