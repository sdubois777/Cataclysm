"""A full class resource bar is the maximum less what minions hold back, in the condition and in the crossing.

WHY THIS EXISTS. Every gain stops at the maximum less what minions, thralls and machines hold back
(`UCataclysmClassResourceAttributeSet::MaximumClassResourceAsked`), but `class_resource_at_maximum` and the full-bar
crossing in `ACataclysmPlayerCharacter::OnClassResourceChanged` compared against the gross maximum, so a character with
a minion out was never "full": a Ritualist with an imp out, or a Necromancer with any minion. The coordinating session
ruled on 2026-11-25 (the Death trees' slice C) that both compare against the maximum less the reserve, on every class.

WHAT IS ASSERTED, read from the C++ source with comments removed, because continuous integration compiles no C++:
the condition's case reads `ClassResourceReserved`; `CurrentConditions` fills it from the same list of commanded
things it already walks; and the crossing reads the reserve. `Cataclysm.KindReserves.AnImpOutLeavesTheBarFullAtThe
MaximumLessItsReserve` and `Cataclysm.KindReserves.FullMeasureCountsAFullBarForASoulCollectorWithAMinionOut` check the
behaviour in the engine.
"""

from __future__ import annotations

import pathlib
import re

SOURCE = pathlib.Path(__file__).resolve().parents[2] / "game" / "Source" / "Cataclysm"
PIPELINE = SOURCE / "AbilitySystem" / "CataclysmStatPipeline.cpp"
ABILITY_SYSTEM = SOURCE / "AbilitySystem" / "CataclysmAbilitySystemComponent.cpp"
PLAYER = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def body_after(code: str, opening: str, closing: str) -> str:
    """The code from the first `opening` to the first `closing` after it."""
    start = code.index(opening)
    return code[start:code.index(closing, start + len(opening))]


def test_the_condition_judges_a_full_bar_against_the_reserve():
    code = code_only(PIPELINE.read_text(encoding="utf-8"))
    # ConditionHolds' case, which returns; the name table's and the asker table's cases do not.
    cases = [m.start() for m in re.finditer(r"case ECataclysmStatCondition::ClassResourceAtMaximum:", code)]
    judged = [code[start:code.index("case ", start + 10)] for start in cases]
    returning = [body for body in judged if "return" in body]
    assert len(returning) == 1, f"expected one returning case, found {len(returning)}"
    assert "ClassResourceReserved" in returning[0], (
        "class_resource_at_maximum compares against the gross maximum, so a character with a minion out is never full")


def test_current_conditions_fills_the_reserve_from_the_commanded_list():
    code = code_only(ABILITY_SYSTEM.read_text(encoding="utf-8"))
    body = body_after(code, "FCataclysmStatConditions UCataclysmAbilitySystemComponent::CurrentConditions(",
                      "\n}\n")
    assert re.search(r"State\.ClassResourceReserved\s*=\s*UCataclysmCommand::ReservedFervourAmong\(", body), (
        "CurrentConditions does not fill ClassResourceReserved, so the condition reads a reserve of nought")


def test_the_full_bar_crossing_reads_the_reserve():
    code = code_only(PLAYER.read_text(encoding="utf-8"))
    body = body_after(code, "void ACataclysmPlayerCharacter::OnClassResourceChanged(", "\n}\n")
    assert "UCataclysmCommand::ReservedFervourOf(" in body, (
        "the full-bar crossing compares against the gross maximum, so it never opens for a character with a minion out")
