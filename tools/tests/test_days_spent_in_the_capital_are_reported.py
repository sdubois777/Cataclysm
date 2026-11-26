"""Days spent in the capital say what they will do before they are spent, and what they did after.

WHY THIS EXISTS. Issue #2857. The owner saw dungeons gone after a clear and thought the clear had taken them; the log
showed they had resolved during 45 days spent at the forge, whose craft ran `Run->AdvanceDays(Step.Days)` and threw away
the `TArray<FCataclysmDayReport>` it returned, so nothing anywhere said a dungeon had resolved or a city had been hit. The
owner ruled on 2026-11-22, "ah right, that makes sense. And yeah those are good calls", that the forge keeps the reports
and shows them, writes the walk's PLAY LOOP words for them, and warns before a craft which dungeons will resolve within
its days. Continuous integration compiles no C++, so the automation tests `Cataclysm.Forge.`, `Cataclysm.ForgeScreen.`,
`Cataclysm.EmpireTreeScreen.`, `Cataclysm.PlayLoop.` and `Cataclysm.EmpireRun.` never run on a pull request; this check
holds what can be read from the files:

  1. no caller in the game throws away the reports a run of days returns -- the shape of the bug itself;
  2. the forge's one craft sequence and the empire upgrade tree's respec name the dungeons standing before their days,
     keep the days' reports, say the days' account after their own line, and keep it for their screen;
  3. every question the forge's screen asks before a craft that costs days ends with the timers those days run out, and
     every craft it reports done ends with what the days did; the respec's two presses do the same.

WHAT IS NOT. That the words and the forecast are right: the automation tests named above check those.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
GAME = SOURCE / "Cataclysm"
CRAFT = GAME / "Player" / "CataclysmForgeCraft.cpp"
GAME_INSTANCE = GAME / "Player" / "CataclysmGameInstance.cpp"
FORGE_WIDGET = GAME / "Interface" / "CataclysmForgeWidget.cpp"
TREE_WIDGET = GAME / "Interface" / "CataclysmEmpireTreeWidget.cpp"
PLAYER_CHARACTER = GAME / "Character" / "CataclysmPlayerCharacter.cpp"

#: The one day the game may still pass without reading its report: `Cataclysm.WalkDungeon`, a console command, spends a
#: day only on a board with no dungeon on it, so that the command has one to walk. No timer can run out on an empty board.
#: Listed in issue #2857's decisions entry. Any other is a new caller this check was written to catch.
ALLOWED_DISCARDS = {("Character/CataclysmPlayerCharacter.cpp", "->AdvanceDay(")}


def strip_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, string literals kept."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j == -1 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def code(path: pathlib.Path) -> str:
    assert path.is_file(), f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. If it moved, move it here too."
    return strip_comments(path.read_bytes().decode("utf-8"))


def body(text: str, signature: str) -> str:
    assert text.count(signature) == 1, f"{signature} should be defined once; it appears {text.count(signature)} times"
    open_at = text.find("{", text.find(signature))
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return squeeze(text[open_at + 1 : i])
    raise AssertionError(f"{signature} is never closed")


def discarded_day_calls(text: str) -> list[tuple[str, str]]:
    """Each `X->AdvanceDays(` or `X->AdvanceDay(` that begins a statement of its own, so its reports are thrown away."""
    found = []
    for match in re.finditer(r"->(AdvanceDays?)\(", text):
        start = max(text.rfind(";", 0, match.start()), text.rfind("{", 0, match.start()),
                    text.rfind("}", 0, match.start()))
        before = text[start + 1 : match.start()].strip()
        # A STATEMENT THAT IS ONLY THE OBJECT, "Run" or "Scene.Run": nothing assigns, iterates, returns or passes on
        # what the days reported.
        if re.fullmatch(r"[\w.]+", before):
            line = text.count("\n", 0, match.start()) + 1
            found.append((f"->{match.group(1)}(", f"{before}->{match.group(1)}( at line {line}"))
    return found


def test_the_scan_finds_the_callers_it_is_for() -> None:
    # A SCAN THAT READS NOTHING PASSES. The game passes days from the forge, the respec, a death and a console command
    # at least, and the console command's empty-board day is the one bare call it allows.
    callers = 0
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.parts:
            continue
        callers += len(re.findall(r"->AdvanceDays?\(", code(path)))
    assert callers >= 5, f"only {callers} calls to AdvanceDays or AdvanceDay found outside the tests; the scan is wrong"
    bare = discarded_day_calls(code(PLAYER_CHARACTER))
    assert [kind for kind, _ in bare] == ["->AdvanceDay("], (
        f"expected Cataclysm.WalkDungeon's one empty-board day as the only bare call in the character's file: {bare}")


def test_no_caller_throws_away_what_the_days_reported() -> None:
    thrown = []
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.parts:
            continue
        relative = path.relative_to(SOURCE / path.relative_to(SOURCE).parts[0]).as_posix()
        for kind, where in discarded_day_calls(code(path)):
            if (relative, kind) not in ALLOWED_DISCARDS:
                thrown.append(f"{path.relative_to(REPO_ROOT).as_posix()}: {where}")
    assert not thrown, (
        "these calls pass days and throw away the reports they return, so a dungeon those days resolve and the city it "
        "hits are never told to the player or the log (issue #2857):\n  " + "\n  ".join(thrown))


def test_the_forges_craft_and_the_respec_keep_and_tell_their_days() -> None:
    for path, signature, days, own_line, lead in (
            (CRAFT, "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(", "Run->AdvanceDays(Step.Days);",
             "SayTheLine(Name, DayBefore, Run->Day());", 'TEXT("at the forge")'),
            (GAME_INSTANCE, "bool UCataclysmGameInstance::RespecEmpireTree(", "Run->AdvanceDays(Days);",
             "FCataclysmPlayLoopLog::TreeRespecLine(", 'TEXT("for the respec")')):
        text = body(code(path), signature)
        named = text.find("FCataclysmPlayLoopLog::NamesStanding(Run)")
        ran = text.find(f"TheDays = {days}")
        account = text.find("FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::AwayAccount(DayBefore, Run->Day(), TheDays, "
                            "NamesBeforeTheDays, Run, " + lead + "))")
        kept = text.find("KeepCapitalDays(WorldContext, DayBefore, Run->Day(), TheDays, NamesBeforeTheDays, Run);")
        said = text.find(own_line)
        assert -1 not in (named, ran, account, kept, said), (
            f"{signature}: expected the dungeons named, the days' reports kept as TheDays, the account said with "
            f"{lead} and kept for the screen; found at {named}, {ran}, {account}, {kept} (its own line at {said})")
        assert named < ran, f"{signature} names the dungeons after the days, when a dungeon they resolved is gone"
        assert said < account, f"{signature} says what the days did before its own line"
        assert ran < kept, f"{signature} keeps the days' account for the screen before the days have run"


def test_every_forge_question_warns_and_every_forge_craft_done_says_what_its_days_did() -> None:
    text = body(code(FORGE_WIDGET), "void UCataclysmForgeWidget::HandleActionClicked(")
    questions = re.findall(r"Message = UCataclysmForge::(\w+Question)\(", text)
    expected = ["SocketingQuestion", "SocketWorkQuestion", "AffixWorkQuestion", "AffixChangeQuestion",
                "DismantleQuestion", "CleanseQuestion", "DamageTypeQuestion", "EnchantmentWorkQuestion", "UpgradeQuestion"]
    assert questions == expected, f"the forge screen's questions changed: {questions}; update this check with them"
    # A DISMANTLE TAKES NO DAYS (issue #2848, slice S2: "no gold and no days"), so it has no timers to warn of and no
    # days to report. It merged on development after this check was written, beside #2857's rebase.
    dayless = {"DismantleQuestion"}
    warned = 0
    for match in re.finditer(r"Message = UCataclysmForge::(\w+Question)\(", text):
        statement = text[match.start() : text.find(";", match.start())]
        if match.group(1) in dayless:
            continue
        warned += 1 if "TimersRunningOutIn(" in statement else 0
    with_days = len(expected) - len(dayless)
    assert warned == with_days, (
        f"{with_days - warned} of the forge screen's {with_days} questions of a craft that takes days do not end with "
        "the dungeons whose timers the craft's days run out (TimersRunningOutIn)")
    # EVERY CRAFT REPORTED DONE: seven in a `case ECataclysmForgeResult::Done:` and the cleanse, which asks `== Done`.
    cases = [text[match.end() : text.find("break;", match.end())]
             for match in re.finditer(r"case ECataclysmForgeResult::Done:", text)]
    cleanse = text[text.find("UCataclysmForgeCraft::Cleanse(") :]
    cases.append(cleanse[: cleanse.find(";")])
    assert len(cases) == with_days, f"expected {with_days} crafts reported done, read {len(cases)}"
    untold = [case[:80] for case in cases if "WhatTheDaysDid()" not in case]
    assert not untold, (
        "these crafts are reported done without what their days did (WhatTheDaysDid), so a dungeon those days resolved "
        "goes unmentioned on the screen:\n  " + "\n  ".join(untold))


def test_the_respecs_two_presses_warn_and_then_tell() -> None:
    text = body(code(TREE_WIDGET), "bool UCataclysmEmpireTreeWidget::PressRespec()")
    warned = text.find("UCataclysmEmpireMapLayout::TimersRunningOutText(")
    respecced = text.find("UCataclysmGameInstance::RespecEmpireTree(")
    told = text.find("LastRefusal = UCataclysmGameInstance::CapitalDaysAccountIn(Context());")
    assert -1 not in (warned, respecced, told), (
        "the respec's first press no longer warns of the timers its days run out, or its second no longer says what "
        f"they did: {warned}, {respecced}, {told}")
    assert warned < respecced < told
