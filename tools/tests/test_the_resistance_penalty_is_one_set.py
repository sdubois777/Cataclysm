"""The difficulty tier's resistance penalty is one set of numbers, wherever it
is written down.

WHY THIS EXISTS. Issue #1229 gives a player's resistance a penalty that grows
with the difficulty tier, and the two numbers behind it are written in three
places that cannot import one another:

    the engine     `UCataclysmDamageCalculation::FirstPenalisedDifficultyTier`
                   and `ResistancePenaltyPerTier`, in
                   `game/Source/Cataclysm/AbilitySystem/CataclysmDamageCalculation.h`
    the model      `FIRST_PENALISED_DIFFICULTY_TIER` and
                   `RESISTANCE_PENALTY_PER_TIER`, in
                   `sim/cataclysm_sim/damage.py`
    the design     a table in `docs/Cataclysm_GDD_v2.md`

`CLAUDE.md` names that exact shape as one that has already cost this project
twice: `sim/cataclysm_sim/scoring.py` is a copy of a file in a separate
repository and drifted from it silently, which is why `sim/verify_scoring_port.py`
was written. `tools/tests/test_the_resistance_cap_is_one_number.py` is the
pattern this file follows, against the same class of failure and for the stat
next door.

READING SOURCE WITH A REGULAR EXPRESSION IS CRUDE. The alternative is no guard
at all. A renamed constant fails here by name, which is a loud failure rather
than a silent drift.

WHAT THIS DELIBERATELY DOES NOT CHECK. That the engine and the model apply the
penalty in the same place. The engine subtracts it where a defender's resistance
is summed, so it reaches every hit; the model has no hit to reach and exposes it
as a function the affix arithmetic calls. What has to match is the two numbers,
because those are what make the two agree about how much resistance a tier
demands.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
CPP_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmDamageCalculation.h")

#: Every row of the design document's table, and what the model must agree the
#: penalty is at that tier. The first entry covers tiers 1 to 3 together,
#: because the document states them as one row.
DOCUMENT_ROWS = [
    (r"\|\s*1 to 3\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", 1, 0, 70),
    (r"\|\s*4\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", 4, 15, 85),
    (r"\|\s*5\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", 5, 30, 100),
    (r"\|\s*6\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", 6, 45, 115),
    (r"\|\s*7\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", 7, 60, 130),
    (r"\|\s*8\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", 8, 75, 145),
]


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path} is not present")
    return path.read_text(encoding="utf-8")


def cpp_constant(name: str) -> float:
    """One `static constexpr` figure out of the damage calculation header."""
    text = read(CPP_HEADER)
    found = re.search(
        rf"static constexpr (?:float|int32) {name}\s*=\s*(-?\d+(?:\.\d+)?)f?;",
        text)
    assert found, (
        f"{CPP_HEADER.name} no longer declares {name}. Either it was renamed, "
        "in which case update this file, or the penalty was removed, in which "
        "case delete this file and the entry in docs/DECISIONS.md's index.")
    return float(found.group(1))


class TestTheEngineAndTheModelAgree:

    def test_the_first_penalised_tier_is_one_number(self):
        from cataclysm_sim import damage
        assert cpp_constant("FirstPenalisedDifficultyTier") == \
            damage.FIRST_PENALISED_DIFFICULTY_TIER

    def test_the_penalty_per_tier_is_one_number(self):
        from cataclysm_sim import damage
        assert cpp_constant("ResistancePenaltyPerTier") == \
            damage.RESISTANCE_PENALTY_PER_TIER


class TestTheModelsArithmetic:
    """The shape, not only the constants. A penalty that went negative below the
    first penalised tier would hand resistance out rather than take it, and the
    two constants alone cannot say whether that happens."""

    def test_no_tier_below_the_first_penalised_one_takes_anything(self):
        from cataclysm_sim import damage
        below = range(1, damage.FIRST_PENALISED_DIFFICULTY_TIER)
        assert [damage.resistance_penalty_at(t) for t in below] == [0.0] * len(below)

    def test_it_never_hands_resistance_out(self):
        """Tier zero and below are not reachable in play. They are checked
        because the arithmetic subtracts before it floors, so an unfloored
        version answers a negative number here and nowhere else."""
        from cataclysm_sim import damage
        assert [damage.resistance_penalty_at(t) for t in (-5, 0)] == [0.0, 0.0]

    def test_each_penalised_tier_adds_exactly_one_step(self):
        from cataclysm_sim import damage
        first = damage.FIRST_PENALISED_DIFFICULTY_TIER
        step = damage.RESISTANCE_PENALTY_PER_TIER
        for tier in range(first, 9):
            expected = (tier - first + 1) * step
            assert damage.resistance_penalty_at(tier) == expected, tier

    def test_what_it_takes_to_cap_is_the_cap_plus_the_penalty(self):
        from cataclysm_sim import damage
        for tier in range(1, 9):
            assert damage.resistance_needed_to_cap(tier) == \
                damage.RESISTANCE_CAP + damage.resistance_penalty_at(tier)


class TestTheDesignDocumentAgrees:
    """Prose cannot import anything, so every row of the table is read back."""

    @pytest.mark.parametrize("pattern,tier,penalty,needed", DOCUMENT_ROWS)
    def test_a_row_states_what_the_model_computes(self, pattern, tier,
                                                  penalty, needed):
        from cataclysm_sim import damage

        text = read(GDD)
        found = re.search(pattern, text)
        assert found, (
            f"{GDD.name} no longer has a difficulty tier penalty row matching "
            f"{pattern!r}. The table was reworded or removed; update this file "
            "or delete the row.")

        stated_penalty, stated_needed = int(found.group(1)), int(found.group(2))
        assert stated_penalty == damage.resistance_penalty_at(tier), (
            f"the document says tier {tier} takes {stated_penalty} off and the "
            f"model computes {damage.resistance_penalty_at(tier)}")
        assert stated_needed == damage.resistance_needed_to_cap(tier), (
            f"the document says tier {tier} needs {stated_needed} to cap and "
            f"the model computes {damage.resistance_needed_to_cap(tier)}")
        assert (stated_penalty, stated_needed) == (penalty, needed), (
            "the document and the model agree with each other but not with the "
            "figures this test was written against, so both moved together. "
            "That is a real change and wants a deliberate update here.")

    def test_the_document_says_which_tier_the_ramp_starts_at(self):
        from cataclysm_sim import damage

        text = read(GDD)
        found = re.search(
            r"From difficulty tier (\d+) onward a player loses "
            r"\*\*(\d+) resistance of every type per tier\*\*", text)
        assert found, (
            f"{GDD.name} no longer states the ramp in the sentence this test "
            "reads. Update the pattern or the document.")
        assert int(found.group(1)) == damage.FIRST_PENALISED_DIFFICULTY_TIER
        assert int(found.group(2)) == damage.RESISTANCE_PENALTY_PER_TIER
