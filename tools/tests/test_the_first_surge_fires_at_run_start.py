"""The first surge fires at run start, on day 0, for every run the game begins. Issue #2733.

`docs/Cataclysm_GDD_v2.md`, the Surge section: "A Surge is triggered at run start" and "The first Surge, at run
start, brings 12 dungeons". The coordinating session ruled on 2026-11-17, under the owner's delegation, that the game
follows the design: a new run's empire map shows its dungeons before any day has passed.

WHERE IT FIRES. `UCataclysmGameInstance::BeginACharactersEmpireRun` calls `UCataclysmEmpireRun::FireOpeningSurge`
straight after `BeginForCharacter`, and after it has loaded the modifier table, so the opening wave carries modifiers.
Not inside `BeginForCharacter`, because that is also how `FCataclysmSaveApply::RunInto` builds the objects of a run it
restores from a save, and a wave fired there would be rolled on every load and written over.

WHY THIS IS CHECKED FROM PYTHON AS WELL. The Unreal tests of the group `Cataclysm.FirstSurge.` and
`Cataclysm.EmpireRun.TheOpeningSurgeFiresOnDayZeroOnceWithTheWaveDayOneWouldHaveBrought` measure the behaviour; these
check, in the fast suite, the three places the behaviour depends on: the one door calls it, the restore does not, and
no other code makes a run object that would need to.

AND THE SIMULATION STILL DIFFERS, which the last test here holds on purpose. `Simulation._empire_day` moves the day on
before it asks whether a surge is due, so its opening wave lands on day 1, as `Begin` followed by `AdvanceDay` does in
the game. The ruling says the simulation is not changed in this work. When it is, that test fails, and the header of
`FireOpeningSurge` and the comment on `UCataclysmEmpireRun::Begin` have to be read again.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
GAME_INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
SAVE_APPLY = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveApply.cpp"
EMPIRE_RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed, so a comment naming a call cannot pass for the call, and every run of
    white space made one space, so `FireSurge(Today, /* bFromCityFall */ false, Report)` reads as one call."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"\s+", " ", text)


def body(path: pathlib.Path, signature: str) -> str:
    """The braces of the one function whose definition starts with `signature`, comments removed."""
    text = code(path)
    assert text.count(signature) == 1, f"{path.name} holds {signature!r} {text.count(signature)} times, not once"
    opening = text.index("{", text.index(signature))
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {path.name} is not closed")


def test_the_one_door_into_a_new_run_fires_the_opening_surge_after_the_pool_and_the_begin() -> None:
    begun = body(GAME_INSTANCE, "UCataclysmEmpireRun* UCataclysmGameInstance::BeginACharactersEmpireRun(")

    pool = begun.find("EmpireRun->ModifierPool = ")
    began = begun.find("EmpireRun->BeginForCharacter(")
    fired = begun.find("EmpireRun->FireOpeningSurge()")
    assert fired != -1, (
        "UCataclysmGameInstance::BeginACharactersEmpireRun no longer calls EmpireRun->FireOpeningSurge(), so a run the "
        "game begins waits for day 1 for its first wave and its empire map is empty until then. Issue #2733")
    assert begun.count("FireOpeningSurge(") == 1, "BeginACharactersEmpireRun asks for the opening surge more than once"
    assert pool != -1 and began != -1 and pool < began < fired, (
        "BeginACharactersEmpireRun must load the modifier table, then begin the run, then fire the opening surge: a "
        "surge fired before the table is loaded lands dungeons with no modifiers, and one fired before the run begins "
        "is fired at a run that has no map")


def test_a_restored_run_does_not_fire_it_and_begin_does_not_either() -> None:
    restored = body(SAVE_APPLY, "bool FCataclysmSaveApply::RunInto(")
    assert "Run.Begin(" in restored, (
        "FCataclysmSaveApply::RunInto no longer builds the run's objects through Begin, which is the reason given for "
        "firing the opening surge outside Begin; read the reason again before moving it")
    assert "FireOpeningSurge" not in restored and "FireSurge" not in restored, (
        "FCataclysmSaveApply::RunInto fires a surge, so a run loaded from a save lands a wave it was not saved with")

    for signature in ("void UCataclysmEmpireRun::Begin(", "void UCataclysmEmpireRun::BeginForCharacter("):
        begin = body(EMPIRE_RUN, signature)
        assert "FireOpeningSurge" not in begin and "FireSurge" not in begin, (
            f"{signature.split('::')[1]} fires a surge. RunInto builds a restored run through it, so every load would "
            "roll a wave and write over it; the game fires the opening surge in "
            "UCataclysmGameInstance::BeginACharactersEmpireRun instead")


def test_the_opening_surge_asks_for_day_0_and_none_fired_before_it_fires() -> None:
    opening = body(EMPIRE_RUN, "FCataclysmDayReport UCataclysmEmpireRun::FireOpeningSurge(")
    refusal = re.search(r"if \(Today != 0 \|\| Surges->SurgesFired != 0 \|\| !Surges->IsDue\(Today\)\)", opening)
    fired = opening.find("FireSurge(Today, false, Report);")
    assert refusal and fired != -1 and refusal.start() < fired, (
        "UCataclysmEmpireRun::FireOpeningSurge no longer refuses a day other than 0, a run that has fired a surge and "
        "a run with none due before it fires, so it could fire twice")
    assert "AdvanceDay(" not in opening, (
        "FireOpeningSurge moves the clock. The opening surge lands on day 0, before any day has passed")


def test_no_other_code_makes_a_run_that_would_miss_the_opening_surge() -> None:
    """A run object is made in exactly two places outside the tests: the one door, and the save's restore.

    A THIRD WOULD BE A NEW WAY FOR A RUN TO BEGIN, which this fails on so whoever adds it decides whether it fires the
    opening surge. The search is for the operation, not for the files that happen to make runs today.
    """
    makers = sorted(
        path.relative_to(REPO_ROOT).as_posix()
        for path in SOURCE.rglob("*.cpp")
        if "/Tests/" not in path.as_posix() and "NewObject<UCataclysmEmpireRun>" in code(path)
    )
    assert makers == [
        "game/Source/Cataclysm/Player/CataclysmGameInstance.cpp",
        "game/Source/Cataclysm/Save/CataclysmSaveLoad.cpp",
    ], (
        "A run object is now made outside UCataclysmGameInstance::BeginACharactersEmpireRun and the save's restore, in "
        f"{makers}. A new run must fire its opening surge on day 0 (UCataclysmEmpireRun::FireOpeningSurge, issue "
        "#2733); a restored one must not")
    instance = code(SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp")
    assert instance.count("NewObject<UCataclysmEmpireRun>") == 1, (
        "CataclysmGameInstance.cpp makes a run in more than one place; only BeginACharactersEmpireRun fires the "
        "opening surge")


def test_the_model_still_lands_its_opening_wave_on_day_1() -> None:
    """THE KNOWN DIFFERENCE, held so a change to the model is noticed. See the module docstring."""
    from dataclasses import replace

    from cataclysm_sim.config import TuningConfig
    from cataclysm_sim.engine import Simulation

    # EVERY CATACLYSM COUNTED AS BUILT, so seed 7 faces War, whose wave size multiplier (`count_mult` in
    # `sim/cataclysm_sim/patterns.py`) is 1.0, so its opening wave is exactly `surge_opening_count`. The two built ones
    # size their waves by design: a wave is `surge_opening_count` times `count_mult` to the power
    # `cataclysm_volume_exponent` (0.7), rounded, so Death's 1.8 gives 18 (seeds 1, 2, 4) and Demonic's 0.9 gives 11
    # (seeds 3, 7, 8). Measured 2026-11-26 when War left the built list and seed 7 began facing Demonic. This test is
    # about the day the wave lands, not its size.
    cfg = replace(TuningConfig(), BUILT_CATACLYSMS=TuningConfig().CATACLYSM_ROSTER)
    simulation = Simulation(cfg, seed=7)
    standing_at_start = len(simulation.dungeons)
    simulation._empire_day()

    assert standing_at_start == 0 and simulation.day == 1 and len(simulation.dungeons) == cfg.surge_opening_count, (
        "The simulation now has dungeons standing before its first day, or no longer lands its opening wave on day 1: "
        f"{standing_at_start} at the start, {len(simulation.dungeons)} on day {simulation.day}. The game fires its "
        "opening surge on day 0 (UCataclysmEmpireRun::FireOpeningSurge, issue #2733) and leaves "
        "UCataclysmEmpireRun::Begin on the model's day 1. If the model moved to day 0, read FireOpeningSurge's header "
        "and Begin's comment again, and decide whether Begin should fire it too")
