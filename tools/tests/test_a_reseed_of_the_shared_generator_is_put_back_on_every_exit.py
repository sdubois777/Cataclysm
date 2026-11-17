"""A test that reseeds the shared random generator puts it back on every exit.

WHAT WENT WRONG. `FMath::RandInit` is `srand` (engine `GenericPlatformMath.h`
about line 609), so it sets the generator that every later `FMath::FRand`,
`FRandRange` and `RandRange` in the same process draws from. One automation
test, `Cataclysm.DungeonModifierEffects.EveryWingsOfTheHostFlyoverOverFiftySeeds
MarksAFeatherAtThirtySeconds`, reseeds it once for each of fifty seeds and left
it there. Every later test that drew was then handed a sequence fixed by that
last seed and by how many numbers the tests in between drew, which is the likely
cause, read and not measured, of the one failure in #2251. Issue #2253.

THE RULE THIS HOLDS. Every `FMath::RandInit(` or `FMath::SRandInit(` in
`game/Source` sits in a function that also has an `ON_SCOPE_EXIT` calling the
same one of the two, and that `ON_SCOPE_EXIT` comes BEFORE the function's first
reseed. Before, because a scope-exit block only runs for exits after the line
that declares it: one written after a loop that has `return false` inside it
misses those returns. And the restore's argument is not a plain number, because
a fixed number would hand every later test one sequence again, which is the
fault itself.

THE SAME KIND, NOT EITHER. `RandInit` seeds the C runtime's generator and
`SRandInit` seeds `FMath::SRand`'s own; restoring one does nothing for the
other.

COMMENTS ARE STRIPPED BY TRACKING THEM, never by a line's first characters. A
code line can open with `*` (a dereference), and a comment can sit after code
on the same line. String and character literals are blanked too, so a brace or
a `//` inside one is not read as code.

WHAT IT DOES NOT CATCH, SAID PLAINLY. A reseed through any other spelling than
`FMath::`, `FPlatformMath::`, `FGenericPlatformMath::` or a bare `srand(`, a
restore done by some other means than `ON_SCOPE_EXIT`, and whether the restore
in fact runs (that is the engine's macro). Nor does it say which later tests drew
from a left-over state: the run order of the automation tests was not read.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"

#: One token of C++ source, for telling code from comments and literals. The
#: number comes before the character literal so a digit separator (`1'200.0f`,
#: used freely in the test files) is not read as the opening of one.
TOKEN = re.compile(
    r"(?P<line>//[^\n]*)"
    r"|(?P<block>/\*.*?\*/)"
    r"|(?P<string>\"(?:\\.|[^\"\\\n])*\")"
    r"|(?P<number>\.?\d(?:[eEpP][+-]|[\w.'])*)"
    r"|(?P<ident>[A-Za-z_]\w*)"
    r"|(?P<char>'(?:\\.|[^'\\\n])+')",
    re.DOTALL)

#: The prefixes a raw string literal opens with. This file does not read raw
#: strings, and none is in `game/Source` (searched 2026-10-23), so one is refused
#: rather than misread.
RAW_STRING_PREFIXES = {"R", "LR", "uR", "UR", "u8R"}

#: A reseed, and which generator it sets. `srand` is what `RandInit` calls.
RESEED = re.compile(
    r"\b(?:(?:FMath|FPlatformMath|FGenericPlatformMath)\s*::\s*(RandInit|SRandInit)|(srand))\s*\(")

#: The engine's scope-exit macro, opening its block.
SCOPE_EXIT = re.compile(r"\bON_SCOPE_EXIT\s*$")

#: What may stand between a function's closing parenthesis and its body.
FUNCTION_TAIL = re.compile(
    r"\)\s*(?:(?:const|override|final|noexcept|mutable|volatile|&&|&)\s*"
    r"|->\s*[\w:<>,*&\s]+?\s*)*$")

#: A restore whose argument is this is a fixed number.
FIXED_NUMBER = re.compile(r"\s*[-+]?\s*\(?\s*\d[\w.']*\s*\)?\s*")


def code_only(text: str) -> str:
    """The text with every comment and the inside of every literal blanked.

    Same length and same line breaks as the input, so a position in the result
    is a position, and a line number, in the source.
    """
    out: list[str] = []
    at = 0
    previous: re.Match[str] | None = None
    for match in TOKEN.finditer(text):
        out.append(text[at:match.start()])
        kind = match.lastgroup
        token = match.group()
        if kind in ("line", "block"):
            out.append(re.sub(r"[^\n]", " ", token))
        elif kind in ("string", "char"):
            if (kind == "string" and previous is not None and previous.end() == match.start()
                    and previous.group() in RAW_STRING_PREFIXES):
                raise ValueError("a raw string literal, which this check does not read")
            out.append(token[0] + re.sub(r"[^\n]", " ", token[1:-1]) + token[-1])
        else:
            out.append(token)
        at = match.end()
        previous = match
    out.append(text[at:])
    return "".join(out)


def blocks(code: str) -> list[tuple[int, int, str]]:
    """Every brace block: where it opens, where it closes, and what it is.

    What it is: "scope_exit" for an `ON_SCOPE_EXIT` body, "function" for a block
    opened straight after a closing parenthesis (a function, a lambda, or a
    `for`, `if` or `while` body -- the outermost of these is the function), and
    "other" for a namespace, a class or a bare block.
    """
    found: list[tuple[int, int, str]] = []
    stack: list[int] = []
    for match in re.finditer(r"[{}]", code):
        if match.group() == "{":
            stack.append(match.start())
            continue
        if not stack:
            raise ValueError("a closing brace with nothing open")
        opened = stack.pop()
        before = code[max(0, opened - 400):opened].rstrip()
        if SCOPE_EXIT.search(before):
            kind = "scope_exit"
        elif FUNCTION_TAIL.search(before):
            kind = "function"
        else:
            kind = "other"
        found.append((opened, match.start(), kind))
    if stack:
        raise ValueError("an opening brace never closed")
    return found


def argument_of(code: str, open_paren: int) -> str:
    """The text between a call's parenthesis and the one matching it."""
    depth = 0
    for at in range(open_paren, len(code)):
        if code[at] == "(":
            depth += 1
        elif code[at] == ")":
            depth -= 1
            if depth == 0:
                return code[open_paren + 1:at]
    raise ValueError("a call never closed")


def reseeds_and_verdicts(text: str) -> list[tuple[int, str, str | None]]:
    """Each reseed in the text: its line, its generator, and what is wrong.

    The last is None when the reseed's function has an `ON_SCOPE_EXIT` calling
    the same generator's seed with a value that is not a fixed number, declared
    before the function's first reseed of that generator.
    """
    code = code_only(text)
    every = blocks(code)
    scope_exits = [(start, end) for start, end, kind in every if kind == "scope_exit"]
    functions = [(start, end) for start, end, kind in every if kind == "function"]

    calls = []
    for match in RESEED.finditer(code):
        generator = match.group(1) or "RandInit"
        inside_a_restore = any(start < match.start() < end for start, end in scope_exits)
        calls.append((match.start(), match.end() - 1, generator, inside_a_restore))

    results: list[tuple[int, str, str | None]] = []
    for at, _paren, generator, inside_a_restore in calls:
        if inside_a_restore:
            continue
        line = code.count("\n", 0, at) + 1
        enclosing = [(start, end) for start, end in functions if start < at < end]
        if not enclosing:
            results.append((line, generator, "is in no function"))
            continue
        start, end = min(enclosing)
        first = min(other for other, _p, kind, restore in calls
                    if not restore and kind == generator and start < other < end)
        restores = [
            (exit_start, paren)
            for exit_start, exit_end in scope_exits if start < exit_start < end
            for other, paren, kind, restore in calls
            if restore and kind == generator and exit_start < other < exit_end]
        if not restores:
            problem = f"has no ON_SCOPE_EXIT in its function that calls {generator}"
        elif not any(exit_start < first for exit_start, _paren in restores):
            problem = "is restored by an ON_SCOPE_EXIT declared after the first reseed"
        elif all(FIXED_NUMBER.fullmatch(argument_of(code, paren))
                 for exit_start, paren in restores if exit_start < first):
            problem = "is restored to a fixed number"
        else:
            problem = None
        results.append((line, generator, problem))
    return results


def source_files_that_name_a_reseed() -> list[pathlib.Path]:
    """Every source file holding the text of a reseed, comments included.

    A file without the text cannot hold the call, so the slower reading is kept
    to the files that might.
    """
    return [
        path for path in sorted(SOURCE_ROOT.rglob("*"))
        if path.suffix in (".cpp", ".h")
        and re.search(r"RandInit|srand", path.read_text(encoding="utf-8", errors="replace"))]


def test_every_reseed_of_the_shared_generator_is_put_back_on_every_exit():
    found = []
    for path in source_files_that_name_a_reseed():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line, generator, problem in reseeds_and_verdicts(text):
            found.append((path.relative_to(REPO_ROOT).as_posix(), line, generator, problem))

    print(f"{len(found)} reseed(s) of the shared generator found under game/Source")
    assert found, (
        "no FMath::RandInit or FMath::SRandInit call was found under game/Source. "
        "There was one on 2026-10-23, in CataclysmDungeonModifierEffectsTests.cpp. "
        "Either it was removed, and this check can go, or the search stopped "
        "finding it, and the check below is checking nothing.")
    wrong = [f"{path}:{line} {generator} {problem}"
             for path, line, generator, problem in found if problem]
    assert not wrong, (
        f"of {len(found)} reseed(s), these leave the shared generator seeded after "
        "the function returns (#2253):\n" + "\n".join(wrong))


#: A reseed with nothing putting it back: the shape #2253 found.
NO_RESTORE = """
bool FSomeTest::RunTest(const FString& Parameters)
{
    for (int32 Seed = 1; Seed <= 50; ++Seed)
    {
        FMath::RandInit(Seed);
        if (Seed == 7)
        {
            return false;
        }
    }
    return true;
}
"""

#: The shape this change built: the restore before the loop.
RESTORED_BEFORE = """
bool FSomeTest::RunTest(const FString& Parameters)
{
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    ON_SCOPE_EXIT { FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles())); };
    for (int32 Seed = 1; Seed <= 50; ++Seed)
    {
        FMath::RandInit(Seed);
        *Out = 1'200;
        if (Seed == 7)
        {
            return false;
        }
    }
    return true;
}
"""


def test_a_reseed_with_no_restore_is_refused():
    """The control: the shape the game's test had before #2253 was fixed."""
    assert reseeds_and_verdicts(NO_RESTORE) == [
        (6, "RandInit", "has no ON_SCOPE_EXIT in its function that calls RandInit")]


def test_a_restore_before_the_loop_is_accepted():
    """So the refusals below are of the shapes they name, not of everything."""
    assert reseeds_and_verdicts(RESTORED_BEFORE) == [(8, "RandInit", None)]


def test_a_restore_declared_after_the_reseed_is_refused():
    """It would miss the `return false` inside the loop."""
    text = NO_RESTORE.replace(
        "    return true;",
        "    ON_SCOPE_EXIT { FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles())); };\n"
        "    return true;")
    assert reseeds_and_verdicts(text) == [
        (6, "RandInit", "is restored by an ON_SCOPE_EXIT declared after the first reseed")]


def test_a_restore_to_a_fixed_number_is_refused():
    """A fixed number hands every later test one sequence again."""
    text = RESTORED_BEFORE.replace(
        "FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()))", "FMath::RandInit(0)")
    assert reseeds_and_verdicts(text) == [(8, "RandInit", "is restored to a fixed number")]


def test_a_restore_of_the_other_generator_is_refused():
    """`SRandInit` does not put back what `RandInit` set."""
    text = RESTORED_BEFORE.replace(
        "ON_SCOPE_EXIT { FMath::RandInit(static_cast<int32>",
        "ON_SCOPE_EXIT { FMath::SRandInit(static_cast<int32>")
    assert reseeds_and_verdicts(text) == [
        (8, "RandInit", "has no ON_SCOPE_EXIT in its function that calls RandInit")]


def test_a_restore_in_a_comment_or_a_string_is_not_a_restore():
    """Comments are tracked, not judged by a line's first characters."""
    in_a_block_comment = RESTORED_BEFORE.replace(
        "    ON_SCOPE_EXIT { FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles())); };",
        "    /* the restore,\n"
        "    ON_SCOPE_EXIT { FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles())); }; */")
    in_a_line_comment = RESTORED_BEFORE.replace(
        "    ON_SCOPE_EXIT { FMath::RandInit(",
        "    int32 Unused = 0; // ON_SCOPE_EXIT { FMath::RandInit(")
    in_a_string = RESTORED_BEFORE.replace(
        "    ON_SCOPE_EXIT { FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles())); };",
        '    const TCHAR* Note = TEXT("ON_SCOPE_EXIT { FMath::RandInit(1); };");')
    for text in (in_a_block_comment, in_a_line_comment, in_a_string):
        verdicts = reseeds_and_verdicts(text)
        assert [problem for _line, _generator, problem in verdicts] == [
            "has no ON_SCOPE_EXIT in its function that calls RandInit"], text


def test_a_reseed_in_a_comment_is_not_counted():
    text = NO_RESTORE.replace("        FMath::RandInit(Seed);", "        // FMath::RandInit(Seed);")
    assert reseeds_and_verdicts(text) == []
