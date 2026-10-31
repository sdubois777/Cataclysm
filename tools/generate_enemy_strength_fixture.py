"""Write the figures the game must build a creature with, from the simulation's conversion.

WHAT IT IS FOR. Issue #1569. The game builds a creature's health, damage and armour
from the design's enemy score on its floor (`UCataclysmEnemyScore::StrengthScoreFor`
and `UCataclysmEnemyStrength`), and the simulation does the same arithmetic in
`enemy_stats.strength_on_floor`. This writes the simulation's answer for a grid of
creatures, rarities, tiers and floors to `game/Tests/EnemyStrength/ExpectedFigures.csv`,
and the game's automation test `Cataclysm.EnemyStrength.EveryFigureIsTheModels` builds
each row and compares. The two are one model only while that file is current:
`tools/tests/test_enemy_strength_fixture_is_current.py` regenerates it in memory and
fails if the committed copy differs.

THE FIGURES ARE THE CREATURE AS IT STANDS ON THE FLOOR, rarity included: the game's
Starting figures multiplied by `UCataclysmEnemyRarity::ScalingFromCommon`.

Run from the repository root: `python tools/generate_enemy_strength_fixture.py`.
"""
from __future__ import annotations

import csv
import io
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import enemy_stats as es  # noqa: E402

FIXTURE = REPO_ROOT / "game" / "Tests" / "EnemyStrength" / "ExpectedFigures.csv"

#: The seven creatures a dungeon floor places, by their row of `game/Data/EnemyArchetypes.csv`
#: and the name `enemy_stats.archetype` knows them by.
KINDS = (("Imp", "Imp"), ("Hellhound", "Hellhound"), ("Brute", "Brute"),
         ("Abyssal_Warden", "Abyssal Warden"), ("Corrupted_Sentinel", "Corrupted Sentinel"),
         ("Succubus", "Succubus"), ("Gatekeeper", "Gatekeeper"))
TIERS = (1, 2, 4, 8)
#: (dungeon type as the game's enum names it, as the model names it, total floors, floors)
DUNGEONS = (("Basic", "Basic", 12, (1, 6, 12)), ("Cataclysm", "Cataclysm", 50, (25,)))
#: A dungeon modifier score, on one floor per kind and tier, so the flat addend is exercised.
MODIFIER_SCORE = 30.0
MODIFIER_FLOOR = ("Basic", "Basic", 12, 6)

HEADER = ("Archetype", "RarityStep", "Tier", "DungeonType", "TotalFloors", "Floor", "ModifierScore",
          "StrengthScore", "Health", "Damage", "Armour")


def rows():
    for row_key, name in KINDS:
        for tier in TIERS:
            cases = [(game, model, total, floor, 0.0) for game, model, total, floors in DUNGEONS for floor in floors]
            cases.append((*MODIFIER_FLOOR, MODIFIER_SCORE))
            for game_type, model_type, total, floor, modifiers in cases:
                for step, rarity in enumerate(es.RARITY_ORDER):
                    built = es.strength_on_floor(rarity, tier, model_type, "None", total, floor, modifiers, kind=name)
                    yield (row_key, step, tier, game_type, total, floor, f"{modifiers:g}", f"{built.score:.4f}",
                           f"{built.health:.4f}", f"{built.damage_per_hit:.4f}", f"{built.armor:.4f}")


def text() -> str:
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(HEADER)
    writer.writerows(rows())
    return out.getvalue()


def main() -> int:
    FIXTURE.parent.mkdir(parents=True, exist_ok=True)
    FIXTURE.write_text(text(), encoding="utf-8", newline="\n")
    print(f"wrote {FIXTURE.relative_to(REPO_ROOT)}: {text().count(chr(10)) - 1} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
