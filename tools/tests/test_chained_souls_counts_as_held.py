"""Fervour counted as held: which readings of the amount held add Chained Souls' share, and which do not.

WHY THIS EXISTS. The Death trees' slice C3e (2026-11-25). Chained Souls says "8% per point of the Fervour your minions,
gadgets and traps hold back counts as Fervour you hold, for everything that counts Fervour held". The coordinating
session ruled that it counts for every reading of the amount held and NOT for whether the bar is full, nor for what
Phylactery spends. The share is `FCataclysmStatConditions::ClassResourceCountedAsHeld`, filled once a conditions fill
from the reserve, and the readings add it through `UCataclysmStatPipeline::FervourHeldCounted`.

WHAT IS ASSERTED. Read from the C++ with comments removed, because continuous integration compiles no C++: the three
readings of the amount held, Empty Hands' new one and the Fervour thresholds use the counted figure; the full bar and
Phylactery read the pool alone; the share is filled from the reserve, and Long Shadow's record counts it too.
`Cataclysm.FervourCounted.` checks the behaviour in the engine.
"""

from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def read(name: str) -> str:
    return code_only((SOURCE / name).read_text(encoding="utf-8"))


def body_of(code: str, signature: str) -> str:
    start = code.index(signature)
    return code[start:code.index("\n}\n", start)]


def case_of(code: str, label: str) -> str:
    """The text of one `case` of `ConditionHolds`, up to the next `case`."""
    start = code.index(f"case ECataclysmStatCondition::{label}:")
    return code[start:code.index("case ECataclysmStatCondition::", start + 10)]


COUNTED = "UCataclysmStatPipeline::FervourHeldCounted(State)"


def test_every_reading_of_the_amount_held_counts_the_share():
    pipeline = read("CataclysmStatPipeline.cpp")
    for label in ("ClassResourceAbovePercent", "ClassResourcePointsAtLeast", "ClassResourcePointsBelow"):
        assert COUNTED in case_of(pipeline, label), f"{label} reads the pool without Chained Souls' share"
    scale = pipeline[pipeline.index("case ECataclysmStatScale::PerPointOfClassResourceHeld:"):]
    scale = scale[:scale.index("case ECataclysmStatScale::", 10)]
    assert COUNTED in scale, "the held scale reads the pool without Chained Souls' share"


def test_the_full_bar_and_phylactery_read_the_pool_alone():
    """THE CONTROL, and the ruling's other half."""
    pipeline = read("CataclysmStatPipeline.cpp")
    # THE CASE IN `ConditionHolds`, found by the line that works out what the bar can hold: the label's first two
    # occurrences are in `ConditionTakesAValue` and `WhatConditionDependsOn`.
    fillable = pipeline.index("const float Fillable")
    start = pipeline.rindex("case ECataclysmStatCondition::ClassResourceAtMaximum:", 0, fillable)
    full = pipeline[start:pipeline.index("case ECataclysmStatCondition::", fillable)]
    assert "FervourHeldCounted" not in full, "the full bar counts Chained Souls' share"
    assert "State.ClassResourceHeld >= Fillable" in full, "re-read this check: the full bar's comparison moved"
    asc = read("CataclysmAbilitySystemComponent.cpp")
    for signature in ("bool UCataclysmAbilitySystemComponent::MaySaveLethalBlowWithFervour(",
                      "float UCataclysmAbilitySystemComponent::TakeFervourForLethalSave("):
        body = body_of(asc, signature)
        assert "FervourHeldCounted" not in body and "ReservedFervourCountedAsHeld" not in body, (
            f"{signature} counts Chained Souls' share")


def test_the_share_is_filled_from_the_reserve_and_tolling_count_counts_it():
    asc = read("CataclysmAbilitySystemComponent.cpp")
    fill = body_of(asc, "FCataclysmStatConditions UCataclysmAbilitySystemComponent::CurrentConditions(")
    assert "State.ClassResourceCountedAsHeld = ReservedFervourCountedAsHeld(State.ClassResourceReserved)" in fill
    assert fill.index("State.ClassResourceReserved =") < fill.index("State.ClassResourceCountedAsHeld ="), (
        "the share is worked out before the reserve it is a share of")
    # TOLLING COUNT (issue #2951) READS THE CONDITIONS' OWN ANSWER, which the fill above works out on the Fervour counted
    # as held, so Chained Souls' share counts for it. Long Shadow's record, which this checked until then, was retired.
    raised = body_of(asc, "int32 UCataclysmAbilitySystemComponent::RaiseFervourThresholdsReached(")
    assert "CurrentConditions().FervourThresholdsReached" in raised, "Tolling Count does not count Chained Souls' share"
    share = body_of(asc, "float UCataclysmAbilitySystemComponent::ReservedFervourCountedAsHeld(")
    assert "StatWithNoReading(" in share and "StatForSkill(" not in share, (
        "Chained Souls' stat is asked through the pipeline while the conditions are filled")
