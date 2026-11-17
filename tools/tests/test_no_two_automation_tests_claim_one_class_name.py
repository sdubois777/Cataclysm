"""Two Unreal automation tests must not have class names differing only in case.

WHAT WENT WRONG, FOUND ON 2026-09-13. Two test classes existed:

    FCataclysmWeaponSubtypeTest   Cataclysm.Damage.SlashingAndMagicTargetDifferentPools
    FCataclysmWeaponSubTypeTest   Cataclysm.WeaponSlots.TheEquippedWeaponDecidesTheSubType

One letter's capitalisation apart. **The second one never ran, for a month.**

WHY THE CLASS NAME DECIDES THIS AND NOT THE TEST NAME. The engine's
`IMPLEMENT_SIMPLE_AUTOMATION_TEST` macro creates the test instance with
`TEXT(#TClass)` -- the stringified CLASS name -- and
`FAutomationTestBase::FAutomationTestBase` registers under that
(`Engine/Source/Runtime/Core/Private/Misc/AutomationTest.cpp` line 165). The
readable `"Cataclysm.Group.Name"` string is only the beautified name, used for
display and filtering. So two tests with different test names and one class name
are a collision, and two tests with one test name and different class names are
not the thing this looks for.

AND THE REGISTRY IS CASE-INSENSITIVE. It is a `TMap<FString, ...>`, and Unreal's
`FString` hashes and compares without regard to case. So `SubType` and `Subtype`
are the same key.

HOW IT FAILS, WHICH IS THE PART THAT COST A MONTH. `RegisterAutomationTest`
keeps the first registration and refuses the second. The refusal is a log
warning, not an error:

    LogAutomationTest: Warning: Failed to register test with the name
    'FCataclysmWeaponSubTypeTest'. Test with the same name is already
    registered and will not be overridden.

The editor starts, the run proceeds, and the refused test is simply not in the
list. The run then reports "15 tests performed, 15 succeeded, 0 failed" -- a
clean result that is indistinguishable from one where all sixteen ran. Reading
the source says the behaviour is covered; it is not, because the body never
executed. Issue #1666.

Registration happens when the game module loads, so that warning is printed
whether or not any test is then run. It sat unread for five days: nine log files
in one worktree, from seven distinct editor starts, the earliest dated
2026-09-08. Those files are in untracked `.claude/crash-evidence-*` and
`.claude/playtest-*` directories, so they are local evidence and not in git.

WHY A PYTHON TEST. Same reason as
`test_no_two_files_share_an_anonymous_helper.py`: continuous integration never
compiles the C++ -- issue #20 is the self-hosted runner that would -- and even a
compiler would not catch this one, because it is not a compile error. It is two
valid classes with different names. Only running the editor reveals it, and only
if somebody reads a warning among thousands of lines.

WHAT IT DOES NOT CATCH, SAID PLAINLY. Two tests sharing a beautified
`"Cataclysm.Group.Name"` string are not looked for here; that is a different key
and a different failure. Nor does this notice a test class declared some way
this parser does not recognise -- which is exactly how the first search for this
bug missed the answer, so `test_both_ways_of_declaring_a_test_are_found` below
exists to keep that specific blindness from coming back.
"""

from __future__ import annotations

import collections
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"

#: The engine macros that create and register a test instance. Every one of them
#: takes the class name first and registers under it.
ENGINE_MACRO = re.compile(
    r"\bIMPLEMENT_(?:SIMPLE|COMPLEX|NETWORKED|BDD)_AUTOMATION_TEST\s*\(\s*"
    r"([A-Za-z_]\w*)\s*,\s*\"([^\"]+)\"",
    re.DOTALL)

#: A `#define` whose body calls an engine macro, each wrapping the flags so a
#: file's tests do not repeat them. A parser that reads only the engine macro
#: misses every test declared through one and reports a clean result.
#:
#: COUNTED WITH THIS FILE'S OWN `WRAPPER_DEFINITION` PATTERN, IMPORTED RATHER
#: THAN RETYPED, ON 2026-09-13: 36 definitions in 36 files, and 384 tests
#: declared through them.
#:
#: **THE WORD "IMPORTED" IS LOAD-BEARING.** Two sessions counted this on the same
#: day and one got 37, by reading the pattern and retyping it into a shell
#: command where the backslashes were mangled. The same session's next attempt
#: returned 0, which is obviously wrong and is what exposed the first. Import the
#: pattern; do not copy it.
#:
#: **Written as a dated measurement rather than as "the project has N", which is
#: what the previous figures said.** They read 35 and 378, were correct when
#: written, and went stale the same day -- the 36th definition arrived in
#: `c63f01fb`, hours before anyone noticed.
#:
#: SO RE-MEASURE RATHER THAN TRUSTING THESE. The floor below already follows
#: that convention and it is the one figure here that never went wrong.
WRAPPER_DEFINITION = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+([A-Z_][A-Z0-9_]*)[ \t]*\([^)]*\)[^\n]*\\\n"
    r"(?:[^\n]*\\\n)*?[^\n]*IMPLEMENT_\w*AUTOMATION_TEST",
    re.MULTILINE)

#: How few registrations mean the parser has stopped working rather than the
#: project having stopped writing tests. There were 1,761 on 2026-09-13.
FEWEST_REGISTRATIONS_A_WORKING_PARSER_FINDS = 500


def strip_comments(text: str) -> str:
    """So a class named in a comment is not counted as one declared."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def source_files() -> list[pathlib.Path]:
    return sorted(SOURCE_ROOT.rglob("*.cpp")) + sorted(SOURCE_ROOT.rglob("*.h"))


def wrapper_macro_names(text: str) -> set[str]:
    """The project-defined macros in this file that declare a test."""
    return {match.group(1) for match in WRAPPER_DEFINITION.finditer(text)}


def uses_of(macro: str) -> re.Pattern[str]:
    return re.compile(
        r"\b" + re.escape(macro) + r"\s*\(\s*([A-Za-z_]\w*)\s*,\s*\"([^\"]+)\"",
        re.DOTALL)


def registrations() -> list[tuple[str, str, str]]:
    """Every test the engine will try to register: class name, test name, file.

    Both ways of declaring one are read: the engine macro directly, and the
    project's own wrapper macros, whose names are found per file rather than
    listed here so a new wrapper is covered the day it is written.
    """
    found: list[tuple[str, str, str]] = []
    for path in source_files():
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in ENGINE_MACRO.finditer(text):
            found.append((match.group(1), match.group(2), path.name))

        for macro in wrapper_macro_names(text):
            for match in uses_of(macro).finditer(text):
                found.append((match.group(1), match.group(2), path.name))
    return found


def test_the_search_finds_something_to_look_at():
    """A guard over an empty list is a guard that cannot fail."""
    found = registrations()
    assert len(found) >= FEWEST_REGISTRATIONS_A_WORKING_PARSER_FINDS, (
        f"only {len(found)} automation test registrations were found under "
        f"{SOURCE_ROOT}, and there were 1,761 on 2026-09-13. Either the "
        "patterns in this file stopped matching how tests are declared, or the "
        "source tree moved. Either way the duplicate check below is now "
        "checking almost nothing.")


def test_both_ways_of_declaring_a_test_are_found():
    """The blindness that hid this bug for a month, kept out on purpose.

    THE FIRST SEARCH FOR THIS DEFECT READ ONLY THE ENGINE MACRO and reported
    1,383 registrations with no duplicate class name anywhere. The answer was in
    the 378 it never looked at, because those tests are declared through the
    project's own wrapper macros. A parser that silently covers part of the tree
    gives a confident wrong answer, which is worse than no answer.

    **BOTH FIGURES IN THAT SENTENCE ARE HISTORICAL AND MUST NOT BE UPDATED.**
    They describe what that one search found, on the day it ran. The tree has
    1,404 engine-macro registrations now, so the 1,383 beside them dates the
    pair. Raising the 378 to today's count would make a record of a past search
    report a number it never saw.
    """
    engine_only = 0
    through_wrappers = 0
    for path in source_files():
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        engine_only += len(ENGINE_MACRO.findall(text))
        for macro in wrapper_macro_names(text):
            through_wrappers += len(uses_of(macro).findall(text))

    assert engine_only > 0, (
        "no test declared with the engine macro directly was found at all.")
    assert through_wrappers > 0, (
        "no test declared through one of the project's own wrapper macros was "
        "found. There were 384 of them on 2026-09-13, in 36 files, each "
        "wrapping IMPLEMENT_SIMPLE_AUTOMATION_TEST to avoid repeating the "
        "flags. If WRAPPER_DEFINITION no longer recognises them, this file has "
        "the same blind spot that let issue #1666 sit for a month.")


def test_a_wrapper_definition_is_not_itself_a_registration():
    """The `#define` line names its parameters, not a class.

    Without this, `#define CATACLYSM_TEST(TestClass, TestName)` would be read as
    a test whose class is `TestClass`, and every wrapper definition would collide
    with each of the others -- a failure with nothing wrong behind it.

    NO COUNT HERE ON PURPOSE. This said "all 35", which is a number that has to
    be maintained to say something the sentence does not need: the collision
    happens whether there are two definitions or two hundred.
    """
    definition = (
        "#define CATACLYSM_TEST(TestClass, TestName) \\\n"
        "\tIMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \\\n"
        "\t\tEAutomationTestFlags::EditorContext) \\\n"
        "\tbool TestClass::RunTest(const FString& Parameters)\n")

    assert ENGINE_MACRO.findall(definition) == [], (
        "the engine-macro pattern matches a wrapper's definition. It must not: "
        "the second argument there is the parameter name TestName, not a "
        "quoted test name, which is what keeps the two apart.")
    assert wrapper_macro_names(definition) == {"CATACLYSM_TEST"}, (
        "the wrapper-definition pattern no longer recognises how this project "
        "writes a wrapper macro, so tests declared through it are invisible.")


def test_no_two_automation_tests_claim_one_class_name():
    """Because the engine registers a test under its class name, case-blind."""
    by_folded_name = collections.defaultdict(list)
    for class_name, test_name, file_name in registrations():
        by_folded_name[class_name.lower()].append(
            (class_name, test_name, file_name))

    clashes = []
    for _folded, entries in sorted(by_folded_name.items()):
        if len(entries) > 1:
            clashes.append("\n".join(
                f"  {class_name}  ({file_name})\n      {test_name}"
                for class_name, test_name, file_name in sorted(entries)))

    assert not clashes, (
        "two automation tests are declared with class names that differ only "
        "in capitalisation, or not at all:\n\n"
        + "\n\n".join(clashes)
        + "\n\nThe engine registers a test under its CLASS name -- the macro "
          "passes TEXT(#TClass) to FAutomationTestBase -- and that registry is "
          "a TMap<FString, ...>, which compares keys without regard to case. "
          "The first registration wins and the second is refused with a log "
          "warning. The refused test does not run, is not listed, and is not "
          "counted, so the run reports a clean pass with one test silently "
          "missing. Issue #1666.\n"
          "\nThe fix is to rename one of the classes. Nothing about the "
          "readable test name needs to change.")
