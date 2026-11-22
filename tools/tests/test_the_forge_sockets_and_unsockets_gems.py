"""The forge's Socket Gem and Unsocket Gem: their numbers against the design, and the shape of the crafts.

WHY THIS EXISTS. Issue #46, slice G4. The coordinating session ruled on 2026-11-21 that socketing is a forge craft that
costs a day, as the design's operations table says, and that a gem comes out only with Dismantling Dust ("Safe
unsocketing"), back to the bag. `UCataclysmForge` (`game/Source/Cataclysm/Items/CataclysmForge.h`) holds the numbers
as constants, and each is stated elsewhere first:

  - the design document's operations rows, "Socket Gem | Jeweler's Setting Agent | 5 | 1" and "Unsocket Gem |
    Dismantling Dust | 3 | 1" (material, residue added, base days);
  - `game/Data/CraftingMaterials.csv`'s operation rows, generated from the workbook's Crafting sheet;
  - `sim/cataclysm_sim/residue.py`'s `SOCKET_GEM_RESIDUE`, which the Consumption Thresholds are derived from.

WHAT IS CHECKED:

  1. the residue and days of both crafts are the design table's, the Crafting sheet's and the simulation's;
  2. the materials they consume are the rows those operations name;
  3. there is no free removal: Unsocket Gem is refused without Dust;
  4. each craft is refused during a walk, passes its days through the run, says its PLAY LOOP line and saves;
  5. whatever a craft took is put back when it cannot be finished;
  6. the screen asks on a socket action's first press and acts on the second;
  7. `Cataclysm.GrantGem` exists, so a gem can be socketed before gems drop.

`Cataclysm.ForgeGems.` (`game/Source/Cataclysm/Tests/CataclysmForgeGemsTests.cpp`) tests what the crafts do.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
WIDGET_CPP = SOURCE / "Interface" / "CataclysmForgeWidget.cpp"
CHARACTER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
MATERIALS = REPO_ROOT / "game" / "Data" / "CraftingMaterials.csv"

sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import residue  # noqa: E402


def code(path: pathlib.Path) -> str:
    """The file with each `//` comment taken off its line, so a mention in a comment cannot satisfy a check."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where}: the body of {signature!r} never closes")


def constant(name: str) -> float:
    found = re.search(rf"static constexpr (?:int32|float) {name} = (\d+(?:\.\d+)?)f?;", FORGE_H.read_text(
        encoding="utf-8"))
    assert found, f"{FORGE_H.name} declares no number {name}"
    return float(found.group(1))


def row_text(name: str) -> str:
    found = re.search(r'const TCHAR\* UCataclysmForge::' + name + r' = TEXT\("([^"]+)"\);', code(FORGE_CPP))
    assert found, f"UCataclysmForge::{name} is not defined as a row name"
    return found.group(1)


def materials() -> dict[str, dict[str, str]]:
    with MATERIALS.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def design_row(action: str) -> list[str]:
    for line in GDD.read_text(encoding="utf-8").splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells and cells[0] == action:
            return cells
    raise AssertionError(f"the design document's operations table has no row {action!r}")


def test_the_residue_and_days_are_the_designs_the_sheets_and_the_simulations() -> None:
    rows = materials()
    for action, residue_name, days_name, sheet_row in (
            ("Socket Gem", "SocketGemResidue", "SocketGemBaseDays", "Material_Socket_Gem"),
            ("Unsocket Gem", "UnsocketGemResidue", "UnsocketGemBaseDays", "Material_Unsocket_Gem")):
        cells = design_row(action)
        assert constant(residue_name) == float(cells[2]), f"{action} adds {cells[2]} residue in the design"
        assert constant(days_name) == float(cells[3]), f"{action} costs {cells[3]} base day in the design"
        sheet = rows[sheet_row]
        assert sheet["PrimaryUse"] == f"{int(constant(residue_name))} CR", f"{sheet_row} states {sheet['PrimaryUse']!r}"
        assert sheet["Functions"] == str(int(constant(days_name))), f"{sheet_row} states {sheet['Functions']!r} days"
    assert constant("SocketGemResidue") == residue.SOCKET_GEM_RESIDUE, (
        "the simulation's SOCKET_GEM_RESIDUE, which the Consumption Thresholds are derived from, differs")


def test_the_materials_are_the_rows_the_operations_name() -> None:
    rows = materials()
    for name, operation in (("SettingAgentRow", "Material_Socket_Gem"), ("DismantlingDustRow", "Material_Unsocket_Gem")):
        material = row_text(name)
        assert material in rows, f"UCataclysmForge::{name} names {material!r}, which CraftingMaterials.csv lacks"
        assert rows[material]["MaterialName"] == rows[operation]["TierAndSource"], (
            f"{operation} consumes {rows[operation]['TierAndSource']!r} and UCataclysmForge::{name} names "
            f"{rows[material]['MaterialName']!r}")
        assert design_row(rows[operation]["MaterialName"])[1] == rows[material]["MaterialName"]


def test_there_is_no_free_removal() -> None:
    unsocketing = body(code(FORGE_CPP), "FCataclysmForgeSocketing UCataclysmForge::UnsocketGemOf(", FORGE_CPP.name)
    refusal = unsocketing.find("Out.MaterialHeld < 1")
    possible = unsocketing.find("Out.bPossible = true;")
    assert refusal != -1 and possible != -1 and refusal < possible, (
        "UnsocketGemOf can say yes without Dismantling Dust; ruled 2026-11-21: no free removal")


def test_each_craft_refuses_during_a_walk_passes_its_days_logs_and_saves() -> None:
    text = code(CRAFT_CPP)
    for signature in ("ECataclysmForgeResult UCataclysmForgeCraft::SocketGem(",
                      "ECataclysmForgeResult UCataclysmForgeCraft::UnsocketGem("):
        found = body(text, signature, CRAFT_CPP.name)
        assert "RefusalNow(WorldContext)" in found and "DoSocketing(" in found, f"{signature} skips a step"
    doing = squeeze(body(text, "ECataclysmForgeResult UCataclysmForgeCraft::DoSocketing(", CRAFT_CPP.name))
    for step in ("Run->AdvanceDays(Plan.Days);", "FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::ForgeSocketLine(",
                 "UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::ForgeCrafted);"):
        assert step in doing, f"DoSocketing no longer does {step!r}"


def test_what_a_craft_took_is_put_back_when_it_cannot_finish() -> None:
    doing = squeeze(body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoSocketing(", CRAFT_CPP.name))
    assert doing.count("Inventory->AddMaterial(Plan.Material, 1);") == 3, (
        "the material must be put back on each of the three failures after it is taken: the gem not taken, the item "
        "not replaced, and the unsocketed gem with nowhere to go")
    assert "PutTheItemBack();" in doing, "an unsocketed gem with nowhere to go must leave the item as it was"


def test_the_screen_asks_before_it_acts() -> None:
    handling = squeeze(body(code(WIDGET_CPP), "void UCataclysmForgeWidget::HandleActionClicked(", WIDGET_CPP.name))
    asked = handling.find("Message = UCataclysmForge::SocketingQuestion(Plan);")
    done = handling.find("UCataclysmForgeCraft::UnsocketGem(")
    assert asked != -1 and done != -1 and asked < done, "the screen socketing on the first press, without asking"
    assert "PendingSocketAction = ButtonName;" in handling


def test_a_gem_can_be_granted_from_the_console() -> None:
    assert 'TEXT("Cataclysm.GrantGem")' in code(CHARACTER_CPP), (
        "Cataclysm.GrantGem is gone, so nothing puts a gem in the bag until gems drop (slice G5)")
