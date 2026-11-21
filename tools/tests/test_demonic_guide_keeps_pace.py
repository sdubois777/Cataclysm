"""The Demonic Guide floor rule's guide keeps a pace the player can follow.

WHY THIS EXISTS. Issue #2790, from the owner's play-test of 2026-11-20: "that dude is SPRINTING. It's impossible to keep
up with him." The guide is an Imp, and it walked at the Imp's own designed 650 cm/s against the player's 400; and it
walked on again as soon as the player was within 15 m, outside its 12 m chain, so a player catching up could not get
back inside. The coordinating session accepted on 2026-11-20: the guide walks at 0.8 of the player's own walk speed,
read at every step (G1), and once it waits it waits until the player is back inside its chain (G2). Both are
judgements; the row states no speed and says nothing of waiting.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the two
`Cataclysm.DungeonModifierEffects.TheGuideKeepsPace` automation tests never run on a pull request.

WHAT IS CHECKED:

  1. the share is a figure below 1, held so by a static assertion, and named in the rule's own description;
  2. each step sets the guide's walk from the player's own walk speed times the share;
  3. the guide is raised at its share of the player's default walk, after it is made the floor's guide;
  4. a guide's walk speed is applied before, and in place of, the designed speed and its multipliers;
  5. once it waits, it waits while the player is beyond its chain.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
EFFECTS_H = SOURCE / "Dungeon" / "CataclysmDungeonModifierEffects.h"
MODE = SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp"
ENEMY = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


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
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {where} is not closed")


def test_the_share_is_below_one_and_named_in_the_rules_description() -> None:
    effects = code(EFFECTS_H)
    found = re.search(r"\bDemonicGuideWalkShareOfPlayer\s*=\s*([0-9.]+)f\s*;", effects)
    assert found, "CataclysmDungeonModifierEffects.h no longer states DemonicGuideWalkShareOfPlayer"
    assert 0.0 < float(found.group(1)) < 1.0, "a guide at the player's own speed or faster could never be caught up with"
    assert re.search(r"static_assert\(\s*DemonicGuideWalkShareOfPlayer > 0\.0f && DemonicGuideWalkShareOfPlayer < 1\.0f",
                     effects), "nothing in the build holds the share below 1"
    text = EFFECTS_H.read_text(encoding="utf-8")
    key = text[:text.index("static const TCHAR* DemonicGuideKey;")]
    assert "`DemonicGuideWalkShareOfPlayer`" in key[key.rindex("/**"):], (
        "the rule's description no longer says the guide walks at its share of the player's speed")


def test_each_step_sets_the_guides_walk_from_the_players_own() -> None:
    step = squeeze(body(code(MODE), "void ACataclysmDungeonGameMode::StepDemonicGuide(", "CataclysmDungeonGameMode.cpp"))
    assert "Player->GetCharacterMovement()" in step, "the step no longer reads the player's movement"
    assert ("Guide->SetGuideWalkSpeed(PlayerMovement->MaxWalkSpeed * Effects::DemonicGuideWalkShareOfPlayer)"
            in step), "the step no longer sets the guide's walk to its share of the player's own walk speed"


def test_the_guide_is_raised_at_its_share_of_the_players_default_walk() -> None:
    place = squeeze(body(code(MODE), "void ACataclysmDungeonGameMode::PlaceTheGuide()", "CataclysmDungeonGameMode.cpp"))
    flag = place.find("Guide->bGuidesThePlayerForTheFloorRule = true;")
    speed = place.find("Guide->SetGuideWalkSpeed(ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond "
                       "* Effects::DemonicGuideWalkShareOfPlayer);")
    assert -1 < flag < speed, (
        "the guide is not raised at its share of the player's default walk after it is made the guide, so it walks at "
        "the Imp's own 650 cm/s until the first step")


def test_a_guides_walk_comes_before_and_in_place_of_the_designed_speed() -> None:
    refresh = squeeze(body(code(ENEMY), "void ACataclysmEnemyCharacter::RefreshWalkSpeed()", "CataclysmEnemyCharacter.cpp"))
    guide = refresh.find("if (bGuidesThePlayerForTheFloorRule && GuideWalkSpeedCmPerSecond > 0.0f)")
    designed = refresh.find("DesignedWalkSpeedCmPerSecond * SpeedMultiplier() * AilmentWalkMultiplier()")
    assert -1 < guide < designed, "a guide's walk speed is not applied before the designed speed and its multipliers"
    branch = refresh[guide:refresh.index("}", refresh.index("return;", guide)) + 1]
    assert "Movement->MaxWalkSpeed = GuideWalkSpeedCmPerSecond;" in branch and "return;" in branch, (
        "a guide's walk speed is not applied in place of the designed speed")


def test_once_it_waits_it_waits_while_the_player_is_beyond_its_chain() -> None:
    step = squeeze(body(code(MODE), "void ACataclysmDungeonGameMode::StepDemonicGuide(", "CataclysmDungeonGameMode.cpp"))
    assert "bBeyond = Apart > Effects::DemonicGuideChainCm;" in step
    assert ("bWaits = Apart > Effects::DemonicGuideWaitsBeyondCm || (Guide->bGuideWaits && bBeyond);" in step), (
        "the guide walks on again at 15 m, outside its chain, so a player catching up cannot get back inside it")
