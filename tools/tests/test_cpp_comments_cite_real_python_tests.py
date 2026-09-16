"""Every Python test a C++ comment cites still exists.

WHY THIS EXISTS. Issue #1705. Comments under `game/Source` name Python tests as
the thing that holds a rule: "`tools/tests/test_x.py` holds the result against
the designed walking speed so that changing it has to confront what it does".
The comment's purpose is to stop a reader changing a constant without seeing
what it costs. A reader who goes looking for the named test, finds nothing, and
concludes the guard was deleted may then change the constant believing nothing
holds it -- the opposite of a loud failure. Nothing checked those names, and
two were wrong when this landed: one cited a file under a name it never had
(the test is `test_a_dungeon_floor_is_a_walk_not_a_room.py`), and five comments
cited `test_every_dressed_enemy_hides_its_placeholder`, a prefix of the real
`test_every_dressed_enemy_hides_its_placeholder_cylinder`.

THE SAME CLASS OF FAULT HAS BEEN FIXED THREE TIMES AS ONE-OFFS (issues #1134,
#1366 and the branch that produced #1705). A rename moves the thing and leaves
every prose reference to it behind, and prose is not compiled. This is the
standing check: `tools/tests/test_the_decisions_log_names_real_files.py` does
the same for file paths in the decisions log.

WHAT COUNTS AS A CITATION. Any `test_` followed by at least ten name characters,
which is every pytest function or file name in this repository and no C++
identifier (the automation tests are named in CamelCase). A citation resolves
when it is a test function defined under `tools/tests` or `sim/tests`, or a
test file there. Both forms are cited, so both count.

WHAT IT MUST NOT DO. Report zero and pass. An earlier version of the check in
the issue used `git ls-files --error-unmatch` on two paths at once, which fails
whenever EITHER is missing, and reported all 85 citations dangling; another
returned empty and printed "none" while a real fault was present. So this file
asserts the parser found a sensible number of citations, and that a citation
known to be good resolves, before it says anything about the rest.

A NAME IS MATCHED WHOLE. `test_foo` cited where the function is `test_foo_bar`
does not resolve: it is the second fault above, and a prefix match would have
hidden it.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"
TEST_ROOTS = (REPO_ROOT / "tools" / "tests", REPO_ROOT / "sim" / "tests")

CITATION = re.compile(r"test_[a-z0-9_]{10,}")
DEFINITION = re.compile(r"^\s*(?:async\s+)?def\s+(test_[a-z0-9_]+)\s*\(", re.M)

#: A citation that exists today and is expected to go on existing, so that a
#: parser reading nothing cannot pass. Named in `CataclysmFloorGenerator.h`.
KNOWN_GOOD = "test_a_dungeon_floor_is_a_walk_not_a_room"

#: Fewer citations than this means the parser is not reading what it read when
#: this landed: 87 distinct names across `game/Source` on 2026-09-14.
LEAST_CITATIONS_EXPECTED = 50


def cited_test_names() -> dict[str, list[pathlib.Path]]:
    """Every cited name, with the C++ files that cite it."""
    found: dict[str, list[pathlib.Path]] = {}
    for path in sorted(SOURCE_ROOT.rglob("*.cpp")) + sorted(SOURCE_ROOT.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for name in set(CITATION.findall(text)):
            found.setdefault(name, []).append(path)
    return found


def existing_test_names() -> set[str]:
    """Every test function and test file name under the two test roots."""
    names: set[str] = set()
    for root in TEST_ROOTS:
        for path in sorted(root.rglob("test_*.py")):
            names.add(path.stem)
            names.update(DEFINITION.findall(path.read_text(encoding="utf-8")))
    return names


def test_the_parser_reads_a_sensible_number_of_citations() -> None:
    """The positive control: a check that read nothing would pass everything."""
    cited = cited_test_names()
    assert len(cited) >= LEAST_CITATIONS_EXPECTED, (
        f"only {len(cited)} test names were found cited under game/Source; "
        f"there were 87 on 2026-09-14, so the parser is reading less than it did")
    assert KNOWN_GOOD in cited, (
        f"{KNOWN_GOOD} is cited in CataclysmFloorGenerator.h and was not found; "
        f"the parser is not reading citations")
    assert KNOWN_GOOD in existing_test_names(), (
        f"{KNOWN_GOOD} does not resolve although tools/tests/{KNOWN_GOOD}.py exists; "
        f"the resolver is broken, so nothing below means anything")


def test_every_python_test_a_cpp_comment_cites_exists() -> None:
    existing = existing_test_names()
    dangling = {name: paths for name, paths in cited_test_names().items()
                if name not in existing}
    assert not dangling, "\n".join(
        f"{name} is cited in {', '.join(str(p.relative_to(REPO_ROOT)) for p in paths)} "
        f"and is neither a test function nor a test file under tools/tests or sim/tests"
        for name, paths in sorted(dangling.items()))
