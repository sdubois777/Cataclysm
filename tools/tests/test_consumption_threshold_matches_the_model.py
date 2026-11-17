"""The Consumption Threshold table in the design document, against the model.

WHY THIS EXISTS. `docs/Cataclysm_GDD_v2.md` states one Consumption Threshold per
difficulty tier, in the Worn Residue and Consumption subsection of section VII.
Those eight numbers are not authored: `sim/cataclysm_sim/residue.py` derives them
from the residue a drop carries, what each Forge operation adds, and the gear a
player is expected to have at the end of each tier.

So the document holds a copy of a computed result. Any of those three inputs can
change -- a drop residue band is retuned, a craft cost is edited in the workbook,
the expected progression in `player_power.reference_character` is revised -- and
every threshold moves while the document goes on stating the old ones. Nothing
else would notice: the tests in `sim/tests/test_residue.py` check the threshold's
share of the path it came from, and that share stays 85% whatever the path is.

This project has had a designed number go stale in prose before, which is what
`tools/tests/test_unreal_pinned_row_counts.py` and
`tools/tests/test_skill_example_numbers.py` were both written for.

WHICH IS AUTHORITATIVE. The model, uniquely for this table, because the numbers
are derived. When this fails, regenerate the table rather than editing the
model to match the document:

    cd sim && python -m cataclysm_sim.residue
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The header of the table this file checks, used to find it.
TABLE_HEADER = "| Difficulty tier | Consumption Threshold |"

#: A row: `| 3 | 2,900 | 3,415 |`
ROW = re.compile(r"^\|\s*(\d)\s*\|\s*([\d,]+)\s*\|\s*([\d,]+)\s*\|\s*$")


@pytest.fixture(scope="module")
def document() -> str:
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")
    return DESIGN_DOCUMENT.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def model():
    if str(REPO_ROOT / "sim") not in sys.path:
        sys.path.insert(0, str(REPO_ROOT / "sim"))
    from cataclysm_sim import residue
    return residue


@pytest.fixture(scope="module")
def stated(document) -> dict[int, tuple[int, int]]:
    """Tier against (threshold, cheapest path), read out of the document."""
    assert TABLE_HEADER in document, (
        f"{DESIGN_DOCUMENT.name} no longer has a table headed "
        f"{TABLE_HEADER!r}. Either the Consumption Threshold table was removed "
        "or its header was reworded, and every comparison below would pass "
        "having compared nothing.")

    after = document[document.index(TABLE_HEADER):]
    rows: dict[int, tuple[int, int]] = {}
    for line in after.splitlines()[1:]:
        match = ROW.match(line)
        if not match:
            if line.startswith("|"):
                continue          # the alignment row
            break                 # the table has ended
        rows[int(match.group(1))] = (int(match.group(2).replace(",", "")),
                                     int(match.group(3).replace(",", "")))
    return rows


def test_the_parser_found_a_row_for_every_tier(stated) -> None:
    """Guarded first: a regular expression that matched nothing would make the
    comparison below vacuous."""
    assert sorted(stated) == list(range(1, 9)), (
        f"the Consumption Threshold table in {DESIGN_DOCUMENT.name} has rows "
        f"for tiers {sorted(stated)}, and there are eight difficulty tiers.")


def test_every_stated_threshold_matches_the_model(stated, model) -> None:
    wrong = [
        f"tier {tier}: document {stated[tier][0]:,}, "
        f"model {model.consumption_threshold(tier):,}"
        for tier in sorted(stated)
        if stated[tier][0] != model.consumption_threshold(tier)
    ]
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states Consumption Thresholds that "
        "sim/cataclysm_sim/residue.py no longer derives: " + "; ".join(wrong)
        + ". These are computed from the drop residue bands, the Forge "
          "operation costs and the expected gear progression, so one of those "
          "moved. Reprint the table with `cd sim && python -m "
          "cataclysm_sim.residue`.")


def test_every_stated_path_total_matches_the_model(stated, model) -> None:
    """The second column, which is what the threshold is a share of. Stating it
    is what lets a reader check the 85% without running anything."""
    wrong = []
    for tier in sorted(stated):
        _start, cheapest = model.cheapest_path(tier)
        if stated[tier][1] != round(cheapest):
            wrong.append(f"tier {tier}: document {stated[tier][1]:,}, "
                         f"model {round(cheapest):,}")
    assert not wrong, (
        f"{DESIGN_DOCUMENT.name} states cheapest-path totals the model does "
        "not produce: " + "; ".join(wrong))


def test_the_stated_share_really_is_the_one_the_document_claims(stated) -> None:
    """The document says 85%. Checked against its own two columns, so a reader
    doing the division by hand gets the answer the prose promised."""
    assert "85%" in stated_paragraph_text(), (
        "the Worn Residue section no longer says what share of the path the "
        "threshold is set at.")
    for tier, (threshold, path) in sorted(stated.items()):
        share = threshold / path
        assert 0.80 <= share <= 0.90, (
            f"tier {tier} states {threshold:,} against a path of {path:,}, "
            f"which is {share:.1%} and outside the 80% to 90% the section "
            "describes.")


def stated_paragraph_text() -> str:
    return DESIGN_DOCUMENT.read_text(encoding="utf-8")


def test_the_document_no_longer_says_the_number_does_not_exist(document) -> None:
    """Two sentences said the Consumption Threshold was undecided, one in
    section VI and one in section VII. Both are wrong now, and a stale
    "to be tuned" beside a table of eight tuned numbers is worse than either."""
    for stale in ("A single fixed number, to be tuned",
                  "The number that decides whether all this is dangerous does "
                  "not exist yet"):
        assert stale not in document, (
            f"{DESIGN_DOCUMENT.name} still says {stale!r}, and the Consumption "
            "Threshold is now derived per difficulty tier.")
