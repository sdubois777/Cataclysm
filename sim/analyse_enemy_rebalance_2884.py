"""The enemy rebalance after the affix rescale -- issue #2884. A MEASUREMENT; it writes no constant.

WHAT IT ANSWERS. The project owner approved the affix ranges of issue #2883 on 2026-11-24 and ruled "upgrade
everything, once we've fixed the ranges and values, we can look at rebalancing enemies to fit" and, of flat damage,
"keep the 6 slots and upgrade the damage. We'll fix it with the enemy balance audit". This prints, for the reference
character the enemy constants were fitted against, what the enemies give it at every difficulty tier, so the figures
can be compared between the simulation as it stands on development (with #2853's two branches) and the simulation
with the approved affixes, the re-fitted reference build and the proposed enemy factors.

    python -u sim/analyse_enemy_rebalance_2884.py --json after.json
    python -u sim/analyse_enemy_rebalance_2884.py --tables today --json after_old_constants.json
    python -u sim/analyse_enemy_rebalance_2884.py --sim-root <a copy of the branch before the change>/sim --json before.json
    python -u sim/analyse_enemy_rebalance_2884.py --fit before.json after_old_constants.json

`--sim-root` runs this same script against another checkout's `cataclysm_sim`, so "before" is the code as it was and
not a second copy of it. `--tables today` sets the proposal's two factor tables to 1.00 at run time (after import,
because `player_damage` refuses at import a reference loadout more than 5% off the damage target, which is the
whole problem this measures). `--fit` prints the two tables the proposal is built from.

WHAT EACH FIGURE IS.
  reference character   `fight.reference_player(tier)`: the tier's reference level, its attribute points in the
                        build's 60:40 split, the reference build's affixes at the tier's affix tier and gear level.
                        At tier 8 it is `reference_build.character()`.
  effective health      its health over the share of a 1,000 Demonic hit that reaches health (every layer, averaged
                        over block): how much a creature's damage has to add up to before it dies.
  hit                   `player_damage.damage_per_hit(tier)`: one non-critical basic attack, the Axe and Sword.
  gap                   hit over `affixes.damage_target(tier)`; 1 means an average Common takes 2 hits.
  boss seconds          `fight.fight(tier)`: the Boss the game builds on floor 25 of a 50-floor Basic dungeon, the
                        player killing it at `player_damage.damage_per_second` and it killing the player.
  ... with the build    the same kill with the reference build's own attack speed and critical strikes counted.
  survival              tier 8, each creature at the rarity it is met at, `damage.hits_to_kill` against
                        `reference_build.defender(8)`, as the design's "How Long a Geared Character Survives" states.

Pure standard library. Runs in a few seconds.
"""

from __future__ import annotations

import argparse
import importlib
import json
import math
import pathlib
import sys

TIERS = range(1, 9)
MET_AT = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"), ("Brute", "Elite"),
          ("Corrupted Sentinel", "Legendary"), ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))
PROBE = 1000.0


def load(sim_root: pathlib.Path) -> dict:
    sys.path.insert(0, str(sim_root))
    modules = {}
    for name in ("affixes", "damage", "enemy_stats", "reference_build", "player_damage", "fight", "player_power"):
        modules[name] = importlib.import_module(f"cataclysm_sim.{name}")
    from cataclysm_sim.character import Attributes, Character
    from cataclysm_sim.classes import DEMONIC_CLASSES
    modules["Attributes"], modules["Character"], modules["DEMONIC_CLASSES"] = Attributes, Character, DEMONIC_CLASSES
    return modules


def landed_share(m, defender) -> float:
    return m["damage"].average_damage_taken(m["damage"].Attacker(damage=PROBE, damage_type="Demonic"),
                                            defender) / PROBE


def per_tier(m) -> list[dict]:
    fg, pd, af, es = m["fight"], m["player_damage"], m["affixes"], m["enemy_stats"]
    rows = []
    for tier in TIERS:
        hero = fg.reference_player(tier)
        defender = fg._defender(hero, tier)
        landed = landed_share(m, defender)
        hit = pd.damage_per_hit(tier)
        target = af.damage_target(tier)
        stalker = fg.fight(tier)
        gear = hero.gear
        attack_speed = gear.increased.get("attack_speed", 0.0)
        chance = min(100.0, hero.stat("crit_chance")) / 100.0
        crit = 1.0 - chance + chance * hero.stat("crit_multiplier") / 100.0
        boss = es.strength_on_floor("Boss", tier, *fg.BOSS_DUNGEON[:3], floor=fg.BOSS_DUNGEON[3])
        with_build = boss.effective_health / (pd.damage_per_second(tier) * (1.0 + attack_speed) * crit
                                              * boss.damage_taken_fraction())
        rows.append({
            "tier": tier, "level": hero.level, "health": hero.stat("max_health"), "armor": hero.stat("armor"),
            "block": hero.stat("block_chance"), "damage_reduction": hero.stat("damage_reduction"),
            "resistance": hero.stat("resistance_demonic"), "landed": landed,
            "effective_health": defender.health / landed, "hit": hit, "target": target, "gap": hit / target,
            "common_hits": af.HITS_TO_KILL_A_COMMON_ENEMY * target / hit,
            "boss_seconds": stalker.boss_seconds_to_kill, "boss_kills_in": stalker.boss_seconds_to_kill_the_player,
            "attack_speed_increase": attack_speed, "crit_factor": crit, "boss_seconds_with_build": with_build,
            "stalker_health_x": stalker.health_multiplier, "stalker_damage_x": stalker.damage_multiplier,
            "boss_health": boss.effective_health, "boss_hit": boss.average_damage_per_hit,
        })
    return rows


def survival(m) -> dict:
    es, dmg, rb = m["enemy_stats"], m["damage"], m["reference_build"]
    against = rb.defender(8)
    out = {"landed_at_8": rb.damage_taken_fraction(8), "health_at_8": against.health,
           "block_at_8": against.block_chance, "damage_reduction_at_8": against.damage_reduction,
           "armor_at_8": against.armor, "resistance_at_8": against.resistances["Demonic"], "met": {}}
    for name, rarity in MET_AT:
        e = es.stats_on_floor(rarity, 8, "Cataclysm", kind=name)
        hits = dmg.hits_to_kill(dmg.Attacker(damage=e.average_damage_per_hit, damage_type=e.damage_type), against)
        out["met"][f"{rarity} {name}"] = {"hit": e.average_damage_per_hit, "hits": hits,
                                          "seconds": math.ceil(hits) * e.attack_interval, "health": e.health}
    average = es.stats_on_floor("Common", 8, "Cataclysm")
    out["average_common_hits"] = dmg.hits_to_kill(
        dmg.Attacker(damage=average.average_damage_per_hit, damage_type=average.damage_type), against)
    imp = es.stats_on_floor("Common", 8, "Cataclysm", kind="Imp")
    per_hit = dmg.average_damage_taken(dmg.Attacker(damage=imp.average_damage_per_hit, damage_type=imp.damage_type),
                                       against)
    out["pack_seconds"] = {n: against.health / (per_hit * n / imp.attack_interval) for n in (1, 10, 20)}
    bare = m["Character"](m["DEMONIC_CLASSES"]["Ravager"], level=100, attributes=m["Attributes"](vitality=100))
    out["gearless_pool"] = bare.stat("max_health") + bare.stat("max_energy_shield")
    out["gatekeeper_hit"] = es.stats_on_floor("Cataclysm Boss", 8, "Cataclysm", kind="Gatekeeper").average_damage_per_hit
    out["mitigation_ceiling"] = es.ENEMY_MITIGATION_CEILING
    return out


def sensitivity(m) -> dict:
    """The effective health of three other ways to re-fit the build, against the one adopted. Only on a checkout that
    has the re-fit (`reference_build.LAYOUT`). Counted as totals, as the build was before #2884, and not placed piece by
    piece: the offence-leaning one fills the 23 offensive suffix places the twelve offensive pieces have left after
    penetration (crit chance 8, multiplier 8, attack speed 7) and leaves six defensive-piece suffixes on utility."""
    rb, fg = m["reference_build"], m["fight"]
    if not hasattr(rb, "LAYOUT"):
        return {}
    saved = dict(rb.PREFIX_SPEND), dict(rb.SUFFIX_SPEND), rb.RESISTANCE_AFFIXES
    base = {"FLAT_BLOCK_CHANCE": 2, "FLAT_DAMAGE_REDUCTION": 1, "FLAT_PENETRATION": 1, "INCREASED_CRIT_CHANCE": 4,
            "FLAT_CRIT_MULTIPLIER": 4, "INCREASED_ATTACK_SPEED": 4}
    variants = {
        "adopted: freed defensive slots on Vitality 8 and Constitution 5": dict(rb.SUFFIX_SPEND),
        "freed slots left empty (16 unspent suffixes)": dict(base),
        "block on one piece, freed slots on offence": dict(base, FLAT_BLOCK_CHANCE=1, INCREASED_CRIT_CHANCE=8,
                                                           FLAT_CRIT_MULTIPLIER=8, INCREASED_ATTACK_SPEED=7),
        "freed slots on Vitality only (13)": dict(base, INCREASED_VITALITY=13, INCREASED_CRIT_CHANCE=5,
                                                  FLAT_CRIT_MULTIPLIER=5, INCREASED_ATTACK_SPEED=5),
    }
    out = {}
    try:
        for label, suffixes in variants.items():
            rb.SUFFIX_SPEND.clear()
            rb.SUFFIX_SPEND.update(suffixes)
            rb.RESISTANCE_AFFIXES = 4
            out[label] = [fg._defender(fg.reference_player(t), t).health / landed_share(
                m, fg._defender(fg.reference_player(t), t)) for t in TIERS]
    finally:
        rb.PREFIX_SPEND.clear()
        rb.PREFIX_SPEND.update(saved[0])
        rb.SUFFIX_SPEND.clear()
        rb.SUFFIX_SPEND.update(saved[1])
        rb.RESISTANCE_AFFIXES = saved[2]
    return out


def report(m, label: str) -> dict:
    es = m["enemy_stats"]
    tables = (getattr(es, "HEALTH_SCALE_BY_TIER_2884", None), getattr(es, "DAMAGE_SCALE_BY_TIER_2884", None))
    rows = per_tier(m)
    surv = survival(m)
    sens = sensitivity(m)
    print(f"== {label}")
    print(f"   enemy factor tables: health {tables[0]}, damage {tables[1]}")
    print(f"   DAMAGE_AT_COMMON {es.DAMAGE_AT_COMMON}, DAMAGE_PER_STEP {es.DAMAGE_PER_STEP}, "
          f"HEALTH_AT_COMMON {es.HEALTH_AT_COMMON}, HEALTH_PER_STEP {es.HEALTH_PER_STEP}")
    print()
    print("   tier level  health   armour  block    DR   res  lands  eff.health    hit  target   gap  Common hits"
          "  boss s  boss kills s  boss s (build)  stalker hx  stalker dx")
    for r in rows:
        print(f"   {r['tier']:>4} {r['level']:>5} {r['health']:>7,.0f} {r['armor']:>8,.0f} {r['block']:>6.1f}"
              f" {r['damage_reduction']:>5.1f} {r['resistance']:>5.0f} {r['landed']:>6.2%} {r['effective_health']:>11,.0f}"
              f" {r['hit']:>6,.0f} {r['target']:>7,.0f} {r['gap']:>5.2f} {r['common_hits']:>12.2f}"
              f" {r['boss_seconds']:>7.1f} {r['boss_kills_in']:>13.1f} {r['boss_seconds_with_build']:>15.1f}"
              f" {r['stalker_health_x']:>11.2f} {r['stalker_damage_x']:>11.2f}")
    print()
    print(f"   Tier 8, against reference_build.defender(8): health {surv['health_at_8']:,.0f}, armour "
          f"{surv['armor_at_8']:,.0f}, block {surv['block_at_8']:.1f}, damage reduction "
          f"{surv['damage_reduction_at_8']:.1f}, resistance {surv['resistance_at_8']:.0f}; a hit lands for "
          f"{surv['landed_at_8']:.2%} (the player stops {100 * (1 - surv['landed_at_8']):.2f}%; the enemy ceiling "
          f"is {surv['mitigation_ceiling']:.0f}%)")
    print(f"   {'creature':<30} {'its hit':>10} {'hits':>7} {'seconds':>8}")
    for name, v in surv["met"].items():
        print(f"   {name:<30} {v['hit']:>10,.0f} {v['hits']:>7.0f} {v['seconds']:>8.1f}")
    print(f"   an average Common: {surv['average_common_hits']:.0f} hits; Imps kill it in "
          + ", ".join(f"{n} {s:.1f} s" for n, s in surv["pack_seconds"].items())
          + f"; the Gatekeeper's hit {surv['gatekeeper_hit']:,.0f} against a gearless pool of "
            f"{surv['gearless_pool']:,.0f}")
    if sens:
        print()
        print("   The re-fit's effective health at tiers 1 to 8, against other ways to spend the freed slots:")
        for label_, values in sens.items():
            print(f"   {label_:<62} " + " ".join(f"{v:>10,.0f}" for v in values))
    print()
    return {"label": label, "tables": tables, "per_tier": rows, "survival": surv, "sensitivity": sens}


def fit(before_path: str, after_path: str) -> None:
    before = json.loads(pathlib.Path(before_path).read_text(encoding="utf-8"))
    after = json.loads(pathlib.Path(after_path).read_text(encoding="utf-8"))
    print("== The factors that restore each tier, the re-fitted character over today's")
    print("   tier   hit ratio (health factor)   effective health ratio (damage factor)")
    health, damage = [], []
    for b, a in zip(before["per_tier"], after["per_tier"], strict=True):
        h = a["hit"] / b["hit"]
        d = a["effective_health"] / b["effective_health"]
        health.append(h)
        damage.append(d)
        print(f"   {b['tier']:>4}   {h:>24.4f}   {d:>38.4f}")
    print("   Tier 1 is held at 1.00 in both: its damage is fitted to the starting character, which carries no affixes.")
    print(f"   health: {tuple([1.0] + [round(h, 2) for h in health[1:]])}")
    print(f"   damage: {tuple([1.0] + [round(d, 2) for d in damage[1:]])}")
    b8, a8 = before["survival"], after["survival"]
    print(f"   tier 8 check through hits_to_kill: average Common {b8['average_common_hits']:.0f} -> "
          f"{a8['average_common_hits']:.0f} hits with today's constants; Gatekeeper "
          f"{b8['met']['Cataclysm Boss Gatekeeper']['hits']:.0f} -> {a8['met']['Cataclysm Boss Gatekeeper']['hits']:.0f}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sim-root", default=str(pathlib.Path(__file__).resolve().parent))
    parser.add_argument("--tables", choices=("proposed", "today"), default="proposed")
    parser.add_argument("--json")
    parser.add_argument("--fit", nargs=2, metavar=("BEFORE", "AFTER"))
    args = parser.parse_args(argv)
    if args.fit:
        fit(*args.fit)
        return 0
    m = load(pathlib.Path(args.sim_root))
    es = m["enemy_stats"]
    label = f"{args.sim_root}, tables {args.tables}"
    if args.tables == "today" and hasattr(es, "HEALTH_SCALE_BY_TIER_2884"):
        es.HEALTH_SCALE_BY_TIER_2884 = (1.0,) * 8
        es.DAMAGE_SCALE_BY_TIER_2884 = (1.0,) * 8
    result = report(m, label)
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(result, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
else:
    # Run by `runpy.run_path`, as `sim/tests/test_analysis_scripts.py` runs every analysis script: the default report.
    main([])
