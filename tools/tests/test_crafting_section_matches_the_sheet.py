"""The Forge section of docs/Cataclysm_GDD_v2.md, checked against the sheet it
is derived from.

WHY THIS EXISTS. Issue #28. The design document's crafting section named two
materials and no operations; the Crafting sheet of
`docs/All_Things_Cataclysm.xlsx` held eighteen of each. The two drifted because
nothing compared them.

WHAT THE SHEET ACTUALLY IS, because it is not obvious and the issue got it
wrong. It is three tables stacked in one sheet, not one table of 46 materials:

    rows 1-27   the materials, with tier, source and use
    rows 5-11   ALSO carry, in columns 7 to 9, a six-row table of the global CR
                penalty scale. Those columns have nothing to do with the
                material on the same row. The header of that inner table sits
                on the Dismantling Dust row.
    row 28      a header row whose first cell is the literal word "Action"
    rows 29-46  the eighteen Forge operations

So a reader who takes the sheet at face value concludes that materials carry
their own CR formulas. They do not. `(CR / 50) + 1` and `CR / 100` sit on the
Aetherial Shard and Chaos Stabilizer rows by accident of paste position, and
they are the global rule — they reproduce all six rows of the inner table
exactly, which `test_the_cr_scale_follows_the_two_formulas` below checks.

WHAT IS ASSERTED HERE.

    the sheet still has the three-table shape this parser assumes
    every operation in the sheet appears in the document's action table
    every operation's CR figure and base days appear on its row
    every material in the sheet appears in the document's material table
    the document invents no operation and no material the sheet lacks
    the two CR formulas still reproduce the sheet's own scale

WHAT IS NOT ASSERTED. The wording of the Note column. It is prose and it is
allowed to be rewritten for the document; only the numbers are held.
"""

from __future__ import annotations

import math
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The first cell of the row that separates the materials from the operations.
ACTIONS_HEADER = "Action"

#: The global CR penalty scale, read off the inner table in columns 7 to 9.
#: Kept here as the expected shape so a change to the sheet fails loudly rather
#: than silently reshaping what this file parses.
CR_SCALE = (
    (0, 1, 0),
    (50, 2, 0),
    (99, 3, 0),
    (100, 3, 1),
    (200, 5, 2),
    (500, 11, 5),
)


@pytest.fixture(scope="module")
def sheet_rows() -> list[tuple]:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")
    book = openpyxl.load_workbook(WORKBOOK, read_only=True)
    grid = list(book["Crafting"].iter_rows(values_only=True))
    return [
        row for row in grid[1:]
        if any(cell is not None and str(cell).strip() for cell in row)
    ]


@pytest.fixture(scope="module")
def split(sheet_rows) -> tuple[list[tuple], list[tuple]]:
    """The materials and the operations, either side of the "Action" header."""
    for index, row in enumerate(sheet_rows):
        if str(row[0] or "").strip() == ACTIONS_HEADER:
            return sheet_rows[:index], sheet_rows[index + 1:]
    raise AssertionError(
        "The Crafting sheet no longer has a row whose first cell is "
        f"{ACTIONS_HEADER!r}. That row is the only thing separating the "
        "materials from the operations, so this file cannot parse the sheet. "
        "Fix the parser rather than deleting this test."
    )


@pytest.fixture(scope="module")
def forge_section() -> str:
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")
    text = DESIGN_DOCUMENT.read_text(encoding="utf-8")
    start = text.index("# **VII. Crafting")
    end = text.index("# **VIII.", start)
    return text[start:end]


def test_the_sheet_still_has_the_shape_this_file_assumes(split):
    """Everything below reads the sheet through this split. If it is wrong the
    other tests compare the wrong things and pass for the wrong reason."""
    materials, actions = split
    # TWENTY-SEVEN SINCE 2026-08-23, issue #852. It was eighteen: the one row
    # named "Upgrade Stone (x)" was a placeholder and became ten stones, "+1"
    # through "+10", spread two to a rarity tier.
    assert len(materials) == 27, (
        f"Expected 27 materials before the {ACTIONS_HEADER!r} row, found "
        f"{len(materials)}. If a material was added, add it to the design "
        "document's material table too and update this number."
    )
    assert len(actions) == 18, (
        f"Expected 18 operations after the {ACTIONS_HEADER!r} row, found "
        f"{len(actions)}."
    )


def test_every_operation_is_in_the_design_document(split, forge_section):
    materials, actions = split
    missing = [
        str(row[0]).strip() for row in actions
        # The sheet writes some names with a parenthetical qualifier the
        # document drops, so match on the part before the bracket.
        if re.split(r"\s*\(", str(row[0] or "").strip())[0] not in forge_section
    ]
    assert not missing, (
        "The Crafting sheet has operations the design document does not "
        f"mention: {missing}. Section VII must list every operation a player "
        "can perform at the Forge."
    )


def test_every_operation_states_its_residue_and_its_days(split, forge_section):
    """The numbers, not only the names. A table listing the operations with the
    wrong costs is worse than no table."""
    materials, actions = split
    table = {}
    for line in forge_section.splitlines():
        if line.startswith("|"):
            cells = [cell.strip() for cell in line.strip("|").split("|")]
            if cells:
                table[cells[0]] = cells

    problems = []
    for row in actions:
        name = re.split(r"\s*\(", str(row[0] or "").strip())[0]
        residue = str(row[2] or "").strip()
        days = str(row[3] or "").strip()
        stated = table.get(name)
        if stated is None:
            problems.append(f"{name}: no row in the document's action table")
            continue
        joined = " ".join(stated)
        # The sheet writes residue as "5 CR" or "-50% CR"; the document's column
        # is headed "CR added" and carries the bare figure. Compare on the
        # digits, which is what must not drift.
        figures = re.findall(r"\d+", residue)
        for figure in figures:
            if figure not in joined:
                problems.append(
                    f"{name}: sheet says residue {residue!r}, the document's "
                    f"row does not contain {figure!r}: {stated}"
                )
        for figure in re.findall(r"\d+", days):
            if figure not in joined:
                problems.append(
                    f"{name}: sheet says {days!r} base days, the document's "
                    f"row does not contain {figure!r}: {stated}"
                )
    assert not problems, "\n".join(problems)


def test_every_material_is_in_the_design_document(split, forge_section):
    materials, actions = split
    missing = [
        str(row[0]).strip() for row in materials
        if re.split(r"\s*\(", str(row[0] or "").strip())[0] not in forge_section
    ]
    assert not missing, (
        "The Crafting sheet has materials the design document does not list: "
        f"{missing}."
    )


def test_the_document_invents_nothing(split, forge_section):
    """The other direction. A document naming a material that does not exist
    sends a reader looking for it in the sheet, and the workbook is the only
    sanctioned place to add one."""
    materials, actions = split
    known = {
        re.split(r"\s*\(", str(row[0] or "").strip())[0]
        for row in materials + actions
    }
    # EVERY TABLE ROW IN THE SECTION IS READ, not only the two this file owns,
    # because there is no marker saying which table a row belongs to. Header
    # rows are skipped by name instead. Adding a table to section VII therefore
    # means adding its header here, and the failure says which one is missing,
    # so the cost is one line and it announces itself.
    invented = []
    for line in forge_section.splitlines():
        if not line.startswith("| ") or line.startswith("| :"):
            continue
        first = line.strip("|").split("|")[0].strip()
        if not first or first.startswith("**") or first in {
            "Action", "Material", "CR Range", "Item's current CR",
            "Difficulty tier",
        }:
            continue
        # Rows of the two CR tables are numbers, not names: "0 - 99", "100+",
        # "99 (maximum before the time penalty)".
        if re.fullmatch(r"[\d\s\-\+\(\)a-z]+", first):
            continue
        if first not in known:
            invented.append(first)
    assert not invented, (
        "Section VII names these in a table, and the Crafting sheet has no "
        f"such material or operation: {invented}. The workbook is the only "
        "sanctioned place to add one."
    )


def test_the_cr_scale_follows_the_two_formulas(forge_section):
    """The claim that the penalty is one global rule rather than a property of
    each material. If either formula stops reproducing the scale, the document's
    statement that they do is false."""
    for residue, gold, days in CR_SCALE:
        computed_gold = (residue / 50) + 1
        assert math.ceil(computed_gold - 1e-9) == gold, (
            f"At {residue} CR the gold multiplier (CR/50)+1 is "
            f"{computed_gold:.2f}, which does not round to the {gold}x the "
            "scale states."
        )
        assert residue // 100 == days, (
            f"At {residue} CR the time penalty CR/100 rounded down is "
            f"{residue // 100}, not the {days} the scale states."
        )


def test_the_two_formulas_are_stated_in_the_document(forge_section):
    """A scale table with no formula beside it is a copy, and copies drift.
    This is the drift #28 found."""
    assert "(CR / 50) + 1" in forge_section, (
        "The gold cost formula is not stated in section VII."
    )
    assert "CR / 100" in forge_section, (
        "The craft time formula is not stated in section VII."
    )


def test_every_material_sits_at_the_tier_the_sheet_gives_it(split, forge_section):
    """The rarity band each material drops in, not only that it is listed.

    WHY THIS IS SEPARATE FROM THE NAME CHECK ABOVE. That one asks whether the
    document lists every material, and it answered yes for a year while three of
    them sat in the wrong band: the document put Purified Essence and Primal
    Spark below their real rarity and Jeweler's Setting Agent above it. Issue
    #864.

    A BAND IS NOT DECORATION. A material drop picks evenly among the materials
    sharing a band, so the band is how often a material turns up. The document
    called Jeweler's Setting Agent Rare where the sheet makes it Common, which is
    a factor of sixteen in drop weight.

    The sheet is authoritative, as everywhere else in this project. When this
    fails, the fix is to correct the design document.
    """
    materials, _ = split

    wrong = []
    for row in materials:
        name = str(row[0] or "").strip()
        on_sheet = re.search(r"Tier\s+(\d+)\s*\(([^)]+)\)", str(row[1] or ""))
        assert on_sheet, (
            f"the Crafting sheet's Tier & Source cell for {name!r} does not begin "
            f"'Tier N (Name)', so this test cannot read a band from it: {row[1]!r}")

        in_document = re.search(
            r"^\|\s*" + re.escape(name) + r"\s*\|\s*([^|]+?)\s*\|",
            forge_section, re.MULTILINE)
        assert in_document, (
            f"{name!r} is on the Crafting sheet but has no row in the material "
            "table in section VII of the design document.")

        want = f"{int(on_sheet.group(1))} ({on_sheet.group(2).strip()})"
        got = in_document.group(1)
        if got != want:
            wrong.append(f"{name}: document says {got!r}, sheet says {want!r}")

    assert not wrong, (
        "the material table in section VII of docs/Cataclysm_GDD_v2.md puts these "
        "materials in a different rarity band from the Crafting sheet of "
        f"docs/All_Things_Cataclysm.xlsx: {'; '.join(wrong)}"
    )
