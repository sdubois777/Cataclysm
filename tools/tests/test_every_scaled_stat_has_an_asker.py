"""The generator's list of stats something asks for equals the engine's probes.

WHY TWO PLACES HOLD ONE SET. `tools/generate_datatables.py` refuses a data row
that carries a `Scale` on a stat nothing asks for, because such a row is
accepted, built, imported and dead -- `Ritualist_capstone_200#3` granted nothing
from the day it was written until issue #1973. The refusal needs the set at
GENERATION time, in Python. The promise behind the set -- that something really
does ask for each of those stats -- can only be measured by running the engine,
so it lives in `Cataclysm.StatExemption.EveryStatTheDataScalesIsAskedForThroughThePipeline`
as one probe per stat.

THIS IS WHAT STOPS THE TWO DRIFTING. A stat added to the generator's list without
a probe would let a dead row through with nothing measuring it; a probe added
without the list entry would refuse a row that works. Either way this fails and
names the difference.

WHY THE SET IS NOT DERIVED FROM THE ENGINE SOURCE. It was tried on 2026-09-17:
collect the stat argument of every pipeline lookup, resolve the named constants,
and compare. It got THREE of eleven wrong, all in the refusing direction, and a
check built on it would have refused 23 shipped rows that work. Two of the three
cannot be found by any search of call sites: `attack_damage` has no lookup call
at all, because its asker finds the stat line and runs the pipeline inline, and
`health_regen` is asked through a lambda that takes the stat as a parameter.

SO THIS PARSE IS DELIBERATELY NARROW, and that is the difference between it and
the one that was rejected. It reads ONE literal block, anchored on the function
that opens it, and raises when the block's shape changes rather than returning
nothing. It does not follow a call anywhere.
"""
from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: The engine file holding the probe table this check reads.
PROBES_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
              / "CataclysmStatExemptionTests.cpp")

#: The block: the accessor's name, then the map literal it returns. Anchored on
#: both so a rename fails here loudly rather than parsing to nothing.
BLOCK = re.compile(
    r"const TMap<FString, FProbe>& ScaledProbes\(\)\s*\{.*?"
    r"static const TMap<FString, FProbe> Made = \{(?P<body>.*?)\};",
    re.S)


def probe_names() -> set[str]:
    """The stats the engine's scaled-stat probes cover.

    RAISES RATHER THAN RETURNING EMPTY when the block cannot be found, for the
    reason `gen.stats_with_no_attribute` gives about the same shape of parse: an
    empty answer would make this check pass while measuring nothing, and the
    failure would surface later somewhere that cannot explain it.
    """
    if not PROBES_CPP.is_file():
        raise AssertionError(f"{PROBES_CPP} is missing, so the probe table "
                             f"cannot be read")

    found = BLOCK.search(PROBES_CPP.read_text(encoding="utf-8", errors="replace"))
    if not found:
        raise AssertionError(
            f"could not find the ScaledProbes() table in {PROBES_CPP.name}. If "
            f"it was renamed or reshaped, update BLOCK in "
            f"{pathlib.Path(__file__).name} rather than restating the names "
            f"here, so the two cannot disagree.")

    names = set(re.findall(r'TEXT\("(\w+)"\)', found.group("body")))
    if not names:
        raise AssertionError(
            "the ScaledProbes() table was found and parsed to nothing, which "
            "would make every check below pass over an empty set.")
    return names


def test_the_probe_table_can_be_read():
    """The parse itself, checked before anything is compared with it."""
    names = probe_names()

    assert len(names) >= 10, (
        f"the probe table parsed to {len(names)} names, which is fewer than the "
        f"shipped data has ever scaled. The parse is probably reading part of "
        f"the block.")

    for name in sorted(names):
        assert re.fullmatch(r"[a-z][a-z0-9_]*", name), (
            f"{name!r} came out of the probe table and is not shaped like a "
            f"stat name, so the regex is matching the wrong construct.")


def test_the_generators_list_and_the_engines_probes_are_the_same_set():
    """The whole point: neither side may gain a stat without the other."""
    listed = set(gen.STATS_WITH_AN_ASKER)
    probed = probe_names()

    unprobed = sorted(listed - probed)
    unlisted = sorted(probed - listed)

    assert not unprobed, (
        f"{len(unprobed)} stat(s) are in STATS_WITH_AN_ASKER in "
        f"tools/generate_datatables.py with no probe in {PROBES_CPP.name}:\n"
        + "\n".join(f"  {name}" for name in unprobed)
        + "\n\nThe list lets a scaled row on those stats through the generator, "
        "and nothing measures that anything asks for them. Add a probe that "
        "grants the stat with a scale the shipped data really pairs with it, "
        "moves the reading and asserts the engine's answer changes.")

    assert not unlisted, (
        f"{len(unlisted)} stat(s) have a probe in {PROBES_CPP.name} and are not "
        f"in STATS_WITH_AN_ASKER:\n"
        + "\n".join(f"  {name}" for name in unlisted)
        + "\n\nThe generator will refuse a scaled row on those stats although "
        "the engine does ask for them. Add them to the list.")


@pytest.mark.parametrize("sheet", ["PassiveEffects", "EnchantmentEffects"])
def test_no_shipped_row_is_refused_by_the_new_rule(sheet):
    """The shipped data passes the refusal, measured rather than assumed.

    IF THIS FAILS, A SHIPPED ROW IS DEAD and that is a finding with rows
    attached, not a reason to widen the list. The stat named needs an asker, or
    the row needs its scale taken off.
    """
    import csv

    path = REPO_ROOT / "game" / "Data" / f"{sheet}.csv"
    rows = list(csv.DictReader(path.open(encoding="utf-8-sig")))
    assert rows, f"{path.name} is empty, so this check measures nothing"

    problems = gen.refuse_a_scale_nothing_asks_for(sheet, rows)

    assert not problems, (
        f"{len(problems)} shipped row(s) in {path.name} carry a Scale on a stat "
        f"nothing asks for:\n" + "\n".join(f"  {p}" for p in problems))
