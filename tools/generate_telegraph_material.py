"""Build the material an enemy attack's warning shape is drawn with.

    rm game/Content/Effects/M_TelegraphMarker.uasset
    python tools/run_editor_python.py tools/generate_telegraph_material.py

**Remove the asset first.** This only builds from nothing, and refuses to run
over an existing one. `build()` explains why, and why the two obvious ways of
avoiding that both fail.

This runs inside the editor's interpreter, not the system Python. See
`tools/run_editor_python.py` for why, and for what it does when the editor
cannot start.

WHY THIS EXISTS. Issue #539. `ACataclysmTelegraphMarker` drew its shapes with no
material at all, so they took the engine default, which is lit. A lit warning
gets darker exactly when the world does, and the design commits to a
deliberately dark world in which the player has to see and dodge these shapes.

WHY IT IS UNLIT. Section XIII of `docs/Cataclysm_GDD_v2.md` states the measured
contrast of the telegraph against the extreme of each of the eight Cataclysm
themes -- from 3.22:1 against War's steel grey to 19.27:1 against a Chaos white
flash. **Those figures only hold if the shape's brightness comes from its own
material rather than from the room's lighting.** Unlit is what makes a stated
contrast a fact about the game instead of a fact about a swatch.

WHY IT IS GENERATED RATHER THAN HAND-AUTHORED. The same reason the DataTable and
Input assets are: a `.uasset` is a binary blob in Git LFS that nobody can review
in a diff. This file is the material's definition in a form that can be read,
reviewed and regenerated.

WHAT IT MAKES. One material with two parameters: `Colour`, a vector wired to
Emissive Colour, and `Opacity`, a scalar wired to Opacity. No colour is baked in
here -- `ACataclysmTelegraphMarker` builds a dynamic instance per band and sets
both, so every band of a marker shares this one material.

WHY IT IS TRANSLUCENT WHEN THREE OF ITS FOUR BANDS ARE FULLY OPAQUE. The project
owner asked on 2026-08-13 for the marker to stop reading as a solid plate. Only
the innermost band is see-through; the three rings around it stay at opacity 1
and are what carry the readability. A translucent material at opacity 1 draws the
same result as an opaque one, so one material serves both rather than two assets
that could drift apart.

The cost is that every band goes through the translucent pass, which does not
write depth. They are concentric and sit at different heights, so they sort
against each other correctly.
"""

from __future__ import annotations

import sys

import unreal

#: Where the material lands. `/Game/` is `game/Content/`.
PACKAGE_PATH = "/Game/Effects"
ASSET_NAME = "M_TelegraphMarker"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"

#: The name `ACataclysmTelegraphMarker` sets on its dynamic instances. Changing
#: it here without changing it there leaves every marker at the default below,
#: which looks correct for the fill and wrong for the outline.
PARAMETER_NAME = "Colour"

#: The scalar the marker sets per band. The three rings use 1; the innermost
#: band uses the designed fill opacity.
OPACITY_PARAMETER_NAME = "Opacity"

#: The designed ring colour, #FF3020, as linear. Section XIII of the design
#: document. Stated as sRGB there because that is what a colour picker shows;
#: converted here because a material parameter is linear.
DEFAULT_COLOUR_SRGB = (0xFF, 0x30, 0x20)

#: What the asset looks like on its own, opened in the editor. The marker
#: overrides it per band.
DEFAULT_OPACITY = 1.0


def srgb_to_linear(channel: int) -> float:
    """One 0-255 sRGB channel as linear 0-1, the same curve the engine uses."""
    c = channel / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def build() -> None:
    # IT REFUSES TO RUN OVER AN EXISTING ASSET, and the two obvious
    # alternatives were both tried and both fail:
    #
    #   delete_asset then create -- delete_asset reports success, leaves the
    #     file on disk, and the create that follows fails on a name that is
    #     still taken. It reported "Could not create".
    #
    #   load_asset then delete_all_material_expressions -- CRASHES THE EDITOR.
    #     It faults inside UnrealEditor-MaterialEditor.dll, which has no window
    #     open in a commandlet. The run ends with no success and no failure,
    #     which is the silent case tools/run_editor_python.py exists to catch.
    #
    # So the file is removed from the shell first and this only ever builds from
    # nothing. That is what a generator should do anyway: the run is the whole
    # definition rather than an edit of an unknown starting point.
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        unreal.log_error(
            f"{ASSET_PATH} already exists. This script only builds from "
            "nothing. Remove the file and run it again:\n"
            f"    rm game/Content/Effects/{ASSET_NAME}.uasset\n"
            "Do not add a delete or an in-place edit here -- both were tried "
            "and both fail, one silently and one by crashing the editor. See "
            "the comment above this message."
        )
        raise SystemExit(1)

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name=ASSET_NAME,
        package_path=PACKAGE_PATH,
        asset_class=unreal.Material,
        factory=unreal.MaterialFactoryNew(),
    )
    if material is None:
        unreal.log_error(f"Could not create {ASSET_PATH}.")
        raise SystemExit(1)

    # UNLIT IS THE WHOLE POINT OF THIS ASSET. Everything else here is
    # bookkeeping around it.
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    # TRANSLUCENT, SO THE INNERMOST BAND CAN BE SEEN THROUGH. The three rings
    # around it stay at opacity 1, where a translucent material draws the same
    # result an opaque one would.
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    colour = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -350, 0
    )
    colour.set_editor_property("parameter_name", PARAMETER_NAME)
    colour.set_editor_property("default_value", unreal.LinearColor(
        srgb_to_linear(DEFAULT_COLOUR_SRGB[0]),
        srgb_to_linear(DEFAULT_COLOUR_SRGB[1]),
        srgb_to_linear(DEFAULT_COLOUR_SRGB[2]),
        1.0,
    ))

    unreal.MaterialEditingLibrary.connect_material_property(
        colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -350, 200
    )
    opacity.set_editor_property("parameter_name", OPACITY_PARAMETER_NAME)
    opacity.set_editor_property("default_value", DEFAULT_OPACITY)

    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    )

    unreal.MaterialEditingLibrary.recompile_material(material)

    if not unreal.EditorAssetLibrary.save_asset(ASSET_PATH):
        unreal.log_error(f"Could not save {ASSET_PATH}.")
        raise SystemExit(1)

    unreal.log(f"Built {ASSET_PATH}: unlit, translucent, a vector parameter "
               f"named {PARAMETER_NAME!r} wired to emissive colour and a scalar "
               f"named {OPACITY_PARAMETER_NAME!r} wired to opacity.")


if __name__ == "__main__":
    try:
        build()
    except SystemExit:
        raise
    except Exception as error:  # noqa: BLE001 -- report and fail, do not hide
        unreal.log_error(f"Building {ASSET_PATH} raised: {error}")
        sys.exit(1)
