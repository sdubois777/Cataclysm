"""The affix range proposal holds its own rules, and applies only to a copy.

`tools/affix_range_proposal.py` is a PROPOSAL for the project owner's ruling of
2026-11-24 ("I really hate decimals ... each affix tier should have it's own
range ... something a PLAYER finds reasonable"). Nothing in the game reads it.
These tests hold three things:

  1. the proposed table obeys the rule it states: whole numbers, seven
     contiguous non-overlapping tiers of at least two values, separate at every
     upgrade level and on a two-handed weapon, every slot still fillable;
  2. the checks that say so can fail, shown by breaking a table on purpose;
  3. the apply step writes a copy and refuses the real design workbook, which
     other branches hold unmerged edits to.
"""

from __future__ import annotations

import dataclasses
import hashlib
import shutil

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
    """The measurement the proposal answers. Every one of the 73 single-stat,
    resistance and ailment affixes shows a decimal at some tier and upgrade
    level, and every one has adjacent tiers that overlap at +0, by design (the
    design document's "Bands overlap between adjacent tiers")."""
    today = p.today(rows)
    assert len(today) == 73
    assert all(a.has_decimal for a in today)
    assert all(a.overlaps >= 1 for a in today)


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


def test_every_value_any_roll_can_produce_is_whole(table):
    for affix in _live(table):
        for tier in p.TIERS:
            for level in (0, 3, 7, 10):
                for roll in (0.0, 0.37, 0.5, 0.99, 1.0):
                    v = p.value(affix, tier, roll, level, two_handed=True)
                    assert isinstance(v, int), affix.name


def test_an_amount_scales_with_upgrades_and_a_percentage_does_not(table):
    health = table["Flat maximum health"]
    damage = table["Increased damage"]
    assert health.amount and not damage.amount
    assert p.value(health, 7, 1.0, 10) == 120
    assert p.value(health, 7, 1.0, 0) == 48
    assert p.value(damage, 7, 1.0, 10) == p.value(damage, 7, 1.0, 0) == 125


def test_the_rules_the_proposal_keeps_exactly(table):
    """The endgame figures `docs/Cataclysm_GDD_v2.md` derives, unmoved."""
    assert table["Increased damage"].top == 125
    assert table["Increased spell damage"].top == 125
    assert table["Increased minion damage"].top == 125
    for cataclysm in ("War", "Demonic", "Death", "Pestilence", "Famine",
                      "Celestial", "Chaos", "Void"):
        assert table[f"Increased damage against {cataclysm} enemies"].top == 400
    for lever in ("Increased damage over time",
                  "Increased damage over time frequency",
                  "Increased damage over time duration"):
        assert table[lever].top == 52
    assert table["Single resistance"].top == 40
    assert table["Two resistances"].top == 28
    assert table["Increased movement speed"].top == 35
    assert table["Flat maximum health"].top == 120
    assert table["Flat armor"].top == 250


def test_an_ailment_chance_is_still_five_above_its_gem(table, rows):
    """"Each of these is five points above the gem that applies the same
    effect." The tops are the ones today, so the rule survives."""
    for row in rows:
        if row["AffixKind"] == "Ailment":
            assert table[row["AffixName"]].top == round(float(row["TopValue"]))


def test_a_raise_is_only_ever_to_the_smallest_top_with_room(table, rows):
    """Where a top moved without a stated reason, it moved to exactly the
    fourteen-value minimum and no further."""
    by_name = {r["AffixName"]: r for r in rows}
    for affix in _live(table):
        if affix.raised and affix.name not in p.OVERRIDES:
            low, high = affix.tiers[0][0], affix.tiers[-1][1]
            assert high - low + 1 == p.MIN_LADDER_VALUES, affix.name
            assert by_name[affix.name]


def test_a_narrowed_stat_holds_its_ceiling_within_a_fifth(table):
    """The movement-speed rule: the top and the slot list move together, so
    the most a character can carry from the stat does not run away."""
    for affix in _live(table):
        if affix.slots == affix.old_slots:
            continue
        before = round(affix.old_top) * affix.old_pieces()
        after = affix.top * affix.pieces()
        assert 0.75 <= after / before <= 1.2, (affix.name, before, after)


def test_two_retired_affixes_name_live_ones_and_no_hybrid_uses_them(table):
    retired = {a.name: a.retired_into for a in table.values() if a.retired}
    assert retired == {
        "Flat critical strike chance": "Increased critical strike chance",
        "Flat mana regeneration": "Increased mana regeneration",
    }
    for hybrid in table.values():
        if hybrid.kind == "Hybrid":
            assert not set(hybrid.hybrid_parts or ()) & set(retired)


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


def test_the_check_fires_when_a_hybrid_rolls_where_a_half_cannot(table, rows):
    found = p.problems(_with(table, "Armor and evasion",
                             slots=("Boots", "Chest", "Head", "Ring")), rows)
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
    tiers = p.ladder(18, 125)
    assert tiers[-1] == (95, 125)
    assert tiers[0][0] == 18


def test_a_hybrid_half_is_its_own_affix_two_tiers_down(table):
    health = table["Flat maximum health"]
    assert p.hybrid_half(health, 7, 1.0, 10) == p.value(health, 5, 1.0, 10)
    assert p.hybrid_half(health, 3, 0.0, 0) == p.value(health, 1, 0.0, 0)
    with pytest.raises(ValueError):
        p.hybrid_half(health, 2)


def test_every_hybrid_share_brackets_the_designs_seventy_percent(table):
    shares = [top for top, _ in p.hybrid_shares(table).values()]
    assert min(shares) >= 0.5
    assert max(shares) <= 0.8


def test_todays_seventy_percent_rule_would_break_the_new_rules(table):
    """The reason the proposal moves a hybrid two tiers down instead."""
    assert len(p.seventy_percent_collisions(table)) > 0


def test_every_implicit_is_whole_and_at_least_one():
    for implicit in p.implicit_proposal():
        assert isinstance(implicit.at_zero, int) and implicit.at_zero >= 1
        for level in range(11):
            assert isinstance(implicit.at(level), int)


def test_the_simulation_is_restored_after_it_is_measured():
    af = p._sim()
    before = af.FLAT_DAMAGE.value_at(3, 0.5, 4)
    original = af.StatAffix.value_at
    with p.simulated():
        assert af.FLAT_DAMAGE.value_at(3, 0.5, 4) != before
    assert af.StatAffix.value_at is original
    assert af.FLAT_DAMAGE.value_at(3, 0.5, 4) == before


def test_the_measurement_keeps_the_damage_target(table):
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

    assert counts["retired rows"] == 2
    assert counts["tier rows"] == len(_live(table))

    book = openpyxl.load_workbook(out, data_only=True)
    sheet = book["Affixes"]
    rows = list(sheet.iter_rows(values_only=True))
    headers = list(rows[0])
    for column in (*p.TIER_COLUMNS, p.SCALES_COLUMN):
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
    assert "Flat critical strike chance" not in seen
    assert "Flat mana regeneration" not in seen


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
