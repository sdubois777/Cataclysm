"""Measure how big the Brute's rock is, in the hand and in the air.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    python tools/run_editor_python.py tools/measure_rock_sizes.py

WHY THIS EXISTS. Issue #453. The project owner reported the thrown rock as
"huge", and every statement made about its size until this script was written
came from reading code rather than from looking at the asset. The two rocks
reach the screen by different routes:

  IN THE HAND.  `ACataclysmBruteCharacter` attaches a `UStaticMeshComponent`
                called CarriedRock to the weapon_r bone and sets the mesh on it.
                **Nothing set a scale until 2026-08-09**, so it drew at whatever
                size the artist authored: measured here at 206.6 cm across,
                against a creature whose whole capsule is 96 cm.

  IN THE AIR.   `ACataclysmProjectile::SetBodyMesh` scales the mesh from its own
                bounds to the projectile's body width, which for a non-piercing
                shot is `DefaultBodyRadiusCm`, currently 40, so 80 cm across.

A comment in `CataclysmBruteCharacter.cpp` said the two "cannot become two
different rocks". That was true of the ASSET and said nothing about the SIZE.
Both now scale through `CataclysmMeshWidth::ScaleFor`, so the report below
should give the same drawn size for each.

RUN IT AGAIN WHENEVER A NEW CREATURE CARRIES SOMETHING. Six more Paragon
characters are coming and the same mistake is available to each of them.

WHAT IT CHANGES. Nothing. It only reads and reports.

WHAT TO COMPARE THE NUMBERS AGAINST. The Brute's own capsule is 48 cm in radius
and 110 cm in half-height, so the creature is about 96 cm wide and 220 cm tall.
The player's capsule is 42 by 96.
"""

import unreal

ROCK = ("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rocks/"
        "SM_Rock_To_Hold.SM_Rock_To_Hold")

CRATER = ("/Game/ParagonRampage/FX/Meshes/Debris/"
          "SM_Rampage_Rock_Rip_Crater.SM_Rampage_Rock_Rip_Crater")

#: What ACataclysmProjectile gives a projectile that does not pierce. Kept here
#: as a plain number rather than read from the header, because this script only
#: reports and a wrong copy would mislead rather than fail. Check it against
#: ACataclysmProjectile::DefaultBodyRadiusCm before trusting the last column.
DEFAULT_BODY_RADIUS_CM = 40.0

#: What the Brute asks the debris burst for when it places the rip crater,
#: from CraterRadiusCm in CataclysmBruteCharacter.h.
CRATER_RADIUS_CM = 50.0

#: What the Brute's capsule is, for scale. From CataclysmBruteCharacter.h.
BRUTE_CAPSULE_RADIUS_CM = 48.0
BRUTE_CAPSULE_HALF_HEIGHT_CM = 110.0


def report(label, path, drawn_radius_cm, drawn_by):
    """Print one mesh's authored size and the size it is actually drawn at.

    @param drawn_radius_cm  the half-width whatever draws it scales it to
    @param drawn_by         what does that scaling, named for the report
    """
    mesh = unreal.load_asset(path)
    if mesh is None:
        unreal.log_warning(
            "{}: not found at {}. This is expected without the Paragon "
            "Rampage pack, which is gitignored.".format(label, path))
        return

    bounds = mesh.get_bounds()
    extent = bounds.box_extent

    # THE HORIZONTAL HALF-EXTENT, which is exactly what SetBodyMesh uses. A
    # bounding sphere would take in the corners of the box and give a different
    # answer, so reading the same figure the engine code reads is the point.
    half_width = max(extent.x, extent.y)

    unreal.log("{}:".format(label))
    unreal.log("    authored half-extent   x {:.1f}  y {:.1f}  z {:.1f} cm"
               .format(extent.x, extent.y, extent.z))
    unreal.log("    authored full size     {:.1f} x {:.1f} x {:.1f} cm"
               .format(extent.x * 2.0, extent.y * 2.0, extent.z * 2.0))

    if half_width <= 0.0:
        unreal.log_warning("    has no width, so nothing could scale it")
        return

    scale = drawn_radius_cm / half_width
    unreal.log("    drawn                  {:.1f} cm across, at scale {:.3f}"
               .format(drawn_radius_cm * 2.0, scale))
    unreal.log("    scaled by              {}".format(drawn_by))
    unreal.log("    unscaled it would be   {:.2f} times that size"
               .format(half_width / drawn_radius_cm))


def main():
    unreal.log("")
    unreal.log("=== How big the Brute's rock is. Issue #453. ===")
    unreal.log("")
    unreal.log("For scale, the Brute's capsule is {:.0f} cm across and "
               "{:.0f} cm tall.".format(BRUTE_CAPSULE_RADIUS_CM * 2.0,
                                        BRUTE_CAPSULE_HALF_HEIGHT_CM * 2.0))
    unreal.log("")

    report("The rock it holds and throws", ROCK,
           DEFAULT_BODY_RADIUS_CM,
           "ACataclysmProjectile::SetBodyMesh in the air, and by "
           "ACataclysmBruteCharacter to the same width in the hand")
    unreal.log("")
    report("The crater it leaves behind", CRATER,
           CRATER_RADIUS_CM,
           "ACataclysmDebrisBurst::Scatter, at CraterRadiusCm")
    unreal.log("")
    unreal.log("=== end ===")


main()
