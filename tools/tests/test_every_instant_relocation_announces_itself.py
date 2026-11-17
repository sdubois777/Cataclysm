"""Every instant relocation in the game announces itself to the movement sampler (#2388).

Nothing in the game called `NoteRelocatedInstantly` until issue #2388, so every
teleport was counted as ground walked. Twelve calls in eleven functions now make
it, through `UCataclysmMovement::NoteRelocatedInstantly`. Eight of them have no
Unreal test of their own, by the coordinating session's ruling of 2026-10-24:
the shared helper is the tested part. This file is what notices one of them
stopping, and it is text and not a run, so it says only that the call is in the
function, not that it is reached.

COMMENTS ARE REMOVED BY TRACKING `/* */`, `//` AND STRING LITERALS, not by line
prefix: a line of code that begins with `*` is not a comment, and the call can
follow code on the same line. An apostrophe after a letter or digit is a C++
digit separator and not a character literal.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
CALL = "UCataclysmMovement::NoteRelocatedInstantly("

#: (file under game/Source/Cataclysm, the function's signature as it opens, how many calls it makes)
SITES = (
    ("AbilitySystem/CataclysmSkillTemplates.cpp", "void UCataclysmMovementSkill::ActivateAbility(", 2),
    ("AbilitySystem/CataclysmSkillTemplates.cpp", "void UCataclysmMovementSkill::FlickerOnce(", 1),
    ("Character/CataclysmEnemyModifiers.cpp", "bool UCataclysmEnemyModifiers::PhaseStep(", 1),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "int32 ACataclysmDungeonGameMode::BringFollowersTo(", 1),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "bool ACataclysmDungeonGameMode::PlaceAtEntrance(", 1),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "void ACataclysmDungeonGameMode::StepRealityRifts(", 1),
    ("Dungeon/CataclysmDungeonGameMode.cpp", "void ACataclysmDungeonGameMode::StepTheRealityChamber(", 1),
    ("Character/CataclysmPlayerCharacter.cpp", "void ACataclysmPlayerCharacter::Revive(", 1),
    ("Save/CataclysmSaveApply.cpp", "bool FCataclysmSaveApply::PlacementInto(", 1),
    ("AbilitySystem/CataclysmMinion.cpp", "void ACataclysmMinion::MoveSkillGadgetTo(", 1),
)

#: The random teleport when hit calls the component's own function, not the library's.
COMPONENT_SITE = ("AbilitySystem/CataclysmAbilitySystemComponent.cpp",
                  "bool UCataclysmAbilitySystemComponent::TeleportTheWearerRandomly(",
                  "NoteRelocatedInstantly(Self->GetActorLocation() - From);")


def without_comments(text: str) -> str:
    """The text with every comment removed and every string literal emptied."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                i += 1
        elif two == "/*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        elif c == "'" and i > 0 and text[i - 1].isalnum():
            # A DIGIT SEPARATOR, `1'200.0f`, and not the start of a character literal.
            out.append(c)
            i += 1
        elif c in "\"'":
            quote = c
            out.append(quote)
            i += 1
            while i < n and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            out.append(quote)
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def body_of(code: str, signature: str) -> str:
    """The braces-matched body of the one function `signature` opens."""
    assert code.count(signature) == 1, f"{signature} is not defined exactly once"
    start = code.index("{", code.index(signature))
    depth = 0
    for i in range(start, len(code)):
        depth += {"{": 1, "}": -1}.get(code[i], 0)
        if depth == 0:
            return code[start:i + 1]
    raise AssertionError(f"{signature} has no closing brace")


def calls_in(path: pathlib.Path, signature: str, call: str = CALL) -> int:
    return body_of(without_comments(path.read_text(encoding="utf-8")), signature).count(call)


@pytest.mark.parametrize("relative, signature, expected", SITES, ids=[s[1] for s in SITES])
def test_each_instant_relocation_announces_itself(relative, signature, expected):
    found = calls_in(SOURCE / relative, signature)
    assert found == expected, (
        f"{signature} in {relative} calls {CALL} {found} times, not {expected}. Each instant relocation tells "
        "the movement sampler so, or the sampler counts it as ground walked. Issue #2388")


def test_the_random_teleport_announces_itself_through_the_component():
    relative, signature, call = COMPONENT_SITE
    assert calls_in(SOURCE / relative, signature, call) == 1, (
        f"{signature} no longer calls {call}. Issue #2388")


def test_twelve_calls_in_all_and_none_elsewhere():
    """THE CONTROL ON THE LIST: every library call in the game is one of the twelve, so a new relocation that
    announces itself is added here, and the count above is not satisfied by calls this file does not name."""
    listed = sum(expected for _, _, expected in SITES)
    assert listed == 11
    found = 0
    for path in SOURCE.rglob("*.cpp"):
        if "Tests" in path.parts or path.name == "CataclysmMovement.cpp":
            continue
        found += without_comments(path.read_text(encoding="utf-8")).count(CALL)
    assert found == listed, (
        f"the game calls {CALL} {found} times outside the tests and the library, and this file lists {listed}")


def test_the_comment_stripper_keeps_code_and_drops_comments():
    """THE CONTROL ON THE STRIPPER: a call in a comment is not counted, a call after code on a line is, and a line
    of code opening with `*` is kept."""
    text = ("void F()\n{\n\t// " + CALL + "A, B);\n\t/* " + CALL + "A, B); */\n\tint X = 1; " + CALL
            + "A, B);\n\t*Ptr = 2; " + CALL + "C, D);\n\tconst TCHAR* S = TEXT(\"" + CALL + "\");\n}\n")
    assert body_of(without_comments(text), "void F(").count(CALL) == 2
