"""A `.cpp` that names a project type as `Type::` includes, directly or through its own includes, the header that
DEFINES that type.

WHY THIS EXISTS. Issue #2022. Unreal's unity build pastes several `.cpp` files into one generated file, so a `.cpp`
that uses a type without including its header compiles while a sibling in the same unity file includes it. An edit
anywhere else can regroup the unity files, and the build then fails in a file the edit never touched. It happened
twice in build windows: `CataclysmEnemyBehaviourTests.cpp` called `UCataclysmPlayerClassStats::ChosenLevel()` without
`Character/CataclysmPlayerClassStats.h`, and `CataclysmPassiveTreeTests.cpp` used `UCataclysmSummonSkill::` reaching
only `AbilitySystem/CataclysmMinion.h`, which declares that class without defining it. Each cost a window. This rule
finds both on the commits before their fixes, and needs no build.

THE RULE. A qualified name, `Type::Member`, needs the type's full definition, which a forward declaration does not
give. So for every `.cpp` under `game/Source`, every `[UAFEI]Cataclysm*` type it qualifies must have its defining header
-- a `.h` holding `class`/`struct` `Name ... {` or `enum class Name ... {` -- among the headers its own `#include "..."`
lines reach, followed transitively inside `game/Source`. A type the `.cpp` defines itself is exempt. Comments are
removed before anything is read, and string literals before uses are searched, so a name inside `TEXT("...")` is not a
use.

WHAT IT DOES NOT CATCH, so a pass is not read as more than it is:
- a member reached through a pointer to a type that is only forward-declared (`Skill->Reach()` with no `::`), which
  also needs the definition but needs type inference to find;
- engine types and anything not named `*Cataclysm*`;
- an include that names a header by a path tail matching two files, which resolves to the first.
One build with unity switched off would catch all of these, and catches them only in a build window; this rule runs in
the fast suite.

The header map is built once per process (`index`), so the rule reads each file once.
"""
from __future__ import annotations

import collections
import functools
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"

#: What the rule must read, measured 2026-10-29 at 5a0d523f7: 422 `.cpp` files and 361 defined types. A floor well
#: below both, so the rule cannot pass by reading nothing.
AT_LEAST_CPP_FILES = 300
AT_LEAST_DEFINED_TYPES = 300

DEFINE = re.compile(
    r"^\s*(?:class|struct)\s+(?:\w+_API\s+)?([UAFEI]Cataclysm\w*)\b(?:\s*final)?\s*(?::[^;{]*)?\{", re.M)
ENUM = re.compile(r"^\s*enum\s+class\s+(ECataclysm\w*)\b[^;{]*\{", re.M)
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
QUALIFIED = re.compile(r"\b([UAFEI]Cataclysm\w*)::")


def without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def without_strings(text: str) -> str:
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)


def defined_in(text: str) -> set[str]:
    return {m.group(1) for m in DEFINE.finditer(text)} | {m.group(1) for m in ENUM.finditer(text)}


class Index:
    """Every `.h` and `.cpp` under one source root, read once: the text without comments, which header defines each
    type, and how each header is spelled when something includes it."""

    def __init__(self, files: dict[pathlib.PurePosixPath, str]):
        self.files = files
        self.by_tail: dict[str, list[pathlib.PurePosixPath]] = {}
        for path in files:
            for index in range(len(path.parts)):
                self.by_tail.setdefault("/".join(path.parts[index:]), []).append(path)
        self.definer: dict[str, set[pathlib.PurePosixPath]] = {}
        for path, text in files.items():
            if path.suffix == ".h":
                for name in defined_in(text):
                    self.definer.setdefault(name, set()).add(path)
        self.spelled: dict[pathlib.PurePosixPath, collections.Counter] = {}
        self._reach: dict[pathlib.PurePosixPath, set[pathlib.PurePosixPath]] = {}

    def resolve(self, name: str, here: pathlib.PurePosixPath) -> pathlib.PurePosixPath | None:
        local = here.parent / name
        if local in self.files:
            return local
        hits = self.by_tail.get(name.replace("\\", "/"), [])
        return hits[0] if hits else None

    def reached_from(self, path: pathlib.PurePosixPath) -> set[pathlib.PurePosixPath]:
        if path in self._reach:
            return self._reach[path]
        seen: set[pathlib.PurePosixPath] = set()
        stack = [path]
        while stack:
            current = stack.pop()
            for name in INCLUDE.findall(self.files[current]):
                header = self.resolve(name, current)
                if header is not None:
                    self.spelled.setdefault(header, collections.Counter())[name] += 1
                    if header not in seen:
                        seen.add(header)
                        stack.append(header)
        self._reach[path] = seen
        return seen

    def include_for(self, header: pathlib.PurePosixPath) -> str:
        """How to write the include: the spelling the tree already uses most, or the path inside its module."""
        spellings = self.spelled.get(header)
        if spellings:
            return spellings.most_common(1)[0][0]
        return "/".join(header.parts[1:])

    def problems(self) -> list[str]:
        out = []
        for path, text in sorted(self.files.items()):
            if path.suffix != ".cpp":
                continue
            own = defined_in(text)
            reach = self.reached_from(path)
            for name in sorted(set(QUALIFIED.findall(without_strings(text)))):
                if name in own or name not in self.definer or self.definer[name] & reach:
                    continue
                headers = sorted(self.definer[name])
                out.append((path, name, headers))
        # SPELLINGS ARE KNOWN ONLY ONCE EVERY FILE HAS BEEN FOLLOWED, so the message is worded after the walk.
        return [f"{path}: uses {name}:: but no include of this file reaches {header.name}, which defines it. "
                f'Add #include "{self.include_for(header)}" to {path.name}. Issue #2022.'
                for path, name, headers in out for header in headers[:1]]


def read_tree(root: pathlib.Path) -> dict[pathlib.PurePosixPath, str]:
    return {pathlib.PurePosixPath(path.relative_to(root).as_posix()):
            without_comments(path.read_text(encoding="utf-8", errors="replace"))
            for path in root.rglob("*") if path.suffix in (".h", ".cpp")}


@functools.lru_cache(maxsize=1)
def index() -> Index:
    return Index(read_tree(SOURCE))


def test_every_cpp_file_reaches_the_header_of_every_project_type_it_qualifies():
    tree = index()
    cpp = sum(1 for path in tree.files if path.suffix == ".cpp")
    assert cpp >= AT_LEAST_CPP_FILES, f"read only {cpp} .cpp files under {SOURCE}; the rule may be reading nothing"
    assert len(tree.definer) >= AT_LEAST_DEFINED_TYPES, (
        f"found only {len(tree.definer)} defined project types; the rule may be reading nothing")
    problems = tree.problems()
    assert not problems, "\n".join(problems)


# ---------------------------------------------------------------------------
# The rule on made-up text.
# ---------------------------------------------------------------------------

DEFINING = "#pragma once\nclass CATACLYSM_API UCataclysmThing : public UObject\n{\npublic:\n    static int Reach();\n};\n"
DECLARING = "#pragma once\nclass UCataclysmThing;\n"


def tree_of(**files: str) -> Index:
    return Index({pathlib.PurePosixPath(name): without_comments(text) for name, text in files.items()})


def test_a_file_that_includes_the_defining_header_passes():
    """CONTROL: directly, and through a second header."""
    direct = tree_of(**{"Mod/Thing/CataclysmThing.h": DEFINING,
                        "Mod/Uses.cpp": '#include "Thing/CataclysmThing.h"\nint A() { return UCataclysmThing::Reach(); }\n'})
    assert direct.problems() == []
    through = tree_of(**{"Mod/Thing/CataclysmThing.h": DEFINING,
                         "Mod/Thing/CataclysmBoth.h": '#pragma once\n#include "Thing/CataclysmThing.h"\n',
                         "Mod/Uses.cpp": '#include "Thing/CataclysmBoth.h"\nint A() { return UCataclysmThing::Reach(); }\n'})
    assert through.problems() == []


def test_a_file_that_reaches_only_a_forward_declaration_is_refused_and_told_what_to_include():
    """The second incident's shape. The message names the file, the type, the header, and the include to add, spelled
    the way another file in the tree already spells it."""
    tree = tree_of(**{"Mod/Thing/CataclysmThing.h": DEFINING,
                      "Mod/Thing/CataclysmDeclares.h": DECLARING,
                      "Mod/Other.cpp": '#include "Thing/CataclysmThing.h"\n',
                      "Mod/Uses.cpp": '#include "Thing/CataclysmDeclares.h"\nint A() { return UCataclysmThing::Reach(); }\n'})
    (problem,) = tree.problems()
    assert "Mod/Uses.cpp: uses UCataclysmThing::" in problem
    assert "CataclysmThing.h, which defines it" in problem
    assert 'Add #include "Thing/CataclysmThing.h" to Uses.cpp' in problem


def test_a_name_in_a_string_or_a_comment_is_not_a_use():
    tree = tree_of(**{"Mod/Thing/CataclysmThing.h": DEFINING,
                      "Mod/Uses.cpp": ('// UCataclysmThing::Reach is read elsewhere\n'
                                       'const TCHAR* Note = TEXT("UCataclysmThing::Reach");\n')})
    assert tree.problems() == []
