"""Broken pieces left at an impact must not be left wearing the placeholder.

WHY THIS EXISTS. Issue #422. `ACataclysmProjectile` stopped, dealt its damage and
destroyed itself, so a thrown rock was gone from one frame to the next. Nothing in
the project had an impact effect of any kind.

THE TRAP, MEASURED RATHER THAN ASSUMED. On 2026-08-08 every one of the five
`SM_Rampage_Rock_Frag` meshes in the Paragon pack was found to have
`/Engine/EngineMaterials/WorldGridMaterial` assigned -- the engine's grey
checkerboard placeholder -- and so does `SM_Rampage_Rock_Rip_Crater`. Spawning any
of them as they come puts large checkered lumps on the floor, which is worse than
nothing appearing. Two other debris meshes beside them carry real materials, so
this is a property of these particular assets rather than of the pack.

WHAT THIS GUARDS. Continuous integration has no Paragon packs and no engine, so
nothing here can look at a mesh. What it holds is that the material is still
passed in, that the caller still supplies one, and that the measurement stays
written down where the next person will find it.

The automation tests in `game/Source/Cataclysm/Tests/CataclysmDebrisBurstTests.cpp`
check the live components, including that a placed piece is not left wearing the
checkerboard.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEBRIS_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
            / "CataclysmDebrisBurst.h")
DEBRIS_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmDebrisBurst.cpp")
BRUTE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmBruteCharacter.cpp")
PROJECTILE_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                / "CataclysmProjectile.h")
ASSET_REFERENCE = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

#: The engine's placeholder. A piece left wearing this is the failure.
PLACEHOLDER_MATERIAL = "WorldGridMaterial"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def code(path: pathlib.Path) -> str:
    """The source with its single-line comments removed.

    WHY THIS IS NEEDED. A guard that searches the whole file is satisfied
    by a call that has been commented out, which is exactly how somebody
    disables a thing temporarily and then forgets. Proved: commenting out
    the Brute's OnFinished binding left the test below passing.

    Only `//` comments are stripped. This project's C++ uses them almost
    exclusively inside function bodies, and a `/* */` block hiding a live
    call would be unusual enough to be seen in review.
    """
    return re.sub(r"//[^\n]*", "", source(path))


def test_the_material_is_supplied_by_the_caller() -> None:
    """Not read off the mesh, because the mesh has the wrong one.

    THE FAILURE THIS EXISTS FOR is somebody simplifying the interface by dropping
    the material argument, on the reasonable-sounding grounds that a mesh already
    knows what it looks like. These five do not.
    """
    header = source(DEBRIS_H)

    assert re.search(r"UMaterialInterface\*\s+Material", header), (
        "ACataclysmDebrisBurst::Scatter no longer takes a material. The five "
        "Paragon fragments carry the engine's checkerboard placeholder, so "
        "pieces spawned without one are grey checkered lumps on the floor."
    )

    body = source(DEBRIS_CPP)
    assert re.search(r"SetMaterial\(\s*0\s*,\s*Material\s*\)", body), (
        "ACataclysmDebrisBurst never applies the material it was given, so the "
        "pieces keep whatever the pack put on them."
    )


def test_the_generic_actor_names_no_project_content() -> None:
    """It has to serve every skill, not just a thrown rock.

    THE DECISION ISSUE #422 ASKED FOR was where an impact effect lives. It lives
    in its own actor, and that only generalises while the actor knows nothing
    about rocks. A stomp and a ground zone want debris as much as a throw does.
    """
    found = re.findall(r"TEXT\(\"(/Game/[^\"]*)\"\)", source(DEBRIS_CPP))
    assert not found, (
        f"CataclysmDebrisBurst.cpp names project or pack content: {found}. This "
        f"actor is meant to serve any skill that wants to leave debris; anything "
        f"it loads itself is worn by every one of them."
    )


def test_the_brute_supplies_the_rock_material_rather_than_the_fragments_own() -> None:
    """The pieces wear the material of the rock they are pieces of."""
    text = source(BRUTE_CPP)

    assert re.search(r"RockMaterial\s*=\s*RockMesh\s*\?\s*RockMesh->GetMaterial\(0\)",
                     text), (
        "the Brute no longer takes its debris material from the rock mesh. The "
        "fragments' own material is the engine's checkerboard placeholder."
    )

    assert "RockMaterial" in text.split("ACataclysmDebrisBurst::Scatter")[1][:400], (
        "the Brute calls Scatter without passing RockMaterial, so the pieces are "
        "left wearing whatever the pack gave them."
    )


def test_the_projectile_is_not_told_about_debris() -> None:
    """It stays generic, which is why the caster listens instead.

    `ACataclysmProjectile::Fire` already takes eleven arguments and is what all
    398 rows of game/Data/WeaponSkills.csv fire through. Adding debris there
    would give every player skill the same pieces and would only ever serve
    projectiles.
    """
    header = source(PROJECTILE_H)

    assert "DebrisBurst" not in header, (
        "ACataclysmProjectile now knows about debris. It is fired by every "
        "projectile skill in the game, so anything it spawns is spawned by all "
        "of them. The caster listens to OnFinished instead."
    )

    assert "FCataclysmProjectileFinished OnFinished" in header, (
        "ACataclysmProjectile no longer broadcasts when it stops, which is the "
        "hook the caster uses to leave debris where the shot ended."
    )


def test_the_brute_listens_for_the_rock_stopping() -> None:
    """And leaves the pieces where it actually stopped, not where it was aimed.

    The two differ for a rock stopped early by an enemy or a wall.

    READ WITH COMMENTS STRIPPED, because a commented-out binding would
    otherwise satisfy this and the rock would silently vanish again.
    """
    text = code(BRUTE_CPP)

    assert re.search(
        r"OnFinished\.AddUObject\(\s*\n?\s*this,\s*&ACataclysmBruteCharacter::"
        r"LeaveRockDebris\s*\)", text), (
        "the Brute does not listen for its thrown rock stopping, so nothing "
        "leaves debris and the rock vanishes again."
    )

    assert "FurthestReached" in text, (
        "the Brute does not use the projectile's FurthestReached, so the debris "
        "is placed where the rock was aimed rather than where it got to. Those "
        "differ whenever something stops it early."
    )


def test_the_measurement_stays_written_down() -> None:
    """The next person to reach for these meshes needs to know."""
    text = source(ASSET_REFERENCE)

    assert PLACEHOLDER_MATERIAL in text, (
        f"game/docs/enemy-source-assets.md no longer records that the fragments "
        f"carry {PLACEHOLDER_MATERIAL}. Without it, the reason the material is "
        f"passed in looks like an arbitrary complication."
    )

    assert "M_Rock_To_Throw" in text, (
        "the document no longer says which material the pieces should wear."
    )
