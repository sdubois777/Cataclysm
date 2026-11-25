"""The affix range proposal holds its own rules, and applies only to a copy.

`tools/affix_range_proposal.py` is a PROPOSAL for the project owner's ruling of
2026-11-24 ("I really hate decimals ... each affix tier should have it's own
range ... something a PLAYER finds reasonable"), redone under the owner's
rulings on issue #2883: upgrades multiply every affix, tiers never overlap,
the small stats move to fewer slots, and flat damage keeps six reference slots
and grows with upgrades. Nothing in the game reads it. These tests hold three
things:

  1. the proposed table obeys the rule it states: whole numbers, seven
     contiguous non-overlapping tiers of at least two values, and at every
     upgrade level from +0 to +10, one- and two-handed, no decimal, no number
     shown twice, every tier above the one below;
  2. the checks that say so can fail, shown by breaking a table, or the
     rounding, on purpose;
  3. the apply step writes a copy and refuses the real design workbook, which
     other branches hold unmerged edits to.
"""

from __future__ import annotations

import csv
import dataclasses
import hashlib
import shutil
from fractions import Fraction

import pytest

import affix_range_proposal as p


@pytest.fixture(scope="module")
def rows():
    return p.read_affix_rows()


@pytest.fixture(scope="module")
def table(rows):
    return p.proposal(rows)


def _live(table):
    return [a for a in table.values() if a.kind != "Hybrid" and not a.retired]


# ---------------------------------------------------------------------------
# What today's affixes are, which is what the owner complained about
# ---------------------------------------------------------------------------

def test_today_the_owners_example_is_a_tier_1_increase_on_a_fresh_drop(rows):
    """"Like 1.2%": Increased maximum health's worst tier 1 roll at +0."""
    by_name = {a.name: a for a in p.today(rows)}
    t1 = by_name["Increased maximum health"].tiers[0]
    assert t1.low0 == pytest.approx(1.2)
    assert t1.high0 == pytest.approx(1.6)


def test_today_every_affix_shows_a_decimal_somewhere_and_overlaps(rows):
    """Every one of the 73 single-stat, resistance and ailment affixes shows a
    decimal at some tier and upgrade level, and every one has adjacent tiers
    that overlap at +0, by design (the design document's "Bands overlap
    between adjacent tiers")."""
    today = p.today(rows)
    assert len(today) == 73
    assert all(a.has_decimal for a in today)
    assert all(a.overlaps >= 1 for a in today)


# ---------------------------------------------------------------------------
# The upgrade step
# ---------------------------------------------------------------------------

def test_an_upgrade_rounds_exactly_and_a_half_goes_up():
    """10 x 1.15 is 11.5, which a double holds as 11.499999999999998 and a
    naive round would send to 11."""
    assert p.upgraded(10, 1) == 12
    assert p.upgraded(1, 10) == 3          # 2.5
    assert p.upgraded(21, 10) == 53        # 52.5
    assert p.upgraded(48, 10) == 120
    assert p.upgraded(7, 3, two_handed=True) == 20   # 7 x 1.45 x 2 = 20.3


def test_every_upgrade_level_is_the_plus_zero_number_times_its_multiplier():
    """Rounded once from the +0 number, never from the level below."""
    for base in range(1, 401):
        for level in p.LEVELS:
            for hands in (1, 2):
                exact = Fraction(base * (100 + 15 * level) * hands, 100)
                expected = int(exact + Fraction(1, 2))   # floor(x + 1/2), x > 0
                assert p.upgraded(base, level, two_handed=hands == 2) == expected


def test_a_whole_number_below_seven_is_the_only_kind_an_upgrade_can_leave_unchanged(table):
    """One upgrade adds 0.15 of the +0 number, which is under one only below
    seven. Information for decision D7, and held here so the claim in the
    proposal cannot drift from the table."""
    steps = p.unchanged_steps(table)
    assert steps and max(steps) < 7
    numbers = {v for a in _live(table) for lo, hi in a.tiers for v in range(lo, hi + 1)}
    for v in numbers:
        if v >= 7:
            shown = [p.upgraded(v, g) for g in p.LEVELS]
            assert len(set(shown)) == len(shown), v


# ---------------------------------------------------------------------------
# The proposal's rule
# ---------------------------------------------------------------------------

def test_the_proposal_has_no_problems(table, rows):
    assert p.problems(table, rows) == []


def test_every_live_affix_has_seven_whole_contiguous_tiers_of_two_or_more(table):
    for affix in _live(table):
        assert len(affix.tiers) == 7, affix.name
        for k, (low, high) in enumerate(affix.tiers):
            assert isinstance(low, int) and isinstance(high, int), affix.name
            assert high - low + 1 >= 2, (affix.name, k + 1)
            if k:
                assert low == affix.tiers[k - 1][1] + 1, (affix.name, k + 1)


def test_no_number_is_shown_twice_at_any_upgrade_level(table):
    for affix in _live(table):
        for level in p.LEVELS:
            for two_handed in (False, True):
                every = [n for k in p.TIERS
                         for n in p.shown(affix, k, level, two_handed)]
                assert len(every) == len(set(every)), (affix.name, level, two_handed)
                assert every == sorted(every), (affix.name, level, two_handed)
                assert all(isinstance(n, int) for n in every)


def test_every_affix_grows_with_upgrades(table):
    """The owner's ruling on issue #2883: "upgrade everything"."""
    for affix in _live(table):
        assert affix.top == p.upgraded(affix.tiers[-1][1], 10)
        assert p.value(affix, 7, 1.0, 10) > p.value(affix, 7, 1.0, 0), affix.name


def test_the_figures_the_proposal_keeps(table):
    """The endgame figures `docs/Cataclysm_GDD_v2.md` derives that have room
    already, unmoved."""
    for name in ("Increased damage", "Increased spell damage", "Increased minion damage"):
        assert table[name].top == 125
    for cataclysm in ("War", "Demonic", "Death", "Pestilence", "Famine",
                      "Celestial", "Chaos", "Void"):
        assert table[f"Increased damage against {cataclysm} enemies"].top == 400
    assert table["Flat maximum health"].top == 120
    assert table["Flat armor"].top == 250
    assert table["Flat maximum energy shield"].top == 50


def test_flat_damage_grows_with_upgrades_on_the_owners_six_slots(table):
    """"keep the 6 slots and upgrade the damage": flat damage is an ordinary
    upgraded ladder, and the reference build still spends six slots on it."""
    from cataclysm_sim import affixes as af
    damage = table["Flat damage"]
    assert damage.tiers[-1][1] == 15 and damage.top == 38
    assert af.REFERENCE_FLAT_DAMAGE_AFFIXES == 6


def test_a_raise_is_only_to_the_smallest_top_with_room_or_a_rounding(table, rows):
    """Where a top moved without a stated reason, the ladder is exactly the
    fourteen-value minimum, or the move is the nearest whole +0 number."""
    for affix in _live(table):
        if not affix.raised or affix.name in p.OVERRIDES:
            continue
        low, high = affix.tiers[0][0], affix.tiers[-1][1]
        assert (high - low + 1 == p.MIN_LADDER_VALUES
                or abs(affix.top - affix.old_top) <= 1), affix.name


def test_a_narrowed_stat_stays_near_its_ceiling(table):
    """The owner's ruling "Fewer slots": the top and the slot list move
    together. The piece count is the one whose ceiling is nearest today's,
    rounded down where a cap is close (damage reduction, evasion)."""
    d5 = {"Flat critical strike chance", "Flat mana regeneration"}
    narrowed = [a for a in _live(table) if a.slots != a.old_slots and a.name not in d5]
    assert len(narrowed) == 12
    for affix in narrowed:
        before = affix.old_top * affix.old_pieces()
        after = affix.top * affix.pieces()
        assert 0.6 <= after / before <= 1.3, (affix.name, before, after)


def test_an_ailment_affix_never_beats_its_gem_on_one_hand(table, rows):
    """"The gem stays the stronger source." Held one-handed; on a two-handed
    weapon two gems (madness, withering, 60%) fall below a doubled affix, which
    the proposal states as a rule that moves."""
    path = p.REPO_ROOT / "game" / "Data" / "Gems.csv"
    with open(path, newline="", encoding="utf-8") as handle:
        gems = {r["GemName"]: r for r in csv.DictReader(handle)}
    for row in rows:
        if row["AffixKind"] == "Ailment" and row["Gem"] in gems:
            best_gem = round(float(gems[row["Gem"]]["Cataclysmic"]) * 100)
            assert table[row["AffixName"]].top < best_gem, row["AffixName"]


def test_the_owners_d5_keeps_flat_crit_and_flat_mana_regeneration(table):
    """"Keep them, but make the ranges like 1-2, 3-4, etc. ... flat crit only
    rolls on weapons and gloves, and flat mana regen only rolls on belts and
    ammys." Nothing is retired, and every hybrid still rolls somewhere."""
    assert not [a.name for a in table.values() if a.retired]
    two_wide = ((1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14))
    crit = table["Flat critical strike chance"]
    regen = table["Flat mana regeneration"]
    assert crit.tiers == two_wide and set(crit.slots) == {"Weapon", "Gloves"}
    assert regen.tiers == two_wide and set(regen.slots) == {"Belt", "Necklace"}
    for hybrid in table.values():
        if hybrid.kind == "Hybrid":
            assert hybrid.slots, hybrid.name


def test_tier_one_starts_follow_the_owners_order(table, rows):
    """D7: "% increases definitely need to be significantly higher than 1%",
    and the owner's correction: "flat crit, is added. It isn't a %." An
    increase starts at 8 at the least; a flat stat -- an amount, or points
    added to a base chance -- starts at today's worst +0 roll, rounded, at
    least 1."""
    af = p._sim()
    assert p.MIN_START == {"increase": 8, "points": 1, "amount": 1}
    for row in rows:
        affix = table[row["AffixName"]]
        if affix.kind == "Hybrid":
            continue
        if p.number_kind(row) == "increase":
            assert affix.tiers[0][0] >= 8, affix.name
        else:
            worst = max(float(row["TopValue"]) * af.WORST_MULTIPLIER,
                        float(row["Floor"] or 0.0))
            assert affix.tiers[0][0] == max(p.round_half_up(worst), 1), affix.name


def test_the_owners_flat_crit_example_reads_as_written(table):
    """"if I say flat crit +1, that means my flat 5% chance to crit goes up to
    6%": flat crit chance adds points, starting at 1-2 on a fresh drop."""
    assert table["Flat critical strike chance"].tiers[0] == (1, 2)
    assert p.number_kind({"AffixKind": "Stat", "ValueKind": "flat", "Percent": "True",
                          "AffixName": "Flat critical strike chance"}) == "points"


def test_increased_sits_above_flat_except_where_flat_is_a_pool(table):
    """Stat by stat, the increased affix starts and tops above the flat one,
    except flat health and flat armour, whose numbers are amounts on their own
    scale (a level 100 character has thousands of each)."""
    rows = p.scale_order()
    assert len(rows) == 10
    exceptions = {r["stat"] for r in rows if not r["ordered"]}
    assert exceptions == {"max_health", "armor"}
    assert all(r["flat is an amount"] for r in rows if not r["ordered"])


# ---------------------------------------------------------------------------
# The checks can fail
# ---------------------------------------------------------------------------

def _with(table, name, **changes):
    broken = dict(table)
    broken[name] = dataclasses.replace(table[name], **changes)
    return broken


def test_the_check_fires_on_an_overlapping_tier(table, rows):
    tiers = list(table["Flat maximum health"].tiers)
    tiers[3] = (tiers[3][0] - 1, tiers[3][1])
    found = p.problems(_with(table, "Flat maximum health", tiers=tuple(tiers)), rows)
    assert any("Flat maximum health T4" in line for line in found)


def test_the_check_fires_on_a_single_value_tier(table, rows):
    tiers = list(table["Flat block chance"].tiers)
    tiers[0] = (2, 2)
    found = p.problems(_with(table, "Flat block chance", tiers=tuple(tiers)), rows)
    assert any("holds one value" in line for line in found)


def test_the_check_fires_when_an_upgrade_level_repeats_a_number(table, rows, monkeypatch):
    """A rounding that loses ground (nine tenths, rounded down) makes two +0
    numbers show the same thing, and the per-level check names it."""
    monkeypatch.setattr(p, "upgraded",
                        lambda base, level, two_handed=False: (base * 9) // 10)
    found = p.problems(table, rows)
    assert any("a number repeats at +0" in line for line in found)
    assert any("reaches" in line and "at +0" in line for line in found)


def test_the_check_fires_when_an_upgrade_lowers_a_roll(table, rows, monkeypatch):
    """A rounding that falls as the level rises would take a roll down."""
    monkeypatch.setattr(p, "upgraded",
                        lambda base, level, two_handed=False:
                        base * 3 - (level % 2) * base)
    found = p.problems(table, rows)
    assert any("an upgrade lowers a roll" in line for line in found)


def test_every_number_a_range_shows_can_roll_and_upgrading_never_lowers_one(table):
    """The roll is a position in the range at the piece's level, so a tooltip's
    range never shows a number that cannot occur."""
    for affix in _live(table):
        for tier in p.TIERS:
            for level in (0, 1, 5, 10):
                low, high = p.tier_range(affix, tier, level)
                count = high - low + 1
                hit = {p.value(affix, tier, (i + 0.5) / count, level)
                       for i in range(count)}
                assert hit == set(range(low, high + 1)), (affix.name, tier, level)
            for roll in (0.0, 0.3, 0.6, 1.0):
                seen = [p.value(affix, tier, roll, g) for g in p.LEVELS]
                assert seen == sorted(seen), (affix.name, tier, roll)


def test_the_check_fires_when_a_hybrid_rolls_where_a_half_cannot(table, rows):
    found = p.problems(_with(table, "Armor and evasion",
                             slots=("Boots", "Chest", "Head")), rows)
    assert any("Armor and evasion: rolls where" in line for line in found)


def test_the_check_fires_when_a_slot_runs_out_of_suffixes(table, rows):
    broken = dict(table)
    for affix in table.values():
        if "Boots" in affix.slots and affix.kind != "Hybrid":
            broken[affix.name] = dataclasses.replace(
                affix, slots=tuple(s for s in affix.slots if s != "Boots") or ("Ring",))
    found = p.problems(broken, rows)
    assert any(line.startswith("Boots: only") for line in found)


# ---------------------------------------------------------------------------
# Pieces of the rule
# ---------------------------------------------------------------------------

def test_a_half_rounds_up():
    assert p.round_half_up(52.5) == 53
    assert p.round_half_up(37.5) == 38
    assert p.round_half_up(2.4) == 2


def test_every_whole_number_in_a_tier_is_equally_likely():
    """The enchantment rule: 1000 evenly spaced rolls over 10-14 land on each
    of the five values 200 times."""
    counts = {}
    for i in range(1000):
        v = p.roll_value(10, 14, i / 1000)
        counts[v] = counts.get(v, 0) + 1
    assert counts == {10: 200, 11: 200, 12: 200, 13: 200, 14: 200}
    assert p.roll_value(10, 14, 1.0) == 14
    assert p.roll_value(10, 14, 0.0) == 10


def test_the_ladder_refuses_fewer_than_fourteen_values():
    with pytest.raises(ValueError):
        p.ladder(3, 15)
    assert len(p.ladder(3, 16)) == 7


def test_the_ladder_starts_tier_seven_at_three_quarters_of_the_top():
    tiers = p.ladder(13, 50)
    assert tiers[-1] == (38, 50)
    assert tiers[0][0] == 13


def test_a_hybrid_half_is_its_own_affix_two_tiers_down(table):
    health = table["Flat maximum health"]
    assert p.hybrid_half(health, 7, 1.0, 10) == p.value(health, 5, 1.0, 10)
    assert p.hybrid_half(health, 3, 0.0, 0) == p.value(health, 1, 0.0, 0)
    with pytest.raises(ValueError):
        p.hybrid_half(health, 2)


def test_every_hybrid_share_brackets_the_designs_seventy_percent(table):
    """0.59 to 0.81 against the design's 0.70: two-wide ladders put tier 5
    nearer tier 7 than a geometric one does."""
    shares = [top for top, _ in p.hybrid_shares(table).values()]
    assert min(shares) >= 0.55
    assert max(shares) <= 0.85


def test_todays_seventy_percent_rule_would_break_the_new_rules(table):
    """The reason the proposal moves a hybrid two tiers down instead."""
    assert len(p.seventy_percent_collisions(table)) > 0


def test_every_implicit_is_whole_and_at_least_one():
    for implicit in p.implicit_proposal():
        assert isinstance(implicit.at_zero, int) and implicit.at_zero >= 1
        for level in p.LEVELS:
            assert isinstance(implicit.at(level), int)


def test_the_levels_table_shows_every_live_affix_once(table):
    text = p.levels_markdown()
    for affix in _live(table):
        assert text.count(affix.name) >= 1, affix.name
    assert text.count("| +10 |") == len(p._ladder_letters(table))


def test_the_simulation_is_restored_after_it_is_measured():
    af = p._sim()
    before = af.FLAT_DAMAGE.value_at(3, 0.5, 4)
    original = af.StatAffix.value_at
    with p.simulated():
        assert af.FLAT_DAMAGE.value_at(3, 0.5, 4) != before
    assert af.StatAffix.value_at is original
    assert af.FLAT_DAMAGE.value_at(3, 0.5, 4) == before


def test_the_measurement_keeps_the_damage_target():
    """The target is read off enemy health, which no affix moves."""
    figures = p.measure()
    target = figures["damage target at tier 8"]
    assert target["today"] == target["proposed"]


def test_the_command_line_check_passes():
    assert p.main(["check"]) == 0


# ---------------------------------------------------------------------------
# Applying it, to a copy only
# ---------------------------------------------------------------------------

def _digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


@pytest.fixture
def workbook_copy(tmp_path):
    if not p.WORKBOOK.is_file():
        pytest.skip("design workbook not present")
    source = tmp_path / "source.xlsx"
    shutil.copyfile(p.WORKBOOK, source)
    return source


def test_apply_refuses_the_real_workbook(workbook_copy, monkeypatch):
    """Pointed at a stand-in, so this test cannot touch the real file even if
    the refusal were broken."""
    monkeypatch.setattr(p, "WORKBOOK", workbook_copy)
    before = _digest(workbook_copy)
    with pytest.raises(p.RealWorkbookRefused):
        p.apply(workbook_copy, workbook_copy)
    assert _digest(workbook_copy) == before
    assert p.main(["apply", "--source", str(workbook_copy),
                   "--out", str(workbook_copy)]) == 2
    assert _digest(workbook_copy) == before


def test_apply_writes_the_table_into_a_copy(workbook_copy, tmp_path, table):
    import openpyxl

    out = tmp_path / "applied.xlsx"
    real_before = _digest(p.WORKBOOK)
    counts = p.apply(workbook_copy, out)
    assert _digest(p.WORKBOOK) == real_before

    assert counts["retired rows"] == 0
    assert counts["tier rows"] == len(_live(table)) == 73

    book = openpyxl.load_workbook(out, data_only=True)
    sheet = book["Affixes"]
    rows = list(sheet.iter_rows(values_only=True))
    headers = list(rows[0])
    for column in p.TIER_COLUMNS:
        assert column in headers
    index = {h: i for i, h in enumerate(headers)}
    seen = set()
    for raw in rows[1:]:
        name = raw[index["Affix Name"]]
        if not name:
            continue
        seen.add(name)
        affix = table[name]
        assert not affix.retired
        if affix.kind == "Hybrid":
            assert (raw[index["Hybrid Part 1"]], raw[index["Hybrid Part 2"]]) == \
                affix.hybrid_parts
            continue
        got = tuple((raw[index[f"T{t} Min"]], raw[index[f"T{t} Max"]])
                    for t in p.TIERS)
        assert got == affix.tiers, name
        assert set(s.strip() for s in raw[index["Allowed Slots"]].split(",")) == \
            set(affix.slots), name
    assert "Flat critical strike chance" in seen
    assert "Flat mana regeneration" in seen


def test_apply_leaves_every_other_sheet_as_it_was(workbook_copy, tmp_path):
    import openpyxl

    out = tmp_path / "applied.xlsx"
    p.apply(workbook_copy, out)
    before = openpyxl.load_workbook(workbook_copy, data_only=True)
    after = openpyxl.load_workbook(out, data_only=True)
    assert before.sheetnames == after.sheetnames
    for title in before.sheetnames:
        if title in ("Affixes", "Item Bases"):
            continue
        old = list(before[title].iter_rows(values_only=True))
        new = list(after[title].iter_rows(values_only=True))
        assert old == new, title
    # Item Bases gains two columns and changes nothing it had.
    old = list(before["Item Bases"].iter_rows(values_only=True))
    new = list(after["Item Bases"].iter_rows(values_only=True))
    width = len(old[0])
    assert [r[:width] for r in new] == old
