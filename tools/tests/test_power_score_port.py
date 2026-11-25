"""The Unreal Power Score constants must match the simulation's.

WHY THIS IS NOT A DRIFT TEST LIKE THE OTHERS. The affix pool, the class stat
lines and the attribute effects all live in the design workbook and are compared
sheet against model. The Power Score anchors do not: they come from
`sim/cataclysm_sim/scoring.py`, which is a verified port of a file in a separate
repository and must never be hand-edited. Putting them in the workbook would
create a third copy of numbers that already have an authoritative source
elsewhere.

So the Unreal side carries them as literals, and this reads those literals back
out of the C++ and compares them. Parsing source is cruder than reading a
generated table, but the alternative is no guard at all: nothing else would
notice if the two lists parted company.

WHAT IS COMPARED. The eight tier anchors and the five weights, because those are
the model's only inputs. SINCE THE OWNER'S RULING OF 2026-11-24 (issue #2853)
the weights are plain numbers written out on both sides -- 10 a level, 2 a gear
rarity step, an upgrade factor of 1, 5 a gem rarity step, 1 a resistance
percent -- so they are compared here directly. Until then four shares were the
inputs and the weights were derived from them on both sides, so this file
compared the shares and an Unreal automation test checked the derivation.

AND THE DESIGN DOCUMENT'S COPY. Section IV states the five weights, the shares
they give, what a piece scores and the reference character's score at each tier;
`TestTheDesignDocument` recomputes each from the model.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
          / "CataclysmPowerScore.cpp")
HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
          / "CataclysmPowerScore.h")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import player_power
    return player_power


@pytest.fixture(scope="module")
def anchors_in_unreal() -> list[int]:
    text = read(SOURCE)
    match = re.search(r"Anchors\s*=\s*\{([^}]*)\}", text)
    if not match:
        pytest.fail("could not find the tier anchor list in "
                    f"{SOURCE.name}; has it been renamed?")
    return [int(part) for part in re.findall(r"-?\d+", match.group(1))]


#: The header's name for each weight, against `player_power.WEIGHTS`'s key.
WEIGHT_NAMES = {
    "PointsPerLevel": "level",
    "PointsPerGearRarityStep": "gear",
    "UpgradeFactorPerGearLevel": "upgrade_factor",
    "PointsPerGemRarityStep": "gem",
    "PointsPerResistancePercent": "resistance",
}


@pytest.fixture(scope="module")
def weights_in_unreal() -> dict[str, float]:
    text = read(HEADER)
    found = {}
    for name in WEIGHT_NAMES:
        values = re.findall(rf"static constexpr float {name} = ([0-9.]+)f;", text)
        assert len(values) == 1, f"{name} is declared {len(values)} times in {HEADER.name}"
        found[name] = float(values[0])
    return found


class TestTheTierAnchors:
    def test_unreal_carries_one_anchor_per_tier_plus_an_unused_zero(
            self, anchors_in_unreal):
        assert len(anchors_in_unreal) == 9, anchors_in_unreal
        assert anchors_in_unreal[0] == 0, "entry 0 is unused and must be zero"

    def test_every_anchor_matches_the_model(self, anchors_in_unreal, model):
        from cataclysm_sim import scoring
        for tier in range(1, 9):
            assert anchors_in_unreal[tier] == scoring.PLAYER_MAX_SCORES[tier], (
                f"tier {tier}: Unreal has {anchors_in_unreal[tier]}, the model "
                f"has {scoring.PLAYER_MAX_SCORES[tier]}")

    def test_the_anchors_climb(self, anchors_in_unreal):
        """A tier that is worth less than the one below it would make the
        difficulty comparison run backwards."""
        assert anchors_in_unreal == sorted(anchors_in_unreal)


class TestTheFiveWeights:
    def test_every_weight_matches_the_model(self, weights_in_unreal, model):
        for name, key in WEIGHT_NAMES.items():
            assert weights_in_unreal[name] == model.WEIGHTS[key], (
                f"{name}: Unreal has {weights_in_unreal[name]}, the model's "
                f"{key} is {model.WEIGHTS[key]}")

    def test_they_are_the_owners_plain_numbers(self, weights_in_unreal):
        """The owner's ruling of 2026-11-24, written out: a re-tune has to
        change this line as well as the two copies."""
        assert weights_in_unreal == {
            "PointsPerLevel": 10.0, "PointsPerGearRarityStep": 2.0,
            "UpgradeFactorPerGearLevel": 1.0, "PointsPerGemRarityStep": 5.0,
            "PointsPerResistancePercent": 1.0}

    def test_the_shares_are_gone_from_the_header(self):
        """A `ShareLevel` left behind would read as still deciding something."""
        assert "constexpr float Share" not in read(HEADER)

    @pytest.mark.parametrize("function, constant", [
        ("LevelWeight", "PointsPerLevel"),
        ("GearWeight", "PointsPerGearRarityStep"),
        ("UpgradeFactor", "UpgradeFactorPerGearLevel"),
        ("GemWeight", "PointsPerGemRarityStep"),
        ("ResistanceWeight", "PointsPerResistancePercent"),
    ])
    def test_each_weight_function_returns_its_constant(self, function, constant):
        text = read(SOURCE)
        found = re.findall(
            rf"float UCataclysmPowerScore::{function}\(\)\s*\{{\s*return (\w+);\s*\}}", text)
        assert found == [constant], (
            f"UCataclysmPowerScore::{function} should return {constant}, found {found}")

    def test_breakdown_reads_the_five_constants(self):
        """The score itself, not only the functions, uses the header's five."""
        text = read(SOURCE)
        start = text.index("FCataclysmPowerBreakdown UCataclysmPowerScore::Breakdown(")
        body = text[start:text.index("\n}", start)]
        for constant in WEIGHT_NAMES:
            assert f"= {constant};" in body, f"Breakdown does not read {constant}"
        assert "Share" not in body and "Derive" not in body


class TestTheOtherConstants:
    """The fixed quantities the score counts against. A disagreement in any of
    them moves the score of the reference character."""

    @pytest.mark.parametrize("name,expected", [
        ("MaxLevel", 100),
        ("MaxRarity", 8),
        ("MaxUpgrade", 10),
        ("GearPieces", 18),
        ("TotalSockets", 45),
        ("ResistanceCount", 8),
    ])
    def test_an_integer_constant_matches(self, name, expected):
        text = read(HEADER)
        match = re.search(rf"constexpr int32 {name}\s*=\s*(\d+)", text)
        assert match, f"could not find {name} in {HEADER.name}"
        assert int(match.group(1)) == expected

    def test_the_integer_constants_match_the_model(self, model):
        text = read(HEADER)

        def constant(name: str) -> int:
            match = re.search(rf"constexpr int32 {name}\s*=\s*(\d+)", text)
            assert match, f"could not find {name} in {HEADER.name}"
            return int(match.group(1))

        assert constant("MaxLevel") == model.MAX_LEVEL
        assert constant("MaxRarity") == model.MAX_RARITY
        assert constant("MaxUpgrade") == model.MAX_UPGRADE
        assert constant("GearPieces") == model.GEAR_PIECES
        assert constant("TotalSockets") == model.TOTAL_SOCKETS
        assert constant("ResistanceCount") == model.RESISTANCE_COUNT

    def test_the_resistance_cap_matches_the_model(self, model):
        text = read(HEADER)
        match = re.search(r"constexpr float ResistanceCap\s*=\s*([0-9.]+)f", text)
        assert match, f"could not find ResistanceCap in {HEADER.name}"
        assert float(match.group(1)) == pytest.approx(model.RESISTANCE_CAP)


class TestTheCharacterSheet:
    """The sheet's tooltip shows each term beside its weight, read from the code."""

    LAYOUT = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
              / "CataclysmCharacterSheetLayout.cpp")

    def test_the_tooltip_reads_every_weight_from_the_power_score(self):
        text = read(self.LAYOUT)
        start = text.index("FString UCataclysmCharacterSheetLayout::PowerScoreTooltip(")
        body = text[start:text.index("\n}", start)]
        for function in ("LevelWeight", "GearWeight", "UpgradeFactor", "GemWeight",
                         "ResistanceWeight"):
            assert f"UCataclysmPowerScore::{function}()" in body, function
        assert "UCataclysmPowerScore::ResistanceCap" in body
        for words in ("a level)", "a rarity step of each piece, times 1 + ",
                      "a rarity step of each socketed gem)", "a resistance percent, up to "):
            assert words in body, words


#: Section IV of the design document, from its Power Score heading to the next.
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
GDD_HEADING = "### **Power Score**"


@pytest.fixture(scope="module")
def power_score_section() -> str:
    text = GDD.read_text(encoding="utf-8")
    assert GDD_HEADING in text, f"the design document has no {GDD_HEADING!r} heading"
    after = text.split(GDD_HEADING, 1)[1]
    return after.split("### **Attributes**", 1)[0]


def _shares_sentence(shares: dict[str, float]) -> str:
    return (f"{round(100 * shares['level'])}% from level, "
            f"{round(100 * shares['gear'])}% from gear, "
            f"{round(100 * shares['gems'])}% from gems and "
            f"{round(100 * shares['resistances'])}% from resistances")


class TestTheDesignDocument:
    """Section IV's copy of the weights and of every figure they produce."""

    @pytest.mark.parametrize("row, key", [
        ("Level Weight", "level"),
        ("Gear Weight", "gear"),
        ("Upgrade Factor", "upgrade_factor"),
        ("Gem Weight", "gem"),
        ("Resistance Weight", "resistance"),
    ])
    def test_the_weight_table_is_the_models(self, power_score_section, model, row, key):
        assert f"| {row} | {model.WEIGHTS[key]:g} |" in power_score_section, (
            f"the {row} row should read {model.WEIGHTS[key]:g}")

    def test_the_old_derived_weights_are_not_stated_as_current(self, power_score_section):
        assert "| Level Weight | 6.3270 |" not in power_score_section

    def test_the_tier_8_shares_are_the_models(self, power_score_section, model):
        shares = model.shares_of(model.reference_character(8))
        assert f"the four terms contribute {_shares_sentence(shares)}." in power_score_section

    def test_the_tier_1_shares_are_the_models(self, power_score_section, model):
        shares = model.shares_of(model.reference_character(1))
        assert f"The finished tier 1 character draws {_shares_sentence(shares)}" in power_score_section

    @pytest.mark.parametrize("label, rarity, upgrade", [
        ("Everyday +0", 1, 0),
        ("Masterful +0, the best a tier 1 drop can be", 4, 0),
        ("Everyday +3, the tier 1 reference character's", 1, 3),
        ("Legendary +4, at its gate", 5, 4),
        ("Mythical +6, at its gate", 6, 6),
        ("Cataclysmic +10", 8, 10),
    ])
    def test_the_piece_table_is_the_models(self, power_score_section, model, label, rarity, upgrade):
        points = model.gear_term(model.Character(1, gear=(model.GearPiece(rarity, upgrade),)))
        assert f"| {label} | {points:g} |" in power_score_section

    def test_a_plus_ten_piece_is_stated_at_the_models_multiple(self, power_score_section, model):
        multiple = 1 + model.MAX_UPGRADE * model.WEIGHTS["upgrade_factor"]
        assert (f"A +10 Cataclysmic piece is worth {multiple:g} times the same piece at +0"
                in power_score_section)

    def test_the_reference_table_is_the_models(self, power_score_section, model):
        from cataclysm_sim import scoring
        for tier in range(1, 9):
            score = model.power_score(model.reference_character(tier))
            anchor = scoring.PLAYER_MAX_SCORES[tier]
            pct = 100.0 * (score - anchor) / anchor
            cell = f"| {score:,} | {anchor:,} ({pct:+.1f}%) |"
            assert cell in power_score_section, f"tier {tier} should end {cell!r}"

    @pytest.mark.parametrize("label, rarities", [
        ("Cataclysmic", [8] * 18),
        ("Ascendant", [7] * 18),
        ("Mythical", [6] * 18),
        ("Legendary", [5] * 18),
        ("Masterful", [4] * 18),
        ("A mix of 4 Cataclysmic, 4 Ascendant, 5 Mythical and 5 Legendary",
         [8] * 4 + [7] * 4 + [6] * 5 + [5] * 5),
    ])
    def test_the_tier_8_gear_table_is_the_models(self, power_score_section, model, label, rarities):
        from cataclysm_sim import scoring
        character = model.Character(
            level=model.MAX_LEVEL,
            gear=tuple(model.GearPiece(r, model.MAX_UPGRADE) for r in rarities),
            gems=tuple([model.MAX_RARITY] * model.TOTAL_SOCKETS),
            resistances=tuple([model.RESISTANCE_CAP] * model.RESISTANCE_COUNT))
        score = model.power_score(character)
        share = round(100 * score / scoring.PLAYER_MAX_SCORES[8])
        assert f"| {label} | {score:,} | {share}% |" in power_score_section
