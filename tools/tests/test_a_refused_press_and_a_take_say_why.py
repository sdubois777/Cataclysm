"""A refused skill press, and every outcome of a take, say why in the play log, naming the numbers.

WHY THIS EXISTS. Issue #2801, from the project owner's play-test of 2026-11-21: "subjugate constantly says I can't use
it even when I have enough resources and it's off cd, not sure what's going on there." No check that refuses a press
wrote anything at any level, and every refusal of a take after the blow wrote at `Verbose`, which no play log holds; so
the log of that play-test could not say which check refused. The lines below are the instrumentation for one press by
the owner to settle it.

WHY IT IS CHECKED FROM PYTHON. No automation test reads the log, and continuous integration compiles no C++. A line
dropped back to `Verbose`, or a figure left out of the line, looks fine in a diff and fails nothing else.

WHAT IS CHECKED:

  1. `ProcessAbilityInput` logs `PressRefusalLine` at `Log`, with "PRESS REFUSED:", when `TryActivateAbility` refuses,
     reading the granted instance rather than the class default object;
  2. `PressRefusalLine` reads every check a press is judged on: the Fervour, the pool the cost is paid from, the
     charges, the lock, the hard stops and a planted weapon;
  3. `FervourFiguresFor` names the ceiling the pool can be filled to now and what holds the rest back, by kind;
  4. every exit of `UCataclysmSummonSkill::Possess` after it starts logs at `Log`, none at `Verbose`, and a take says so.

The Fervour figures' arithmetic is tested in `Cataclysm.PressRefusal.` (`CataclysmPressRefusalTests.cpp`).
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ABILITY = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
COMPONENT = ABILITY / "CataclysmAbilitySystemComponent.cpp"
TEMPLATE = ABILITY / "CataclysmSkillTemplate.cpp"
TEMPLATES = ABILITY / "CataclysmSkillTemplates.cpp"


def code(path: pathlib.Path) -> str:
    """The file with each `//` comment taken off its line, so a mention in a comment cannot satisfy a check."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def squeeze(text: str) -> str:
    """Whitespace runs as one space, so a wrapped call reads as one line."""
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str, where: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where}: the body of {signature!r} never closes")


def test_a_refused_press_is_logged_at_log_with_every_figure() -> None:
    process = squeeze(body(code(COMPONENT), "void UCataclysmAbilitySystemComponent::ProcessAbilityInput()",
                           "CataclysmAbilitySystemComponent.cpp"))
    refused = process.find("if (!TryActivateAbility(Handle, /*bAllowRemoteActivation=*/true))")
    assert refused != -1, "a refused press is not asked about: TryActivateAbility's answer is ignored"
    logged = process.find('UE_LOG(LogCataclysm, Log, TEXT("PRESS REFUSED: %s"), *Skill->PressRefusalLine(this));',
                          refused)
    assert logged != -1, "a refused press is not logged at Log level with its figures"
    assert "GetPrimaryInstance()" in process[refused:logged], (
        "the refused skill is not read from its granted instance, which holds its name and row")


def test_the_refusal_line_reads_every_check_a_press_is_judged_on() -> None:
    line = squeeze(body(code(TEMPLATE), "FString UCataclysmSkillTemplate::PressRefusalLine(", "CataclysmSkillTemplate.cpp"))
    for figure, check in (
        ("FervourFiguresFor(AbilitySystem, Who, FervourCostFor(AbilitySystem))", "the Fervour against its cost"),
        ("ManaCostFor(AbilitySystem)", "the cost of the pool it is paid from"),
        ("ManaCostPaidAsHealthPercent(AbilitySystem)", "a cost moved onto health"),
        ("SkillChargesHeld(Slot, SkillTagsForStats())", "the charges, which is the cooldown"),
        ("UCataclysmSkillSlots::LockedStat", "the lock"),
        ("UCataclysmSkillEffects::CannotAct(Who)", "being unable to act"),
        ("UCataclysmFear::IsFeared(Who)", "fear"),
        ("ACataclysmPlantedWeapon::HeldBy(Who)", "a planted weapon"),
    ):
        assert figure in line, f"the refusal line does not read {check}: {figure}"


def test_the_fervour_figures_name_the_ceiling_and_who_holds_the_rest() -> None:
    figures = squeeze(body(code(TEMPLATE), "FString UCataclysmSkillTemplate::FervourFiguresFor(",
                           "CataclysmSkillTemplate.cpp"))
    assert "Resource->MaximumClassResourceAsked()" in figures, (
        "the Fervour figures do not name the ceiling the pool can be filled to now")
    assert "ReservedFervourByKindText(Commander)" in figures, "the Fervour figures do not name who holds the rest back"


def test_every_outcome_of_a_take_is_logged_at_log() -> None:
    possess = squeeze(body(code(TEMPLATES), "bool UCataclysmSummonSkill::Possess()", "CataclysmSkillTemplates.cpp"))
    assert "UE_LOG(LogCataclysm, Verbose" not in possess, "an outcome of a take is still logged at Verbose"
    assert possess.count("UE_LOG(LogCataclysm, Log,") == 7, (
        "a take has seven outcomes -- no avatar, no enemy in range, no ability system, too healthy, no room, the "
        "command's refusal and the take -- and each is logged at Log")
    assert possess.count('TEXT("TAKE REFUSED:') == 6, "each refusal of a take does not say TAKE REFUSED"
    assert "TAKE: '%s' took '%s', reserving %.0f." in possess, "a take that succeeds does not say so"
