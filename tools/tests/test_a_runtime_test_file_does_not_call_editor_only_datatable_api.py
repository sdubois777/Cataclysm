"""A runtime module must not call an editor-only UDataTable function unguarded.

WHAT WENT WRONG, ON 2026-09-01. `CataclysmDataTableTests.cpp` gained a
comparison of each DataTable asset against the CSV it was imported from, and it
made that comparison by calling `UDataTable::GetTableAsCSV` on both sides. That
function is declared inside `#if WITH_EDITOR` in the engine. The test file is
compiled under `WITH_AUTOMATION_TESTS`, which is **true in a Development
packaged build and does not imply `WITH_EDITOR`**, so the call exists for the
`CataclysmEditor` target and does not exist for the packaged `Cataclysm` target:

    CataclysmDataTableTests.cpp(610,37): error C2039: 'GetTableAsCSV' is not a
        member of 'UDataTable'
    Result: Failed (OtherCompilationError)

The packaged target did not compile for a day and nobody saw it. Issue #1196.

WHY NOBODY SAW IT, WHICH IS THE INTERESTING HALF. Continuous integration never
builds the C++ at all -- issue #20 is the self-hosted runner that would -- so a
change that breaks one of the two engine targets passes every check the project
has. Ordinary work by hand builds the editor target, because that is what the
automation tests run against; the packaged target is only ever built
deliberately. So neither the machine nor the person was looking.

WHY A PYTHON TEST. Same reason as
`test_no_two_files_share_an_anonymous_helper.py`: the compiler is not consulted
on a pull request, and this one runs on every one.

WHAT IT DOES NOT CATCH, SAID PLAINLY. This looks for one API family --
`UDataTable`'s editor-only members -- and nothing else. Every other editor-only
engine function called from a runtime module has exactly the same fault and this
does not look for it. Widening the list is easy; guessing at the whole engine is
not, and a list that pretended to be complete would be worse than one that says
it is not. Building both targets, as `game/README.md` describes, remains the
only complete check until #20 lands.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"

sys.path.insert(0, str(REPO_ROOT / "tools"))
from unreal_build import ENGINE_ROOT  # noqa: E402

#: The engine header that declares the functions below.
DATATABLE_HEADER = (ENGINE_ROOT / "Engine" / "Source" / "Runtime" / "Engine"
                    / "Classes" / "Engine" / "DataTable.h")

#: The modules that ship in the packaged `Cataclysm` target.
#:
#: `CataclysmEditor` is deliberately absent. It is an editor-only module, is
#: excluded from the packaged target by `Source/Cataclysm.Target.cs`, and may
#: call anything the editor offers.
RUNTIME_MODULES = ("Cataclysm", "CataclysmEmpire")

#: UDataTable members the engine declares inside `#if WITH_EDITOR`.
#:
#: WRITTEN OUT HERE RATHER THAN READ FROM THE ENGINE, because continuous
#: integration has no Unreal install and a test that skipped there would not
#: guard the one place the fault can reach `development` unseen. The list is
#: held to the engine by `test_the_editor_only_list_still_matches_the_engine`
#: below, which runs wherever the engine IS present -- that is, on the machine
#: making the change.
EDITOR_ONLY_DATATABLE_MEMBERS = (
    "CleanBeforeStructChange",
    "CopyImportOptions",
    "GetColumnTitles",
    "GetTableAsCSV",
    "GetTableAsJSON",
    "GetTableAsString",
    "GetTableData",
    "GetUniqueColumnTitles",
    "RestoreAfterStructChange",
    "WriteRowAsJSON",
    "WriteTableAsJSON",
    "WriteTableAsJSONObject",
)

#: A call to one of the members above: `->Name(` or `.Name(`.
CALL = re.compile(
    r"(?:->|\.)(" + "|".join(EDITOR_ONLY_DATATABLE_MEMBERS) + r")\s*\(")

#: A preprocessor conditional line, with its directive and its condition.
DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def source_files() -> list[pathlib.Path]:
    """Every C++ source and header in the modules that ship."""
    files: list[pathlib.Path] = []
    for module in RUNTIME_MODULES:
        root = SOURCE_ROOT / module
        if root.is_dir():
            files.extend(sorted(root.rglob("*.cpp")))
            files.extend(sorted(root.rglob("*.h")))
    return files


def lines_guarded_by_with_editor(text: str) -> set[int]:
    """The 1-based line numbers that only compile when WITH_EDITOR is true.

    DELIBERATELY BLUNT, in the same way the anonymous-helper test's regular
    expression is. This is not a preprocessor. It tracks nesting and asks one
    question of each `#if`: is the condition exactly `WITH_EDITOR`. That covers
    every guard this project writes and the engine's own style.

    A `#else` attached to such an `#if` is NOT guarded -- that branch is what
    the packaged build compiles, and it is exactly where issue #1196's fix put
    its skip report, so getting this half wrong would make the fix look broken.
    """
    guarded: set[int] = set()

    #: One entry per open conditional: whether the branch being read right now
    #: is the `WITH_EDITOR`-true branch.
    stack: list[bool] = []

    for number, line in enumerate(text.splitlines(), start=1):
        match = DIRECTIVE.match(line)
        if match:
            directive, rest = match.group(1), match.group(2).strip()
            if directive in ("if", "ifdef", "ifndef"):
                stack.append(directive == "if" and rest == "WITH_EDITOR")
            elif directive == "elif":
                if stack:
                    stack[-1] = rest == "WITH_EDITOR"
            elif directive == "else":
                if stack:
                    # The other side of a WITH_EDITOR guard is the packaged
                    # build's branch, so it stops being guarded here.
                    stack[-1] = False
            elif directive == "endif":
                if stack:
                    stack.pop()
            # The directive line itself is never a call site.
            continue

        if any(stack):
            guarded.add(number)

    return guarded


def test_no_runtime_module_calls_an_editor_only_datatable_function_unguarded():
    """The fault of #1196, in the shape it actually took."""
    offences: list[str] = []

    for path in source_files():
        text = path.read_text(encoding="utf-8")
        if not CALL.search(text):
            continue

        guarded = lines_guarded_by_with_editor(text)
        for number, line in enumerate(text.splitlines(), start=1):
            found = CALL.search(line)
            if found and number not in guarded:
                offences.append(
                    f"{path.relative_to(REPO_ROOT).as_posix()}:{number} calls "
                    f"UDataTable::{found.group(1)}, which the engine declares "
                    f"inside #if WITH_EDITOR: {line.strip()}")

    assert not offences, (
        "These calls compile for the CataclysmEditor target and do not exist "
        "for the packaged Cataclysm target, so the packaged build fails with "
        "error C2039 and continuous integration cannot see it. Put the call "
        "inside #if WITH_EDITOR and say what the test no longer checks in the "
        "#else, as CataclysmDataTableTests.cpp does. Issue #1196.\n  "
        + "\n  ".join(offences))


@pytest.mark.skipif(not DATATABLE_HEADER.is_file(),
                    reason=f"no Unreal install at {ENGINE_ROOT}; the list "
                           "above cannot be checked against the engine here. "
                           "This is expected on continuous integration and "
                           "means the drift check did not run, not that it "
                           "passed.")
def test_the_editor_only_list_still_matches_the_engine():
    """What the engine guards, and what this file says it guards, are the same.

    Without this, the list above is a snapshot of Unreal 5.8 that would quietly
    rot across an engine upgrade -- gaining a false entry nobody notices, or
    missing a newly editor-only function, which is the direction that lets
    #1196 happen again.
    """
    text = DATATABLE_HEADER.read_text(encoding="utf-8", errors="replace")
    guarded = lines_guarded_by_with_editor(text)

    #: A member declaration inside the class: a name followed by `(`.
    declaration = re.compile(r"\b([A-Z]\w+)\s*\(")

    from_engine: set[str] = set()
    for number, line in enumerate(text.splitlines(), start=1):
        if number not in guarded or line.lstrip().startswith(("*", "//", "/*")):
            continue
        for name in declaration.findall(line):
            from_engine.add(name)

    listed = set(EDITOR_ONLY_DATATABLE_MEMBERS)
    missing = sorted(listed - from_engine)

    assert not missing, (
        f"{sorted(listed)} are listed here as editor-only UDataTable members, "
        f"but {missing} were not found inside a #if WITH_EDITOR block in "
        f"{DATATABLE_HEADER}. Either the engine moved them out of the guard, "
        "in which case delete them from the list, or this file's reading of "
        "the header has broken.")
