"""The Soul Collector's four Fervour thresholds: the figures the vows state, where the engine works them out, and the
one capstone row with no option the generator now accepts.

WHY THIS EXISTS. The Death trees' slice C3d (2026-11-25). Each of the Soul Collector's four vows opens a Fervour
threshold: "Reaching 25 points also opens the first Fervour threshold: while you hold 20 or more Fervour, all your
skills have 20% more Area of Effect", and the same at 50 for 40, 100 for 60 and 200 for 100. The condition
`fervour_threshold_reached` and the scale `fervour_thresholds_reached` read one answer
`UCataclysmAbilitySystemComponent::FervourThresholdsReachedFor` works out once a conditions fill; Generous Count lowers
every figure and Long Shadow keeps a threshold for seconds after a fall. Each vow's clause is a row with no option on
its capstone, which the generator accepts only where the capstone's own sentence states a clause (the coordinating
session's ruling 4).

WHAT IS ASSERTED. The figures and their points against the four sentences, the generator's list and the C++ table; that
the generator refuses a figure that is none of the four; that a capstone sentence saying only its choice states no
clause; and, read from the C++ with comments removed, that each hook is where it must be. `Cataclysm.FervourThreshold.`
checks the behaviour in the engine.
"""

from __future__ import annotations

import pathlib
import re
import sys

import openpyxl
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))

import generate_datatables as gen  # noqa: E402
from test_generate_datatables import workbook_with  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
VOW = re.compile(r"Reaching (\d+) points also opens the (first|second|third|fourth) Fervour threshold: "
                 r"while you hold (\d+) or more Fervour")


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def read(relative: str) -> str:
    return code_only((SOURCE / relative).read_text(encoding="utf-8"))


def body_of(code: str, signature: str) -> str:
    start = code.index(signature)
    return code[start:code.index("\n}\n", start)]


def the_vows() -> list[tuple[int, float]]:
    """(points that open it, figure held), in order, read from the Soul Collector's capstone sentences."""
    found = {}
    for row in gen.passive_nodes():
        if row["Name"].startswith("SoulCollector_capstone_"):
            match = VOW.search(row["Description"])
            if match:
                found[match.group(2)] = (int(match.group(1)), float(match.group(3)))
    return [found[word] for word in ("first", "second", "third", "fourth")]


def test_the_figures_and_their_points_are_the_four_vows():
    vows = the_vows()
    assert [figure for _, figure in vows] == list(gen.FERVOUR_THRESHOLDS)
    pipeline = read("AbilitySystem/CataclysmStatPipeline.cpp")
    figures = re.findall(r"case (\d): return ([\d.]+)f;", body_of(pipeline, "float CataclysmFervourThresholds::Figure("))
    points = re.findall(r"case (\d): return (\d+);", body_of(pipeline, "int32 CataclysmFervourThresholds::OpensAtPoints("))
    assert [float(value) for _, value in figures] == [figure for _, figure in vows], figures
    assert [int(value) for _, value in points] == [opens for opens, _ in vows], points


def threshold_row(tmp_path, figure):
    header = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition", "Condition Value",
              "Scale", "Scale Step", "Option", "Reach Metres", "Min Points", "Scale Max Steps",
              *gen.PASSIVE_ACTION_COLUMNS, "Per Point From",
              # AND MAX POINTS, required since the War line (the bundle 3 merge).
              "Max Points"]
    cells = {"Node": "A_node", "Stat": "area_of_effect", "Value Kind": "more", "Value Per Point": 20,
             "Condition": "fervour_threshold_reached", "Condition Value": figure}
    row = [cells.get(name) for name in header]
    return openpyxl.load_workbook(workbook_with(tmp_path / "threshold.xlsx", {"Passive Effects": [header, row]}))


def test_a_figure_that_is_none_of_the_four_is_refused(tmp_path):
    out = gen.passive_effects(threshold_row(tmp_path, 40))
    assert [(r["Condition"], r["ConditionValue"]) for r in out] == [("fervour_threshold_reached", 40.0)]
    with pytest.raises(gen.DataError, match="the four are 20, 40, 60, 100"):
        gen.passive_effects(threshold_row(tmp_path, 50))


def test_only_a_capstone_whose_sentence_states_a_clause_may_carry_a_row_with_no_option():
    capstones = {row["Name"]: row["Description"] for row in gen.passive_nodes()
                 if str(row.get("Kind", "")).strip().lower() == "capstone"}
    for name in ("SoulCollector_capstone_25", "SoulCollector_capstone_50", "SoulCollector_capstone_100",
                 "SoulCollector_capstone_200"):
        assert gen.capstone_states_a_clause(capstones[name]), name
    # THE CONTROL: a capstone whose sentence offers its choice and nothing more, which every other tree's are.
    choice_only = [name for name, sentence in capstones.items() if not gen.capstone_states_a_clause(sentence)]
    assert choice_only, "every capstone states a clause: re-read this check"
    assert not gen.capstone_states_a_clause("Unlocks at 25 points spent. Choose one. The choice is permanent.")


def test_the_engine_works_the_answer_out_once():
    asc = read("AbilitySystem/CataclysmAbilitySystemComponent.cpp")
    fill = body_of(asc, "FCataclysmStatConditions UCataclysmAbilitySystemComponent::CurrentConditions(")
    # SINCE SLICE C3e, ON THE FERVOUR COUNTED AS HELD: Chained Souls' share counts for the thresholds too.
    assert re.search(r"State\.FervourThresholdsReached\s*=\s*FervourThresholdsReachedFor\(\s*"
                     r"UCataclysmStatPipeline::FervourHeldCounted\(State\),\s*State\.ClassPointsSpent\)", fill), (
        "the conditions fill does not work the thresholds out on the Fervour counted as held")
    reached = body_of(asc, "int32 UCataclysmAbilitySystemComponent::FervourThresholdsReachedFor(")
    # READ WITH NO READING: asking either through the pipeline here would fill the conditions again.
    # ONE STAT SINCE ISSUE #2951: Generous Count's. Long Shadow's seconds, and the record they read, were retired.
    assert "StatForSkill(" not in reached and reached.count("StatWithNoReading(") == 1, (
        "Generous Count's stat is asked through the pipeline while the conditions are filled")
    assert "->FervourThresholdStep(UCataclysmRegeneration::StepSeconds)" in read("Character/CataclysmCharacterBase.cpp"), (
        "the regeneration step does not run the thresholds' step")
