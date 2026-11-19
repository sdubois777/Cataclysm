"""Thorns of Glass strikes back for the creature's own attack damage. Issue #2749 (B2).

THE RULING. The project owner, 2026-11-18: reflect becomes a fixed hit from the
creature, so it no longer grows with the player's damage. The coordinating session
ruled the row's sentence the same day (Q1): "Strikes back at whatever hits it, once
per hit, for its own attack damage." It replaced "Reflects 50% of all damage taken
back to the attacker", which the creature carried out by writing 50 into its
`Retaliation` attribute, so `UCataclysmRetaliation::Pay` sent half of every blow
back and a stronger player was punished harder.

WHY PYTHON READS C++ HERE. Continuous integration compiles no C++. The Unreal tests
in `Cataclysm.ThornsOfGlass.` hold the figures; these hold the shape, so a pull
request that brings the share back -- a retaliation figure written for the row, or
a strike back sized by anything but the creature's own attack -- fails before a
window does, and the sentence a player reads cannot drift from the ruling unseen.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
ENEMY_MODIFIERS_CSV = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"
CREATURE = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"
MODIFIERS_HEADER = SOURCE / "Character" / "CataclysmEnemyModifiers.h"
RETALIATION = SOURCE / "AbilitySystem" / "CataclysmRetaliation.cpp"
RETALIATION_HEADER = SOURCE / "AbilitySystem" / "CataclysmRetaliation.h"

#: The coordinating session's words of 2026-11-19, carrying the project owner's ruling of the same day (at most once
#: per its own attack interval, which replaced the 2026-11-18 sentence's "once per hit"), as the design workbook's Enemy
#: Modifiers sheet carries them and the generator copies them.
RULED_SENTENCE = ("Strikes back at whatever hits it, at most once per its own attack interval, "
                  "for its own attack damage.")
VITAL = SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The text with every `//` comment and `/* */` block taken out.

    BY SYNTAX AND NOT BY A LINE'S FIRST CHARACTER: a code line can open with `*`
    (a multiplication continued from the line above), and a comment can follow
    code on the same line. A copy of the helper in
    `test_a_rule_gives_a_creature_damage_as_a_part.py`.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def body_of(text: str, opening: str) -> str:
    """The braced body that follows `opening`, by counting braces.

    ANOTHER COPY OF THE SUITE'S HELPER: `test_dungeon_modifier_rules_are_the_rows.py`
    records why there is no shared module.
    """
    start = text.index(opening)
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"braces never balanced after {opening!r}")


def calls_of(text: str, name: str) -> list[str]:
    """Every call `name(...)` in `text`, arguments and all, by counting parentheses."""
    found = []
    for match in re.finditer(re.escape(name) + r"\s*\(", text):
        depth = 0
        for index in range(match.end() - 1, len(text)):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    found.append(text[match.start():index + 1])
                    break
    return found


def test_the_row_says_the_ruled_sentence():
    with ENEMY_MODIFIERS_CSV.open(encoding="utf-8-sig", newline="") as handle:
        rows = [row for row in csv.DictReader(handle) if row["ModifierName"] == "Thorns of Glass"]

    assert len(rows) == 1, f"expected one Thorns of Glass row in {ENEMY_MODIFIERS_CSV.name}, found {len(rows)}"
    assert rows[0]["Description"] == RULED_SENTENCE, rows[0]["Description"]


def test_the_creature_writes_no_retaliation_for_its_rows():
    """The share was carried out by writing the `Retaliation` attribute in
    `ApplyModifierAttributes`; nothing there may write it again. THE FIRST
    ASSERTION SHOWS THE PARSE FOUND THE FUNCTION THAT WRITES THE ROWS' STATS, so
    the second cannot pass on an empty body."""
    body = body_of(without_comments(read(CREATURE)),
                   "void ACataclysmEnemyCharacter::ApplyModifierAttributes()")

    assert len(calls_of(body, "ApplyIfHeld")) >= 2, body[:400]
    assert "GetRetaliationAttribute" not in body
    assert "RetaliationPercent" not in without_comments(read(MODIFIERS_HEADER))


def test_the_strike_back_is_the_creatures_own_hit_at_its_whole_attack_damage():
    """One `ApplyHit` from the creature at the attacker, at
    `StrikeBackDamagePercent`, which is 100: the creature's own attack damage
    through its own attack's pipeline, and nothing taken from the blow it took.
    And marked as a returned blow that cannot itself be retaliated against."""
    body = body_of(without_comments(read(RETALIATION)), "float UCataclysmRetaliation::StrikeBack(")
    hits = calls_of(body, "UCataclysmSkillEffects::ApplyHit")

    assert len(hits) == 1, hits
    assert re.fullmatch(r"UCataclysmSkillEffects::ApplyHit\(\s*Creature\s*,\s*Attacker\s*,"
                        r"\s*StrikeBackDamagePercent\s*,.*", hits[0], flags=re.DOTALL), hits[0]
    assert re.search(r"static constexpr float StrikeBackDamagePercent = 100\.0f;",
                     without_comments(read(RETALIATION_HEADER)))
    for mark in ("bCannotBeRetaliatedAgainst", "bIsRetaliation"):
        assert re.search(rf"Delivery\.{mark}\s*=\s*true;", body), mark


def test_the_strike_back_waits_its_own_attack_interval():
    """THE OWNER'S RULING OF 2026-11-19: "It can strike back at most once per its own attack interval". The attribute
    set strikes back only when the creature carries the row AND its own clock says one is due; the clock reads the
    interval its own swings use, buffs included (`SecondsBetweenAttacks`), and records the strike back. THE FIRST
    ASSERTION SHOWS THE PARSE FOUND THE STRIKE BACK, so the others cannot pass on an empty match."""
    vital = without_comments(read(VITAL))
    assert "UCataclysmRetaliation::StrikeBack(" in vital
    assert re.search(r"StrikesBackWhenHit\(\)\s*&&\s*ThornedCreature->TakeAStrikeBackIfDue\(\)", vital), (
        "the strike back is no longer gated by the creature's own interval")
    due = body_of(without_comments(read(CREATURE)), "bool ACataclysmEnemyCharacter::TakeAStrikeBackIfDue()")
    assert "SecondsBetweenAttacks()" in due, due
    assert re.search(r"LastStrikeBackSeconds\s*=\s*Now", due), due
