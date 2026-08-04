"""Maximum resistance: only enchantments raise it, and 90% is the ceiling.

WHY THIS EXISTS. Issue #215. The project owner raised that maximum resistance is
worth having and cannot be an ordinary affix, because every affix has seven tiers
and can appear on several pieces, and that range is far too wide for a modifier
that is multiplicative with every other defensive stat.

TWO THINGS ARE EASY TO CONFUSE AND THE DESIGN DOCUMENT USED TO CONFUSE THEM.

    over-capping         having more than 70% resistance. Any resistance affix
                         does it, and it is worth having because penetration and
                         Overwhelm are subtracted before the cap is applied.
    raising the maximum  moving the 70% itself, so more of a hit is stopped.

The Caps table in `docs/Cataclysm_GDD_v2.md` read "Soft. Affixes may raise the
cap itself", which states the second and means the first. No affix raises the
cap. One enchantment does.

WHAT WAS ALREADY TRUE BEFORE #215, and what the issue asked to confirm. The
placement is already an enchantment in the data: `game/Data/EnchantmentsPositive.csv`
has "You have +10 maximum resists" at weight 1, the rarest and most powerful
tier, and `game/Data/EnchantmentsNegative.csv` has three that lower the maximum.
So the recommendation the issue makes is what the design already does. What was
missing is a ceiling, and the rule that no affix may do it.

WHAT WAS DECIDED. The maximum is hard capped at 90%. Without a ceiling, stacking
reaches immunity: damage taken is proportional to 100% minus resistance, so 70 to
80 removes a third of what still gets through, 80 to 90 removes half of what is
left, and 100 removes all of it. 90 is Path of Exile's figure, which caps
resistances at 75% and hard caps the maximum at 90% for exactly this reason.

WHAT IS ASSERTED HERE.

    the design document states both numbers and the model matches them
    no affix in any of the three copies of the pool raises the maximum
    the enchantment that does exists, is positive, and is weight 1
    two of that enchantment reach the ceiling and no single one overshoots it
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
AFFIX_CSV = REPO_ROOT / "game" / "Data" / "Affixes.csv"
POSITIVE_CSV = REPO_ROOT / "game" / "Data" / "EnchantmentsPositive.csv"
NEGATIVE_CSV = REPO_ROOT / "game" / "Data" / "EnchantmentsNegative.csv"

SECTION = "### **Maximum Resistance**"

#: What an enchantment's text looks like when it touches the maximum. Matches
#: "maximum resists", "max resistances" and the singular of each, so a rewording
#: does not quietly drop an enchantment out of these checks.
TOUCHES_THE_MAXIMUM = re.compile(r"max(?:imum)? resist", re.IGNORECASE)

#: The rarest and most powerful enchantment tier. `Cataclysm_GDD_v2.md`: "Weight
#: 1 enchantments are rare and very powerful. Weight 4 enchantments are common
#: and modest."
RAREST_WEIGHT = 1.0


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    if not path.is_file():
        pytest.skip(f"{path.name} not present")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def section_text() -> str:
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
def damage():
    from cataclysm_sim import damage as dmg
    return dmg


@pytest.fixture(scope="module")
def positives() -> list[dict[str, str]]:
    return read_csv(POSITIVE_CSV)


@pytest.fixture(scope="module")
def negatives() -> list[dict[str, str]]:
    return read_csv(NEGATIVE_CSV)


@pytest.fixture(scope="module")
def raise_the_maximum(positives) -> list[dict[str, str]]:
    return [r for r in positives if TOUCHES_THE_MAXIMUM.search(r["Effect"])]


class TestTheTwoNumbers:
    def test_the_document_and_the_model_agree_on_the_ceiling(self, section, damage):
        """The ceiling is written in the design document's figures table.

        Parsed rather than repeated, so the two cannot drift.
        """
        match = re.search(r"\|\s*Ceiling on the cap\s*\|\s*(\d+(?:\.\d+)?)%", section)
        assert match, (
            "the Maximum Resistance section's figures table has no 'Ceiling on "
            "the cap' row")
        assert float(match.group(1)) == damage.MAX_RESISTANCE_CEILING

    def test_the_document_and_the_model_agree_on_the_base_cap(self, section, damage):
        match = re.search(r"\|\s*Base cap\s*\|\s*(\d+(?:\.\d+)?)%", section)
        assert match, "the figures table has no 'Base cap' row"
        assert float(match.group(1)) == damage.RESISTANCE_CAP

    def test_the_ceiling_is_above_the_cap_and_below_immunity(self, damage):
        """At 100 a character takes nothing, whatever hits them. That is the
        state the ceiling exists to keep out of reach."""
        assert damage.RESISTANCE_CAP < damage.MAX_RESISTANCE_CEILING < 100.0


class TestOnlyEnchantmentsRaiseIt:
    """The rule the design document now states, checked against all three copies
    of the affix pool."""

    def test_no_affix_in_the_table_the_game_loads(self):
        rows = read_csv(AFFIX_CSV)
        offenders = sorted(r["AffixName"] for r in rows
                           if TOUCHES_THE_MAXIMUM.search(r["AffixName"]))
        assert not offenders, (
            f"{AFFIX_CSV.name} has affixes touching maximum resistance: "
            f"{offenders}. The design document allows only enchantments to raise "
            "it, because seven affix tiers across several pieces is far too wide "
            "a range for a modifier multiplicative with every other defence.")

    def test_no_affix_in_the_workbook(self):
        import openpyxl

        if not WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
        sheet = book["Affixes"]
        rows = list(sheet.iter_rows(values_only=True))
        headers = [str(h).strip() if h is not None else "" for h in rows[0]]
        name_at = headers.index("Affix Name")
        offenders = sorted(str(r[name_at]) for r in rows[1:]
                           if r and r[name_at]
                           and TOUCHES_THE_MAXIMUM.search(str(r[name_at])))
        assert not offenders, offenders

    def test_no_affix_in_the_model(self):
        from cataclysm_sim import affixes as af

        names = ([a.name for a in af.AFFIX_POOL]
                 + [f.name for f in af.RESISTANCE_FAMILIES]
                 + [h.name for h in af.HYBRID_AFFIXES])
        offenders = sorted(n for n in names if TOUCHES_THE_MAXIMUM.search(n))
        assert not offenders, offenders

    def test_the_design_document_states_the_rule(self, section):
        assert "**Only enchantments raise the maximum. No affix may.**" in section

    def test_the_design_document_separates_over_capping_from_raising_it(self, section):
        """The two were conflated in the Caps table and that is what produced
        the report. The section has to keep them apart or it will happen again."""
        assert "Over-capping" in section and "Raising the maximum" in section

    def test_the_caps_table_no_longer_credits_affixes(self):
        """The Caps table used to read "Soft. Affixes may raise the cap itself"."""
        if not GDD.is_file():
            pytest.skip("the design document is not present")
        body = GDD.read_text(encoding="utf-8")
        assert "Soft. Affixes may raise the cap itself." not in body


class TestTheEnchantmentThatDoesIt:
    def test_exactly_one_positive_enchantment_raises_the_maximum(self,
                                                                raise_the_maximum):
        assert len(raise_the_maximum) == 1, (
            [r["Effect"] for r in raise_the_maximum])

    def test_it_is_the_rarest_and_most_powerful_weight(self, raise_the_maximum):
        """Weight 1. `Cataclysm_GDD_v2.md`: weight 1 enchantments are rare and
        very powerful, weight 4 common and modest. A modifier this strong at a
        common weight would be on most Legendary items."""
        assert float(raise_the_maximum[0]["Weight"]) == RAREST_WEIGHT

    def test_it_is_not_marked_negative(self, raise_the_maximum):
        assert raise_the_maximum[0]["IsNegative"].strip().lower() == "false"

    def test_it_is_tagged_as_a_resistance_modifier(self, raise_the_maximum):
        """The tag is what decides which items it can roll on."""
        assert "Stat.Defense.Resist" in raise_the_maximum[0]["Tags"]

    def test_one_roll_does_not_reach_the_ceiling_and_two_do(self, raise_the_maximum,
                                                            damage):
        """The property the ceiling is for: reachable, and then wasted.

        A single enchantment that reached the ceiling on its own would make the
        ceiling the only thing that mattered about it. One that could never
        reach it would make the ceiling decoration.
        """
        match = re.search(r"\+(\d+(?:\.\d+)?)", raise_the_maximum[0]["Effect"])
        assert match, (
            f"cannot read a value out of {raise_the_maximum[0]['Effect']!r}")
        granted = float(match.group(1))
        headroom = damage.MAX_RESISTANCE_CEILING - damage.RESISTANCE_CAP
        assert granted < headroom, "one enchantment reaches the ceiling by itself"
        assert granted * 2 >= headroom, "two enchantments still fall short"


class TestTheNegativeSideExists:
    """Recorded because it is what makes the positive one a real choice.

    An enchantment that only ever goes up is a strictly good roll. These are why
    a player can lose the maximum as well as gain it, and why the positive one
    is worth a slot.
    """

    def test_some_negative_enchantment_lowers_the_maximum(self, negatives):
        lowering = [r for r in negatives if TOUCHES_THE_MAXIMUM.search(r["Effect"])]
        assert lowering, (
            f"{NEGATIVE_CSV.name} no longer has any enchantment that lowers "
            "maximum resistance")

    def test_every_one_of_them_is_marked_negative(self, negatives):
        for row in negatives:
            if TOUCHES_THE_MAXIMUM.search(row["Effect"]):
                assert row["IsNegative"].strip().lower() == "true", row["Name"]
