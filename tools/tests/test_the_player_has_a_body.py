"""The player character wears the Mannequin, and every path it names is real.

WHY THIS EXISTS. Issue #1124. The player was two engine primitives from
/Engine/BasicShapes: a cylinder for the body and a cone stuck on the front so
that a player could tell which way it faced. It had no skeleton, nothing to hang
a weapon on, and nothing to play a death animation on -- so nothing did.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmPlayerBodyTests.cpp`
never run on a pull request. Reading the source as text does. That is the same
arrangement `tools/tests/test_a_dying_enemy_plays_its_death_animation.py` uses,
and its header says why.

THE THING MOST WORTH GUARDING IS THE PATHS. Every asset here is found by path at
run time through `FSoftObjectPath::TryLoad`, which answers null for a path that
does not exist. `ResolveBody` treats null as survivable and logs a warning,
because a checkout without the art should still be able to walk and fight. That
is the right behaviour and it is also why a mistyped path is invisible: the game
starts, the character is missing, and nothing fails. So each path is checked
against the file on disk here rather than trusted.

THE SECOND THING IS THAT REVIVING PUTS THE ANIMATION BLUEPRINT BACK. Dying
switches the mesh component into single-node mode so the body holds the last
frame of its death clip. Nothing else ever takes it out of that mode, so a
`Revive` that forgot would leave the character walking around frozen in the pose
it died in -- and the code that plays the death clip would look perfectly fine.

WHAT IS NOT CHECKED HERE. Whether the character looks right, or whether the
blend space blends well. The automation command in `tools/unreal_build.py`
passes `-nullrhi`, so nothing reaches a screen under test. Somebody has to look.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
PLAYER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
PLAYER_H = SOURCE / "Character" / "CataclysmPlayerCharacter.h"
CONTENT = REPO_ROOT / "game" / "Content"
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "player-source-assets.md"

#: The folder every asset in this feature lives under.
MANNEQUINS = "/Game/Characters/Mannequins"

#: A `/Game/...` path inside a C++ string literal, including one broken across
#: several adjacent literals by a line wrap. The engine's own convention repeats
#: the asset name after a dot, and a Blueprint class adds a `_C`.
GAME_PATH = re.compile(r"\"(/Game/[^\"]*)\"((?:\s*\"[^\"]*\")*)")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. Giving "
            f"the player a body is issue #1124; if a file was renamed, rename "
            f"it here too.")
    return path.read_text(encoding="utf-8")


def game_paths(text: str) -> set[str]:
    """Every `/Game/...` path the source names, wrapped literals joined up."""
    found = set()
    for match in GAME_PATH.finditer(text):
        joined = match.group(1)
        for piece in re.findall(r"\"([^\"]*)\"", match.group(2) or ""):
            joined += piece
        found.add(joined)
    return found


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out.

    THE COMMENTS HAVE TO GO OR THE CHECKS BELOW READ THEM, which is the same
    reason `tools/tests/test_a_dying_enemy_plays_its_death_animation.py` has
    this function and says so. It bit here rather than being copied on
    principle: the first version of the test below searched the whole file for
    `PlayDeathAnimation();`, and commenting the call out left the text in place
    and the test still passed. Both of this file's behaviour checks were
    worthless until this existed.
    """
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


def asset_file(game_path: str) -> pathlib.Path:
    """The .uasset on disk for a `/Game/Folder/Name.Name` reference.

    A path with no `.Name` after the last slash is a FOLDER rather than an
    asset -- `DeathAnimationFolder` is one, because the six clip names are held
    separately from it -- and the directory itself is what has to exist.
    """
    without_root = game_path[len("/Game/"):]
    if "." not in without_root.rsplit("/", 1)[-1]:
        return CONTENT / without_root
    package = without_root.split(".", 1)[0]
    return CONTENT / (package + ".uasset")


# --------------------------------------------------------------------------
# The placeholder is gone
# --------------------------------------------------------------------------


def test_the_player_no_longer_builds_a_cylinder_and_a_cone():
    """The two engine primitives are gone from the player, header and all.

    NOT A TIDINESS CHECK. `ACataclysmAbyssalWardenCharacter::ResolveBody` has to
    call `PlaceholderBody->SetVisibility(false)` because assigning a skeletal
    mesh does not remove a static mesh component that is still attached, and the
    project owner saw exactly that failure on the Warden within minutes: "the
    cylinder base is appearing over him". The player does not hide its
    placeholder, it does not have one, and this is what keeps it that way.
    """
    # THE CODE, NOT THE WORDS. Both files still say in a comment what they used
    # to build and why, which is worth keeping; what must not come back is
    # anything that actually builds one.
    for path, constructs in (
        (PLAYER_CPP, ("CreateDefaultSubobject<UStaticMeshComponent>",
                      "ConstructorHelpers::FObjectFinder<UStaticMesh>",
                      "SetStaticMesh(")),
        (PLAYER_H, ("TObjectPtr<UStaticMeshComponent>",)),
    ):
        text = read(path)
        for construct in constructs:
            assert construct not in text, (
                f"{path.name} builds a static mesh component again "
                f"(`{construct}`). The player's placeholder cylinder and cone "
                f"were removed by issue #1124. If a placeholder is wanted "
                f"again it has to be hidden when the skeletal mesh goes on, "
                f"the way ACataclysmAbyssalWardenCharacter::ResolveBody hides "
                f"the Warden's, or it draws on top of the character.")


# --------------------------------------------------------------------------
# The paths are real
# --------------------------------------------------------------------------


def test_every_game_path_the_player_names_exists_on_disk():
    """A mistyped asset path is silent, so it is checked rather than trusted.

    `FSoftObjectPath::TryLoad` answers null for a path that does not exist and
    `ResolveBody` treats that as survivable, because a checkout without the art
    should still walk and fight. So a typo produces a warning in a log nobody is
    reading and an invisible character, and nothing fails.
    """
    named = game_paths(read(PLAYER_CPP))
    assert named, (
        "CataclysmPlayerCharacter.cpp names no /Game/ asset path at all. It "
        "should name the Mannequin mesh, its animation Blueprint and its death "
        "clips. Issue #1124.")

    missing = []
    for game_path in sorted(named):
        # A Blueprint's generated class is the asset plus a `_C` suffix, and the
        # file on disk is named for the asset rather than the class.
        on_disk = asset_file(game_path.removesuffix("_C"))
        if not (on_disk.is_file() or on_disk.is_dir()):
            missing.append(f"  {game_path}\n      expected "
                           f"{on_disk.relative_to(REPO_ROOT).as_posix()}")

    assert not missing, (
        "CataclysmPlayerCharacter.cpp names asset paths that do not exist:\n"
        + "\n".join(missing)
        + "\n\nThese are loaded by path at run time and a missing one is only a "
          "warning in the log, so nothing else would notice. "
          f"See {ASSET_RECORD.relative_to(REPO_ROOT).as_posix()} for where "
          "these assets came from.")


def test_the_body_and_its_animation_blueprint_are_named():
    """The two paths without which the character is a moving invisible capsule."""
    text = read(PLAYER_CPP)

    assert f"{MANNEQUINS}/Meshes/" in text.replace('"\n\t\t "', ""), (
        "CataclysmPlayerCharacter.cpp no longer names a mesh under "
        f"{MANNEQUINS}/Meshes/. Without one the player is invisible.")

    named = game_paths(text)
    blueprints = [p for p in named if p.endswith("_C")]
    assert blueprints, (
        "CataclysmPlayerCharacter.cpp names no animation Blueprint class. The "
        "`_C` suffix is what makes a path the generated class rather than the "
        "asset, and `TryLoadClass<UAnimInstance>` returns null without it, "
        "which leaves the character standing in its reference pose.")


def test_all_six_death_clips_are_named_and_present():
    """Six clips, and the count is part of the behaviour rather than a detail.

    `UCataclysmEnemyDeath::ClipToPlay` draws an index into the array, so how
    many clips there are decides which one any given death plays.
    """
    text = read(PLAYER_CPP)
    death_folder = f"{MANNEQUINS}/Anims/Death"

    names = re.findall(r"TEXT\(\"(MM_Death_\w+)\"\)", text)
    assert len(names) == 6, (
        f"CataclysmPlayerCharacter.cpp names {len(names)} death clips, not 6. "
        f"{death_folder} holds six. `DeathAnimationNames` is declared as an "
        f"array of six in the header, so adding one without widening it does "
        f"not compile, and removing one leaves a null entry that is drawn.")

    for name in names:
        on_disk = CONTENT / "Characters" / "Mannequins" / "Anims" / "Death" / (
            name + ".uasset")
        assert on_disk.is_file(), (
            f"{name} is named as a death clip but "
            f"{on_disk.relative_to(REPO_ROOT).as_posix()} does not exist.")


# --------------------------------------------------------------------------
# Dying, and standing back up
# --------------------------------------------------------------------------


def test_dying_plays_a_clip_and_reviving_puts_the_graph_back():
    """The half that breaks silently is the second one.

    `PlayDeathAnimation` switches the mesh into single-node mode so the body
    holds the last frame of its death clip rather than blending back to an idle
    stand. Nothing else takes it out of that mode. A `Revive` that did not
    restore the animation Blueprint would leave a living character frozen in the
    pose it died in, and the code that plays the death clip would look right.
    """
    text = read(PLAYER_CPP)

    dying = function_body(text, "void ACataclysmPlayerCharacter::HandleDeath")
    assert "PlayDeathAnimation()" in dying, (
        "HandleDeath does not call PlayDeathAnimation, so a player character "
        "that dies plays nothing and stands there until it comes back. That is "
        "what issue #1124 set out to fix.")

    reviving = function_body(text, "void ACataclysmPlayerCharacter::Revive")
    assert "ResolveAnimationBlueprint" in reviving, (
        "Revive does not call ResolveAnimationBlueprint. PlayDeathAnimation "
        "puts the mesh component into single-node mode to hold the death pose, "
        "and nothing else takes it out, so a revived character keeps walking "
        "around in the pose it died in.")


def test_the_asset_record_exists_and_names_where_the_art_came_from():
    """The durable record, in the same shape as enemy-source-assets.md."""
    text = read(ASSET_RECORD)

    for expected in ("TemplateResources", "ABP_Unarmed", "SKM_Manny_Simple"):
        assert expected in text, (
            f"{ASSET_RECORD.relative_to(REPO_ROOT).as_posix()} does not "
            f"mention {expected}. It is the only record of where the player's "
            f"art came from and what was deliberately left behind.")
