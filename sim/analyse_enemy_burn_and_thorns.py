"""Hellfire Aura's burn and Thorns of Glass's strike back against a new character -- issue #2749, the enemy burn slice.

WHY THIS EXISTS. The project owner played on 2026-11-19 and died on floor 2 of a 5-floor Outpost dungeon at level 2,
with 228 maximum health. Most of it was Hellfire Aura's burn, the status table's flat 25 a tick, which no tier, ring
or floor scaled; and on floor 1 one Elite Brute carrying Thorns of Glass struck back seven times in four seconds, once
for every hit. The coordinating session ruled that an enemy-applied damage over time is sized from what applies it
(the share is section 1's measurement), and the owner ruled that Thorns of Glass strikes back at most once per its
own attack interval (section 3 prices both rules). This file writes no constant.

THE FIGHT IS THE PACK TEST'S OWN, run from `tools/tests/test_the_starting_character_survives_a_pack.py` so the
measurement and the guard cannot part company: 10 Common Imps as the game builds them on a floor, the starting Ravager
with the gear-level-0 Greataxe, levelling at half each floor's kills. LOADING THAT FILE NEEDS `pytest`
(`sim/requirements-dev.txt`); the simulation itself still has no dependency.

WHAT IT MEASURED, 2026-11-19, on development 5b401cb87 with this slice. Section 1 at 300 seeds (3,057 first-surge
Outpost dungeons), one aura carrier in reach on every floor, won with 10% health left on the last floor:

    carrier                 flat 25   0.05    0.08    0.09    0.10    0.20
    Elite Imp                  41    3,057   3,057   3,057   3,057   3,057
    Elite Abyssal Warden       41    3,057   3,057   3,057   3,054   2,775

0.09 is the largest step of 0.01 that passes with the strongest carrier; the three short at 0.10 are one shape, a
3-floor Volatile dungeon of modifier score 15, ending with 9.3% health. On seeds 0 to 59 alone (597 dungeons) 0.10
passed, which is the figure the coordinating session first ruled; Path of Exile 2's 0.20 left 36 of those 597
short, and 282 of the 3,057. Section 2: standing alone in a Warden's aura on floor 4, a level 1 character dies in
6 seconds at the flat 25, 192 at 0.09 and 48 at 0.20.

Section 3, Thorns of Glass, an Elite carrier killed by the pack rotation at level 1 (130 health), creature armour
included: once per hit against once per its own attack interval, and what its own swings deal over the same seconds:

    floor 8 of 8, Brute (interval 1.2 s)           303 back -> 91;  its swings 80
    floor 8 of 8, Abyssal Warden (2.4 s)           527 back -> 99;  its swings 76
    floor 4 of 8, Abyssal Warden                   126 back -> 18;  its swings 12
    the owner's floor-1 Brute, from the log        7 strike backs (84) -> 3 (36)

RUNNING IT. The default is a smoke run of 20 seeds, which proves the code path and nothing else, and says so. For the
figures above:

    CATACLYSM_BURN_SEEDS=300 python -u sim/analyse_enemy_burn_and_thorns.py
"""

from __future__ import annotations

import importlib.util
import math
import os
import pathlib
import re
import time

from cataclysm_sim import damage as dmg
from cataclysm_sim import enemy_stats as es

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PACK_TEST = REPO_ROOT / "tools" / "tests" / "test_the_starting_character_survives_a_pack.py"
CHARACTERS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"

#: How many seeds' first surges section 1 walks. 300 is the measurement; the default is a smoke run.
SEEDS = int(os.environ.get("CATACLYSM_BURN_SEEDS", "20"))
SMOKE = SEEDS < 300

#: The aura shares section 1 walks, as shares of the carrier's hit a tick. None is the table's flat figure.
SHARES = (None, 0.05, 0.08, 0.09, 0.10, 0.20)
#: The carriers section 1 walks: the Elite with the smallest hit and the one with the largest.
CARRIERS = (("Elite", "Imp"), ("Elite", "Abyssal Warden"))
#: The game's header for each kind, for its designed attack interval.
KIND_HEADERS = {"Imp": "CataclysmImpCharacter.h", "Hellhound": "CataclysmHellhoundCharacter.h",
                "Brute": "CataclysmBruteCharacter.h", "Abyssal Warden": "CataclysmAbyssalWardenCharacter.h",
                "Corrupted Sentinel": "CataclysmCorruptedSentinelCharacter.h",
                "Succubus": "CataclysmSuccubusCharacter.h"}
#: When the owner's floor-1 Elite Brute struck back, in seconds of the play log (lines 4216 to 4271 of
#: `Saved/Logs/Cataclysm.log` of 2026-11-19, copied into the slice's window folder).
OWNERS_STRIKE_BACKS = (33.110, 34.107, 35.108, 35.382, 35.634, 36.102, 37.095)


def _load_pack():
    spec = importlib.util.spec_from_file_location("starting_character_pack", PACK_TEST)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACK = _load_pack()
CONSTANTS = PACK.read_constants()
LINES = PACK.read_class_lines()
SLOTS = PACK.read_slots()
WEAPON = PACK.read_weapon(CONSTANTS)
MODEL = PACK.experience_model()
CLASS = CONSTANTS["starting_class"]


def game_attack_interval(kind: str) -> float:
    """A kind's designed seconds between attacks, read from its own C++ header; 1.5 for one that states none."""
    text = (CHARACTERS / KIND_HEADERS[kind]).read_text(encoding="utf-8")
    found = re.search(r"DesignedAttackIntervalSeconds\s*=\s*([\d.]+)f", text)
    if found:
        return float(found.group(1))
    base = (CHARACTERS / "CataclysmEnemyCharacter.h").read_text(encoding="utf-8")
    return float(re.search(r"float AttackIntervalSeconds\s*=\s*([\d.]+)f", base).group(1))


def walk_with_carrier(dungeon, carrier, share):
    """`PACK.walk_in_an_aura` with any carrier and any share; None is the table's flat figure."""
    banked, left = 0.0, 0.0
    for floor in range(1, dungeon.floors + 1):
        level = PACK.level_for(banked, MODEL)
        imp = es.strength_on_floor("Common", 1, "Basic", dungeon.subtype, dungeon.floors, floor,
                                   dungeon.modifier_score, kind="Imp")
        hit = es.strength_on_floor(carrier[0], 1, "Basic", dungeon.subtype, dungeon.floors, floor,
                                   dungeon.modifier_score, kind=carrier[1]).damage_per_hit
        burn = PACK.table_burn_per_tick() if share is None else share * hit
        result = PACK.fight(CLASS, level=level, lines=LINES, slots=SLOTS, weapon=WEAPON, burn_per_second=burn,
                            constants=dict(CONSTANTS, imp_health=str(imp.health), imp_damage=str(imp.damage_per_hit)))
        if result["won"] is not True:
            return False, 0.0
        left = result["health_left"] / result["max_health"]
        banked += PACK.KILL_SHARE * PACK.floor_experience(MODEL, dungeon.subtype, dungeon.floors, floor,
                                                          dungeon.modifier_score)
    return True, left


def rotation_hits(level: int, health: float) -> tuple[int, float]:
    """The pack fight's rotation against one target of `health`: the hits it takes to kill and the seconds."""
    slots = PACK.slots_at_level(SLOTS, LINES, CONSTANTS, level)
    weapon = PACK.weapon_against_health(WEAPON, CONSTANTS)
    mana = top = PACK.stat(LINES, CONSTANTS, CLASS, "max_mana", level)
    ready = {slot: 0.0 for slot in PACK.ROTATION}
    elapsed = since_basic = 0.0
    hits = 0
    while elapsed < PACK.FIGHT_LIMIT_SECONDS and health > 0.0:
        since_basic += PACK.STEP_SECONDS
        if since_basic >= 1.0 / weapon["speed"]:
            since_basic -= 1.0 / weapon["speed"]
            health -= weapon["damage"] * slots["Basic"]["damage"]
            hits += 1
            mana = min(top, mana + slots["Basic"]["mana_on_hit"])
        for slot in PACK.ROTATION:
            spec = slots.get(slot)
            if health > 0.0 and spec and elapsed >= ready[slot] and mana >= spec["mana"]:
                health -= weapon["damage"] * spec["damage"]
                hits += 1
                mana -= spec["mana"]
                ready[slot] = elapsed + spec["cooldown"]
                break
        mana = min(top, mana + PACK.stat(LINES, CONSTANTS, CLASS, "mana_regen", level) * PACK.STEP_SECONDS)
        elapsed += PACK.STEP_SECONDS
    return hits, elapsed


def once_per_interval(times: tuple[float, ...], interval: float) -> list[float]:
    """The strike backs the owner's rule keeps: the first, then each one its interval after the last kept."""
    kept: list[float] = []
    for moment in times:
        if not kept or moment - kept[-1] >= interval:
            kept.append(moment)
    return kept


def main() -> dict:
    started = time.time()
    results: dict = {}
    if SMOKE:
        print(f"SMOKE RUN: {SEEDS} seeds. The figures in the docstring are 300; set CATACLYSM_BURN_SEEDS.")
        print()

    print("1. HELLFIRE AURA'S BURN: A NEW CHARACTER THROUGH THE FIRST SURGE'S OUTPOST DUNGEONS IN AN AURA ON EVERY FLOOR")
    print()
    dungeons = PACK.first_surge_dungeons(range(SEEDS))
    print(f"  {len(dungeons)} dungeons from seeds 0 to {SEEDS - 1}; won with 10% health left on the last floor:")
    print("  carrier                " + "".join(f"{'flat' if s is None else s:>9}" for s in SHARES))
    sweep: dict = {}
    for carrier in CARRIERS:
        row = []
        for share in SHARES:
            walked = [walk_with_carrier(d, carrier, share) for d in dungeons]
            passed = sum(1 for won, left in walked if won and left >= PACK.LEAST_HEALTH_LEFT)
            sweep[(carrier, share)] = (passed, len(walked))
            row.append(f"{passed:>9}")
        print(f"  {' '.join(carrier):<22} " + "".join(row))
    results["sweep"] = sweep
    print(f"  shipped share: {es.HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE}")
    print()
    print()

    print("2. STANDING IN ONE AURA ALONE ON FLOOR 4 OF AN 8-FLOOR DUNGEON: SECONDS UNTIL IT KILLS A CHARACTER AT FULL HEALTH")
    print()
    hit = es.strength_on_floor("Elite", 1, "Basic", "None", 8, 4, 25.0, kind="Abyssal Warden").damage_per_hit
    alone: dict = {}
    for share in (None, es.HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE, 0.20):
        cells = []
        for level in (1, 2, 3, 5):
            burn = PACK.table_burn_per_tick() if share is None else share * hit
            net = (burn * PACK.share_of_a_tick_taken(LINES, CONSTANTS, CLASS, level)
                   - PACK.stat(LINES, CONSTANTS, CLASS, "health_regen", level))
            seconds = PACK.stat(LINES, CONSTANTS, CLASS, "max_health", level) / net if net > 0 else math.inf
            alone[(share, level)] = seconds
            cells.append(f"L{level} {seconds:6.0f} s" if net > 0 else f"L{level}   never")
        label = "the table's flat figure" if share is None else f"{share} of an Elite Warden's hit"
        print(f"  {label:<34} " + "  ".join(cells))
    results["alone"] = alone
    print()
    print()

    print("3. THORNS OF GLASS: AN ELITE CARRIER KILLED BY THE PACK ROTATION AT LEVEL 1, ITS ARMOUR INCLUDED")
    print()
    thorns: dict = {}
    taken = PACK.share_of_a_hit_taken(LINES, CONSTANTS, CLASS, 1, 1)
    health = PACK.stat(LINES, CONSTANTS, CLASS, "max_health", 1)
    for floors, floor, sub in ((8, 4, "None"), (8, 8, "None"), (5, 1, "Volatile")):
        print(f"  floor {floor} of a {floors}-floor {sub if sub != 'None' else 'Basic'} dungeon:")
        for kind in ("Brute", "Abyssal Warden", "Corrupted Sentinel", "Hellhound"):
            elite = es.strength_on_floor("Elite", 1, "Basic", sub, floors, floor, 25.0, kind=kind)
            kept = 1.0 - dmg.armor_reduction(elite.armor, 1) / 100.0
            hits, seconds = rotation_hits(1, elite.health / kept)
            interval = game_attack_interval(kind)
            gated = min(hits, 1 + math.floor(seconds / interval))
            per_hit = hits * elite.damage_per_hit * taken
            per_interval = gated * elite.damage_per_hit * taken
            own = seconds / interval * elite.damage_per_hit * taken
            thorns[(floors, floor, sub, kind)] = (hits, per_hit, gated, per_interval, own)
            print(f"    {kind:<18} interval {interval:3.1f} s: {hits:>2} hits, {per_hit:6.1f} back once per hit "
                  f"({per_hit / health:4.0%}) -> {gated} strike backs, {per_interval:6.1f} ({per_interval / health:4.0%}); "
                  f"its own swings {own:5.1f}")
    results["thorns"] = thorns
    brute = game_attack_interval("Brute")
    kept = once_per_interval(OWNERS_STRIKE_BACKS, brute)
    results["owners_brute"] = (len(OWNERS_STRIKE_BACKS), len(kept))
    print(f"  the owner's floor-1 Elite Brute (12 a strike back): {len(OWNERS_STRIKE_BACKS)} once per hit, "
          f"{len(kept)} once per {brute} s ({kept})")
    print()
    print("  Every skill taken as single-target and creature evasion left out, as the pack test does; one area hit on N")
    print("  carriers draws one strike back from each that is due.")
    print()
    print(f"Done in {time.time() - started:.0f} seconds.")
    return results


# CALLED AT IMPORT RATHER THAN UNDER AN `if __name__` GUARD, as the other analysis scripts are:
# `sim/tests/test_analysis_scripts.py` runs them through `runpy.run_path`, which does not set `__name__`.
RESULTS = main()
