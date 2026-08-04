"""Leech: three stats, three affixes, one rule, and the document that states it.

WHY THIS EXISTS. Issue #214. `game/Data/Affixes.csv`, the generated table of
rollable affixes, had exactly one leech affix: flat life leech. The design
already relied on two more. `Cataclysm_GDD_v2.md` describes the Energy Leech
class as draining enemy mana to refill its own pool, and names leech as a
recovery stat group and as a suffix category. Neither mana leech nor energy
shield leech existed as a stat, so neither could be granted by anything and the
class could not be geared for.

A SECOND GAP UNDERNEATH IT. Nothing anywhere said what leech DOES. The stat
existed, one class had 3% of it, an enchantment doubled it, and no document
stated whether it was a share of damage or a flat amount, whether it arrived at
once or over time, or whether overkill damage counted. Adding two more undefined
stats would have made that worse. The rule is now written in the "Leech" section
of `docs/Cataclysm_GDD_v2.md` and this file holds the code to it.

WHAT IS ASSERTED HERE, and where each thing lives:

    docs/Cataclysm_GDD_v2.md        the rule, and the character sheet's stat count
    sim/cataclysm_sim/character.py  the three stats, in the Recovery group
    sim/cataclysm_sim/affixes.py    the three affixes and LEECH_PAYOUT_SECONDS
    docs/All_Things_Cataclysm.xlsx  the Affixes sheet, which is authoritative
    game/Data/Affixes.csv           generated from it, and what the game loads

THE PAYOUT PERIOD IS READ, NOT REPEATED. The number of seconds is parsed out of
the design document's rules table and compared with the constant, so the two
cannot drift.

WHAT IS NOT ASSERTED. No C++ attribute is checked. The Gameplay Ability System
attribute set has `LifeLeech` and does not yet have the other two; that is
implementation and is tracked on #214 rather than pinned here, because a test
that fails until unwritten code exists is not a guard.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
AFFIX_CSV = REPO_ROOT / "game" / "Data" / "Affixes.csv"

SECTION = "### **Leech**"

#: The three leech stats and the pool each one fills. A leech stat that named a
#: pool the character sheet does not have would recover into nothing.
LEECH_STATS: dict[str, str] = {
    "life_leech": "max_health",
    "mana_leech": "max_mana",
    "energy_shield_leech": "max_energy_shield",
}


def section_text() -> str:
    """The "Leech" section of the design document, bounded by the next heading."""
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    body = GDD.read_text(encoding="utf-8")
    start = body.find(SECTION)
    assert start != -1, f"{GDD.name} no longer has a section headed {SECTION!r}"
    after = body[start + len(SECTION):]
    ends = [m.start() for m in re.finditer(r"^#{1,6} ", after, re.MULTILINE)]
    return after[:ends[0]] if ends else after


@pytest.fixture(scope="module")
def section() -> str:
    return section_text()


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import affixes as af
    return af


@pytest.fixture(scope="module")
def sheet():
    from cataclysm_sim import character as ch
    return ch


@pytest.fixture(scope="module")
def affix_rows() -> list[dict[str, str]]:
    if not AFFIX_CSV.is_file():
        pytest.skip("Affixes.csv not present")
    with AFFIX_CSV.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


class TestTheDesignDocumentDefinesLeech:
    """Before #214 none of these sentences existed anywhere."""

    def test_leech_is_a_share_of_damage_dealt(self, section):
        assert "**Leech is a percentage of the damage actually dealt.**" in section

    def test_overkill_does_not_count(self, section):
        assert "**Overkill does not count.**" in section

    def test_it_is_not_instant(self, section):
        assert "not instantly" in section, (
            "the design document no longer says leech arrives over time. Instant "
            "leech makes a character that is winning unkillable and does nothing "
            "for one that is losing.")

    def test_the_sources_the_shape_came_from_are_named(self, section):
        """CLAUDE.md requires the games a shape was read off to be recorded."""
        assert "Last Epoch" in section and "Path of Exile" in section


class TestThePayoutPeriodIsWrittenOnce:
    def test_the_document_and_the_constant_agree(self, section, model):
        """The number of seconds is parsed out of the rules table.

        Writing it in both places is how the two drift. `scoring.py` in this
        project drifted twice from a copy for exactly that reason.
        """
        match = re.search(r"\|\s*Payout period\s*\|\s*(\d+(?:\.\d+)?) seconds",
                          section)
        assert match, (
            "the Leech section's rules table has no 'Payout period' row giving "
            "a number of seconds")
        assert float(match.group(1)) == model.LEECH_PAYOUT_SECONDS

    def test_it_is_a_real_period(self, model):
        assert model.LEECH_PAYOUT_SECONDS > 0.0


class TestTheThreeStatsExist:
    def test_all_three_are_on_the_character_sheet(self, sheet):
        missing = sorted(set(LEECH_STATS) - set(sheet.ALL_STATS))
        assert not missing, missing

    def test_all_three_are_recovery_stats(self, sheet):
        recovery = set(sheet.STAT_GROUPS["Recovery"])
        assert set(LEECH_STATS) <= recovery, sorted(set(LEECH_STATS) - recovery)

    def test_each_one_fills_a_pool_that_exists(self, sheet):
        """A leech stat recovering into a pool the sheet does not have does
        nothing, and nothing else would say so."""
        for leech, pool in LEECH_STATS.items():
            assert pool in sheet.ALL_STATS, f"{leech} fills {pool}, which is not a stat"

    def test_the_design_document_agrees_on_the_stat_count(self, sheet):
        """The Character Sheet section states how many stats there are.

        A stat added to the model and not to the document leaves the document
        describing a character that no longer exists.
        """
        if not GDD.is_file():
            pytest.skip("the design document is not present")
        body = GDD.read_text(encoding="utf-8")
        match = re.search(r"A character has (\d+) stats", body)
        assert match, f"{GDD.name} no longer states how many stats a character has"
        assert int(match.group(1)) == len(sheet.ALL_STATS)


class TestTheThreeAffixesExist:
    def test_there_is_one_affix_per_leech_stat(self, model):
        granted = {a.stat for a in model.LEECH_AFFIXES}
        assert granted == set(LEECH_STATS), (
            f"only in LEECH_AFFIXES: {sorted(granted - set(LEECH_STATS))}; "
            f"only in this file: {sorted(set(LEECH_STATS) - granted)}")

    def test_every_leech_affix_is_in_the_pool(self, model):
        for affix in model.LEECH_AFFIXES:
            assert affix in model.AFFIX_POOL, affix.name

    def test_the_pool_holds_no_leech_affix_outside_that_group(self, model):
        """`LEECH_AFFIXES` is what the rules above are written against.

        An affix granting a leech stat and not in that tuple would escape every
        check in this class.
        """
        in_pool = {a for a in model.AFFIX_POOL if a.stat in LEECH_STATS}
        assert in_pool == set(model.LEECH_AFFIXES), sorted(
            a.name for a in in_pool - set(model.LEECH_AFFIXES))

    def test_all_three_are_suffixes(self, model):
        """Leech says how much comes back per hit, not how big a number is,
        which is what the prefix and suffix split means."""
        for affix in model.LEECH_AFFIXES:
            assert affix.position == model.SUFFIX, affix.name

    def test_all_three_roll_where_a_hit_comes_from(self, model):
        for affix in model.LEECH_AFFIXES:
            assert affix.allowed_slots == model.OFFENSIVE_SLOTS, affix.name

    def test_all_three_are_flat_and_worth_the_same(self, model):
        """One value for the family, which is a judgement recorded in
        DECISIONS.md rather than a derivation: leech is a percentage of the same
        damage number in all three cases, so what differs is only which pool it
        fills."""
        values = {a.top_value for a in model.LEECH_AFFIXES}
        kinds = {a.kind for a in model.LEECH_AFFIXES}
        assert len(values) == 1, {a.name: a.top_value for a in model.LEECH_AFFIXES}
        assert kinds == {"flat"}, kinds

    def test_they_reach_the_table_the_game_loads(self, affix_rows, model):
        by_name = {row["AffixName"].strip(): row for row in affix_rows}
        for affix in model.LEECH_AFFIXES:
            assert affix.name in by_name, (
                f"{affix.name} is in the model and not in {AFFIX_CSV.name}. "
                "Run tools/generate_datatables.py.")
            row = by_name[affix.name]
            assert row["Stat"].strip() == affix.stat, affix.name
            assert float(row["TopValue"]) == pytest.approx(affix.top_value), affix.name
            assert row["Position"].strip() == affix.position, affix.name
