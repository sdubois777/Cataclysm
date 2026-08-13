"""Build the material an enemy attack's warning shape is drawn with.

    python tools/run_editor_python.py tools/generate_telegraph_material.py

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

WHAT IT MAKES. One material with one vector parameter, `Colour`, wired to
Emissive Colour. The two colours the design specifies are not baked in here --
`ACataclysmTelegraphMarker` builds a dynamic instance per marker and sets the
parameter, so the fill and the outline share this one material. The default
below is the fill colour, so the asset on its own looks like what it is.
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

#: The designed fill, #00B8C4, as linear. Section XIII of the design document.
#: Stated as sRGB there because that is what a colour picker shows; converted
#: here because a material parameter is linear.
DEFAULT_COLOUR_SRGB = (0x00, 0xB8, 0xC4)


def srgb_to_linear(channel: int) -> float:
    """One 0-255 sRGB channel as linear 0-1, the same curve the engine uses."""
    c = channel / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def build() -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        unreal.log(f"{ASSET_PATH} already exists; deleting it so this run is "
                   "the whole definition rather than an edit of an unknown "
                   "starting point.")
        unreal.EditorAssetLibrary.delete_asset(ASSET_PATH)

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

    # OPAQUE RATHER THAN TRANSLUCENT. A translucent patch lets the floor show
    # through it, which drags the shape's on-screen colour back toward whatever
    # it is lying on -- the exact coupling unlit exists to break. It is also the
    # cheaper of the two to draw, and twenty Imps can be telegraphing at once.
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

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

    unreal.MaterialEditingLibrary.recompile_material(material)

    if not unreal.EditorAssetLibrary.save_asset(ASSET_PATH):
        unreal.log_error(f"Could not save {ASSET_PATH}.")
        raise SystemExit(1)

    unreal.log(f"Built {ASSET_PATH}: unlit, opaque, one vector parameter "
               f"named {PARAMETER_NAME!r} wired to emissive colour.")


if __name__ == "__main__":
    try:
        build()
    except SystemExit:
        raise
    except Exception as error:  # noqa: BLE001 -- report and fail, do not hide
        unreal.log_error(f"Building {ASSET_PATH} raised: {error}")
        sys.exit(1)
