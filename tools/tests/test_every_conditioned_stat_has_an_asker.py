"""A row may state a stat under a condition only where the game can judge it.

WHY THIS EXISTS. Issue #1833, 2026-10-06. `UCataclysmPlayerClassStats::ApplyTo`
works out a gameplay attribute with an empty state, which refuses every
condition. So a conditioned row reaches play only where the code that uses its
stat asks the stat pipeline at the moment of use. Where that code reads the
attribute, the row is accepted, built, imported and dead: a resistance row under
`health_below` would have been one on 2026-10-05, because a blow reads a
defender's resistance from the attribute.

`tools/generate_datatables.py` refuses the shape with
`refuse_a_condition_nothing_asks_for`, which needs two things at generation
time: the stats something asks for, and what each one's asker hands over (the
wearer's state only, the blow being taken, or the hit being dealt). Both are in
`CONDITIONED_STATS_WITH_AN_ASKER`.

WHAT HOLDS THAT LIST TO THE GAME. The promise behind each name can only be
measured by running the engine, so it lives in three probe tables in
`CataclysmStatExemptionTests.cpp`: the scaled probes, the probes for stats with
no attribute, and the conditioned probes added with this file. A scale and a
condition are both judged against the state handed over when the stat is asked
for, so a probe of either kind measures the ask. This file requires every listed
stat to be in one of the three, or to be named in
`READ_FROM_THE_CODE_AND_NOT_PROBED` below.

WHAT IS NOT MEASURED. Which of "blow taken" and "hit dealt" an asker passes is
read from the code, stat by stat, on 2026-10-06; no probe hands over a blow or a
target to prove it. And a stat can have a second consumer that reads the
attribute: issues #2233, #2234 and #2235 record three.

THE PARSE IS NARROW, as in `test_every_scaled_stat_has_an_asker.py`: one literal
block per table, anchored on the accessor that opens it, raising when the shape
changes.
"""
from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

PROBES_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
              / "CataclysmStatExemptionTests.cpp")

#: The three accessors, each followed by the map literal it returns. `Probes`
#: is matched with the reference sign in front of it, so that it cannot match
#: the tail of `ScaledProbes` or `ConditionedProbes`.
TABLES = ("ScaledProbes", "Probes", "ConditionedProbes")

#: Stats the shipped passives state under a condition whose asker was read from
#: the code on 2026-10-06 and has NO PROBE in any of the three tables. Each is
#: asked for with `StatForSkill` and the wearer's own state. A name leaves this
#: set when a probe is written for it; `test_no_unprobed_name_has_a_probe` fails
#: if one is left here after that.
READ_FROM_THE_CODE_AND_NOT_PROBED = frozenset({
    "cooldown_skip_chance",
    "debuffs_do_not_expire",
    "fervour_loss_suppressed",
    "fervour_per_cast",
    "health_cost_suppressed",
    "movement_speed_reduction_suppressed",
    "nova_damage_of_missing_health",
})


def table(accessor: str) -> set[str]:
    """The stat names one probe table holds. Raises when it cannot be read."""
    if not PROBES_CPP.is_file():
        raise AssertionError(f"{PROBES_CPP} is missing")
    block = re.compile(
        r"const TMap<FString, FProbe>& " + re.escape(accessor) + r"\(\)\s*\{.*?"
        r"static const TMap<FString, FProbe> Made = \{(?P<body>.*?)\};", re.S)
    found = block.search(PROBES_CPP.read_text(encoding="utf-8", errors="replace"))
    if not found:
        raise AssertionError(
            f"could not find the {accessor}() table in {PROBES_CPP.name}. If it "
            f"was renamed or reshaped, update this file's pattern.")
    names = set(re.findall(r'TEXT\("(\w+)"\)', found.group("body")))
    if not names:
        raise AssertionError(f"the {accessor}() table parsed to nothing")
    return names


def probed() -> set[str]:
    return set().union(*(table(accessor) for accessor in TABLES))


def test_the_three_probe_tables_can_be_read_and_are_three_tables():
    """Each parse, checked before anything is compared with it. THE THREE SIZES
    DIFFER, which is what shows `Probes` did not match another table's tail."""
    sizes = {accessor: len(table(accessor)) for accessor in TABLES}

    assert sizes["ConditionedProbes"] == 9, sizes
    assert sizes["ScaledProbes"] >= 20, sizes
    assert sizes["Probes"] >= 40, sizes
    assert len(set(sizes.values())) == 3, sizes
    for name in sorted(probed()):
        assert re.fullmatch(r"[a-z][a-z0-9_]*", name), name


def test_every_listed_stat_has_a_probe_or_is_named_as_unprobed():
    listed = set(gen.CONDITIONED_STATS_WITH_AN_ASKER)
    unprobed = sorted(listed - probed() - READ_FROM_THE_CODE_AND_NOT_PROBED)

    assert not unprobed, (
        f"{unprobed} are in CONDITIONED_STATS_WITH_AN_ASKER with no probe in "
        f"{PROBES_CPP.name}. The list lets a conditioned row on those stats "
        f"through the generator, and nothing measures that anything asks for "
        f"them. Add a probe to ConditionedProbes() that gives the stat a "
        f"conditioned modifier and asserts the engine's answer changes when "
        f"the condition comes to hold.")


def test_no_unprobed_name_has_a_probe():
    """An excuse that outlives its reason hides the next gap."""
    stale = sorted(READ_FROM_THE_CODE_AND_NOT_PROBED & probed())
    unlisted = sorted(READ_FROM_THE_CODE_AND_NOT_PROBED
                      - set(gen.CONDITIONED_STATS_WITH_AN_ASKER))

    assert not stale, f"{stale} have a probe now and should leave the unprobed set"
    assert not unlisted, f"{unlisted} are excused here and not on the generator's list"


def test_every_conditioned_probe_is_on_the_list():
    """A probe with no list entry would refuse a row that works."""
    unlisted = sorted(table("ConditionedProbes")
                      - set(gen.CONDITIONED_STATS_WITH_AN_ASKER))

    assert not unlisted, unlisted


def test_every_condition_about_a_blow_or_a_hit_is_classed_as_one():
    """THE TWO SETS ARE HAND LISTS, so a condition added later must be put in one
    or deliberately left as the wearer's own state. The prefixes below are how
    the condition names say which they are; a new `target_` or `opponent_`
    condition outside the sets fails here."""
    blow = gen.CONDITIONS_OF_A_BLOW_TAKEN
    hit = gen.CONDITIONS_OF_A_HIT_DEALT

    assert blow <= set(gen.CONDITIONS), sorted(blow - set(gen.CONDITIONS))
    assert hit <= set(gen.CONDITIONS), sorted(hit - set(gen.CONDITIONS))
    assert not (blow & hit), sorted(blow & hit)
    for name in gen.CONDITIONS:
        if name.startswith(("opponent_", "hit_is_", "attacker_")):
            assert name in blow, f"{name} asks about a blow taken and is not classed"
        if name.startswith(("target_", "enemies_hit_")):
            assert name in hit, f"{name} asks about a hit dealt and is not classed"


@pytest.mark.parametrize("sheet", ["PassiveEffects", "EnchantmentEffects"])
def test_no_shipped_row_is_refused(sheet):
    """The shipped data passes, measured. IF THIS FAILS A SHIPPED ROW IS DEAD,
    which is a finding with rows attached and not a reason to widen the list."""
    path = REPO_ROOT / "game" / "Data" / f"{sheet}.csv"
    rows = list(csv.DictReader(path.open(encoding="utf-8-sig")))
    conditioned = [row for row in rows if row["Stat"] and row["Condition"]]
    assert conditioned, f"{path.name} holds no conditioned row, so this measures nothing"

    problems = gen.refuse_a_condition_nothing_asks_for(sheet, rows)

    assert not problems, "\n".join(problems)


def row(stat: str, condition: str = "", second: str = "") -> dict:
    return {"Name": "Made_up#1", "Stat": stat, "Condition": condition,
            "Condition2": second}


def refused(*rows: dict) -> list[str]:
    return gen.refuse_a_condition_nothing_asks_for("Made up", list(rows))


def test_a_conditioned_resistance_row_is_refused():
    """The row this check exists for: Brute's Heart's resistances."""
    problems = refused(row("resistance_war", "health_below"))

    assert len(problems) == 1 and "nothing is known to ask" in problems[0]


def test_the_same_row_with_no_condition_is_allowed():
    assert refused(row("resistance_war")) == []


def test_a_condition_about_a_target_is_refused_on_a_stat_asked_with_none():
    problems = refused(row("movement_speed", "target_is_boss"))

    assert len(problems) == 1 and "a hit being dealt" in problems[0]


def test_a_condition_about_a_blow_taken_is_refused_on_an_attackers_stat():
    problems = refused(row("attack_damage", "hit_is_spell"))

    assert len(problems) == 1 and "a blow being taken" in problems[0]


def test_each_kind_of_condition_is_allowed_where_its_asker_passes_it():
    assert refused(row("armor", "hit_is_spell"),
                   row("attack_damage", "target_is_boss"),
                   row("movement_speed", "health_below"),
                   row("armor", "health_below")) == []


def test_the_second_condition_is_judged_as_the_first_is():
    problems = refused(row("damage_taken", "health_above", "target_is_boss"))

    assert len(problems) == 1 and "target_is_boss" in problems[0]


def test_an_action_row_is_skipped():
    """A row with no stat moves a pool on an event, and its condition is judged
    where the action fires."""
    assert refused(row("", "target_is_staggered")) == []
