"""Build the four Niagara Effect Type assets every particle system must set.

    python tools/run_editor_python.py tools/generate_effect_types.py

This runs inside the editor's interpreter, not the system Python. See
`tools/run_editor_python.py` for why, and for what it does when the editor
cannot start.

WHY THIS EXISTS. Issue #555, and step 3 of the build order in
`docs/Niagara_Conventions.md`. An Effect Type is the asset that decides when a
particle system is culled, how often it updates, and how many of it may exist at
once.

**NOTHING IN NIAGARA IS CULLED BY DEFAULT.** A system with no Effect Type set
runs at full cost at any distance, off screen, and in unlimited numbers. The
design's vertical slice puts twenty Brutes on screen attacking at once and the
binding constraint on this machine is an 8 GB graphics card, so these are built
BEFORE the first system rather than retro-fitted to it.

WHY IT IS GENERATED RATHER THAN HAND-AUTHORED. The same reason the DataTable,
Input and telegraph material assets are: a `.uasset` is a binary blob in Git LFS
that nobody can review in a diff. This file is the definition in a form that can
be read, reviewed and regenerated.

IT OVERWRITES EVERY PROPERTY IT OWNS, so an asset edited by hand in the editor
loses that edit on the next run. That matches `tools/generate_input_assets.py`
and is stated in `game/README.md`.

## Every number here was measured on 2026-08-22, and none of them moved

Until then they were guesses and this docstring said so. Issue #547 measured a
dungeon floor at the designed creature density with the player in a fight
against twelve to seventeen creatures. Section 4 of
`docs/Niagara_Conventions.md` holds the figures and the procedure. What the
measurement said about each number:

  **4000 cm, the distance cull.** Never fired. In a fight everything is near the
  player by definition, so this limit is for effects elsewhere on the floor. It
  was the one figure that already had reasoning behind it:
  `CataclysmPlayerCharacter.cpp` sets the camera arm to 800 cm, clamped between
  500 and 1200, at a downward pitch of 60 degrees, so 4000 cm is comfortably
  past the edge of the frame at maximum zoom.

  **60 instances across the effect type.** Never approached. The most that were
  alive at once across every system was 15, and 26 with the per-system cap
  switched off. Kept as headroom, now knowing it is headroom.

  **20 instances of one system.** THE ONLY LIMIT THAT EVER FIRES, and it fires
  hard: 70 instances of NS_Impact_Point were killed in a single fight. Switching
  it off multiplied that system's game-thread cost by 4.6 and its worst frame by
  3.7, which is why it is not being raised. Issue #822 is the work to make the
  effect cheap enough that the cap stops mattering.

  **Visibility and view frustum culling.** Never fired either.

So the values below are unchanged. That is the outcome the measurement gave, not
an outcome anybody wanted in advance -- and it is worth stating plainly, because
"the numbers did not move" reads like nothing was checked.

## THE DOCUMENT'S SETTING NAMES ARE THE EDITOR'S, NOT THE ENGINE'S

This is the trap in this file and it cost a discovery run to find. The
conventions document names cull reactions "Kill and Clear" and "Asleep" and says
"The setting names are exact". They are exact for what a person sees in the
editor. They are not what a script sets:

    "Kill and Clear"  ->  NiagaraCullReaction.DEACTIVATE_IMMEDIATE
    "Asleep"          ->  NiagaraCullReaction.DEACTIVATE_RESUME

The full engine list is DEACTIVATE, DEACTIVATE_IMMEDIATE, DEACTIVATE_RESUME,
DEACTIVATE_IMMEDIATE_RESUME and PAUSE_RESUME. Anything that reads the document
and types the display name gets an AttributeError, which is the good outcome;
anything that guesses at the nearest-looking name gets a different behaviour
silently, which is not.

Two more things the same discovery run settled, both of which would otherwise
have cost a build each:

  The scalability settings list starts EMPTY on a new Effect Type, so an entry
  has to be created rather than edited.

  The `Distance` significance handler is not exposed to Python by name. It is
  loaded by its engine path instead; see SIGNIFICANCE_HANDLER_PATH.
"""

from __future__ import annotations

import sys

import unreal

#: Where the four assets land. `/Game/` is `game/Content/`.
PACKAGE_PATH = "/Game/Effects/EffectTypes"

#: The factory that makes one. Checked before use, because a wrong name here
#: would otherwise fail with an AttributeError that says nothing about what the
#: right name is.
FACTORY_NAME = "NiagaraEffectTypeFactoryNew"

#: The significance handler that culls by distance from the camera.
#:
#: LOADED BY PATH BECAUSE PYTHON DOES NOT EXPOSE IT BY NAME. `dir(unreal)` lists
#: only the base `NiagaraSignificanceHandler`; the Distance and Age subclasses
#: are not bound. Loading the class by its engine path and instancing it works
#: and is what this does.
SIGNIFICANCE_HANDLER_PATH = "/Script/Niagara.NiagaraSignificanceHandlerDistance"


#: What a Niagara Effect Type is when the engine makes one, confirmed by
#: inspecting a fresh object rather than assumed. FXT_MustBeSeen is exactly
#: this, and every other asset is written back to it before its own settings go
#: on, so a run fully defines each asset. See `build_one`.
ENGINE_DEFAULT_UPDATE_FREQUENCY = unreal.NiagaraScalabilityUpdateFrequency.SPAWN_ONLY
ENGINE_DEFAULT_CULL_REACTION = unreal.NiagaraCullReaction.DEACTIVATE_IMMEDIATE


class EffectType:
    """One Effect Type asset and every setting this file gives it.

    A value left as None means the engine's own default, and the default is
    WRITTEN rather than left alone -- see `build_one` for why that distinction
    is the whole reliability of this script.
    """

    def __init__(self, name, purpose, update_frequency=None, cull_reaction=None,
                 by_distance=None, max_instances=None,
                 max_of_one_system=None, cull_when_not_rendered=False,
                 cull_by_view_frustum=False, significance_by_distance=False):
        self.name = name
        self.purpose = purpose
        self.update_frequency = update_frequency
        self.cull_reaction = cull_reaction
        self.by_distance = by_distance
        self.max_instances = max_instances
        self.max_of_one_system = max_of_one_system
        self.cull_when_not_rendered = cull_when_not_rendered
        self.cull_by_view_frustum = cull_by_view_frustum
        self.significance_by_distance = significance_by_distance

    @property
    def has_scalability(self):
        return any(v is not None for v in
                   (self.by_distance, self.max_instances, self.max_of_one_system))


#: See the module docstring for where these numbers come from and what they are
#: worth, which is not much until #547 is answered.
EFFECT_TYPES = [
    EffectType(
        "FXT_Enemy",
        "The twenty-at-once case. Everything an enemy spawns uses this.",
        update_frequency=unreal.NiagaraScalabilityUpdateFrequency.MEDIUM,
        cull_reaction=unreal.NiagaraCullReaction.DEACTIVATE_IMMEDIATE,
        by_distance=4000.0,
        max_instances=60,
        max_of_one_system=20,
        cull_when_not_rendered=True,
        cull_by_view_frustum=True,
        significance_by_distance=True,
    ),
    EffectType(
        "FXT_PlayerSkill",
        "What the player casts. Fewer at once and seen from further away.",
        update_frequency=unreal.NiagaraScalabilityUpdateFrequency.LOW,
        cull_reaction=unreal.NiagaraCullReaction.DEACTIVATE_IMMEDIATE,
        by_distance=6000.0,
        max_instances=40,
        significance_by_distance=True,
    ),
    EffectType(
        "FXT_Ambient",
        "Scenery. It goes to sleep rather than being killed, because the "
        "player walking back has to find it still there.",
        update_frequency=unreal.NiagaraScalabilityUpdateFrequency.LOW,
        # ASLEEP, NOT KILL AND CLEAR. This is the one reaction that differs
        # between the four and the reason is in the purpose above.
        cull_reaction=unreal.NiagaraCullReaction.DEACTIVATE_RESUME,
        by_distance=5000.0,
        cull_when_not_rendered=True,
        significance_by_distance=True,
    ),
    EffectType(
        "FXT_MustBeSeen",
        "The deliberate no-culling escape hatch, left exactly as the engine "
        "makes it. It exists so that 'this must never disappear' is a choice "
        "somebody made rather than the accident of forgetting to set one.",
    ),
]


def fail(message):
    unreal.log_error(message)
    raise SystemExit(1)


def asset_path(name):
    return f"{PACKAGE_PATH}/{name}.{name}"


def make_or_load(name):
    """The asset, created if it is not there and loaded if it is."""
    full = asset_path(name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        existing = unreal.EditorAssetLibrary.load_asset(full)
        if existing is None:
            fail(f"{full} exists and could not be loaded.")
        return existing

    factory_class = getattr(unreal, FACTORY_NAME, None)
    if factory_class is None:
        available = sorted(n for n in dir(unreal)
                           if "Niagara" in n and n.endswith("FactoryNew"))
        fail(f"unreal.{FACTORY_NAME} does not exist. The Niagara factories "
             f"Python exposes are {available}.")

    made = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name=name,
        package_path=PACKAGE_PATH,
        asset_class=unreal.NiagaraEffectType,
        factory=factory_class(),
    )
    if made is None:
        fail(f"Could not create {full}.")
    return made


def apply_scalability(asset, wanted):
    """Write the culling numbers, which live two structs deep.

    THE WRITE-BACK AT THE END IS DEFENSIVE, AND SAYING SO IS THE POINT. An
    earlier version of this comment claimed Unreal's Python returns a struct
    property as a copy, so that changing it without setting it back would
    silently do nothing. **That was asserted and not checked, and the check
    contradicts it**: removing the write-back and rebuilding the assets left
    every setting correct, so on this engine version the value that comes back
    behaves as a view onto the asset rather than as a copy.

    It is kept anyway. Whether a struct property is a view or a copy is not
    documented and is not something to depend on, and setting it back costs one
    call. But it is not load-bearing, so do not point at it as the reason
    anything works.

    AN EFFECT TYPE THAT CULLS NOTHING GETS AN EMPTY LIST, not a skipped call.
    That is FXT_MustBeSeen, and writing the empty list is what stops it keeping
    culling settings from an earlier run of a different version of this file.
    """
    if not (wanted.has_scalability or wanted.cull_when_not_rendered
            or wanted.cull_by_view_frustum):
        holder = asset.get_editor_property("system_scalability_settings")
        holder.set_editor_property("settings", [])
        asset.set_editor_property("system_scalability_settings", holder)
        return

    entry = unreal.NiagaraSystemScalabilitySettings()

    entry.set_editor_property("cull_by_distance", wanted.by_distance is not None)
    if wanted.by_distance is not None:
        entry.set_editor_property("max_distance", wanted.by_distance)

    entry.set_editor_property("cull_max_instance_count",
                              wanted.max_instances is not None)
    if wanted.max_instances is not None:
        entry.set_editor_property("max_instances", wanted.max_instances)

    entry.set_editor_property("cull_per_system_max_instance_count",
                              wanted.max_of_one_system is not None)
    if wanted.max_of_one_system is not None:
        entry.set_editor_property("max_system_instances",
                                  wanted.max_of_one_system)

    visibility = entry.get_editor_property("visibility_culling")
    visibility.set_editor_property("cull_when_not_rendered",
                                   wanted.cull_when_not_rendered)
    visibility.set_editor_property("cull_by_view_frustum",
                                   wanted.cull_by_view_frustum)
    entry.set_editor_property("visibility_culling", visibility)

    # CULLING BY THE FRAME-TIME BUDGET IS OFF, AND IT IS OFF ON PURPOSE.
    #
    # This property was not written at all before 2026-08-22, which meant it
    # held whatever a previous run of a previous version of this file had left
    # -- exactly the silent case `build_one` below spends its docstring warning
    # about. Writing it makes the state a decision.
    #
    # WHY FALSE. `game/Config/DefaultEngine.ini` now sets a budget of 2 ms on
    # each of the three FX thread groups and turns the engine's tracking of it
    # on, so the budget is measurable. Culling by it would also need this, and
    # the measurement says it would fire in real fights today: the worst frame
    # reached 2.37 ms against the 2 ms budget. What happens when it fires is
    # that effects are killed, and issue #822 records that effects already
    # disappear from the instance-count cap, which has not been ruled out as
    # part of what the project owner called "kinda messy". Adding a second
    # thing that kills effects before anyone has looked at the first would make
    # that harder to tell apart rather than easier.
    #
    # Issue #824 is the decision to turn it on, and it is blocked on #822.
    #
    # IT IS ON A NESTED STRUCT AND NOT ON THE ENTRY. `bCullByGlobalBudget` reads
    # like a sibling of `bCullByDistance` and it is not: it lives on
    # `FNiagaraGlobalBudgetScaling`, reached through the entry's `BudgetScaling`
    # member, the same shape as `visibility_culling` above. Setting it on the
    # entry raises "Failed to find property", which is the good outcome and is
    # how this was found.
    budget = entry.get_editor_property("budget_scaling")
    budget.set_editor_property("cull_by_global_budget", False)
    entry.set_editor_property("budget_scaling", budget)

    holder = asset.get_editor_property("system_scalability_settings")
    holder.set_editor_property("settings", [entry])
    asset.set_editor_property("system_scalability_settings", holder)


def build_one(wanted):
    """Write EVERY property this file owns, including the ones left at default.

    NOT "SET WHAT IS WANTED AND LEAVE THE REST", which is what this did first
    and which is a silent trap. This script loads an asset that already exists
    rather than rebuilding it, so a property it stops writing keeps whatever the
    PREVIOUS run put there. Two consequences, and the second is worse:

      An asset edited by hand in the editor keeps that edit for any property
      this file does not write, which contradicts what `game/README.md` says
      these generators do.

      A setting removed from this file does not leave the asset. That made the
      proof of the automation tests worthless: breaking this script so it
      stopped writing the culling settings left the assets correct from the run
      before, the tests passed, and nothing had actually been tested. It was
      found by running that proof, which is the whole reason for running one.

    So every value below is written on every run, defaults included.
    """
    asset = make_or_load(wanted.name)

    asset.set_editor_property(
        "update_frequency",
        wanted.update_frequency if wanted.update_frequency is not None
        else ENGINE_DEFAULT_UPDATE_FREQUENCY)

    asset.set_editor_property(
        "cull_reaction",
        wanted.cull_reaction if wanted.cull_reaction is not None
        else ENGINE_DEFAULT_CULL_REACTION)

    handler = None
    if wanted.significance_by_distance:
        handler_class = unreal.load_class(None, SIGNIFICANCE_HANDLER_PATH)
        if handler_class is None:
            fail(f"Could not load {SIGNIFICANCE_HANDLER_PATH}. Without a "
                 "significance handler nothing decides which instance is the "
                 "least important, so the instance count limits do nothing.")
        handler = unreal.new_object(handler_class, outer=asset)
    # Written even when it is None, so an asset that had one and should not
    # loses it.
    asset.set_editor_property("significance_handler", handler)

    apply_scalability(asset, wanted)

    if not unreal.EditorAssetLibrary.save_asset(asset_path(wanted.name)):
        fail(f"Could not save {asset_path(wanted.name)}.")

    unreal.log(f"built {wanted.name}: {wanted.purpose}")


def build():
    for wanted in EFFECT_TYPES:
        build_one(wanted)
    unreal.log(f"built {len(EFFECT_TYPES)} Niagara effect types in "
               f"{PACKAGE_PATH}")


if __name__ == "__main__":
    try:
        build()
    except SystemExit:
        raise
    except Exception as error:  # noqa: BLE001 -- report and fail, do not hide
        unreal.log_error(f"Building the effect types raised: {error}")
        sys.exit(1)
