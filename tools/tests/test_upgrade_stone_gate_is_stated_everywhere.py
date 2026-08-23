"""The upgrade stone drop gate, pinned across the model, the design document,
the engine and the shipped data.

WHY THIS EXISTS. Issue #863. The design document names three gates the difficulty
tier applies -- gear rarity, affix tier, and "The best upgrade stone that can
drop is capped by the current difficulty tier" -- and only the first two were
built. Every stone up to +10 dropped at every tier.

Issue #870 is what this file is really guarding against. That is the rarity gate
disagreeing with its own table by one rung at seven of the eight tiers, found by
reading rather than by any test, because nothing compared the two. The affix gate
has `test_affix_tier_gate_is_stated_everywhere.py`; the rarity gate has nothing;
this is the third gate's.

The rule is `min(10, difficulty tier + 2)`, and it is stated in four places:

    sim/cataclysm_sim/player_power.py   `reference_character`, which is the
                                        authority -- the whole power model is
                                        solved against the character it builds
    docs/Cataclysm_GDD_v2.md            section VI's difficulty tier table, in
                                        the column "What else that tier brings"
    game/Source/.../CataclysmDropRoll.h `UpgradeLevelsAboveDifficulty`, which is
                                        what the engine enforces
    game/Data/CraftingMaterials.csv     the ten stones the gate selects among

WHAT THIS DOES NOT CHECK, because a Python test cannot run the engine: that
`MaxUpgradeStoneOnADrop` applies the constant the way this file assumes, and that
the drop site passes the tier being played. Those are
`Cataclysm.Drop.TheBestUpgradeStoneIsCappedByTheDifficultyTier` and
`Cataclysm.Drop.AnUpgradeStoneOnTheFloorRespectsTheTier`.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
CRAFTING_CSV = REPO_ROOT / "game" / "Data" / "CraftingMaterials.csv"
DROP_ROLL_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
               / "CataclysmDropRoll.h")
ITEM_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
          / "CataclysmItem.h")
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import player_power
    return player_power


@pytest.fixture(scope="module")
def documented_levels() -> dict[int, int]:
    """The upgrade level each difficulty tier brings, out of section VI's table.

    Read from the same rows `test_affix_tier_gate_is_stated_everywhere.py`
    reads, which is the one table in the document that states all three gates
    side by side.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    body = GDD.read_text(encoding="utf-8")
    found: dict[int, int] = {}
    for line in body.splitlines():
        match = re.match(
            r"\|\s*(\d+)\s*\|\s*T\d+\s*\|\s*\w+ gear, \+(\d+)", line.strip())
        if match:
            found[int(match.group(1))] = int(match.group(2))
    assert found, (
        "no difficulty tier row in the design document states an upgrade "
        "level. The table's third column should read like "
        "'Everyday gear, +3 upgrade level'.")
    return found


@pytest.fixture(scope="module")
def stones() -> dict[int, dict[str, str]]:
    """The upgrade stones from the shipped table, keyed by upgrade level."""
    if not CRAFTING_CSV.is_file():
        pytest.skip(f"{CRAFTING_CSV.name} is not present")
    with CRAFTING_CSV.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    assert "UpgradeLevel" in (rows[0] if rows else {}), (
        f"{CRAFTING_CSV.name} has no UpgradeLevel column. It is derived from "
        "the material's name by tools/generate_datatables.py; regenerate with "
        "python tools/generate_datatables.py.")
    return {int(row["UpgradeLevel"]): row
            for row in rows if int(row["UpgradeLevel"] or 0) > 0}


def cpp_constant(path: pathlib.Path, name: str) -> int:
    body = path.read_text(encoding="utf-8")
    match = re.search(rf"constexpr int32 {name} = (\d+);", body)
    assert match, f"{path.name} no longer declares {name}"
    return int(match.group(1))


class TestTheModelIsTheAuthority:
    def test_the_reference_character_follows_tier_plus_two_capped_at_ten(
            self, model):
        """Not a restatement of the code: it walks the character the whole power
        model is solved against, so this is the rule as the balance depends on
        it."""
        for tier in range(1, 9):
            character = model.reference_character(tier)
            assert character.gear[0].upgrade == min(model.MAX_UPGRADE, tier + 2)

    def test_it_reaches_the_maximum_exactly_at_the_deepest_tier(self, model):
        """The cap and the ladder have to end together. If tier 8 fell short of
        MAX_UPGRADE the top stone would never drop anywhere; if it passed it,
        tiers below 8 would already be at the maximum."""
        assert model.reference_character(8).gear[0].upgrade == model.MAX_UPGRADE
        assert model.reference_character(7).gear[0].upgrade < model.MAX_UPGRADE


class TestTheDocumentSaysTheSame:
    def test_the_table_covers_every_difficulty_tier(self, documented_levels):
        assert sorted(documented_levels) == list(range(1, 9))

    def test_every_row_matches_the_model(self, documented_levels, model):
        for tier, level in sorted(documented_levels.items()):
            expected = model.reference_character(tier).gear[0].upgrade
            assert level == expected, (
                f"the design document says difficulty tier {tier} brings "
                f"+{level}; the reference character in player_power.py says "
                f"+{expected}")

    def test_it_states_the_rule_in_words_as_well_as_a_table(self):
        """A table can be read as a list of eight unrelated numbers. The
        sentence is what says they follow a rule."""
        body = GDD.read_text(encoding="utf-8")
        assert "Gear level is tier + 2 capped at +10" in body

    def test_it_names_the_gate_as_a_use_of_the_difficulty_tier(self):
        body = GDD.read_text(encoding="utf-8")
        assert ("The best upgrade stone that can drop is capped by the current "
                "difficulty tier") in body


class TestTheEngineEnforcesTheSameRule:
    def test_the_step_above_the_tier_matches_the_document(
            self, documented_levels):
        """The constant the engine adds to the tier, against the step the
        document's own table shows below its cap."""
        above = cpp_constant(DROP_ROLL_H, "UpgradeLevelsAboveDifficulty")
        for tier, level in sorted(documented_levels.items()):
            if level < max(documented_levels.values()):
                assert level - tier == above, (
                    f"the document has tier {tier} bringing +{level}, a step "
                    f"of {level - tier}; CataclysmDropRoll.h adds {above}")

    def test_the_maximum_gear_level_is_one_number_across_the_project(
            self, model, documented_levels):
        """Four copies of 10: the model, the engine, the generator and the top
        of the document's table."""
        engine = cpp_constant(ITEM_H, "MaxGearLevel")
        generator = re.search(r"MAX_UPGRADE_STONE_LEVEL = (\d+)",
                              GENERATOR.read_text(encoding="utf-8"))
        assert generator, "generate_datatables.py no longer states the maximum"
        assert engine == model.MAX_UPGRADE == int(generator.group(1)) == max(
            documented_levels.values())

    def test_the_gate_is_not_the_one_above_the_other_two_gates_use(self):
        """It is deliberately two where rarity and affixes are one. A single
        shared constant would be the easy thing to reach for and would be
        wrong, so this states that the difference is intended."""
        above = cpp_constant(DROP_ROLL_H, "UpgradeLevelsAboveDifficulty")
        assert above == 2
        assert cpp_constant(DROP_ROLL_H, "AffixTiersAboveDifficulty") == 1
        assert cpp_constant(DROP_ROLL_H, "RaritiesAboveDifficulty") == 1


class TestTheShippedDataCanSatisfyTheGate:
    def test_there_is_one_stone_for_every_level(self, stones, model):
        assert sorted(stones) == list(range(1, model.MAX_UPGRADE + 1)), (
            "the upgrade stone ladder has a hole or a duplicate. The level is "
            "derived from the material's name by tools/generate_datatables.py, "
            "so a rename in the Crafting sheet of "
            "docs/All_Things_Cataclysm.xlsx silently turns a stone into a "
            "level of 0.")

    def test_each_stone_is_named_for_the_level_it_carries(self, stones):
        for level, row in sorted(stones.items()):
            assert row["MaterialName"] == f"Upgrade Stone +{level}"

    def test_no_material_band_is_emptied_by_the_cap(self, stones):
        """The cap removes stones from a band and leaves the rest. A band that
        held nothing but stones would roll nothing at all at a low tier, which
        `RollMaterial` logs as a design fault rather than a data one."""
        with CRAFTING_CSV.open(newline="", encoding="utf-8-sig") as handle:
            rows = [row for row in csv.DictReader(handle)
                    if int(row["Tier"] or 0) > 0]
        for band in range(1, 6):
            in_band = [r for r in rows if int(r["Tier"]) == band]
            others = [r for r in in_band if not int(r["UpgradeLevel"] or 0)]
            assert others, (
                f"material tier {band} holds nothing but upgrade stones, so a "
                "low difficulty tier rolling that band would produce nothing.")

    def test_the_stones_are_spread_across_the_bands(self, stones):
        """The project owner chose this on 2026-08-23, issue #852: ten stones in
        one band would have made most of that band upgrade stones, and a
        material drop picks evenly among the materials sharing its band."""
        bands = {int(row["Tier"]) for row in stones.values()}
        assert bands == {1, 2, 3, 4, 5}

    def test_a_bands_stones_are_not_its_rarity(self, stones):
        """The two numbers are independent and the code keeps them in separate
        columns. If they were ever equal for every stone, a later reader would
        be right to think one could be derived from the other, and the drop cap
        would quietly become a band cap."""
        assert any(int(row["Tier"]) != level
                   for level, row in stones.items())
