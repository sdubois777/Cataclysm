"""The pools and events an enchantment row may name are the ones the game acts on.

WHY THIS EXISTS. Issue #1815. `tools/generate_datatables.py` refuses an
Enchantment Effects row that moves a pool outside `POOL_ACTIONS`, or moves it on
an event outside `action_events()`. The game keeps two lists of its own:

    the pools    `UCataclysmAbilitySystemComponent::PoolAttributesFor`, which
                 turns a pool's name into the attribute held and its maximum
    the events   every call of `ActOnEvent` that passes a name, which on
                 2026-09-16 were in `CataclysmAbilitySystemComponent.cpp` and
                 `CataclysmPlayerCharacter.cpp`

**A name on one side only fails in silence, whichever side it is on.** A pool
the generator accepts and the game has no attributes for is a row that logs a
warning when it fires and moves nothing. An event the generator accepts and the
game never fires is quieter still: nothing asks for the row, so nothing is
logged at all. A name the game fires and the generator refuses is a row nobody
can write. Continuous integration builds no C++, so the two sides are compared
here as text, as `test_stat_condition_names_match_the_engine.py` compares the
condition names.

THE TWO SIDES MATCHED ON 2026-09-16, when this was written: four pools and
fifteen events.

WHAT IS NOT ASSERTED. That an event fires at the right moment, or that a pool
moves by the right amount. Those are for the Unreal tests in
`CataclysmEnchantmentEffectTests.cpp`. This holds two vocabularies to one list.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
ABILITY_SYSTEM = (SOURCE / "Cataclysm" / "AbilitySystem"
                  / "CataclysmAbilitySystemComponent.cpp")

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: Comments are removed before anything is matched, so prose that names a call
#: or a comparison cannot stand in for one.
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)

#: `const FName HealthPoolName(TEXT("health"));`
POOL_NAME = re.compile(
    r'\bconst\s+FName\s+(\w+)\s*\(\s*TEXT\("([a-z_]+)"\)\s*\)\s*;')

#: `if (Pool == HealthPoolName)`. THE COMPARISON AND NOT THE DECLARATION, so a
#: name declared and never compared is not counted as a pool the game accepts.
POOL_COMPARED = re.compile(r"\bPool\s*==\s*(\w+)")

#: `ActOnEvent(FName(TEXT("block")))`, with or without more arguments after it.
EVENT_FIRED = re.compile(
    r'(?<![:\w])ActOnEvent\(\s*FName\(\s*TEXT\("([a-z_]+)"\)\s*\)')

#: Every call, whatever it passes. The definition is `::ActOnEvent(` and is not
#: a call, which is what the look-behind is for.
ANY_CALL = re.compile(r"(?<![:\w])ActOnEvent\(")

PARSE_ADVICE = ("Read the C++ and change the pattern at the top of this file "
                "rather than the lists it compares.")


def code_of(path: pathlib.Path) -> str:
    return COMMENT.sub("", path.read_text(encoding="utf-8"))


def body_of(code: str, opening: str) -> str:
    """From a definition to the closing brace at column zero, or empty."""
    start = code.find(opening)
    if start < 0:
        return ""
    end = code.find("\n}", start)
    return code[start:end] if end > start else ""


@pytest.fixture(scope="module")
def pool_reading() -> tuple[str, dict[str, str], list[str]]:
    """The body of `PoolAttributesFor`, the named constants, the comparisons."""
    if not ABILITY_SYSTEM.is_file():
        pytest.skip(f"{ABILITY_SYSTEM.name} is not present")
    code = code_of(ABILITY_SYSTEM)
    body = body_of(
        code, "bool UCataclysmAbilitySystemComponent::PoolAttributesFor(")
    return body, dict(POOL_NAME.findall(code)), POOL_COMPARED.findall(body)


@pytest.fixture(scope="module")
def event_reading() -> tuple[set[str], int, int]:
    """Every event name fired outside the tests, and two counts of calls."""
    names: set[str] = set()
    named_calls = 0
    calls = 0
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.relative_to(SOURCE).parts:
            continue
        code = code_of(path)
        fired = EVENT_FIRED.findall(code)
        names.update(fired)
        named_calls += len(fired)
        calls += len(ANY_CALL.findall(code))
    if not calls:
        pytest.skip(f"no call of ActOnEvent found under {SOURCE}")
    return names, named_calls, calls


def test_the_parser_read_every_comparison_and_every_call(pool_reading,
                                                          event_reading):
    """Without this, a pattern that stopped matching would make a comparison
    below fail with the wrong explanation, and a call passing a name held in a
    variable would be an event nobody compared."""
    body, constants, compared = pool_reading
    assert body, (
        f"no definition of PoolAttributesFor in {ABILITY_SYSTEM.name}. "
        f"{PARSE_ADVICE}")
    unresolved = [name for name in compared if name not in constants]
    assert compared and not unresolved, (
        f"PoolAttributesFor compares {compared}, and these are not constants "
        f"spelled `const FName X(TEXT(\"...\"));`: {unresolved}. "
        f"{PARSE_ADVICE}")

    _, named_calls, calls = event_reading
    assert named_calls == calls, (
        f"{calls} calls of ActOnEvent and only {named_calls} pass a name "
        f"written as FName(TEXT(\"...\")), so an event is fired that this "
        f"file cannot read. {PARSE_ADVICE}")


def test_every_pool_a_row_may_move_is_one_the_game_has_attributes_for(
        pool_reading):
    _, constants, compared = pool_reading
    engine = {constants[name] for name in compared if name in constants}
    generator = set(gen.POOL_ACTIONS)
    assert engine == generator, (
        f"only the generator knows {sorted(generator - engine)}; only the "
        f"game knows {sorted(engine - generator)}. Add the pool to "
        f"PoolAttributesFor in CataclysmAbilitySystemComponent.cpp and to "
        f"POOL_ACTIONS in tools/generate_datatables.py together.")


def test_every_event_a_row_may_hang_on_is_one_the_game_fires(event_reading):
    engine, _, _ = event_reading
    generator = gen.action_events()
    assert engine == generator, (
        f"only the generator knows {sorted(generator - engine)}; only the "
        f"game fires {sorted(engine - generator)}. An event is a clock "
        f"condition named seconds_after_<event> or one of ACTION_ONLY_EVENTS "
        f"in tools/generate_datatables.py; add it there and fire it with "
        f"ActOnEvent in the same change.")
