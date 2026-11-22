"""Every gem names a stat the game reads, in the bucket its text and the rulings give it.

WHY THIS EXISTS. Issue #46, slice G1. Until this slice no gem mapped to a stat: the Gems sheet's Effect column is prose
("20% chance to apply poison", "Increases HP by 5%"), and no game code read a gem at all. The coordinating session
ruled on 2026-11-21, from the design document, each gem's own text and the owner's precedent in #2815:

  - an ailment gem is a FLAT chance on the ailment's chance stat, as the ailment affixes are;
  - every other gem is an INCREASE, by its text: each says "Increases ...", Of Hatred's crit multiplier included;
  - the sheet's fraction is percentage points in the stat pipeline, so 0.2 is 20 (#2803 ruled the same for Of The
    Scholar);
  - the four gems for stats no class has a base for (block chance, evasion, cooldown reduction, magic find) stay as
    they are, and their tooltip says plainly when one does nothing for the character (#2815's "Keep it, just explain
    it"). Slice G2 writes that tooltip; this file names which four it must cover.

RULED below is that table. The Gems sheet gains Stat and ValueKind columns in this slice's workbook stage, and a check
that the generated `game/Data/Gems.csv` matches RULED is added with them.

WHAT IS CHECKED:

  1. RULED holds every gem in `game/Data/Gems.csv` and no other;
  2. every stat RULED names is one the game reads: in `UCataclysmPlayerClassStats::StatToAttribute` or
     `StatsWithNoAttribute`, read out of the C++ by `tools/generate_datatables.py`;
  3. each ailment gem is flat on the chance stat `UCataclysmAilments` keeps for the ailment its affix names (the
     `Gem` column of `game/Data/Affixes.csv`), and no other gem is flat;
  4. every other gem's text says "Increases", and RULED makes it an increase;
  5. the gems whose stat nothing gives a base to are exactly the four the ruling names;
  6. `UCataclysmGems` multiplies a sheet value by 100 and gives its modifier source Gem.

`Cataclysm.GemStats.` (`game/Source/Cataclysm/Tests/CataclysmGemStatsTests.cpp`) tests what the modifier does.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
AILMENTS = SOURCE / "AbilitySystem" / "CataclysmAilments.cpp"
GEMS_H = SOURCE / "Items" / "CataclysmGems.h"
GEMS_CPP = SOURCE / "Items" / "CataclysmGems.cpp"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: Each gem's stat and bucket, as ruled on 2026-11-21. Keyed by the row name in `game/Data/Gems.csv`.
RULED: dict[str, tuple[str, str]] = {
    "Gem_Of_The_Abyss": ("void_splinter_chance", "flat"),
    "Gem_Of_The_Viper": ("poison_chance", "flat"),
    "Gem_Of_Rending": ("bleed_chance", "flat"),
    "Gem_Of_Madness": ("madness_chance", "flat"),
    "Gem_Of_Rot": ("disease_chance", "flat"),
    "Gem_Of_Ticking": ("dot_frequency", "increased"),
    "Gem_Of_Striking": ("attack_speed", "increased"),
    "Gem_Of_Hatred": ("crit_multiplier", "increased"),
    "Gem_Of_Ferocity": ("crit_chance", "increased"),
    "Gem_Of_Recovery": ("health_regen", "increased"),
    "Gem_Of_Shielding": ("energy_shield_regen", "increased"),
    "Gem_Of_The_Titan": ("armor", "increased"),
    "Gem_Of_The_Bulwark": ("block_chance", "increased"),
    "Gem_Of_The_Wraith": ("evasion", "increased"),
    "Gem_Of_The_Mage": ("max_energy_shield", "increased"),
    "Gem_Of_Life": ("max_health", "increased"),
    "Gem_Of_Maiming": ("cripple_chance", "flat"),
    "Gem_Of_Withering": ("weaken_chance", "flat"),
    "Gem_Of_Shredding": ("shred_chance", "flat"),
    "Gem_Of_Rejuvination": ("mana_regen", "increased"),
    "Gem_Of_Quickness": ("movement_speed", "increased"),
    "Gem_Of_Area": ("area_of_effect", "increased"),
    "Gem_Of_Urgency": ("cooldown_reduction", "increased"),
    "Gem_Of_Mana": ("max_mana", "increased"),
    "Gem_Of_The_Goblin": ("magic_find", "increased"),
    "Gem_Of_Wasting": ("necrosis_chance", "flat"),
    "Gem_Of_Embers": ("burn_chance", "flat"),
    "Gem_Of_The_Scholar": ("skill_experience_gain", "increased"),
}

#: The four gems whose increase has nothing to multiply on any class until an item adds the stat (ruled 2026-11-21).
NOTHING_TO_INCREASE = {"Gem_Of_The_Bulwark", "Gem_Of_The_Wraith", "Gem_Of_Urgency", "Gem_Of_The_Goblin"}

#: Stats with no row in `game/Data/ClassStats.csv` whose base the game supplies in code, so a gem raising them acts:
#: a held weapon's swing rate and its 5% critical strike chance (`UCataclysmEquipmentComponent::StatBasesFromWeapons`),
#: and skill experience gain, which `ACataclysmPlayerState::SkillExperienceGainFactor` asks with a base of 100.
BASE_FROM_CODE = {
    "attack_speed": "the held weapon's swing rate",
    "crit_chance": "the held weapon's 5%",
    "skill_experience_gain": "SkillExperienceGainFactor's base of 100",
}


def read_csv(name: str) -> list[dict[str, str]]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def gems() -> dict[str, dict[str, str]]:
    return {row["Name"]: row for row in read_csv("Gems.csv")}


def ailment_stats() -> dict[str, str]:
    """`UCataclysmAilments`' table: each ailment's name and the stat its chance is kept under."""
    found = dict(re.findall(r'\{TEXT\("([A-Za-z ]+)"\),\s*TEXT\("([a-z_]+_chance)"\)', AILMENTS.read_text(
        encoding="utf-8")))
    assert len(found) >= 10, f"read {len(found)} ailments out of {AILMENTS.name}; the table's shape changed"
    return found


def test_the_generated_gems_carry_the_ruled_stat_and_bucket() -> None:
    # THE GEMS SHEET'S STAT AND VALUE KIND COLUMNS, written at the gems window's workbook stage, are RULED.
    with (DATA / "Gems.csv").open(encoding="utf-8", newline="") as handle:
        rows = {row["Name"]: (row["Stat"], row["ValueKind"]) for row in csv.DictReader(handle)}
    wrong = {name: (rows.get(name), ruled) for name, ruled in RULED.items() if rows.get(name) != ruled}
    assert not wrong, f"Gems.csv's Stat and ValueKind differ from the ruling of 2026-11-21: {wrong}"


def test_the_ruled_table_holds_every_gem_and_no_other() -> None:
    assert sorted(RULED) == sorted(gems()), (
        f"Gems.csv and the ruled table differ. Only in the sheet: {sorted(set(gems()) - set(RULED))}; only in the "
        f"table: {sorted(set(RULED) - set(gems()))}. A new gem needs a stat and a bucket ruled for it")


@pytest.mark.parametrize("gem", sorted(RULED))
def test_every_ruled_stat_is_one_the_game_reads(gem: str) -> None:
    stat, kind = RULED[gem]
    assert kind in ("flat", "increased"), f"{gem}: a gem is flat or increased, never {kind!r}"
    assert stat in gen.stats_with_an_attribute() | gen.stats_with_no_attribute(), (
        f"{gem} names {stat!r}, which is in neither UCataclysmPlayerClassStats::StatToAttribute nor "
        f"StatsWithNoAttribute, so its modifier would be dropped before it reached the character")


def test_each_ailment_gem_is_flat_on_its_affixs_ailment_stat_and_no_other_gem_is_flat() -> None:
    names = {row["GemName"]: name for name, row in gems().items()}
    stats = ailment_stats()
    ailment_gems = {}
    for affix in read_csv("Affixes.csv"):
        if affix["AffixKind"] == "Ailment" and affix["Gem"]:
            assert affix["Gem"] in names, f"{affix['Name']} names the gem {affix['Gem']!r}, which Gems.csv lacks"
            assert affix["Ailment"] in stats, f"{affix['Name']} names {affix['Ailment']!r}, no ailment the game knows"
            ailment_gems[names[affix["Gem"]]] = stats[affix["Ailment"]]
    assert len(ailment_gems) == 10, f"expected the 10 ailment gems the affix sheet names, read {sorted(ailment_gems)}"
    for gem, stat in ailment_gems.items():
        assert RULED[gem] == (stat, "flat"), (
            f"{gem} applies the same ailment as its affix, so it is flat on {stat!r}; the table says {RULED[gem]}")
    flat_others = sorted(gem for gem, (_, kind) in RULED.items() if kind == "flat" and gem not in ailment_gems)
    assert not flat_others, f"only ailment gems are flat, and these are not ailment gems: {flat_others}"


@pytest.mark.parametrize("gem", sorted(RULED))
def test_a_gems_text_says_what_its_bucket_is(gem: str) -> None:
    effect = gems()[gem]["Effect"]
    if RULED[gem][1] == "flat":
        assert "chance to apply" in effect, f"{gem} is flat, as an ailment chance, but its text reads {effect!r}"
    else:
        assert effect.startswith("Increases "), (
            f"{gem} is an increase because its text says so, but its text reads {effect!r}")


def test_the_gems_with_nothing_to_increase_are_the_four_the_ruling_names() -> None:
    based = {row["Stat"] for row in read_csv("ClassStats.csv") if float(row["Base"] or 0) > 0}
    unbased = {gem for gem, (stat, kind) in RULED.items()
               if kind == "increased" and stat not in based and stat not in BASE_FROM_CODE}
    assert unbased == NOTHING_TO_INCREASE, (
        f"the gems whose stat no class line and no code gives a base to are {sorted(unbased)}, and the ruling of "
        f"2026-11-21 names {sorted(NOTHING_TO_INCREASE)}. Their tooltip must say when they do nothing (slice G2)")


def test_the_gem_library_states_percentage_points_from_source_gem() -> None:
    header = GEMS_H.read_text(encoding="utf-8")
    body = re.sub(r"//.*", "", GEMS_CPP.read_text(encoding="utf-8"))
    assert re.search(r"static constexpr float PointsPerSheetUnit = 100\.0f;", header), (
        "UCataclysmGems::PointsPerSheetUnit is not 100: the sheet's 0.2 must be 20 percentage points")
    assert "Modifier.Source = ECataclysmModifierSource::Gem;" in body, "a gem's modifier must have source Gem"
    assert "Modifier.Value = SheetValue * PointsPerSheetUnit;" in body, (
        "a gem's modifier must be its sheet value times PointsPerSheetUnit")
