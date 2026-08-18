"""The enemy drop numbers, checked everywhere they are written down.

WHY THIS EXISTS. How much a kill drops, and how much it raises the rarity of what
it drops, lives in THREE places: the Enemy Drops sheet of
`docs/All_Things_Cataclysm.xlsx`, and `ENEMY_GEAR_DROPS` and `ENEMY_MAGIC_FIND`
in `sim/cataclysm_sim/loot.py`, and the What a Kill Drops table in
`docs/Cataclysm_GDD_v2.md`. The simulation is pure standard library Python and
does not read the workbook, so it mirrors those numbers as constants, and the
design document restates them for a reader.

Three copies of twelve numbers drift. This project has been bitten by exactly that
twice with `sim/cataclysm_sim/scoring.py`, which is why
`sim/verify_scoring_port.py` exists, and the same comparison is made for the drop
weights, the socket maxima and the affix tier weights by their own test files.

WHICH IS AUTHORITATIVE. The workbook, as everywhere else. When this fails, the
usual fix is to change the Python to match the sheet.

AND THE RARITY LADDER IS CHECKED AGAINST A THIRD PLACE, which is the part worth
having. The six enemy rarities are not this project's to choose: they come from
`RARITY_WEIGHTS` in `sim/cataclysm_sim/scoring.py`, a port of the external
DungeonSimulator power model that `CLAUDE.md` names as authoritative. A seventh
rarity typed into the sheet would be a rarity nothing else in the game knows
about, and the drop table would be the only place it existed. That was worth a
test because it has already been got wrong once in conversation: six was
remembered as eight.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
SHEET = "Enemy Drops"


def text(value) -> str:
    return "" if value is None else str(value).strip()


@pytest.fixture(scope="module")
def sheet() -> list[dict]:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")

    book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
    if SHEET not in book.sheetnames:
        pytest.fail(f"the workbook has no sheet named {SHEET!r}")

    rows = list(book[SHEET].iter_rows(values_only=True))
    headers = [text(h) for h in rows[0]]
    out = []
    for raw in rows[1:]:
        if not raw or not text(raw[0]):
            continue
        out.append({headers[i]: raw[i]
                    for i in range(len(headers)) if i < len(raw)})
    assert out, f"the {SHEET} sheet has a header row and no rows under it"
    return out


@pytest.fixture(scope="module")
def loot():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import loot as module
    return module


@pytest.fixture(scope="module")
def scoring():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import scoring as module
    return module


def test_the_sheet_names_the_six_rarities_the_power_model_has(sheet,
                                                              scoring) -> None:
    """In order, and no others. The ladder belongs to the external power model,
    not to the drop rules."""
    named = [text(row["Enemy Rarity"]) for row in sheet]
    assert named == list(scoring.RARITY_WEIGHTS), (
        f"the {SHEET} sheet lists {named}, and the enemy rarity ladder is "
        f"{list(scoring.RARITY_WEIGHTS)}. That ladder comes from "
        "scoring.RARITY_WEIGHTS, which is a port of the DungeonSimulator power "
        "model and is authoritative. A rarity added here would exist nowhere "
        "else in the game.")


def test_the_step_column_matches_the_ladders_own_position(sheet) -> None:
    """The Step column restates where a rarity sits, the same way
    FCataclysmEnemyRarityRow carries one. A Step that disagreed with the row
    order would make the sheet unreadable to anyone checking the curve."""
    steps = [int(row["Step"]) for row in sheet]
    assert steps == list(range(len(sheet)))


def test_every_gear_drop_rate_matches_the_model(sheet, loot) -> None:
    wrong = []
    for row in sheet:
        rarity = text(row["Enemy Rarity"])
        stated = float(row["Gear Drops"])
        mirrored = loot.ENEMY_GEAR_DROPS.get(rarity)
        if mirrored is None or stated != pytest.approx(mirrored):
            wrong.append(f"{rarity}: sheet {stated:g}, model {mirrored}")
    assert not wrong, (
        f"the {SHEET} sheet and loot.ENEMY_GEAR_DROPS disagree about how many "
        "items a kill drops: " + "; ".join(wrong)
        + ". The workbook is authoritative.")


def test_every_magic_find_contribution_matches_the_model(sheet, loot) -> None:
    wrong = []
    for row in sheet:
        rarity = text(row["Enemy Rarity"])
        stated = float(row["Magic Find"])
        mirrored = loot.ENEMY_MAGIC_FIND.get(rarity)
        if mirrored is None or stated != pytest.approx(mirrored):
            wrong.append(f"{rarity}: sheet {stated:g}, model {mirrored}")
    assert not wrong, (
        f"the {SHEET} sheet and loot.ENEMY_MAGIC_FIND disagree about how much "
        "an enemy raises the rarity of its own drops: " + "; ".join(wrong)
        + ". The workbook is authoritative.")


def test_the_model_names_nothing_the_sheet_does_not(sheet, loot) -> None:
    """The other direction. A rarity in the model and not the sheet would be a
    number nobody can edit, which is the failure mirroring exists to avoid."""
    named = {text(row["Enemy Rarity"]) for row in sheet}
    assert set(loot.ENEMY_GEAR_DROPS) == named
    assert set(loot.ENEMY_MAGIC_FIND) == named


def test_the_magic_find_column_follows_the_enemy_power_ladder(sheet,
                                                              scoring) -> None:
    """A CHECK AGAINST SOMETHING OTHER THAN THE OTHER COPY. Both the sheet and
    the model could be edited together and stay wrong; this compares the shape
    of the curve against `scoring.RARITY_WEIGHTS`, which is where the decision
    said it came from.

    The weights rise 0, 0.05, 0.1, 0.15, 0.3, 0.5 and jump at Boss rather than
    rising evenly. The magic find column is that shape times 1000. If the two
    ever stop being proportional, the reasoning recorded in `docs/DECISIONS.md`
    has stopped holding and this test is where that shows.
    """
    scale = 1000.0
    wrong = []
    for row in sheet:
        rarity = text(row["Enemy Rarity"])
        stated = float(row["Magic Find"])
        expected = scoring.RARITY_WEIGHTS[rarity] * scale
        if stated != pytest.approx(expected):
            wrong.append(f"{rarity}: sheet {stated:g}, "
                         f"power ladder times {scale:g} gives {expected:g}")
    assert not wrong, (
        "the Magic Find column no longer follows the enemy power ladder: "
        + "; ".join(wrong)
        + ". That proportion is the reason given in docs/DECISIONS.md for these "
          "six numbers rather than six others. Either restore it or record why "
          "it changed.")


def test_a_common_kill_is_the_baseline_both_columns_are_read_against(
        sheet) -> None:
    first = sheet[0]
    assert text(first["Enemy Rarity"]) == "Common"
    assert float(first["Magic Find"]) == 0.0, (
        "a Common kill adds magic find, so the whole ladder is measured against "
        "something other than nothing.")
    assert float(first["Gear Drops"]) > 0.0, (
        "a Common kill drops nothing at all, and loot quantity multiplies the "
        "rate, so no amount of it would help.")


# --------------------------------------------------------------------------
# The third copy: the table a reader of the design document sees
# --------------------------------------------------------------------------

DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: A row of the What a Kill Drops table: `| Herald | 2.0 | 4.0 | 150% |`
DOCUMENT_ROW = re.compile(
    r"^\|\s*([A-Za-z ]+?)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)\s*\|"
    r"\s*([\d.]+)%\s*\|\s*$")

TABLE_HEADER = ("| Enemy rarity | Gear drops per kill | "
                "Material drops per kill | Magic find it adds |")


@pytest.fixture(scope="module")
def stated() -> dict[str, tuple[float, float, float]]:
    """Rarity against (gear drops, material drops, magic find)."""
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")
    document = DESIGN_DOCUMENT.read_text(encoding="utf-8")

    assert TABLE_HEADER in document, (
        f"{DESIGN_DOCUMENT.name} no longer has a table headed "
        f"{TABLE_HEADER!r}. Either the What a Kill Drops table was removed or "
        "its header was reworded, and the comparisons below would pass having "
        "compared nothing.")

    rows: dict[str, tuple[float, float]] = {}
    for line in document[document.index(TABLE_HEADER):].splitlines()[1:]:
        match = DOCUMENT_ROW.match(line)
        if not match:
            if line.startswith("|"):
                continue          # the alignment row
            break                 # the table has ended
        rows[match.group(1)] = (float(match.group(2)), float(match.group(3)),
                                float(match.group(4)))
    return rows


def test_the_document_states_a_row_for_every_enemy_rarity(stated, loot) -> None:
    """Guarded first: a pattern that matched nothing would make the two
    comparisons below vacuous."""
    assert set(stated) == set(loot.ENEMY_GEAR_DROPS), (
        f"{DESIGN_DOCUMENT.name} states rows for {sorted(stated)} and the "
        f"model has {sorted(loot.ENEMY_GEAR_DROPS)}")


def test_the_document_states_the_drop_rates_the_model_uses(stated,
                                                           loot) -> None:
    wrong = [f"{rarity}: document {drops:g}, model {loot.ENEMY_GEAR_DROPS[rarity]:g}"
             for rarity, (drops, _mats, _find) in sorted(stated.items())
             if drops != pytest.approx(loot.ENEMY_GEAR_DROPS[rarity])]
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states drop rates the simulation does not "
        "use: " + "; ".join(wrong)
        + ". A reader would plan against numbers the game does not produce.")


def test_the_document_states_the_magic_find_the_model_uses(stated,
                                                           loot) -> None:
    wrong = [f"{rarity}: document {find:g}%, model {loot.ENEMY_MAGIC_FIND[rarity]:g}%"
             for rarity, (_drops, _mats, find) in sorted(stated.items())
             if find != pytest.approx(loot.ENEMY_MAGIC_FIND[rarity])]
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states magic find contributions the "
        "simulation does not use: " + "; ".join(wrong))


# --------------------------------------------------------------------------
# Crafting materials, which drop on their own roll
# --------------------------------------------------------------------------

MATERIAL_SHEET = "Material Tiers"
CRAFTING_SHEET = "Crafting"

#: The first cell of the row separating the materials from the operations in
#: the Crafting sheet. See test_crafting_section_matches_the_sheet.py, which
#: documents that sheet's three-table shape in full.
ACTIONS_HEADER = "Action"


@pytest.fixture(scope="module")
def material_tiers() -> list[dict]:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")

    book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
    if MATERIAL_SHEET not in book.sheetnames:
        pytest.fail(f"the workbook has no sheet named {MATERIAL_SHEET!r}")

    rows = list(book[MATERIAL_SHEET].iter_rows(values_only=True))
    headers = [text(h) for h in rows[0]]
    out = [{headers[i]: raw[i] for i in range(len(headers)) if i < len(raw)}
           for raw in rows[1:] if raw and text(raw[0])]
    assert out, f"the {MATERIAL_SHEET} sheet has a header row and nothing under it"
    return out


@pytest.fixture(scope="module")
def materials_by_tier() -> dict[str, int]:
    """How many materials each tier holds, counted off the Crafting sheet.

    A THIRD SOURCE, not the Material Tiers sheet's own Materials column. That
    column and this count could both be edited to the same wrong number; the
    Crafting sheet is where the materials actually are.
    """
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")

    book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
    grid = [row for row in book[CRAFTING_SHEET].iter_rows(values_only=True)
            if any(cell is not None and str(cell).strip() for cell in row)]

    counted: dict[str, int] = {}
    for row in grid:
        if text(row[0]) == ACTIONS_HEADER:
            break
        found = re.match(r"Tier\s*\d\s*\(([^)]+)\)", text(row[1]))
        if found:
            counted[found.group(1)] = counted.get(found.group(1), 0) + 1
    assert counted, (
        f"no 'Tier N (Name)' cells could be read out of the {CRAFTING_SHEET} "
        "sheet, so the material counts below would compare nothing.")
    return counted


def test_the_material_tier_sheet_names_the_tiers_the_model_has(material_tiers,
                                                               loot) -> None:
    named = [text(row["Tier Name"]) for row in material_tiers]
    assert named == list(loot.MATERIAL_TIERS)


def test_the_tier_numbers_run_from_one_upward(material_tiers) -> None:
    assert [int(row["Tier"]) for row in material_tiers] == \
        list(range(1, len(material_tiers) + 1))


def test_every_material_tier_weight_matches_the_model(material_tiers,
                                                      loot) -> None:
    wrong = [f"{text(row['Tier Name'])}: sheet {float(row['Drop Weight']):g}, "
             f"model {loot.MATERIAL_TIER_DROP_WEIGHT.get(text(row['Tier Name']))}"
             for row in material_tiers
             if float(row["Drop Weight"]) != pytest.approx(
                 loot.MATERIAL_TIER_DROP_WEIGHT.get(text(row["Tier Name"]), -1))]
    assert not wrong, (
        f"the {MATERIAL_SHEET} sheet and loot.MATERIAL_TIER_DROP_WEIGHT "
        "disagree: " + "; ".join(wrong) + ". The workbook is authoritative.")


def test_the_material_counts_match_the_crafting_sheet(material_tiers,
                                                      materials_by_tier,
                                                      loot) -> None:
    """Both the sheet's own Materials column and the model's mirror of it,
    against the Crafting sheet where the materials really are."""
    wrong = []
    for row in material_tiers:
        tier = text(row["Tier Name"])
        real = materials_by_tier.get(tier)
        if int(row["Materials"]) != real:
            wrong.append(f"{tier}: {MATERIAL_SHEET} says "
                         f"{int(row['Materials'])}, Crafting has {real}")
        if loot.MATERIALS_IN_TIER.get(tier) != real:
            wrong.append(f"{tier}: the model says "
                         f"{loot.MATERIALS_IN_TIER.get(tier)}, Crafting has "
                         f"{real}")
    assert not wrong, (
        "the number of crafting materials in a tier disagrees between the "
        "Crafting sheet and something that restates it: " + "; ".join(wrong)
        + ". How often a NAMED top-tier material drops is derived from this "
          "count, and Purified Essence is the only thing that clears the "
          "Consumption Threshold.")


def test_every_material_drop_rate_matches_the_model(sheet, loot) -> None:
    wrong = [f"{text(row['Enemy Rarity'])}: sheet {float(row['Material Drops']):g}, "
             f"model {loot.ENEMY_MATERIAL_DROPS.get(text(row['Enemy Rarity']))}"
             for row in sheet
             if float(row["Material Drops"]) != pytest.approx(
                 loot.ENEMY_MATERIAL_DROPS.get(text(row["Enemy Rarity"]), -1))]
    assert not wrong, (
        f"the {SHEET} sheet and loot.ENEMY_MATERIAL_DROPS disagree about how "
        "many crafting materials a kill drops: " + "; ".join(wrong))


def test_the_document_states_the_material_rates_the_model_uses(stated,
                                                               loot) -> None:
    wrong = [f"{rarity}: document {mats:g}, "
             f"model {loot.ENEMY_MATERIAL_DROPS[rarity]:g}"
             for rarity, (_drops, mats, _find) in sorted(stated.items())
             if mats != pytest.approx(loot.ENEMY_MATERIAL_DROPS[rarity])]
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states crafting material drop rates the "
        "simulation does not use: " + "; ".join(wrong))
