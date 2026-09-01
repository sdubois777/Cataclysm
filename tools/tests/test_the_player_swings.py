"""The player character moves when it uses a skill, and the swing does not walk it forward.

WHY THIS EXISTS. Issue #1126. The player used every skill it had without moving
at all: the cast effect flashed, damage landed, and the body stood still through
it. Until issue #1124 there was no skeleton to play anything on, so there was
nothing to fix; now there is.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in
`game/Source/Cataclysm/Tests/CataclysmPlayerAttackAnimationTests.cpp` never run
on a pull request. Reading the source as text does. That is the same arrangement
`tools/tests/test_the_player_has_a_body.py` uses.

THE THING MOST WORTH GUARDING IS THE ROOT MOTION. All three attack clips carry
it, measured through the editor on 2026-09-01, and
`UCharacterMovementComponent` takes root motion from montages by default. Left
alone, every basic attack shoves the character a step forward -- and the basic
attack fires by itself, several times a second, at whatever is in reach. Nothing
about the code that plays the clip would look wrong; the character would simply
drift across the floor while fighting.

THE SECOND THING IS THAT THE CALL IS IN THE SHARED PLACE. All eight skill shapes
call `UCataclysmSkillTemplate::CommitAndBegin` first, so the call belongs there
and nowhere else. Put in one shape's `ActivateAbility` instead, seven of the
eight would silently animate nothing.

WHAT IS NOT CHECKED HERE. Whether the swing looks like a swing, whether it is
the right clip for the weapon being held, or whether it lands where the damage
does. The automation command in `tools/unreal_build.py` passes `-nullrhi`, so
nothing reaches a screen under test. Somebody has to look.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
PLAYER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
PLAYER_H = SOURCE / "Character" / "CataclysmPlayerCharacter.h"
BASE_H = SOURCE / "Character" / "CataclysmCharacterBase.h"
SKILL_CPP = SOURCE / "AbilitySystem" / "CataclysmSkillTemplate.cpp"
SHAPES_CPP = SOURCE / "AbilitySystem" / "CataclysmSkillTemplates.cpp"
CONTENT = REPO_ROOT / "game" / "Content"
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "player-source-assets.md"

#: The clips the character cycles through, and the one deliberately left out.
#:
#: MM_ChargedAttack IS EXCLUDED ON PURPOSE. At 1.8333 seconds it is nearly three
#: times a fast weapon's swing interval, so cycling it into an attack that fires
#: by itself would mean playing it at close to triple speed. It is copied into
#: the repository and available for a skill that deserves a heavier swing.
ATTACK_CLIPS = ("MM_Attack_01", "MM_Attack_02", "MM_Attack_03")
NOT_CYCLED = "MM_ChargedAttack"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. Making "
            f"the player swing is issue #1126; if a file was renamed, rename it "
            f"here too.")
    return path.read_text(encoding="utf-8")


def strip_comments(text: str) -> str:
    """The file's code, without its comments.

    Every rule in this project is written down beside the code that implements
    it, in a comment naming the very thing being searched for. Searching the raw
    text finds the explanation rather than the code.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out."""
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


# --------------------------------------------------------------------------
# The swing happens at all, and in the one shared place
# --------------------------------------------------------------------------


def test_using_any_skill_plays_an_attack_animation():
    """The call sits in the function every skill shape calls first."""
    body = function_body(
        read(SKILL_CPP), "bool UCataclysmSkillTemplate::CommitAndBegin")

    assert "PlayAttackAnimation" in body, (
        "UCataclysmSkillTemplate::CommitAndBegin does not call "
        "PlayAttackAnimation, so a character uses every skill it has without "
        "moving. That is what issue #1126 set out to fix.")


def test_the_call_is_in_the_shared_place_and_not_in_one_shape():
    """Eight shapes, one call.

    A call placed in `UCataclysmStrikeSkill::ActivateAbility` instead would
    animate a sword swing and nothing else: the other seven shapes would use
    their skills in silence and nothing would look broken in that one file.
    """
    shapes = strip_comments(read(SHAPES_CPP))

    assert "PlayAttackAnimation" not in shapes, (
        "CataclysmSkillTemplates.cpp calls PlayAttackAnimation from an "
        "individual skill shape. It belongs in "
        "UCataclysmSkillTemplate::CommitAndBegin, which all eight shapes call "
        "first, so that one call reaches every shape and the basic attack.")

    # AND THE SHARED FUNCTION IS STILL THE ONE THEY ALL CALL. If a shape ever
    # stopped calling it, the check above would keep passing while that shape
    # silently animated nothing.
    calls = shapes.count("CommitAndBegin(Handle, ActorInfo, ActivationInfo)")
    assert calls == 8, (
        f"{calls} of the eight skill shapes call CommitAndBegin, not 8. Every "
        f"shape has to, or the ones that do not animate nothing and pay no "
        f"cost.")


def test_the_default_is_to_do_nothing_so_enemies_are_unaffected():
    """A virtual with an empty body, because the skeletons differ.

    `ACataclysmHellhoundCharacter` uses a skill template and wears a Paragon
    body. A Mannequin clip played on it would be an animation for a skeleton it
    does not have.
    """
    base = read(BASE_H)

    assert "virtual void PlayAttackAnimation() {}" in base, (
        "ACataclysmCharacterBase does not declare PlayAttackAnimation as a "
        "virtual that does nothing. Every enemy relies on that default: they "
        "animate their own attacks from their own classes with clips from "
        "their own packs, and must not be given the player's.")


# --------------------------------------------------------------------------
# The swing does not walk the character forward
# --------------------------------------------------------------------------


def test_no_animation_may_move_the_character():
    """The half that breaks silently, and the reason this file exists.

    All three attack clips carry root motion and an animation instance defaults
    to `RootMotionFromMontagesOnly`, so without something stopping it every
    basic attack drives the capsule forward -- several times a second, at
    whatever happens to be in reach.

    THIS CHECK REPLACED ONE THAT WAS WORTHLESS, AND THAT IS WORTH RECORDING.
    The first version asserted that `PlayAttackAnimation` cleared
    `bEnableRootMotionTranslation` and `bEnableRootMotionRotation` on the
    montage. It passed. The project owner then played the game and reported
    every ability making the character surge forward: "it's just a slide
    forward". Those two flags are read in `UAnimMontage::PostLoad` and nowhere
    else at run time; what decides it is `UAnimMontage::HasRootMotion()`, which
    asks each sequence and never looks at either flag, and which
    `UAnimInstance::Montage_Play` consults while starting the montage -- so a
    flag set afterwards was set too late twice over.

    THE LESSON IS THE SHAPE OF THE CHECK, not the API. The old one asserted
    that a line I wrote was present. It could not tell whether the character
    moved, so it could not fail for the reason it existed.
    """
    body = function_body(
        read(PLAYER_CPP),
        "bool ACataclysmPlayerCharacter::ResolveAnimationBlueprint")

    assert "SetRootMotionMode(ERootMotionMode::IgnoreRootMotion)" in body, (
        "ResolveAnimationBlueprint does not set the animation instance to "
        "ignore root motion, so every attack clip drives the character "
        "forward. IgnoreRootMotion means 'extract it but do not apply it', "
        "which keeps the mesh on the capsule while moving neither.")


def test_using_a_skill_turns_the_character_to_face_the_aim():
    """The body faces what the skill is aimed at.

    WHY THIS EXISTS. The project owner played the first attack animations on
    2026-09-01 and reported that the character attacked in the direction it was
    facing "instead of attacking towards the mouse". A skill has always aimed
    its damage and its effect with `AimDirection`; nothing ever turned the
    character, so the body faced whatever direction it last walked in. With
    nothing drawn that was invisible. With a visible body swinging a visible
    weapon it is not.

    IN THE SHARED FUNCTION, so all eight skill shapes turn rather than one.
    """
    body = function_body(
        read(SKILL_CPP), "bool UCataclysmSkillTemplate::CommitAndBegin")

    assert "SetActorRotation" in body, (
        "UCataclysmSkillTemplate::CommitAndBegin does not turn the character to "
        "face its aim, so a skill hits toward the cursor while the body faces "
        "wherever it last walked.")

    assert "Facing.Pitch = 0.0f" in body and "Facing.Roll = 0.0f" in body, (
        "the facing is not flattened to yaw. A cursor trace lands on the floor, "
        "so aiming a character at it without clearing pitch tips the character "
        "over.")


def test_the_ineffective_montage_flags_are_not_used_again():
    """The approach that looks right, does nothing, and was tried.

    PINNED SO IT IS NOT TRIED A SECOND TIME. Clearing the montage's root motion
    flags is the obvious-looking answer and reads correctly in a diff. It has
    no effect at run time. If it ever comes back it will come back with a
    confident comment attached, so this is what says otherwise.
    """
    code = strip_comments(read(PLAYER_CPP))

    for flag in ("bEnableRootMotionTranslation", "bEnableRootMotionRotation"):
        assert flag not in code, (
            f"CataclysmPlayerCharacter.cpp sets {flag} again. Those two are "
            f"read in UAnimMontage::PostLoad and nowhere else at run time, so "
            f"setting them on a montage does not stop root motion. "
            f"SetRootMotionMode(ERootMotionMode::IgnoreRootMotion) in "
            f"ResolveAnimationBlueprint is what does.")


def test_the_swing_goes_through_the_animation_blueprint_slot():
    """Through the slot, so locomotion keeps running underneath.

    Played onto the component directly -- which is what a death does -- the
    swing would replace the locomotion graph and the character would hold the
    last frame of it until something else took the mesh back.
    """
    body = function_body(
        read(PLAYER_CPP), "void ACataclysmPlayerCharacter::PlayAttackAnimation")

    assert "PlaySlotAnimationAsDynamicMontage" in body, (
        "PlayAttackAnimation does not play through the animation Blueprint's "
        "slot. ABP_Unarmed carries a DefaultSlot exactly so an attack can blend "
        "over the locomotion graph and back out again.")

    assert "PlayAnimation(" not in body, (
        "PlayAttackAnimation plays the clip onto the mesh component directly. "
        "That is what PlayDeathAnimation does, and it is right there because a "
        "corpse must hold its last frame. A swing must not: the character would "
        "freeze mid-attack.")


def test_a_clip_is_never_slowed_below_its_authored_speed():
    """Faster to fit, never slower, which is the Abyssal Warden's rule.

    Every attack clip is longer than the interval a weapon's attack speed
    allows, so the rate only ever needs raising. Stretching a clip to fill a
    longer window reads as slow motion, which the Brute's comments record as
    having been tried and rejected.
    """
    body = function_body(
        read(PLAYER_CPP), "void ACataclysmPlayerCharacter::PlayAttackAnimation")

    assert "FMath::Max(1.0f" in body, (
        "PlayAttackAnimation does not floor the play rate at 1, so a clip "
        "shorter than the swing interval would be stretched and play in slow "
        "motion.")

    assert "MaximumAttackPlayRate" in body, (
        "PlayAttackAnimation does not cap the play rate. Attack speed is a stat "
        "that affixes and passives raise and nothing in the design caps it, so "
        "a character stacked far enough would ask for a clip at many times "
        "speed.")


# --------------------------------------------------------------------------
# The clips
# --------------------------------------------------------------------------


def test_the_three_cycled_clips_are_named_and_present():
    """Three clips, cycled in order, and each one is a real asset."""
    text = read(PLAYER_CPP)

    named = re.findall(r"TEXT\(\"(MM_Attack_\w+|MM_ChargedAttack)\"\)", text)
    assert named == list(ATTACK_CLIPS), (
        f"CataclysmPlayerCharacter.cpp names {named} as its attack clips, "
        f"expected {list(ATTACK_CLIPS)} in that order. The order is the cycle, "
        f"so changing it changes which clip follows which.")

    for name in named:
        on_disk = (CONTENT / "Characters" / "Mannequins" / "Anims" / "Unarmed"
                   / "Attack" / (name + ".uasset"))
        assert on_disk.is_file(), (
            f"{name} is named as an attack clip but "
            f"{on_disk.relative_to(REPO_ROOT).as_posix()} does not exist.")


def test_the_charged_attack_is_left_out_on_purpose_and_still_present():
    """Excluded from the cycle, kept in the repository.

    PINNED SO THAT LEAVING IT OUT STAYS A DECISION. It is the obvious fourth
    clip and somebody will wonder why it is not used; this is where the answer
    is, next to a check that it is still there to be used later.
    """
    text = read(PLAYER_CPP)

    assert f'TEXT("{NOT_CYCLED}")' not in text, (
        f"{NOT_CYCLED} is now in the attack cycle. At 1.8333 seconds it is "
        f"nearly three times a fast weapon's swing interval, so cycling it into "
        f"an attack that fires by itself means playing it at close to triple "
        f"speed. If that is wanted, say why here.")

    on_disk = (CONTENT / "Characters" / "Mannequins" / "Anims" / "Unarmed"
               / "Attack" / (NOT_CYCLED + ".uasset"))
    assert on_disk.is_file(), (
        f"{NOT_CYCLED} is no longer in the repository. It is deliberately not "
        f"cycled but is kept for a skill that deserves a heavier swing.")


def test_the_asset_record_names_the_attack_clips():
    """The durable record of what each clip is and how long it runs."""
    text = read(ASSET_RECORD)

    for name in (*ATTACK_CLIPS, NOT_CYCLED):
        assert name in text, (
            f"{ASSET_RECORD.relative_to(REPO_ROOT).as_posix()} does not "
            f"mention {name}. It is the only record of the attack clips' "
            f"lengths and that they carry root motion.")
