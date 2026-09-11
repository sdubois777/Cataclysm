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

THE PASSIVE TREE STILL CARRIES ITS OWN CHAIN OF THESE NAMES, in
`UCataclysmPassiveTree::AccumulateInto`. It is not read here. Moving it onto
the shared tables waits for the passive-tree work on another branch to merge.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PIPELINE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
            / "CataclysmStatPipeline.cpp")

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
