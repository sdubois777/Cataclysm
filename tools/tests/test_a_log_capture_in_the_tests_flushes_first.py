"""A log capture in the automation tests flushes the log before it reads its lines.

WHY THIS EXISTS. Issue #1522. Four test files each carry their own capture: a
struct that registers itself with `GLog->AddOutputDevice(this)`, keeps the lines
`Serialize` hands it, and later searches them. `FOutputDeviceRedirector` buffers
lines written from a thread that is not the primary one, so a capture that reads
its lines without calling `GLog->Flush()` first can see none of them. A test that
asserts a line WAS written would then fail loudly; a test that asserts a line was
NOT written would pass against the very bug it exists to catch.

All four flush today. The ruling of 2026-10-29 kept the four copies -- each is
used in its own file, and merging them would change one search from
case-insensitive to case-sensitive for no player benefit -- and guards the next
copy here instead.

THE RULE. In every struct under `game/Source/Cataclysm/Tests/` that calls
`GLog->AddOutputDevice`, each member function other than `Serialize` that reads
the container `Serialize` adds to must call `GLog->Flush()` before its first read
of it. Comments are removed before anything is read, so a comment naming the call
cannot satisfy the rule.

A GUARD PROOF CANNOT BE RUN IN THE ENGINE FOR THIS. Lines written on the game
thread are not buffered, so removing a flush fails no automation test today. The
rule is what notices.
"""
from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"

#: The captures known on 2026-10-29. The rule must find at least this many, so it
#: cannot pass by finding none.
KNOWN_AT_LEAST = 4

STRUCT = re.compile(r"\bstruct\s+(\w+)\s*:\s*public\s+FOutputDevice\b[^{;]*\{")
ADDED_TO = re.compile(r"\b(\w+)\s*\.\s*Add\s*\(")
FLUSH = "GLog->Flush()"


def without_comments(text: str) -> str:
    """The text with `//` and `/* */` comments replaced by spaces, string and
    character literals kept, so a `//` inside a string is not taken for one."""
    out = []
    index = 0
    length = len(text)
    while index < length:
        two = text[index:index + 2]
        char = text[index]
        if two == "//":
            end = text.find("\n", index)
            end = length if end < 0 else end
            out.append(" " * (end - index))
            index = end
        elif two == "/*":
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[index:end]))
            index = end
        elif char in "\"'":
            end = index + 1
            while end < length and text[end] != char:
                end += 2 if text[end] == "\\" else 1
            out.append(text[index:end + 1])
            index = end + 1
        else:
            out.append(char)
            index += 1
    return "".join(out)


def block_from(text: str, open_brace: int) -> str:
    """The text between the brace at `open_brace` and its matching close."""
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
    raise AssertionError(f"no closing brace for the block at {open_brace}")


def member_functions(body: str) -> list[tuple[str, str]]:
    """(header, body) of each block at the struct's own depth whose header has
    parentheses, which is a member function; nested structs and initialiser
    blocks are not."""
    found = []
    depth = 0
    last_end = 0
    index = 0
    while index < len(body):
        if body[index] == "{" and depth == 0:
            header = body[last_end:index]
            inner = block_from(body, index)
            close = index + len(inner) + 1
            name_part = header.strip().splitlines()[-1] if header.strip() else ""
            if "(" in header and not re.search(r"\bstruct\b", name_part):
                found.append((header.strip(), inner))
            index = close + 1
            last_end = index
            continue
        if body[index] in ";}":
            last_end = index + 1
        index += 1
    return found


def captures(text: str) -> list[tuple[str, str]]:
    """(name, body) of each struct in `text` that registers with GLog."""
    found = []
    for match in STRUCT.finditer(text):
        body = block_from(text, match.end() - 1)
        if "GLog->AddOutputDevice" in body:
            found.append((match.group(1), body))
    return found


def problems_in(name: str, body: str) -> list[str]:
    """What is wrong with one capture, or nothing."""
    functions = member_functions(body)
    serialize = [inner for header, inner in functions if re.search(r"\bSerialize\s*\(", header)]
    if not serialize:
        return [f"{name} registers with GLog and has no Serialize to read"]
    containers = set(ADDED_TO.findall(serialize[0]))
    if not containers:
        return [f"{name}'s Serialize adds to no container this rule can read"]
    out = []
    readers = 0
    for header, inner in functions:
        if re.search(r"\bSerialize\s*\(", header) or re.search(r"~|\b" + re.escape(name) + r"\s*\(", header):
            continue
        reads = [match.start() for container in containers
                 for match in re.finditer(r"\b" + re.escape(container) + r"\b", inner)]
        if not reads:
            continue
        readers += 1
        flush = inner.find(FLUSH)
        if flush < 0 or flush > min(reads):
            out.append(f"{name}: `{header.splitlines()[-1].strip()}` reads "
                       f"{sorted(containers)} without calling {FLUSH} first")
    if readers == 0:
        out.append(f"{name} registers with GLog and no member function reads what it keeps")
    return out


def test_every_log_capture_in_the_tests_flushes_before_it_reads():
    found = []
    problems = []
    for path in sorted(TESTS_DIR.glob("*.cpp")):
        text = without_comments(path.read_text(encoding="utf-8", errors="replace"))
        for name, body in captures(text):
            found.append(f"{path.name}:{name}")
            problems.extend(f"{path.name}: {problem}" for problem in problems_in(name, body))

    assert len(found) >= KNOWN_AT_LEAST, (
        f"the rule found {len(found)} log captures, fewer than the {KNOWN_AT_LEAST} known on "
        f"2026-10-29, so it may be reading nothing: {found}")
    assert not problems, "\n".join(problems)


SAMPLE = """
struct FGoodCapture : public FOutputDevice
{
    FGoodCapture() { if (GLog) { GLog->AddOutputDevice(this); } }
    virtual ~FGoodCapture() { if (GLog) { GLog->RemoveOutputDevice(this); } }
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type V, const class FName& C) override
    {
        Lines.Add(FString(Text));
    }
    TArray<FString> Kept() { if (GLog) { GLog->Flush(); } return Lines; }
    TArray<FString> Lines;
};
"""


def test_the_rule_passes_a_capture_that_flushes_and_refuses_one_that_does_not():
    """The CONTROL and the refusal, on made-up text: the same capture with the
    flush moved after the read, with it removed, and with it only in a comment."""
    (good,) = captures(without_comments(SAMPLE))
    assert problems_in(*good) == []

    late = SAMPLE.replace("{ if (GLog) { GLog->Flush(); } return Lines; }",
                          "{ TArray<FString> Copy = Lines; if (GLog) { GLog->Flush(); } return Copy; }")
    none = SAMPLE.replace("if (GLog) { GLog->Flush(); } ", "")
    commented = SAMPLE.replace("if (GLog) { GLog->Flush(); } ", "// GLog->Flush();\n ")
    for broken in (late, none, commented):
        (capture,) = captures(without_comments(broken))
        problems = problems_in(*capture)
        assert len(problems) == 1 and "Kept()" in problems[0], problems


def test_a_struct_that_does_not_register_with_glog_is_not_a_capture():
    """A console command's output device is handed straight to the command and
    never registers with GLog, so it has no flush to make; the rule leaves it."""
    console = SAMPLE.replace("GLog->AddOutputDevice(this);", "").replace("FGoodCapture", "FCommandOutput")
    assert captures(without_comments(console)) == []
