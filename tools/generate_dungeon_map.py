"""Build L_Dungeon, the level a generated dungeon floor is played in.

    python tools/run_editor_python.py tools/generate_dungeon_map.py

This does not run in the system Python. It runs inside the editor's interpreter,
through the `pythonscript` commandlet, which is what `run_editor_python.py` is
for and what checks that it actually ran.

WHAT THE LEVEL HOLDS, AND WHAT IT DELIBERATELY DOES NOT. A light, a sky, a place
for the player to be created, and navigation bounds. **No floor.**
`ACataclysmDungeonGameMode` builds one when play begins, out of blocks, from a
seed. That is the whole point: the floor does not exist until the game runs, so
there is nothing to author into the map and nothing to bake against.

WHY THE NAVIGATION BOUNDS ARE HERE RATHER THAN SPAWNED WITH THE FLOOR. A
navigation mesh is only built inside a bounds volume, a bounds volume takes its
size from a brush, and brushes are made by `UCubeBuilder`, which is editor-only.
A runtime game mode cannot build one. So the volume is placed once, large enough
for any floor, and `RuntimeGeneration=Dynamic` in `game/Config/DefaultEngine.ini`
fills it in at run time from whatever geometry is actually there.

WHY THERE IS NO CHECK THAT A POINT IS ON THE NAVIGATION MESH, unlike the sandbox
level's generator which checks exactly that. There is no geometry in this level,
so its navigation mesh is legitimately empty when saved and stays that way until
the game runs and builds a floor. What IS checked is that the bounds volume built
to the size asked for, because a volume whose brush failed reports a zero extent,
covers nothing, and produces an empty navigation mesh with no error anywhere --
and here that failure would look exactly like the empty mesh this level expects.
"""

import unreal

MAPS_DIR = "/Game/Maps"
DUNGEON_LEVEL = "L_Dungeon"

# --- how big the level has to be ---------------------------------------------
#
# A floor is DefaultWidth by DefaultHeight cells of CellSizeCm, all three of them
# constants in game/Source/Cataclysm/Dungeon/CataclysmFloorGenerator.h. 40 cells
# of 4 metres is 160 metres across.
#
# THESE NUMBERS ARE A COPY AND THAT IS A RISK, so it is guarded:
# tools/tests/test_the_dungeon_map_covers_a_whole_floor.py reads the C++
# constants and fails if the bounds below stop covering a floor. Without it,
# raising the floor size would silently leave the outer ring of every dungeon
# without a navigation mesh, and nothing reports that -- enemies simply stop
# pathing out there.
FLOOR_CELLS_ACROSS = 40
CELL_SIZE_CM = 400.0
FLOOR_WIDTH_CM = FLOOR_CELLS_ACROSS * CELL_SIZE_CM

# Wider than the floor, so the navigation mesh reaches past the outermost wall
# rather than being clipped level with it.
NAV_MARGIN_CM = 800.0
NAV_WIDTH_CM = FLOOR_WIDTH_CM + NAV_MARGIN_CM

# Tall enough for the ground blocks below the walking surface and the wall blocks
# above it, which are 40 cm and 400 cm in CataclysmDungeonFloor.h, with headroom.
NAV_HEIGHT_CM = 1600.0

# Every actor this script puts in the level, by label. Re-running removes exactly
# these and makes them again, so nothing added by hand in the editor is destroyed
# and nothing is duplicated.
DUNGEON_ACTOR_LABELS = ["Sun", "SkyAtmosphere", "SkyLight", "PlayerStart",
                        "NavMeshBounds"]

GAME_MODE_CLASS = "/Script/Cataclysm.CataclysmDungeonGameMode"

log = unreal.log
editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def fail(message):
    """Stop with a message the caller will actually see in the log."""
    unreal.log_error(message)
    raise SystemExit(1)


def make_dungeon_level():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # An existing level is opened and emptied rather than deleted and remade, for
    # the reason the sandbox's generator records: delete_asset reports success on
    # a level and leaves the .umap on disk, after which new_level refuses.
    path = "{}/{}".format(MAPS_DIR, DUNGEON_LEVEL)
    if editor_assets.does_asset_exist(path):
        if not level_subsystem.load_level(path):
            fail("Could not open the existing level at {}".format(path))

        managed = set(DUNGEON_ACTOR_LABELS)
        for actor in actor_subsystem.get_all_level_actors():
            if actor.get_actor_label() in managed:
                actor_subsystem.destroy_actor(actor)
    elif not level_subsystem.new_level(path):
        fail("Could not create the level at {}".format(path))

    sun = actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 2000.0),
        unreal.Rotator(0.0, -50.0, 0.0))
    sun.set_actor_label("Sun")

    # Movable, so no lighting build is ever required. A stationary light is what
    # makes the editor demand one and then write a _BuiltData asset beside the
    # map, which tools/tests/test_map_built_data_is_not_committed.py forbids.
    # Nothing here could be baked anyway: the floor does not exist until run time.
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if sun_component is None:
        fail("The directional light has no DirectionalLightComponent")
    sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)

    sky = actor_subsystem.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    sky.set_actor_label("SkyAtmosphere")

    sky_light = actor_subsystem.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0))
    sky_light.set_actor_label("SkyLight")

    # Reached by class rather than by an attribute name: ASkyLight exposes its
    # component to C++ but not to the editor scripting layer.
    sky_light_component = sky_light.get_component_by_class(unreal.SkyLightComponent)
    if sky_light_component is None:
        fail("The sky light has no SkyLightComponent")
    sky_light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_light_component.set_editor_property("real_time_capture", True)

    # WHERE THE PLAYER IS CREATED, NOT WHERE THEY END UP. A pawn has to be made
    # somewhere, and the game mode then moves it to whichever cell the generator
    # chose as the way in, which is different on every floor.
    start = actor_subsystem.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(0.0, 0.0, 200.0))
    start.set_actor_label("PlayerStart")

    world = unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()

    # Built by UCataclysmLevelAuthoring rather than here, because the brush that
    # gives the volume its size needs UCubeBuilder, which the editor scripting
    # layer does not expose. See CataclysmLevelAuthoring.h.
    #
    # Centred half a wall's height up, so the volume holds the ground blocks
    # under the walking surface and the walls over it with room on both sides.
    nav = unreal.CataclysmLevelAuthoring.add_nav_mesh_bounds(
        world,
        unreal.Vector(0.0, 0.0, 200.0),
        unreal.Vector(NAV_WIDTH_CM, NAV_WIDTH_CM, NAV_HEIGHT_CM))
    if nav is None:
        fail("Could not create the navigation bounds volume")
    nav.set_actor_label("NavMeshBounds")

    # Checked, not assumed. See this file's docstring for why the usual
    # is-a-point-on-the-navigation-mesh check cannot be used here.
    extent = unreal.CataclysmLevelAuthoring.get_volume_extent(nav)
    if extent.x * 2.0 < FLOOR_WIDTH_CM or extent.y * 2.0 < FLOOR_WIDTH_CM:
        fail("The navigation bounds volume built to extent {}, which does not "
             "cover a floor {} cm across. Enemies would stop pathing outside it "
             "and nothing would report why.".format(extent, FLOOR_WIDTH_CM))

    # The level names its own game mode, rather than the project default naming
    # it, so opening L_Sandbox still gives the sandbox.
    game_mode = unreal.load_class(None, GAME_MODE_CLASS)
    if game_mode is None:
        fail("Could not load {}. The C++ has to be compiled before this runs."
             .format(GAME_MODE_CLASS))

    settings = unreal.GameplayStatics.get_all_actors_of_class(
        world, unreal.WorldSettings)
    if not settings:
        fail("The level has no WorldSettings actor to set a game mode on")
    settings[0].set_editor_property("default_game_mode", game_mode)

    if not level_subsystem.save_current_level():
        fail("Could not save the level at {}".format(path))

    return path, extent


def main():
    path, extent = make_dungeon_level()
    log("created {} with navigation bounds extent {} for a floor {} cm across"
        .format(path, extent, FLOOR_WIDTH_CM))
    log("done")


main()
