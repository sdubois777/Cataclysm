"""The condition and scale names the engine reads are the generator's names.

WHY THIS EXISTS. Issue #45. `tools/generate_datatables.py` refuses a sheet row
that names a condition or a scale outside `CONDITIONS` and `SCALES`. The
enchantment effects are turned into modifiers through
`UCataclysmStatPipeline::ConditionNamed` and `ScaleNamed`, whose name tables
are in `game/Source/Cataclysm/AbilitySystem/CataclysmStatPipeline.cpp`.

**The two lists have to be one list.** A name the generator accepts and the
engine does not know is a row that grants nothing, with only a log line to say
so. A name the engine knows and the generator refuses is dead code. Continuous
integration builds no C++, so the two are compared here as text.

THE PASSIVE TREE READS THE SAME TABLES SINCE ISSUE #1581, and until then it did
not. `UCataclysmPassiveTree::AccumulateInto` carried its own chain of eight of
these names, this file did not read that chain, and the two tests above passed
while a name in both lists they compare was unknown to the passive tree. A row
naming one was applied with no condition at all, which is a bonus that holds all
the time. `test_the_passive_tree_keeps_no_chain_of_its_own` below is what stops
such a chain coming back.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PIPELINE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
            / "CataclysmStatPipeline.cpp")
PASSIVE_TREE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmPassiveTree.cpp")

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: `{ TEXT("health_below"), ECataclysmStatCondition::HealthBelowPercent },`
CONDITION_ENTRY = re.compile(
    r'\{\s*TEXT\("([a-z_]+)"\)\s*,\s*ECataclysmStatCondition::\w+\s*\}')

#: `{ TEXT("health_missing"), ECataclysmStatScale::PerPercent... },`
SCALE_ENTRY = re.compile(
    r'\{\s*TEXT\("([a-z_]+)"\)\s*,\s*ECataclysmStatScale::\w+\s*\}')


@pytest.fixture(scope="module")
def source() -> str:
    if not PIPELINE.is_file():
        pytest.skip(f"{PIPELINE.name} is not present")
    return PIPELINE.read_text(encoding="utf-8")


def test_the_parser_found_both_tables(source):
    """Without this, a pattern that matched nothing would make both tests below
    compare an empty list and fail with the wrong explanation, or pass after
    somebody emptied the generator's lists to match."""
    assert len(CONDITION_ENTRY.findall(source)) >= 8
    assert len(SCALE_ENTRY.findall(source)) >= 9


def test_every_condition_name_is_one_the_engine_reads(source):
    engine = set(CONDITION_ENTRY.findall(source))
    assert engine == set(gen.CONDITIONS), (
        f"only the generator knows {sorted(set(gen.CONDITIONS) - engine)}; "
        f"only the engine knows {sorted(engine - set(gen.CONDITIONS))}. Add "
        f"the name to NamedStatConditions in CataclysmStatPipeline.cpp and to "
        f"CONDITIONS in tools/generate_datatables.py together.")


def test_every_scale_name_is_one_the_engine_reads(source):
    engine = set(SCALE_ENTRY.findall(source))
    assert engine == set(gen.SCALES), (
        f"only the generator knows {sorted(set(gen.SCALES) - engine)}; only "
        f"the engine knows {sorted(engine - set(gen.SCALES))}. Add the name to "
        f"NamedStatScales in CataclysmStatPipeline.cpp and to SCALES in "
        f"tools/generate_datatables.py together.")


def test_the_passive_tree_keeps_no_chain_of_its_own():
    """`AccumulateInto` reads the tables above rather than naming them itself.

    WHY A TEXT CHECK RATHER THAN A C++ TEST. There is a C++ test that every name
    the table holds reaches a passive modifier, and it is the stronger of the
    two. It is also invisible to continuous integration, which builds no C++ at
    all, so a chain re-added on a branch would reach `development` and only be
    caught the next time somebody ran the Unreal suite by hand.

    WHAT IT LOOKS FOR AND WHY THAT IS THE RIGHT MARK. The chain named
    enumerators directly -- `Modifier.Condition = ECataclysmStatCondition::
    HealthAtOrBelowPercent` and eleven more like it. Reading through
    `ConditionNamed` needs no enumerator name anywhere in the file, so any
    occurrence at all is either the chain coming back or a new special case
    deciding a condition in this file instead of in the shared table. Both are
    the thing issue #1581 was about.

    THE SCALE HALF IS DELIBERATELY NOT CHECKED THE SAME WAY. Its unknown-name
    fallback has to name one enumerator to make the modifier worth nothing, so
    the count there is one rather than zero and a count is not a useful mark.
    """
    if not PASSIVE_TREE.is_file():
        pytest.skip(f"{PASSIVE_TREE.name} is not present")

    source = PASSIVE_TREE.read_text(encoding="utf-8")
    named = re.findall(r"ECataclysmStatCondition::\w+", source)
    assert not named, (
        f"{PASSIVE_TREE.name} names {len(named)} condition enumerator(s) of "
        f"its own: {sorted(set(named))}. Issue #1581 removed that chain because "
        f"nothing held it equal to CONDITIONS in tools/generate_datatables.py, "
        f"and four names drifted out of it unnoticed. Read the name through "
        f"UCataclysmStatPipeline::ConditionNamed instead.")
