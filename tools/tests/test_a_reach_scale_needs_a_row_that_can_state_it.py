"""A scale counting enemies in a radius is refused where no row can state one.

WHY THIS IS A SEPARATE RULE FROM THE ONE BESIDE IT.
`test_every_scaled_stat_has_an_asker.py` holds the rule that a row may only
scale a stat something asks for. That rule looks at the row's STAT. This one
looks at the row's SCALE against the sheet it is written on, and the two do not
overlap: a row scaling `attack_damage` -- a stat plenty asks for -- by
`enemies_in_reach` on the Enchantment Effects sheet passes the stat rule and is
still dead.

WHY SUCH A ROW IS DEAD. The radius belongs to the row, not to the character, and
lives in a `Reach Metres` column the Passive Effects sheet has and the
Enchantment Effects sheet does not. The engine's
`UCataclysmAbilitySystemComponent::WithEnemiesInReach` walks the level only when
some modifier states a reach above zero, so a modifier built from a row that
could not state one keeps -1, the distance list stays empty, and the row counts
nobody. Nothing errors and nothing warns. Issue #1987.
"""
from __future__ import annotations

import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def a_row(**over) -> dict:
    """An enchantment-shaped row: no `ReachMetres` key, because that sheet has
    no such column and its rows are built without one."""
    row = {"Name": "Positive_Invented_for_this_test#1",
           "Stat": "attack_damage",
           "Scale": "enemies_in_reach",
           "ScaleStep": "1.0"}
    row.update(over)
    return row


def test_the_named_scales_are_scales_the_generator_actually_accepts():
    """A typo in the set would make this rule refuse nothing, for ever.

    THE FAILURE THIS GUARDS IS SILENT. `refuse_a_reach_scale_a_row_cannot_state`
    compares the row's scale against the set by name; a name no row can ever
    carry simply never matches, so every check below would pass over a rule that
    does nothing.
    """
    for scale in gen.SCALES_THAT_COUNT_ENEMIES_IN_A_RADIUS:
        assert scale in gen.SCALES, (
            f"{scale!r} is listed as a scale that counts enemies in a radius "
            f"and the generator does not accept it as a scale at all, so no row "
            f"can carry it and this rule can never fire. Fix the spelling "
            f"against SCALES in tools/generate_datatables.py.")


def test_a_row_that_cannot_state_a_reach_is_refused():
    """The rule fires, and says which column is missing."""
    problems = gen.refuse_a_reach_scale_a_row_cannot_state(
        "EnchantmentEffects", [a_row()])

    assert len(problems) == 1, (
        f"a row scaling by enemies_in_reach on a sheet with no reach column "
        f"was not refused: {problems}")
    assert "Reach Metres" in problems[0], (
        f"the refusal does not name the column that is missing, so an author "
        f"cannot act on it: {problems[0]!r}")


def test_the_crippled_variant_is_refused_too():
    """Both radius-counting scales, not only the first."""
    problems = gen.refuse_a_reach_scale_a_row_cannot_state(
        "EnchantmentEffects", [a_row(Scale="crippled_enemies_in_reach")])

    assert len(problems) == 1, (
        f"crippled_enemies_in_reach counts bodies in a radius exactly as "
        f"enemies_in_reach does and was not refused: {problems}")


def test_the_stat_rule_beside_this_one_does_not_catch_it():
    """What makes this rule worth having rather than a second spelling of one
    that already exists.

    IF THIS EVER FAILS, the two rules have converged and one of them is
    redundant -- which is worth knowing rather than leaving two checks that look
    independent and are not.
    """
    row = a_row(Stat="attack_damage")
    assert row["Stat"] in gen.STATS_WITH_AN_ASKER, (
        "this test needs a stat something asks for, or it proves nothing")

    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", [row]) == []
    assert len(gen.refuse_a_reach_scale_a_row_cannot_state(
        "EnchantmentEffects", [row])) == 1


def test_a_row_that_can_state_a_reach_is_left_alone():
    """Scope: a passive row carries the column, so this rule is not its judge.

    `validate_passive_effects` already refuses a passive row that names one of
    these scales and leaves the column empty, and it checks the value as well.
    Refusing it here too would report one fault twice and would fail every
    shipped Ravager row.
    """
    passive_shaped = a_row(ReachMetres="4.0")

    assert gen.refuse_a_reach_scale_a_row_cannot_state(
        "PassiveEffects", [passive_shaped]) == []


@pytest.mark.parametrize("sheet", ["PassiveEffects", "EnchantmentEffects"])
def test_no_shipped_row_is_refused(sheet):
    """Measured on the shipped data rather than assumed.

    IF THIS FAILS, a shipped row is dead and that is a finding with the row
    attached, not a reason to soften the rule.
    """
    path = REPO_ROOT / "game" / "Data" / f"{sheet}.csv"
    rows = list(csv.DictReader(path.open(encoding="utf-8-sig")))
    assert rows, f"{path.name} is empty, so this check measures nothing"

    problems = gen.refuse_a_reach_scale_a_row_cannot_state(sheet, rows)

    assert not problems, (
        f"{len(problems)} shipped row(s) in {path.name} scale by a count of "
        f"enemies in a radius they cannot state:\n"
        + "\n".join(f"  {p}" for p in problems))
