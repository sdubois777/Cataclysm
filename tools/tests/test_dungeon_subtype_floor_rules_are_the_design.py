"""Three dungeon sub-types change the floors they generate, and the design says how.

WHY THIS IS A PYTHON TEST. Continuous integration never builds the C++ -- issue
[#20](https://github.com/sdubois777/Cataclysm/issues/20) is the self-hosted
runner that would -- so `Cataclysm.FloorBrief.*` in
`game/Source/Cataclysm/Tests/CataclysmFloorBriefTests.cpp` only ever runs on a
developer's machine. This runs on every pull request.

WHAT IT GUARDS, AND IT IS NOT THE ARITHMETIC. The automation tests already check
that a Volatile dungeon's modifiers change, that an Elite dungeon has a boss on
every floor and that a Horde dungeon's floors are one open space. Every one of
those tests builds its own dungeon by hand, so all of them keep passing if the
DESIGN changes underneath them. This holds the other end: that the sentence in
`docs/Cataclysm_GDD_v2.md` each rule was built from still reads the way the rule
assumes, and that the one dungeon modifier built through the same seam is still
a real row of `game/Data/DungeonModifiers.csv` with the meaning the rule gives
it.

WHAT IT CANNOT CHECK. Whether the rules behave. A design sentence and a C++ file
quoting it can agree perfectly while the code does the wrong thing; that is what
`Cataclysm.FloorBrief.*` is for. These two halves are separate on purpose and
neither replaces the other.
"""

from __future__ import annotations

import csv
import io
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
RULES_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                / "CataclysmFloorBrief.h")
RULES_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                / "CataclysmFloorBrief.cpp")
MODIFIERS_CSV = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"

#: The three sub-type rules this seam was built to carry, and the sentence in
#: the design's Dungeon Sub-Types table each one comes from.
#:
#: THE SENTENCES ARE QUOTED FROM THE TABLE AND NOT PARAPHRASED. A paraphrase
#: would let the design move without the test noticing, which is the whole
#: failure being guarded against.
#:
#: THE OTHER FOUR SUB-TYPES ARE DELIBERATELY ABSENT. Timed is a clock on the run
#: and a reward multiplier, Sacrificial is a player choice with an economy behind
#: it, and Siege and Cow Level act on cities and the day clock. None of them
#: changes what a floor contains, so none of them belongs in this seam. Adding
#: one here without building it would make this test lie.
SUBTYPE_RULES = {
    "Horde": "Number of floors equals number of enemy waves.",
    "Elite": "Every floor ends with a boss fight.",
    "Volatile": "Dungeon modifiers change every floor.",
}

#: The one dungeon modifier built through the same seam, as it appears in the
#: `Name` column of `game/Data/DungeonModifiers.csv`.
UNSTABLE_DIMENSIONS = "Chaos_Unstable_Dimensions"

#: The part of that row's description the rule depends on. It is what makes the
#: modifier a per-floor rule rather than a whole-dungeon one.
UNSTABLE_DIMENSIONS_MEANING = "on the next floor"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def flattened(text: str) -> str:
    """The text with every run of whitespace turned into one space.

    THE FILES ARE HARD-WRAPPED AND CRLF. A sentence quoted in a C++ comment is
    split across two lines with a `*` and some indentation between the halves,
    so searching the raw text for it finds nothing and reports the quotation
    missing when it is there. Flattening is what makes a phrase search mean what
    it looks like it means.
    """
    return re.sub(r"\s+", " ", text)


def design_subtype_rows() -> dict[str, str]:
    """Every row of the design's Dungeon Sub-Types table, by sub-type name."""
    text = read(DESIGN)

    heading = "## **Dungeon Sub-Types**"
    start = text.find(heading)
    if start < 0:
        pytest.fail(
            f"docs/Cataclysm_GDD_v2.md has no '{heading}' section. The three "
            "dungeon sub-type rules in "
            "game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.h were built "
            "from that table."
        )

    # THE NEXT `## ` HEADING ENDS IT. Reading to the end of the file would pick
    # up the Dungeon Modifiers section below, which has its own table.
    rest = text[start + len(heading):]
    end = rest.find("\n## ")
    table = rest if end < 0 else rest[:end]

    rows: dict[str, str] = {}
    for line in table.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) != 2:
            continue
        rows[cells[0]] = cells[1]
    return rows


def modifier_rows() -> dict[str, dict[str, str]]:
    """Every row of the dungeon modifier table, by its `Name` column."""
    if not MODIFIERS_CSV.is_file():
        pytest.fail("game/Data/DungeonModifiers.csv does not exist")

    # BYTES AND A BOM-TOLERANT DECODE. Every file in this repository is CRLF and
    # the exported CSV carries a byte order mark; `csv` handles the line endings
    # and `utf-8-sig` handles the mark.
    raw = MODIFIERS_CSV.read_bytes().decode("utf-8-sig")
    return {row["Name"]: row for row in csv.DictReader(io.StringIO(raw))}


class TestTheDesignStillSaysWhatTheRulesWereBuiltFrom:
    def test_the_sub_type_table_holds_all_three_rules(self):
        rows = design_subtype_rows()

        for subtype, sentence in SUBTYPE_RULES.items():
            assert subtype in rows, (
                f"docs/Cataclysm_GDD_v2.md's Dungeon Sub-Types table no longer "
                f"has a {subtype} row. "
                f"game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.cpp changes "
                f"what a floor holds for that sub-type and quotes that row."
            )
            assert rows[subtype] == sentence, (
                f"the design's {subtype} row now reads {rows[subtype]!r} and "
                f"the floor rules were built from {sentence!r}. Change "
                f"game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.cpp and its "
                f"tests to match the design, not this test to match the code."
            )

    def test_the_rules_quote_the_design_word_for_word(self):
        # A NAME SEARCH WOULD PROVE NOTHING. "Volatile" appears in this header
        # several times over; what has to be there is the sentence the rule
        # implements, so that a reader of the code can see the design without
        # opening the design.
        text = flattened(read(RULES_HEADER) + " " + read(RULES_SOURCE))

        for subtype, sentence in SUBTYPE_RULES.items():
            assert sentence in text, (
                f"the {subtype} rule in "
                f"game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.h or .cpp "
                f"no longer quotes the design sentence it implements, "
                f"{sentence!r}."
            )

    def test_the_search_would_notice_a_sentence_that_is_not_there(self):
        # **THE POSITIVE CONTROL FOR THE TWO CHECKS ABOVE.** A flattening or
        # decoding fault would make every phrase search pass or every one fail,
        # and an empty search and a working one look identical from the outside.
        # This asks the same machinery for a sentence that is deliberately not
        # in either file.
        text = flattened(read(RULES_HEADER) + " " + read(RULES_SOURCE))
        assert "Failing the time limit is treated as dying." not in text, (
            "the Timed sub-type's design sentence has appeared in the floor "
            "rules. Timed is a clock on the run and a reward multiplier; it "
            "does not change what a floor contains and is deliberately not "
            "built through this seam."
        )


class TestTheModifierBuiltThroughTheSameSeam:
    def test_it_is_a_real_row_of_the_modifier_table(self):
        rows = modifier_rows()

        assert UNSTABLE_DIMENSIONS in rows, (
            f"game/Data/DungeonModifiers.csv has no {UNSTABLE_DIMENSIONS!r} "
            f"row. game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.h names "
            f"that row key in `UnstableDimensionsKey` and gives every floor of "
            f"a dungeon carrying it one extra modifier. A renamed row makes the "
            f"rule stop firing silently."
        )

    def test_the_row_key_in_the_cpp_is_the_one_in_the_table(self):
        source = read(RULES_HEADER) + read(RULES_SOURCE)

        found = re.search(
            r'UnstableDimensionsKey\s*=\s*\n?\s*TEXT\("([^"]+)"\)', source)
        assert found, (
            "game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.cpp no longer "
            "defines `UnstableDimensionsKey` as a TEXT literal, so this test "
            "cannot read which row the rule looks for."
        )
        assert found.group(1) == UNSTABLE_DIMENSIONS, (
            f"the C++ looks for the modifier row {found.group(1)!r} and the "
            f"table's row is {UNSTABLE_DIMENSIONS!r}."
        )

    def test_the_row_still_means_a_modifier_on_the_next_floor(self):
        row = modifier_rows()[UNSTABLE_DIMENSIONS]
        description = flattened(row["Description"])

        assert UNSTABLE_DIMENSIONS_MEANING in description, (
            f"the {UNSTABLE_DIMENSIONS} row now reads {description!r}. The rule "
            f"in game/Source/Cataclysm/Dungeon/CataclysmFloorBrief.cpp gives "
            f"every floor of a dungeon carrying it one extra modifier, which "
            f"only follows while the row says the new modifier applies "
            f"{UNSTABLE_DIMENSIONS_MEANING!r}."
        )

    def test_it_belongs_to_one_cataclysm_rather_than_every_dungeon(self):
        row = modifier_rows()[UNSTABLE_DIMENSIONS]

        # A `Generic` MODIFIER WOULD BE A RULE ON EVERY DUNGEON. The project
        # owner ruled on 2026-09-05 that the one Generic row is granted
        # separately and takes no slot; a per-floor rule attached to such a row
        # would reach every run rather than the runs facing one Cataclysm.
        assert row["CataclysmType"] == "Chaos", (
            f"the {UNSTABLE_DIMENSIONS} row now belongs to "
            f"{row['CataclysmType']!r}. The rule was built on it being one "
            f"Cataclysm's modifier, so that a run not facing that Cataclysm "
            f"never draws it."
        )
