"""A Rift at difficulty tier 1 never draws the Demonic Guide, in the game and in the model alike (issue #51).

THE OWNER, 2026-11-25, verbatim: "Make demonic guide not spawn on the Rifts. It would be really annoying if you're
trying to destroy a pillar and you can't because that guy will kill you if you let him get out of range.", and a moment
later, "at least not on t1".

WHY THIS EXISTS. The game names the Guide's row twice: `UCataclysmDungeonModifierEffects::DemonicGuideKey` in the
Cataclysm module, which raises the guide on a floor, and `UCataclysmDungeonModifierRules::DemonicGuideKey` in the
CataclysmEmpire module, which leaves it out of a tier 1 Rift's pool and may not include the other. If the row were
renamed in the design workbook and only one name followed, the exclusion would match nothing and no automation test
built from hand-made rows would notice. This holds both names to `game/Data/DungeonModifiers.csv`, and the game's tier
to the model's `TuningConfig.rift_without_the_guide_up_to_tier`.
"""
from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"
RULES_HEADER = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire" / "CataclysmDungeonModifier.h"
RULES_SOURCE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire" / "CataclysmDungeonModifier.cpp"
EFFECTS_SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon" / "CataclysmDungeonModifierEffects.cpp"
RUN_SOURCE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"

sys.path.insert(0, str(REPO_ROOT / "sim"))
from cataclysm_sim.config import TuningConfig  # noqa: E402
from cataclysm_sim.modifiers import DEMONIC_GUIDE, MODIFIERS  # noqa: E402


def _key_after(text: str, pattern: str) -> str:
    found = re.search(pattern + r'\s*TEXT\("([^"]+)"\)', text, re.S)
    assert found, f"no key found after {pattern!r}"
    return found.group(1)


def empire_key() -> str:
    return _key_after(RULES_SOURCE.read_text(encoding="utf-8"),
                      r"FName UCataclysmDungeonModifierRules::DemonicGuideKey\(\)\s*\{\s*return FName\(")


def effects_key() -> str:
    return _key_after(EFFECTS_SOURCE.read_text(encoding="utf-8"),
                      r"const TCHAR\* UCataclysmDungeonModifierEffects::DemonicGuideKey =")


def table_rows() -> dict[str, dict[str, str]]:
    with TABLE.open(newline="", encoding="utf-8") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def test_both_modules_name_the_same_row() -> None:
    assert empire_key() == effects_key() == "Demonic_Demonic_Guide"


def test_the_row_is_in_the_table_in_demonics_column() -> None:
    rows = table_rows()
    assert empire_key() in rows, f"{empire_key()} is not a row of {TABLE.name}"
    assert "Demonic" in rows[empire_key()].values()


def test_the_model_names_the_same_modifier() -> None:
    rows = table_rows()
    assert DEMONIC_GUIDE in rows[empire_key()].values(), "the model's name is not the row's display name"
    assert DEMONIC_GUIDE in {name for name, _ in MODIFIERS["Demonic"]}


def test_the_game_and_the_model_leave_it_out_up_to_the_same_tier() -> None:
    found = re.search(r"static constexpr int32 RiftWithoutTheGuideUpToTier = (\d+);",
                      RULES_HEADER.read_text(encoding="utf-8"))
    assert found, "RiftWithoutTheGuideUpToTier is not declared"
    assert int(found.group(1)) == TuningConfig().rift_without_the_guide_up_to_tier == 1


def test_a_rift_draws_from_the_pool_without_it() -> None:
    """The draw a Rift's eight come from passes through `RiftPoolFor`; a draw from `Pool` itself would hold the
    Guide at tier 1 again. The control: the ordinary draw above it still takes `Pool` as it is."""
    text = RUN_SOURCE.read_text(encoding="utf-8")
    rift = re.search(r"if \(Dungeon\.IsARift\(\)\)\s*\{(.*?)\n\t\}", text, re.S)
    assert rift, "the Rift branch of GiveModifiers was not found"
    assert "RiftPoolFor(Pool, DifficultyTier)" in rift.group(1)
    assert "Draw(Pool, Count, Draws)" in text
