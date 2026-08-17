"""The 75% flat damage reduction cap is one number, wherever it is written down.

WHY THIS EXISTS. Issue #644. Flat damage reduction was the one layer of the
mitigation order with nothing bounding it: not in the design document's caps
table, not in `sim/cataclysm_sim/damage.py`, and not in the engine. At 100 it was
exact immunity, which the design says plainly that no combination of layers
reaches. The cap now exists in three places that cannot see each other, which is
the shape `CLAUDE.md` names as one that has already cost this project twice, and
which `tools/tests/test_the_resistance_cap_is_one_number.py` was written against
for the resistance cap. This file is that file's twin.

WHAT AN IMPORT CANNOT REACH, and therefore what this is for:

    the C++ constant       Unreal cannot import a Python module.
    the design document    Prose cannot import anything.

WHY 75 AND NOT 90, recorded here because a future reader will ask. Path of Exile
caps the closest layer it ships, additive physical damage reduction, at 90%. That
figure was deliberately not copied: its 90% covers physical damage alone, one
damage type among several, where this project's layer covers all eight types with
no roll, no curve and no per-type split. 75 is the same figure as the armour cap,
so the design has one number for the most a single unconditional mitigation layer
may remove. Last Epoch caps every layer it has; Diablo 4 needs no cap because its
sources stack multiplicatively. The full reasoning is in `docs/DECISIONS.md`.

READING SOURCE WITH A REGULAR EXPRESSION IS CRUDE. The alternative is no guard at
all. A renamed constant fails here by name, which is a loud failure rather than a
silent drift.
"""

from __future__ import annotations

import pathlib
import re

import pytest

from cataclysm_sim import damage

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"

#: The one C++ constant, the file it lives in, and what applies it.
CPP_COPIES = [
    ("AbilitySystem/CataclysmDamageCalculation.h", "DamageReductionCap",
     "the damage calculation"),
]

#: Every place `docs/Cataclysm_GDD_v2.md` states the cap, and a pattern that
#: captures the number out of that statement. The description is what fails, so
#: a rewording says which sentence stopped matching rather than only that
#: something did.
DOCUMENT_COPIES = [
    ("the caps table row",
     r"\|\s*Damage reduction\s*\|\s*(\d+(?:\.\d+)?)%\s*\|\s*Hard"),
    ("the mitigation order's step 5",
     r"\|\s*5\. Damage reduction \|[^|]*?capped at (\d+(?:\.\d+)?)%"),
    ("the paragraph explaining why the layer needs a cap",
     r"stops at (\d+(?:\.\d+)?)% for the same reason armor does"),
]


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path} is not in this checkout")
    return path.read_text(encoding="utf-8")


def test_the_model_owns_the_cap():
    """One module holds the literal and it is the one that applies it."""
    assert damage.DAMAGE_REDUCTION_CAP == pytest.approx(75.0)


def test_the_cap_is_stated_once_in_the_simulation_package():
    """No second copy of the literal anywhere in the package.

    The same check `test_the_resistance_cap_is_one_number.py` makes, for the
    same reason: four copies of the resistance cap drifted apart before anyone
    noticed, which is what issue #228 was.
    """
    package = REPO_ROOT / "sim" / "cataclysm_sim"
    pattern = re.compile(r"^\s*DAMAGE_REDUCTION_CAP\s*=\s*\d+(?:\.\d+)?\s*$",
                         re.MULTILINE)
    written_out = [p.name for p in sorted(package.glob("*.py"))
                   if pattern.search(p.read_text(encoding="utf-8"))]
    assert written_out == ["damage.py"], (
        "the cap should be written out in damage.py and imported everywhere "
        f"else, but a literal was found in {written_out}")


@pytest.mark.parametrize("relative,constant,applied_by", CPP_COPIES)
def test_the_cpp_constant_matches_the_model(relative, constant, applied_by):
    text = read(SOURCE / relative)
    match = re.search(rf"constexpr float {constant}\s*=\s*([0-9.]+)f", text)
    assert match, (
        f"could not find {constant} in {pathlib.Path(relative).name}, which is "
        f"what {applied_by} applies. If it was renamed, rename it here too.")
    assert float(match.group(1)) == pytest.approx(damage.DAMAGE_REDUCTION_CAP)


@pytest.mark.parametrize("description,pattern", DOCUMENT_COPIES)
def test_the_design_document_states_the_same_cap(description, pattern):
    match = re.search(pattern, read(GDD))
    assert match, (
        f"could not find {description} in docs/Cataclysm_GDD_v2.md. Either it "
        "was reworded, in which case update the pattern, or the document no "
        "longer says what the cap is, in which case it needs putting back.")
    assert float(match.group(1)) == pytest.approx(damage.DAMAGE_REDUCTION_CAP)


def test_the_cap_is_the_same_figure_as_the_armour_cap():
    """They are equal on purpose, and a reader should be told so rather than
    left to think it a coincidence.

    If either is ever retuned this fails, which is the point: whoever changes
    one has to decide whether the other moves with it.
    """
    assert damage.DAMAGE_REDUCTION_CAP == pytest.approx(
        damage.ARMOR_REDUCTION_CAP), (
        "the flat damage reduction cap and the armour cap were set to the same "
        "figure deliberately, so the design has one number for the most a "
        "single unconditional mitigation layer may remove. If they are meant "
        "to differ now, say so in docs/DECISIONS.md and delete this check.")


class TestTheCapIsActuallyApplied:
    """Every check above compares numbers and none notices if nothing reads them.

    That is precisely the state issue #644 described: the design's sentence
    about immunity was already written, the layer was already in the mitigation
    order, and no bound existed anywhere. A file that only compared constants
    would have passed throughout.
    """

    def test_the_model_applies_it(self):
        text = (REPO_ROOT / "sim" / "cataclysm_sim" / "damage.py").read_text(
            encoding="utf-8")
        assert re.search(
            r"damage \*= 1\.0 - effective_damage_reduction\("
            r"defender\.damage_reduction\) / 100\.0", text), (
            "the mitigation order no longer routes flat damage reduction "
            "through effective_damage_reduction, so the cap is declared and "
            "not applied.")

    def test_the_engine_applies_it(self):
        text = read(SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.cpp")
        assert "EffectiveDamageReduction(Combat->GetDamageReduction())" in text, (
            "UCataclysmDamageCalculation::Resolve no longer routes flat damage "
            "reduction through EffectiveDamageReduction, so the cap is "
            "declared and not applied.")
        assert re.search(
            r"FMath::Clamp\(Reduction, 0\.0f, DamageReductionCap\)", text), (
            "EffectiveDamageReduction no longer clamps against the cap.")

    def test_immunity_is_unreachable_in_the_model(self):
        """The thing the whole issue is about, asked directly."""
        attacker = damage.Attacker(damage=1_000_000.0, damage_type="Demonic")
        defender = damage.Defender(
            health=1e9, armor=1_000_000.0, damage_reduction=1_000.0,
            resistances={"Demonic": 300.0}, tier=1,
            # AND WITH THE MULTIPLICATIVE BUCKET FULL TOO, since issue #665.
            # Twelve sources of 90% is far past anything the passive trees
            # contain and still must not reach immunity.
            damage_reduction_more=(90.0,) * 12)
        landed = damage.average_damage_taken(attacker, defender)
        assert landed > 0.0, (
            "every defensive layer at or past its cap stopped the whole hit, "
            "which is immunity. The design document says no combination of "
            "these layers reaches it.")


class TestTheMultiplicativeBucketIsOneRule:
    """The second damage reduction bucket, added under issue #665.

    THE 75% CAP DOES NOT BIND IT, by the project owner's ruling of 2026-08-17.
    That is safe only because multiplicative stacking cannot reach 100%, so the
    thing that has to be true here is different from the thing the cap enforces
    and needs its own checks.
    """

    def test_the_model_and_the_engine_bound_one_source_the_same(self):
        text = read(SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.h")
        match = re.search(
            r"constexpr float MoreDamageReductionCap\s*=\s*([0-9.]+)f", text)
        assert match, (
            "could not find MoreDamageReductionCap in "
            "CataclysmDamageCalculation.h. If it was renamed, rename it here "
            "too.")
        assert float(match.group(1)) == pytest.approx(
            damage.MORE_DAMAGE_REDUCTION_CAP)

    def test_it_is_a_different_bound_from_the_additive_cap(self):
        """They are not the same number and must not be conflated.

        75 is a balance figure bounding the additive pool. The other exists only
        so that one source cannot be exact immunity, and is deliberately close to
        100 rather than close to 75.
        """
        assert damage.MORE_DAMAGE_REDUCTION_CAP != damage.DAMAGE_REDUCTION_CAP
        assert damage.MORE_DAMAGE_REDUCTION_CAP > damage.DAMAGE_REDUCTION_CAP

    def test_the_model_applies_the_bucket(self):
        text = (REPO_ROOT / "sim" / "cataclysm_sim" / "damage.py").read_text(
            encoding="utf-8")
        assert "combined_more_damage_reduction(\n        defender.damage_reduction_more)" in text, (
            "the mitigation order no longer applies the multiplicative damage "
            "reduction bucket, so twelve passive tree nodes would do nothing.")

    def test_the_engine_applies_the_bucket(self):
        text = read(SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.cpp")
        assert "Combat->GetDamageReductionMore()" in text, (
            "UCataclysmDamageCalculation::Resolve no longer reads the "
            "multiplicative damage reduction attribute.")

    def test_the_design_document_says_what_multiplicative_means(self):
        """The half of issue #665 that is not code.

        The word appears on twelve passive tree nodes and meant nothing until
        this was written down. If the sentence goes, the nodes are ambiguous
        again and nothing else would report it.
        """
        text = read(GDD)
        assert '**"Multiplicative" and "more" are the same word.**' in text, (
            "docs/Cataclysm_GDD_v2.md no longer defines what a multiplicative "
            "damage reduction is. Twelve passive tree nodes depend on it.")
        assert "Damage reduction has two buckets" in text, (
            "the Damage Calculation section no longer describes the two "
            "damage reduction buckets.")
