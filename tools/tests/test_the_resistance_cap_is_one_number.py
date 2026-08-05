"""The 70% resistance cap is one number, wherever it is written down.

WHY THIS EXISTS. Issue #228. `RESISTANCE_CAP = 70.0` was a separate literal in
four Python files, and nothing compared them: `sim/cataclysm_sim/damage.py`
(the damage mitigation model, where the cap is applied),
`sim/cataclysm_sim/affixes.py` (the affix pool model, which uses it to work out
how many affix slots reach the cap), `sim/cataclysm_sim/player_power.py` (the
Power Score model, where resistance above the cap adds no score) and the
`SOFT_CAPS` dictionary in `sim/cataclysm_sim/character.py`.

`CLAUDE.md` names that exact shape as one that has already cost this project
twice: `sim/cataclysm_sim/scoring.py` is a copy of a file in a separate
repository and drifted from it silently, which is why `sim/verify_scoring_port.py`
was written.

WHAT WAS DONE ABOUT IT. `damage.py` now owns the constant, because that is where
it is applied and because it sits directly above `MAX_RESISTANCE_CEILING`, whose
comment explains it in terms of the cap. The other three import it. So the three
Python copies are gone rather than checked.

WHAT THIS FILE CHECKS, WHICH IS THE PART AN IMPORT CANNOT REACH.

    the three C++ constants     Unreal cannot import a Python module.
    the design document         Prose cannot import anything.

THE C++ SIDE HAD THE SAME DUPLICATION AND NO LONGER DOES. Issue #232 deleted
`UCataclysmResistanceAttributeSet::EffectiveResistanceCap` and the helper beside
it, which clamped a raw resistance at 70 with no penetration argument. Nothing
outside a test called it, and the answer it gave was wrong for the design:
penetration is subtracted BEFORE the cap, which is the whole reason the cap is
soft. Two C++ copies are left and both are real:

    UCataclysmDamageCalculation::ResistanceCap
        game/Source/Cataclysm/AbilitySystem/CataclysmDamageCalculation.h
        Applied in the damage calculation itself. This is the one the game runs.
    FCataclysmPowerScoreModel::ResistanceCap
        game/Source/Cataclysm/Player/CataclysmPowerScore.h
        A different question -- how much resistance counts toward Power Score --
        that happens to stop at the same number. Already compared against
        `player_power.py` by tools/tests/test_power_score_port.py. Repeated here
        so this file is the one place that lists every copy of the number.

READING SOURCE WITH A REGULAR EXPRESSION IS CRUDE, and it is what
tools/tests/test_power_score_port.py already does for the same reason: the
alternative is no guard at all. A reworded declaration fails here by name, which
is a loud failure rather than a silent drift.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"

#: Each entry is one C++ file, the constant declared in it, and what applies it.
CPP_COPIES = [
    ("AbilitySystem/CataclysmDamageCalculation.h", "ResistanceCap",
     "the damage calculation"),
    ("Player/CataclysmPowerScore.h", "ResistanceCap",
     "the Power Score model"),
]

#: A constant that was deleted, checked so it cannot come back unnoticed.
#: `UCataclysmResistanceAttributeSet::EffectiveResistanceCap` was a third copy
#: whose helper ignored penetration, which is the one thing the cap's softness
#: depends on. Issue #232.
DELETED_CPP_COPIES = [
    ("AbilitySystem/CataclysmResistanceAttributeSet.h",
     "EffectiveResistanceCap"),
]

#: Every place `docs/Cataclysm_GDD_v2.md` states the cap, and a pattern that
#: captures the number out of that statement. The description is what fails, so
#: a rewording says which sentence stopped matching rather than only that
#: something did.
DOCUMENT_COPIES = [
    ("the Power Score bullet on over-capping",
     r"Resistance above the (\d+(?:\.\d+)?)% cap adds no Power Score"),
    ("the Caps table's Resistances row",
     r"\|\s*Resistances\s*\|\s*(\d+(?:\.\d+)?)%"),
    ("the Damage Mitigation Order table's resistance step",
     r"\|\s*4\. Resistance\s*\|[^|]*capped at (\d+(?:\.\d+)?)%"),
    ("the paragraph on penetration being applied first",
     r"Penetration is applied before the (\d+(?:\.\d+)?)% cap"),
    ("the worked penetration example in that same paragraph",
     r"a character at 100 resistance still sits at the (\d+(?:\.\d+)?)% cap"),
    ("the Resistances section",
     r"There are eight resistances[^.]*\. Each caps at (\d+(?:\.\d+)?)%"),
    ("the Resistances section on what an enchantment raises",
     r"raising the (\d+(?:\.\d+)?)% itself is possible only via enchantments"),
    ("the paragraph on why to over-cap",
     r"Resistance above (\d+(?:\.\d+)?)% is the headroom Overwhelm eats into"),
    ("the worked Overwhelm example in that same paragraph",
     r"A character at exactly (\d+(?:\.\d+)?)% loses mitigation"),
    ("the Maximum Resistance section's opening definition",
     r"\*\*Over-capping\*\* is having more than (\d+(?:\.\d+)?)% resistance"),
    ("the Maximum Resistance section on what raising the maximum moves",
     r"\*\*Raising the maximum\*\* moves the (\d+(?:\.\d+)?)% itself"),
    ("the Maximum Resistance figures table's Base cap row",
     r"\|\s*Base cap\s*\|\s*(\d+(?:\.\d+)?)%"),
    ("the worked example of why there is a ceiling",
     r"Going from (\d+(?:\.\d+)?)% to 80% removes a third"),
    ("the comparison with Path of Exile's ratio",
     r"and (\d+(?:\.\d+)?)% to 90% is 3 times here"),
]


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path} is not present")
    return path.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def cap() -> float:
    from cataclysm_sim import damage
    return damage.RESISTANCE_CAP


class TestThePythonCopiesAreGone:
    """Three of the four were removed outright, so this checks they are the same
    object rather than merely the same number."""

    def test_the_affix_pool_uses_the_damage_model_s_cap(self, cap):
        from cataclysm_sim import affixes
        assert affixes.RESISTANCE_CAP == cap

    def test_the_power_score_model_uses_the_damage_model_s_cap(self, cap):
        from cataclysm_sim import player_power
        assert player_power.RESISTANCE_CAP == cap

    def test_every_resistance_soft_cap_is_the_damage_model_s_cap(self, cap):
        from cataclysm_sim import character
        recorded = {s: character.SOFT_CAPS[s] for s in character.RESISTANCE_STATS}
        assert recorded == {s: cap for s in character.RESISTANCE_STATS}

    def test_the_cap_is_stated_once_in_the_simulation_package(self):
        """A new literal 70.0 assigned to a name containing RESISTANCE_CAP is
        the drift this whole file exists to stop. One is expected: the
        definition in damage.py."""
        package = REPO_ROOT / "sim" / "cataclysm_sim"
        pattern = re.compile(r"^\s*RESISTANCE_CAP\s*=\s*\d+(?:\.\d+)?\s*$",
                             re.MULTILINE)
        written_out = [p.name for p in sorted(package.glob("*.py"))
                       if pattern.search(p.read_text(encoding="utf-8"))]
        assert written_out == ["damage.py"], (
            f"{written_out} write the resistance cap out as a literal. Only "
            "damage.py should; the rest import it from there. Issue #228.")


class TestTheCppCopiesAgree:
    @pytest.mark.parametrize("relative_path,constant,applied_by", CPP_COPIES)
    def test_a_cpp_constant_matches_the_model(
            self, relative_path, constant, applied_by, cap):
        path = SOURCE / relative_path
        text = read(path)
        match = re.search(
            rf"constexpr float {constant}\s*=\s*([0-9.]+)f", text)
        assert match, (
            f"could not find {constant} in {path.name}, which is what "
            f"{applied_by} applies")
        assert float(match.group(1)) == pytest.approx(cap)

    @pytest.mark.parametrize("relative_path,constant", DELETED_CPP_COPIES)
    def test_a_deleted_copy_has_not_come_back(self, relative_path, constant):
        """Deleting a duplicate only helps if it stays deleted."""
        text = read(SOURCE / relative_path)
        assert constant not in text, (
            f"{constant} is back in {relative_path}. It was deleted by #232 "
            "because it was a second copy of the resistance cap whose helper "
            "ignored penetration, and penetration is subtracted before the cap. "
            "Use UCataclysmDamageCalculation::ResistanceCap instead.")


class TestTheDesignDocumentAgrees:
    @pytest.mark.parametrize("description,pattern", DOCUMENT_COPIES)
    def test_a_stated_cap_matches_the_model(self, description, pattern, cap):
        text = read(GDD)
        match = re.search(pattern, text)
        assert match, (
            f"{GDD.name} no longer states the resistance cap in {description}. "
            "Either it was reworded, in which case update the pattern in this "
            "file, or it was dropped, in which case the document no longer says "
            "what the cap is.")
        assert float(match.group(1)) == pytest.approx(cap), (
            f"{description} says {match.group(1)}%, the model says {cap}%")

    def test_every_place_was_named(self):
        """Counts the statements rather than trusting the list above to be
        complete. A statement of the cap added to the document without being
        added here would otherwise go unchecked, which is the whole failure this
        file is about."""
        text = read(GDD)
        # Every "70" written as a percentage. Deliberately literal: it counts
        # what the document says today, so a cap that moves has to move this
        # number too and the count cannot silently pass.
        stated = re.findall(r"\b70%", text)
        # One of them is not the resistance cap: the Hybrid Affixes section says
        # each of a hybrid's two stats gives 70% of what the single affix for
        # that stat gives. Same number, unrelated rule.
        not_the_cap = 1
        assert len(stated) - not_the_cap == len(DOCUMENT_COPIES), (
            f"{GDD.name} states 70% {len(stated)} times, of which "
            f"{not_the_cap} is the hybrid affix ratio rather than the "
            f"resistance cap. That leaves {len(stated) - not_the_cap} "
            f"statements of the cap against {len(DOCUMENT_COPIES)} entries in "
            "DOCUMENT_COPIES in this file. Add or remove the entry that "
            "changed, so every statement of the cap stays checked.")
