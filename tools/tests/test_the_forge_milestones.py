"""The forge's two empire milestone options act as their texts say, and nowhere else.

WHY THIS EXISTS. Issue #47, slice F6, ruled by the coordinating session on 2026-11-22 (R1 to R8). The Soul Forge and The
Master Tinkerer waited on crafting until this slice. Continuous integration compiles no C++, so the automation tests
`Cataclysm.ForgeMilestones.` never run on a pull request; this check holds what can be read from the files:

  1. the two effects' figures are the document's 15% and 5% (`docs/Empire_Skill_Tree_Keystones.md`);
  2. neither option waits on a system any more, in the generator or in the generated node table;
  3. the upgrade asks `SoulForgeLevels` inside the change it hands the one craft sequence, and the enchantment crafts ask
     `EnchantCritical` and take the days away BEFORE they hand theirs over, since the sequence passes the step's days;
  4. each draw has a salt no other forge draw uses, so a craft without the option draws as it did (R3).
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
CRAFT = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
KEYSTONES = REPO_ROOT / "docs" / "Empire_Skill_Tree_Keystones.md"

if str(REPO_ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def body(text: str, signature: str) -> str:
    """The function that opens with `signature`, to its closing brace at the start of a line."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    end = text.find("\n}\n", start)
    return text[start:end]


def the_documents_percent(option: str) -> float:
    line = next(line for line in KEYSTONES.read_text(encoding="utf-8").splitlines() if f"**{option}:**" in line)
    found = re.search(r"\*\*(\d+)%\*\*", line)
    assert found, line
    return int(found.group(1)) / 100.0


def test_the_two_effects_are_the_documents_figures():
    assert gen.EMPIRE_TREE_EFFECTS["The Soul Forge"] == [("UpgradeExtraLevelChance",
                                                          the_documents_percent("The Soul Forge"))]
    assert gen.EMPIRE_TREE_EFFECTS["The Master Tinkerer"] == [("EnchantCriticalChance",
                                                               the_documents_percent("The Master Tinkerer"))]


def test_neither_option_waits_on_a_system():
    assert "The Soul Forge" not in gen.EMPIRE_TREE_REFUSED
    assert "The Master Tinkerer" not in gen.EMPIRE_TREE_REFUSED
    with (REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    seen = []
    for row in rows:
        for index in (1, 2, 3):
            if row[f"Option{index}Name"] in ("The Soul Forge", "The Master Tinkerer"):
                seen.append(row[f"Option{index}Name"])
                assert row[f"Option{index}WaitsOn"] == "", row[f"Option{index}Name"]
    assert sorted(seen) == ["The Master Tinkerer", "The Soul Forge"]


def test_the_crafts_ask_the_options_where_the_sequence_lets_them_act():
    text = CRAFT.read_text(encoding="utf-8")
    upgrade = body(text, "ECataclysmForgeResult UCataclysmForgeCraft::Upgrade(")
    hand_over = upgrade.find("DoItemCraft(")
    asked = upgrade.find("UCataclysmForge::SoulForgeLevels(")
    assert hand_over != -1 and asked > hand_over, "the upgrade asks The Soul Forge inside the change it hands over"
    assert "Changed.GearLevel = Done.ToLevel;" in upgrade

    work = body(text, "ECataclysmForgeResult UCataclysmForgeCraft::DoEnchantmentWork(")
    hand_over = work.find("DoItemCraft(")
    critical = work.find("UCataclysmForge::EnchantCritical(")
    days = work.find("Step.Days = 0;")
    assert -1 < critical < hand_over, "a critical success is decided before the sequence passes the days"
    assert -1 < days < hand_over, "and its days are taken away before"
    assert work.count("UCataclysmForge::LuckyRolls(") == 2, "an imprint and a change each take the better rolls"


def test_each_draw_has_a_salt_of_its_own():
    salts = re.findall(r"static constexpr int32 (\w+Salt) = (0x[0-9A-Fa-f]+|\d+);", FORGE_H.read_text(encoding="utf-8"))
    values = {}
    for name, value in salts:
        values.setdefault(int(value, 0), []).append(name)
    assert "SoulForgeSalt" in dict(salts) and "EnchantCriticalSalt" in dict(salts)
    shared = {hex(value): names for value, names in values.items() if len(names) > 1}
    assert not shared, f"forge draws sharing a salt: {shared}"
