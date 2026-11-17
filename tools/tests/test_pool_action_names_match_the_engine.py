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


#: `ActOnEvent(FName(TEXT("hit_dealt")), <tags>, <amount>, ...);`, up to the
#: call's own semicolon. Comments are gone before this is matched.
CALL_OF = r'(?<![:\w])ActOnEvent\(\s*FName\(\s*TEXT\("%s"\)\s*\)([^;]*)\)\s*;'


def amounts_passed(event: str) -> list[str]:
    """What every call firing `event` passes as its amount: its third argument,
    or an empty string for a call that passes none."""
    passed = []
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.relative_to(SOURCE).parts:
            continue
        for rest in re.findall(CALL_OF % re.escape(event), code_of(path)):
            arguments = [part.strip() for part in rest.split(",")][1:]
            passed.append(arguments[1] if len(arguments) > 1 else "")
    return passed


def carries_an_amount(event: str) -> bool:
    """Whether some call firing `event` passes an amount that is not nought."""
    return any(amount and not re.fullmatch(r"0(\.0*)?f?", amount)
               for amount in amounts_passed(event))


def test_every_event_a_row_may_take_a_share_of_is_fired_with_an_amount():
    """Issue #1833, 2026-10-05. `EVENTS_WITH_AN_AMOUNT` lets a row take a share
    of what its event carried. A name there that the game fires with no amount
    is a row that validates, is built, and moves nothing. THE CONTROLS ARE IN
    THE SAME TEST: `hit_taken` is fired with a literal nought and `dot_applied`
    with no amount at all, and the reader must say so for both, or it is reading
    the wrong argument. `kill` WAS THE SECOND CONTROL UNTIL 2026-10-07, when it
    began to carry the slain enemy's maximum health and joined the list."""
    assert amounts_passed("hit_taken"), (
        f"no call firing hit_taken was read. {PARSE_ADVICE}")
    assert not carries_an_amount("hit_taken"), (
        f"hit_taken is fired with {amounts_passed('hit_taken')}, which this "
        f"reader takes for an amount. {PARSE_ADVICE}")
    assert amounts_passed("dot_applied") and not carries_an_amount("dot_applied"), (
        f"dot_applied is fired with {amounts_passed('dot_applied')}. {PARSE_ADVICE}")

    without = [event for event in gen.EVENTS_WITH_AN_AMOUNT
               if not carries_an_amount(event)]
    assert not without, (
        f"{without} are in EVENTS_WITH_AN_AMOUNT and the game fires them with "
        f"{[amounts_passed(event) for event in without]}, so a row taking a "
        f"share of their amount would move nothing.")


def arguments_after_the_name(rest: str) -> list[str]:
    """The arguments a call passes after its event's name, split at the commas
    that are not inside brackets, so `FMath::Max(0.0f, Blocked)` is one."""
    parts: list[str] = []
    depth = 0
    current = ""
    for character in rest:
        if character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
        if character == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += character
    parts.append(current.strip())
    return parts[1:]


def characters_passed(event: str) -> list[str]:
    """What every call firing `event` passes as its other character: its fifth
    argument, or an empty string for a call that passes none."""
    passed = []
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.relative_to(SOURCE).parts:
            continue
        for rest in re.findall(CALL_OF % re.escape(event), code_of(path)):
            arguments = arguments_after_the_name(rest)
            passed.append(arguments[3] if len(arguments) > 3 else "")
    return passed


def carries_a_character(event: str) -> bool:
    """Whether some call firing `event` passes a character that is not null."""
    return any(character and character != "nullptr"
               for character in characters_passed(event))


def test_every_event_a_strike_may_hang_on_is_fired_with_a_character(event_reading):
    """Ruled 2026-10-07. `STRIKE_TARGET_EVENTS` lets a row deal a hit to the
    other character of its event. A name there that the game raises with no
    character is a row that validates, is built, and strikes nobody.

    AND THE OTHER WAY ROUND: every event the game raises with a character is
    either one a strike may hang on or is listed, with its reason, as one whose
    character cannot be struck. So a new event that carries a character has to
    be put in one list or the other.

    THE CONTROLS ARE IN THE SAME TEST. `hit_taken` is raised with three
    arguments after its name and `dot_applied` with none, and the reader must
    say neither carries a character. `block` passes `FMath::Max(0.0f,
    DamageBlocked)` before its attacker, so reading `Attacker` there shows the
    comma inside the brackets was not taken for a separator."""
    assert characters_passed("hit_taken") and not carries_a_character("hit_taken"), (
        f"hit_taken is fired with {characters_passed('hit_taken')}, which this "
        f"reader takes for a character. {PARSE_ADVICE}")
    assert characters_passed("dot_applied") and not carries_a_character("dot_applied"), (
        f"dot_applied is fired with {characters_passed('dot_applied')}. {PARSE_ADVICE}")
    assert "Attacker" in characters_passed("block"), (
        f"block is fired with {characters_passed('block')} and none of them is "
        f"its attacker. {PARSE_ADVICE}")

    strikes = set(gen.STRIKE_TARGET_EVENTS)
    refused = set(gen.EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK)
    assert not strikes & refused, (
        f"{sorted(strikes & refused)} are in both lists.")

    engine, _, _ = event_reading
    carrying = {event for event in engine if carries_a_character(event)}
    assert carrying == strikes | refused, (
        f"the game raises {sorted(carrying - strikes - refused)} with a "
        f"character and neither list has them; {sorted((strikes | refused) - carrying)} "
        f"are listed and the game raises them with no character. A strike on "
        f"an event with no character strikes nobody.")


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
