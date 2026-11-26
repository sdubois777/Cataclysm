"""A kill by a critical strike reaches Clean Kill, and a tick of damage over time never counts as one.

WHY THIS EXISTS. The Shadow's Clean Kill (`Shadow_basic_b_a1`, the Death trees' slice C4a, 2026-11-25) reads "Killing
an enemy with a critical strike restores 1% of your Maximum Health for every 2 points". Three pieces of C++ carry that
fact from the blow to the restoration: the combat events record whether the lethal blow was critical, the death notice
hands it on with a tick of damage over time left out (ruled by the coordinating session on 2026-11-25), and the
player's kill handler restores health only when the notice says so. Continuous integration compiles no C++, so this
reads the three from the source with comments removed. `Cataclysm.KillHooks.` checks the behaviour in the engine.

WHAT IS ASSERTED. Each of the three statements is present exactly once; the restoration is called once, directly
under the notice's test; and the four Clean Kill rows name the stat the C++ reads, one at each second point.
"""

from __future__ import annotations

import csv
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source" / "Cataclysm"
EVENTS = SOURCE / "AbilitySystem" / "CataclysmCombatEvents.cpp"
FERVOUR = SOURCE / "AbilitySystem" / "CataclysmFervour.cpp"
HANDLER = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
EFFECTS = ROOT / "game" / "Data" / "PassiveEffects.csv"


def code_only(path: pathlib.Path) -> str:
    """The source with block and line comments blanked and runs of white space made one, so prose is not code."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"\s+", " ", text)


def test_the_lethal_blow_records_whether_it_was_critical():
    code = code_only(EVENTS)
    assert code.count("Blow.bKillingBlowWasCritical = Outcome.bWasCritical;") == 1


def test_the_death_notice_leaves_out_a_tick_of_damage_over_time():
    code = code_only(EVENTS)
    assert code.count(
        "Notice.bByACriticalStrike = Blow.bKillingBlowWasCritical && !Blow.bDamageOverTime;") == 1, (
        "the death notice must say a kill was by a critical strike only when the killing blow was critical and "
        "was not a tick of damage over time")


def test_the_kill_handler_restores_health_only_for_a_critical_kill():
    code = code_only(HANDLER)
    calls = [m.start() for m in re.finditer(r"UCataclysmFervour::RestoreHealthOnCriticalKill\(", code)]
    assert len(calls) == 1, f"expected one call in {HANDLER.name}; found {len(calls)}"
    guarded = re.search(
        r"if \( ?Notice\.bByACriticalStrike ?\) \{ UCataclysmFervour::RestoreHealthOnCriticalKill\( ?Acting ?\); \}",
        code)
    assert guarded and guarded.start() < calls[0] < guarded.end(), (
        "the restoration is not called directly under `if (Notice.bByACriticalStrike)`")


def test_the_clean_kill_rows_name_the_stat_the_code_reads():
    match = re.search(r'HealthRestoredOnCriticalKillStat = TEXT\("([a-z_]+)"\);', code_only(FERVOUR))
    assert match, f"no HealthRestoredOnCriticalKillStat definition in {FERVOUR.name}"
    with EFFECTS.open(encoding="utf-8-sig", newline="") as handle:
        rows = [row for row in csv.DictReader(handle) if row["Node"] == "Shadow_basic_b_a1"]
    assert {row["Stat"] for row in rows} == {match.group(1)}
    assert sorted(int(row["MinPoints"]) for row in rows) == [2, 4, 6, 8]
