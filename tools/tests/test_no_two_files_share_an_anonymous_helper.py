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

VARIABLES AND TYPES TOO, SINCE 2026-11-22. This said it did not look for a file-scope
variable, which collides in exactly the same way. Window lf-w1's second build then
stopped on one: `constexpr int32 RarityCount` in an anonymous namespace in
`CataclysmLootFilter.cpp`, `CataclysmLootFilterLayout.cpp` and `CataclysmItem.cpp`
("error C2374: 'anonymous-namespace'::RarityCount: redefinition"). The function
check could not see it. The last two checks read each anonymous namespace's own
top-level declarations -- a variable with an initializer, and a struct, class or
enum -- and refuse the same name in two files of one module.
"""

from __future__ import annotations

import collections
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"

#: C++ keywords that are followed by a parenthesis, and so can take the place of a
#: function's name in the shape below. `else if (Ready)` with its brace on the
#: next line reads as a function called `if` returning `else`.
#:
#: IT FAILED THIS CHECK ON 2026-09-10. Issue #1545's reproduction command,
#: `CataclysmClothStress.cpp`, put two `else if` branches inside an anonymous
#: namespace, `CataclysmFloorGenerator.cpp` already had one, and the check
#: reported a helper named `if` defined in both. No function can be named after a
#: keyword, so refusing them loses nothing, and a rename could never have fixed
#: that failure.
KEYWORDS = ("if", "for", "while", "switch", "catch", "return")

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
    r"(?!(?:" + "|".join(KEYWORDS) + r")\b)"
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


def test_a_statement_is_not_mistaken_for_a_helper():
    """`else if (...)` and its brace are not a function called `if`.

    THE SECOND HALF IS WHAT MAKES THE FIRST MEAN ANYTHING. A pattern that matched
    nothing at all would pass the first half, so a real helper laid out the same
    way -- its brace on the next line, indented inside a namespace -- must still
    be found.
    """
    statement = "\t\t\telse if (Class == ACataclysmSuccubusCharacter::StaticClass())\n\t\t\t{\n"
    helper = "\tFString ClipPathIn(const TCHAR* Folder, const TCHAR* Name)\n\t{\n"

    assert [match.group(1) for match in FUNCTION.finditer(statement)] == [], (
        "The FUNCTION pattern takes an `else if` statement for a helper "
        "definition, so any two files with an `else if` inside an anonymous "
        "namespace fail the duplicate check with a helper named `if`. See the "
        "note on KEYWORDS.")
    assert [match.group(1) for match in FUNCTION.finditer(helper)] == ["ClipPathIn"], (
        "The FUNCTION pattern no longer finds a helper written with its brace "
        "on the next line, which is how this project writes them.")


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


def top_level(block: str) -> str:
    """The block with every nested brace pair's contents removed, keeping the braces.

    So only the namespace's own declarations remain: a function body, a struct's
    members and an initializer list's contents are gone, and a function definition
    reads `Type Name(args) {}`.
    """
    out, depth = [], 0
    for ch in block:
        if ch == "{":
            depth += 1
            if depth == 1:
                out.append("{")
        elif ch == "}":
            depth -= 1
            if depth == 0:
                out.append("}")
        elif depth == 0:
            out.append(ch)
    return "".join(out)


#: A file-scope variable with an initializer, after `top_level` and split at `;`:
#: `constexpr int32 RarityCount = ...`, `const FName Tag{...}` (read as `{}`).
VARIABLE = re.compile(
    r"^\s*(?:static\s+|inline\s+|constexpr\s+|const\s+)*"
    r"[A-Za-z_][\w:<>,*&\s]*?[\s*&]"
    r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?(?:=|\{)")

#: A struct, class or enum defined in the namespace.
TYPE = re.compile(r"^\s*(?:struct|class|enum(?:\s+class)?)\s+([A-Za-z_]\w*)\s*(?::[^{]*)?\{")

#: What a statement may begin with and be neither a variable nor a type.
NOT_A_DECLARATION = ("using", "namespace", "typedef", "template", "friend")


def is_a_function_definition(statement: str) -> bool:
    """A function definition reads `Type Name(args) {}` after `top_level`; the function check reads those."""
    return ")" in statement.split("=")[0] and "{}" in statement


def variables_and_types_in(block: str) -> tuple[list[str], list[str]]:
    """The names of the variables and of the types one anonymous namespace block declares at its own top level."""
    variables, types = [], []
    for statement in top_level(block).split(";"):
        statement = statement.strip()
        if not statement or statement.startswith(NOT_A_DECLARATION):
            continue
        kind = TYPE.match(statement)
        if kind:
            types.append(kind.group(1))
            continue
        if is_a_function_definition(statement):
            continue
        match = VARIABLE.match(statement)
        if match:
            variables.append(match.group(1))
    return variables, types


def declarations_by_module() -> tuple[dict[str, dict[str, list[str]]], dict[str, dict[str, list[str]]]]:
    """Every anonymous-namespace variable and type, by module and then by name."""
    variables: dict[str, dict[str, list[str]]] = collections.defaultdict(lambda: collections.defaultdict(list))
    types: dict[str, dict[str, list[str]]] = collections.defaultdict(lambda: collections.defaultdict(list))
    for path in sorted(SOURCE_ROOT.rglob("*.cpp")):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for block in anonymous_namespace_blocks(text):
            found_variables, found_types = variables_and_types_in(block)
            for name in found_variables:
                variables[module_of(path)][name].append(path.name)
            for name in found_types:
                types[module_of(path)][name].append(path.name)
    return variables, types


def clashes_in(table: dict[str, dict[str, list[str]]]) -> list[str]:
    return [f"  {module}: {name} in {', '.join(sorted(set(files)))}"
            for module, names in sorted(table.items()) for name, files in sorted(names.items())
            if len(set(files)) > 1]


def test_the_variable_and_type_search_reads_what_it_should():
    """A constant and a struct are found, a function and its locals are not taken for variables, and the search of
    game/Source is not empty."""
    block = ("\tconstexpr int32 RarityCount = 8;\n"
             "\tstruct FHold { int32 A = 0; };\n"
             "\tFString PieceName(const FCataclysmItem& Item)\n\t{\n\t\tconst int32 Inner = 1;\n\t\treturn {};\n\t}\n")
    assert variables_and_types_in(block) == (["RarityCount"], ["FHold"]), (
        f"the search read {variables_and_types_in(block)} from a constant, a struct and a function")
    variables, types = declarations_by_module()
    assert sum(len(f) for m in variables.values() for f in m.values()) >= 50, "the variable search found almost nothing"
    assert sum(len(f) for m in types.values() for f in m.values()) >= 5, "the type search found almost nothing"


def test_no_two_files_in_one_module_define_the_same_private_variable_or_type():
    """The same unity-blob collision as a helper's, for a file-scope variable or a struct, class or enum."""
    variables, types = declarations_by_module()
    found = clashes_in(variables) + clashes_in(types)
    assert not found, (
        "two files in one Unreal module define the same name inside an anonymous namespace, and a module's .cpp "
        "files are merged into one translation unit, so the compiler stops on a redefinition (C2374 for a variable, "
        "C2011 for a type) as soon as both land in the same unity blob:\n" + "\n".join(found)
        + "\n\nGive one of them a name no other file uses. Window lf-w1's second build stopped on RarityCount this way.")
