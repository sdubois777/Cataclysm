"""Demonic Guide's guide is nobody's target, the player's side does not block itself, and the guide leaves footprints.

WHY THIS EXISTS. Three issues from the owner's play-test of 2026-11-23, on the main checkout at development 4d33f5994:

  #2860 "Demonic Guide: sometimes he manages to die. My minions are constantly trying to attack him even though they
        usually do 0 damage."
  #2861 "He can't walk through my minions, and neither can I, which is annoying. There shouldn't be collision between
        player and minions."
  #2862 "I also think we should show like yellow footsteps or something pointing to where he's gone. He's kinda easy
        to lose track of."

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the automation tests that cover these --
`Cataclysm.DungeonModifierEffects.TheGuide...` and `Cataclysm.PassThroughBodies.` -- never run on a pull request.
This reads the source with its comments removed, so a line that only a comment states cannot satisfy it, and it
checks the one piece of arithmetic the three share: which pair of bodies blocks which.

WHAT IS CHECKED:

  1. the targeting search refuses a character that is nobody's target, in the sided search and the side-less one;
  2. the guide is that character, and a blow aimed at it is refused;
  3. its death, called outright, is refused before the creature is marked dead;
  4. a body's mask is written at BeginPlay and whenever its side is set, and the guide's once it is the guide;
  5. the three bits pass the pairs the issue names and block every other pair, worked out here from the figures;
  6. the footprints are made with the guide, stepped on its beat, destroyed with it, and their pool holds a
     lifetime's trail at the guide's default walk.
"""

from __future__ import annotations

import itertools
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
TARGETING = SOURCE / "AbilitySystem" / "CataclysmTargeting.cpp"
SKILL_EFFECTS = SOURCE / "AbilitySystem" / "CataclysmSkillEffects.cpp"
BASE_H = SOURCE / "Character" / "CataclysmCharacterBase.h"
BASE_CPP = SOURCE / "Character" / "CataclysmCharacterBase.cpp"
PLAYER_H = SOURCE / "Character" / "CataclysmPlayerCharacter.h"
ENEMY_H = SOURCE / "Character" / "CataclysmEnemyCharacter.h"
ENEMY_CPP = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"
MODE = SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp"
EFFECTS_H = SOURCE / "Dungeon" / "CataclysmDungeonModifierEffects.h"
FOOTPRINTS = SOURCE / "Dungeon" / "CataclysmGuideFootprints.cpp"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def squeeze(text: str) -> str:
    """Whitespace runs as one space, so a wrapped call reads as one line."""
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str, where: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`, squeezed."""
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return squeeze(text[opening:index + 1])
    raise AssertionError(f"{signature!r} in {where} is not closed")


def figure(text: str, name: str) -> float:
    """A `static constexpr` number stated in `text`."""
    found = re.search(rf"\b{name}\s*=\s*([0-9.]+)f?\s*;", text)
    assert found, f"no figure named {name}"
    return float(found.group(1))


# ---------------------------------------------------------------------------
# #2860: nobody's target
# ---------------------------------------------------------------------------

def test_the_search_refuses_nobodys_target_on_every_side() -> None:
    targeting = code(TARGETING)
    matches = body(targeting, "bool UCataclysmTargeting::MatchesAttitude(", "CataclysmTargeting.cpp")
    refusal = matches.find("if (IsNeverATarget(Actor)) { return false; }")
    sides = matches.find("if (!Instigator)")
    assert refusal != -1, (
        "MatchesAttitude no longer refuses a character that is nobody's target, so a minion's and a thrall's "
        "search finds Demonic Guide's guide again (issue #2860)")
    assert refusal < sides, (
        "the refusal comes after the instigator's side is asked, so a search with no instigator -- a hazard "
        "belonging to nobody -- still finds the guide")
    gather = body(targeting, "TArray<AActor*> UCataclysmTargeting::Gather(", "CataclysmTargeting.cpp")
    assert "!UCataclysmSkillEffects::IsDead(Actor) && !IsNeverATarget(Actor))" in gather, (
        "the side-less search, a Hellhound's burning lane, no longer refuses nobody's target")
    asked = body(targeting, "bool UCataclysmTargeting::IsNeverATarget(", "CataclysmTargeting.cpp")
    assert "Character && Character->IsNeverATarget()" in asked


def test_the_guide_is_nobodys_target_and_a_blow_at_it_is_refused() -> None:
    enemy = squeeze(code(ENEMY_H))
    assert ("virtual bool IsNeverATarget() const override { return bGuidesThePlayerForTheFloorRule; }"
            in enemy), "Demonic Guide's guide is no longer the character that is nobody's target"
    base = squeeze(code(BASE_H))
    assert "virtual bool IsNeverATarget() const { return false; }" in base, (
        "the player and every other creature must be targets as before")
    untargetable = body(code(SKILL_EFFECTS), "bool UCataclysmSkillEffects::IsUntargetable(",
                        "CataclysmSkillEffects.cpp")
    assert "UCataclysmTargeting::IsNeverATarget(Actor)" in untargetable, (
        "a blow aimed straight at the guide is no longer refused by ApplyHit")


def test_the_guides_death_called_outright_is_refused_before_the_mark() -> None:
    death = body(code(ENEMY_CPP), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    guard = death.find("if (IsNeverATarget())")
    mark = death.find("UCataclysmSkillEffects::MarkDead(this)")
    assert guard != -1, (
        "HandleDeath no longer refuses nobody's target, so a rule that kills by calling it outright -- Epidemic, "
        "Infernal Sacrifice, a drained delayed blow -- kills Demonic Guide's guide (issue #2860)")
    assert -1 < guard < mark, "the guide is marked dead before its death is refused, so every rule reads it as dead"
    assert "return;" in death[guard:mark], "the guard no longer returns before the death is handled"


# ---------------------------------------------------------------------------
# #2861: which bodies block one another
# ---------------------------------------------------------------------------

def bits() -> dict[str, int]:
    base = code(BASE_H)
    return {name: int(figure(base, f"{name}BodyMaskBit")) for name in ("Player", "Commanded", "Guide")}


def test_the_three_bits_are_separate_and_fit_the_engines_filter() -> None:
    found = bits()
    values = list(found.values())
    assert all(value > 0 and value & (value - 1) == 0 for value in values), f"each must be one bit: {found}"
    assert len(set(values)) == 3, f"two bodies share a bit: {found}"
    # `NumExtraFilterBits` in Engine/Classes/Engine/EngineTypes.h is 6; the .cpp asserts it against the engine's.
    assert sum(values) < 1 << 6, f"a bit above the engine's six extra filter bits: {found}"
    assert re.search(r"static_assert\(PassThroughBodyMaskBits < \(1 << NumExtraFilterBits\)", code(BASE_CPP))


def test_the_bodies_block_the_pairs_the_issue_names_and_no_others() -> None:
    """Worked from the figures and the rule `MovesThroughMaskFor` states: a moving body skips a body whose mask
    filter shares a bit with its own move-ignore mask, which is the other two of the three bits.

    The table is the owner's issue: the player and what it commands pass through each other, the guide passes
    through both, a creature blocks and is blocked by all three, and -- a judgement -- two commanded bodies still
    block each other.
    """
    moves = body(code(BASE_CPP), "uint8 ACataclysmCharacterBase::MovesThroughMaskFor(", "CataclysmCharacterBase.cpp")
    assert "OwnBit != 0 ? static_cast<uint8>(PassThroughBodyMaskBits & ~OwnBit)" in moves, (
        "MovesThroughMaskFor no longer moves through the other two bits and not its own")
    found = bits()
    carried = {"player": found["Player"], "minion": found["Commanded"], "thrall": found["Commanded"],
               "guide": found["Guide"], "creature": 0}
    every = found["Player"] | found["Commanded"] | found["Guide"]

    def ignores(own: int) -> int:
        return every & ~own if own else 0

    def blocks(mover: str, other: str) -> bool:
        return (ignores(carried[mover]) & carried[other]) == 0

    passes = {frozenset(pair) for pair in (("player", "minion"), ("player", "thrall"), ("player", "guide"),
                                           ("minion", "guide"), ("thrall", "guide"))}
    for mover, other in itertools.permutations(carried, 2):
        expected = frozenset((mover, other)) not in passes
        assert blocks(mover, other) == expected, (
            f"a {mover} moving into a {other} is {'blocked' if blocks(mover, other) else 'let through'}; the "
            f"issue's table says it should be {'blocked' if expected else 'let through'}")


def test_a_body_is_written_at_begin_play_and_with_its_side() -> None:
    base = code(BASE_CPP)
    assert "RefreshPassThroughBody();" in body(base, "void ACataclysmCharacterBase::BeginPlay()",
                                                "CataclysmCharacterBase.cpp"), (
        "a character's body is no longer written as it begins play, so the player starts blocking its minions")
    team = body(base, "void ACataclysmCharacterBase::SetGenericTeamId(", "CataclysmCharacterBase.cpp")
    assert team.find("TeamId = NewTeamId;") < team.find("RefreshPassThroughBody();"), (
        "a side set no longer writes the body after it, so a thrall keeps the monsters' body when it is taken")
    refresh = body(base, "void ACataclysmCharacterBase::RefreshPassThroughBody()", "CataclysmCharacterBase.cpp")
    assert "Capsule->SetMaskFilterOnBodyInstance(OwnBit);" in refresh
    assert "Capsule->SetMoveIgnoreMask(MovesThroughMaskFor(OwnBit));" in refresh
    side = body(base, "uint8 ACataclysmCharacterBase::PassThroughBodyMaskBit()", "CataclysmCharacterBase.cpp")
    assert "TeamId == UCataclysmTeams::IdFor(ECataclysmTeam::Players) ? CommandedBodyMaskBit : 0" in side
    assert ("virtual uint8 PassThroughBodyMaskBit() const override { return PlayerBodyMaskBit; }"
            in squeeze(code(PLAYER_H))), "the player is no longer the player's body"
    assert ("return bGuidesThePlayerForTheFloorRule ? GuideBodyMaskBit : Super::PassThroughBodyMaskBit();"
            in squeeze(code(ENEMY_H))), "the guide is no longer the guide's body, or a thrall no longer a commanded one"


# ---------------------------------------------------------------------------
# #2861 and #2862 at the guide's raising, its beat and its end
# ---------------------------------------------------------------------------

def test_the_guide_takes_its_body_and_its_trail_when_it_is_raised() -> None:
    place = body(code(MODE), "void ACataclysmDungeonGameMode::PlaceTheGuide()", "CataclysmDungeonGameMode.cpp")
    flag = place.find("Guide->bGuidesThePlayerForTheFloorRule = true;")
    refresh = place.find("Guide->RefreshPassThroughBody();")
    trail = place.find("DemonicGuideFootprints = ACataclysmGuideFootprints::Begin(")
    assert -1 < flag < refresh, (
        "the guide's body is not written after it is made the guide, so it keeps a creature's body and the "
        "player's minions block it (issue #2861)")
    assert trail != -1, "no footprints are begun with the guide (issue #2862)"


def test_the_trail_is_stepped_on_the_guides_beat_and_ends_with_it() -> None:
    mode = code(MODE)
    step = body(mode, "void ACataclysmDungeonGameMode::StepDemonicGuide(", "CataclysmDungeonGameMode.cpp")
    assert "Footprints->Step(Feet, Guide->GetActorRotation(), BeatSeconds, bWaits);" in step, (
        "the guide's beat no longer steps its footprints, so none is ever laid")
    forget = body(mode, "void ACataclysmDungeonGameMode::ForgetTheGuide()", "CataclysmDungeonGameMode.cpp")
    assert "Footprints->Destroy();" in forget and "DemonicGuideFootprints = nullptr;" in forget, (
        "the footprints outlive their guide")


def test_the_pool_holds_a_lifetimes_trail_at_the_guides_default_walk() -> None:
    effects = code(EFFECTS_H)
    spacing = figure(effects, "DemonicGuideFootprintSpacingCm")
    seconds = figure(effects, "DemonicGuideFootprintSeconds")
    pool = figure(effects, "DemonicGuideFootprintsAtMost")
    share = figure(effects, "DemonicGuideWalkShareOfPlayer")
    jump = figure(effects, "DemonicGuideFootprintJumpCm")
    opacity = figure(effects, "DemonicGuideFootprintOpacity")
    player_walk = figure(code(SOURCE / "Character" / "CataclysmPlayerCharacter.h"), "DefaultWalkSpeedCmPerSecond")
    assert pool * spacing >= seconds * player_walk * share, (
        f"{pool:.0f} prints {spacing:.0f} cm apart cannot hold {seconds:.0f} s of a guide walking at "
        f"{player_walk * share:.0f} cm/s")
    assert jump > 2 * spacing, "a jump shorter than two prints would cut a fast guide's walk into separate trails"
    assert 0.0 < opacity <= 1.0
    colour = re.search(r'DemonicGuideFootprintColourHex\s*=\s*TEXT\("([0-9A-Fa-f]{6})"\)', effects)
    assert colour, "the footprints' colour is not six hex digits"
    red, green, blue = (int(colour.group(1)[i:i + 2], 16) for i in (0, 2, 4))
    assert red > 200 and green > 150 and blue < 80, f"#{colour.group(1)} is not the yellow the owner asked for"
    footprints = code(FOOTPRINTS)
    assert "ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond" in footprints, (
        "the build no longer checks the pool against the player's own default walk")
