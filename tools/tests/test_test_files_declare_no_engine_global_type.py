"""No Unreal test file declares a type the engine already declares at global scope.

A test file's helpers live in a namespace of their own and each test opens it with `using namespace`. A type there with
the name of a type the engine declares at global scope is then ambiguous wherever the test names it (C2872), and the
build stops: every error after the first follows from the type having no usable meaning. The build is the first place
it shows, because the Python suite never compiles C++.

It has happened twice, both with `FScene`, the renderer's scene:
  - window D, #2807's hover tests, renamed to `FHoverScene`;
  - window power-score-w1's first build, 2026-11-22: 50 errors in `CataclysmForgeAffixChangeTests.cpp` (#47, slice F2),
    renamed to `FAffixChangeScene` (commit 5d470823e). The coordinating session ruled that twice makes it a check.

WHAT IT HOLDS: no `.cpp` or `.h` under any module's `Tests` folder in `game/Source` declares a `struct` or `class` named
in `ENGINE_GLOBAL_TYPES`. Add a name there when another such collision is found. It does not know the engine's whole
global scope; it knows the names that have already broken a build.
"""
from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"

#: Types the engine declares at global scope that a test file has declared and broken the build with.
ENGINE_GLOBAL_TYPES = {
    "FScene": "the renderer's scene (Runtime/Renderer); window D (#2807) and window power-score-w1 (#47 F2)",
}


def unreal_test_files() -> list[pathlib.Path]:
    return sorted(path for path in SOURCE.glob("*/Tests/*") if path.suffix in (".cpp", ".h"))


def test_the_check_reads_the_test_files() -> None:
    files = unreal_test_files()
    # A GLOB THAT MATCHED NOTHING WOULD PASS THE CHECK BELOW, so it is held to the folders it is meant to read.
    assert len(files) > 100, f"only {len(files)} test files found under {SOURCE}/*/Tests; the check reads the wrong place"
    assert any(path.name == "CataclysmForgeAffixChangeTests.cpp" for path in files), "the file that broke the build is not read"


def test_no_test_file_declares_a_type_the_engine_declares_globally() -> None:
    pattern = re.compile(rf"\b(?:struct|class)\s+({'|'.join(ENGINE_GLOBAL_TYPES)})\b(?!\s*;)")
    found = []
    for path in unreal_test_files():
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            match = pattern.search(line)
            if match:
                found.append(f"{path.relative_to(REPO_ROOT).as_posix()}:{number}: {match.group(1)}, "
                             f"{ENGINE_GLOBAL_TYPES[match.group(1)]}")
    assert not found, ("a test file declares a type the engine declares at global scope; under `using namespace` it is "
                       "ambiguous and the build stops. Give it a name of the file's own:\n  " + "\n  ".join(found))
