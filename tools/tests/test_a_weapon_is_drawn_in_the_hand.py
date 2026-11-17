"""The equipped weapon is drawn in the character's hand, and taking it off clears it.

WHY THIS EXISTS. Issue #1125. No weapon was drawn anywhere in this game. A
player equipped a Greataxe, its stats and its six skills changed, and nothing on
screen changed at all. A search of the whole codebase for mesh attachment found
two socket uses in total: the camera boom on the player, and
`ACataclysmBruteCharacter` holding a rock.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmWeaponMeshTests.cpp`
never run on a pull request. Reading the source and the data as text does.

THE HALF THAT BREAKS SILENTLY IS CLEARING, NOT DRAWING. A version of
`RefreshWeaponMeshes` that only ever assigned a mesh would look completely
correct: equipping a sword would put a sword in the hand. Taking it off would
leave the sword there, so the character would go on holding a weapon it does not
have while every number said otherwise. That is issue #840 one step along, and
it is what the check below is for.

THE SECOND THING IS THAT THE TWO TABLES SHARE ROW NAMES.
`game/Data/WeaponMeshes.csv` is looked up by `FCataclysmItem::Base`, which is a
row name in `game/Data/ItemBases.csv`. Both are built by the same `row_name` call
in `tools/generate_datatables.py`, and if they ever drift the lookup silently
finds nothing and the hand stays empty.

WHAT IS NOT CHECKED HERE. Whether the weapon looks right, whether it sits in the
hand at a sensible angle, or whether the scale figures read well. The automation
command in `tools/unreal_build.py` passes `-nullrhi`, so nothing reaches a screen
under test. Somebody has to look.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
ITEM_BASES = DATA / "ItemBases.csv"
WEAPON_MESHES = DATA / "WeaponMeshes.csv"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
PLAYER_CPP = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
LOOKUP_CPP = SOURCE / "Items" / "CataclysmWeaponMeshes.cpp"
CONTENT = REPO_ROOT / "game" / "Content"
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "weapon-source-assets.md"

#: Bases that are meant to draw nothing, and why each one does.
#:
#: THE FIST IS A DESIGN DECISION AND THE OTHER TWO ARE A GAP IN THE ART. Unarmed
#: should show no weapon. The Wand and the Whip have nothing suitable in the
#: weapons pack, which is worth telling apart from the Fist because buying
#: another pack fixes two of these three and should not fix the first.
DRAWS_NOTHING = {
    "Weapon_Fist": "unarmed draws nothing, by design",
    "Weapon_Wand": "nothing suitable in the weapons pack",
    "Weapon_Whip": "nothing suitable in the weapons pack",
}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. Drawing "
            f"the equipped weapon is issue #1125; if a file was renamed, rename "
            f"it here too.")
    return path.read_text(encoding="utf-8")


def rows(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. It is "
            f"produced by tools/generate_datatables.py from the workbook; run "
            f"that first.")
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out.

    THE COMMENTS HAVE TO GO OR THE CHECKS BELOW READ THEM, which is the same
    reason `tools/tests/test_the_player_has_a_body.py` and
    `tools/tests/test_a_dying_enemy_plays_its_death_animation.py` both have this
    function. Every rule in this project is written down beside the code that
    implements it, in a comment naming the very thing being searched for.
    """
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


# --------------------------------------------------------------------------
# The two tables agree
# --------------------------------------------------------------------------


def test_every_weapon_base_has_a_mesh_row_and_nothing_else_does():
    """The lookup is by row name, so the two tables have to hold the same set.

    A BASE WITH NO ROW DRAWS NOTHING AND SAYS NOTHING, which is the exact
    failure issue #1125 asked to be made impossible: "Drawing nothing is a
    reasonable answer; drawing nothing silently is not."
    """
    bases = {r["Name"] for r in rows(ITEM_BASES) if r["Slot"] == "Weapon"}
    meshes = {r["Name"] for r in rows(WEAPON_MESHES)}

    assert bases, "ItemBases.csv holds no weapon bases at all."

    missing = sorted(bases - meshes)
    assert not missing, (
        "these weapon bases have no row in WeaponMeshes.csv, so a character "
        f"holding one would show nothing and no warning: {missing}. Add a row "
        "to the Weapon Meshes sheet of docs/All_Things_Cataclysm.xlsx, writing "
        "'None' in the Mesh column for a weapon meant to be invisible.")

    extra = sorted(meshes - bases)
    assert not extra, (
        "these rows in WeaponMeshes.csv name no weapon base in ItemBases.csv, "
        f"so nothing will ever look them up: {extra}. Both row names are built "
        "by the same row_name('Weapon', name) in tools/generate_datatables.py.")


def test_every_mesh_path_is_content_and_every_scale_is_positive():
    """A path outside /Game/ cannot load, and a scale of zero cannot be seen."""
    for row in rows(WEAPON_MESHES):
        mesh = row["Mesh"]
        if mesh:
            assert mesh.startswith("/Game/"), (
                f"{row['Name']} names the mesh {mesh!r}, which is not under "
                "/Game/ and so is not in the project's content at all.")

        scale = float(row["Scale"])
        assert scale > 0.0, (
            f"{row['Name']} has a scale of {scale}. A scale of zero or less is "
            "a weapon nobody can see, which is what an empty Mesh is for.")


def test_the_bases_that_draw_nothing_are_the_expected_three():
    """Drawing nothing is deliberate for three bases and a mistake anywhere else.

    PINNED SO THAT A FOURTH CANNOT APPEAR QUIETLY. If a weapons pack is bought
    that covers the Wand or the Whip, this test is what says so.
    """
    empty = {r["Name"] for r in rows(WEAPON_MESHES) if not r["Mesh"]}

    assert empty == set(DRAWS_NOTHING), (
        f"the bases drawing nothing are {sorted(empty)}, expected "
        f"{sorted(DRAWS_NOTHING)}.\n"
        + "\n".join(f"  {name}: {why}" for name, why in DRAWS_NOTHING.items())
        + "\nIf a base gained a mesh, take it out of DRAWS_NOTHING here. If one "
          "lost its mesh, say why in this table rather than only in the data.")


def test_the_meshes_named_exist_when_the_weapons_pack_is_installed():
    """The paths are real, checked against disk rather than trusted.

    SKIPPED RATHER THAN FAILED WITHOUT THE PACK. The weapons pack is
    third-party content and `.gitignore` excludes it, exactly as it excludes the
    Paragon packs, so a fresh checkout and continuous integration have none of
    it. A skip here means this check did not run, not that it passed.
    """
    named = [r for r in rows(WEAPON_MESHES) if r["Mesh"]]
    assert named, "no row names a mesh at all, so this check has nothing to do."

    present = [r for r in named
               if (CONTENT / (r["Mesh"][len("/Game/"):] + ".uasset")).is_file()]
    if not present:
        pytest.skip(
            "the weapons pack is not on this machine, so no mesh path could be "
            "checked against disk. Every path's SHAPE is checked by "
            "test_every_mesh_path_is_content_and_every_scale_is_positive; that "
            "the assets exist is not.")

    missing = []
    for row in named:
        on_disk = CONTENT / (row["Mesh"][len("/Game/"):] + ".uasset")
        if not on_disk.is_file():
            missing.append(f"  {row['Name']} -> {row['Mesh']}")

    assert not missing, (
        "the weapons pack is installed but these meshes are not in it:\n"
        + "\n".join(missing)
        + "\n\nThese load by path at run time and a missing one is only a "
          "warning in the log, so nothing else would notice.")


# --------------------------------------------------------------------------
# The character draws it, and stops drawing it
# --------------------------------------------------------------------------


def test_an_equipment_change_redraws_what_is_in_the_hands():
    """The swap hangs on the moment that already exists for equipment changes."""
    text = read(PLAYER_CPP)

    changed = function_body(
        text, "void ACataclysmPlayerCharacter::OnEquipmentChanged")
    assert "RefreshWeaponMeshes" in changed, (
        "OnEquipmentChanged does not call RefreshWeaponMeshes, so equipping a "
        "weapon changes the character's stats and its six skills and leaves its "
        "hands exactly as they were. That is what issue #1125 set out to fix.")


def test_a_hand_holding_nothing_is_cleared():
    """The half that would break silently.

    A `RefreshWeaponMeshes` that only assigned would pass every other check in
    this file. Equipping would work; unequipping would leave the last weapon in
    the character's hand for ever.
    """
    body = function_body(
        read(PLAYER_CPP), "void ACataclysmPlayerCharacter::RefreshWeaponMeshes")

    assert "SetStaticMesh(nullptr)" in body, (
        "RefreshWeaponMeshes never clears a hand. Taking a weapon off has to "
        "remove its mesh, or the character goes on holding a weapon it no "
        "longer has while every number says otherwise.")


def test_the_hand_sockets_are_the_ones_the_skeleton_ships():
    """Named once, in C++, and checked here because a typo is silent.

    `USceneComponent::SetupAttachment` accepts any socket name. A misspelled one
    does not fail: the component attaches to the mesh's origin instead, so the
    weapon appears at the character's feet rather than in its hand.
    """
    text = read(LOOKUP_CPP)

    for socket in ("HandGrip_R", "HandGrip_L"):
        assert socket in text, (
            f"CataclysmWeaponMeshes.cpp no longer names the {socket} socket. "
            f"SK_Mannequin ships HandGrip_R on the hand_r bone and HandGrip_L "
            f"on hand_l; those are the two the weapon components attach to.")


def test_the_asset_record_exists_and_names_the_pack():
    """The durable record, in the same shape as the other source-asset files."""
    text = read(ASSET_RECORD)

    for expected in ("Medieval_Weapons", "HandGrip_R", "Scale"):
        assert expected in text, (
            f"{ASSET_RECORD.relative_to(REPO_ROOT).as_posix()} does not "
            f"mention {expected}. It is the only record of which mesh each "
            f"weapon base draws and what was measured to choose it.")
