"""The three critical strike numbers are the same in Python, C++ and the design.

WHY THIS EXISTS. Issue #649. Critical strike chance and multiplier existed as
Unreal attributes that no code read, so nothing ever rolled one and nothing ever
compared the engine's figures with the model's. When the roll was built, the
numbers on both sides happened to agree and no test would have noticed if they
stopped:

    the base chance a skill gets      5%, when the skill names none
    the hard cap on that chance       100%
    the base multiplier               150%, meaning a critical strike is 1.5x

`CLAUDE.md` names this exact shape as one that has already cost the project
twice: `sim/cataclysm_sim/scoring.py` is a copy of a file in another repository
and drifted from it silently, which is why `sim/verify_scoring_port.py` exists.
`tools/tests/test_the_resistance_cap_is_one_number.py` is the pattern this file
follows, for the same reason and against the same class of failure.

WHAT AN IMPORT CANNOT REACH, and therefore what this file is for:

    the C++ constants      Unreal cannot import a Python module.
    the design document    Prose cannot import anything.

READING SOURCE WITH A REGULAR EXPRESSION IS CRUDE. The alternative is no guard
at all. A renamed constant fails here by name, which is a loud failure rather
than a silent drift.

WHAT THIS DELIBERATELY DOES NOT CHECK. Whether the multiplier is applied in the
same place in both. The Python model never rolls a critical strike -- it
multiplies every hit by the long-run average `(1 - chance + chance x multiplier)`
because it models many hits rather than one -- and the game rolls per hit,
because an averaged hit can never be drawn as a critical strike. That divergence
is deliberate and is recorded in `docs/DECISIONS.md`. What has to match is the
three numbers, because those are what make the two produce the same mean.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
SIM = REPO_ROOT / "sim" / "cataclysm_sim"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def cpp_constant(relative: str, name: str) -> float:
    """One `static constexpr float NAME = VALUE;` out of a C++ header."""
    path = SOURCE / relative
    assert path.is_file(), f"{path} does not exist"
    match = re.search(rf"constexpr float {name}\s*=\s*([0-9.]+)f", read(path))
    assert match, (
        f"could not find {name} in {path.name}. If it was renamed, rename it "
        f"here too; if it was deleted, this whole check needs rethinking.")
    return float(match.group(1))


def python_constant(module: str, name: str) -> float:
    """One `NAME = VALUE` out of a simulation module, read rather than imported.

    Read as text on purpose, so that this file says which line in which module
    it is pinning rather than depending on the package importing cleanly.
    """
    path = SIM / module
    assert path.is_file(), f"{path} does not exist"
    match = re.search(rf"^{name}\s*=\s*([0-9.]+)\s*$", read(path), re.MULTILINE)
    assert match, f"could not find {name} in {path.name}"
    return float(match.group(1))


class TestTheBaseChanceIsFivePercent:
    """A skill that names no critical strike chance gets 5%."""

    def test_the_model_and_the_engine_agree(self):
        model = python_constant("character.py", "DEFAULT_SKILL_CRIT_CHANCE")
        engine = cpp_constant(
            "Items/CataclysmWeaponSlotsComponent.h",
            "DefaultSkillCritChancePercent")

        assert model == pytest.approx(engine), (
            f"the simulation gives a skill {model}% base critical strike chance "
            f"and the game gives it {engine}%. The design decided one number on "
            f"2026-08-04 and docs/DECISIONS.md records it.")

    def test_the_number_is_five(self):
        # NAMED OUTRIGHT, because comparing the two constants alone would pass
        # if both were zero -- and zero is exactly the state issue #649 was
        # about, where the engine's chance was never written at all.
        assert python_constant(
            "character.py", "DEFAULT_SKILL_CRIT_CHANCE") == pytest.approx(5.0)


class TestTheHardCapIsOneHundred:
    """Critical strike chance stops at 100% and the design calls it hard."""

    def test_the_model_and_the_engine_agree(self):
        text = read(SIM / "character.py")
        match = re.search(r'HARD_CAPS[^=]*=\s*\{"crit_chance":\s*([0-9.]+)\}',
                          text)
        assert match, "could not find HARD_CAPS['crit_chance'] in character.py"
        model = float(match.group(1))

        engine = cpp_constant(
            "AbilitySystem/CataclysmCombatAttributeSet.h", "CritChanceCap")

        assert model == pytest.approx(engine), (
            f"the simulation caps critical strike chance at {model} and the "
            f"game caps it at {engine}.")

    def test_the_design_document_states_the_same_cap(self):
        # The caps table row, quoted so a rewording says which sentence stopped
        # matching rather than only that something did.
        match = re.search(r"\|\s*Crit chance\s*\|\s*([0-9.]+)%\s*\|\s*Hard",
                          read(GDD))
        assert match, (
            "could not find the Crit chance row of the caps table in "
            "docs/Cataclysm_GDD_v2.md. It began "
            "'| Crit chance | 100% | Hard, and nothing raises it. ...'")
        assert float(match.group(1)) == pytest.approx(100.0)


class TestNothingClaimsTheCapCanBeLifted:
    """No passive tree may say critical strike chance is uncapped. Issue #658.

    WHAT WENT WRONG. A Berserker keystone named Hair Trigger read "Your critical
    strike chance is uncapped. Any critical strike chance above 100% is converted
    to critical strike damage at a 2:1 ratio." The caps table said the cap was
    hard. Both are shipped design and they could not both be true, and neither
    the model nor the engine had any route past the cap, so the keystone could
    not have been built as written.

    THE PROJECT OWNER SETTLED IT ON 2026-08-17: the cap is hard and nothing
    raises it. The keystone now converts the excess into critical strike damage
    without lifting the cap, which is a thing the 100% cap permits.

    WHY A TEST AND NOT JUST AN EDIT. The trees are authored in a separate tool at
    C:\\Projects\\PassiveTreeCreator and re-exported over these files, so the old
    wording can come back by accident. Nothing else in the project would notice:
    no code reads a keystone's description.
    """

    #: Every class passive tree, which is where a keystone could claim it.
    TREES = sorted((REPO_ROOT / "docs").glob("*_Class_Tree_*.json"))

    #: Wordings that would mean the cap can be exceeded. Matched case-insensitively
    #: against every node description.
    FORBIDDEN = ("uncapped", "cap is removed", "ignores the cap",
                 "no longer capped", "removes the cap")

    def test_the_trees_were_actually_found(self):
        """A glob that matches nothing would make the next test vacuous."""
        assert len(self.TREES) >= 3, (
            f"expected several class tree files in docs/, found "
            f"{[p.name for p in self.TREES]}")

    def test_no_node_says_critical_strike_chance_is_uncapped(self):
        import json

        offenders = []
        for path in self.TREES:
            for node in json.loads(read(path)).get("nodes", []):
                data = node.get("data", {})
                description = str(data.get("description", ""))
                if "crit" not in description.lower():
                    continue
                for phrase in self.FORBIDDEN:
                    if phrase in description.lower():
                        offenders.append(
                            f"{path.name}: {data.get('name')} -- {description}")
                        break

        assert not offenders, (
            "these passive tree nodes say critical strike chance can exceed its "
            "cap, which the caps table forbids: " + "; ".join(offenders))

    def test_the_berserker_keystone_converts_rather_than_uncaps(self):
        """The node the contradiction was found on, checked by name.

        Pinned by name rather than only by the sweep above, because the sweep can
        only catch wordings it was told about, and this is the one node whose
        history makes it worth naming.
        """
        import json

        path = REPO_ROOT / "docs" / "Berserker_Class_Tree_Final.json"
        assert path.is_file(), f"{path} does not exist"

        nodes = {n.get("data", {}).get("name"): n.get("data", {})
                 for n in json.loads(read(path)).get("nodes", [])}
        keystone = nodes.get("Hair Trigger")
        assert keystone, (
            "the Berserker tree has no keystone named Hair Trigger. If it was "
            "renamed or removed, this check needs rethinking rather than "
            "deleting; issue #658 has the history.")

        description = str(keystone.get("description", ""))
        assert "converted to critical strike damage" in description, (
            f"Hair Trigger reads {description!r}. It is meant to convert "
            "critical strike chance past 100% into critical strike damage, "
            "which is what the hard cap permits.")


class TestTheBaseMultiplierIsOneHundredAndFifty:
    """A critical strike is worth one and a half times the hit by default."""

    def test_the_model_and_the_engine_agree(self):
        # The model keeps it on the shared default class stat line rather than
        # as a named constant, because it is a stat a class could override --
        # none of the three does.
        text = read(SIM / "character.py")
        match = re.search(
            r'"crit_multiplier":\s*Scaling\(base=([0-9.]+)\)', text)
        assert match, (
            "could not find the crit_multiplier entry of DEFAULT_STAT_LINE in "
            "character.py")
        model = float(match.group(1))

        engine_text = read(
            SOURCE / "AbilitySystem" / "CataclysmCombatAttributeSet.cpp")
        engine_match = re.search(r"InitCritMultiplier\(([0-9.]+)f\)", engine_text)
        assert engine_match, (
            "could not find InitCritMultiplier in "
            "CataclysmCombatAttributeSet.cpp")
        engine = float(engine_match.group(1))

        assert model == pytest.approx(engine), (
            f"the simulation's default critical strike multiplier is {model} "
            f"and the game initialises the attribute to {engine}.")

    def test_the_number_is_one_hundred_and_fifty(self):
        engine_text = read(
            SOURCE / "AbilitySystem" / "CataclysmCombatAttributeSet.cpp")
        match = re.search(r"InitCritMultiplier\(([0-9.]+)f\)", engine_text)
        assert match
        assert float(match.group(1)) == pytest.approx(150.0)


class TestTheEngineStillRollsAtAll:
    """The roll exists, so this file cannot pass over a feature that was removed.

    EVERY CHECK ABOVE COMPARES TWO NUMBERS AND NONE OF THEM NOTICES IF NOTHING
    READS EITHER. That is precisely the state issue #649 described: both
    attributes present, both clamped, both set on enemies from data, and no code
    anywhere reading them. This file would have passed happily throughout.
    """

    def test_the_damage_calculation_reads_the_chance_and_the_multiplier(self):
        text = read(SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.cpp")
        assert "Hit.CritChance" in text, (
            "UCataclysmDamageCalculation::Resolve no longer reads the hit's "
            "critical strike chance, so nothing rolls one. That is the state "
            "issue #649 was filed about.")

        # THE MULTIPLICATION ITSELF, NOT THE NAME. Looking for the text
        # "Hit.CritMultiplier" anywhere in the file does not work, and that is
        # not a guess: blanking this statement to `Damage *= 1.0f;` was tried on
        # purpose and this check still passed, because the name also appears in
        # the condition on the line above. A guard that does not fire is worth
        # less than no guard, because it reads as coverage.
        assert re.search(r"Damage\s*\*=\s*Hit\.CritMultiplier\s*/\s*100\.0f",
                         text), (
            "Resolve no longer multiplies the hit by the critical strike "
            "multiplier. If the statement was reworded rather than removed, "
            "reword this pattern too.")

        assert "bWasCritical" in text, (
            "Resolve no longer reports that a hit was a critical strike, so "
            "nothing that draws a damage number can mark one.")

    def test_the_attacker_is_read_when_a_hit_lands(self):
        text = read(SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp")
        assert "GetCritChance()" in text, (
            "the attacker's critical strike chance is no longer read where a "
            "hit is built, so a hit arriving as a gameplay effect can never "
            "critically strike however the calculation behaves.")
