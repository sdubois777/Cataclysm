"""Build L_Capital, the capital's level: the empire map's home. Issue #2733 slice 2.

    python tools/run_editor_python.py tools/generate_capital_map.py

This does not run in the system Python. It runs inside the editor's interpreter,
through the `pythonscript` commandlet, which is what `run_editor_python.py` is
for and what checks that it actually ran.

THE OWNER'S RULING OF 2026-11-17, on #2733: "The empire map's home is a plain
capital level, `L_Capital`. It is a new, nearly empty level that opens the
empire map as soon as the player arrives." Its game mode,
`ACataclysmCapitalGameMode`, loads the character the select screen chose and
opens the map; this level only has to give that character somewhere to stand.

WHAT THE LEVEL HOLDS: a floor, a light, a sky and a place for the player to be
created -- the sandbox's level (`make_sandbox_level` in
`tools/generate_input_assets.py`) without its navigation bounds, because nothing
here walks by navigation: the capital spawns no creatures. The capital's real
content is issue #48 and can be built into this level later.

WHY A FLOOR, when the map covers the screen: the player's character is made here
and loaded into, and a character with nothing under it falls until the world
destroys it. The floor's top is at z=0, where the player start puts it down.

NO _BuiltData. Both lights are movable, so the editor never asks for a lighting
build and never writes `L_Capital_BuiltData.uasset`, which
`tools/tests/test_map_built_data_is_not_committed.py` forbids.

ITS OWN SCRIPT, NOT A FUNCTION OF `generate_interface_assets.py`, which made
`L_FrontEnd`: that script also saves every interface asset it owns, and a
window whose editor run must add this one level and nothing else is easier to
check when the run touches nothing else.

RE-RUNNING EMPTIES WHAT IT MADE AND MAKES IT AGAIN, by label, as the sandbox's
and the dungeon's generators do, so nothing added by hand is destroyed and
nothing is duplicated.
"""

import unreal

MAPS_DIR = "/Game/Maps"
CAPITAL_LEVEL = "L_Capital"

GAME_MODE_CLASS = "/Script/Cataclysm.CataclysmCapitalGameMode"

# The sandbox's floor: 40 metres square and a metre thick. Nothing walks far here.
FLOOR_EXTENT = 4000.0     # centimetres across
FLOOR_THICKNESS = 100.0

# Every actor this script puts in the level, by label.
CAPITAL_ACTOR_LABELS = ["Floor", "Sun", "SkyAtmosphere", "SkyLight", "PlayerStart"]

log = unreal.log
editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def fail(message):
    """Stop with a message the caller will actually see in the log."""
    unreal.log_error(message)
    raise SystemExit(1)


def make_capital_level():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # AN EXISTING LEVEL IS OPENED AND EMPTIED rather than deleted and remade, for
    # the reason the sandbox's generator records: delete_asset reports success on
    # a level and leaves the .umap on disk, after which new_level refuses.
    path = "{}/{}".format(MAPS_DIR, CAPITAL_LEVEL)
    if editor_assets.does_asset_exist(path):
        if not level_subsystem.load_level(path):
            fail("Could not open the existing level at {}".format(path))

        managed = set(CAPITAL_ACTOR_LABELS)
        for actor in actor_subsystem.get_all_level_actors():
            if actor.get_actor_label() in managed:
                actor_subsystem.destroy_actor(actor)
    elif not level_subsystem.new_level(path):
        fail("Could not create the level at {}".format(path))

    cube = editor_assets.load_asset("/Engine/BasicShapes/Cube.Cube")
    if cube is None:
        fail("Could not load /Engine/BasicShapes/Cube")

    # The floor, sunk by half its thickness so its top surface is exactly z=0.
    floor = actor_subsystem.spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(0.0, 0.0, -FLOOR_THICKNESS / 2.0))
    floor.set_actor_label("Floor")
    floor.static_mesh_component.set_static_mesh(cube)
    floor.set_actor_scale3d(unreal.Vector(FLOOR_EXTENT / 100.0,
                                          FLOOR_EXTENT / 100.0,
                                          FLOOR_THICKNESS / 100.0))
    floor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)

    # Movable, so no lighting build is ever required. See the module docstring.
    sun = actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0),
        unreal.Rotator(0.0, -50.0, 0.0))
    sun.set_actor_label("Sun")
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if sun_component is None:
        fail("The directional light has no DirectionalLightComponent")
    sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)

    sky = actor_subsystem.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    sky.set_actor_label("SkyAtmosphere")

    sky_light = actor_subsystem.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 500.0))
    sky_light.set_actor_label("SkyLight")
    # Reached by class rather than by an attribute name: ASkyLight exposes its
    # component to C++ but not to the editor scripting layer.
    sky_light_component = sky_light.get_component_by_class(unreal.SkyLightComponent)
    if sky_light_component is None:
        fail("The sky light has no SkyLightComponent")
    sky_light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_light_component.set_editor_property("real_time_capture", True)

    start = actor_subsystem.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0))
    start.set_actor_label("PlayerStart")

    # THE LEVEL NAMES ITS OWN GAME MODE, rather than the project default naming
    # it, so opening L_Sandbox still gives the sandbox and this level the capital.
    game_mode = unreal.load_class(None, GAME_MODE_CLASS)
    if game_mode is None:
        fail("Could not load {}. The C++ has to be compiled before this runs."
             .format(GAME_MODE_CLASS))

    world = unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(
        world, unreal.WorldSettings)
    if not settings:
        fail("The level has no WorldSettings actor to set a game mode on")
    settings[0].set_editor_property("default_game_mode", game_mode)

    if not level_subsystem.save_current_level():
        fail("Could not save the level at {}".format(path))

    return path


def main():
    path = make_capital_level()
    log("created {} with the game mode {}".format(path, GAME_MODE_CLASS))
    log("done")


main()
