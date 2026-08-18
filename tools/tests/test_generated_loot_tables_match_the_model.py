"""The generated loot CSVs must carry what the simulation says they carry.

WHY THIS IS A THIRD COMPARISON AND NOT A DUPLICATE OF THE OTHER TWO. The drop
weights, socket maxima, affix tier weights and affix name words each live in
three places: the design workbook, the simulation package, and the CSV the engine
imports. Four files already compare the WORKBOOK against the SIMULATION --
`test_loot_sheet_matches_the_model.py`, `test_socket_sheet_matches_the_model.py`,
`test_affix_tier_sheet_matches_the_model.py` and
`test_affix_name_words_match_the_sheet.py`.

Nothing compared the CSV against either. A handler in
`tools/generate_datatables.py` that read the wrong column, rounded a value, or
dropped a row would leave both of those comparisons passing and hand the engine
different numbers from the ones the simulation was balanced against. That is the
gap this file closes, and it is the half that actually reaches the game: the CSV
becomes a DataTable asset, and a packaged build reads the asset.

WHICH IS AUTHORITATIVE. The workbook, as everywhere else. When one of these fails
the fix is usually in the generator, because the simulation is already checked
against the sheet by the four files above; if those are failing too, fix the
simulation first and come back.

THE GEAR RARITY LADDER IS ALSO CHECKED AGAINST THE ENGINE'S OWN ENUM here. The
eight rows are keyed on the rarity's name, and the engine looks each one up by
the name of its `ECataclysmRarity` entry rather than carrying a ladder column of
its own. That join is the reason `FCataclysmGearRarityRow` has no Step field
where `FCataclysmEnemyRarityRow` does, so it needs a test: a rarity renamed on
one side and not the other would look up nothing, and a DataTable returning no
row is not an error Unreal reports.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
ITEM_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
               / "CataclysmItem.h")


def table(name: str) -> list[dict]:
    path = DATA / name
    if not path.is_file():
        pytest.skip(f"{name} is not present. Run tools/generate_datatables.py")
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def loot():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import loot as module
    return module


@pytest.fixture(scope="module")
def affixes():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import affixes as module
    return module


@pytest.fixture(scope="module")
def naming():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import naming as module
    return module


# --------------------------------------------------------------------------
# Gear Rarity
# --------------------------------------------------------------------------

class TestTheGearRarityTable:
    def test_it_holds_every_rarity_and_nothing_else(self, loot) -> None:
        rows = table("GearRarity.csv")
        assert [row["Rarity"] for row in rows] == list(loot.af.RARITIES)

    def test_the_row_key_is_the_rarity_itself(self) -> None:
        """The engine looks a row up by the name of its ECataclysmRarity entry,
        so the key has to be the plain rarity and not a decorated form."""
        for row in table("GearRarity.csv"):
            assert row["Name"] == row["Rarity"], (
                f"the GearRarity row for {row['Rarity']} is keyed "
                f"{row['Name']!r}. The engine looks it up by the rarity's own "
                "name, so anything else finds no row at all.")

    def test_every_drop_weight_matches_the_model(self, loot) -> None:
        for row in table("GearRarity.csv"):
            assert float(row["DropWeight"]) == pytest.approx(
                loot.RARITY_DROP_WEIGHT[row["Rarity"]]), (
                f"GearRarity.csv weights {row['Rarity']} at {row['DropWeight']} "
                f"and loot.RARITY_DROP_WEIGHT at "
                f"{loot.RARITY_DROP_WEIGHT[row['Rarity']]}. The drop rates the "
                "engine produces would not be the ones the simulation was "
                "balanced against.")

    def test_every_upgrade_level_gate_matches_the_model(self, loot) -> None:
        for row in table("GearRarity.csv"):
            assert int(row["GearLevelGate"]) == \
                loot.RARITY_GEAR_LEVEL_GATE[row["Rarity"]]

    def test_every_residue_band_matches_the_model(self, loot) -> None:
        for row in table("GearRarity.csv"):
            lowest, highest = loot.RARITY_RESIDUE_BAND[row["Rarity"]]
            assert (float(row["ResidueOnDropLowest"]),
                    float(row["ResidueOnDropHighest"])) == \
                pytest.approx((lowest, highest))

    def test_the_whole_ladder_still_sums_to_what_the_decision_says(self) -> None:
        """25,531, so a Cataclysmic drop is one in 25,531 with no magic find.

        Stated separately from any weight, so mistyping two weights that still
        agree with each other is caught. `docs/DECISIONS.md` records the figure
        and the project owner set it on 2026-08-18.
        """
        rows = table("GearRarity.csv")
        total = sum(float(row["DropWeight"]) for row in rows)
        cataclysmic = next(float(row["DropWeight"]) for row in rows
                           if row["Rarity"] == "Cataclysmic")
        assert total == pytest.approx(25531.0)
        assert total / cataclysmic == pytest.approx(25531.0)


class TestTheLadderMatchesTheEnginesEnum:
    """The eight rows are joined to the engine by name, so the names must agree.

    `FCataclysmGearRarityRow` deliberately carries no ladder column, unlike
    `FCataclysmEnemyRarityRow` which carries `Step`. The order comes from
    `ECataclysmRarity` instead, which the engine already indexes
    `RarityComposition` by. That saves a second copy of the ladder and costs this
    test: a rarity renamed in the workbook and not in the enum would look up no
    row, and Unreal reports a missing row as an empty result rather than an error.
    """

    @staticmethod
    def enum_entries() -> list[str]:
        if not ITEM_HEADER.is_file():
            pytest.skip(f"{ITEM_HEADER.name} is not present")
        text = ITEM_HEADER.read_text(encoding="utf-8")

        block = re.search(
            r"enum\s+class\s+ECataclysmRarity\s*:\s*uint8\s*\{(.*?)\};",
            text, re.S)
        assert block, (
            f"ECataclysmRarity could not be found in {ITEM_HEADER.name}. Either "
            "it was renamed or moved, or the pattern in this file has stopped "
            "matching, in which case both tests here would pass having compared "
            "nothing.")
        return re.findall(r"^\s*(\w+)\s+UMETA", block.group(1), re.M)

    def test_the_parser_actually_found_the_entries(self) -> None:
        """Guarded first, because an empty list makes the comparison vacuous."""
        entries = self.enum_entries()
        assert len(entries) == len(table("GearRarity.csv")), (
            f"{len(entries)} ECataclysmRarity entries were parsed out of "
            f"{ITEM_HEADER.name}: {entries}. GearRarity.csv has "
            f"{len(table('GearRarity.csv'))} rows.")

    def test_the_enum_and_the_table_name_the_same_eight_in_the_same_order(
            self) -> None:
        rows = [row["Name"] for row in table("GearRarity.csv")]
        assert self.enum_entries() == rows, (
            "ECataclysmRarity and GearRarity.csv disagree. The engine walks the "
            "ladder by the enum and looks each row up by the enum entry's name, "
            "so a mismatch means a rarity with no row: the drop would find no "
            "weight, no gate and no residue band, and nothing would report it.")


# --------------------------------------------------------------------------
# Item Sockets
# --------------------------------------------------------------------------

class TestTheItemSocketTable:
    def test_every_slot_maximum_matches_the_model(self, loot) -> None:
        stated = {row["Slot"]: int(row["MaxSockets"])
                  for row in table("ItemSockets.csv") if row["Hands"] == "0"}
        assert stated == loot.MAX_SOCKETS_BY_SLOT

    def test_both_weapon_maxima_match_the_model(self, loot) -> None:
        stated = {int(row["Hands"]): int(row["MaxSockets"])
                  for row in table("ItemSockets.csv") if row["Hands"] != "0"}
        assert stated == loot.MAX_SOCKETS_BY_WEAPON_HANDS

    def test_two_one_handed_weapons_match_a_two_hander(self) -> None:
        """The design's own rule, checked against the table rather than against
        the model, so it holds even if both copies were changed together."""
        rows = {row["Hands"]: int(row["MaxSockets"])
                for row in table("ItemSockets.csv") if row["Slot"] == "Weapon"}
        assert rows["1"] * 2 == rows["2"]

    def test_the_table_adds_up_to_the_designed_total(self, loot) -> None:
        """41 sockets across worn gear, which with the 4 potion sockets is the
        45 the design states. Stated here as a number rather than read from the
        model, so one mistyped maximum is caught even if both copies moved.
        """
        rows = table("ItemSockets.csv")
        worn = sum(int(row["MaxSockets"]) for row in rows
                   if row["Slot"] not in ("Weapon", "Ring"))
        worn += sum(int(row["MaxSockets"]) for row in rows
                    if row["Slot"] == "Ring") * loot.RINGS_WORN
        worn += next(int(row["MaxSockets"]) for row in rows
                     if row["Slot"] == "Weapon" and row["Hands"] == "2")
        assert worn == 41
        assert worn + loot.POTION_SOCKETS == 45


# --------------------------------------------------------------------------
# Affix Tiers
# --------------------------------------------------------------------------

class TestTheAffixTierTable:
    def test_it_holds_every_tier_the_model_has(self, affixes) -> None:
        rows = table("AffixTiers.csv")
        assert [int(row["Tier"]) for row in rows] == list(affixes.AFFIX_TIERS)

    def test_every_weight_matches_the_model(self, affixes) -> None:
        for row in table("AffixTiers.csv"):
            assert float(row["DropWeight"]) == pytest.approx(
                affixes.AFFIX_TIER_DROP_WEIGHT[int(row["Tier"])])

    def test_each_tier_is_half_as_likely_as_the_one_below(self) -> None:
        """The decision in its own words, checked against the table rather than
        against the model. The project owner set it on 2026-08-18."""
        weights = [float(row["DropWeight"]) for row in table("AffixTiers.csv")]
        for below, above in zip(weights[:-1], weights[1:], strict=True):
            assert above == pytest.approx(below / 2.0)

    def test_a_top_tier_affix_is_one_in_the_stated_number(self) -> None:
        """127, at difficulty tier 8 where all seven tiers are reachable."""
        weights = [float(row["DropWeight"]) for row in table("AffixTiers.csv")]
        assert sum(weights) / min(weights) == pytest.approx(127.0)


# --------------------------------------------------------------------------
# The word an affix gives an item's name
# --------------------------------------------------------------------------

class TestTheAffixNameWords:
    def test_every_suffix_carries_the_word_the_model_gives_it(
            self, naming) -> None:
        for row in table("Affixes.csv"):
            if row["Position"] != "suffix":
                continue
            assert row["NameWord"] == naming.AFFIX_NAME_WORD[row["AffixName"]], (
                f"Affixes.csv names {row['AffixName']!r} "
                f"{row['NameWord']!r} and naming.AFFIX_NAME_WORD says "
                f"{naming.AFFIX_NAME_WORD[row['AffixName']]!r}. An item would "
                "be called two different things depending on which read it.")

    def test_no_prefix_carries_one(self) -> None:
        """The first word of an item's name is its rarity, so a word on a prefix
        could never be read."""
        carried = [row["AffixName"] for row in table("Affixes.csv")
                   if row["Position"] == "prefix" and row["NameWord"]]
        assert carried == []

    def test_the_table_and_the_model_cover_the_same_affixes(
            self, naming) -> None:
        """Neither direction is caught by the test above: an extra entry in the
        model is never looked at, and an extra suffix in the table would raise a
        KeyError there rather than say what is missing."""
        in_table = {row["AffixName"] for row in table("Affixes.csv")
                    if row["Position"] == "suffix"}
        assert in_table == set(naming.AFFIX_NAME_WORD)

    def test_no_two_affixes_share_a_word(self) -> None:
        """A player reading "of Warding" should be able to look it up and find
        one thing."""
        words = [row["NameWord"] for row in table("Affixes.csv")
                 if row["NameWord"]]
        assert len(words) == len(set(words))
