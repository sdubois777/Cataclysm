"""Every console variable an Unreal test writes is written at console priority (`ECVF_SetByConsole`). Issue #1470.

Unreal remembers the priority a console variable was last set at and silently discards a later write at a lower one.
Automation tests run in name order in one process, so a test writing at code priority (`ECVF_SetByCode`, which a
plain `Set(value)` means) works only while no test sorting before it has written the same variable at console
priority. `Cataclysm.Difficulty.*` passed only because it sorts before `Cataclysm.Drop.*`, and the five tests
`Cataclysm.EmpireRunTier.*` added after it failed in the whole suite while passing alone.

A console variable here is the receiver of a `->Set(` call that is either a name the same file declares as an
`IConsoleVariable*`, or a call to `FindConsoleVariable(...)` or `AsVariable()` written in place.
"""
import functools
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "game" / "Source" / "Cataclysm" / "Tests"

# Every Set of a console variable found on 2026-10-29, when this was written. A count far below it means the scan
# stopped finding them, and the check would then pass on nothing.
FEWEST_EXPECTED = 200

DECLARED = re.compile(r"IConsoleVariable\s*\*\s*(\w+)")
SET_CALL = re.compile(r"->\s*Set\s*\(")


def matching_close(text, open_index):
    """The index of the parenthesis that closes the one at `open_index`."""
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unbalanced parentheses")


def matching_open(text, close_index):
    """The index of the parenthesis that the one at `close_index` closes."""
    depth = 0
    for index in range(close_index, -1, -1):
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unbalanced parentheses")


def console_variable_sets(path):
    """(line, call text) for every Set of a console variable in one file."""
    text = path.read_text(encoding="utf-8", errors="replace")
    declared = set(DECLARED.findall(text))
    found = []
    for match in SET_CALL.finditer(text):
        before = text[:match.start()].rstrip()
        if before.endswith(")"):
            opened = matching_open(before, len(before) - 1)
            callee = re.search(r"(\w+)\s*$", before[:opened])
            if not callee or callee.group(1) not in ("FindConsoleVariable", "AsVariable"):
                continue
        else:
            receiver = re.search(r"(\w+)\s*$", before)
            if not receiver or receiver.group(1) not in declared:
                continue
        open_index = match.end() - 1
        call = text[match.start():matching_close(text, open_index) + 1]
        found.append((text.count("\n", 0, match.start()) + 1, " ".join(call.split())))
    return found


@functools.lru_cache(maxsize=1)
def every_set():
    """Every console variable Set in the tests, scanned ONCE per process. Issue #1451: each of the two tests below
    scanned every test file itself, about 8 seconds each, so the same scan ran twice. A tuple, so a caller cannot
    change what the next one reads."""
    return tuple((path.name, line, call)
                 for path in sorted(TESTS.glob("*.*")) if path.suffix in (".cpp", ".h")
                 for line, call in console_variable_sets(path))


def test_the_scan_finds_the_console_variable_writes():
    found = every_set()
    assert len(found) >= FEWEST_EXPECTED, (
        f"only {len(found)} console variable writes were found in {TESTS}; the scan has stopped finding them")


def test_every_console_variable_a_test_writes_is_written_at_console_priority():
    below = [f"{name}:{line}: {call}" for name, line, call in every_set() if "ECVF_SetByConsole" not in call]
    assert not below, (
        "a console variable written below console priority is discarded once any earlier test has written it at "
        "console priority, so the test passes or fails by the order tests run in (issue #1470). Pass "
        "ECVF_SetByConsole:\n" + "\n".join(below))
