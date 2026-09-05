"""A city upgrade's tier 1 magnitude must be readable as a number.

WHY THIS FILE EXISTS. Issue #1262. The `City Upgrades` sheet of the design
workbook has four columns -- Type, Tier 1, Tier 2, Tier 3 -- and its `Tier 1`
cell is the effect SENTENCE rather than a number. Tiers 2 and 3 are bare numbers
beside it. So `game/Data/CityUpgrades.csv` published what tier 2 would improve a
figure to, and never published what the figure starts at, which is enough to
block a city upgrade system: an upgrade is bought at tier 1 first.

`parse_tier_one` in `tools/generate_datatables.py` reads the magnitude out of the
sentence. That is a heuristic on prose, and the failure mode of a heuristic on
prose is a WRONG NUMBER rather than an error, so every one of the 24 published
values is pinned here. A sentence reworded in the workbook fails in this file
instead of quietly changing what an upgrade is worth.

THREE SENTENCES ARE BUILT TO DEFEAT A NAIVE READER, and each has a test of its
own below:

  "Every 20 days this city's defenses heal 5%."   the interval comes FIRST
  "take 4 less days to beat, to a minimum of 1"   a second number that is a floor
  "Cleanse ... lose 50% of their remaining ..."   two magnitudes, one is a word

WHAT IS CHECKED BEYOND THE PINS. Two invariants that can actually fail if a
tier 1 value is parsed wrongly, rather than merely restating it:

  - tier 1's kind must equal tier 2's kind, because they describe one effect
  - each ladder must move in one direction across the three tiers

Both compare the new value against the two that were already published, so a
mis-parse has to be consistent with the existing data to survive them.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import generate_datatables as gen  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
UPGRADES = REPO_ROOT / "game" / "Data" / "CityUpgrades.csv"

#: Every published row: name -> (kind, value, interval days).
#:
#: READ OFF THE PARSER'S OUTPUT AND THEN CHECKED BY HAND against the sentence in
#: the same row of the workbook. Pinning what a run produced without reading it
#: would only prove the code still does what it did.
EXPECTED = {
    "Architect_Increase_max_defense_by_20":              ("Percent", 0.20, 0.0),
    "Architect_Increase_max_population_by_20":           ("Percent", 0.20, 0.0),
    "Architect_Remove_25_of_dungeons_on_this_city":      ("Percent", 0.25, 0.0),
    "Architect_Restore_city_s_defenses_by_50":           ("Percent", 0.50, 0.0),
    "Architect_Restore_city_s_population_by_50":         ("Percent", 0.50, 0.0),
    "Architect_This_city_resists_25_of_damage":          ("Percent", 0.25, 0.0),
    "Architect_This_city_resists_25_of_population_loss": ("Percent", 0.25, 0.0),
    "Architect_Every_20_days_this_city_s_defenses_heal":
        ("IntervalPercent", 0.05, 20.0),
    "Architect_Every_20_days_this_city_s_population_rec":
        ("IntervalPercent", 0.05, 20.0),
    "Architect_When_you_clear_a_dungeon_the_city_s_Def": ("Percent", 0.05, 0.0),
    "Explorer_There_can_be_no_more_than_15_dungeons_on": ("Flat", 15.0, 0.0),
    "Explorer_Dungeons_here_take_4_less_days_to_beat":   ("Flat", 4.0, 0.0),
    "Explorer_Dungeons_here_have_5_more_floors":         ("Flat", 5.0, 0.0),
    "Explorer_Dungeons_here_have_5_fewer_floors_to_a":   ("Flat", 5.0, 0.0),
    "Explorer_Increases_the_chance_of_dungeons_on_this": ("Percent", 0.25, 0.0),
    "Explorer_Increases_the_chance_of_dungeons_on_this_1":
        ("Percent", 0.25, 0.0),
    "Explorer_Increases_the_chance_of_dungeons_on_this_2":
        ("Percent", 0.25, 0.0),
    "Explorer_Increases_the_chance_of_dungeons_on_thie": ("Percent", 0.25, 0.0),
    "Treasurer_Dungeons_here_provide_2x_more_experience":
        ("Multiplier", 2.0, 0.0),
    "Treasurer_Dungeons_here_have_10_increased_magic_f": ("Percent", 0.10, 0.0),
    "Treasurer_Dungeons_here_drop_25_more_gold":         ("Percent", 0.25, 0.0),
    "Treasurer_Dungeons_here_drop_25_more_loot":         ("Percent", 0.25, 0.0),
    "Artisan_Dungeons_here_drop_25_more_crafting_mat":   ("Percent", 0.25, 0.0),
    # No tier ladder at all, so no tier 1 value either. See the class below.
    "Unbranched_Cleanse_every_player_city_of_half_of_the": ("", 0.0, 0.0),
}


def published_rows() -> list[dict]:
    """The committed CSV, or a skip naming what is missing."""
    if not UPGRADES.is_file():
        pytest.skip(f"{UPGRADES.name} is not present")
    with UPGRADES.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


class TestTheFileIsActuallyBeingRead:
    """First in the file on purpose.

    Every assertion below reads a column out of a CSV by name. A renamed or
    missing column makes `DictReader` hand back None for it, and comparing None
    to None passes. These two are the guard against this whole file quietly
    checking nothing.
    """

    def test_the_csv_has_the_tier_one_columns(self):
        rows = published_rows()
        assert rows, "CityUpgrades.csv has no rows"
        for column in ("Tier1Kind", "Tier1Value", "Tier1IntervalDays"):
            assert column in rows[0], (
                f"CityUpgrades.csv has no {column} column, so nothing below "
                f"checks anything. Regenerate it: python "
                f"tools/generate_datatables.py")

    def test_every_pinned_row_is_present(self):
        names = {row["Name"] for row in published_rows()}
        missing = sorted(set(EXPECTED) - names)
        assert not missing, (
            f"CityUpgrades.csv no longer has these rows, so their pins below "
            f"are dead: {missing}")
        extra = sorted(names - set(EXPECTED))
        assert not extra, (
            f"CityUpgrades.csv has rows this file does not pin: {extra}. Add "
            f"each to EXPECTED after reading its sentence in the workbook.")


class TestEveryPublishedValue:
    """The 24 pins."""

    def test_each_row_publishes_the_magnitude_its_sentence_states(self):
        wrong = []
        for row in published_rows():
            want = EXPECTED[row["Name"]]
            got = (row["Tier1Kind"], float(row["Tier1Value"]),
                   float(row["Tier1IntervalDays"]))
            if got != want:
                wrong.append(f"{row['Name']}\n"
                             f"      sentence {row['Effect']!r}\n"
                             f"      wanted   {want}\n"
                             f"      got      {got}")
        assert not wrong, (
            "The tier 1 magnitude read out of these sentences is not what the "
            "sentence says:\n  " + "\n  ".join(wrong))


class TestTheThreeSentencesBuiltToDefeatANaiveReader:
    def test_an_interval_sentence_takes_the_second_number_as_the_magnitude(self):
        """"Every 20 days this city's defenses heal 5%." states the interval
        first. Taking the first number in the sentence gives 20, which would
        make the upgrade heal 2000% of a city's defences every 0 days."""
        kind, value, interval = gen.parse_tier_one(
            "Every 20 days this city's defenses heal 5%.", 1)
        assert (kind, value, interval) == ("IntervalPercent", 0.05, 20.0)

    def test_a_minimum_clause_is_not_mistaken_for_the_magnitude(self):
        """Two rows end "to a minimum of 1". That 1 is a floor under the effect,
        not the effect's strength, so the FIRST number is the wanted one."""
        assert gen.parse_tier_one(
            "Dungeons here take 4 less days to beat, to a minimum of 1", 1) == \
            ("Flat", 4.0, 0.0)
        assert gen.parse_tier_one(
            "Dungeons here have 5 fewer floors, to a minimum of 1.", 1) == \
            ("Flat", 5.0, 0.0)

    def test_the_upgrade_with_no_tier_ladder_publishes_no_magnitude(self):
        """The unbranched upgrade cleanses half the dungeons from every city and
        COSTS 50% of their defences and population. Publishing the 50% would say
        its strength is 50% when 50% is the price. Its Tier 2 and Tier 3 cells
        are both empty, and that is what suppresses it."""
        row = next(r for r in published_rows()
                   if r["Name"].startswith("Unbranched_"))
        assert (row["Tier2Raw"], row["Tier3Raw"]) == ("", ""), (
            "This row is meant to be the one with no tier ladder. It now has "
            "one, so the rule below no longer describes it.")
        assert row["Tier1Kind"] == ""
        assert float(row["Tier1Value"]) == 0.0
        assert "50%" in row["Effect"], (
            "The sentence no longer contains the 50% that a naive reader would "
            "have published, so this test no longer proves anything.")


class TestTierOneAgreesWithTheTiersThatWereAlreadyPublished:
    """These two compare tier 1 against tiers 2 and 3, which were published long
    before it and parsed from separate cells. A mis-read tier 1 has to agree
    with both of them to survive, which is a far stronger check than a pin."""

    def test_tier_one_is_the_same_kind_as_tier_two(self):
        wrong = []
        for row in published_rows():
            if not row["Tier2Kind"]:
                continue
            if row["Tier1Kind"] != row["Tier2Kind"]:
                wrong.append(f"{row['Name']}: tier 1 is {row['Tier1Kind']!r} "
                             f"and tier 2 is {row['Tier2Kind']!r}")
        assert not wrong, (
            "One effect cannot be a percentage at one tier and a flat number at "
            "the next, so one of the two is read wrongly:\n  "
            + "\n  ".join(wrong))

    def test_each_ladder_moves_in_one_direction(self):
        """Tier 2 improves on tier 1 and tier 3 on tier 2, so the three values
        must be ordered. ONE LADDER RUNS DOWNWARDS and that is correct: the
        dungeon cap is "no more than N dungeons on this city", where a smaller N
        is the better upgrade."""
        falls = []
        for row in published_rows():
            if not row["Tier2Kind"] or not row["Tier3Kind"]:
                continue
            values = (float(row["Tier1Value"]), float(row["Tier2Value"]),
                      float(row["Tier3Value"]))
            rising = values[0] < values[1] < values[2]
            falling = values[0] > values[1] > values[2]
            assert rising or falling, (
                f"{row['Name']} is not ordered across its three tiers: "
                f"{values}. Tier 1 is the suspect, because tiers 2 and 3 were "
                f"published and checked long before it.")
            if falling:
                falls.append(row["Name"])

        assert falls == ["Explorer_There_can_be_no_more_than_15_dungeons_on"], (
            f"The dungeon cap is the only upgrade whose ladder should run "
            f"downwards, because a lower cap is the better one. These run "
            f"downwards instead: {falls}")

    def test_an_interval_ladder_shortens_the_wait_while_raising_the_amount(self):
        """The two "every N days" upgrades improve BOTH halves: the magnitude
        rises and the wait between triggers shrinks."""
        checked = 0
        for row in published_rows():
            if row["Tier1Kind"] != "IntervalPercent":
                continue
            checked += 1
            intervals = (float(row["Tier1IntervalDays"]),
                         float(row["Tier2IntervalDays"]),
                         float(row["Tier3IntervalDays"]))
            assert intervals[0] > intervals[1] > intervals[2], (
                f"{row['Name']} does not shorten its wait as it is upgraded: "
                f"{intervals}")
        assert checked == 2, (
            f"Expected the two 'every 20 days' upgrades to be interval ones, "
            f"found {checked}")


class TestTheParserItself:
    @pytest.mark.parametrize("sentence,kind,value,days", [
        ("Increase max defense by 20%",              "Percent",         0.20, 0),
        ("This city resists 25% of damage",          "Percent",         0.25, 0),
        ("Dungeons here have 5 more floors.",        "Flat",            5.00, 0),
        ("There can be no more than 15 dungeons",    "Flat",           15.00, 0),
        ("Dungeons here provide 2x more experience", "Multiplier",      2.00, 0),
        ("Every 20 days this city heals 5%.",        "IntervalPercent", 0.05, 20),
        ("Every 1 day this city heals 12.5%.",       "IntervalPercent", 0.125, 1),
        ("", "", 0.0, 0),
    ])
    def test_each_shape_parses(self, sentence, kind, value, days):
        assert gen.parse_tier_one(sentence, 1) == (kind, value, days)

    def test_a_sentence_with_no_number_is_an_error_rather_than_a_zero(self):
        """Publishing 0 for an upgrade whose magnitude could not be read is the
        exact failure this whole file exists to prevent, so generation stops."""
        with pytest.raises(gen.DataError, match="no magnitude at all"):
            gen.parse_tier_one("This city can never fall", 7)

    def test_a_multiplier_is_not_read_as_a_flat_number(self):
        """"2x" contains a bare 2, so the flat branch would match it and turn a
        doubling into two of something."""
        assert gen.parse_tier_one("provide 2x more experience", 1)[0] == \
            "Multiplier"

    def test_a_percentage_is_not_read_as_a_flat_number(self):
        """"20%" contains a bare 20, so the flat branch would match it and turn
        a fifth into twenty of something."""
        assert gen.parse_tier_one("Increase max defense by 20%", 1) == \
            ("Percent", 0.20, 0.0)
