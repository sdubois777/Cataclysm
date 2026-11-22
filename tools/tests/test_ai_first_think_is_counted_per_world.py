"""A creature's first AI think is staggered by a count kept per world, not one kept for the whole process.

WHY THIS EXISTS. Issue #2827. `ACataclysmEnemyController` staggers each creature's first thinking pass by a count of
the possessions made before it (`FirstThinkDelaySeconds`, issue #1543). The count was one for the whole editor process,
so a creature's first think depended on how many enemies earlier tests had possessed, and a test that moved the clock
with an enemy alive could give one result alone and another in the whole suite: #2811's two tests did, in window D.
The count is now
kept per world. In play nothing changes inside a world, and creatures of two worlds never share a timer manager.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so
`Cataclysm.AI.ACreaturesFirstThinkDependsOnlyOnItsOwnWorld` never runs on a pull request.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTROLLER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmEnemyController.cpp"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def test_the_first_think_is_staggered_by_the_worlds_own_count() -> None:
    text = code(CONTROLLER)
    assert re.search(r"TMap<\s*TObjectKey<\s*UWorld\s*>\s*,\s*uint32\s*>\s+GCataclysmEnemyControllerPossessionsByWorld",
                     text), "the possession count is not kept per world"
    assert not re.search(r"\buint32\s+GCataclysmEnemyControllerPossessions\s*=", text), (
        "a process-wide possession count came back, so a first think depends on what earlier tests possessed")
    assert re.search(r"FirstThinkDelaySeconds\(\s*TakeThePossessionCountIn\(\s*World\s*\)\s*\)", text), (
        "OnPossess does not stagger the first think by its world's own count")
    take = text[text.index("uint32 TakeThePossessionCountIn("):]
    take = take[:take.index("\n\t}\n") if "\n\t}\n" in take else len(take)]
    assert "ResolveObjectPtr() == nullptr" in take and "RemoveCurrent()" in take, (
        "the counts of worlds that are gone are never removed, so the map grows with every test world")
