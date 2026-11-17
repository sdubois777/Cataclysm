"""A creature that dies plays a death clip, and its body waits for it.

WHY THIS EXISTS. Issue #522. An enemy whose health reached zero stopped acting
and was destroyed on the next tick. It played nothing and was gone within a
frame, which is the smallest thing that makes a fight end and is not what dying
should look like. This project settles combat by playing it, and death is the
most visible moment in a fight.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmEnemyDeathTests.cpp`
never run on a pull request. Reading the source as text does. That is the same
arrangement `tools/tests/test_an_enemys_rarity_is_shown_before_the_fight.py`
uses, and its header says why.

THE THING MOST WORTH GUARDING, AND IT IS NOT PLAYING THE CLIP. Playing it is one
call. **Keeping it** is the part that breaks silently: the Abyssal Warden's
`UpdateLoopingAnimation` runs every frame and puts an idle loop on whenever the
creature is not moving, and a corpse is not moving. Without the creature's
per-frame work being switched off, the death pose is replaced within a frame or
two and the creature appears to stand up before vanishing. Nothing about the code
that plays the clip would look wrong.

WHAT IS NOT CHECKED HERE. Whether the clip looks like dying, or whether the
length reads well. The automation command in `tools/unreal_build.py` passes
`-nullrhi`, so nothing reaches a screen under test.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
CHARACTERS = SOURCE / "Character"
ENEMY_CPP = CHARACTERS / "CataclysmEnemyCharacter.cpp"
ENEMY_H = CHARACTERS / "CataclysmEnemyCharacter.h"
DEATH_H = CHARACTERS / "CataclysmEnemyDeath.h"
DEATH_CPP = CHARACTERS / "CataclysmEnemyDeath.cpp"
CONTENT = REPO_ROOT / "game" / "Content"
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: `TEXT("/Game/ParagonGrux/.../Animations/" "Death_A.Death_A");` as it is
#: written after a wrapped string literal has been joined back together.
DEATH_PATH = re.compile(
    r"Death\w*AnimationPath\s*=\s*((?:TEXT\(\s*)?(?:\"[^\"]*\"\s*)+)")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. A dying "
            f"creature's animation is issue #522; if a file was renamed, rename "
            f"it here too.")
    return path.read_text(encoding="utf-8")


def flat(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out.

    THE COMMENTS HAVE TO GO OR THE CHECKS BELOW READ THEM. Every rule in this
    project is written down beside the code that implements it, in a comment
    naming the very thing being searched for. `HandleDeath` carries a comment
    saying what SetActorTickEnabled does and why, which contains the text one of
    the checks below looks for.
    """
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


def joined(literal: str) -> str:
    """A wrapped C++ string literal, as the one string the compiler sees."""
    return "".join(re.findall(r"\"([^\"]*)\"", literal))


# ---------------------------------------------------------------------------
# That a clip is played at all
# ---------------------------------------------------------------------------

def test_a_dying_creature_plays_a_clip() -> None:
    body = function_body(read(ENEMY_CPP), "void ACataclysmEnemyCharacter::HandleDeath")

    assert "PlayDeathAnimation()" in body, (
        "HandleDeath never plays a death animation, so a creature still blinks "
        "out of existence when it dies. Issue #522.")


def test_the_body_waits_for_the_clip_rather_than_going_next_tick() -> None:
    """Playing a clip and removing the body immediately shows nothing.

    WHAT THE MISTAKE LOOKS LIKE. Adding the call above and leaving
    `SetTimerForNextTick` where it was. The clip starts, the actor is destroyed a
    frame later, and the result on screen is what it was before: a creature that
    vanishes. Every test that only checks the clip was chosen still passes.
    """
    body = function_body(read(ENEMY_CPP), "void ACataclysmEnemyCharacter::HandleDeath")

    assert "CorpseSeconds" in body, (
        "HandleDeath does not record how long the body is kept, so it cannot be "
        "waiting for the clip. Issue #522.")

    assert re.search(r"SetTimer\s*\(", body), (
        "HandleDeath sets no timer for a measured length, so the body is not "
        "waiting for the death clip to finish.")

    # AND THE OLD BEHAVIOUR IS STILL THERE FOR A CREATURE WITH NO ART, which is
    # five of the seven vertical slice creatures.
    assert "SetTimerForNextTick" in body, (
        "HandleDeath no longer removes a creature on the next tick at all. That "
        "is still the right answer for a creature with no art, which has "
        "nothing to play and nothing to wait for.")


def test_the_dying_creature_stops_its_per_frame_work() -> None:
    """The load-bearing half, and the one that would break silently.

    WITHOUT IT THE CORPSE STANDS UP. The Abyssal Warden's
    `UpdateLoopingAnimation` runs from Tick and puts an idle loop on whenever the
    creature is not moving. A corpse is not moving. So the death pose is replaced
    within a frame or two, and nothing about the code that played the clip looks
    wrong.
    """
    body = function_body(read(ENEMY_CPP), "void ACataclysmEnemyCharacter::HandleDeath")

    assert re.search(r"SetActorTickEnabled\s*\(\s*false\s*\)", body), (
        "HandleDeath does not stop the creature's per-frame work, so whatever "
        "its Tick does keeps running over the corpse for the length of the "
        "death clip. For the Abyssal Warden that is an idle loop replacing the "
        "death pose. Issue #522.")


def test_the_death_draw_is_not_the_drop_draw() -> None:
    """Which clip a creature falls with must not change what it dropped.

    BOTH ARE SEEDED FROM THE SAME TWO FACTS -- the creature and the moment it
    died -- because those are the only two available, so without a salt the two
    streams are identical and the clip drawn shifts the loot.
    """
    body = function_body(
        read(ENEMY_CPP), "float ACataclysmEnemyCharacter::PlayDeathAnimation")

    assert "DeathDrawSalt" in body, (
        "PlayDeathAnimation does not salt its random stream, so it draws from "
        "the same sequence the creature's drops came from and which clip it "
        "died with changes what it dropped.")

    found = re.search(
        r"static\s+constexpr\s+int32\s+DeathDrawSalt\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;",
        read(ENEMY_H))
    assert found, (
        "ACataclysmEnemyCharacter has no DeathDrawSalt. It is what separates "
        "the death clip's draw from the drop roll's.")

    assert int(found.group(1), 0) != 0, (
        "DeathDrawSalt is zero, which salts nothing: the death draw and the "
        "drop draw would be the same stream again.")


# ---------------------------------------------------------------------------
# Which clip
# ---------------------------------------------------------------------------

def test_the_last_clip_can_actually_be_drawn() -> None:
    """An off-by-one here reads as a creature with no death animation.

    `FRandomStream::RandRange` includes both ends. Passing the count rather than
    the count minus one lets the draw answer one past the array, and
    `PlayDeathAnimation` refuses an index it cannot use -- so the Abyssal Warden
    would simply fail to play anything a fraction of the time, with no error
    anywhere.
    """
    body = function_body(read(DEATH_CPP), "int32 UCataclysmEnemyDeath::ClipToPlay")

    assert re.search(r"RandRange\s*\(\s*0\s*,\s*ClipCount\s*-\s*1\s*\)", body), (
        "UCataclysmEnemyDeath::ClipToPlay does not draw with an inclusive upper "
        "bound of ClipCount - 1. FRandomStream::RandRange includes both ends, "
        "so passing ClipCount draws one past the array. Issue #522.")


def test_a_creature_with_no_clip_keeps_its_body_for_no_time() -> None:
    """Five of the seven vertical slice creatures reach this every time.

    It has to answer zero, which HandleDeath reads as "on the next tick, as
    before", rather than a small positive number that would leave a placeholder
    cylinder standing still for a moment.
    """
    body = function_body(
        read(DEATH_CPP), "float UCataclysmEnemyDeath::CorpseSecondsFor")

    assert re.search(r"ClipLength\s*<=\s*0", body), (
        "UCataclysmEnemyDeath::CorpseSecondsFor no longer answers zero for a "
        "clip of no length. A creature with no art would then have its body "
        "held for whatever a missing clip reports.")


def test_the_body_is_never_kept_indefinitely() -> None:
    """A ceiling on how long a body stays, whatever clip it was given."""
    header = read(DEATH_H)

    found = re.search(
        r"static\s+constexpr\s+float\s+LongestCorpseSeconds\s*=\s*([\d.]+)f\s*;",
        header)
    assert found, (
        "UCataclysmEnemyDeath has no LongestCorpseSeconds. It is the ceiling "
        "that stops an art choice made in a folder leaving a body standing in "
        "the room.")

    ceiling = float(found.group(1))
    assert 1.6667 < ceiling <= 30.0, (
        f"LongestCorpseSeconds is {ceiling}. It has to be longer than the "
        f"longest death clip that exists, which is the Abyssal Warden's "
        f"1.6667 seconds, or that creature's own death would be cut short.")

    body = function_body(
        read(DEATH_CPP), "float UCataclysmEnemyDeath::CorpseSecondsFor")
    assert "LongestCorpseSeconds" in body, (
        "CorpseSecondsFor does not use LongestCorpseSeconds, so the ceiling is "
        "declared and never applied.")


# ---------------------------------------------------------------------------
# The art the clips point at
# ---------------------------------------------------------------------------

def test_every_death_clip_path_points_at_an_asset_that_exists() -> None:
    """A path typed wrong reads as a creature that simply has no death clip.

    WHAT GOES WRONG WITHOUT THIS. `FSoftObjectPath::TryLoad` answers null for a
    path that names nothing, `PlayDeathAnimation` refuses a null clip, and the
    creature vanishes on the next tick exactly as it did before issue #522. No
    error is logged and no test in the engine would fail.

    SKIPPED WITHOUT THE PACKS, which are gitignored and therefore absent on
    continuous integration. A skip here means this check did not run, not that
    it passed.
    """
    named: dict[str, str] = {}
    for source in sorted(CHARACTERS.glob("*.cpp")):
        for literal in DEATH_PATH.findall(source.read_text(encoding="utf-8")):
            path = joined(literal)
            if path:
                named[f"{source.name}: {path}"] = path

    assert named, (
        "No enemy class names a death clip, so no creature in the game plays "
        "one. Issue #522.")

    for where, path in sorted(named.items()):
        # `/Game/Folder/Name.Name` is the package followed by the object inside
        # it. The file on disk is the package, with .uasset on the end.
        package = path.split(".", 1)[0]
        assert package.startswith("/Game/"), (
            f"{where} does not start with /Game/, so it names nothing under "
            f"game/Content/.")

        asset = CONTENT / (package[len("/Game/"):] + ".uasset")
        if not asset.parent.is_dir():
            pytest.skip(
                f"{asset.parent.relative_to(REPO_ROOT).as_posix()} is not "
                f"present. The Paragon packs are gitignored, so this check "
                f"only runs on a machine that has them.")

        assert asset.is_file(), (
            f"{where} names an asset that does not exist at "
            f"{asset.relative_to(REPO_ROOT).as_posix()}. The creature would "
            f"load nothing and vanish on the next tick with no error anywhere. "
            f"Issue #522.")


def test_the_measured_lengths_are_written_down() -> None:
    """Every figure taken off an asset is recorded, per CLAUDE.md."""
    text = flat(ASSET_RECORD)

    for clip, length in (("Death_A", "1.6667"), ("Death_B", "1.6333"),
                         ("Death_A", "0.7667")):
        assert f"`{clip}` | {length}" in text, (
            f"game/docs/enemy-source-assets.md no longer records {clip} at "
            f"{length} seconds. Every length taken off an asset is written "
            f"there, and the body of a dying creature is kept for exactly it.")


def test_the_design_document_records_what_dying_is() -> None:
    """A design decision is not real until it is in docs/, per CLAUDE.md."""
    text = flat(GDD)

    assert ("A creature plays a death animation and its body is removed when "
            "the clip ends") in text, (
        "docs/Cataclysm_GDD_v2.md no longer says that a creature plays a death "
        "animation. That is the decision this code implements. Issue #522.")

    assert "A creature with no art is still removed on the next frame" in text, (
        "docs/Cataclysm_GDD_v2.md no longer records what a creature with no art "
        "does when it dies. That is five of the seven vertical slice creatures, "
        "so it is the common case rather than the exception.")
