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

#: A row of the What a Kill Drops table: `| Herald | 2.0 | 150% |`
DOCUMENT_ROW = re.compile(
    r"^\|\s*([A-Za-z ]+?)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)%\s*\|\s*$")

TABLE_HEADER = "| Enemy rarity | Gear drops per kill | Magic find it adds |"


@pytest.fixture(scope="module")
def stated() -> dict[str, tuple[float, float]]:
    """Rarity against (gear drops, magic find), read out of the document."""
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
        rows[match.group(1)] = (float(match.group(2)), float(match.group(3)))
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
             for rarity, (drops, _find) in sorted(stated.items())
             if drops != pytest.approx(loot.ENEMY_GEAR_DROPS[rarity])]
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states drop rates the simulation does not "
        "use: " + "; ".join(wrong)
        + ". A reader would plan against numbers the game does not produce.")


def test_the_document_states_the_magic_find_the_model_uses(stated,
                                                           loot) -> None:
    wrong = [f"{rarity}: document {find:g}%, model {loot.ENEMY_MAGIC_FIND[rarity]:g}%"
             for rarity, (_drops, find) in sorted(stated.items())
             if find != pytest.approx(loot.ENEMY_MAGIC_FIND[rarity])]
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states magic find contributions the "
        "simulation does not use: " + "; ".join(wrong))
