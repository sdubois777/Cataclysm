"""The shape keys the Death weapon skills' second slice added, and where the generator refuses them. Issue #2885.

Slice DS2 built the Death draft's N1 (`Permanent=1`, a raise that stands until it is destroyed), N3 (`Detonates=1`, the
caster's minions in the skill's area explode), N4 (`CommandStrike=1`, until then a projectile's alone, on a Strike) and
N11 (`MinionsLayEffect=1`, a summon's minions lay its `Effect` with their blows). N2 has no key: a Summon row stating a
Damage Percent of its own strikes as it rises, and lays its `Effect` with that blow. N16 widened `SKILL_ROW_AILMENTS`,
which `test_stalker_kit_shape_keys.py` holds. Slice DS3 added N5, N6, N7, N8 and N10's keys, and slice DS4 N13's
`SpendsAllFervour` with `ScalingSource=FervourSpent` and N15's `CashInRadius` on a Strike (N9, a strike's refund, is the
`RefundsCooldown` rider and has no key of its own), N12's `CollapseBursts` on a Summon, and N17's `EveryBlow` and N21's
`MeleeReachMetres` on a SelfBuff. Slice DS5 added N14's `OnDeath=Burst` with `BurstPercentOfMaximumHealth` on a Strike,
N19's `HitFervour` and `HitFervourPerCast` and the Crossbow draft's C1 `CritChanceAgainst` and `CritChanceAgainstMax` on
a flying Projectile, C2's `StrikesOnCritical` on a SelfBuff, and N18's refusal of `Untargetable` on anything but a
flicker and a walked charge.

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


# --------------------------------------------------------------------------
# Slice DS4: N9 a strike's refund, N13 SpendsAllFervour and ScalingSource=FervourSpent, N15 CashInRadius

REQUIEM = ("Radius=14; Angle=360; Effect=Cripple; EffectMagnitude=40; EffectDuration=6; SpendsAllFervour=1; "
           "MoreDamagePer=1; ScalingSource=FervourSpent; MaxDamagePercent=700")


def test_a_strike_returning_its_own_cooldown_is_accepted():
    """Mercy Stroke's cell, word for word. N9 has no key of its own: `RefundsCooldown` is a rider every shape may state,
    and a strike reads it since slice DS4."""
    assert parse("Radius=2.5; Angle=60; MaxTargets=1; RefundsCooldown=Self", "Strike")["RefundsCooldown"] == "Self"


def test_a_strike_spending_every_fervour_and_scaling_by_what_it_spent_is_accepted():
    """Requiem's cell, word for word."""
    params = parse(REQUIEM, "Strike")
    assert params["SpendsAllFervour"] == "1" and params["ScalingSource"] == "FervourSpent"


@pytest.mark.parametrize("shape, text", [
    ("Strike", "Radius=14; Angle=360; MoreDamagePer=1; ScalingSource=FervourSpent"),
    ("Strike", "Radius=14; Angle=360; SpendsAllFervour=0; MoreDamagePer=1; ScalingSource=FervourSpent"),
    ("Projectile", "Range=14; Radius=1; Speed=2400; MoreDamagePer=1; ScalingSource=FervourSpent"),
])
def test_scaling_by_the_fervour_spent_without_spending_it_is_refused(shape, text):
    """Nothing is spent, so the scaling would count a nought it never moves. A projectile cannot spend at all."""
    assert "SpendsAllFervour=1" in refused(text, shape)


def test_spending_every_fervour_alone_passes():
    """THE CONTROL for the refusal above: a row may spend and scale by nothing, and the key is a strike's to state."""
    assert parse("Radius=14; Angle=360; SpendsAllFervour=1", "Strike")["SpendsAllFervour"] == "1"


@pytest.mark.parametrize("shape, text", [
    ("Projectile", "Range=14; Radius=1; Speed=2400; SpendsAllFervour=1"),
    ("SelfBuff", "Duration=6; SpendsAllFervour=1"),
    ("Movement", "Mode=Leap; Range=10; Radius=3; SpendsAllFervour=1"),
])
def test_spending_every_fervour_is_a_strikes_alone(shape, text):
    assert "has no parameter 'SpendsAllFervour'" in refused(text, shape)


def test_a_strike_cashing_in_its_casters_ailments_is_accepted():
    """Quietus's cell, word for word."""
    assert parse("Radius=2.5; Angle=60; MaxTargets=1; CashInRadius=10", "Strike")["CashInRadius"] == "10"


@pytest.mark.parametrize("radius", ["0", "-2"])
def test_a_cash_in_that_reaches_nobody_is_refused(radius):
    assert "reaches nobody" in refused(f"Radius=2.5; Angle=60; MaxTargets=1; CashInRadius={radius}", "Strike")


@pytest.mark.parametrize("shape, text", [
    ("Projectile", "Range=14; Radius=1; Speed=2400; CashInRadius=10"),
    ("Debuff", "Range=12; Radius=5; Effect=Cripple; CashInRadius=10"),
])
def test_a_cash_in_is_a_strikes_alone(shape, text):
    assert "has no parameter 'CashInRadius'" in refused(text, shape)


# --------------------------------------------------------------------------
# Slice DS4: N12 CollapseBursts, N17 EveryBlow, N21 MeleeReachMetres

GRAVE_MOUTH = ("Range=12; Radius=3; Count=1; Duration=4; Interval=0.5; MaxActive=8; Minions=Wisp:1; "
               "FervourReserve=2; CollapseBursts=1")


def test_a_rift_that_bursts_what_it_raised_as_it_closes_is_accepted():
    """Grave Mouth's cell, word for word."""
    assert parse(GRAVE_MOUTH, "Summon")["CollapseBursts"] == "1"


@pytest.mark.parametrize("text", [
    "Range=12; Radius=3; Count=1; Minions=Wisp:1; FervourReserve=2; CollapseBursts=1",
    "Range=12; Radius=3; Count=1; Duration=4; Minions=Wisp:1; FervourReserve=2; CollapseBursts=1",
    "Range=12; Radius=3; Count=1; Interval=0.5; Minions=Wisp:1; FervourReserve=2; CollapseBursts=1",
    "Count=2; Radius=5; Minions=BarrowGuard:2; FervourReserve=10; Permanent=1; CollapseBursts=1",
])
def test_bursting_what_was_raised_on_a_summon_that_never_closes_is_refused(text):
    """No rift: no Interval and Duration, one without the other, or a permanent raise, which stands until destroyed."""
    assert "is not a rift" in refused(text, "Summon")


def test_a_summon_that_is_no_rift_with_the_key_switched_off_passes():
    """THE CONTROL for the refusal above: nought is the key switched off."""
    assert parse("Range=10; Radius=3; Count=5; Duration=6; Minions=Shade:5; FervourReserve=3; CollapseBursts=0",
                 "Summon")["CollapseBursts"] == "0"


@pytest.mark.parametrize("shape, text", [
    ("Deployable", "Count=1; Duration=5; Interval=1; Minions=BoltTurret:1; FervourReserve=5; CollapseBursts=1"),
    ("Strike", "Radius=3; Angle=90; CollapseBursts=1"),
])
def test_bursting_at_a_close_is_a_summons_alone(shape, text):
    assert "has no parameter 'CollapseBursts'" in refused(text, shape)


@pytest.mark.parametrize("text, laid", [
    # Hemlock, Death Grip and Grim Reach, word for word.
    ("Duration=6; Ailment=Poison; EveryBlow=1", "Ailment"),
    ("Duration=6; ForcedMovement=Pull; ForcedMovementDistance=1; EveryBlow=1", "ForcedMovement"),
    ("Duration=10; MeleeReachMetres=3; Effect=Cripple; EffectMagnitude=30; EffectDuration=4; EveryBlow=1", "Effect"),
])
def test_a_buff_laying_its_riders_with_every_blow_is_accepted(text, laid):
    params = parse(text, "SelfBuff")
    assert params["EveryBlow"] == "1" and params[laid]


def test_a_buff_on_every_blow_that_lays_nothing_is_refused():
    assert "would lay nothing" in refused("Duration=6; EveryBlow=1", "SelfBuff")


def test_a_buff_laying_nothing_with_the_key_switched_off_passes():
    """THE CONTROL for the refusal above: nought is the key switched off."""
    assert parse("Duration=6; EveryBlow=0", "SelfBuff")["EveryBlow"] == "0"


@pytest.mark.parametrize("shape, text", [
    ("Strike", "Radius=3; Angle=90; Ailment=Poison; EveryBlow=1"),
    ("Aura", "Radius=8; Duration=8; Interval=1; Effect=Cripple; EveryBlow=1"),
])
def test_every_blow_is_a_self_buffs_alone(shape, text):
    assert "has no parameter 'EveryBlow'" in refused(text, shape)


def test_a_buff_lengthening_its_holders_melee_reach_is_accepted():
    assert parse("Duration=10; MeleeReachMetres=3", "SelfBuff")["MeleeReachMetres"] == "3"


@pytest.mark.parametrize("metres", ["0", "-1"])
def test_a_buff_adding_no_reach_is_refused(metres):
    assert "lengthens nothing" in refused(f"Duration=10; MeleeReachMetres={metres}", "SelfBuff")


def test_added_melee_reach_is_a_self_buffs_alone():
    """A strike's own reach is its `Radius`; the flat reach is what a running buff adds to it."""
    assert "has no parameter 'MeleeReachMetres'" in refused("Radius=3; Angle=90; MeleeReachMetres=3", "Strike")


# --------------------------------------------------------------------------
# Slice DS5: N14 OnDeath=Burst, N19 HitFervour, N18 Untargetable on a walked charge

GRAVETOUCH = ("Radius=2; Angle=60; MaxTargets=1; Ailment=Bleed; OnDeath=Burst; BurstPercentOfMaximumHealth=10; "
              "OnDeathRange=4")
SOUL_LANCE = "Range=14; Radius=1; Pierce=99; Speed=2400; HitFervour=1; HitFervourPerCast=4"
WRAITHWALK = ("Mode=Charge; Range=12; Duration=1; Radius=1.5; Untargetable=1; Effect=Cripple; EffectMagnitude=30; "
              "EffectDuration=3")


def test_a_wound_that_bursts_as_its_bearer_dies_is_accepted_on_a_strike():
    """Gravetouch's cell, word for word."""
    params = parse(GRAVETOUCH, "Strike")
    assert params["OnDeath"] == "Burst" and params["BurstPercentOfMaximumHealth"] == "10"


@pytest.mark.parametrize("text, said", [
    ("Radius=2; Angle=60; MaxTargets=1; OnDeath=Burst; BurstPercentOfMaximumHealth=10; OnDeathRange=4",
     "names no Ailment"),
    ("Radius=2; Angle=60; MaxTargets=1; Ailment=Bleed; OnDeath=Burst; OnDeathRange=4", "would deal nothing"),
    ("Radius=2; Angle=60; MaxTargets=1; Ailment=Bleed; OnDeath=Burst; BurstPercentOfMaximumHealth=0; OnDeathRange=4",
     "would deal nothing"),
    ("Radius=2; Angle=60; MaxTargets=1; Ailment=Bleed; OnDeath=Burst; BurstPercentOfMaximumHealth=10",
     "would reach nobody"),
])
def test_a_wound_burst_that_could_burst_nothing_is_refused(text, said):
    """No wound to bleed, no share to deal, or no reach: a burst that would do nothing."""
    assert said in refused(text, "Strike")


def test_a_burst_share_without_the_burst_is_refused():
    assert "not OnDeath=Burst" in refused(
        "Radius=2; Angle=60; Ailment=Bleed; BurstPercentOfMaximumHealth=10; OnDeathRange=4", "Strike")


def test_a_strike_laying_a_bleed_with_no_burst_passes():
    """THE CONTROL for the refusals above: Severing Rush's bleed, and a strike's, burst nothing and need no share."""
    assert parse("Radius=2; Angle=60; MaxTargets=1; Ailment=Bleed", "Strike")["Ailment"] == "Bleed"


def test_a_burst_share_is_a_strikes_alone():
    assert "has no parameter 'BurstPercentOfMaximumHealth'" in refused(
        "Range=12; Radius=2; Speed=2000; Ailment=Poison; OnDeath=Burst; BurstPercentOfMaximumHealth=10; "
        "OnDeathRange=4", "Projectile")


def test_a_shot_paying_fervour_for_each_enemy_hit_to_a_cap_is_accepted():
    """Soul Lance's cell, word for word."""
    params = parse(SOUL_LANCE, "Projectile")
    assert params["HitFervour"] == "1" and params["HitFervourPerCast"] == "4"


def test_a_shot_paying_fervour_with_no_cap_passes():
    """THE CONTROL for the cap's refusals: a cap is the row's to state, and a row stating none pays every hit."""
    assert parse("Range=14; Radius=1; Pierce=99; Speed=2400; HitFervour=1", "Projectile")["HitFervour"] == "1"


@pytest.mark.parametrize("text, said", [
    ("Range=14; Radius=1; Pierce=99; Speed=2400; HitFervour=0", "pays nothing"),
    ("Range=14; Radius=1; Pierce=99; Speed=2400; HitFervour=-1", "pays nothing"),
    ("Range=14; Radius=1; Pierce=99; HitFervour=1; HitFervourPerCast=4", "a beam"),
    ("Range=14; Radius=1; Pierce=99; Speed=2400; HitFervourPerCast=4", "no payment to cap"),
    ("Range=14; Radius=1; Pierce=99; Speed=2400; HitFervour=1; HitFervourPerCast=0", "would let no payment"),
])
def test_fervour_for_each_enemy_hit_that_would_pay_nothing_is_refused(text, said):
    """Nought or less a hit; a beam, whose landing is no flying contact; a cap with nothing to cap; a cap of nought."""
    assert said in refused(text, "Projectile")


@pytest.mark.parametrize("key", ["HitFervour=1", "HitFervourPerCast=4"])
def test_fervour_for_each_enemy_hit_is_a_projectiles_alone(key):
    assert f"has no parameter '{key.split('=')[0]}'" in refused(f"Radius=3; Angle=90; {key}", "Strike")


@pytest.mark.parametrize("text", [
    WRAITHWALK,
    # The Dagger's Everywhere at Once and the Sword's Danse Macabre, the two flickers that stated it before.
    "Mode=Flicker; Range=10; Duration=4; Interval=0.33; Burn=1; RearHits=1; Untargetable=1",
])
def test_an_untouchable_move_that_lasts_is_accepted(text):
    assert parse(text, "Movement")["Untargetable"] == "1"


@pytest.mark.parametrize("shape, text", [
    ("Movement", "Mode=Leap; Range=10; Radius=3; Untargetable=1"),
    ("Movement", "Mode=Charge; Range=12; Radius=2; Untargetable=1"),
    ("Movement", "Mode=Blink; Range=10; Radius=3; Untargetable=1"),
    ("SelfBuff", "Duration=6; Untargetable=1"),
    ("Strike", "Radius=3; Angle=90; Untargetable=1"),
])
def test_untargetable_on_a_move_or_shape_that_does_not_last_is_refused(shape, text):
    """A leap, a charge with no `Duration` and a blink arrive in the frame they start; no other shape reads it."""
    assert "untouchable for no time" in refused(text, shape)


def test_untargetable_switched_off_on_a_leap_passes():
    """THE CONTROL for the refusal above: nought is the key switched off."""
    assert parse("Mode=Leap; Range=10; Radius=3; Untargetable=0", "Movement")["Untargetable"] == "0"


# --------------------------------------------------------------------------
# Slice DS5: the Crossbow draft's C1 CritChanceAgainst and C2 StrikesOnCritical

COFFIN_NAIL = ("Range=14; Radius=0.4; Pierce=0; Speed=3000; CritChanceAgainst=5; CritChanceAgainstMax=20; "
               "EffectDuration=4")
CARRION_CROWS = "Duration=8; Radius=10; StrikesOnCritical=1"
CROSSBOW_SUPPORT = "Weapon Skills row 405 (Death Crossbow Support)"


def test_a_shot_driving_nails_is_accepted():
    """Coffin Nail's cell, word for word."""
    params = parse(COFFIN_NAIL, "Projectile")
    assert params["CritChanceAgainst"] == "5" and params["CritChanceAgainstMax"] == "20"


def test_a_shot_driving_nails_with_no_ceiling_passes():
    """THE CONTROL for the ceiling's refusals: the ceiling is the row's to state; with none every nail counts."""
    assert parse("Range=14; Radius=0.4; Speed=3000; CritChanceAgainst=5; EffectDuration=4",
                 "Projectile")["CritChanceAgainst"] == "5"


@pytest.mark.parametrize("text, said", [
    ("Range=14; Radius=0.4; Speed=3000; CritChanceAgainst=0; EffectDuration=4", "raises nothing"),
    ("Range=14; Radius=0.4; Speed=3000; CritChanceAgainst=-5; EffectDuration=4", "raises nothing"),
    ("Range=14; Radius=0.4; Speed=3000; CritChanceAgainst=5; CritChanceAgainstMax=4; EffectDuration=4",
     "not even one nail"),
    ("Range=14; Radius=0.4; Speed=3000; CritChanceAgainst=5; CritChanceAgainstMax=20", "no EffectDuration"),
    ("Range=14; Radius=0.4; CritChanceAgainst=5; CritChanceAgainstMax=20; EffectDuration=4", "a beam"),
    ("Range=14; Radius=0.4; Speed=3000; CritChanceAgainstMax=20; EffectDuration=4", "no nails to cap"),
])
def test_nails_that_would_raise_nothing_are_refused(text, said):
    """Nought or less a nail; a ceiling below one nail; nails that never lapse; a beam; a ceiling with no nails."""
    assert said in refused(text, "Projectile")


@pytest.mark.parametrize("key", ["CritChanceAgainst=5", "CritChanceAgainstMax=20"])
def test_nails_are_a_projectiles_alone(key):
    assert f"has no parameter '{key.split('=')[0]}'" in refused(
        f"Radius=3; Angle=90; EffectDuration=4; {key}", "Strike")


def test_a_buff_striking_again_on_each_critical_strike_is_accepted():
    """Carrion Crows' cell, word for word."""
    assert parse(CARRION_CROWS, "SelfBuff")["StrikesOnCritical"] == "1"


@pytest.mark.parametrize("shape, text", [
    ("Strike", "Radius=3; Angle=90; StrikesOnCritical=1"),
    ("Projectile", "Range=14; Radius=0.4; Speed=3000; StrikesOnCritical=1"),
])
def test_striking_on_a_critical_is_a_self_buffs_alone(shape, text):
    assert "has no parameter 'StrikesOnCritical'" in refused(text, shape)


def test_a_buff_striking_on_a_critical_that_states_no_damage_is_refused():
    """Its strikes are the row's own Damage Percent; left blank they would take the Support slot's nought."""
    with pytest.raises(gen.DataError, match="no Damage Percent"):
        gen.check_a_buff_striking_on_a_critical_states_its_damage(
            gen.parse_shape_params(CARRION_CROWS, "SelfBuff", CROSSBOW_SUPPORT), "", CROSSBOW_SUPPORT)


@pytest.mark.parametrize("text, damage", [
    (CARRION_CROWS, "40"),
    ("Duration=8; Radius=10; StrikesOnCritical=0", ""),
    ("Duration=6; Ailment=Poison; EveryBlow=1", ""),
])
def test_a_buff_striking_on_a_critical_with_its_damage_or_none_passes(text, damage):
    """THE CONTROL for the refusal above: Carrion Crows' own 40, the key switched off, and Hemlock, which strikes
    nothing of its own and needs no Damage Percent."""
    gen.check_a_buff_striking_on_a_critical_states_its_damage(
        gen.parse_shape_params(text, "SelfBuff", CROSSBOW_SUPPORT), damage, CROSSBOW_SUPPORT)


def test_the_crows_quarter_second_is_the_generators_trigger_cooldown():
    """C2's "at most four crows dive each second" is the quarter second every hit-fired row waits, as the Warhound's is
    (`test_warlords_will_six_rows.py`). THE CONTROL is the generator's own figure, read first."""
    import re

    assert gen.DEFAULT_TRIGGER_COOLDOWN == 0.25
    header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmSkillTemplates.h")
    (written,) = re.findall(r"CrowCooldownSeconds\s*=\s*([0-9.]+)f;", header.read_text(encoding="utf-8"))
    assert float(written) == gen.DEFAULT_TRIGGER_COOLDOWN
