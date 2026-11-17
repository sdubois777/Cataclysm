"""The affix tier gate, pinned across the design document, the shipped crafting
data and the model.

WHY THIS EXISTS. Issue #129, revised by the project owner on 2026-08-05 in issue
#241. The rule is now:

    a DROP rolls up to min(7, difficulty tier + 1)
    CRAFTING has no tier gate; cost is the only limit
    a player may equip an affix above their own difficulty tier

It is stated in several places and each can drift from the others:

    docs/Cataclysm_GDD_v2.md            the design document, section VI, in the
                                        section "What Tier an Affix Can Roll At"
    docs/DECISIONS.md                   the reasoning and the sources
    game/Data/CraftingMaterials.csv     what the game loads: the Potency
                                        Crystal's stated function
    sim/cataclysm_sim/affixes.py        the model that implements the gate

THE ONE THING THE CRAFTING CHECK IS FOR. It said the opposite until #241: that
the Potency Crystal could not raise an affix past the difficulty tier. The
project owner reversed that, so the check now holds the reverse — that the row
does NOT claim a difficulty tier gate. Same file, opposite assertion, which is
why it is checked rather than left to memory. That row is generated from the
Crafting sheet of `docs/All_Things_Cataclysm.xlsx`, so a change has to be made
there and the table regenerated with `python tools/generate_datatables.py`.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"
CRAFTING_CSV = REPO_ROOT / "game" / "Data" / "CraftingMaterials.csv"

SECTION = "### **What Tier an Affix Can Roll At**"


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import affixes
    return affixes


@pytest.fixture(scope="module")
def section() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    body = GDD.read_text(encoding="utf-8")
    start = body.find(SECTION)
    assert start != -1, f"{GDD.name} has no section headed {SECTION!r}"
    after = body[start + len(SECTION):]
    ends = [m.start() for m in re.finditer(r"^#{1,6} ", after, re.MULTILINE)]
    return after[:ends[0]] if ends else after


@pytest.fixture(scope="module")
def gate_table(section) -> dict[int, int]:
    """The difficulty tier to affix tier table, read out of the document."""
    found: dict[int, int] = {}
    for line in section.splitlines():
        match = re.match(r"\|\s*(\d+)\s*\|\s*T(\d+)\s*\|", line.strip())
        if match:
            found[int(match.group(1))] = int(match.group(2))
    return found


@pytest.fixture(scope="module")
def potency_crystal() -> dict[str, str]:
    if not CRAFTING_CSV.is_file():
        pytest.skip(f"{CRAFTING_CSV.name} not present")
    with CRAFTING_CSV.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    for row in rows:
        if (row.get("MaterialName") or "").strip() == "Potency Crystal":
            return row
    pytest.fail(f"{CRAFTING_CSV.name} has no Potency Crystal row")


@pytest.fixture(scope="module")
def entry() -> str:
    if not DECISIONS.is_file():
        pytest.skip("the decision log is not present")
    body = DECISIONS.read_text(encoding="utf-8")
    start = body.find("## 2026-08-05 — The difficulty tier caps which affix tier")
    assert start != -1, "the decision log has no entry for issue #129"
    after = body[start:]
    end = after.find("\n---\n")
    return after[:end] if end != -1 else after


class TestTheDocumentTableMatchesTheModel:
    def test_the_table_covers_every_difficulty_tier(self, gate_table, model):
        assert sorted(gate_table) == list(
            range(1, model.DIFFICULTY_TIERS + 1)), (
            "the table in the design document does not have one row per "
            f"difficulty tier: it has {sorted(gate_table)}")

    def test_every_row_matches_the_model(self, gate_table, model):
        for difficulty_tier, affix_tier in sorted(gate_table.items()):
            assert affix_tier == model.max_affix_tier_on_a_drop(
                    difficulty_tier), (
                f"the document says a drop at difficulty tier {difficulty_tier} "
                f"reaches T{affix_tier}; the model says "
                f"T{model.max_affix_tier_on_a_drop(difficulty_tier)}")

    def test_the_gear_rarity_column_matches_the_reference_progression(
            self, section, model):
        """The third column names the gear rarity and upgrade level at each
        tier. Those come from the reference character in
        `sim/cataclysm_sim/player_power.py`, so they cannot be typed in freely."""
        from cataclysm_sim import player_power
        for line in section.splitlines():
            match = re.match(
                r"\|\s*(\d+)\s*\|\s*T\d+\s*\|\s*(\w+) gear, \+(\d+)",
                line.strip())
            if not match:
                continue
            tier, rarity, upgrade = (int(match.group(1)), match.group(2),
                                     int(match.group(3)))
            character = player_power.reference_character(tier)
            assert model.RARITIES[character.gear[0].rarity - 1] == rarity, (
                f"the document says tier {tier} is {rarity} gear; the "
                "reference character says "
                f"{model.RARITIES[character.gear[0].rarity - 1]}")
            assert character.gear[0].upgrade == upgrade, (
                f"the document says tier {tier} is +{upgrade}; the reference "
                f"character says +{character.gear[0].upgrade}")


class TestTheDocumentStatesTheRule:
    def test_it_states_the_drop_formula(self, section):
        assert "min(7, difficulty tier + 1)" in section

    def test_it_says_crafting_is_limited_by_cost_and_not_by_tier(self, section):
        assert "Crafting is not gated by the difficulty tier" in section
        assert "cost is what limits it" in section

    def test_it_says_every_tier_below_the_cap_stays_in_the_pool(self, section):
        assert "at or below the cap stays in the pool" in section

    def test_it_says_no_tier_is_drop_only(self, section):
        assert "No affix tier is drop-only" in section

    def test_it_names_the_games_the_shape_came_from(self, section):
        assert "Path of Exile" in section
        assert "Last Epoch" in section

    def test_it_says_where_the_drop_cap_stops_climbing(self, section):
        assert "reaches T7 at difficulty tier 6 and stays there" in section

    def test_the_odds_it_quotes_follow_from_the_cap(self, section, model):
        """"about one time in seven" is 1 / max_affix_tier(8) under a uniform
        draw. If the draw stops being uniform this sentence stops being true."""
        match = re.search(r"reaches T7 about one time in (\w+)", section)
        assert match, "the section does not say how often a deep drop reaches T7"
        words = {"five": 5, "six": 6, "seven": 7, "eight": 8}
        stated = words.get(match.group(1))
        assert stated is not None, (
            f"the section says 'one time in {match.group(1)}', which this test "
            "cannot read as a number")
        assert stated == model.max_affix_tier_on_a_drop(model.DIFFICULTY_TIERS)

    def test_the_average_it_quotes_follows_from_the_cap(self, section, model):
        match = re.search(r"averages T(\d+)", section)
        assert match, "the section does not say what a deep drop averages"
        expected = (model.max_affix_tier_on_a_drop(model.DIFFICULTY_TIERS)
                    + 1) / 2
        assert abs(int(match.group(1)) - expected) < 0.5


class TestTheAffixTiersSectionPointsAtTheGate:
    def test_the_opening_of_the_affix_tiers_section_names_the_gate(self):
        """A reader arriving at the tier ladder has to be told a gate exists,
        or they will read all seven tiers as available from the start."""
        body = GDD.read_text(encoding="utf-8")
        start = body.find("### **Affix Tiers**")
        assert start != -1, "the design document has no Affix Tiers section"
        opening = body[start:start + 1200]
        assert "difficulty tier" in opening, (
            "the Affix Tiers section does not mention the difficulty tier gate")


class TestTheShippedCraftingDataSaysTheSame:
    def test_the_potency_crystal_does_not_claim_a_difficulty_tier_gate(
            self, potency_crystal):
        """It said it did until #241. The project owner reversed that: crafting
        may reach T7 at any difficulty tier and cost is the limit. A row that
        claims a gate again would contradict the design document."""
        functions = (potency_crystal.get("Functions") or "")
        assert "never above the current difficulty tier" not in functions, (
            "the Potency Crystal row in game/Data/CraftingMaterials.csv claims "
            "a difficulty tier gate on crafting. The project owner removed that "
            "on issue #241: crafting may reach T7 anywhere and cost is what "
            "limits it. Edit the Crafting sheet of "
            "docs/All_Things_Cataclysm.xlsx and regenerate with "
            "python tools/generate_datatables.py.")
        assert "cost is the limit" in functions.lower(), (
            f"the Potency Crystal row says {functions!r}, which does not say "
            "what limits crafting. A reader of the crafting table alone should "
            "not have to infer it.")

    def test_it_still_states_the_top_affix_tier(self, potency_crystal, model):
        functions = (potency_crystal.get("Functions") or "").lower()
        assert f"t{model.AFFIX_TIERS[-1]}" in functions, (
            "the Potency Crystal row no longer names the top affix tier")


class TestTheDecisionIsRecorded:
    def test_it_cites_a_source_for_the_shape(self, entry):
        assert "http" in entry

    def test_it_says_which_parts_are_judgements(self, entry):
        """Three parts of this decision are calls rather than readings: which
        end doubles up, whether crafting is capped, and whether any tier is
        drop-only. The entry has to say so rather than present them as
        derived."""
        assert entry.count("JUDGEMENT, NOT DERIVED") >= 2

    def test_it_says_which_part_is_most_open_to_reversal(self, entry):
        assert "most open to reversal" in entry
