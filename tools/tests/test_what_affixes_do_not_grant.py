"""The one thing the design says an ordinary affix must never be.

WHY THIS EXISTS. `docs/Cataclysm_GDD_v2.md` has a section headed "What Affixes Do
Not Grant". It held two rules. One of them has been reversed and one stands.

    No ordinary affix is a "more" multiplier. An affix is flat or increased.
    Multiplicative sources come from gems, passive tree keystones and
    enchantments, as section IV states.

THE RULE THAT WAS REVERSED, recorded here because this file used to pin it and
someone will wonder where it went. The section also said "There are no attribute
affixes". Issue #204 reported the absence of attribute affixes as a hole; the
first version of this file closed that report by pinning the rule, which was
correct at the time and wrong within the hour: the project owner reversed the
decision on 2026-08-04, and gear must be able to grant primary attributes. The
attribute assertions are gone and the design document section now says gear can
grant them. See "Nine decisions from an audit of the affix pool" in
`docs/DECISIONS.md`, reversal 1.

THE POINT IS DISCOVERABILITY. The remaining rule is written in prose and nothing
else checks it. An audit reading `game/Data/Affixes.csv` sees only value kinds
`flat` and `increased` and has no way to know that a third one is forbidden
rather than merely absent. A rule that lives only in prose gets re-reported, which
is what happened to the rule that used to sit beside it.

IT ALSO GUARDS THE DECISION ITSELF. If the rule is ever reversed too, this test
fails, and the fix is to change the design document and this file together rather
than to let the data and the prose disagree.

WHAT IT CHECKS AND WHERE. Both copies of the affix pool that hold a value kind:

    docs/All_Things_Cataclysm.xlsx, Affixes sheet   authoritative, hand-edited
    game/Data/Affixes.csv                           generated, what the game loads

`sim/cataclysm_sim/affixes.py` is the third copy and needs no check here:
`StatAffix.__post_init__` already raises on any kind but flat or increased.

The eight primary attribute names used to be pinned here as well. They moved to
`tools/tests/test_primary_attribute_names.py`, which is where they belong now
that an affix may name one.
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
AFFIX_CSV = REPO_ROOT / "game" / "Data" / "Affixes.csv"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The section of the design document the rule comes from. Named here so a
#: failure says where to go, and asserted below so renaming the section does not
#: leave this file pointing at nothing.
GDD_SECTION = "What Affixes Do Not Grant"

#: The two value kinds an ordinary affix may have. "more" is deliberately absent.
ALLOWED_VALUE_KINDS = frozenset({"flat", "increased"})


def text(value) -> str:
    return "" if value is None else str(value).strip()


def read_sheet(title: str) -> list[dict[str, object]]:
    import openpyxl

    book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
    if title not in book.sheetnames:
        pytest.fail(f"the workbook has no sheet named {title!r}")

    rows = list(book[title].iter_rows(values_only=True))
    headers = [text(h) for h in rows[0]]
    out = []
    for raw in rows[1:]:
        if not raw or raw[0] is None or not text(raw[0]):
            continue
        out.append({headers[i]: raw[i] for i in range(len(headers))
                    if i < len(raw)})
    return out


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    if not path.is_file():
        pytest.skip(f"{path.name} not present")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def affix_sheet():
    if not WORKBOOK.is_file():
        pytest.skip("design workbook not present")
    return read_sheet("Affixes")


@pytest.fixture(scope="module")
def affix_rows():
    return read_csv(AFFIX_CSV)


class TestNoAffixIsAMoreMultiplier:
    """An affix is flat or increased, never multiplicative."""

    def test_every_stat_affix_in_the_workbook_is_flat_or_increased(self, affix_sheet):
        wrong = sorted({text(row["Value Kind"]) for row in affix_sheet
                        if text(row["Affix Kind"]) == "Stat"}
                       - ALLOWED_VALUE_KINDS)
        assert not wrong, (
            f"the Affixes sheet uses value kinds {wrong}. The design document "
            f"allows only {sorted(ALLOWED_VALUE_KINDS)} under '{GDD_SECTION}': "
            "multiplicative sources are gems, keystones and enchantments.")

    def test_every_stat_affix_the_game_loads_is_flat_or_increased(self, affix_rows):
        wrong = sorted({row["ValueKind"].strip() for row in affix_rows
                        if row["AffixKind"].strip() == "Stat"}
                       - ALLOWED_VALUE_KINDS)
        assert not wrong, f"{AFFIX_CSV.name} uses value kinds {wrong}"

    def test_the_model_rejects_any_other_kind(self):
        """`StatAffix` validates the kind on construction.

        The third copy of the pool, and the one the tuning work reads. A stat
        affix cannot be given a "more" kind even by hand.
        """
        from cataclysm_sim import affixes as af

        with pytest.raises(ValueError, match="'flat' or 'increased'"):
            af.StatAffix("More damage", "attack_damage", "more", 20.0,
                         af.OFFENSIVE_SLOTS, af.PREFIX)

    def test_there_are_stat_affixes_of_both_kinds_to_check(self, affix_rows):
        """A check over an empty set passes and proves nothing."""
        kinds = [row["ValueKind"].strip() for row in affix_rows
                 if row["AffixKind"].strip() == "Stat"]
        assert kinds.count("flat") >= 1 and kinds.count("increased") >= 1


class TestTheDesignDocumentStillSaysIt:
    def test_the_section_and_the_rule_are_still_there(self):
        """Without this, deleting the section would leave the tests enforcing a
        rule the design no longer holds."""
        if not GDD.is_file():
            pytest.skip("the design document is not present")
        gdd = GDD.read_text(encoding="utf-8")
        assert GDD_SECTION in gdd, (
            f"{GDD.name} no longer has a '{GDD_SECTION}' section")
        assert 'No ordinary affix is a "more" multiplier.' in gdd

    def test_the_reversed_rule_is_gone(self):
        """The design document must not still forbid attribute affixes.

        The reversal is only real if the document stops saying the old thing.
        This is what stops the two halves of #204 drifting apart: the sentence
        being deleted here and an attribute affix being added there are the same
        decision, and a document that still forbids it would make the affix look
        like a mistake.
        """
        if not GDD.is_file():
            pytest.skip("the design document is not present")
        gdd = GDD.read_text(encoding="utf-8")
        assert "**There are no attribute affixes.**" not in gdd, (
            f"{GDD.name} still forbids attribute affixes. The project owner "
            "reversed that on 2026-08-04; see reversal 1 of 'Nine decisions "
            "from an audit of the affix pool' in DECISIONS.md.")
