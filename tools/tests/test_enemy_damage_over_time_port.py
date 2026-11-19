"""An enemy-applied damage over time is sized from what applies it: the game's C++ against the simulation's copy.

ISSUE #2749, THE ENEMY BURN SLICE. The project owner died on 2026-11-19 on floor 2 of a 5-floor Outpost dungeon at level
2, most of it to Hellfire Aura's burn, which was the status table's flat 25 a tick at every tier, ring and floor. The
coordinating session ruled the same day:

- a floor creature's Hellfire Aura burns for 9% of its carrier's weapon damage a tick
  (`UCataclysmEnemyModifiers::HellfireAuraBurnShareOfWeaponDamage`, `enemy_stats.HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE`);
- a Corrupted Stalker's kit damage over time is the table figure times the damage multiplier its hits carry, and the
  corrupted double, which carries no multiplier, keeps the player's own figure;
- the player's own burns stay the table's (the owner, 2026-08-24).

WHY PYTHON READS C++ HERE. Continuous integration compiles no C++. The Unreal tests in `Cataclysm.EnemyBurn.` hold the
figures; these hold the share equal to the simulation's, which the pack test's aura guard uses, and the shape of the
three places that size the damage, so a change that brings the flat figure back fails before a window does.
"""
from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
MODIFIERS_HEADER = SOURCE / "Character" / "CataclysmEnemyModifiers.h"
MODIFIERS = SOURCE / "Character" / "CataclysmEnemyModifiers.cpp"
SKILL_EFFECTS = SOURCE / "AbilitySystem" / "CataclysmSkillEffects.cpp"
STALKER = SOURCE / "Character" / "CataclysmCorruptedStalkerCharacter.cpp"


def without_comments(text: str) -> str:
    """The text with every `//` comment and `/* */` block taken out, by syntax and not by a line's first character."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def body_of(text: str, opening: str) -> str:
    """The braced body that follows `opening`, by counting braces."""
    start = text.index(opening)
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"braces never balanced after {opening!r}")


@pytest.fixture(scope="module")
def header() -> str:
    return MODIFIERS_HEADER.read_text(encoding="utf-8")


def test_the_auras_share_is_the_simulations(header):
    from cataclysm_sim import enemy_stats

    found = re.search(r"static\s+constexpr\s+float\s+HellfireAuraBurnShareOfWeaponDamage\s*=\s*([\d.]+)f?\s*;", header)
    assert found, "HellfireAuraBurnShareOfWeaponDamage is gone from CataclysmEnemyModifiers.h"
    assert float(found.group(1)) == pytest.approx(enemy_stats.HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE)
    assert enemy_stats.HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE == pytest.approx(0.09), "ruled 2026-11-19, issue #2749"
    assert enemy_stats.hellfire_aura_burn_per_tick(200.0) == pytest.approx(18.0)
    assert enemy_stats.hellfire_aura_burn_per_tick(-5.0) == 0.0


def test_the_auras_tick_is_its_carriers_weapon_damage_times_the_share_and_not_the_tables():
    source = without_comments(MODIFIERS.read_text(encoding="utf-8"))
    per_tick = body_of(source, "float UCataclysmEnemyModifiers::HellfireAuraBurnPerTick(")
    assert re.search(r"\*\s*HellfireAuraBurnShareOfWeaponDamage", per_tick), per_tick
    aura = body_of(source, "int32 UCataclysmEnemyModifiers::AuraStep(")
    assert re.search(r"HellfireAuraBurnPerTick\(\s*UCataclysmSkillEffects::WeaponDamageOf\(", aura), (
        "the aura no longer sizes its burn from its carrier's weapon damage")
    assert "ApplyBurn(" not in aura, (
        "the aura applies the table's burn again (ApplyBurn); its tick must be the carrier's share, issue #2749")


def test_a_stalkers_damage_over_time_takes_the_multiplier_its_hits_carry():
    effects = without_comments(SKILL_EFFECTS.read_text(encoding="utf-8"))
    applied = body_of(effects, "bool UCataclysmSkillEffects::ApplyDamageOverTime(")
    assert re.search(r"Cast<ACataclysmCorruptedStalkerCharacter>\(\s*Instigator\s*\)", applied), applied[:400]
    assert re.search(r"DamageOverTimeMultiplier\(\)", applied)
    assert re.search(r"!\s*bIsConvertedDamage", applied), "converted damage would take the multiplier twice"
    assert re.search(r"DamagePerTick\s*\*\s*KeptUnderTheCurse\s*\*\s*StalkersShare", applied)
    wearing = body_of(without_comments(STALKER.read_text(encoding="utf-8")),
                      "bool ACataclysmCorruptedStalkerCharacter::WearTheBuild(")
    assert re.search(r"WornDamageMultiplier\s*=\s*Effects::CorruptedStalkerDamageMultiplier\(", wearing), (
        "the Stalker no longer keeps the damage multiplier its build applied")
    # WITH THE FLOOR'S RING, since the ring slice. The ring's parameter defaults to an Outpost, so a call without it
    # compiles; the rebase of this slice onto the ring slice produced exactly that.
    assert re.search(r"WornDamageMultiplier\s*=\s*Effects::CorruptedStalkerDamageMultiplier\(\s*DifficultyTier\s*,"
                     r"\s*Ring\s*\)", wearing), (
        "the Stalker keeps a damage multiplier without its floor's ring, so its damage over time takes an Outpost's")
    assert re.search(r"GetNumericAttribute\(\s*AttackDamage\s*\)\s*\*\s*WornDamageMultiplier", wearing), (
        "the Stalker's hits and its damage over time no longer take the same multiplier")


def test_a_share_of_the_targets_health_takes_no_stalkers_multiplier():
    """The coordinating session, 2026-11-19: "EXEMPT percentage-of-health effects from the Stalker's multiplier". Void
    Splinter is laid by `ApplyShareOfHealthOverTime`, which must not read the multiplier nor pass through
    `ApplyDamageOverTime`, where the multiplier is. THE FIRST ASSERTION SHOWS THE PARSE FOUND THE FUNCTION."""
    effects = without_comments(SKILL_EFFECTS.read_text(encoding="utf-8"))
    share = body_of(effects, "bool UCataclysmSkillEffects::ApplyShareOfHealthOverTime(")
    assert "SharePerTick" in share, share[:300]
    assert "DamageOverTimeMultiplier" not in share
    assert "ApplyDamageOverTime(" not in share
