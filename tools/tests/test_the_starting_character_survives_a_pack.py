"""The character a player gets by pressing Play must win the fight in front of it.

WHY THIS FILE EXISTS. Issue #806. The project owner reported they could not
finish a dungeon floor. The character had 100 health, 1 health a second of
regeneration, no armour, no resistance, no block and no flat damage reduction,
against a pack of ten Imps dealing 100 damage a second between them.

Several things have been fixed since: the class stat line reaches the player
(#807), gear modifiers reach attributes, and life leech pays out (#905). Measured
on 2026-08-24 none of that was enough, for a reason nobody had looked at:
`Cataclysm.PlayerClass` defaulted to `Default`, which is the shared line every
class inherits a stat from when it states none of its own. That line carries no
defensive layer at all. Every character anyone had ever played sat on it, and it
is the only one of the four lines that loses the fight.

The console variable now defaults to the Ravager. This file is what stops that
being undone by accident, and what stops a class re-tune making the starting
character unplayable again without anybody noticing.

WHAT IS MEASURED. Ten Imps against one character, both dealing damage until one
side is gone, at difficulty tier 1. Modelled as a fight rather than compared as
two rates, because incoming damage falls as Imps die -- a rate comparison says
the character loses when they may out-kill the pack.

WHAT IS DELIBERATELY PESSIMISTIC, so that passing means something:

* All ten Imps are in contact from the first second and the character stands
  still and trades. A real pack arrives over time.
* Every skill is treated as single-target. Several designed Greataxe skills are
  area effects that would hit the whole pack.
* No critical strikes, and no potions, because no healing item exists.

WHAT IT READS RATHER THAN RESTATES. Every constant is parsed out of the C++ that
owns it and every table value out of the generated CSV, so a change to either
moves this measurement instead of leaving it asserting a stale number. A constant
that cannot be found fails the test rather than falling back to a literal.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"

#: Where each C++ constant lives, and the pattern that reads its value.
#: The name on the left is what this file calls it.
CONSTANTS: dict[str, tuple[pathlib.Path, str]] = {
    "starting_class": (
        SOURCE / "Character" / "CataclysmPlayerClassStats.cpp",
        r"StartingClassName\s*=\s*TEXT\(\"([^\"]+)\"\)"),
    "shared_line": (
        SOURCE / "Character" / "CataclysmClassStats.cpp",
        r"DefaultClassName\s*=\s*TEXT\(\"([^\"]+)\"\)"),
    "default_level": (
        SOURCE / "Character" / "CataclysmPlayerClassStats.h",
        r"DefaultLevel\s*=\s*(\d+)"),
    "starting_weapon": (
        SOURCE / "Character" / "CataclysmPlayerCharacter.h",
        r"StartingWeaponBase\s*=\s*TEXT\(\"([^\"]+)\"\)"),
    "imp_count": (
        SOURCE / "Player" / "CataclysmGameMode.h",
        r"int32 ImpCount\s*=\s*(\d+)"),
    "imp_health": (
        SOURCE / "Player" / "CataclysmGameMode.h",
        r"float ImpHealth\s*=\s*([\d.]+)f"),
    "imp_damage": (
        SOURCE / "Player" / "CataclysmGameMode.h",
        r"float ImpAttackDamage\s*=\s*([\d.]+)f"),
    "imp_interval": (
        SOURCE / "Character" / "CataclysmImpCharacter.h",
        r"DesignedAttackIntervalSeconds\s*=\s*([\d.]+)f"),
    "armour_constant_per_tier": (
        SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.h",
        r"ArmorConstantPerTier\s*=\s*([\d.]+)f"),
    "armour_reduction_cap": (
        SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.h",
        r"ArmorReductionCap\s*=\s*([\d.]+)f"),
    "damage_reduction_cap": (
        SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.h",
        r"DamageReductionCap\s*=\s*([\d.]+)f"),
    "gear_level_factor": (
        SOURCE / "Items" / "CataclysmItem.h",
        r"GearLevelFactor\s*=\s*([\d.]+)f?"),
    "max_gear_level": (
        SOURCE / "Items" / "CataclysmItem.h",
        r"MaxGearLevel\s*=\s*(\d+)"),
    "two_handed_multiplier": (
        SOURCE / "Items" / "CataclysmItem.h",
        r"TwoHandedMultiplier\s*=\s*([\d.]+)f"),
}

#: The slots worth pressing, strongest first. Support deals nothing and an Aura
#: is a per-second drain rather than a hit, so neither is in a rotation.
ROTATION = ("Ultimate", "Heavy", "Special", "Movement")

#: How long one fight may run before it is called a stalemate, in seconds.
#: Far longer than any measured outcome; it exists so a bug cannot hang the run.
FIGHT_LIMIT_SECONDS = 180.0

#: The step the fight advances by, in seconds. Small enough that a 0.9 second
#: attack interval and a 1.5 second cooldown both land near their real times.
STEP_SECONDS = 0.05


@pytest.fixture(scope="module")
def constants() -> dict[str, str]:
    """Every C++ constant this measurement needs, read from the file owning it.

    A pattern that does not match fails here rather than anywhere else, because
    a measurement quietly using a made-up number is worse than one that stops.
    """
    found: dict[str, str] = {}
    for name, (path, pattern) in CONSTANTS.items():
        if not path.is_file():
            pytest.fail(f"{name}: {path} is not here, so the constant it owns "
                        f"cannot be read. If the file moved, update CONSTANTS.")
        match = re.search(pattern, path.read_text(encoding="utf-8"))
        if not match:
            pytest.fail(
                f"{name}: nothing in {path.name} matches {pattern!r}. Either "
                f"the constant was renamed, in which case update CONSTANTS "
                f"here, or it was removed, in which case this measurement no "
                f"longer describes the game. Issue #806.")
        found[name] = match.group(1)
    return found


@pytest.fixture(scope="module")
def class_lines() -> dict[str, dict[str, tuple[float, float]]]:
    """class -> stat -> (base, per level), from game/Data/ClassStats.csv."""
    path = DATA / "ClassStats.csv"
    if not path.is_file():
        pytest.skip(f"{path} has not been generated")
    out: dict[str, dict[str, tuple[float, float]]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            out.setdefault(row["ClassName"], {})[row["Stat"]] = (
                float(row["Base"]), float(row["PerLevel"]))
    assert out, f"{path} holds no class stat rows"
    return out


@pytest.fixture(scope="module")
def slots() -> dict[str, dict[str, float]]:
    """slot -> its damage percent, cooldown and mana, from SkillSlots.csv.

    Every row of game/Data/WeaponSkills.csv states DamagePercent -1, and
    UCataclysmSkillTemplate::GetDamagePercent treats -1 as "take the slot's
    figure", so these are what every skill in the game is worth today.
    """
    path = DATA / "SkillSlots.csv"
    if not path.is_file():
        pytest.skip(f"{path} has not been generated")
    out: dict[str, dict[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            out[row["Slot"]] = {
                "damage": float(row["DamagePercent"]) / 100.0,
                "cooldown": float(row["Cooldown"]),
                "mana": float(row["ManaCost"]),
                "mana_on_hit": float(row["ManaOnHit"]),
            }
    assert "Basic" in out, f"{path} has no Basic row, so nothing swings"
    return out


@pytest.fixture(scope="module")
def weapon(constants) -> dict[str, float]:
    """What the starting weapon actually grants, not what its row states.

    THE STATED VALUE IS THE FIGURE AT MAXIMUM GEAR LEVEL.
    `UCataclysmItemValues::ImplicitValue` divides a stated implicit by the
    multiplier at MaxGearLevel and scales back up to the item's own level, then
    applies the two-handed multiplier. `GiveStartingWeapon` builds the item at
    gear level 0 with no affixes, so the Greataxe's stated 72 attack damage is
    not 72 in a player's hands. `docs/Cataclysm_GDD_v2.md` states the rule:
    "Gear upgrade level multiplies an implicit exactly as it multiplies an
    affix, so the values below are the fully upgraded ones."
    """
    path = DATA / "ItemBases.csv"
    if not path.is_file():
        pytest.skip(f"{path} has not been generated")
    wanted = constants["starting_weapon"]
    with path.open(newline="", encoding="utf-8") as handle:
        row = next((r for r in csv.DictReader(handle) if r["Name"] == wanted), None)
    assert row is not None, (
        f"game/Data/ItemBases.csv has no {wanted!r} row, and that is what "
        f"ACataclysmPlayerCharacter::StartingWeaponBase names. A character "
        f"would start holding nothing.")

    stated = 0.0
    for index in (1, 2):
        if row.get(f"Implicit{index}Stat") == "attack_damage":
            stated = float(row[f"Implicit{index}Value"])
    assert stated > 0.0, (
        f"the {wanted!r} row states no attack_damage implicit, so the starting "
        f"weapon would deal nothing")

    factor = float(constants["gear_level_factor"])
    at_max = 1.0 + factor * int(constants["max_gear_level"])
    two_handed = (float(constants["two_handed_multiplier"])
                  if int(row["Hands"]) == 2 else 1.0)
    # Gear level 0, so the multiplier at the item's own level is exactly 1.
    return {"damage": stated / at_max * two_handed,
            "speed": float(row["AttackSpeed"])}


def stat(lines, constants, class_name: str, name: str, level: int) -> float:
    """What a class has at a level.

    Falls back to the shared line when the class states nothing, which is what
    `UCataclysmClassStats::BaseFor` does, and applies the per-level gain to
    levels above the first, which is what it does too.
    """
    row = (lines.get(class_name, {}).get(name)
           or lines.get(constants["shared_line"], {}).get(name))
    if row is None:
        return 0.0
    base, per_level = row
    return base + per_level * (max(1, level) - 1)


def share_of_a_hit_taken(lines, constants, class_name: str, level: int,
                         tier: int) -> float:
    """After every defensive layer the class has, which multiply.

    Only armour and the damage reduction percentage are ever above zero for a
    starting character: no class line states a resistance or a block chance, and
    the starting weapon carries no affixes.
    """
    armour = stat(lines, constants, class_name, "armor", level)
    reduction = 0.0
    if armour > 0.0:
        k = float(constants["armour_constant_per_tier"]) * max(1, tier)
        reduction = min(float(constants["armour_reduction_cap"]),
                        100.0 * armour / (armour + k))
    flat = min(stat(lines, constants, class_name, "damage_reduction", level),
               float(constants["damage_reduction_cap"]))
    return (1.0 - reduction / 100.0) * (1.0 - flat / 100.0)


def fight(class_name: str, level: int, *, lines, constants, slots, weapon,
          tier: int = 1, use_skills: bool = True) -> dict:
    """Ten Imps against one character. Returns who won and how long it took.

    Everything after `level` is keyword-only, because `lines`, `constants`,
    `slots` and `weapon` are four dictionaries and passing them positionally is
    a mistake nothing would catch.
    """
    def line(name: str) -> float:
        return stat(lines, constants, class_name, name, level)

    max_health, max_shield = line("max_health"), line("max_energy_shield")
    max_mana = line("max_mana")
    health, shield, mana = max_health, max_shield, max_mana
    leech = line("life_leech") / 100.0
    through = share_of_a_hit_taken(lines, constants, class_name, level, tier)

    imp_count = int(constants["imp_count"])
    imp_damage = float(constants["imp_damage"])
    imp_interval = float(constants["imp_interval"])
    imps = [float(constants["imp_health"])] * imp_count

    ready = {slot: 0.0 for slot in ROTATION}
    elapsed = since_swing = since_basic = 0.0
    swing_gap = 1.0 / weapon["speed"]

    def strike(amount: float) -> None:
        nonlocal health
        for index, remaining in enumerate(imps):
            if remaining > 0.0:
                imps[index] = max(0.0, remaining - amount)
                health = min(max_health, health + amount * leech)
                return

    while elapsed < FIGHT_LIMIT_SECONDS:
        alive = sum(1 for remaining in imps if remaining > 0.0)
        if alive == 0:
            return {"won": True, "seconds": round(elapsed, 2),
                    "health_left": round(health, 1), "max_health": max_health}

        since_basic += STEP_SECONDS
        if since_basic >= swing_gap:
            since_basic -= swing_gap
            strike(weapon["damage"] * slots["Basic"]["damage"])
            mana = min(max_mana, mana + slots["Basic"]["mana_on_hit"])

        if use_skills:
            for slot in ROTATION:
                spec = slots.get(slot)
                if spec and elapsed >= ready[slot] and mana >= spec["mana"]:
                    strike(weapon["damage"] * spec["damage"])
                    mana -= spec["mana"]
                    ready[slot] = elapsed + spec["cooldown"]
                    break

        since_swing += STEP_SECONDS
        if since_swing >= imp_interval:
            since_swing -= imp_interval
            incoming = alive * imp_damage * through
            absorbed = min(shield, incoming)
            shield -= absorbed
            health -= incoming - absorbed

        health = min(max_health, health + line("health_regen") * STEP_SECONDS)
        shield = min(max_shield, shield + line("energy_shield_regen") * STEP_SECONDS)
        mana = min(max_mana, mana + line("mana_regen") * STEP_SECONDS)

        if health <= 0.0:
            return {"won": False, "seconds": round(elapsed, 2),
                    "imps_left": alive, "max_health": max_health}
        elapsed += STEP_SECONDS

    return {"won": None, "seconds": FIGHT_LIMIT_SECONDS,
            "imps_left": sum(1 for r in imps if r > 0.0),
            "max_health": max_health}


def test_every_constant_this_measurement_needs_was_found(constants) -> None:
    """Nothing below means anything if the parsing silently returned nothing."""
    assert set(constants) == set(CONSTANTS), (
        f"missing {sorted(set(CONSTANTS) - set(constants))}")
    for name, value in constants.items():
        assert value, f"{name} parsed as empty"


def test_the_starting_class_is_a_real_class_and_not_the_shared_line(
        constants, class_lines) -> None:
    """The whole of what issue #806 turned out to be.

    `UCataclysmClassStats::DefaultClassName` is the line a class inherits a stat
    from when it names none of its own. It is not a class anybody should play:
    it states no armour, no resistance, no block, no flat damage reduction and
    no leech. `StartingClassName` is what a character with no choice made sits
    on, and the two were the same string until 2026-08-24.
    """
    starting, shared = constants["starting_class"], constants["shared_line"]
    assert starting != shared, (
        f"the class a character starts on is {starting!r}, which is the same "
        f"shared line every class inherits stats from. That line has no "
        f"defensive layer of its own, and a character on it loses the fight "
        f"below. Point UCataclysmPlayerClassStats::StartingClassName at one of "
        f"{sorted(set(class_lines) - {shared})}. Issue #806.")
    assert starting in class_lines, (
        f"the class a character starts on is {starting!r} and "
        f"game/Data/ClassStats.csv has no such row, so every stat would fall "
        f"back to the shared line and the character would have no defences.")


def test_the_starting_character_survives_a_pack_of_imps(
        constants, class_lines, slots, weapon) -> None:
    """The regression guard. This is the fight that opened issue #806."""
    class_name = constants["starting_class"]
    level = int(constants["default_level"])
    result = fight(class_name, level=level, lines=class_lines,
                   constants=constants, slots=slots, weapon=weapon)

    assert result["won"] is True, (
        f"a {class_name} at level {level} -- which is what pressing Play gives "
        f"you -- loses to the pack of {constants['imp_count']} Imps the sandbox "
        f"places. It "
        f"{'ran out of time' if result['won'] is None else 'died'} after "
        f"{result['seconds']}s with {result.get('imps_left')} Imps still up, "
        f"starting from {result['max_health']:.0f} health.\n"
        f"Either a class stat was re-tuned, or the Imp constants in "
        f"CataclysmGameMode.h moved, or the starting weapon changed. Issue "
        f"#806 is the fight this describes.")


def test_the_shared_line_still_loses_that_same_fight(
        constants, class_lines, slots, weapon) -> None:
    """Proves the measurement can tell the two apart.

    Without this, a bug making every fight winnable would leave the test above
    passing and saying nothing. The shared line losing is the reason the
    starting class was changed, so it is the control.
    """
    shared = constants["shared_line"]
    level = int(constants["default_level"])
    result = fight(shared, level=level, lines=class_lines,
                   constants=constants, slots=slots, weapon=weapon)

    assert result["won"] is False, (
        f"the shared {shared!r} line now WINS the pack fight at level {level}, "
        f"which it did not on 2026-08-24. That is not a failure -- it may be "
        f"good news -- but this file exists because it lost, and the test "
        f"above can no longer tell a survivable starting class from an "
        f"unsurvivable one. Re-read issue #806 and decide whether this control "
        f"is still worth keeping.")


def test_no_class_survives_that_fight_at_level_one(
        constants, class_lines, slots, weapon) -> None:
    """Recorded because it is the part of #806 that is NOT fixed.

    Levelling does not exist -- `Cataclysm.PlayerLevel` is a console variable
    read at possession -- so nothing in the game can start a character at 1 and
    grow them. If that changes, this test fails and says so, which is the point.
    """
    level_one_losers = [
        name for name in sorted(class_lines)
        if fight(name, level=1, lines=class_lines, constants=constants,
                 slots=slots, weapon=weapon)["won"] is not True
    ]
    assert level_one_losers == sorted(class_lines), (
        f"these classes now survive the pack at level 1: "
        f"{sorted(set(class_lines) - set(level_one_losers))}. On 2026-08-24 "
        f"none of them did. Issue #806 records that as unfixed, so if it is "
        f"fixed now, update the issue and this test.")
