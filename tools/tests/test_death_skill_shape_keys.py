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
# N11 MinionsLayEffect


def test_minions_that_lay_the_rows_effect_are_accepted_on_a_summon_that_names_one():
    """Bone Choir's cell, word for word."""
    assert parse("Range=12; Count=2; Radius=5; Minions=SkeletalMage:2; FervourReserve=10; Permanent=1; "
                 "Effect=Cripple; EffectMagnitude=20; EffectDuration=2; MinionsLayEffect=1",
                 "Summon")["MinionsLayEffect"] == "1"


def test_minions_told_to_lay_an_effect_the_row_does_not_name_are_refused():
    said = refused("Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; MinionsLayEffect=1", "Summon")
    assert "names no Effect" in said


def test_minions_that_lay_an_effect_on_a_summon_that_possesses_are_refused():
    said = refused("Range=15; MaxTargets=1; Possess=1; FervourReserve=30; HealthThresholdPercent=50; "
                   "Effect=Cripple; MinionsLayEffect=1", "Summon")
    assert "possesses" in said


# --------------------------------------------------------------------------
# N3 Detonates, and N4 CommandStrike on a strike


@pytest.mark.parametrize("shape, text", [
    ("Strike", "Radius=4.5; Angle=140; Ailment=Necrosis; Detonates=1; CommandStrike=1"),
    ("Projectile", "Range=14; Radius=1.2; Pierce=99; Speed=2400; Ailment=Necrosis; Detonates=1"),
    ("Movement", "Mode=Leap; Range=10; Radius=4; Effect=Cripple; EffectMagnitude=30; EffectDuration=3; Detonates=1"),
    ("Movement", "Mode=Recall; Range=14; Radius=3; EffectDuration=8; Knockback=3; Detonates=1"),
    ("Movement", "Mode=Charge; Range=12; Radius=2; Detonates=1"),
])
def test_a_detonation_is_accepted_where_a_single_blow_strikes_an_area(shape, text):
    """Gravebell's, Marrow Lance's, Barrow Leap's and Grave Return's cells, word for word, and a charge that arrives at
    once, whose line `UCataclysmMovementSkill::DetonateWhereItArrived` measures."""
    assert parse(text, shape)["Detonates"] == "1"


@pytest.mark.parametrize("text, what", [
    ("Mode=Flicker; Range=10; Duration=4; Interval=0.33; Detonates=1", "flicker"),
    ("Mode=Charge; Range=12; Duration=1; Radius=1.5; Detonates=1", "walked charge"),
])
def test_a_detonation_on_a_move_that_strikes_step_by_step_is_refused(text, what):
    assert f"Detonates on a {what}" in refused(text, "Movement")


@pytest.mark.parametrize("shape, text", [
    ("Summon", "Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; Detonates=1"),
    ("Aura", "Radius=8; Duration=8; Interval=1; Detonates=1"),
    ("SelfBuff", "Duration=6; Detonates=1"),
    ("Debuff", "Range=12; Radius=5; Detonates=1"),
])
def test_a_detonation_on_a_shape_that_strikes_no_area_of_its_own_is_refused(shape, text):
    assert "has no parameter 'Detonates'" in refused(text, shape)


def test_a_command_strike_is_accepted_on_a_strike_and_still_on_compels_projectile():
    assert parse("Radius=3; Angle=120; CommandStrike=1", "Strike")["CommandStrike"] == "1"
    assert parse("Range=14; Radius=5; Pierce=0; Speed=0; Burn=1; CommandStrike=1", "Projectile")["CommandStrike"] == "1"


# --------------------------------------------------------------------------
# N2: a Summon's Effect needs something to lay it


@pytest.mark.parametrize("params, damage", [
    ({"Effect": "Cripple"}, "300"),
    ({"Effect": "Cripple", "MinionsLayEffect": "1"}, ""),
    ({"Effect": "Cripple", "MinionsLayEffect": "1"}, "300"),
    ({}, ""),
])
def test_a_summons_effect_is_laid_by_its_rising_blow_or_its_minions(params, damage):
    """Barrow Guard (its 300% rising blow), Bone Choir (both), and a summon naming no effect at all."""
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


# --------------------------------------------------------------------------
# Slice DS3: N6 KillFervour, N7 ScalingSource=Fervour, N8 HoldsFervour


@pytest.mark.parametrize("shape, text", [
    # Grim Swath, Last Toll, Restless Spirits, Hungering Soul, Pale Rider and Death Warrant, word for word.
    ("Strike", "Radius=4; Angle=160; MoreDamagePer=0.5; ScalingSource=Fervour; MaxDamagePercent=350; KillFervour=2"),
    ("Strike", "Radius=12; Angle=360; ForcedMovement=Pull, Knockdown; ForcedMovementDuration=1.5; "
               "Ailment=Necrosis; KillFervour=5"),
    ("Projectile", "Range=12; Radius=1; Speed=1800; Count=6; Duration=2; Interval=0.33; TargetMode=Nearest; "
                   "Ailment=Necrosis; KillFervour=1"),
    ("Projectile", "Range=14; Radius=1; Speed=2600; Ailment=Necrosis; OnDeath=Leap; OnDeathRange=8; KillFervour=2"),
    ("Movement", "Mode=Charge; Range=14; Duration=1.5; Radius=2.5; Immune=CrowdControl; Effect=Cripple; "
                 "EffectMagnitude=50; EffectDuration=4; KillFervour=5"),
    ("Debuff", "Range=15; Radius=1.5; MaxTargets=1; EffectDuration=10; Effect=Cripple; EffectMagnitude=30; "
               "OnDeath=SpreadDebuff; OnDeathRange=10; KillFervour=10"),
])
def test_kill_fervour_is_accepted_on_the_shapes_whose_kills_are_their_own(shape, text):
    assert float(parse(text, shape)["KillFervour"]) > 0.0


@pytest.mark.parametrize("shape, text", [
    ("Summon", "Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; KillFervour=2"),
    ("Deployable", "Count=1; Duration=5; Minions=BoltTurret:1; FervourReserve=5; KillFervour=2"),
    ("SelfBuff", "Duration=6; KillFervour=2"),
    ("Aura", "Radius=8; Duration=8; Interval=1; KillFervour=2"),
])
def test_kill_fervour_on_a_shape_whose_kills_are_something_elses_is_refused(shape, text):
    """A summon's and a machine's kills are their minions', a buff and an aura pay for none: refused by name."""
    assert "has no parameter 'KillFervour'" in refused(text, shape)


def test_kill_fervour_below_nought_is_refused():
    assert "below nought" in refused("Radius=3; Angle=90; KillFervour=-2", "Strike")


def test_a_curse_paying_kill_fervour_without_marking_the_death_is_refused():
    """A Support-slot curse strikes no blow of its own, so its Fervour is for a death it marks, which only
    `OnDeath=SpreadDebuff` does."""
    said = refused("Range=15; Radius=1.5; MaxTargets=1; EffectDuration=10; Effect=Cripple; KillFervour=10", "Debuff")
    assert "OnDeath=SpreadDebuff" in said


def test_a_curse_stating_kill_fervour_at_nought_beside_no_mark_passes():
    """THE CONTROL for the refusal above: nought is the key switched off."""
    assert parse("Range=15; Radius=1.5; MaxTargets=1; EffectDuration=10; Effect=Cripple; KillFervour=0", "Debuff")


@pytest.mark.parametrize("shape, text", [
    ("Strike", "Radius=4; Angle=160; MoreDamagePer=0.5; ScalingSource=Fervour; MaxDamagePercent=350; KillFervour=2"),
    ("SelfBuff", "Duration=6; MoreDamagePer=0.25; ScalingSource=Fervour; HoldsFervour=1"),
])
def test_scaling_by_the_fervour_held_is_accepted_on_a_strike_and_a_self_buff(shape, text):
    """Grim Swath's and Cold Blood's cells, word for word."""
    assert parse(text, shape)["ScalingSource"] == "Fervour"


@pytest.mark.parametrize("shape, text", [
    ("Projectile", "Range=14; Radius=1; Speed=2400; MoreDamagePer=1; ScalingSource=Fervour"),
    ("Movement", "Mode=Leap; Range=10; Radius=3; MoreDamagePer=1; ScalingSource=Fervour"),
    ("Aura", "Radius=8; Duration=8; Interval=1; MoreDamagePer=1; ScalingSource=Fervour"),
])
def test_scaling_by_the_fervour_held_on_a_shape_that_does_not_read_it_is_refused(shape, text):
    assert "ScalingSource=Fervour" in refused(text, shape)


def test_holding_fervour_is_a_self_buffs_alone():
    assert parse("Duration=6; HoldsFervour=1", "SelfBuff")["HoldsFervour"] == "1"
    assert "has no parameter 'HoldsFervour'" in refused("Radius=3; Angle=90; HoldsFervour=1", "Strike")


# --------------------------------------------------------------------------
# Slice DS3: N10 a flicker its deaths lengthen, N5 a curse under which the dying rise

DANSE_MACABRE = ("Mode=Flicker; Range=10; Duration=4; Interval=0.33; RearHits=1; Untargetable=1; "
                 "DurationPer=0.5; ScalingSource=Kill; MaxDuration=8; KillFervour=5")


def test_a_flicker_its_deaths_lengthen_is_accepted():
    """Danse Macabre's cell, word for word."""
    params = parse(DANSE_MACABRE, "Movement")
    assert params["MaxDuration"] == "8" and params["DurationPer"] == "0.5" and params["ScalingSource"] == "Kill"


@pytest.mark.parametrize("text, said", [
    ("Mode=Charge; Range=12; Duration=1.5; Radius=2; DurationPer=0.5; ScalingSource=Kill", "not a flicker counting"),
    ("Mode=Flicker; Range=10; Duration=4; Interval=0.33; DurationPer=0.5", "not a flicker counting"),
    ("Mode=Flicker; Range=10; Duration=4; Interval=0.33; ScalingSource=Kill; MaxDuration=8", "nothing to cap"),
    ("Mode=Flicker; Range=10; Duration=4; Interval=0.33; DurationPer=0.5; ScalingSource=Kill; MaxDuration=3",
     "below the move's own Duration"),
])
def test_a_lengthening_a_move_would_not_read_is_refused(text, said):
    assert said in refused(text, "Movement")


def test_a_ceiling_is_a_moves_alone():
    assert "has no parameter 'MaxDuration'" in refused("Radius=3; Angle=90; MaxDuration=8", "Strike")


DEATH_RATTLE = ("Range=12; Radius=5; MaxTargets=8; EffectDuration=8; Effect=Cripple; EffectMagnitude=20; "
                "OnDeath=Rise; RisesAs=Husk")


def test_a_rising_curse_is_accepted_on_a_debuff():
    """Death Rattle's cell, with the `RisesAs=Husk` slice DS3 adds to name the husk its sentence names."""
    params = parse(DEATH_RATTLE, "Debuff")
    assert params["OnDeath"] == "Rise" and params["RisesAs"] == "Husk"


@pytest.mark.parametrize("shape, text, said", [
    ("Strike", "Radius=3; Angle=90; Effect=Cripple; OnDeath=Rise", "only a Debuff"),
    ("Debuff", "Range=12; Radius=5; Effect=Cripple; OnDeath=Rise", "no RisesAs"),
    ("Debuff", "Range=12; Radius=5; Effect=Cripple; RisesAs=Husk", "not OnDeath=Rise"),
    ("Debuff", "Range=12; Radius=5; Effect=Cripple; OnDeath=Rise; RisesAs=Two Husks", "not one Minion Types"),
    ("Debuff", "Range=12; Radius=5; OnDeath=Rise; RisesAs=Husk", "no curse"),
])
def test_a_rising_curse_that_could_raise_nothing_is_refused(shape, text, said):
    assert said in refused(text, shape)


def rise_tables(*summons: tuple[str, str, str]) -> dict[str, list[dict]]:
    """A weapon skill table with Death Rattle and the given Summon rows (name, damage type, cell), and the minion
    table `validate_risen_kinds` reads."""
    skills = [{"Name": "Death_Greataxe_Support", "DamageType": "Death", "Shape": "Debuff",
               "ShapeParams": DEATH_RATTLE}]
    skills += [{"Name": name, "DamageType": damage, "Shape": "Summon", "ShapeParams": cell}
               for name, damage, cell in summons]
    return {"WeaponSkills": skills, "MinionTypes": [{"Name": "Husk"}, {"Name": "Shade"}]}


SHALLOW_GRAVES = "Range=10; Radius=4; Count=4; Duration=8; Minions=Husk:4; FervourReserve=3; Taunts=1"


def test_a_risen_kind_one_summon_row_of_its_damage_type_raises_passes():
    assert gen.validate_risen_kinds(rise_tables(("Death_Greataxe_Special", "Death", SHALLOW_GRAVES))) == []


@pytest.mark.parametrize("summons, said", [
    ((), "0 Death Summon rows"),
    ((("Demonic_Staff_Special", "Demonic", SHALLOW_GRAVES),), "0 Death Summon rows"),
    ((("Death_Greataxe_Special", "Death", "Range=10; Radius=4; Count=4; Minions=Husk:4; FervourReserve=3"),),
     "0 Death Summon rows"),
    ((("Death_Greataxe_Special", "Death", SHALLOW_GRAVES), ("Death_Staff_Special", "Death", SHALLOW_GRAVES)),
     "2 Death Summon rows"),
])
def test_a_risen_kind_no_single_summon_row_of_its_damage_type_raises_is_refused(summons, said):
    """None at all; one of another damage type; one with no Duration, which raises nothing that lasts a stated time;
    and two, which would leave the engine choosing by table order."""
    problems = gen.validate_risen_kinds(rise_tables(*summons))
    assert len(problems) == 1 and said in problems[0], problems


def test_a_risen_kind_the_minion_table_lacks_is_refused():
    tables = rise_tables(("Death_Greataxe_Special", "Death", SHALLOW_GRAVES))
    tables["WeaponSkills"][0]["ShapeParams"] = DEATH_RATTLE.replace("RisesAs=Husk", "RisesAs=Ghoul")
    problems = gen.validate_risen_kinds(tables)
    assert len(problems) == 1 and "no row in the Minion Types sheet" in problems[0], problems
