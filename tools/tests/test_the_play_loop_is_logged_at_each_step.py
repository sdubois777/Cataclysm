"""Each step of the main loop says one PLAY LOOP line, and the owner's checklist quotes only lines the game says.

WHY THIS EXISTS. Issue #2733 slice 4: the project owner presses Play once and reads the loop back from the log, step by
step, with `docs/Play_Loop_Checklist.md` saying what to click and what each step should show. `FCataclysmPlayLoopLog`
builds the lines; ten places in the game say them. An automation test reaches some of those places -- a walk to its
last floor, a death, a level gained -- but not the front end's start, a press on Begin or on a saved character, or the
capital's Walk, which only a real level reaches. So the calls are checked here, by reading the C++. The tenth, a
level gained, is issue #2744's; the eleventh to thirteenth, the empire upgrade tree's point, option and respec, are
issue #2741 slice 2's; the fourteenth to twentieth, the character's build -- a passive point, a capstone option, the
tree reset, attribute points spent and reset, a skill chosen and a choice forgotten -- are issue #2763's; the
twenty-first and twenty-second, the forge's upgrade and combine, are the owner's ruling of 2026-11-19; the
twenty-third and twenty-fourth, a piece of gear picked up and one worn, are the coordinating session's ruling of
2026-11-19; the twenty-fifth, a skill level gained, is issue #2764's. The thirty-fourth and thirty-fifth, what days spent
at the forge and on a respec did, are issue #2857's. The fortieth, what the empire tree's material nodes kept at the
forge, is #47 slice F6b's; the forty-first, what its affix nodes did to a forge craft's item, #2741's crafting slices C3
and C4's.

WHAT IS CHECKED, by reading the C++ with comments removed:

  1. each of the forty-one places says its builder's line;
  2. each says it after the step it reports -- the floor's line after the floor's day is spent, the clear's account
     after the walk's days have run, the death's after the death's own days;
  3. the dungeons are named before the days run, because a dungeon those days resolve is gone from the run after;
  4. the capital empties the carried account after saying it, so it is said once;
  5. every line `docs/Play_Loop_Checklist.md` quotes matches a builder's format, and every builder is quoted;
  6. `game/README.md` links the checklist.

WHAT IS NOT. That the words are right for the figures given: the `Cataclysm.PlayLoop.` automation tests check those.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
LOG_CPP = SOURCE / "Player" / "CataclysmPlayLoopLog.cpp"
CHECKLIST = REPO_ROOT / "docs" / "Play_Loop_Checklist.md"
GAME_README = REPO_ROOT / "game" / "README.md"

#: The forty-one places, by file and signature, and the builder each one says.
STEPS = [
    ("Player/CataclysmFrontEndGameMode.cpp", "void ACataclysmFrontEndPlayerController::BeginPlay()", "FrontEndLine"),
    ("Interface/CataclysmNewCharacterWidget.cpp", "bool UCataclysmNewCharacterWidget::ConfirmInto(", "NewCharacterLine"),
    ("Interface/CataclysmCharacterSelectWidget.cpp", "bool UCataclysmCharacterSelectWidget::ContinueAt(", "ContinueLine"),
    ("Player/CataclysmCapitalGameMode.cpp", "bool ACataclysmCapitalGameMode::OpenTheEmpireMapIfReady()", "CapitalLine"),
    ("Player/CataclysmCapitalGameMode.cpp", "FString ACataclysmCapitalGameMode::TravelToWalk(", "WalkLine"),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "bool ACataclysmDungeonGameMode::EnterEmpireDungeon(", "EnteredLine"),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "bool ACataclysmDungeonGameMode::GoDownOneFloor(", "FloorLine"),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "bool ACataclysmDungeonGameMode::ClearEmpireDungeon()", "ClearLine"),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()", "DeathLine"),
    ("Player/CataclysmPlayerState.cpp", "int32 ACataclysmPlayerState::GrantExperience(int64 Amount)", "LevelGainedLine"),
    # THE EMPIRE UPGRADE TREE, issue #2741 slice 2: a point bought, an option chosen, a respec. Said by the game
    # instance rather than the tree's screen, so a console command or a test that changes the tree logs it too.
    ("Player/CataclysmGameInstance.cpp", "bool UCataclysmGameInstance::BuyEmpireTreePoint(", "TreePointLine"),
    ("Player/CataclysmGameInstance.cpp", "bool UCataclysmGameInstance::ChooseEmpireTreeOption(", "TreeOptionLine"),
    ("Player/CataclysmGameInstance.cpp", "bool UCataclysmGameInstance::RespecEmpireTree(", "TreeRespecLine"),
    # THE CHARACTER'S BUILD, issue #2763: said by the player state, beside the write of the character record, so
    # the screens and the console commands log it alike. A skill choice is said by `ChooseSkill` and
    # `ForgetSkillChoice`, the player's acts, and never by `AssignSkill`, which a load calls.
    ("Player/CataclysmPlayerState.cpp", "bool ACataclysmPlayerState::SpendPassivePoint(", "PassivePointLine"),
    ("Player/CataclysmPlayerState.cpp", "bool ACataclysmPlayerState::ChoosePassiveOption(", "PassiveOptionLine"),
    ("Player/CataclysmPlayerState.cpp", "void ACataclysmPlayerState::ResetPassivePoints()", "PassiveResetLine"),
    ("Player/CataclysmPlayerState.cpp", "bool ACataclysmPlayerState::SpendAttributePoints(", "AttributePointLine"),
    ("Player/CataclysmPlayerState.cpp", "void ACataclysmPlayerState::ResetAttributePoints()", "AttributeResetLine"),
    ("Player/CataclysmPlayerState.cpp", "bool ACataclysmPlayerState::ChooseSkill(", "SkillChosenLine"),
    ("Player/CataclysmPlayerState.cpp", "bool ACataclysmPlayerState::ForgetSkillChoice(", "SkillForgottenLine"),
    # AND WHAT A CHANGE OF GEAR ADDED TO THE SKILL POOL ON NO KEY, issue #837 slice 2: said beside the notice the pawn
    # posts, and never for a load.
    ("Character/CataclysmPlayerCharacter.cpp", "void ACataclysmPlayerCharacter::NoteSkillsAddedToThePool(",
     "SkillsAddedLine"),
    # THE FORGE, ruled 2026-11-19: an item raised a level, and two stones combined into one of the next.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::Upgrade(", "ForgeUpgradeLine"),
    ("Player/CataclysmForgeCraft.cpp", "bool UCataclysmForgeCraft::Combine(", "ForgeCombineLine"),
    # AND A GEM SOCKETED OR UNSOCKETED, issue #46 slice G4: both crafts finish in `DoSocketing`, which says the line.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoSocketing(", "ForgeSocketLine"),
    # AND TWO GEMS COMBINED INTO ONE OF THE NEXT RARITY, issue #46 slice G6.
    ("Player/CataclysmForgeCraft.cpp", "bool UCataclysmForgeCraft::CombineGems(", "ForgeGemCombineLine"),
    # AND AN ENCHANTMENT IMPRINTED OR A DRAWBACK CHANGED, issue #47 slice F3: both crafts finish in `DoEnchantmentWork`.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoEnchantmentWork(",
     "ForgeEnchantmentWorkLine"),
    # AND AN AFFIX REROLLED, PERFECTED OR RAISED A TIER, issue #47 slice F1.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::CraftAffix(", "ForgeAffixLine"),
    # AND AN AFFIX ADDED OR SHIFTED, issue #47 slice F2: both crafts finish in `DoAffixChange`, which says the line.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoAffixChange(", "ForgeAffixChangeLine"),
    # AND A WEAPON'S DAMAGE TYPE INFUSED OR CHANGED, issue #47 slice F4.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::CraftDamageType(", "ForgeDamageTypeLine"),
    # AND AN ITEM'S RESIDUE HALVED, issue #47 slice F5; and the residue tools used with any item craft, said once by the
    # one sequence every item craft goes through, after the craft's own line.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::Cleanse(", "ForgeCleanseLine"),
    # AND A CARRIED PIECE DISMANTLED, issue #2848 slice S2, through the same sequence.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::Dismantle(", "ForgeDismantleLine"),
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(", "ForgeOptionsLine"),
    # AND WHAT THE FORGE'S TWO EMPIRE MILESTONE OPTIONS DID, issue #47 slice F6: The Soul Forge's second level, said by
    # the upgrade after its own line, and The Master Tinkerer's critical success, said by the enchantment crafts' end.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::Upgrade(", "ForgeSoulForgeLine"),
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoEnchantmentWork(",
     "ForgeCriticalLine"),
    # AND WHAT THE EMPIRE TREE'S MATERIAL NODES KEPT, issue #47 slice F6b: said by the one item craft sequence after the
    # craft's own line and its options', only when it kept anything (N4).
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(",
     "ForgeMaterialsKeptLine"),
    # AND WHAT THE EMPIRE TREE'S AFFIX NODES DID, #2741's crafting slices C3 and C4: said by the same sequence after what
    # the material nodes kept, only when they did anything.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(",
     "ForgeAffixNodesLine"),
    # GEAR PICKED UP AND WORN, the coordinating session's ruling of 2026-11-19: the owner's play log said nothing about
    # gear. Said where every pickup and every wear the player chooses comes through.
    ("Items/CataclysmDroppedItem.cpp", "bool UCataclysmDropPickup::TakeInto(", "PickedUpLine"),
    ("Items/CataclysmWearing.cpp", "ECataclysmWearResult UCataclysmWearing::WearFromCarried(", "WoreLine"),
    # A SKILL LEVEL GAINED, issue #2764: said by the player state for every skill whose level rose, after a kill's
    # share or a character level that let a skill's kept experience through.
    ("Player/CataclysmPlayerState.cpp", "void ACataclysmPlayerState::SaySkillLevelsGained(", "SkillLevelLine"),
    # WHAT DAYS SPENT IN THE CAPITAL DID, issue #2857: the walk's account of its days away, after the forge's lead and
    # the respec's, said by the one craft sequence and by the respec after their own lines. Until then the forge threw
    # its days' reports away, and the owner saw dungeons gone with nothing saying why.
    ("Player/CataclysmForgeCraft.cpp", "ECataclysmForgeResult UCataclysmForgeCraft::DoItemCraft(", "AwayAccount"),
    ("Player/CataclysmGameInstance.cpp", "bool UCataclysmGameInstance::RespecEmpireTree(", "AwayAccount"),
]

#: What must come before each place's line, whitespace aside: the step it reports.
AFTER = {
    "TravelToWalk": "if (!TravelTo(",
    "EnterEmpireDungeon": "SpendFloorTimeInTheEmpire();",
    "GoDownOneFloor": "SpendFloorTimeInTheEmpire();",
    "ClearEmpireDungeon": "Run->NoteDaysAway(",
    "EndTheDungeonForADeath": "Run->AdvanceDays(DaysTheDeathCost)",
    "GrantExperience": "CharacterLevel = Level;",
    "BuyEmpireTreePoint": "UCataclysmEmpireTree::Spend(",
    "ChooseEmpireTreeOption": "UCataclysmEmpireTree::ChooseOption(",
    "RespecEmpireTree": "Run->AdvanceDays(Days)",
    "SpendPassivePoint": "UCataclysmPassiveTree::Spend(",
    "ChoosePassiveOption": "PassiveAllocation = Attempt;",
    "ResetPassivePoints": "PassiveAllocation.Clear();",
    "SpendAttributePoints": "SpentAttributePoints = Wanted;",
    "ResetAttributePoints": "SpentAttributePoints = FCataclysmAttributePoints();",
    "ChooseSkill": "AssignSkill(Slot, SkillRow, OutReason)",
    "ForgetSkillChoice": "ClearSkillChoice(Slot)",
    # ISSUE #47 SLICE F5: AN ITEM CRAFT'S LINE IS SAID BY THE LAMBDA IT HANDS `DoItemCraft`, which passes the days before
    # it calls that lambda (`test_every_item_craft_goes_through_one_sequence.py`), so the step each line must follow in its
    # own body is the hand-over.
    "Upgrade": "DoItemCraft(",
    "Combine": "Inventory->AddMaterial(Plan.Into, 1)",
    "DoSocketing": "DoItemCraft(",
    "DoEnchantmentWork": "DoItemCraft(",
    "CraftAffix": "DoItemCraft(",
    "DoAffixChange": "DoItemCraft(",
    "CraftDamageType": "DoItemCraft(",
    "Cleanse": "DoItemCraft(",
    "Dismantle": "DoItemCraft(",
    "DoItemCraft": "Run->AdvanceDays(Step.Days);",
    "TakeInto": "Inventory->AddItem(Drop->Item)",
    "WearFromCarried": "Equipment->Equip(Item, First, Second, OutSlot)",
}


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


def braced_from(text: str, start: int, what: str) -> str:
    open_at = text.find("{", start)
    assert open_at != -1, f"{what} has no opening brace"
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    raise AssertionError(f"{what} is never closed")


def read_code(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. If it moved, move it here too.")
    return strip_comments(path.read_bytes().decode("utf-8"))


def function_body(code: str, signature: str) -> str:
    assert code.count(signature) == 1, f"{signature} should be defined once; it appears {code.count(signature)} times"
    return squeeze(braced_from(code, code.find(signature), signature))


def body_of(relative: str, signature: str) -> str:
    return function_body(read_code(SOURCE / relative), signature)


@pytest.mark.parametrize("relative, signature, builder", STEPS, ids=[s[2] for s in STEPS])
def test_each_step_of_the_loop_says_its_line(relative: str, signature: str, builder: str) -> None:
    body = body_of(relative, signature)
    said = f"FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::{builder}("
    assert said in body, (
        f"{signature} no longer says its PLAY LOOP line. Expected, whitespace aside:\n  {said}\nThe owner's checklist, "
        "docs/Play_Loop_Checklist.md, tells them to look for it (#2733 slice 4).")
    name = signature.split("::")[-1].split("(")[0]
    if name in AFTER:
        step = AFTER[name]
        assert step in body, f"{signature} no longer does {step!r}, which its line reports"
        assert body.index(step) < body.index(said), (
            f"{signature} says its line before {step!r}, the step it reports, so the line's figures are not yet true.")


def test_the_capital_lists_the_dungeons_standing_and_says_the_days_away_once() -> None:
    body = body_of("Player/CataclysmCapitalGameMode.cpp", "bool ACataclysmCapitalGameMode::OpenTheEmpireMapIfReady()")
    carried = "Instance->PlayLoopAwayAccount"
    assert "FCataclysmPlayLoopLog::CapitalLine(" in body and carried in body, (
        "The capital's arrival line no longer carries the days away the dungeon level left on the game instance.")
    emptied = "Instance->PlayLoopAwayAccount.Empty();"
    assert emptied in body, "The capital no longer empties the carried account, so it would be said on every arrival."
    assert body.index("FCataclysmPlayLoopLog::CapitalLine(") < body.index(emptied), (
        "The capital empties the carried account before saying it.")
    assert "FCataclysmPlayLoopLog::StandingLines(Run)" in body, (
        "The capital no longer lists the dungeons standing, the log's account of what no screen shows.")


@pytest.mark.parametrize("signature, first", [
    ("bool ACataclysmDungeonGameMode::ClearEmpireDungeon()", "LeaveEmpireDungeon();"),
    ("bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()", "Run->ResolveDungeonOnDeath("),
], ids=["clear", "death"])
def test_the_dungeons_are_named_before_the_days_run_and_the_account_is_left_for_the_capital(
        signature: str, first: str) -> None:
    body = body_of("Dungeon/CataclysmDungeonGameMode.cpp", signature)
    named = "FCataclysmPlayLoopLog::NamesStanding(Run)"
    assert named in body and first in body, f"{signature}: expected both {named!r} and {first!r}"
    assert body.index(named) < body.index(first), (
        f"{signature} names the dungeons standing after {first!r}, so a dungeon those days resolve has no name.")
    left = "Instance->PlayLoopAwayAccount = FCataclysmPlayLoopLog::AwayAccount("
    assert left in body, f"{signature} no longer leaves its account of the days away for the capital to say."


def returned_formats(code: str) -> dict[str, list[str]]:
    """Each builder's returned format strings: `return FString::Printf(TEXT("..."))` and `return TEXT("...")`."""
    formats: dict[str, list[str]] = {}
    for match in re.finditer(r"FCataclysmPlayLoopLog::(\w+)\(", code):
        name = match.group(1)
        start = match.start()
        line_start = code.rfind("\n", 0, start) + 1
        if not code[line_start:start].strip().startswith(("FString", "TArray", "TMap", "void")):
            continue
        body = braced_from(code, start, name)
        found = re.findall(r'return (?:FString::Printf\(\s*)?TEXT\("((?:[^"\\]|\\.)*)"\)', body)
        if found:
            formats[name] = found
    return formats


def as_pattern(fmt: str) -> str:
    pattern = ""
    for piece in re.split(r"(%\.0f|%lld|%d|%s)", fmt):
        pattern += {"%.0f": r"-?\d+", "%lld": r"-?\d+", "%d": r"-?\d+", "%s": r".*"}.get(piece, re.escape(piece))
    return pattern


def builder_patterns() -> dict[str, list[str]]:
    formats = returned_formats(read_code(LOG_CPP))
    for wanted in ("FrontEndLine", "NewCharacterLine", "ContinueLine", "DungeonLine", "CapitalLine", "WalkLine",
                   "EnteredLine", "FloorLine", "ClearLine", "DeathLine", "AwayAccount", "LevelGainedLine"):
        assert wanted in formats, f"FCataclysmPlayLoopLog::{wanted} returns no format this check can read"
    patterns = {name: [as_pattern(f) for f in fmts] for name, fmts in formats.items()}
    patterns["StandingLines"] = [r"standing: " + p for p in patterns["DungeonLine"]]
    return patterns


def quoted_lines() -> list[str]:
    if not CHECKLIST.is_file():
        pytest.fail("docs/Play_Loop_Checklist.md does not exist: the owner's checklist for one press of Play.")
    text = CHECKLIST.read_bytes().decode("utf-8")
    return [q[len("PLAY LOOP: "):] for q in re.findall(r"`(PLAY LOOP: [^`]+)`", text)]


def test_the_checklist_quotes_only_lines_the_game_says() -> None:
    patterns = builder_patterns()
    quoted = quoted_lines()
    assert quoted, "docs/Play_Loop_Checklist.md quotes no PLAY LOOP line; it should show the owner each one."
    strangers = [q for q in quoted
                 if not any(re.fullmatch(p, q) for ps in patterns.values() for p in ps)]
    assert not strangers, (
        "docs/Play_Loop_Checklist.md quotes lines no FCataclysmPlayLoopLog builder can say. Either the wording changed "
        "in CataclysmPlayLoopLog.cpp and the checklist was not updated, or the checklist is wrong:\n  "
        + "\n  ".join(strangers))


def test_the_checklist_quotes_every_line_the_game_says() -> None:
    patterns = builder_patterns()
    quoted = quoted_lines()
    # `DungeonLine` is never said alone: `WalkLine` and `StandingLines` say it. `AwayAccount` is said inside the
    # capital's line, so it is looked for inside a quoted line rather than as the whole of one.
    unquoted = sorted(
        name for name, ps in patterns.items()
        if name != "DungeonLine"
        and not any((re.search if name == "AwayAccount" else re.fullmatch)(p, q) for p in ps for q in quoted))
    assert not unquoted, (
        "docs/Play_Loop_Checklist.md quotes no line from these builders, so the owner is not told to look for them: "
        + ", ".join(unquoted))


def test_the_games_readme_links_the_checklist() -> None:
    text = GAME_README.read_bytes().decode("utf-8")
    assert "](../docs/Play_Loop_Checklist.md)" in text, (
        "game/README.md no longer links docs/Play_Loop_Checklist.md, the owner's checklist for one press of Play.")
