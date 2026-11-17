"""Minion count comes from one enchantment, and the bound it sets.

WHY THIS EXISTS. Issue #339. Two enchantments raised the number of minions a
player can have active and **the rarer one was strictly weaker**: weight 2 gave
+1 to +2, weight 4 gave +2 to +4. For enchantments a lower weight is rarer, which
`docs/Cataclysm_GDD_v2.md` states plainly -- "The maximum resistance enchantment
is weight 1, which is the rarest and most powerful tier" -- so a player finding
the rarer one was worse off.

They said the same thing in different words, which is how the inversion became
possible at all in a table of 379 entries.

WHAT WAS DECIDED, by the project owner on 2026-08-14: merge into one at weight 2,
granting +2 to +4.

WHY THE BOUND IS WORTH A TEST OF ITS OWN. Each enchantment can appear only once
across all equipped gear, so the count of minion count enchantments IS the bound.
Two of them bounded gear-granted count at +3 to +6; one bounds it at +2 to +4.
The design document states that figure, and issue #209 put count in the
enchantment table rather than allowing it as an affix precisely because count
multiplies damage, effective health and every rider at once. A second count
enchantment added later would silently double the bound the document promises,
and nothing else would notice.

WHAT IS ASSERTED HERE.

    exactly one positive enchantment raises maximum minion count
    it is the merged one, at weight 2, with the tags of both
    the removed one is gone from the generated data
    the negative enchantment that shared its spreadsheet row survived, because
      the two tables sit side by side and a row deletion would have taken it
    the design document states the +2 to +4 bound and no longer says "two"
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
POSITIVE = REPO_ROOT / "game" / "Data" / "EnchantmentsPositive.csv"
NEGATIVE = REPO_ROOT / "game" / "Data" / "EnchantmentsNegative.csv"

#: The merged enchantment, exactly as the workbook spells it.
MERGED = "Add 2-4 to your maximum minion count"

#: Its weight. Lower is rarer, so the stronger of the two took the rarer tier.
MERGED_WEIGHT = 2.0

#: The one that was removed. Named so that putting it back fails.
REMOVED = "You can have 1-2 additional minions active simultaneously"

#: The negative enchantment that shared spreadsheet row 135 with the removed
#: positive. Deleting the row rather than clearing four cells would have taken
#: this with it, and nothing about it has anything to do with minions.
NEIGHBOUR = "Melee skills cost 5%-10% of your maximum HP to use"

#: What "raises the maximum number of minions" looks like in the effect text.
#: Deliberately broader than the merged wording, so a NEW enchantment phrased
#: differently is still caught.
RAISES_COUNT = re.compile(
    r"(maximum minion count"
    r"|additional minions"
    r"|to your max(?:imum)? minion)",
    re.IGNORECASE)


def unwrapped(text: str) -> str:
    return " ".join(text.split())


def rows_of(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} has not been generated")
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def positives() -> list[dict]:
    return rows_of(POSITIVE)


@pytest.fixture(scope="module")
def negatives() -> list[dict]:
    return rows_of(NEGATIVE)


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return unwrapped(GDD.read_text(encoding="utf-8"))


# --------------------------------------------------------------------------
# One enchantment, and the bound that follows from that
# --------------------------------------------------------------------------

def test_exactly_one_enchantment_raises_minion_count(positives) -> None:
    """THE BOUND IS THE COUNT. Each enchantment appears at most once across all
    equipped gear, so however many of these exist is how many a player can
    stack. Adding a second silently doubles what the design document promises.
    """
    raising = sorted(r["Effect"] for r in positives
                     if RAISES_COUNT.search(r["Effect"]))
    assert raising == [MERGED], (
        f"the positive enchantment table has {len(raising)} enchantment(s) "
        f"raising maximum minion count: {raising}. There is meant to be exactly "
        f"one, {MERGED!r}, because each enchantment can appear once across all "
        f"gear and so the number of them IS the bound. The design document "
        f"states +2 to +4 on that basis. Issue #339.")


def test_the_surviving_one_is_the_stronger_at_the_rarer_weight(
        positives) -> None:
    """The inversion issue #339 was raised for. A lower weight is rarer, so the
    +2 to +4 grant belongs at weight 2 and not at weight 4."""
    row = next((r for r in positives if r["Effect"] == MERGED), None)
    assert row is not None, (
        f"the positive enchantment table no longer has {MERGED!r}. It is the "
        f"merged minion count enchantment. Issue #339.")
    assert float(row["Weight"]) == MERGED_WEIGHT, (
        f"{MERGED!r} is weight {row['Weight']}, not {MERGED_WEIGHT}. A lower "
        f"weight is rarer, and the point of the merge was that the stronger "
        f"grant sits at the rarer tier. Issue #339.")
    for tag in ("Type.Summon", "Type.Minion"):
        assert tag in row["Tags"], (
            f"{MERGED!r} no longer carries the {tag} tag. The merged "
            f"enchantment took the tags of both, and the removed one supplied "
            f"Type.Summon. Issue #339.")


def test_the_removed_enchantment_is_gone(positives) -> None:
    """Stated separately from the count above so a failure says which of the
    two things went wrong: a NEW count enchantment, or the old one restored."""
    assert not any(r["Effect"] == REMOVED for r in positives), (
        f"{REMOVED!r} is back in the positive enchantment table. It was merged "
        f"into {MERGED!r} on 2026-08-14 because it granted half as much at a "
        f"rarer weight. Issue #339.")


# --------------------------------------------------------------------------
# The neighbour that a careless edit would have taken with it
# --------------------------------------------------------------------------

def test_the_negative_sharing_its_spreadsheet_row_survived(
        negatives) -> None:
    """The Enchantments sheet holds two independent tables side by side --
    positives in columns A to D, negatives in F to I -- and they are not paired.
    Deleting the removed positive's spreadsheet row would have taken this
    negative, which has nothing to do with minions, and no other test in this
    repository would have said so."""
    assert any(r["Effect"] == NEIGHBOUR for r in negatives), (
        f"{NEIGHBOUR!r} is missing from the negative enchantment table. It "
        f"shared a spreadsheet row with the positive removed for issue #339, "
        f"and the two tables in that sheet are independent. Clearing four "
        f"cells removes one; deleting the row removes both.")


# --------------------------------------------------------------------------
# The design document agrees with the data
# --------------------------------------------------------------------------

def test_the_document_states_the_bound_that_follows(document) -> None:
    """The number a reader takes away, and it changed. Two enchantments bounded
    gear-granted count at +3 to +6; one bounds it at +2 to +4."""
    assert ("gear contributes between +2 and +4 minions and can never "
            "contribute more") in document, (
        "the minion section of the design document does not state what gear "
        "can contribute to minion count. It is +2 to +4, because one "
        "enchantment raises it and each enchantment appears once across all "
        "gear. Issue #339.")


def test_the_document_no_longer_says_two_enchantments_raise_it(
        document) -> None:
    """The sentence that was true until 2026-08-14 and is now false. Leaving it
    would put the old +3 to +6 bound back in a reader's head."""
    assert "Two enchantments raise it" not in document, (
        "the minion section still says two enchantments raise minion count. "
        "They were merged into one on 2026-08-14, and the bound moved from "
        "+3 to +6 down to +2 to +4. Issue #339.")
