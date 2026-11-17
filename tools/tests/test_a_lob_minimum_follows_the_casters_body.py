"""A lobbed attack's minimum range follows the caster's body as it stands.

WHY THIS EXISTS. Issue #2716. The Brute's Rip and Toss, the Gatekeeper's
Soulfall and the Corrupted Sentinel's Brimstone Mortar each mark a circle where
they land and refuse a target nearer than `marked radius + the caster's body
radius`, so the circle never covers the ground the caster stands on (issue
#475). The body in that sum was the UNSCALED 48 cm, and a rarer creature is
bigger: `ACataclysmEnemyCharacter::SetRarityStep` scales the whole actor,
capsule included, by the rung's `BodyScale` (game/Data/EnemyRarities.csv). At
the Cataclysm Boss rung a body is 119.44 cm, so a Soulfall circle aimed at
anything between 348 and 419.44 cm covered the Gatekeeper's own feet.

THE RULING, by the coordinating session on 2026-11-03: each minimum is the
designed margin plus the caster's body as it stands, so a Common caster's is
exactly what it was. The growth is read through ONE helper,
`ACataclysmEnemyCharacter::BodyGrowthCm` -- `max(0, the capsule's scaled
radius - its unscaled radius)` -- which `ACataclysmEnemyController::
ReachAgainstCm` already used for a contact reach (issue #2706) and now calls
rather than writing the subtraction again.

WHY A PYTHON TEST. Continuous integration never builds the C++, so the Unreal
tests in `game/Source/Cataclysm/Tests/CataclysmLobMinimumTests.cpp` run only on
a machine with the engine. This reads the source text, which is present either
way: each ability is built from its designed sum plus the growth, and the
growth is measured in exactly one place.

COMMENTS ARE STRIPPED FIRST, so a line that is only described, or commented
out, does not satisfy a check.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
CHARACTER = SOURCE / "Cataclysm" / "Character"

#: Each lob: the file whose `EnemyAbilities` builds it, the class, the local the
#: ability is built in, and the two designed terms of its minimum.
LOBS = {
    "Rip and Toss": ("CataclysmBruteCharacter.cpp", "ACataclysmBruteCharacter",
                     "RockThrow", "RockThrowRadiusCm", "BruteCapsuleRadius"),
    "Soulfall": ("CataclysmGatekeeperCharacter.cpp",
                 "ACataclysmGatekeeperCharacter", "Soulfall",
                 "SoulfallRadiusCm", "GatekeeperCapsuleRadius"),
    "Brimstone Mortar": ("CataclysmCorruptedSentinelCharacter.cpp",
                         "ACataclysmCorruptedSentinelCharacter", "Mortar",
                         "BrimstoneMortarRadiusCm", "SentinelCapsuleRadius"),
}


def without_comments(text: str) -> str:
    """The text with `/* ... */` blocks and `//` line comments removed.

    A block comment is removed by its delimiters, never by a line's leading
    `*`, because a code line can begin with one (a dereference or a product
    continued from the line above).
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return without_comments(path.read_text(encoding="utf-8-sig"))


def function_body(text: str, signature: str, where: str) -> str:
    """The braces-and-all body of the first definition matching `signature`."""
    match = re.search(signature, text)
    if match is None:
        pytest.fail(f"{where} has no definition matching {signature!r}. If it "
                    f"was renamed or moved, follow it here.")
    start = text.index("{", match.end())
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    pytest.fail(f"the body of {signature!r} in {where} never closes")


@pytest.mark.parametrize("lob", sorted(LOBS))
def test_each_lob_minimum_is_its_designed_sum_plus_the_body_growth(lob):
    file_name, owner, local, radius, body = LOBS[lob]
    body_text = function_body(
        source(CHARACTER / file_name),
        rf"TArray<FCataclysmEnemyAbility>\s*{owner}::EnemyAbilities\(\)\s*const",
        file_name)

    built = re.findall(rf"\b{local}\.MinRangeCm\s*=\s*([^;]*);", body_text)
    assert len(built) == 1, (
        f"{owner}::EnemyAbilities sets {local}.MinRangeCm {len(built)} times; "
        f"one assignment was expected, so this can say what {lob}'s minimum is.")
    assert re.fullmatch(
        rf"\s*{radius}\s*\+\s*{body}\s*\+\s*BodyGrowthCm\(\)\s*", built[0]), (
        f"{lob}'s minimum is built as `{built[0].strip()}`. Issue #2716 rules "
        f"it `{radius} + {body} + BodyGrowthCm()`: the marked circle plus the "
        f"caster's body as it stands, so a rarer, bigger caster never stands "
        f"inside the circle it marks, and a Common one's minimum is exactly the "
        f"designed figure.")


def test_the_growth_is_measured_by_one_helper_on_the_enemy_character():
    body_text = function_body(
        source(CHARACTER / "CataclysmEnemyCharacter.cpp"),
        r"float\s+ACataclysmEnemyCharacter::BodyGrowthCm\(\)\s*const",
        "CataclysmEnemyCharacter.cpp")
    squashed = re.sub(r"\s+", "", body_text)

    assert ("FMath::Max(0.0f,Capsule->GetScaledCapsuleRadius()"
            "-Capsule->GetUnscaledCapsuleRadius())") in squashed, (
        "ACataclysmEnemyCharacter::BodyGrowthCm no longer answers "
        "max(0, the capsule's scaled radius - its unscaled radius). That is "
        "the growth issue #2706 ruled for a contact reach and issue #2716 for "
        "a lob's minimum; the unscaled radius is the capsule's own, so no "
        "creature's designed body is written twice.")


def test_the_reach_asks_the_helper_rather_than_measuring_again():
    reach = function_body(
        source(CHARACTER / "CataclysmEnemyController.cpp"),
        r"float\s+ACataclysmEnemyController::ReachAgainstCm\([^)]*\)",
        "CataclysmEnemyController.cpp")

    assert re.search(r"Reach\s*\+=\s*Creature->BodyGrowthCm\(\)\s*;", reach), (
        "ACataclysmEnemyController::ReachAgainstCm no longer lengthens a "
        "contact reach by Creature->BodyGrowthCm(), the attacker's own growth "
        "(issue #2706).")


def test_the_unscaled_radius_is_read_in_exactly_one_place_in_the_game():
    """One helper, not a copy: no other game source measures the growth.

    THE SCOPE IS PRINTED ON FAILURE, so a reader can see what was searched:
    every `.cpp` and `.h` under game/Source except the automation tests, which
    read the unscaled radius to state their set-up and are not the game.
    """
    searched = [path for path in sorted(SOURCE.rglob("*"))
                if path.suffix in (".cpp", ".h")
                and "Tests" not in path.relative_to(SOURCE).parts]
    readers = [path.relative_to(REPO_ROOT).as_posix() for path in searched
               if "GetUnscaledCapsuleRadius" in source(path)]

    assert readers == [
        "game/Source/Cataclysm/Character/CataclysmEnemyCharacter.cpp"], (
        f"of {len(searched)} game source files under game/Source (tests "
        f"excluded), these read a capsule's unscaled radius: {readers}. Only "
        f"ACataclysmEnemyCharacter::BodyGrowthCm should, so a creature's growth "
        f"has one definition (issues #2706 and #2716).")
