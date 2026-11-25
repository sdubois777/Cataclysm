"""Every call that lays damage over time, or carries a skill's damage on after the skill, names the skill or its row.

WHY THIS FILE EXISTS. Issue #2764, slice 2a: a skill earns experience from, and grows with its level through, its burns,
ailments, ground, tether, late shots and buried axe. Each of those reaches the credit through a function that was given
a new last parameter with a default, `SkillRowWithNoSkill = NAME_None`, beside the skill it already took. A call that
should pass the skill or its row and does not still compiles, and its damage then credits and scales nothing, silently.
The coordinating session asked for this check on 2026-11-20 for exactly that reason ("A call site that should pass the
row and does not still compiles").

WHAT IS CHECKED. Every call, in the game's own source (not the tests), to `ApplyDamageOverTime`,
`ApplyShareOfHealthOverTime`, `ApplyBurn`, `UCataclysmAilments::Apply`, `LaySkillTrapsDamageOverTime` and `BuryIn`,
and every function that hands `ApplyDirectDamage` a delivery marked as damage over time. A call PASSES A SKILL when its
skill argument is there and is not `nullptr`, and PASSES A ROW when its row argument is there and is not `NAME_None`; a
function marking a delivery as damage over time names one when it also sets that delivery's `Skill`,
`SkillRowToCredit` or `SkillRowWithNoSkill`. A call that names neither must be in `CREDITS_NOTHING_BY_RULE`, with its
reason; and every entry there must still match such a call, so the list cannot go stale.

HOW A CALL IS KEYED: its file, the function it sits in, the function it calls, and its place among that function's calls
to the same function. Reading is by text with comments removed; arguments are split at their top-level commas. A
definition starts at column 0 and a call never does, in this project's layout, so definitions are passed over.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"

#: The functions checked, and the 0-based places of their skill and row arguments (None: the function takes none).
CALLED = {
    "ApplyDamageOverTime": (7, 11),
    "ApplyShareOfHealthOverTime": (6, 7),
    "ApplyBurn": (6, 8),
    "UCataclysmAilments::Apply": (4, 8),
    "LaySkillTrapsDamageOverTime": (None, 3),
    "BuryIn": (None, 7),
}

#: The marker of a function that hands `ApplyDirectDamage` a damage-over-time delivery.
DAMAGE_OVER_TIME_DELIVERY = "ApplyDirectDamage with bIsDamageOverTime"

_WORN_ROW = ("a worn row's status, not a skill's: damage a passive, a worn row or a set bonus adds credits nothing "
             "(the coordinating session's judgement 2, 2026-11-19)")
_SPREAD = "a spread copy of a running ailment: judgement 2, a copy credits nothing"
_OWN_EFFECT = "the function that lays the effect; it sets the row on the effect itself (`Effect->SkillRowToCredit`)"
_FLOOR = "a dungeon floor's own hazard, dealt in the floor's name: no skill deals it"
_CREATURE = "a creature's or a dungeon rule's damage, not the player's: no skill deals it"
_EXPLOSION = ("a minion's explosion's burn: every explosion is one a passive sets off, which credits nothing "
              "(judgement 2), since issue #2787 removed Summon Imp's cap explosion, the one that credited the skill")

#: The calls that name neither a skill nor a row, by rule, each with its reason.
CREDITS_NOTHING_BY_RULE: dict[tuple[str, str, str, int], str] = {
    ("AbilitySystem/CataclysmMinion.cpp", "ACataclysmMinion::Explode", "ApplyBurn", 0): _EXPLOSION,
    ("AbilitySystem/CataclysmAbilitySystemComponent.cpp", "UCataclysmAbilitySystemComponent::ApplyStatusToTheWearer",
     "UCataclysmAilments::Apply", 0): _WORN_ROW + "; laid on its own wearer",
    ("AbilitySystem/CataclysmAbilitySystemComponent.cpp", "UCataclysmAbilitySystemComponent::ApplyStatusToTheWearer",
     "UCataclysmAilments::Apply", 1): _WORN_ROW + "; laid on its own wearer",
    ("AbilitySystem/CataclysmAbilitySystemComponent.cpp", "UCataclysmAbilitySystemComponent::ApplyStatusOf",
     "UCataclysmAilments::Apply", 0): _WORN_ROW,
    ("AbilitySystem/CataclysmAbilitySystemComponent.cpp", "UCataclysmAbilitySystemComponent::ApplyStatusOf",
     "UCataclysmAilments::Apply", 1): _WORN_ROW,
    ("AbilitySystem/CataclysmContagion.cpp", "UCataclysmContagion::SpreadOne", "ApplyDamageOverTime", 0): _SPREAD,
    ("AbilitySystem/CataclysmContagion.cpp", "UCataclysmContagion::SpreadOne", "ApplyShareOfHealthOverTime", 0): _SPREAD,
    ("AbilitySystem/CataclysmSkillEffects.cpp", "UCataclysmSkillEffects::ApplySpreadCopy",
     "ApplyShareOfHealthOverTime", 0): _SPREAD,
    ("AbilitySystem/CataclysmSkillEffects.cpp", "UCataclysmSkillEffects::ApplySpreadCopy", "ApplyDamageOverTime", 0):
        _SPREAD,
    ("AbilitySystem/CataclysmDamageConversion.cpp", "UCataclysmDamageConversion::ConvertIfActive",
     "ApplyDamageOverTime", 0): "damage the player takes, converted into a bleed on the player: no skill of theirs deals it",
    ("AbilitySystem/CataclysmSkillEffects.cpp", "UCataclysmSkillEffects::ApplyDamageOverTime",
     DAMAGE_OVER_TIME_DELIVERY, 0): _OWN_EFFECT,
    ("AbilitySystem/CataclysmSkillEffects.cpp", "UCataclysmSkillEffects::ApplyShareOfHealthOverTime",
     DAMAGE_OVER_TIME_DELIVERY, 0): _OWN_EFFECT,
    ("Character/CataclysmEnemyModifiers.cpp", "UCataclysmEnemyModifiers::AuraStep", "ApplyDamageOverTime", 0):
        _CREATURE + "; a creature modifier's aura",
    ("Character/CataclysmEnemyModifiers.cpp", "UCataclysmEnemyModifiers::TetherOnHealthLost", "ApplyDamageOverTime", 0):
        _CREATURE + "; Death's Soul Tether, a share of the health a creature lost laid on whoever struck it",
    ("Dungeon/CataclysmDungeonGameMode.cpp", "ACataclysmDungeonGameMode::NoteDeathForPlagueHarbingers",
     "UCataclysmAilments::Apply", 0): _CREATURE + "; a dungeon modifier at a creature's death",
    ("Dungeon/CataclysmDungeonGameMode.cpp", "ACataclysmDungeonGameMode::NoteDeathForSporeClouds",
     "UCataclysmAilments::Apply", 0): _CREATURE + "; a dungeon modifier at a creature's death",
    **{("Dungeon/CataclysmDungeonGameMode.cpp", f"ACataclysmDungeonGameMode::{step}", DAMAGE_OVER_TIME_DELIVERY, 0): _FLOOR
       for step in ("StepPlagueConvergence", "StepRawSewage", "StepSwarmOfLocusts", "StepInfestedHoard",
                    "StepFunerealProcession", "StepInfestedVeins", "StepBloodPrice", "StepNecroticGround")},
}

#: `(` + any `)` the call pattern and the splitting count. Kept as names so no backslash appears in a string here.
OPENERS = "([{"
CLOSERS = ")]}"
NEWLINE = chr(10)


def strip_comments(text: str) -> str:
    """The C++ text with comments blanked out, keeping every line where it was."""
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def blank_strings(text: str) -> str:
    """The text with every string and character literal's contents blanked, keeping every offset."""
    return re.sub(r"\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])'",
                  lambda m: m.group(0)[0] + " " * (len(m.group(0)) - 2) + m.group(0)[-1], text)


def function_spans(text: str) -> list[tuple[int, int, str]]:
    """Each function body's start and end offsets and its name, by counting braces in `text` (comments and strings
    blanked). A block is a function's when no function encloses it and the text before it is not a namespace, a class,
    a struct or an enum; its name is the first `Name(` in that text."""
    spans = []
    stack: list[tuple[str, int, str]] = []
    previous_end = 0
    for brace in re.finditer(r"[{};]", text):
        ch = brace.group(0)
        if ch == ";":
            if not stack or stack[-1][0] != "fn":
                previous_end = brace.end()
            continue
        if ch == "{":
            if any(kind == "fn" for kind, _, _ in stack):
                stack.append(("block", brace.start(), ""))
                continue
            header = text[previous_end:brace.start()]
            if re.search(r"\b(?:namespace|class|struct|enum|union)\b", header) or "(" not in header:
                stack.append(("scope", brace.start(), ""))
            else:
                name = re.search(r"((?:\w+::)*~?\w+)\s*\(", header)
                stack.append(("fn", brace.start(), name.group(1) if name else "(unnamed)"))
            previous_end = brace.end()
        else:
            if stack:
                kind, start, name = stack.pop()
                if kind == "fn":
                    spans.append((start, brace.end(), name))
            previous_end = brace.end()
    return spans


def enclosing_function(spans: list[tuple[int, int, str]], offset: int) -> str:
    """The name of the function whose body holds `offset`."""
    for start, end, name in spans:
        if start <= offset < end:
            return name
    return "(file scope)"


def arguments_at(text: str, open_paren: int) -> list[str] | None:
    """The top-level arguments of the call whose `(` is at `open_paren`, or None if it never closes."""
    depth = 0
    start = open_paren + 1
    out: list[str] = []
    for at in range(open_paren, len(text)):
        ch = text[at]
        if ch in OPENERS:
            depth += 1
        elif ch in CLOSERS:
            depth -= 1
            if depth == 0:
                last = text[start:at].strip()
                if last or out:
                    out.append(last)
                return out
        elif ch == "," and depth == 1:
            out.append(text[start:at].strip())
            start = at + 1
    return None


def names_it(argument: str | None, empty: str) -> bool:
    """Whether an argument names something: present, and not the empty value."""
    return argument is not None and argument.strip() not in ("", empty)


#: `UCataclysmAilments::Apply` qualified, because `Apply` alone is a common name; the others by name.
CALL = re.compile(r"(UCataclysmAilments::Apply|(?<!\w)(?:ApplyDamageOverTime|ApplyShareOfHealthOverTime|ApplyBurn"
                  r"|LaySkillTrapsDamageOverTime|BuryIn))\s*\(")


def game_sources() -> list[pathlib.Path]:
    return [p for p in sorted(SOURCE.rglob("*.cpp")) if "Tests" not in p.parts]


def calls_in_the_game() -> list[tuple[tuple[str, str, str, int], bool]]:
    """Every checked call in the game's source, keyed, with whether it names a skill or a row."""
    found = []
    for path in game_sources():
        text = blank_strings(strip_comments(path.read_text(encoding="utf-8")))
        lines = text.split(NEWLINE)
        spans = function_spans(text)
        seen: dict[tuple[str, str], int] = {}
        for match in CALL.finditer(text):
            called = match.group(1)
            line_index = text.count(NEWLINE, 0, match.start())
            if not lines[line_index][:1].isspace():
                continue  # a definition
            arguments = arguments_at(text, match.end() - 1)
            if arguments is None:
                continue
            function = enclosing_function(spans, match.start())
            place = seen.get((function, called), 0)
            seen[(function, called)] = place + 1
            skill_at, row_at = CALLED[called]
            skill = arguments[skill_at] if skill_at is not None and skill_at < len(arguments) else None
            row = arguments[row_at] if row_at < len(arguments) else None
            named = names_it(skill, "nullptr") or names_it(row, "NAME_None")
            found.append(((path.relative_to(SOURCE).as_posix(), function, called, place), named))

        # AND EVERY FUNCTION THAT MARKS A DELIVERY AS DAMAGE OVER TIME, for `ApplyDirectDamage`.
        for match in re.finditer(r"(\w+)\.bIsDamageOverTime\s*=\s*true", text):
            variable = match.group(1)
            function = enclosing_function(spans, match.start())
            body_end = text.find(NEWLINE + "}", match.end())
            body_start = text.rfind(NEWLINE + "{", 0, match.start())
            body = text[body_start:body_end if body_end != -1 else len(text)]
            named = bool(re.search(re.escape(variable) + r"\.(?:Skill|SkillRowToCredit|SkillRowWithNoSkill)\s*=", body))
            place = seen.get((function, DAMAGE_OVER_TIME_DELIVERY), 0)
            seen[(function, DAMAGE_OVER_TIME_DELIVERY)] = place + 1
            found.append(((path.relative_to(SOURCE).as_posix(), function, DAMAGE_OVER_TIME_DELIVERY, place), named))
    return found


def test_every_call_names_its_skill_or_is_listed_with_its_reason() -> None:
    calls = calls_in_the_game()
    assert len(calls) >= 20, f"only {len(calls)} calls found; the reading has gone wrong"
    unnamed = {key for key, named in calls if not named}
    missing = sorted(unnamed - set(CREDITS_NOTHING_BY_RULE))
    stale = sorted(set(CREDITS_NOTHING_BY_RULE) - unnamed)
    assert not missing, (
        "These calls pass neither a skill nor a row, so their damage credits no skill and is not sized by a level. "
        "Pass the skill, or the row it carries (`SkillRowWithNoSkill`), or add the call to CREDITS_NOTHING_BY_RULE with "
        "the rule that says it credits nothing:" + NEWLINE + "  " + (NEWLINE + "  ").join(map(str, missing)))
    assert not stale, (
        "These entries in CREDITS_NOTHING_BY_RULE match no call that passes neither; remove them:" + NEWLINE + "  "
        + (NEWLINE + "  ").join(map(str, stale)))
