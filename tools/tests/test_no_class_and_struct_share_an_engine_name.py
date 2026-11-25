"""No reflected class and reflected struct in game/Source share an engine name.

WHY THIS EXISTS. Unreal Header Tool names every reflected type by its C++ name with the one-letter prefix taken off:
`UCataclysmFoo` and `ACataclysmFoo` (classes) and `FCataclysmFoo` (a struct) are all the engine name `CataclysmFoo`.
Two types with the same engine name stop the build before any file compiles:

    Error: Class 'UCataclysmLootFilter' shares engine name 'CataclysmLootFilter' with struct 'FCataclysmLootFilter'

Window lf-w1's first build stopped on exactly that, on 2026-11-22 (#2846): a function library named after the struct it
works on. It was the second build that week stopped by a name the compiler or header tool rejects. Continuous
integration compiles no C++, so this reads the headers and catches it on a pull request.

WHAT IS CHECKED: every `UCLASS(...)` followed by `class [API] UName` or `AName`, and every `USTRUCT(...)` followed by
`struct [API] FName`, in every header under game/Source; no engine name appears twice, across all modules, since every
module's types share one engine namespace for this rule.

WHAT IS NOT: enums (`UENUM`, the `E` prefix, whose engine name keeps its prefix), interfaces, and types the header tool
does not reflect. A floor holds the reading honest: game/Source declares hundreds of reflected types.
"""

from __future__ import annotations

import collections
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"

#: A reflection macro, its arguments (which may span lines), and the declaration that follows it.
CLASS = re.compile(r"\bUCLASS\s*\((?:[^()]|\([^()]*\))*\)\s*class\s+(?:[A-Z0-9_]+_API\s+)?([UA])(\w+)")
STRUCT = re.compile(r"\bUSTRUCT\s*\((?:[^()]|\([^()]*\))*\)\s*struct\s+(?:[A-Z0-9_]+_API\s+)?F(\w+)")


def without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def reflected_types() -> dict[str, list[str]]:
    """Every reflected class and struct, by engine name, as 'UFoo in path' strings."""
    found: dict[str, list[str]] = collections.defaultdict(list)
    for header in sorted(SOURCE.rglob("*.h")):
        text = without_comments(header.read_text(encoding="utf-8", errors="replace"))
        where = header.relative_to(REPO_ROOT).as_posix()
        for prefix, name in CLASS.findall(text):
            found[name].append(f"{prefix}{name} in {where}")
        for name in STRUCT.findall(text):
            found[name].append(f"F{name} in {where}")
    return found


def test_no_class_and_struct_share_an_engine_name() -> None:
    found = reflected_types()
    # A FLOOR, so a reading that silently matches nothing cannot pass: game/Source has hundreds of reflected types.
    assert len(found) >= 300, f"only {len(found)} reflected classes and structs were read; the patterns are stale"
    shared = {name: where for name, where in found.items() if len(where) > 1}
    assert not shared, (
        "these engine names are declared more than once, which Unreal Header Tool refuses ('shares engine name'): "
        + "; ".join(f"{name}: {', '.join(where)}" for name, where in sorted(shared.items())))
