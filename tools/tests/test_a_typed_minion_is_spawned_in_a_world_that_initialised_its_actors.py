"""A C++ test that summons a minion of a named type makes a world that has initialised its actors.

WHY THIS EXISTS. Window bun1-w1 (2026-11-25) failed
`Cataclysm.DamageTypeAuras.BloodAndIronMakesItsCasterAndAMinionInsideFasterAndUnslowable` on an engine ensure, with
none of the test's own checks failing: "FActiveGameplayEffectsContainer::SetAttributeBaseValue: Unable to get
attribute set for attribute MaxHealth", from `ACataclysmMinion::Spawn`. A minion summoned WITH A TYPE NAME is given its
type's health there, by `SetNumericAttributeBase` on MaxHealth. The ability system finds the minion's vital attribute
set only once the minion's components have been initialised, and a spawned actor's components are initialised only
in a world that has run `InitializeActorsForPlay`. The test made its world with a file-local `MakeWorld()`, which is
`UWorld::CreateWorld` and nothing else, so the imp had no vital set when its health was written. The game is not
affected: every world it plays in has initialised its actors. `CataclysmTestWorld::MakeWorldThatHasBegunPlay` does it,
and every other test summoning a typed minion already used it.

AN ENSURE REPORTS ONCE PER CALL SITE PER RUN, so a second test with the same fault, run later, fails nothing. The Pall
of Ending test beside it had the same set-up and passed in that window for that reason alone.

WHAT IS CHECKED, over every `.cpp` under `game/Source/Cataclysm/Tests`, comments removed:
  1. a WORLD MAKER THAT DOES NOT INITIALISE is a function whose body calls `UWorld::CreateWorld` and neither
     `InitializeActorsForPlay` nor `MakeWorldThatHasBegunPlay`;
  2. a TYPED SUMMON is a call to `ACataclysmMinion::Spawn` with five or more arguments, the fifth being the type name,
     made in the test's body or in a function of the same file the test calls;
  3. no test's `RunTest` body does both.
The check reports how many tests it found summoning a typed minion, so a parse that found none cannot pass quietly.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
SPAWN = "ACataclysmMinion::Spawn("
FUNCTION = re.compile(r"\b(\w+)\s*\([^;{}()]*(?:\([^()]*\)[^;{}()]*)*\)\s*(?:const\s*)?\{")
RUN_TEST = re.compile(r"bool\s+(\w+)::RunTest\([^)]*\)\s*\{")
NOT_FUNCTIONS = {"if", "for", "while", "switch", "catch", "RunTest"}


def code(text: str) -> str:
    """The source with comments and string contents removed."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def braced(text: str, open_brace: int) -> str:
    depth = 0
    for index in range(open_brace, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[open_brace:index + 1]
    return text[open_brace:]


def argument_count(text: str, open_paren: int) -> int:
    """How many top-level arguments the call whose `(` is at `open_paren` passes."""
    depth = 0
    count = 1
    for index in range(open_paren, len(text)):
        char = text[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                return count
        elif char == "," and depth == 1:
            count += 1
    return count


def summons_a_typed_minion(body: str) -> bool:
    return any(argument_count(body, m.end() - 1) >= 5 for m in re.finditer(re.escape(SPAWN), body))


def functions(text: str) -> dict[str, str]:
    found: dict[str, str] = {}
    for m in FUNCTION.finditer(text):
        if m.group(1) not in NOT_FUNCTIONS:
            found.setdefault(m.group(1), braced(text, m.end() - 1))
    return found


def calls(body: str, names: set[str]) -> set[str]:
    return {name for name in names if re.search(rf"\b{re.escape(name)}\s*\(", body)}


def scan() -> tuple[list[str], int]:
    faults: list[str] = []
    typed = 0
    for path in sorted(TESTS.glob("*.cpp")):
        text = code(path.read_text(encoding="utf-8", errors="replace"))
        if SPAWN not in text:
            continue
        local = functions(text)
        bare_makers = {name for name, body in local.items()
                       if "UWorld::CreateWorld(" in body and "InitializeActorsForPlay" not in body
                       and "MakeWorldThatHasBegunPlay" not in body}
        typed_helpers = {name for name, body in local.items() if summons_a_typed_minion(body)}
        for m in RUN_TEST.finditer(text):
            body = braced(text, m.end() - 1)
            if not (summons_a_typed_minion(body) or calls(body, typed_helpers)):
                continue
            typed += 1
            makers = calls(body, bare_makers)
            if makers:
                faults.append(f"{path.name}: {m.group(1)} summons a typed minion in a world made by "
                              f"{sorted(makers)}, which never initialises its actors")
    return faults, typed


def test_no_test_summons_a_typed_minion_in_a_world_that_never_initialised_its_actors():
    faults, typed = scan()
    assert typed >= 100, f"only {typed} tests were found summoning a typed minion, so the parse has failed"
    assert not faults, ("\n".join(faults) + "\nMake the world with CataclysmTestWorld::MakeWorldThatHasBegunPlay(): "
                        "ACataclysmMinion::Spawn writes a typed minion's MaxHealth, and the ability system has no "
                        "vital attribute set to write it to until the world has initialised its actors.")


def test_the_bare_world_maker_is_recognised_where_it_is_defined():
    """Control on the parse: the file-local `MakeWorld()` of the two files that define one is found as a maker that
    does not initialise, and the shared begun-play helper is not."""
    for name in ("CataclysmSkillTemplateTests.cpp", "CataclysmTeamsTests.cpp"):
        local = functions(code((TESTS / name).read_text(encoding="utf-8")))
        assert "UWorld::CreateWorld(" in local["MakeWorld"]
        assert "InitializeActorsForPlay" not in local["MakeWorld"]
