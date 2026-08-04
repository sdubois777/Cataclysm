"""Generate the Enhanced Input assets and the sandbox level.

Runs inside the Unreal editor's Python interpreter, not the system Python:

    "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
      "$PWD/Cataclysm.uproject" -run=pythonscript \
      -script="../tools/generate_input_assets.py" -unattended -nopause -nosplash

WHY THIS IS GENERATED RATHER THAN CLICKED TOGETHER IN THE EDITOR. The same reason
the data tables and the gameplay tag list are: a .uasset is binary, so a change
made by hand leaves no reviewable record of what changed or why. This script is
the record. Re-running it overwrites every property of every asset it owns, so
an asset edited by hand in the editor loses that edit on the next run.

WHAT THIS SCRIPT CANNOT DO, WHICH MATTERS WHEN COMPARING IT TO THE OTHER TWO
GENERATORS. It has no --check mode. A .uasset carries generated identifiers and
package metadata that differ between runs, so two runs over unchanged input do
not produce identical bytes and a byte comparison would always report a
difference. tools/generate_datatables.py --check works because comma-separated
values are text. The equivalent guarantee here is the automation test
Cataclysm.Input.* suite, which reads the generated assets back and checks their
contents rather than their bytes.

The seven ability slots come from the C++ enum by way of the Slot.* gameplay
tags. Adding an eighth means adding a row to the Tags sheet of the workbook, a
value to ECataclysmAbilitySlot, and an entry to ABILITY_ACTIONS below.
"""

import unreal

# --- where things go ---------------------------------------------------------

INPUT_DIR = "/Game/Input"
ACTIONS_DIR = "/Game/Input/Actions"
MAPS_DIR = "/Game/Maps"

CONFIG_ASSET = "DA_InputConfig"
MOUSE_CONTEXT = "IMC_MouseMovement"
KEYBOARD_CONTEXT = "IMC_KeyboardMovement"
SANDBOX_LEVEL = "L_Sandbox"

# --- what to create ----------------------------------------------------------

BOOLEAN = unreal.InputActionValueType.BOOLEAN
AXIS2D = unreal.InputActionValueType.AXIS2D

# Native actions: the controller binds these to specific functions by name. The
# names must match CataclysmInputActionNames in Input/CataclysmInputConfig.h.
NATIVE_ACTIONS = [
    # (asset name, action name in C++, value type, display name)
    ("IA_Move", "Move", AXIS2D, "Move"),
    ("IA_MoveToCursor", "MoveToCursor", BOOLEAN, "Move To Cursor"),
    ("IA_StandStill", "StandStill", BOOLEAN, "Stand Still"),
]

# Ability actions: the controller binds these to one shared handler that passes
# the slot tag through. No ability is named anywhere.
#
# The basic attack slot is deliberately absent. The design document's combat
# section says basic attacks are handled automatically, so no key fires one.
# That leaves the six skill slots the design document also calls for.
ABILITY_ACTIONS = [
    # (asset name, Slot.* gameplay tag, display name)
    ("IA_SlotHeavy", "Slot.Heavy", "Heavy Ability"),
    ("IA_SlotSpecial", "Slot.Special", "Special Ability"),
    ("IA_SlotSupport", "Slot.Support", "Support Ability"),
    ("IA_SlotAura", "Slot.Aura", "Aura Ability"),
    ("IA_SlotUltimate", "Slot.Ultimate", "Ultimate Ability"),
    ("IA_SlotMovement", "Slot.Movement", "Movement Ability"),
]

# The two control schemes. They are alternatives, not layers: only one is added
# at a time. See the comment in game/Config/DefaultGame.ini for why -- the design
# document puts the Support ability on W and directional movement on WASD, and
# one key cannot be both.
#
# A mapping is (action asset name, key name, [modifier spec]). Modifier specs are
# "negate" and "swizzle", which are what turn four separate keys into one
# two-axis value.
MOUSE_MAPPINGS = [
    # The left mouse button moves and only moves. It does not fire the basic
    # attack; that is automatic. Recorded in docs/DECISIONS.md.
    ("IA_MoveToCursor", "LeftMouseButton", []),
    ("IA_StandStill", "LeftShift", []),
    ("IA_SlotHeavy", "RightMouseButton", []),
    ("IA_SlotSpecial", "Q", []),
    ("IA_SlotSupport", "W", []),
    ("IA_SlotAura", "E", []),
    ("IA_SlotUltimate", "R", []),
    ("IA_SlotMovement", "SpaceBar", []),
    # Directional movement on the gamepad stick only. Putting it on WASD here
    # would collide with the Support ability on W.
    ("IA_Move", "Gamepad_Left2D", []),
]

KEYBOARD_MAPPINGS = [
    ("IA_Move", "W", ["swizzle"]),
    ("IA_Move", "S", ["swizzle", "negate"]),
    ("IA_Move", "A", ["negate"]),
    ("IA_Move", "D", []),
    ("IA_Move", "Gamepad_Left2D", []),
    ("IA_StandStill", "LeftShift", []),
    ("IA_SlotHeavy", "RightMouseButton", []),
    ("IA_SlotSpecial", "Q", []),
    # Support moves off W, which is now a movement key. Everything else stays
    # where the design document put it.
    ("IA_SlotSupport", "One", []),
    ("IA_SlotAura", "E", []),
    ("IA_SlotUltimate", "R", []),
    ("IA_SlotMovement", "SpaceBar", []),
]

# --- the sandbox level -------------------------------------------------------

FLOOR_EXTENT = 4000.0     # centimetres across
FLOOR_THICKNESS = 100.0
NAV_HEIGHT = 500.0

# Every actor this script puts in the sandbox level, by label. Re-running removes
# exactly these and spawns them again, so nothing a person added by hand in the
# editor is destroyed and nothing is duplicated.
SANDBOX_ACTOR_LABELS = [
    "Floor", "Sun", "SkyAtmosphere", "SkyLight", "PlayerStart", "NavMeshBounds",
]

log = unreal.log
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Subsystems rather than EditorAssetLibrary and EditorLevelLibrary: those
# belong to the Editor Scripting Utilities plugin, which the engine now
# reports as deprecated on every call.
editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def fail(message):
    """Stop with a message the caller will actually see in the log."""
    unreal.log_error(message)
    raise SystemExit(1)


def full_path(directory, name):
    return "{}/{}.{}".format(directory, name, name)


def make_key(key_name):
    """An FKey. Python cannot pass the name to the constructor, so it is imported."""
    key = unreal.Key()
    key.import_text(key_name)
    if key.get_editor_property("key_name") != unreal.Name(key_name):
        fail("'{}' is not a key name the engine recognises".format(key_name))
    return key


def make_tag(tag_name):
    """A gameplay tag, checked against the registry rather than assumed."""
    tag = unreal.GameplayTag()
    tag.import_text(tag_name)
    if not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
        fail("'{}' is not a registered gameplay tag. It has to come from the Tags "
             "sheet of docs/All_Things_Cataclysm.xlsx by way of "
             "tools/generate_gameplay_tags.py.".format(tag_name))
    return tag


def get_or_create_asset(directory, name, asset_class, factory):
    """The asset, created if absent and reused if already there.

    REUSED RATHER THAN DELETED AND REBUILT, which is not a detail. The mapping
    contexts and the input config both reference the input actions, so deleting
    an action that a saved context still points at fails, and the run dies on its
    second invocation with nothing but "could not create". Overwriting in place
    also keeps each asset's identity stable across runs, so re-running produces
    the smallest diff it can.

    Every caller sets every property it cares about, so a reused asset carries
    nothing from the previous run except its name.
    """
    path = "{}/{}".format(directory, name)

    if editor_assets.does_asset_exist(path):
        existing = editor_assets.load_asset(path)
        if existing is not None and existing.get_class() == asset_class.static_class():
            return existing

        # A different class under the same name. That only happens if the
        # generator changed, so replacing it is right, and nothing references it
        # yet under the new class.
        if not editor_assets.delete_asset(path):
            fail("{} exists as the wrong class and could not be deleted".format(path))

    asset = asset_tools.create_asset(name, directory, asset_class, factory)
    if asset is None:
        fail("Could not create {}".format(path))
    return asset


def make_input_action(name, value_type, display_name):
    """One input action, marked rebindable."""
    action = get_or_create_asset(ACTIONS_DIR, name, unreal.InputAction,
                                 unreal.InputAction_Factory())
    action.set_editor_property("value_type", value_type)

    # Player mappable key settings are what let Enhanced Input's user settings
    # rebind this action at runtime. Without them the key is fixed. Issue #16
    # asks for the bindings to be rebindable; the screen that would let a player
    # do it is a separate piece of work, but the data supports it now.
    settings = unreal.new_object(unreal.PlayerMappableKeySettings, outer=action)
    settings.set_editor_property("name", unreal.Name(name))
    settings.set_editor_property("display_name", unreal.Text(display_name))
    action.set_editor_property("player_mappable_key_settings", settings)

    return action


def make_modifiers(specs, outer):
    """Turn modifier names into modifier objects."""
    modifiers = []
    for spec in specs:
        if spec == "negate":
            modifiers.append(unreal.new_object(unreal.InputModifierNegate, outer=outer))
        elif spec == "swizzle":
            swizzle = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=outer)
            # YXZ moves the value a single key produces from the X axis to the Y
            # axis, which is what makes W and S forward and back rather than
            # left and right.
            swizzle.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
            modifiers.append(swizzle)
        else:
            fail("Unknown modifier '{}'".format(spec))
    return modifiers


def make_mapping_context(name, mappings, actions_by_name):
    context = get_or_create_asset(INPUT_DIR, name, unreal.InputMappingContext,
                                  unreal.InputMappingContext_Factory())

    entries = []
    for action_name, key_name, modifier_specs in mappings:
        action = actions_by_name.get(action_name)
        if action is None:
            fail("{} maps {}, which was never created".format(name, action_name))

        entry = unreal.EnhancedActionKeyMapping()
        entry.set_editor_property("action", action)
        entry.set_editor_property("key", make_key(key_name))
        entry.set_editor_property("modifiers", make_modifiers(modifier_specs, context))
        # Take the rebinding settings from the action, so every key in every
        # context is rebindable without repeating the settings per mapping.
        entry.set_editor_property(
            "setting_behavior",
            unreal.PlayerMappableKeySettingBehaviors.INHERIT_SETTINGS_FROM_ACTION)
        entries.append(entry)

    data = unreal.InputMappingContextMappingData()
    data.set_editor_property("mappings", entries)
    context.set_editor_property("default_key_mappings", data)

    return context


def make_input_config(actions_by_name):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CataclysmInputConfig)
    config = get_or_create_asset(INPUT_DIR, CONFIG_ASSET, unreal.CataclysmInputConfig, factory)

    native = []
    for asset_name, action_name, _value_type, _display in NATIVE_ACTIONS:
        entry = unreal.CataclysmNativeInputAction()
        entry.set_editor_property("input_action", actions_by_name[asset_name])
        entry.set_editor_property("name", unreal.Name(action_name))
        native.append(entry)

    ability = []
    for asset_name, tag_name, _display in ABILITY_ACTIONS:
        entry = unreal.CataclysmAbilityInputAction()
        entry.set_editor_property("input_action", actions_by_name[asset_name])
        entry.set_editor_property("slot_tag", make_tag(tag_name))
        ability.append(entry)

    config.set_editor_property("native_input_actions", native)
    config.set_editor_property("ability_input_actions", ability)

    return config


def make_sandbox_level():
    """A flat floor, a light, a sky, a spawn point and navigation bounds.

    Everything here is engine content, so the level costs one .umap through Git
    LFS and nothing else.
    """
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # An existing level is opened and emptied rather than deleted and remade.
    # EditorAssetLibrary.delete_asset reports success on a level and leaves the
    # .umap on disk, after which new_level refuses with "an asset already exists
    # at this location" and the run dies there. Emptying is also what stops a
    # second run adding a second floor and a second sun.
    path = "{}/{}".format(MAPS_DIR, SANDBOX_LEVEL)
    if editor_assets.does_asset_exist(path):
        if not level_subsystem.load_level(path):
            fail("Could not open the existing level at {}".format(path))

        managed = set(SANDBOX_ACTOR_LABELS)
        for actor in actor_subsystem.get_all_level_actors():
            if actor.get_actor_label() in managed:
                actor_subsystem.destroy_actor(actor)
    elif not level_subsystem.new_level(path):
        fail("Could not create the level at {}".format(path))

    cube = editor_assets.load_asset("/Engine/BasicShapes/Cube.Cube")
    if cube is None:
        fail("Could not load /Engine/BasicShapes/Cube")

    # The floor. Sunk by half its thickness so its top surface is exactly z=0,
    # which is where the player start puts the character down.
    floor = actor_subsystem.spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(0.0, 0.0, -FLOOR_THICKNESS / 2.0))
    floor.set_actor_label("Floor")
    floor.static_mesh_component.set_static_mesh(cube)
    floor.set_actor_scale3d(unreal.Vector(FLOOR_EXTENT / 100.0,
                                          FLOOR_EXTENT / 100.0,
                                          FLOOR_THICKNESS / 100.0))
    # Static, because the floor never moves and a static mesh is cheaper to
    # render. Navigation no longer depends on this: the navigation mesh is built
    # when the game starts, from collision geometry, whatever its mobility. See
    # RuntimeGeneration in game/Config/DefaultEngine.ini. Mobility belongs to the
    # component, not the actor.
    floor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)

    sun = actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0),
        unreal.Rotator(0.0, -50.0, 0.0))
    sun.set_actor_label("Sun")

    # Movable, so no lighting build is ever required. ADirectionalLight defaults
    # to Stationary, and a stationary light above the static floor above is
    # exactly what makes the editor demand a lighting build and then write
    # L_Sandbox_BuiltData.uasset next to the map. Nothing here wants baked
    # lighting: the project already renders with Lumen, and generated dungeon
    # floors cannot be baked at all because they do not exist until run time.
    #
    # Reached by class rather than by an attribute name, for the same reason as
    # the sky light below.
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if sun_component is None:
        fail("The directional light has no DirectionalLightComponent")
    sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)

    # Sky atmosphere and a sky light together are what stop everything not facing
    # the sun from being pure black. Without them the placeholder character is a
    # silhouette and it is impossible to tell which way it is facing.
    sky = actor_subsystem.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    sky.set_actor_label("SkyAtmosphere")

    sky_light = actor_subsystem.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 500.0))
    sky_light.set_actor_label("SkyLight")
    # Movable, so it captures the sky without a lighting build. A static sky
    # light in an unbuilt level contributes nothing and everything not facing the
    # sun stays black.
    #
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

    # Navigation bounds. Click-to-move paths through the navigation system, and
    # with no bounds volume there is no navigation mesh, no path is ever found,
    # and clicking does nothing at all with no error anywhere.
    #
    # Built by UCataclysmLevelAuthoring rather than here, because the brush that
    # gives the volume its size needs UCubeBuilder, which the editor scripting
    # layer does not expose. See CataclysmLevelAuthoring.h.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    nav = unreal.CataclysmLevelAuthoring.add_nav_mesh_bounds(
        world,
        unreal.Vector(0.0, 0.0, NAV_HEIGHT / 2.0),
        unreal.Vector(FLOOR_EXTENT, FLOOR_EXTENT, NAV_HEIGHT))
    if nav is None:
        fail("Could not create the navigation bounds volume")
    nav.set_actor_label("NavMeshBounds")

    # Checked, not assumed. A volume whose brush failed to build reports a zero
    # extent, covers nothing, and click-to-move then silently does not work.
    extent = unreal.CataclysmLevelAuthoring.get_volume_extent(nav)
    if extent.x < FLOOR_EXTENT / 4.0 or extent.y < FLOOR_EXTENT / 4.0:
        fail("The navigation bounds volume has extent {}, which is too small to "
             "cover a floor {}cm across. Click-to-move would not work."
             .format(extent, FLOOR_EXTENT))

    # Build the navigation mesh before saving. Creating the bounds volume above
    # only starts an asynchronous build; saving straight afterwards stores a
    # navigation mesh with no data in it, and then click-to-move calls
    # SimpleMoveToLocation, finds no path, and the character turns to face the
    # point clicked and stops. Holding the button still works, because that
    # steers directly and never asks for a path, which is what made this hard to
    # notice. Issue #142.
    #
    # Reached through UCataclysmLevelAuthoring because UNavigationSystemV1::Build
    # is not a UFUNCTION and the editor scripting layer cannot call it.
    if not unreal.CataclysmLevelAuthoring.build_navigation(world):
        fail("The navigation build did not run. Without it the level is saved "
             "with an empty navigation mesh and click-to-move does nothing.")

    # Checked, not assumed, for the same reason as the extent above. This asks
    # the question that actually breaks: can a point be placed on the navigation
    # mesh? The failure being guarded against reports "start point not on
    # navmesh", so anything weaker than this would not have caught it.
    if not unreal.CataclysmLevelAuthoring.is_point_on_nav_mesh(
            world, unreal.Vector(0.0, 0.0, 0.0), 200.0):
        fail("The navigation mesh has no data at the middle of the floor, so "
             "click-to-move will find no path. The navigation build ran but "
             "produced nothing.")

    if not level_subsystem.save_current_level():
        fail("Could not save the level at {}".format(path))

    return path, extent


def main():
    actions_by_name = {}

    for name, _action_name, value_type, display in NATIVE_ACTIONS:
        actions_by_name[name] = make_input_action(name, value_type, display)
        log("created {}".format(full_path(ACTIONS_DIR, name)))

    for name, _tag, display in ABILITY_ACTIONS:
        actions_by_name[name] = make_input_action(name, BOOLEAN, display)
        log("created {}".format(full_path(ACTIONS_DIR, name)))

    make_mapping_context(MOUSE_CONTEXT, MOUSE_MAPPINGS, actions_by_name)
    log("created {} with {} mappings".format(MOUSE_CONTEXT, len(MOUSE_MAPPINGS)))

    make_mapping_context(KEYBOARD_CONTEXT, KEYBOARD_MAPPINGS, actions_by_name)
    log("created {} with {} mappings".format(KEYBOARD_CONTEXT, len(KEYBOARD_MAPPINGS)))

    make_input_config(actions_by_name)
    log("created {} with {} native and {} ability actions".format(
        CONFIG_ASSET, len(NATIVE_ACTIONS), len(ABILITY_ACTIONS)))

    editor_assets.save_directory(INPUT_DIR, recursive=True)

    level_path, nav_extent = make_sandbox_level()
    log("created {} with navigation bounds extent {}".format(level_path, nav_extent))

    log("done")


main()
