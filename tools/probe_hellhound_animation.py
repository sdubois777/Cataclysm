"""Measure the Hellhound's clip lengths, and read what its mesh is made of.

    python tools/run_editor_python.py tools/probe_hellhound_animation.py

WHY THIS EXISTS. Issue #39 builds the seven Demonic enemies and the Hellhound is
next. `ACataclysmAbyssalWardenCharacter` shows what a creature needs before it
can be written: the length of every clip it plays, so that a wind-up, an attack
interval and a play rate are derived from measured figures rather than guessed.
`game/docs/enemy-source-assets.md` records six Hellhound clips and no idle, no
locomotion and no death, which is not enough to dress one.

WHAT IT CHANGES. Nothing. It loads assets and prints what they say.

THE SECOND THING IT ASKS IS NOT A LENGTH. The Hellhound is played by
`IggyScorch` from the Paragon Iggy and Scorch pack, and that pack holds exactly
one skeletal mesh and one skeleton for what is really two creatures: Iggy is a
goblin who rides Scorch. Every other creature in this project is played by a mesh
that is one animal. So this also prints the mesh's material slots and sections,
because hiding the rider -- if that is what the project owner wants -- is done by
section, and nothing can be decided about it until somebody knows how many there
are and what they are called.
"""

import unreal

#: The pack's one skeletal mesh, and the folder its animations live in.
MESH = ("/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch"
        "/Meshes/IggyScorch.IggyScorch")
ANIMATIONS = "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations"

#: What the creature needs to stand, walk, swing and die, which is the set
#: `ACataclysmAbyssalWardenCharacter` resolves for the Abyssal Warden.
#:
#: MEASURED AS A WHOLE LIST RATHER THAN ONE AT A TIME, so that a wrong folder is
#: visible as a wrong folder: one missing clip is a typo, ten missing clips is
#: the wrong path.
DRESSING = [
    "IggyScorch_Idle",
    "IggyScorch_Idle_Relaxed",
    "Idle_Pose",
    "Jog_Fwd",
    "Jog_Bwd",
    "Jog_Left",
    "Jog_Right",
    "Death_Front",
    "Death_Back",
    "IggyScorch_HitReact_Front",
    "IggyScorch_HitReact_Back",
    "IggyScorch_HitReact_Left",
    "IggyScorch_HitReact_Right",
    "IggyScorch_KnockBack",
    "IggyScorch_KnockBack_Bwd",
]

#: Maul, the basic attack. `game/docs/enemy-source-assets.md` states 0.97 s for
#: the medium one and calls it the fit for the 1.1 second interval.
ATTACKS = [
    "Scorch_Primary_Fire_Fast",
    "Scorch_Primary_Fire_Med",
]

#: Hellrush, the charge that leaves a burning lane. Nothing in the pack is a
#: charge, so the closest motion is the fire breath, which is what the asset
#: notes already suggest. Measured so that a wind-up can be derived from a real
#: number.
BREATH = [
    "R_Ability_FireBreath_Start",
    "R_Ability_FireBreath_Loop",
    "R_Ability_FireBreath_End",
    "R_Ability_Fire",
]

#: What the model designs for this creature, from `ARCHETYPES` in
#: `sim/cataclysm_sim/enemy_stats.py`. Written out rather than imported, because
#: this runs inside the editor's own Python and `sim/` is not on its path.
#: `tools/tests/test_enemy_animation_fits_its_interval.py` holds the two copies
#: together.
ATTACK_INTERVAL_SECONDS = 1.1
MOVE_SPEED_METRES_PER_SECOND = 7.5

#: Hellrush, from `ABILITIES['Hellhound']` in
#: `sim/cataclysm_sim/enemy_abilities.py`. Metres and seconds, as every distance
#: in the design is.
HELLRUSH_RANGE_METRES = 10.0
HELLRUSH_RADIUS_METRES = 1.5


def say(line):
    unreal.log("PROBE| " + str(line))


def load(name):
    return unreal.load_asset("{0}/{1}.{1}".format(ANIMATIONS, name))


def measure(names):
    """Print each clip's play length. Returns the lengths that were readable."""
    lengths = {}
    for name in names:
        anim = load(name)
        if anim is None:
            say("  {0:34} NOT FOUND".format(name))
            continue
        seconds = float(anim.get_play_length())
        lengths[name] = seconds
        say("  {0:34} {1:>8.4f} s".format(name, seconds))
    return lengths


def describe_the_mesh():
    """What the one mesh is made of, which decides whether the rider can go."""
    mesh = unreal.load_asset(MESH)
    if mesh is None:
        say("  THE MESH WAS NOT FOUND AT " + MESH)
        return

    slots = mesh.get_editor_property("materials")
    say("  material slots: {0}".format(len(slots)))
    for index, slot in enumerate(slots):
        name = slot.get_editor_property("material_slot_name")
        material = slot.get_editor_property("material_interface")
        say("    {0:>2}  {1:28} {2}".format(
            index, str(name), material.get_name() if material else "(none)"))

    # THE SECTIONS ARE WHAT COULD BE HIDDEN, and they are not the same list as
    # the slots: one slot can be used by several sections and a section is what
    # `USkeletalMeshComponent::ShowMaterialSection` switches off.
    try:
        lod = mesh.get_editor_property("lod_info")
        say("  levels of detail: {0}".format(len(lod)))
    except Exception as problem:  # noqa: BLE001 -- the editor's own errors vary
        say("  levels of detail could not be read: {0}".format(problem))

    skeleton = mesh.get_editor_property("skeleton")
    say("  skeleton: {0}".format(skeleton.get_name() if skeleton else "(none)"))


def main():
    say("=== WHAT THE HELLHOUND'S MESH IS MADE OF ===")
    describe_the_mesh()

    say("")
    say("=== STANDING, WALKING, BEING HIT AND DYING ===")
    measure(DRESSING)

    say("")
    say("=== MAUL, THE BASIC ATTACK ===")
    attacks = measure(ATTACKS)
    say("  the designed interval is {0:.2f} s".format(ATTACK_INTERVAL_SECONDS))
    for name, seconds in attacks.items():
        rate = seconds / ATTACK_INTERVAL_SECONDS
        say("  {0:34} play rate {1:.4f} to fit".format(name, max(1.0, rate)))

    say("")
    say("=== HELLRUSH, AND WHAT IT COULD BE PLAYED WITH ===")
    breath = measure(BREATH)

    say("")
    say("  the charge covers {0:.1f} m and the creature walks at {1:.1f} m/s"
        .format(HELLRUSH_RANGE_METRES, MOVE_SPEED_METRES_PER_SECOND))
    for name, seconds in breath.items():
        if seconds <= 0.0:
            continue
        say("  {0:34} would carry it at {1:>7.1f} cm/s over {2:.1f} m".format(
            name, HELLRUSH_RANGE_METRES * 100.0 / seconds, HELLRUSH_RANGE_METRES))

    say("")
    say("=== PROBE FINISHED ===")


main()
