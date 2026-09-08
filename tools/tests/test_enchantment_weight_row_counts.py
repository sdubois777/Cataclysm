"""How many enchantment rows are written at each weight, against how often that
weight is actually drawn.

WHY THIS EXISTS. Issue #1458. An enchantment's `Weight` is two things at once:
the strength tier that decides which drawback may be paired with it, and the
band the draw prices. Since
[#1457](https://github.com/sdubois777/Cataclysm/pull/1457) the 1/4/16/64 step
prices the whole BAND rather than the individual row, so the four weights come
up at 1.2%, 4.7%, 18.8% and 75.3% whatever the sheet holds.

The sheet held the opposite shape. Measured on 2026-09-07, before the
redistribution this file guards:

    benefit rows   39, 154, 113, 28   for weights 1 to 4 on a chest piece
    drawback rows  22,  80,  58, 22

**The band the player met most often was the smallest pool written.** Weight 4
carried 75.3% of benefit appearances out of 28 rows while weight 2 carried 4.7%
out of 154, so one written weight 4 row was drawn 88 times as often as one
written weight 2 row, and a player who inspected 40 pairs met about 30 weight 4
benefits drawn from 28 rows. Repeats started immediately.

WHAT THIS GUARDS, AND WHAT IT DELIBERATELY DOES NOT. It does not check the
ladder's steepness against a number written here. The step of four is the
project owner's ruling of 2026-09-07 and the shares follow from it, so this file
READS the step out of `CataclysmDropRoll.h` and measures the row counts against
whatever ladder that gives. Change the step and these checks re-aim themselves;
they only ever complain about the row counts, which is what #1458 is about.

    every weight band has rows written at it
    a band drawn more often never holds fewer rows than a band drawn less often
    no single written row carries more than twice its flat share of the draws

THE THIRD IS THE ONE WITH A NUMBER IN IT, and the number is a bound rather than
a target. A row's flat share is what it would be drawn at if every row on its
side were equally likely -- one in 337 for benefits, one in 182 for drawbacks.
The old sheet's worst row sat at 9.06 times flat on the benefit side and 4.69
times on the drawback side. It now sits at 1.68 and 1.31. Two is a ceiling with
room to add content under it, not a line the sheet is balanced on.

WHY THERE IS NO FLOOR AS WELL. The rare end is still thin -- a weight 2 benefit
row is drawn at 0.29 times flat -- so a symmetric floor would fail today.
Repetition at the common end is what #1458 measured and what the redistribution
fixed. The rare end needs rows written rather than rows moved, which is that
issue's first option and was not the one the project owner took.

THE MEASUREMENT MIRRORS THE DRAW, and it has to. `UCataclysmDropRoll` drops a row
from the ORDINARY pool when its `Weight` is not a whole 1 to 4, which is every
`Set` row: the 56 of them carry a set identifier of 5 to 18 in that column
instead of a weight, which is issue
[#1443](https://github.com/sdubois777/Cataclysm/issues/1443) and is not this
file's business. Excluding them the way the draw does is. Counting them would put
42 phantom benefit rows into the bands.

A SET IS DRAWN, JUST NOT AS A LOOSE ROW IN A BAND. Until 2026-09-08 no set could
be drawn at all: `EnchantmentSuitsSlot` refused every `Set` row on the reading
that some other mechanism handed a set out whole, and no such mechanism was ever
written. The project owner ruled that a set IS an enchantment, and
`EnchantmentSetsFor` now offers each complete set as ONE option inside the weight
1 band. So the band counts below are still the right measurement -- a set is not
a row in a band -- but "a set row cannot drop" is no longer true and this file
must not be read as saying it.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
POSITIVE = REPO_ROOT / "game" / "Data" / "EnchantmentsPositive.csv"
NEGATIVE = REPO_ROOT / "game" / "Data" / "EnchantmentsNegative.csv"
DROP_ROLL_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
               / "CataclysmDropRoll.h")

#: The weights the draw can price. `LowestEnchantmentWeight` to
#: `HighestEnchantmentWeight` in CataclysmDropRoll.h.
WEIGHTS = (1, 2, 3, 4)

#: How much of the flat rate one written row may carry. See the docstring: a
#: ceiling with headroom, not the value the sheet sits at.
REPETITION_CEILING = 2.0

#: The two slots that differ. Three of the 574 rows carry `Item.Slot.Weapon`,
#: so a weapon draws from three more benefit rows than every other slot does.
SLOTS = ("Chest", "Weapon")


def read_rows(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return list(csv.DictReader(path.read_text(encoding="utf-8-sig").splitlines()))


def suits_slot(row: dict, slot: str) -> bool:
    """Mirror `UCataclysmDropRoll::EnchantmentSuitsSlot`, plus the set exclusion.

    A row carrying one or more `Item.Slot.` tags is restricted to those slots and
    a row carrying none may appear anywhere -- the project owner's 2026-09-07
    ruling that a tag says what an enchantment AFFECTS rather than where it may
    sit. That part is what the C++ function does.

    THE SET EXCLUSION IS NOT THAT FUNCTION'S ANY MORE. `EnchantmentSuitsSlot`
    refused every `Set` row until 2026-09-08; now `EnchantmentDrawWeight` is what
    keeps them out of the ordinary bands, by pricing a weight of 5 to 18 at zero.
    It is kept here because these are per-band counts and a set is not a member
    of a band, and `band_of` below would drop them anyway.
    """
    if row["EnchantmentType"].strip().lower() == "set":
        return False

    prefix = "item.slot."
    has_slot_tag = False
    for tag in row["Tags"].split(","):
        tag = tag.strip()
        if not tag.lower().startswith(prefix):
            continue
        has_slot_tag = True
        if tag[len(prefix):].lower() == slot.lower():
            return True
    return not has_slot_tag


def band_of(row: dict) -> int | None:
    """The row's weight band, or None when the draw cannot price it.

    Mirrors `UCataclysmDropRoll::EnchantmentDrawWeight` returning zero: a weight
    that is not a whole number from 1 to 4 takes the row out of the draw rather
    than being priced wrongly.
    """
    try:
        weight = float(row["Weight"])
    except (TypeError, ValueError):
        return None
    nearest = round(weight)
    if abs(nearest - weight) > 1e-6 or nearest not in WEIGHTS:
        return None
    return nearest


def counts_for(rows: list[dict], slot: str) -> dict[int, int]:
    """How many drawable rows sit in each band, for one gear slot."""
    found = {weight: 0 for weight in WEIGHTS}
    for row in rows:
        if not suits_slot(row, slot):
            continue
        band = band_of(row)
        if band is not None:
            found[band] += 1
    return found


@pytest.fixture(scope="module")
def step() -> float:
    """`EnchantmentWeightStep` read out of the header, not copied into here.

    A second hand-maintained copy of a tuning value is the mistake CLAUDE.md
    records the scoring port making twice. Reading it means a retune of the
    ladder re-aims these checks instead of making them lie.
    """
    if not DROP_ROLL_H.is_file():
        pytest.skip("CataclysmDropRoll.h is not present")
    match = re.search(
        r"EnchantmentWeightStep\s*=\s*([0-9]+(?:\.[0-9]+)?)f?\s*;",
        DROP_ROLL_H.read_text(encoding="utf-8"))
    assert match, (
        "CataclysmDropRoll.h no longer declares EnchantmentWeightStep as a "
        "literal. This file reads the ladder from it rather than holding a "
        "second copy; give it the value some other way before deleting this.")
    return float(match.group(1))


@pytest.fixture(scope="module")
def benefit_share(step) -> dict[int, float]:
    """What share of benefit draws each band takes.

    The band's own frequency over the total, which is what per-band pricing
    means. At a step of four this is 1.2%, 4.7%, 18.8% and 75.3%.
    """
    raw = {weight: step ** (weight - 1) for weight in WEIGHTS}
    total = sum(raw.values())
    return {weight: value / total for weight, value in raw.items()}


@pytest.fixture(scope="module")
def drawback_share(benefit_share, step) -> dict[int, float]:
    """What share of drawback draws each band takes.

    DERIVED FROM THE FLOOR RATHER THAN CHOSEN. The drawback is drawn from weight
    1 up to the benefit's weight, on the same ladder renormalised over that
    range, so every benefit tier can reach a weight 1 drawback while only the
    top tier can reach a weight 1 benefit. At a step of four this is 3.9%,
    10.9%, 28.5% and 56.7%.
    """
    out = {weight: 0.0 for weight in WEIGHTS}
    for benefit in WEIGHTS:
        allowed = [weight for weight in WEIGHTS if weight <= benefit]
        total = sum(step ** (weight - 1) for weight in allowed)
        for weight in allowed:
            out[weight] += benefit_share[benefit] * (step ** (weight - 1)) / total
    return out


@pytest.fixture(scope="module")
def sides(benefit_share, drawback_share) -> list[tuple[str, list[dict], dict]]:
    return [("benefit", read_rows(POSITIVE), benefit_share),
            ("drawback", read_rows(NEGATIVE), drawback_share)]


class TestTheMeasurementMirrorsTheDraw:
    """A count that includes rows the draw never reaches is not a measurement."""

    def test_set_rows_are_excluded_from_the_bands(self, sides):
        """A filter that excludes nothing is a filter that is not running.

        A set is drawn as ONE option inside the weight 1 band rather than as a
        loose row in it, so its rows must stay out of these per-band counts. If
        the sheet stopped spelling the type that way this filter would quietly
        pass everything through and the bands would gain 56 rows that are not
        band members.

        THIS IS NOT "A SET CANNOT DROP". It could not until 2026-09-08 and now
        it can; what stays true is that a set row is not priced by the band
        counts measured here.
        """
        for name, rows, _ in sides:
            excluded = [row for row in rows
                        if row["EnchantmentType"].strip().lower() == "set"]
            assert excluded, (
                f"no {name} row has EnchantmentType 'Set', so the exclusion "
                f"this file shares with "
                f"UCataclysmDropRoll::EnchantmentDrawWeight is not being "
                f"exercised and these counts prove nothing.")

    def test_every_row_that_is_not_a_set_can_be_drawn(self, sides):
        for name, rows, _ in sides:
            for row in rows:
                if row["EnchantmentType"].strip().lower() == "set":
                    continue
                assert band_of(row) is not None, (
                    f"{name} row {row['Name']!r} has Weight "
                    f"{row['Weight']!r}, which is not a whole 1 to 4, so "
                    f"UCataclysmDropRoll::EnchantmentDrawWeight prices it at "
                    f"zero and it can never drop. Only the set rows of issue "
                    f"#1443 are allowed to do that.")


class TestTheLadderIsUnchanged:
    """The shares are the owner's ruling. Row counts are measured against them."""

    def test_the_step_is_four(self, step):
        """The project owner ruled the step of four on 2026-09-07, and issue
        #1458 records that they chose to redistribute rows rather than change
        it. Changing it moves every share this file measures against, and it is
        a design decision rather than a way to make a count fit."""
        assert step == 4.0

    def test_the_benefit_shares_are_the_documented_ones(self, benefit_share):
        rounded = [round(benefit_share[weight] * 100, 1) for weight in WEIGHTS]
        assert rounded == [1.2, 4.7, 18.8, 75.3]

    def test_the_drawback_shares_are_the_documented_ones(self, drawback_share):
        rounded = [round(drawback_share[weight] * 100, 1) for weight in WEIGHTS]
        assert rounded == [3.9, 10.9, 28.5, 56.7]


class TestRowCountsFollowHowOftenTheWeightIsDrawn:
    """Issue #1458. This is the thing that drifted and this is the guard."""

    def test_every_band_has_rows_written_at_it(self, sides):
        """An empty band is not a gap, it is a change to the ladder.

        `DrawEnchantmentWeight` renormalises over the bands that can supply a
        row, so a band with nothing in it hands its share to the others and the
        four documented frequencies stop being true.
        """
        for slot in SLOTS:
            for name, rows, _ in sides:
                found = counts_for(rows, slot)
                empty = [weight for weight in WEIGHTS if not found[weight]]
                assert not empty, (
                    f"on a {slot} no {name} row is written at weight "
                    f"{empty}. The draw renormalises over the bands that can "
                    f"supply a row, so an empty band silently redistributes "
                    f"its share and the documented frequencies become wrong.")

    def test_a_commoner_band_never_holds_fewer_rows(self, sides):
        """The shape issue #1458 measured, stated as an invariant.

        Weight 4 is drawn 64 times as often as weight 1, so it needs at least
        as much written at it. The sheet had 28 benefit rows at weight 4 and
        154 at weight 2, which is this the wrong way round.
        """
        for slot in SLOTS:
            for name, rows, _ in sides:
                found = counts_for(rows, slot)
                for lower, higher in zip(WEIGHTS, WEIGHTS[1:], strict=False):
                    assert found[higher] >= found[lower], (
                        f"on a {slot} weight {higher} holds {found[higher]} "
                        f"{name} rows and weight {lower} holds {found[lower]}, "
                        f"but weight {higher} is drawn more often. The "
                        f"commoner band must not be the smaller pool: that is "
                        f"issue #1458. Counts are {found}.")

    def test_no_row_carries_more_than_twice_its_flat_share(self, sides):
        """The repetition bound, which is what a player actually feels.

        A row's flat share is one over the number of rows on its side. Before
        the redistribution the worst benefit row sat at 9.06 times flat.
        """
        for slot in SLOTS:
            for name, rows, share in sides:
                found = counts_for(rows, slot)
                flat = 1.0 / sum(found.values())
                ratio, weight = max(
                    (share[weight] / found[weight] / flat, weight)
                    for weight in WEIGHTS if found[weight])
                assert ratio <= REPETITION_CEILING, (
                    f"on a {slot} one weight {weight} {name} row is drawn "
                    f"{ratio:.2f} times as often as it would be if every "
                    f"{name} row were equally likely, over the ceiling of "
                    f"{REPETITION_CEILING}. Weight {weight} takes "
                    f"{share[weight] * 100:.1f}% of the draws out of "
                    f"{found[weight]} written rows. Counts are {found}.")
