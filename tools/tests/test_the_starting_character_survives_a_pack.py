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

A NEW CHARACTER STARTS AT LEVEL 1 SINCE ISSUE #2744, AND LEVELS AS IT KILLS. The
guard on tier 1's creature damage, at the bottom of this file, walks that
character through the dungeons the first surge really makes, levelling as it
goes (issue #2749). The guard on each ring's tier 1 figure, after it, walks
every Bulwark, Sanctuary and Pillar dungeon tier 1 campaigns clear, from the
level the character held when it entered. The sandbox's fixed pack in the first
tests is still fought at `DefaultLevel`, which stays the level of a character
nothing made or loaded.

WHAT IS MEASURED. Ten Imps against one character, both dealing damage until one
side is gone, at difficulty tier 1. Modelled as a fight rather than compared as
two rates, because incoming damage falls as Imps die -- a rate comparison says
the character loses when they may out-kill the pack.

WHAT IS DELIBERATELY PESSIMISTIC, so that passing means something:

* All ten Imps are in contact from the first second and the character stands
  still and trades. A real pack arrives over time.
* Every skill is treated as single-target. Several designed Greataxe skills are
  area effects that would hit the whole pack.
* No critical strikes, and no potions.

WHAT IS LEFT OUT IN THE CHARACTER'S FAVOUR: an Imp's 25% evasion and its armour.
A skill's mana cost and the basic attack's mana on hit are the slot's level 100
figures scaled to the character's level, as `UCataclysmSkillSlots::ManaCostAtLevel`
scales them (issue #2749; until then this charged the level 100 figures at every
level, which overcharged a level 1 character about thirteen times). The starting
weapon's sub-type bonus counts against the Imps' health, as the game counts it.

WHAT IT READS RATHER THAN RESTATES. Every constant is parsed out of the C++ that
owns it and every table value out of the generated CSV, so a change to either
moves this measurement instead of leaving it asserting a stale number. A constant
that cannot be found fails the test rather than falling back to a literal.

THE READERS ARE PLAIN FUNCTIONS, WITH FIXTURES AROUND THEM, so
`sim/analyse_opening.py` runs this file's own fight and walk and the measurement
and the guard cannot part company.
"""

from __future__ import annotations

import contextlib
import csv
import io
import pathlib
import re
import runpy

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
EXPERIENCE_MODEL = REPO_ROOT / "sim" / "analyse_experience_curve.py"

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
    "subtype_bonus": (
        SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.h",
        r"SubtypeBonus\s*=\s*([\d.]+)f"),
    "mana_cost_reference_level": (
        SOURCE / "AbilitySystem" / "CataclysmSkillSlots.h",
        r"ManaCostReferenceLevel\s*=\s*(\d+)"),
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

#: The sub-type whose bonus is "more damage vs. HP" (`docs/Cataclysm_GDD_v2.md`,
#: "Weapon Sub-Types"), which is all an Imp has.
HEALTH_SUBTYPE = "Slashing"


# ---------------------------------------------------------------------------
# The readers: plain functions, so `sim/analyse_opening.py` can call them.


def read_constants() -> dict[str, str]:
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


def read_class_lines() -> dict[str, dict[str, tuple[float, float]]]:
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


def read_slots() -> dict[str, dict[str, float]]:
    """slot -> its damage percent, cooldown and mana, from SkillSlots.csv.

    Every row of game/Data/WeaponSkills.csv states DamagePercent -1, and
    UCataclysmSkillTemplate::GetDamagePercent treats -1 as "take the slot's
    figure", so these are what every skill in the game is worth today. The mana
    figures are the LEVEL 100 ones; `slots_at_level` scales them.
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


def read_weapon(constants: dict[str, str]) -> dict:
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
            "speed": float(row["AttackSpeed"]),
            "subtype": row["SubType"]}


@pytest.fixture(scope="module")
def constants() -> dict[str, str]:
    return read_constants()


@pytest.fixture(scope="module")
def class_lines() -> dict[str, dict[str, tuple[float, float]]]:
    return read_class_lines()


@pytest.fixture(scope="module")
def slots() -> dict[str, dict[str, float]]:
    return read_slots()


@pytest.fixture(scope="module")
def weapon(constants) -> dict:
    return read_weapon(constants)


# ---------------------------------------------------------------------------
# The fight.


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


def slots_at_level(slots, lines, constants, level: int) -> dict[str, dict[str, float]]:
    """The slots with their mana cost and mana on hit at a character's level.

    `UCataclysmSkillSlots::ManaCostAtLevel`: the level 100 figure times the shared
    line's maximum mana at the level over the same at `ManaCostReferenceLevel`,
    for every class ("Costs ride the default mana progression", the design's
    "What a Skill Costs"). The game charged the level 1 figure at every level
    until issue #2749 made a skill's cost read the character's level.
    """
    shared = constants["shared_line"]
    reference = stat(lines, constants, shared, "max_mana", int(constants["mana_cost_reference_level"]))
    share = stat(lines, constants, shared, "max_mana", level) / reference
    return {name: dict(spec, mana=spec["mana"] * share, mana_on_hit=spec["mana_on_hit"] * share)
            for name, spec in slots.items()}


def weapon_against_health(weapon, constants) -> dict:
    """The weapon's damage against a creature's health: a Slashing weapon's sub-type
    bonus is `SubtypeBonus`% more damage against health, as
    `UCataclysmDamageCalculation` applies it, and an Imp has nothing else."""
    if weapon.get("subtype") != HEALTH_SUBTYPE:
        return weapon
    return dict(weapon, damage=weapon["damage"] * (1.0 + float(constants["subtype_bonus"]) / 100.0))


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


def share_of_a_tick_taken(lines, constants, class_name: str, level: int) -> float:
    """What a damage over time tick keeps after the class's flat damage reduction. Armour is left off: whether it meets
    a tick is what `Cataclysm.EnemyBurn.ATableBurnFromACreatureArrivesAtWhatTheDefendersStepsLeave` measures, and
    leaving it off overstates a tick by at most the 3.4% a level 2 Ravager's armour takes, so this errs harder."""
    flat = min(stat(lines, constants, class_name, "damage_reduction", level),
               float(constants["damage_reduction_cap"]))
    return 1.0 - flat / 100.0


def fight(class_name: str, level: int, *, lines, constants, slots, weapon,
          tier: int = 1, use_skills: bool = True, burn_per_second: float = 0.0,
          slot_factor: dict | None = None, dealt: dict | None = None) -> dict:
    """Ten Imps against one character. Returns who won and how long it took.

    Everything after `level` is keyword-only, because `lines`, `constants`,
    `slots` and `weapon` are four dictionaries and passing them positionally is
    a mistake nothing would catch. The mana costs are taken at `level` and the
    weapon's sub-type bonus is applied here, so every caller fights the same way.

    `burn_per_second` IS A BURN TICKING ONCE A SECOND FOR THE WHOLE FIGHT, before the character's defences: a Hellfire
    Aura the character stands in throughout (issue #2749's enemy burn slice). Nought by default, which is the fight
    every caller had before.

    FOR THE SKILL-LEVEL MEASUREMENT (issue #2764, `sim/analyse_opening.py` section 5), and for nothing this file
    asserts: `slot_factor` multiplies a rotation slot's blow by its skill level's factor, and `dealt`, when given,
    gathers the health each slot removed. Neither is passed by any test here, so the guard fights as it did.
    """
    def line(name: str) -> float:
        return stat(lines, constants, class_name, name, level)

    slots = slots_at_level(slots, lines, constants, level)
    weapon = weapon_against_health(weapon, constants)

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
    elapsed = since_swing = since_basic = since_tick = 0.0
    swing_gap = 1.0 / weapon["speed"]
    tick = burn_per_second * share_of_a_tick_taken(lines, constants, class_name, level)

    def strike(amount: float, slot: str = "Basic") -> None:
        nonlocal health
        for index, remaining in enumerate(imps):
            if remaining > 0.0:
                if dealt is not None:
                    dealt[slot] = dealt.get(slot, 0.0) + min(remaining, amount)
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
                    strike(weapon["damage"] * spec["damage"] * (slot_factor or {}).get(slot, 1.0), slot)
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

        if tick > 0.0:
            since_tick += STEP_SECONDS
            if since_tick >= 1.0:
                since_tick -= 1.0
                absorbed = min(shield, tick)
                shield -= absorbed
                health -= tick - absorbed

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


def test_the_mana_a_skill_costs_rides_the_level(constants, class_lines, slots) -> None:
    """ISSUE #2749. A level 1 character pays a small share of the level 100 cost, and the level 100 character pays the
    whole of it; free stays free. The share is the shared line's maximum mana at the level over the same at 100."""
    at_one = slots_at_level(slots, class_lines, constants, 1)
    at_hundred = slots_at_level(slots, class_lines, constants, int(constants["mana_cost_reference_level"]))
    for name, spec in slots.items():
        assert at_hundred[name]["mana"] == pytest.approx(spec["mana"]), name
        assert at_one[name]["mana"] <= spec["mana"] * 0.1, name
    assert at_one["Basic"]["mana"] == 0.0
    assert at_one["Heavy"]["mana"] == pytest.approx(15.0 * 50.0 / 644.0), "a 15 cost at level 1, on the 50 + 6 line"


def test_the_starting_weapon_is_slashing_and_its_bonus_counts(constants, weapon) -> None:
    """The coordinating session, 2026-11-19 (slice 2 of issue #2749): the starting weapon's Slashing sub-type counts
    in any time-to-kill sum. A greataxe that stopped being Slashing would make this file's fights wrong, so it says."""
    assert weapon["subtype"] == HEALTH_SUBTYPE
    assert weapon_against_health(weapon, constants)["damage"] == pytest.approx(weapon["damage"] * 1.10)


def test_the_starting_character_survives_a_pack_of_imps(
        constants, class_lines, slots, weapon) -> None:
    """The regression guard. This is the fight that opened issue #806."""
    class_name = constants["starting_class"]
    level = int(constants["default_level"])
    result = fight(class_name, level=level, lines=class_lines,
                   constants=constants, slots=slots, weapon=weapon)

    assert result["won"] is True, (
        f"a {class_name} at level {level} -- which is what pressing Play gives "
        f"a character nothing made or loaded -- loses to the pack of "
        f"{constants['imp_count']} Imps the sandbox places. It "
        f"{'ran out of time' if result['won'] is None else 'died'} after "
        f"{result['seconds']}s with {result.get('imps_left')} Imps still up, "
        f"starting from {result['max_health']:.0f} health.\n"
        f"Either a class stat was re-tuned, or the Imp constants in "
        f"CataclysmGameMode.h moved, or the starting weapon changed. Issue "
        f"#806 is the fight this describes.")


#: Where the shared line and the starting class still come apart.
#:
#: MOVED FROM 20 TO 10 ON 2026-09-03. The control below used to run at the
#: default level of 20, where the shared "Default" line lost and the starting
#: class won. Issue #1179 eased both affix ladders, which made every early
#: character stronger, and at level 20 the shared line now wins too -- so the
#: control stopped discriminating and started passing for the wrong reason.
#:
#: 10 WAS MEASURED RATHER THAN CHOSEN. At levels 1 and 5 both lines lost; at 15
#: and 20 both won; at 10 the shared line lost and the starting class won.
#:
#: MOVED FROM 10 TO 6 ON 2026-11-19, MEASURED AGAIN AS THIS SAYS TO. Issue #2749
#: made the fight charge a skill's mana at the character's level rather than the
#: level 100 figure, and count the Greataxe's Slashing 10% against the Imps'
#: health, so both lines win more: at 10 the shared line won with 47 health left.
#: Every level from 1 to 20 was fought. Levels 1 to 3 both lose, 8 to 20 both
#: win, and 4 to 7 come apart -- the shared line losing with 3 Imps up, the
#: starting class winning with 22 to 87 health left. 6 is that band's upper
#: middle. Re-measure it rather than nudging it if this ever fails again.
CONTROL_LEVEL = 6


def test_the_shared_line_still_loses_a_fight_the_starting_class_wins(
        constants, class_lines, slots, weapon) -> None:
    """Proves the measurement can tell the two apart.

    Without this, a bug making every fight winnable would leave the test above
    passing and saying nothing. The shared line losing is the reason the
    starting class was changed, so it is the control.

    BOTH HALVES ARE ASSERTED AT THE SAME LEVEL, which the earlier version did
    not do -- it only checked that the shared line lost, at a different level
    from the test it was controlling for. Checking the pair at one level is what
    actually demonstrates the fight discriminates, rather than demonstrating
    that some character loses somewhere.
    """
    shared, starting = constants["shared_line"], constants["starting_class"]

    lost = fight(shared, level=CONTROL_LEVEL, lines=class_lines,
                 constants=constants, slots=slots, weapon=weapon)
    won = fight(starting, level=CONTROL_LEVEL, lines=class_lines,
                constants=constants, slots=slots, weapon=weapon)

    assert lost["won"] is False, (
        f"the shared {shared!r} line now WINS the pack fight at level "
        f"{CONTROL_LEVEL}, so this control no longer tells a survivable "
        f"starting class from an unsurvivable one. That is not necessarily a "
        f"failure -- characters getting stronger is what moved it from level 20 "
        f"to level 10 on 2026-09-03 -- but the level has to be re-measured "
        f"rather than nudged. Try each level and find the one where these two "
        f"lines still come apart. Issue #806.")

    assert won["won"] is True, (
        f"the {starting!r} starting class now LOSES the pack fight at level "
        f"{CONTROL_LEVEL}, where it used to win. Either a class stat was "
        f"re-tuned or the fight got harder; this control needs both halves to "
        f"hold to mean anything.")


def test_no_class_survives_that_fight_at_level_one(
        constants, class_lines, slots, weapon) -> None:
    """No class survives the SANDBOX's fixed pack at level 1, and that is recorded rather than fixed.

    The sandbox places ten Imps of 87 health hitting for 9 (`CataclysmGameMode.h`), a pack no dungeon floor builds:
    a tier 1 floor builds its creatures from the enemy score, with damage scaled by the tier, and the guard at the
    bottom of this file walks a level 1 character through those. This said "Levelling does not exist" until issue
    #2749; a character starts at level 1 and levels as it kills since issue #2744. If a class ever survives the
    sandbox pack at level 1, this fails and says so.
    """
    level_one_losers = [
        name for name in sorted(class_lines)
        if fight(name, level=1, lines=class_lines, constants=constants,
                 slots=slots, weapon=weapon)["won"] is not True
    ]
    assert level_one_losers == sorted(class_lines), (
        f"these classes now survive the sandbox pack at level 1: "
        f"{sorted(set(class_lines) - set(level_one_losers))}. On 2026-08-24 "
        f"none of them did. Issue #806 records that, so if it changed, update "
        f"the issue and this test.")


# ---------------------------------------------------------------------------
# THE GUARD ON TIER 1'S CREATURE DAMAGE, since issue #2749.
#
# The owner ruled on 2026-11-18 to "make the first levels much cheaper ... Then re-set tier 1 enemy damage so a
# character levelling at that pace survives". The coordinating session set the target on 2026-11-19: the starting
# character, starting at level 1 and levelling as it kills half of each floor's creatures, beats a full pack of 10
# Common Imps, as the game builds them, on every floor of every Outpost ordinary dungeon the first surge makes, with at
# least 10% of its health left on the last floor. Until then the target was a level 20 character (`DefaultLevel`) on a
# 12-floor Basic dungeon with no sub-type (issue #1569), which a new character never is and which a surge never makes.

#: The share of each floor's creatures the character kills, for the experience it banks. A judgement (issue #925:
#: nobody has measured what share a player kills); the opening's cost was fitted at the same share.
KILL_SHARE = 0.5

#: The least share of its health the character ends the last floor with.
LEAST_HEALTH_LEFT = 0.10

#: The simulation's seeds whose first surge's dungeons are walked: every one of them, 3,057 dungeons of 3 to 16 floors,
#: every sub-type a surge rolls and modifier scores from 5 to 50. The damage scale was fitted on the same seeds.
FIRST_SURGE_SEEDS = range(300)

#: The damage scale tier 1 had until issue #2749, which the control below must lose with.
FORMER_FIRST_TIER_SCALE = 0.20


def experience_model() -> dict:
    """`sim/analyse_experience_curve.py`'s globals, its printed report thrown away. Its `whole_level_cost` is the
    game's curve: `tools/tests/test_experience_curve_port.py` holds the two equal at every level."""
    with contextlib.redirect_stdout(io.StringIO()):
        return runpy.run_path(str(EXPERIENCE_MODEL))


def first_surge_dungeons(seeds=FIRST_SURGE_SEEDS) -> list:
    """The Outpost ordinary dungeons the simulation's first surge makes, for each seed: the opening wave a new
    character walks into. `Simulation.trigger_surge` is what the engine calls on day 0."""
    from cataclysm_sim.config import CityTier, DungeonType, TuningConfig
    from cataclysm_sim.engine import Simulation

    out = []
    for seed in seeds:
        sim = Simulation(TuningConfig(), seed)
        sim.trigger_surge()
        out += [d for d in sim.dungeons.values()
                if d.dtype is DungeonType.BASIC and d.city_tier is CityTier.OUTPOST]
    return out


def floor_experience(model: dict, subtype: str, floors: int, floor: int, modifier_score: float,
                     dungeon_type: str = "Basic", tier: int = 1) -> float:
    """What killing every creature on one floor pays, at tier 1 unless told otherwise: each rarity's design score,
    which IS its experience, at the floor's spawn weights and population, the dungeon's boss added on its last floor.
    The same arithmetic as `analyse_experience_curve.dungeon_experience`, on the dungeon's own type, sub-type and
    modifiers."""
    from cataclysm_sim import scoring

    scores = scoring.enemy_scores(tier, dungeon_type, subtype, floors, floor, modifier_score)
    paid = model["POPULATION"] * sum(max(0, scores[rarity]) * weight
                                     for rarity, weight in model["WEIGHTS"].items())
    if floor == floors:
        paid += max(0, scoring.final_boss_score(tier, dungeon_type, subtype, floors, modifier_score))
    return paid


def level_for(total: float, model: dict) -> int:
    """The level a total amount of experience reaches from level 1, on the game's curve."""
    level, paid = 1, 0
    while level < model["MAX_LEVEL"] and paid + model["whole_level_cost"](level + 1) <= total:
        paid += model["whole_level_cost"](level + 1)
        level += 1
    return level


def level_after(dungeon, model, kill_share=KILL_SHARE) -> int:
    """The level a new character ends a dungeon at from its experience alone, every floor's share banked: no fight,
    so the curve's target is checked apart from the damage scale's."""
    banked = sum(kill_share * floor_experience(model, dungeon.subtype, dungeon.floors, floor, dungeon.modifier_score)
                 for floor in range(1, dungeon.floors + 1))
    return level_for(banked, model)


def walk(dungeon, *, model, lines, constants, slots, weapon, kill_share=KILL_SHARE, first_tier_scale=None,
         dungeon_type="Basic", ring="Outpost", banked=0.0) -> dict:
    """A character through one dungeon, floor by floor: it fights the floor's pack at the level it held when the floor
    began, then banks `kill_share` of the floor's experience. A new character by default: an Outpost's Basic dungeon
    and nothing banked; a campaign's clear passes its own type, ring and the experience banked before it.
    `first_tier_scale` replaces tier 1's damage scale on that ring when given, for the controls. Returns whether every
    floor was won, the first floor lost, the share of health left on the last floor and the level at the end."""
    from cataclysm_sim import enemy_stats as es

    class_name = constants["starting_class"]
    left = 0.0
    for floor in range(1, dungeon.floors + 1):
        level = level_for(banked, model)
        imp = es.strength_on_floor("Common", 1, dungeon_type, dungeon.subtype, dungeon.floors, floor,
                                   dungeon.modifier_score, kind="Imp", ring=ring)
        damage = imp.damage_per_hit
        if first_tier_scale is not None:
            damage = damage / es.damage_scale(1, ring) * first_tier_scale
        result = fight(class_name, level=level, lines=lines, slots=slots, weapon=weapon,
                       constants=dict(constants, imp_health=str(imp.health), imp_damage=str(damage)))
        if result["won"] is not True:
            return {"won": False, "lost_on": floor, "left": 0.0, "level": level}
        left = result["health_left"] / result["max_health"]
        banked += kill_share * floor_experience(model, dungeon.subtype, dungeon.floors, floor,
                                                dungeon.modifier_score, dungeon_type)
    return {"won": True, "lost_on": None, "left": left, "level": level_for(banked, model)}


@pytest.fixture(scope="module")
def model() -> dict:
    return experience_model()


@pytest.fixture(scope="module")
def surge_dungeons() -> list:
    dungeons = first_surge_dungeons()
    assert len(dungeons) > 2000, f"only {len(dungeons)} first-surge Outpost dungeons; the set this was fitted on had 3,057"
    return dungeons


def test_a_new_character_beats_the_pack_on_every_floor_of_every_first_surge_dungeon(
        model, surge_dungeons, constants, class_lines, slots, weapon) -> None:
    """THE GUARD ON TIER 1'S DAMAGE SCALE ON AN OUTPOST (`UCataclysmEnemyScore::DamageScaleAtFirstTier`, 0.05 since
    issue #2749). The other rings' are guarded at the end of this file."""
    failed = []
    for dungeon in surge_dungeons:
        result = walk(dungeon, model=model, lines=class_lines, constants=constants, slots=slots, weapon=weapon)
        if not result["won"] or result["left"] < LEAST_HEALTH_LEFT:
            failed.append((dungeon.did, dungeon.subtype, dungeon.floors, dungeon.modifier_score,
                           result["lost_on"], round(result["left"], 3)))
    assert not failed, (
        f"{len(failed)} of {len(surge_dungeons)} first-surge Outpost dungeons are lost, or won with under "
        f"{LEAST_HEALTH_LEFT:.0%} of the character's health left on the last floor, by a new {constants['starting_class']} "
        f"levelling at {KILL_SHARE:.0%} of each floor's kills. (id, sub-type, floors, modifier score, floor lost, "
        f"health left), the first ten: {failed[:10]}. The tier 1 damage scale was fitted so none is. Issue #2749.")


def test_control_the_former_tier_one_scale_loses_those_dungeons(
        model, surge_dungeons, constants, class_lines, slots, weapon) -> None:
    """THE CONTROL: the walk tells a survivable scale from one that is not. At 0.20, tier 1's figure until issue #2749
    and fitted to a level 20 character, the same new character loses most of the same dungeons."""
    lost = sum(1 for dungeon in surge_dungeons
               if not walk(dungeon, model=model, lines=class_lines, constants=constants, slots=slots, weapon=weapon,
                           first_tier_scale=FORMER_FIRST_TIER_SCALE)["won"])
    assert lost > len(surge_dungeons) // 2, (
        f"at the former scale of {FORMER_FIRST_TIER_SCALE} the new character loses only {lost} of "
        f"{len(surge_dungeons)} dungeons, so this walk no longer tells the two scales apart")


def test_a_new_character_gains_at_least_three_levels_over_a_first_dungeon_of_seven_or_eight_floors(
        model, surge_dungeons) -> None:
    """ISSUE #2749, B4: "a new character gains at least three levels (level 1 to level 4 or higher) over a first
    dungeon of the typical Outpost depth, 7 or 8 floors" -- the coordinating session's reading of the owner's
    "several", a judgement -- at the kill share the opening's cost was fitted at. From the experience alone, so a
    changed damage scale cannot fail it; the guard above is the fight's."""
    typical = [dungeon for dungeon in surge_dungeons if dungeon.floors in (7, 8)]
    assert len(typical) > 500
    short = [(dungeon.did, dungeon.subtype, dungeon.floors, level)
             for dungeon in typical if (level := level_after(dungeon, model)) < 4]
    assert not short, f"{len(short)} of {len(typical)} end below level 4; the first ten: {short[:10]}"


# ---------------------------------------------------------------------------
# THE GUARD ON EACH RING'S TIER 1 DAMAGE, since issue #2749's ring slice.
#
# The project owner, 2026-11-19: "Rise by city ring" -- Outpost dungeons stay gentle, Bulwark and Sanctuary dungeons
# hit harder. The coordinating session ruled the figures the same day by the rule that chose the Outpost's 0.05: for
# each ring, the largest step of 0.01 at which every dungeon of that ring a tier 1 campaign clears, walked again from
# the level the character held when it entered, is won on every floor with at least `LEAST_HEALTH_LEFT` of its health
# left on the last. Fitted in `sim/analyse_opening.py` section 5 on 60 seeds of every policy; guarded here on fewer,
# for run time. A subset of a set that passes passes, so this guard can fail only when a figure, the fight, the
# experience model or the campaigns move.
#
# THE CAMPAIGNS RUN WITH DEMONIC FOLLOWING THE FRONT LINE, AS THE GAME'S DOES. The game's surge targets the exposed
# front line for every Cataclysm, ruled for Demonic on 2026-09-14 (issue #1085); the simulation's Demonic still ignores
# it, which would put level 1 characters into Bulwark dungeons the game never sends them to. Making the simulation
# follow the ruling is the next slice of issue #2749, after which `game_front_line` is no longer needed.

#: The seeds of each policy's tier 1 campaigns the guard walks. The fit used 0 to 59 and was checked on 60 to 119.
RING_GUARD_SEEDS = range(15)

#: The simulation's dungeon type -> the name `scoring` and `enemy_stats` take.
SCORING_TYPE = {"Basic": "Basic", "Quest": "Quest", "FallenCity": "Fallen City", "Cataclysm": "Cataclysm"}


@contextlib.contextmanager
def game_front_line():
    """The simulation's Demonic set to target only the exposed front line, as the game's does, while the block runs."""
    import dataclasses

    from cataclysm_sim import patterns

    shipped = patterns.PATTERNS["Demonic"]
    patterns.PATTERNS["Demonic"] = dataclasses.replace(shipped, ignores_frontier=False)
    try:
        yield
    finally:
        patterns.PATTERNS["Demonic"] = shipped


class Clear:
    """One dungeon a campaign cleared: what `walk` needs, the ring its depth was rolled on, the ring of the city it
    stood on when cleared (None for the boss dungeon, which stands on no city), and the experience the character had
    banked when it entered."""

    def __init__(self, policy, seed, dungeon, banked, standing_on=None):
        self.policy, self.seed = policy, seed
        self.dungeon_type = SCORING_TYPE[dungeon.dtype.value]
        self.ring = dungeon.city_tier.value
        self.standing_on = standing_on
        self.subtype, self.floors, self.modifier_score = dungeon.subtype, dungeon.floors, dungeon.modifier_score
        self.banked = banked


def campaign_clears(model, seeds=RING_GUARD_SEEDS, kill_share=KILL_SHARE, tier=1, banked_at_start=0.0) -> list:
    """Every dungeon every policy's campaign at `tier` clears for `seeds`, in the order cleared, the character starting
    with `banked_at_start` experience and banking `kill_share` of each floor's as it goes."""
    from cataclysm_sim import policies
    from cataclysm_sim.config import TuningConfig
    from cataclysm_sim.engine import Simulation

    class Logged(Simulation):
        def _finish_current(self):
            dungeon, before = self.current, self.cleared
            city = self.empire.cities.get(dungeon.city_id) if dungeon.city_id is not None else None
            super()._finish_current()
            if self.cleared > before:
                self.cleared_in_order.append((dungeon, city.tier.value if city is not None else None))

    out = []
    with game_front_line():
        for name, policy in policies.ALL.items():
            for seed in seeds:
                sim = Logged(TuningConfig(tier=tier), seed)
                sim.cleared_in_order = []
                sim.run(policy)
                banked = banked_at_start
                for dungeon, standing_on in sim.cleared_in_order:
                    clear = Clear(name, seed, dungeon, banked, standing_on)
                    out.append(clear)
                    banked += kill_share * sum(
                        floor_experience(model, clear.subtype, clear.floors, floor, clear.modifier_score,
                                         clear.dungeon_type, tier) for floor in range(1, clear.floors + 1))
                    clear.banked_after = banked
    return out


def walk_clear(clear, *, model, lines, constants, slots, weapon, first_tier_scale=None) -> dict:
    """A campaign's clear walked again from the experience banked when it was entered, on its own ring."""
    return walk(clear, model=model, lines=lines, constants=constants, slots=slots, weapon=weapon,
                first_tier_scale=first_tier_scale, dungeon_type=clear.dungeon_type, ring=clear.ring,
                banked=clear.banked)


@pytest.fixture(scope="module")
def clears(model) -> list:
    found = campaign_clears(model)
    by_ring = {ring: sum(1 for c in found if c.ring == ring) for ring in ("Outpost", "Bulwark", "Sanctuary", "Pillar")}
    # Measured 2026-11-19 on seeds 0 to 14: 3,884 Outpost, 493 Bulwark, 217 Sanctuary and 60 Pillar clears.
    assert by_ring["Bulwark"] > 200 and by_ring["Sanctuary"] > 100 and by_ring["Pillar"] > 20, by_ring
    return found


@pytest.mark.parametrize("ring", ["Bulwark", "Sanctuary", "Pillar"])
def test_every_dungeon_a_tier_one_campaign_clears_on_this_ring_is_won_with_health_left(
        ring, model, clears, constants, class_lines, slots, weapon) -> None:
    """THE GUARD ON EACH RING'S FIGURE (`UCataclysmEnemyScore::DamageScale(1, Ring)`, issue #2749's ring slice). The
    Outpost is guarded by the first-surge test above, and its later clears pass at 0.05 in the measurement."""
    from cataclysm_sim import enemy_stats as es

    failed = []
    for clear in (c for c in clears if c.ring == ring):
        result = walk_clear(clear, model=model, lines=class_lines, constants=constants, slots=slots, weapon=weapon)
        if not result["won"] or result["left"] < LEAST_HEALTH_LEFT:
            failed.append((clear.policy, clear.seed, clear.dungeon_type, clear.subtype, clear.floors,
                           level_for(clear.banked, model), result["lost_on"], round(result["left"], 3)))
    assert not failed, (
        f"{len(failed)} of the {ring} dungeons tier 1 campaigns clear are lost, or won with under "
        f"{LEAST_HEALTH_LEFT:.0%} health left, at the {ring}'s damage scale of {es.damage_scale(1, ring)}. (policy, "
        f"seed, type, sub-type, floors, level at entry, floor lost, health left), the first ten: {failed[:10]}")


def test_control_the_bulwarks_clears_are_lost_at_four_times_an_outposts_figure(
        model, clears, constants, class_lines, slots, weapon) -> None:
    """THE CONTROL: the walk tells a survivable ring figure from one that is not. At 0.20 -- tier 1's figure until issue
    #2749 -- the 60-seed measurement lost 176 of 2,039 Bulwark clears and left 123 more under 10% health; on this
    guard's 15 seeds, measured 2026-11-19, 52 of 493 fail, against the 24 this asks for."""
    bulwark = [c for c in clears if c.ring == "Bulwark"]
    failed = 0
    for clear in bulwark:
        result = walk_clear(clear, model=model, lines=class_lines, constants=constants, slots=slots, weapon=weapon,
                            first_tier_scale=FORMER_FIRST_TIER_SCALE)
        failed += not result["won"] or result["left"] < LEAST_HEALTH_LEFT
    assert failed > len(bulwark) // 20, (
        f"at {FORMER_FIRST_TIER_SCALE} only {failed} of {len(bulwark)} Bulwark clears fail, so this walk no longer "
        "tells a survivable ring figure from one that is not")


# ---------------------------------------------------------------------------
# THE GUARD ON HELLFIRE AURA'S BURN, since issue #2749's enemy burn slice.
#
# The project owner died on 2026-11-19 on floor 2 of a 5-floor Outpost dungeon at level 2, most of it to Hellfire Aura's
# burn: the status table's flat 25 a tick, which no tier, ring or floor scaled. The coordinating session ruled the same
# day that an enemy-applied damage over time is sized from what applies it, and set the share by the rule that chose tier
# 1's damage scale: a new character, levelling at half the kills, wins every floor of every first-surge Outpost dungeon
# with at least `LEAST_HEALTH_LEFT` of its health left while standing in a Hellfire Aura on every floor, carried by an
# Elite Abyssal Warden, the creature whose hit is the largest. `sim/analyse_enemy_burn_and_thorns.py` is the measurement.

#: The aura's carrier in the guard: the Elite of the kind whose hit is the largest, so the guard is the hardest case.
AURA_CARRIER = ("Elite", "Abyssal Warden")


def table_burn_per_tick() -> float:
    """The status table's burn, `DoT_Burn`'s flat figure a tick: what Hellfire Aura dealt until the enemy burn slice."""
    with (DATA / "StatusEffects.csv").open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["Name"] == "DoT_Burn":
                return float(row["FlatDamagePerTick"])
    raise AssertionError("no DoT_Burn row in game/Data/StatusEffects.csv")


def walk_in_an_aura(dungeon, *, model, lines, constants, slots, weapon, flat_burn=None,
                    kill_share=KILL_SHARE) -> dict:
    """`walk`, with a Hellfire Aura burning the character for the whole of every floor's fight: the carrier's hit times
    the aura's share, or `flat_burn` a tick when given, for the control."""
    from cataclysm_sim import enemy_stats as es

    class_name = constants["starting_class"]
    banked, left = 0.0, 0.0
    for floor in range(1, dungeon.floors + 1):
        level = level_for(banked, model)
        imp = es.strength_on_floor("Common", 1, "Basic", dungeon.subtype, dungeon.floors, floor,
                                   dungeon.modifier_score, kind="Imp")
        carrier = es.strength_on_floor(AURA_CARRIER[0], 1, "Basic", dungeon.subtype, dungeon.floors, floor,
                                       dungeon.modifier_score, kind=AURA_CARRIER[1])
        burn = flat_burn if flat_burn is not None else es.hellfire_aura_burn_per_tick(carrier.damage_per_hit)
        result = fight(class_name, level=level, lines=lines, slots=slots, weapon=weapon, burn_per_second=burn,
                       constants=dict(constants, imp_health=str(imp.health), imp_damage=str(imp.damage_per_hit)))
        if result["won"] is not True:
            return {"won": False, "lost_on": floor, "left": 0.0, "level": level}
        left = result["health_left"] / result["max_health"]
        banked += kill_share * floor_experience(model, dungeon.subtype, dungeon.floors, floor, dungeon.modifier_score)
    return {"won": True, "lost_on": None, "left": left, "level": level_for(banked, model)}


def test_a_new_character_survives_a_hellfire_aura_on_every_floor_of_every_first_surge_dungeon(
        model, surge_dungeons, constants, class_lines, slots, weapon) -> None:
    """THE GUARD ON THE AURA'S SHARE (`UCataclysmEnemyModifiers::HellfireAuraBurnShareOfWeaponDamage`, 0.09)."""
    from cataclysm_sim import enemy_stats as es

    failed = []
    for dungeon in surge_dungeons:
        result = walk_in_an_aura(dungeon, model=model, lines=class_lines, constants=constants, slots=slots,
                                 weapon=weapon)
        if not result["won"] or result["left"] < LEAST_HEALTH_LEFT:
            failed.append((dungeon.did, dungeon.subtype, dungeon.floors, dungeon.modifier_score, result["lost_on"],
                           round(result["left"], 3)))
    assert not failed, (
        f"{len(failed)} of {len(surge_dungeons)} first-surge Outpost dungeons are lost, or won with under "
        f"{LEAST_HEALTH_LEFT:.0%} health left, standing in an {' '.join(AURA_CARRIER)}'s Hellfire Aura at "
        f"{es.HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE} of its hit a tick. The first ten: {failed[:10]}")


def test_control_the_status_tables_flat_burn_kills_most_of_them(
        model, surge_dungeons, constants, class_lines, slots, weapon) -> None:
    """THE CONTROL: the walk tells a survivable aura from one that is not. At the table's flat 25 a tick, the aura's
    figure until the enemy burn slice, measured 2026-11-19: 41 of the 3,057 dungeons are won with 10% health left."""
    flat = table_burn_per_tick()
    assert flat == 25.0, flat
    passed = sum(1 for dungeon in surge_dungeons
                 if (result := walk_in_an_aura(dungeon, model=model, lines=class_lines, constants=constants,
                                               slots=slots, weapon=weapon, flat_burn=flat))["won"]
                 and result["left"] >= LEAST_HEALTH_LEFT)
    assert passed < len(surge_dungeons) // 2, (
        f"at the table's flat {flat} a tick {passed} of {len(surge_dungeons)} pass, so this walk no longer tells a "
        "survivable aura from one that is not")
