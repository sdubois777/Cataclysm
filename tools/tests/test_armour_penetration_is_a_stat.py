"""Armour penetration exists as a stat in all three places. Issue #520.

WHY THIS EXISTS. `FCataclysmIncomingHit::ArmorPenetration` in
`game/Source/Cataclysm/AbilitySystem/CataclysmDamageCalculation.h` was applied
correctly by the damage calculation and was never set, because **nothing in the
project held an armour penetration value**. Three enchantments in
`game/Data/EnchantmentsPositive.csv` grant it and none of them could do anything.

IT IS A SECOND STAT, NOT THE ONE THAT WAS ALREADY THERE.
`UCataclysmCombatAttributeSet` has had a `Penetration` attribute since issue
#486, and that is RESISTANCE penetration. The two are applied at different steps
of the damage calculation -- armour at step 3, resistance at step 4 -- and the
enchantment tables have always listed them separately.

WHAT IT COSTS TO GET WRONG. Enemy armour reached no arithmetic at all until issue
#481 and is now the largest single mitigation layer on the most armoured
creatures: the Abyssal Warden's 5,954 armour removes 48.19% of a hit at
difficulty tier 8.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++ at all, so
the attribute set is read out of the source as text. That catches the two ways
this goes wrong: the stat existing in the model and not the engine, and the
engine reading the resistance penetration where the armour one was meant.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ABILITY_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
COMBAT_HEADER = ABILITY_DIR / "CataclysmCombatAttributeSet.h"
COMBAT_SOURCE = ABILITY_DIR / "CataclysmCombatAttributeSet.cpp"
VITAL_SOURCE = ABILITY_DIR / "CataclysmVitalAttributeSet.cpp"
ENCHANTMENTS = REPO_ROOT / "game" / "Data" / "EnchantmentsPositive.csv"

#: The character sheet stat name, matching `sim/cataclysm_sim/character.py`.
STAT = "armor_penetration"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


# --------------------------------------------------------------------------
# The model
# --------------------------------------------------------------------------

def test_the_character_sheet_has_it() -> None:
    from cataclysm_sim import character as ch

    assert STAT in ch.ALL_STATS, (
        f"{STAT!r} is not on the character sheet in "
        f"sim/cataclysm_sim/character.py. Three enchantments grant it and a "
        f"piercing weapon adds to it, so without a line on the sheet there is "
        f"nothing for any of them to modify.")
    assert STAT in ch.STAT_GROUPS["Offense"]
    assert STAT in ch.DEFAULT_STAT_LINE


def test_it_starts_at_zero_rather_than_one_hundred() -> None:
    """An ADDED percentage, not a percentage OF something. The rule
    `tools/tests/test_stat_baselines_match_the_attribute_set.py` holds: a stat
    that is a percentage of something starts at 100 because 100 means unchanged,
    and one that is added starts at 0. Ignoring 100% of a target's armour is the
    maximum this stat can mean, so 100 is not "unchanged" here."""
    from cataclysm_sim import character as ch

    line = ch.DEFAULT_STAT_LINE[STAT]
    assert line.base == 0.0
    assert ch.Character(ch.GENERIC, level=100).stat(STAT) == 0.0


def test_it_is_a_different_stat_from_resistance_penetration() -> None:
    from cataclysm_sim import character as ch

    assert "penetration" in ch.ALL_STATS
    assert STAT != "penetration"
    assert len({"penetration", STAT} & set(ch.ALL_STATS)) == 2, (
        "the two penetrations have collapsed into one stat. They cut into "
        "different layers -- armour at step 3 and resistance at step 4 -- and "
        "the enchantment tables list them separately.")


# --------------------------------------------------------------------------
# The engine
# --------------------------------------------------------------------------

def test_the_attribute_set_has_it() -> None:
    header = source(COMBAT_HEADER)
    assert "FGameplayAttributeData ArmorPenetration;" in header, (
        "UCataclysmCombatAttributeSet has no ArmorPenetration attribute, so "
        "FCataclysmIncomingHit::ArmorPenetration has nothing to be set from and "
        "the damage calculation applies a figure that is always zero.")
    assert "ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, ArmorPenetration)" \
        in header


def test_it_is_initialised_replicated_and_listed() -> None:
    """Three things every attribute in this set needs, and each fails silently
    on its own: an uninitialised attribute reads as whatever the memory held, an
    unreplicated one is right on the server and wrong on every client, and one
    missing from `GetAllAttributes` is skipped by anything that walks the set."""
    text = source(COMBAT_SOURCE)
    assert "InitArmorPenetration(0.0f);" in text
    assert "CATACLYSM_REPLICATE(UCataclysmCombatAttributeSet, ArmorPenetration);" \
        in text
    assert "GetArmorPenetrationAttribute()" in text
    assert "CATACLYSM_ON_REP(UCataclysmCombatAttributeSet, ArmorPenetration)" in text


def test_a_hit_reads_it_off_the_attacker() -> None:
    """Penetration of either kind belongs to whoever is swinging rather than to
    any one blow, so both are read off the attacker at the moment the blow lands.
    `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` is the only place in
    the running game that resolves an incoming hit."""
    text = source(VITAL_SOURCE)
    assert re.search(r"Hit\.ArmorPenetration\s*=\s*Offence->GetArmorPenetration\(\)",
                     text), (
        "PostGameplayEffectExecute in CataclysmVitalAttributeSet.cpp does not "
        "read the attacker's armour penetration onto the hit. Without that line "
        "the attribute exists and reaches nothing, which is the state issue "
        "#520 describes with the attribute missing entirely.")

    assert re.search(r"Hit\.ResistancePenetration\s*=\s*Offence->GetPenetration\(\)",
                     text), (
        "the resistance penetration is no longer read onto the hit either. The "
        "two are separate stats and both have to arrive.")


# --------------------------------------------------------------------------
# What the stat is for
# --------------------------------------------------------------------------

def test_the_enchantments_that_grant_it_still_exist() -> None:
    """The reason the stat exists. If these are ever removed from the design, the
    stat is left with only the piercing weapon sub-type as a source and this
    file should say so rather than quietly checking a stat nothing grants."""
    if not ENCHANTMENTS.is_file():
        pytest.skip("game/Data/EnchantmentsPositive.csv is not present")

    text = ENCHANTMENTS.read_text(encoding="utf-8-sig").lower()
    granting = [line for line in text.splitlines() if "enemy armor" in line]
    assert len(granting) >= 3, (
        f"only {len(granting)} enchantment(s) in "
        f"game/Data/EnchantmentsPositive.csv mention ignoring enemy armor. "
        f"There were three when the stat was added for issue #520: skills "
        f"ignoring 10-25%, critical hits ignoring 20-40%, and a first hit "
        f"ignoring all of it.")


def test_the_model_applies_it_to_armour_and_not_to_resistance() -> None:
    """The arithmetic the engine mirrors. Written against the model because that
    is where the order is defined and where it can actually be run."""
    from cataclysm_sim import damage as dm

    defender = dm.Defender(health=10_000.0, armor=800.0, tier=1,
                           resistances={"Demonic": 50.0})

    def landed(**kwargs) -> float:
        return dm.resolve(dm.Attacker(damage=1000.0, **kwargs), defender,
                          force_evade=False, force_block=False).dealt_to_health

    # 800 armour at tier 1 removes exactly half, and 50% resistance halves what
    # is left, so a quarter of the hit lands.
    assert landed() == pytest.approx(250.0)

    # Ignoring all the armour doubles that, and does nothing to the resistance.
    assert landed(armor_penetration=100.0) == pytest.approx(500.0)

    # And the resistance penetration does nothing to the armour.
    assert landed(penetration=50.0) == pytest.approx(500.0)
