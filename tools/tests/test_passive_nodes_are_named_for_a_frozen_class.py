"""Every passive node is named for its tree, and its tree is in the class list
the version 3 save migration froze.

WHY THIS EXISTS. Issue #2064. `Migrate_2_to_3` in
`game/Source/Cataclysm/Save/CataclysmSaveRecords.cpp` keeps one class tree per
damage type in a character written at version 2. A migration step may not read
the game's data tables, so it cannot look a node up to learn its tree. It reads
the tree from the node's name instead -- `Masochist_basic_spine_000` is in the
Masochist tree -- and groups the trees by damage type with a copy of the class
list frozen on 2026-09-24.

WHAT BREAKING IT WOULD COST. A node row renamed so that its name no longer
starts with its tree, or a tree added whose name the frozen list does not hold,
would be grouped under the wrong tree or under none. The step would then keep
or remove the wrong points, and it succeeds while doing it, so nothing reports
an error. This reads the node table and the frozen list and says so first.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, and the
node table is data, so a text check is the one that runs on every pull request.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
RECORDS = REPO / "game/Source/Cataclysm/Save/CataclysmSaveRecords.cpp"
NODES = REPO / "game/Data/PassiveNodes.csv"

#: The frozen list, from its declaration to the closing brace of its initialiser.
FROZEN = re.compile(r"ClassesOn20260924\[8\]\[3\]\s*=\s*\{(?P<body>.*?)\n\t\};",
                    re.DOTALL)
NAME = re.compile(r'TEXT\("(?P<name>[^"]+)"\)')


def frozen_classes() -> list[str]:
    """The 24 class names the migration froze, in the order it holds them."""
    match = FROZEN.search(RECORDS.read_text(encoding="utf-8"))
    assert match, f"no ClassesOn20260924 list found in {RECORDS.relative_to(REPO)}"
    return NAME.findall(match.group("body"))


def prefix_of(tree: str) -> str:
    """What a node of this tree is named with in front, as the step matches it."""
    return tree.replace(" ", "") + "_"


def test_the_frozen_list_holds_eight_damage_types_of_three_classes() -> None:
    names = frozen_classes()
    assert len(names) == 24, f"{len(names)} names read from the frozen list: {names}"
    assert len(set(names)) == 24, f"a class is named twice: {names}"


def test_every_passive_node_is_named_for_its_tree_and_only_that_tree() -> None:
    prefixes = {prefix_of(name): name for name in frozen_classes()}
    rows = list(csv.DictReader(NODES.open(encoding="utf-8-sig")))
    assert rows, f"{NODES.relative_to(REPO)} has no rows"

    wrong = []
    for row in rows:
        matching = [tree for prefix, tree in prefixes.items()
                    if row["Name"].startswith(prefix)]
        if matching != [row["Tree"]]:
            wrong.append(f"{row['Name']} (Tree column {row['Tree']!r}, "
                         f"named for {matching or 'no frozen class'})")

    assert not wrong, (
        f"{len(wrong)} of {len(rows)} passive nodes would be grouped under the "
        "wrong tree by Migrate_2_to_3, which reads a node's tree from its name: "
        + "; ".join(wrong[:10]))
