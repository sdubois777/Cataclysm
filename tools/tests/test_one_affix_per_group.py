"""One affix per group, pinned across the design document and the model.

WHY THIS EXISTS. Issue #128. Nothing stopped a four-affix piece rolling the same
affix four times. The rule that now stops it is stated in
`docs/Cataclysm_GDD_v2.md`, in the section headed "One Affix Per Group", and
implemented in `sim/cataclysm_sim/affixes.py`. Two copies of a rule drift, so
this file compares them.

WHAT IT CHECKS AND WHERE.

    docs/Cataclysm_GDD_v2.md        the design document, section VI
    docs/DECISIONS.md               the reasoning and the sources
    sim/cataclysm_sim/affixes.py    the model that implements the rule
    game/Data/Affixes.csv           the table the game loads

THE ONE THING THE CSV CHECK IS FOR. Issue #128 proposed adding a group column to
`game/Data/Affixes.csv`. The decision went the other way: the group is derived
from what the affix grants, from columns already present, because a derived group
cannot drift from the affix and an authored one can. So a group column appearing
later is a reversal of a decision rather than an addition, and this notices it.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"
AFFIX_CSV = REPO_ROOT / "game" / "Data" / "Affixes.csv"

SECTION = "### **One Affix Per Group**"


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import affixes
    return affixes


@pytest.fixture(scope="module")
def headers() -> list[str]:
    """The column names of the table the game loads."""
    if not AFFIX_CSV.is_file():
        pytest.skip(f"{AFFIX_CSV.name} not present")
    with AFFIX_CSV.open(newline="", encoding="utf-8-sig") as handle:
        return next(csv.reader(handle))


@pytest.fixture(scope="module")
def entry() -> str:
    """The decision log entry for issue #128."""
    if not DECISIONS.is_file():
        pytest.skip("the decision log is not present")
    body = DECISIONS.read_text(encoding="utf-8")
    start = body.find("## 2026-08-05 — One affix per group")
    assert start != -1, "the decision log has no entry for issue #128"
    after = body[start:]
    end = after.find("\n---\n")
    return after[:end] if end != -1 else after


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


class TestTheDocumentStatesTheRule:
    def test_it_states_the_rule_itself(self, section):
        assert "at most one affix from any group" in section, (
            "the section does not state the rule in one sentence")

    def test_it_says_what_names_a_group(self, section):
        assert "the stat and the kind together" in section

    def test_it_says_the_group_is_derived_rather_than_written_on_the_affix(
            self, section):
        assert "derived from what the affix grants" in section

    def test_it_covers_flat_against_increased(self, section):
        assert "Flat and increased" in section

    def test_it_covers_a_hybrid_against_its_own_half(self, section):
        assert "hybrid and one of its halves" in section

    def test_it_covers_resistance(self, section):
        assert "single-resistance" in section
        assert "all-resistance" in section

    def test_it_covers_prefixes_against_suffixes(self, section):
        assert "prefix and a suffix" in section

    def test_it_names_the_game_the_shape_came_from(self, section):
        """The project's own rule: name the source in the design, not only in
        the pull request that added it."""
        assert "Path of Exile" in section
        assert "mod group" in section


class TestTheNumbersInTheSectionComeFromTheModel:
    def test_the_hybrid_share_it_quotes_is_the_model_figure(
            self, section, model):
        match = re.search(r"grants each half at (\d+)%", section)
        assert match, "the section does not say what share of each half a hybrid grants"
        assert int(match.group(1)) == round(model.HYBRID_FRACTION * 100)

    def test_the_slots_to_cap_a_resistance_it_quotes_is_the_model_figure(
            self, section, model):
        match = re.search(r"takes roughly (\w+) affix slots", section)
        assert match, "the section does not say what capping a resistance costs"
        words = {"ten": 10, "eleven": 11, "twelve": 12, "thirteen": 13,
                 "fourteen": 14}
        stated = words.get(match.group(1))
        assert stated is not None, (
            f"the section says 'roughly {match.group(1)}', which this test "
            "cannot read as a number")
        actual = model.slots_to_cap(model.ALL_RESISTANCE, 8, 8, roll=1.0)
        assert abs(stated - actual) <= 1.0, (
            f"the section says roughly {stated} slots; the model gives "
            f"{actual:.1f}")


class TestTheModelImplementsWhatTheDocumentSays:
    def test_flat_and_increased_are_different_groups(self, model):
        assert model.groups_of(model.FLAT_HEALTH) != \
            model.groups_of(model.INCREASED_HEALTH)

    def test_a_hybrid_shares_a_group_with_each_of_its_halves(self, model):
        for hybrid in model.HYBRID_AFFIXES:
            for part in hybrid.parts:
                assert model.groups_of(part) <= model.groups_of(hybrid), \
                    f"{hybrid.name} does not occupy {part.name}'s group"

    def test_resistance_is_grouped_by_damage_type(self, model):
        singles = {model.resistance_group(t) for t in model.DAMAGE_TYPES}
        assert len(singles) == len(model.DAMAGE_TYPES)
        assert model.groups_of(model.ALL_RESISTANCE, model.DAMAGE_TYPES) == \
            singles

    def test_the_two_positions_share_no_group(self, model):
        prefixes = {g for a in model.AFFIX_POOL if a.position == model.PREFIX
                    for g in model.groups_of(a)}
        suffixes = {g for a in model.AFFIX_POOL if a.position == model.SUFFIX
                    for g in model.groups_of(a)}
        assert not (prefixes & suffixes)

    def test_every_slot_can_still_fill_its_slots_without_repeating_a_group(
            self, model):
        for slot in model.GEAR_SLOTS:
            assert model.distinct_groups_for(slot, model.PREFIX) >= \
                model.PREFIXES_PER_PIECE, slot
            assert model.distinct_groups_for(slot, model.SUFFIX) >= \
                model.SUFFIXES_PER_PIECE, slot


class TestTheGroupIsNotAColumn:
    def test_the_affix_table_has_no_group_column(self, headers):
        offending = [h for h in headers if "group" in h.lower()]
        assert not offending, (
            f"{AFFIX_CSV.name} has {offending}. The decision on issue #128 was "
            "that an affix's group is DERIVED from what it grants, not written "
            "beside it, so the two cannot disagree. Reversing that is a design "
            "decision; record it in docs/DECISIONS.md and change this test.")

    def test_the_columns_a_group_is_derived_from_are_all_present(self, headers):
        """If any of these went away the group could no longer be computed, and
        the derived-rather-than-authored decision would stop being possible."""
        for column in ("Stat", "ValueKind", "Breadth", "Ailment",
                       "HybridPart1", "HybridPart2"):
            assert column in headers, (
                f"{AFFIX_CSV.name} has no {column} column, so an affix's group "
                "can no longer be derived")


class TestTheDecisionIsRecorded:
    def test_it_cites_a_source_for_the_shape(self, entry):
        assert "http" in entry, (
            "the entry names no source. The project's rule is that a shape "
            "taken from another game is cited so the next reader can check it.")

    def test_it_says_which_part_is_a_judgement_rather_than_researched(
            self, entry):
        """A hybrid excluding its own halves is the one place this design
        departs from the game it took the shape from, so the entry has to say
        so rather than present it as read off the source."""
        assert "JUDGEMENT, NOT DERIVED" in entry

    def test_it_says_what_it_could_not_confirm(self, entry):
        assert "could not confirm" in entry
