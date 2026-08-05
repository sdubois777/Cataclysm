"""The design document's Enemy Score section matches the verified power model.

WHY THIS EXISTS. Issue #30. `docs/Cataclysm_GDD_v2.md` section X documented the
January 2025 first-commit version of the Enemy Score formula, which multiplied a
base score by a rarity multiplier, a dungeon type multiplier and a subtype
multiplier. That was replaced upstream in the separate, private
`sdubois777/DungeonSimulator` repository and this document was never re-exported,
so it described a formula the game had not used for a year.

The stale copy was not harmless. It said a Boss is 2.5 times a Common enemy; the
real model puts it about 5% above. Enemy rarity produces the Overwhelm ladder in
section IV from these numbers, so anyone reading the document's version would
badly mis-estimate how much mitigation a Boss strips.

THE CHAIN OF AUTHORITY, and why this file sits at the end of it.

    src/utils/calculateScores.tsx        authoritative, in another repository
      ^ checked by sim/verify_scoring_port.py
    sim/cataclysm_sim/scoring.py         a verified port, never hand-edited
      ^ checked by THIS FILE
    docs/Cataclysm_GDD_v2.md section X   what a person reads

`CLAUDE.md` says the port has silently drifted from its source twice, which is
why `verify_scoring_port.py` exists. This file closes the same gap one link
further down: the document was a third copy with nothing comparing it.

WHAT IS COMPARED. Every table in the section, cell by cell, against the model's
own dictionaries. Not the prose, and not the formula's shape -- a formula written
as prose cannot be executed, and `sim/cataclysm_sim/scoring.py` already has a
self-test that locks the arithmetic. What this catches is a number in the
document parting company with the number the game runs on, which is what
happened.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

SECTION = "## **Enemy Score Formula**"
SECTION_ENDS = "## **Vertical Slice Enemies (Demonic Cataclysm)**"

DUNGEON_SCORE_SECTION = "## **Dungeon Score Formula**"


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import scoring
    return scoring


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def section(document) -> str:
    start = document.find(SECTION)
    assert start != -1, f"{GDD.name} has no section headed {SECTION!r}"
    end = document.find(SECTION_ENDS, start)
    assert end != -1, f"{GDD.name} has no section headed {SECTION_ENDS!r}"
    return document[start:end]


def table_after(section: str, heading: str) -> dict[str, float]:
    """Read a two-column name-and-number table that follows a heading.

    The document's tables come from a Google Docs conversion: an empty header
    row, then the real headers as escaped bold in the first body row, then the
    data. Escaped-bold rows are skipped rather than parsed, so this reads the
    data whether or not the conversion artefacts are cleaned up later. Issue
    #238 is that cleanup.
    """
    start = section.find(heading)
    assert start != -1, f"the section has no heading {heading!r}"
    after = section[start + len(heading):]
    stop = re.search(r"^#{1,6} ", after, re.MULTILINE)
    body = after[:stop.start()] if stop else after

    out: dict[str, float] = {}
    for line in body.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 2 or r"\*\*" in line:
            continue
        name, value = cells[0], cells[1]
        if not name or not re.fullmatch(r"-?\d+(?:\.\d+)?", value):
            continue
        out[name] = float(value)
    return out


class TestEveryTableMatchesTheModel:
    def test_the_rarity_weights(self, section, model):
        stated = table_after(section, "### **Rarity Weights**")
        assert stated == {k: float(v)
                          for k, v in model.RARITY_WEIGHTS.items()}

    def test_the_dungeon_type_weights(self, section, model):
        stated = table_after(section, "### **Dungeon Type Weights**")
        assert stated == {k: float(v) for k, v in model.TYPE_WEIGHTS.items()}

    def test_the_subtype_weights(self, section, model):
        stated = table_after(section, "### **Subtype Weights**")
        assert stated == {k: float(v) for k, v in model.SUBTYPE_WEIGHTS.items()}

    def test_the_floor_scaling_bases(self, section, model):
        stated = table_after(section, "### **Floor Scaling Bases**")
        assert stated == {k: float(v)
                          for k, v in model.FLOOR_SCALING_BASES.items()}

    def test_the_base_type_scores(self, section, model):
        """Listed even though the current formula never reads them, because the
        port still verifies them against the authoritative source."""
        stated = table_after(section, "### **Base Type Scores**")
        assert stated == {k: float(v)
                          for k, v in model.BASE_TYPE_SCORES.items()}

    def test_the_player_maximum_power_scores_and_their_widths(
            self, section, model):
        start = section.find("### **Player Maximum Power Scores**")
        assert start != -1, "the section has no Player Maximum Power Scores table"
        body = section[start:]

        stated: dict[int, tuple[float, float]] = {}
        for line in body.splitlines():
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) < 3 or r"\*\*" in line:
                continue
            if not all(re.fullmatch(r"\d+", c) for c in cells[:3]):
                continue
            stated[int(cells[0])] = (float(cells[1]), float(cells[2]))

        expected = {t: (float(model.PLAYER_MAX_SCORES[t]),
                        float(model.tier_width(t)))
                    for t in range(1, 9)}
        assert stated == expected


class TestTheRarityListItself:
    """The list, not just the numbers. A rarity added or removed upstream has to
    reach the document, and the superseded list must not come back."""

    def test_the_document_names_every_rarity_the_model_has(
            self, section, model):
        stated = table_after(section, "### **Rarity Weights**")
        assert set(stated) == set(model.RARITY_WEIGHTS)

    def test_there_is_no_rare_tier_anywhere_in_the_section(self, section):
        """Rare existed in the first-commit model and does not exist now. It sat
        in two tables in this document for a year."""
        assert not re.search(r"\|\s*Rare\s*\|", section), (
            "the Enemy Score section has a table row for a Rare enemy rarity. "
            "That rarity was removed from the model and does not exist. "
            "Issue #30.")

    def test_the_superseded_multiplier_table_has_not_come_back(self, section):
        """The old table said a Boss is 2.5 times a Common enemy. It is about
        5% above one. That table read like ordinary design data, so it has to
        be kept out by name rather than by hoping."""
        assert "Rarity Multipliers" not in section, (
            "the Enemy Score section has a Rarity Multipliers table again. "
            "Rarity is a weight added as a fraction of the tier width, not a "
            "multiplier applied to a score. Issue #30.")
        assert "Dungeon Type Multipliers" not in section


@pytest.fixture(scope="module")
def dungeon_section(document) -> str:
    start = document.find(DUNGEON_SCORE_SECTION)
    assert start != -1, (
        f"{GDD.name} has no section headed {DUNGEON_SCORE_SECTION!r}")
    after = document[start + len(DUNGEON_SCORE_SECTION):]
    stop = re.search(r"^#{1,6} ", after, re.MULTILINE)
    return after[:stop.start()] if stop else after


class TestTheDungeonScoreFormulaAgrees:
    """It is in a different section of the document and carried the same
    superseded rarity list."""

    def test_it_states_the_model_s_weights_against_the_model_s_rarities(
            self, dungeon_section, model):
        stated = dict(re.findall(
            r"\(([A-Za-z ]+?)\s*×\s*(\d+(?:\.\d+)?)\)", dungeon_section))
        expected = {rarity: str(weight)
                    for rarity, weight in model.DUNGEON_SCORE_MIX}
        assert stated == expected

    def test_the_weights_sum_to_one(self, model):
        """They are how common each rarity is on a floor, so they have to."""
        total = sum(w for _, w in model.DUNGEON_SCORE_MIX)
        assert total == pytest.approx(1.0)

    def test_it_says_the_score_is_the_middle_floor(self, dungeon_section):
        assert "middle floor" in dungeon_section, (
            "the formula collapses the MIDDLE floor's rarity spread, and a "
            "reader who does not know that will compute the wrong number")
