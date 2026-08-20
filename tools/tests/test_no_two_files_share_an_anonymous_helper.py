"""Two files in one Unreal module must not define the same private helper.

WHAT WENT WRONG, ON 2026-08-20. `CataclysmImpCharacter.cpp` and
`CataclysmCorruptedSentinelCharacter.cpp` each carried an identical

    namespace
    {
        FString ClipPathIn(const TCHAR* Folder, const TCHAR* Name) { ... }
    }

which is the ordinary way to keep a helper private to one file. **Unreal merges a
module's `.cpp` files into one translation unit**, so the moment both landed in
the same unity blob the two definitions collided:

    error C2084: function 'FString `anonymous-namespace'::ClipPathIn(
        const TCHAR *,const TCHAR *)' already has a body

WHY IT WAS NOT CAUGHT BEFORE IT MERGED, WHICH IS THE INTERESTING HALF.
UnrealBuildTool uses `git status` to decide which files to compile on their own
rather than in the blob -- the "adaptive non-unity working set" it prints at the
top of every build. While either file was modified or untracked it was kept OUT
of the blob, so the collision could not happen. Both creatures were built,
tested, and merged with a clean build every time. **The build passed for a reason
that went away the moment the work was committed**, and the first failure was on
`development`.

WHY A PYTHON TEST RATHER THAN LEAVING IT TO THE COMPILER. Continuous integration
never builds the C++ -- issue #20 is the self-hosted runner that would -- so the
compiler is not consulted on a pull request at all. This runs on every one.

WHAT IT DOES NOT CATCH. Anything that is not a function definition: two files
declaring the same file-scope variable collide in exactly the same way, and this
does not look for those. The project names its console variables after the
creature they belong to, which is what has kept that from happening so far.
"""

from __future__ import annotations

import collections
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"

#: A function definition at the top level of a block: a return type, a name, an
#: argument list, and an opening brace on the same line or the next.
#:
#: DELIBERATELY BLUNT. This is not a C++ parser and does not need to be. It is
#: looking for one shape -- a free function defined inside `namespace { }` -- and
#: a false positive here costs somebody a rename, while a false negative costs a
#: broken `development`.
FUNCTION = re.compile(
    r"^[ \t]*(?:static\s+|inline\s+|constexpr\s+)*"
    r"[A-Za-z_][\w:<>,*&\s]*?[\s*&]"
    r"([A-Za-z_]\w*)\s*\([^;]*?\)\s*(?:const\s*)?\{",
    re.MULTILINE)


def module_of(path: pathlib.Path) -> str:
    """Which Unreal module a source file belongs to.

    A unity blob is per module, so two files in DIFFERENT modules may define the
    same helper without colliding.
    """
    parts = path.relative_to(SOURCE_ROOT).parts
    return parts[0] if parts else "?"


def anonymous_namespace_blocks(text: str) -> list[str]:
    """The body of every `namespace {` block in the file.

    Found by brace counting rather than by a regex, because a helper's own braces
    would end the match otherwise.
    """
    blocks = []
    for opening in re.finditer(r"^\s*namespace\s*\n?\s*\{", text, re.MULTILINE):
        start = text.index("{", opening.start())
        depth = 0
        for index in range(start, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    blocks.append(text[start + 1:index])
                    break
    return blocks


def strip_comments(text: str) -> str:
    """So a function named in a comment is not counted as one defined."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def helpers_by_module() -> dict[str, dict[str, list[str]]]:
    """Every anonymous-namespace function, by module and then by name."""
    found: dict[str, dict[str, list[str]]] = collections.defaultdict(
        lambda: collections.defaultdict(list))

    for path in sorted(SOURCE_ROOT.rglob("*.cpp")):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for block in anonymous_namespace_blocks(text):
            for match in FUNCTION.finditer(block):
                found[module_of(path)][match.group(1)].append(path.name)
    return found


def test_the_search_finds_something_to_look_at():
    """A guard over an empty list is a guard that cannot fail.

    If the shape this looks for disappears from the project -- because nobody
    writes anonymous-namespace helpers any more, or because the pattern above
    stopped matching them -- this test would pass for ever while checking
    nothing. So it asserts there is something to check first.
    """
    total = sum(len(names) for names in helpers_by_module().values())
    assert total > 0, (
        "no anonymous-namespace helper was found anywhere under game/Source/. "
        "Either the project stopped using them, or the FUNCTION pattern in this "
        "file stopped matching what they look like. Either way the duplicate "
        "check below is now checking nothing.")


def test_no_two_files_in_one_module_define_the_same_private_helper():
    """Because Unreal compiles a module's files as one translation unit."""
    clashes = []
    for module, names in sorted(helpers_by_module().items()):
        for name, files in sorted(names.items()):
            if len(set(files)) > 1:
                clashes.append(
                    f"  {module}: {name} is defined in {', '.join(sorted(set(files)))}")

    assert not clashes, (
        "two files in one Unreal module define the same helper inside an "
        "anonymous namespace, and a module's .cpp files are merged into one "
        "translation unit, so the compiler will report 'function already has a "
        "body' as soon as both land in the same unity blob:\n"
        + "\n".join(clashes)
        + "\n\nIt will NOT necessarily fail before then. UnrealBuildTool keeps "
          "modified files out of the blob -- the adaptive non-unity working "
          "set -- so a local build of work in progress compiles fine and the "
          "first failure is on development. That is what happened on "
          "2026-08-20 with ClipPathIn.\n"
          "\nThe fix is to give the helper one home. "
          "ACataclysmEnemyCharacter::ClipPathIn is the worked example.")


def test_the_helper_the_incident_produced_still_has_one_home():
    """The specific case, named, so a reader of a failure knows the story.

    Written as its own check rather than trusted to the general one above,
    because the general one would also pass if `ClipPathIn` were deleted
    entirely and both creatures went back to writing paths out by hand.
    """
    base = (SOURCE_ROOT / "Cataclysm" / "Character"
            / "CataclysmEnemyCharacter.h")
    if not base.is_file():
        pytest.fail(f"{base} does not exist")

    assert "ClipPathIn" in base.read_text(encoding="utf-8", errors="replace"), (
        "ACataclysmEnemyCharacter no longer declares ClipPathIn. Two creatures "
        "needed it and each had its own copy, which broke the build; if it has "
        "been removed, check that neither of them has taken a private copy "
        "back.")
