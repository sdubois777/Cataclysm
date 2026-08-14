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
themes. **Those figures only hold if the shape's brightness comes from its own
material rather than from the room's lighting.** Unlit is what makes a stated
contrast a fact about the game instead of a fact about a swatch.

WHY IT IS GENERATED RATHER THAN HAND-AUTHORED. The same reason the DataTable and
Input assets are: a `.uasset` is a binary blob in Git LFS that nobody can review
in a diff. This file is the material's definition in a form that can be read,
reviewed and regenerated.

WHY IT IS TRANSLUCENT WHEN THREE OF ITS FOUR BANDS ARE FULLY OPAQUE. The project
owner asked on 2026-08-13 for the marker to stop reading as a solid plate. Only
the innermost band is see-through; the three rings around it stay at opacity 1
and are what carry the readability. A translucent material at opacity 1 draws the
same result as an opaque one, so one material serves both rather than two assets
that could drift apart.

The cost is that every band goes through the translucent pass, which does not
write depth. They are concentric and sit at different heights, so they sort
against each other correctly.

WHAT IT MAKES. One material with seven parameters. No value is baked in here --
`ACataclysmTelegraphMarker` builds a dynamic instance per band and sets them, so
every band of a marker shares this one material.

  Colour          vector, wired to Emissive Colour
  Opacity         scalar, multiplied by the sweep mask below and wired to Opacity
  SweepOrigin     vector, the point in the mesh's own space the sweep grows from
  SweepScale      vector, one over the distance to the edge along each axis, or
                  zero on an axis the sweep ignores. ALL ZERO MEANS NO SWEEP.
  SweepStartTime  scalar, the world time the wind-up began
  SweepDuration   scalar, how long the wind-up lasts, in seconds
  SweepBand       scalar, how thick the moving band is, as a fraction of the
                  distance from the sweep's origin to the edge

## The sweep, added for issue #544

The design says a telegraph is "a hard-edged geometric shape ... with a fill that
sweeps as the wind-up runs out", and until #544 nothing drew that: the fill was a
plate at full size for the whole wind-up. It both looked wrong and threw away the
one thing the fill is supposed to say, which is how long is left.

The material computes, per pixel:

    Progress = saturate((Time - SweepStartTime) / SweepDuration)
    Where    = length((LocalPosition - SweepOrigin) * SweepScale)
    Revealed = (Where <= Progress) AND (Where >= Progress - SweepBand)
    Opacity  = Opacity * Revealed

`Where` is 0 at the point the sweep starts from and 1 at the edge it finishes on,
so a band of ground is drawn while the sweep's leading edge is passing over it,
and the ground behind the band goes clear again. The band leaves the middle as
the wind-up starts and arrives at the marker's edge as the attack lands.

**A BAND AND NOT A GROWING DISC.** The first version of this filled everything
behind the leading edge, so by the moment of impact the whole marker was tinted.
The project owner looked at that on 2026-08-14 and asked for the interior back:
"We don't need the giant pink circle if we're also showing an expanding one."
A band gives the same reading of how much time is left and hides almost none of
the floor.

**ALL-ZERO `SweepScale` IS WHAT SWITCHES THE SWEEP OFF, and it needs no parameter
of its own.** With every axis zeroed, `Where` is 0 everywhere, which is never
greater than `Progress`, so every pixel is drawn from the first frame. That is
what the three rings use and it is the material's own default, so this asset
opened on its own still looks like what it drew before #544.

**IT READS LOCAL POSITION AND NOT TEXTURE COORDINATES**, which is the whole
reason one material serves both marker shapes. `CataclysmTelegraphMarker.h`
records that the two meshes map their texture coordinates differently -- a
cylinder's cap is radial and a cube's face is not -- so a sweep built on those
would need a branch per shape. Local position behaves the same on both, and it is
taken before the component's scale is applied, so it does not change when a
marker is drawn at one metre or at six.

The caller turns that into the shape it wants by choosing the two vectors:

    a circle   SweepOrigin (0, 0, 0)  SweepScale (1/50, 1/50, 0)
               distance from the middle, reaching 1 at the rim

    a lane     SweepOrigin (-50, 0, 0)  SweepScale (1/100, 0, 0)
               distance along the lane from the caster's end, reaching 1 at the
               point that was aimed at

Both use the engine basic shape's 100 unit size, which is
`ACataclysmCharacterBase::BasicShapeSize` in C++ and is where those figures come
from.

**DIVIDING BY THE DURATION IS SAFE AT ZERO** because it is floored at
`SmallestDuration` first. A duration of zero then makes `Progress` 1 rather than
producing a division by zero, which means fully drawn -- the same answer the
rings get by a different route, and the right answer for a marker with no wind-up
to show.
"""

from __future__ import annotations

import sys

import unreal

#: Where the material lands. `/Game/` is `game/Content/`.
PACKAGE_PATH = "/Game/Effects"
ASSET_NAME = "M_TelegraphMarker"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"

#: The names `ACataclysmTelegraphMarker` sets on its dynamic instances. Changing
#: one here without changing it there leaves every marker at the default below,
#: which looks correct for the fill and wrong for the outline.
PARAMETER_NAME = "Colour"

#: The scalar the marker sets per band. The three rings use 1; the innermost
#: band uses the designed fill opacity.
OPACITY_PARAMETER_NAME = "Opacity"

#: The four the sweep is built from. See the module docstring.
SWEEP_ORIGIN_PARAMETER_NAME = "SweepOrigin"
SWEEP_SCALE_PARAMETER_NAME = "SweepScale"
SWEEP_START_PARAMETER_NAME = "SweepStartTime"
SWEEP_DURATION_PARAMETER_NAME = "SweepDuration"
SWEEP_BAND_PARAMETER_NAME = "SweepBand"

#: The designed ring colour, #FF3020, as linear. Section XIII of the design
#: document. Stated as sRGB there because that is what a colour picker shows;
#: converted here because a material parameter is linear.
DEFAULT_COLOUR_SRGB = (0xFF, 0x30, 0x20)

#: What the asset looks like on its own, opened in the editor. The marker
#: overrides it per band.
DEFAULT_OPACITY = 1.0

#: What `SweepDuration` is floored at before it is divided by, so a marker that
#: states no wind-up draws fully rather than dividing by zero. Small enough that
#: any real wind-up is unaffected: the shortest the design produces is 0.4
#: seconds, four thousand times this.
SMALLEST_DURATION = 0.0001


def srgb_to_linear(channel: int) -> float:
    """One 0-255 sRGB channel as linear 0-1, the same curve the engine uses."""
    c = channel / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def add(material, kind, x: int, y: int):
    """One expression node, positioned so the built graph can be read."""
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, kind, x, y)
    if node is None:
        unreal.log_error(f"Could not create a {kind} node.")
        raise SystemExit(1)
    return node


def wire(source, target, to_input: str = "", from_output: str = "") -> None:
    """Connect one expression's output to another's input, or stop.

    IT CHECKS THE RETURN VALUE, and that is not defensive padding.
    `connect_material_expressions` returns False for a pin name it does not
    recognise rather than raising, so a mistyped input name would leave the node
    unwired, the material would still compile, and the sweep would silently do
    nothing on every marker in the game.
    """
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, from_output, target, to_input):
        unreal.log_error(
            f"Could not connect to the {to_input or 'first'!r} input. The pin "
            "name is wrong, so the node would have been left unwired.")
        raise SystemExit(1)


def scalar(material, name: str, default: float, x: int, y: int):
    node = add(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def vector(material, name: str, default, x: int, y: int):
    node = add(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def rgb_of(material, parameter, x: int, y: int):
    """The first three channels of a vector parameter.

    NEEDED RATHER THAN TIDY. A vector parameter's default output is four
    channels and local position is three, and Unreal refuses to do arithmetic
    between the two rather than dropping the fourth.
    """
    mask = add(material, unreal.MaterialExpressionComponentMask, x, y)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", True)
    mask.set_editor_property("a", False)
    wire(parameter, mask)
    return mask


def build_sweep_mask(material):
    """How much of the shape is drawn yet, as 0 or 1 per pixel.

    Returns the expression to multiply the opacity by. See the module docstring
    for the arithmetic and for why an all-zero SweepScale switches it off.
    """
    # --- how far through the wind-up we are -------------------------------
    now = add(material, unreal.MaterialExpressionTime, -1400, 300)
    started = scalar(material, SWEEP_START_PARAMETER_NAME, 0.0, -1400, 420)

    elapsed = add(material, unreal.MaterialExpressionSubtract, -1150, 340)
    wire(now, elapsed, "A")
    wire(started, elapsed, "B")

    stated = scalar(material, SWEEP_DURATION_PARAMETER_NAME, 0.0, -1400, 540)
    floor_value = add(material, unreal.MaterialExpressionConstant, -1400, 640)
    floor_value.set_editor_property("r", SMALLEST_DURATION)

    duration = add(material, unreal.MaterialExpressionMax, -1150, 560)
    wire(stated, duration, "A")
    wire(floor_value, duration, "B")

    fraction = add(material, unreal.MaterialExpressionDivide, -900, 440)
    wire(elapsed, fraction, "A")
    wire(duration, fraction, "B")

    progress = add(material, unreal.MaterialExpressionSaturate, -700, 440)
    wire(fraction, progress)

    # --- how far out this pixel is ----------------------------------------
    here = add(material, unreal.MaterialExpressionLocalPosition, -1400, 780)
    origin = vector(material, SWEEP_ORIGIN_PARAMETER_NAME,
                    unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1400, 900)
    origin_rgb = rgb_of(material, origin, -1150, 900)

    offset = add(material, unreal.MaterialExpressionSubtract, -900, 820)
    wire(here, offset, "A")
    wire(origin_rgb, offset, "B")

    # ALL ZERO MEANS NO SWEEP, which is the default and what the rings use.
    extent = vector(material, SWEEP_SCALE_PARAMETER_NAME,
                    unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1400, 1020)
    extent_rgb = rgb_of(material, extent, -1150, 1020)

    scaled = add(material, unreal.MaterialExpressionMultiply, -700, 880)
    wire(offset, scaled, "A")
    wire(extent_rgb, scaled, "B")

    distance = add(material, unreal.MaterialExpressionLength, -500, 880)
    wire(scaled, distance)

    # --- drawn yet? --------------------------------------------------------
    # A BAND, NOT EVERYTHING BEHIND THE EDGE. The project owner looked at the
    # filled version on 2026-08-14 and asked for the interior back: "We don't
    # need the giant pink circle if we're also showing an expanding one." So a
    # pixel is drawn only while the sweep's leading edge is passing over it, and
    # the ground it has already crossed goes clear again.
    #
    # Two comparisons, multiplied:
    #
    #     inside  Where <= Progress                  behind the leading edge
    #     after   Where >= Progress - SweepBand      ahead of the trailing one
    #
    # BOTH ARE WRITTEN AS 1 - ceil(saturate(x)) rather than as ceil of the
    # opposite, and that is not a style choice. The two differ exactly where the
    # three rings sit: every term there is 0, and ceil(0) is 0, so the other
    # form would hide all three rings and the marker would have no edge at all.
    beyond = add(material, unreal.MaterialExpressionSubtract, -300, 560)
    wire(distance, beyond, "A")
    wire(progress, beyond, "B")

    past_edge = add(material, unreal.MaterialExpressionSaturate, -150, 560)
    wire(beyond, past_edge)

    not_yet = add(material, unreal.MaterialExpressionCeil, 0, 560)
    wire(past_edge, not_yet)

    inside = add(material, unreal.MaterialExpressionOneMinus, 150, 560)
    wire(not_yet, inside)

    band = scalar(material, SWEEP_BAND_PARAMETER_NAME, 0.0, -700, 1180)
    trailing = add(material, unreal.MaterialExpressionSubtract, -500, 1120)
    wire(progress, trailing, "A")
    wire(band, trailing, "B")

    behind = add(material, unreal.MaterialExpressionSubtract, -300, 1120)
    wire(trailing, behind, "A")
    wire(distance, behind, "B")

    crossed = add(material, unreal.MaterialExpressionSaturate, -150, 1120)
    wire(behind, crossed)

    gone = add(material, unreal.MaterialExpressionCeil, 0, 1120)
    wire(crossed, gone)

    after = add(material, unreal.MaterialExpressionOneMinus, 150, 1120)
    wire(gone, after)

    revealed = add(material, unreal.MaterialExpressionMultiply, 300, 840)
    wire(inside, revealed, "A")
    wire(after, revealed, "B")
    return revealed


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

    colour = vector(material, PARAMETER_NAME, unreal.LinearColor(
        srgb_to_linear(DEFAULT_COLOUR_SRGB[0]),
        srgb_to_linear(DEFAULT_COLOUR_SRGB[1]),
        srgb_to_linear(DEFAULT_COLOUR_SRGB[2]),
        1.0,
    ), -350, 0)

    unreal.MaterialEditingLibrary.connect_material_property(
        colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    opacity = scalar(material, OPACITY_PARAMETER_NAME, DEFAULT_OPACITY, -350, 200)

    # The opacity the band asks for, times how much of the shape has been swept
    # over yet. The rings pass an all-zero SweepScale, which makes the second
    # term 1 everywhere, so this is exactly the opacity they asked for.
    drawn = add(material, unreal.MaterialExpressionMultiply, 400, 200)
    wire(opacity, drawn, "A")
    wire(build_sweep_mask(material), drawn, "B")

    unreal.MaterialEditingLibrary.connect_material_property(
        drawn, "", unreal.MaterialProperty.MP_OPACITY
    )

    unreal.MaterialEditingLibrary.recompile_material(material)

    if not unreal.EditorAssetLibrary.save_asset(ASSET_PATH):
        unreal.log_error(f"Could not save {ASSET_PATH}.")
        raise SystemExit(1)

    unreal.log(f"Built {ASSET_PATH}: unlit, translucent, a vector parameter "
               f"named {PARAMETER_NAME!r} wired to emissive colour, and a "
               f"scalar named {OPACITY_PARAMETER_NAME!r} multiplied by a "
               f"moving-band mask built from {SWEEP_ORIGIN_PARAMETER_NAME!r}, "
               f"{SWEEP_SCALE_PARAMETER_NAME!r}, "
               f"{SWEEP_START_PARAMETER_NAME!r}, "
               f"{SWEEP_DURATION_PARAMETER_NAME!r} and "
               f"{SWEEP_BAND_PARAMETER_NAME!r}, wired to opacity.")


if __name__ == "__main__":
    try:
        build()
    except SystemExit:
        raise
    except Exception as error:  # noqa: BLE001 -- report and fail, do not hide
        unreal.log_error(f"Building {ASSET_PATH} raised: {error}")
        sys.exit(1)
