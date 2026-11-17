"""Measure the Imp's clip lengths and its mesh's size inside the editor.

    python tools/run_editor_python.py tools/probe_imp_animation.py

WHY THIS EXISTS. Issue #39 builds the seven Demonic enemies and the Imp is next.
`game/docs/enemy-source-assets.md` records the Imp's ten attack clips and nothing
else: no idle, no locomotion, no death. That is not enough to dress a creature,
because `ACataclysmHellhoundCharacter` and its two predecessors each need the
length of every clip they play so that a play rate is derived from a measured
figure rather than guessed.

WHAT IT CHANGES. Nothing. It loads assets and prints what they say.

THE MESH'S HEIGHT IS THE SECOND QUESTION AND IT IS NOT A LENGTH. The Imp is
played by `Minion_Lane_Melee_Dawn`, the melee lane minion, and
`game/docs/enemy-source-assets.md` records that mesh at 175.9 cm -- "roughly a
person, so playing a small swarming creature with it means scaling it down rather
than using it as authored". The design says the enemy Imp is the same creature as
the player's summoned lesser imp, whose capsule in
`game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp` is 30 cm across the
radius and 45 cm to the half-height -- a creature 90 cm tall. So the mesh has to
be scaled, and the factor is 90 divided by whatever this prints. Reading it here
rather than trusting the 175.9 in the notes means the scale is derived from the
asset in the same run that measures the clips.

THIS MESH IS SHARED WITH THE CORRUPTED SENTINEL'S PACK BUT NOT ITS SKELETON.
`Minion_Lane_Melee_Dawn` and `Minion_Lane_Siege_Dawn` are separate meshes in the
same folder, and `game/docs/enemy-source-assets.md` records that every character
in these packs carries its own skeleton, so nothing here retargets to anything.
The skeleton is printed so that claim can be checked rather than assumed.
"""

import unreal

#: The melee lane minion, which plays the Imp, and where its clips live.
MESH = ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
        "/Meshes/Minion_Lane_Melee_Dawn.Minion_Lane_Melee_Dawn")
ANIMATIONS = ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
              "/Animations/Melee")

#: Rend, the basic attack, which is the creature's only ability.
#: `game/docs/enemy-source-assets.md` states 0.80 s for each `_SetA` and 0.83 s
#: for each `_SetB`, against a designed 0.9 second interval, and says the four
#: unsuffixed attacks at 1.00 s do not fit and should not be used.
#:
#: ALL FOURTEEN ARE MEASURED, including the four that should not be used, because
#: "does not fit" is a claim about a number and the number is what this reads.
ATTACKS = [
    "Attack_A_SetA", "Attack_B_SetA", "Attack_C_SetA", "Attack_D_SetA",
    "Attack_E_SetA",
    "Attack_A_SetB", "Attack_B_SetB", "Attack_C_SetB", "Attack_D_SetB",
    "Attack_E_SetB",
    "Attack_A", "Attack_B", "Attack_C", "Attack_D",
]

#: What the creature needs to stand and walk. The pack ships both a combat and a
#: non-combat set, which no other creature in this project does, so both are
#: measured before either is chosen.
#:
#: THERE IS NO COMBAT IDLE IN THE FOLDER. `NonCombat_Idle` is the only idle, so
#: whether it reads as a creature about to attack is a judgement rather than a
#: choice between two clips.
LOCOMOTION = [
    "NonCombat_Idle",
    "Combat_JogFwd",
    "Combat_JogFwd_Start",
    "Combat_JogFwd_AggroMinion",
    "NonCombat_JogFwd",
    "NonCombat_JogFwd_A",
    "NonCombat_JogFwd_B",
]

#: FIVE DEATHS, WHICH IS MORE THAN ANY OTHER CREATURE IN THE PROJECT HAS. The
#: Brute has one, the Abyssal Warden and the Hellhound have two.
#: `ACataclysmEnemyCharacter::PlayDeathAnimation` draws one of however many
#: entries it is given, so a swarm of ten dying at once has five ways to fall.
DEATHS = ["Death_A", "Death_B", "Death_C", "Death_D", "Death_E"]

#: Being hit and being stunned. Nothing plays these yet on any creature; they are
#: measured so the record is complete and so a later reaction system has figures.
REACTIONS = [
    "HitReact_Front", "HitReact_Back", "HitReact_Left", "HitReact_Right",
    "Stun", "KnockUp", "KnockUp_A",
]

#: What the model designs for this creature, from `ARCHETYPES` in
#: `sim/cataclysm_sim/enemy_stats.py`. Written out rather than imported, because
#: this runs inside the editor's own Python and `sim/` is not on its path.
ATTACK_INTERVAL_SECONDS = 0.9
MOVE_SPEED_METRES_PER_SECOND = 6.5

#: The capsule the design says this creature has, from the lesser imp minion in
#: `game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp`, which the game
#: design document says is the same creature. Centimetres.
DESIGNED_CAPSULE_RADIUS_CM = 30.0
DESIGNED_CAPSULE_HALF_HEIGHT_CM = 45.0


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
            say("  {0:30} NOT FOUND".format(name))
            continue
        seconds = float(anim.get_play_length())
        lengths[name] = seconds
        say("  {0:30} {1:>8.4f} s".format(name, seconds))
    return lengths


def describe_the_mesh():
    """How big it is, and what it is made of."""
    mesh = unreal.load_asset(MESH)
    if mesh is None:
        say("  THE MESH WAS NOT FOUND AT " + MESH)
        return

    bounds = mesh.get_bounds()
    extent = bounds.box_extent
    say("  reference-pose bounds, full width in centimetres:")
    say("    X {0:.1f}   Y {1:.1f}   Z (height) {2:.1f}".format(
        extent.x * 2.0, extent.y * 2.0, extent.z * 2.0))

    height = float(extent.z) * 2.0
    if height > 0.0:
        wanted = DESIGNED_CAPSULE_HALF_HEIGHT_CM * 2.0
        say("")
        say("  the design wants a creature {0:.0f} cm tall, so the mesh has to "
            "be scaled by {1:.4f}".format(wanted, wanted / height))
        say("  and its {0:.0f} cm capsule radius is {1:.4f} of the mesh's own "
            "half-width across Y".format(
                DESIGNED_CAPSULE_RADIUS_CM,
                DESIGNED_CAPSULE_RADIUS_CM / max(float(extent.y), 1.0)))

    slots = mesh.get_editor_property("materials")
    say("")
    say("  material slots: {0}".format(len(slots)))
    for index, slot in enumerate(slots):
        name = slot.get_editor_property("material_slot_name")
        material = slot.get_editor_property("material_interface")
        say("    {0:>2}  {1:28} {2}".format(
            index, str(name), material.get_name() if material else "(none)"))

    skeleton = mesh.get_editor_property("skeleton")
    say("  skeleton: {0}".format(skeleton.get_name() if skeleton else "(none)"))


#: The bones the body's width is read across, each with the chain of parents
#: that puts it in the character's own space. Shoulders and hips, because those
#: are the two widest parts of a body that is not its arms.
#:
#: NOT THE PHYSICS ASSET, WHICH IS HOW THE BRUTE'S WIDTH WAS READ.
#: `game/docs/enemy-source-assets.md` quotes an 82.1 cm sphere on Rampage's
#: `spine_03` as "Epic's own answer to how wide the torso is". The editor's
#: Python on Unreal 5.8 exposes only `get_constraints` on a `PhysicsAsset` and no
#: way at all to reach its bodies, checked on 2026-08-20, so that route is closed
#: from a script and this measures the skeleton instead.
WIDTH_ACROSS = [
    ("upperarm_l", ["clavicle_l", "spine_03", "spine_02", "spine_01",
                    "pelvis", "root"]),
    ("upperarm_r", ["clavicle_r", "spine_03", "spine_02", "spine_01",
                    "pelvis", "root"]),
    ("thigh_l", ["pelvis", "root"]),
    ("thigh_r", ["pelvis", "root"]),
]


def in_character_space(anim, bone, chain, time):
    """Where a bone is relative to the character, rather than to its parent."""
    total = unreal.AnimationLibrary.get_bone_pose_for_time(
        anim, bone, time, False)
    for parent in chain:
        total = total * unreal.AnimationLibrary.get_bone_pose_for_time(
            anim, parent, time, False)
    return total.translation


def describe_the_collision():
    """How wide this creature's body is, shoulder to shoulder and hip to hip.

    WHY IT MATTERS FOR THIS CREATURE AND NOT MUCH FOR THE OTHERS. The design
    fixes the Imp's body radius at 0.30 m and builds the whole pack geometry on
    it: seven fit in the first ring at 0.72 m from the player and thirteen more
    in the second at 1.32 m, which is where the creature's 1.32 m reach comes
    from. If the mesh is far wider than 0.60 m at the size it is worn, ten of
    them standing in those rings pass through each other.

    THE REFERENCE-POSE BOUNDS CANNOT ANSWER THIS. They are 174.2 cm across Y,
    and `game/docs/enemy-source-assets.md` already says why that overstates every
    body in these packs: the reference pose has the arms out, so the figure is an
    arm span.
    """
    anim = load("NonCombat_Idle")
    if anim is None:
        say("  NonCombat_Idle was not found, so nothing can be measured")
        return

    at = {}
    for bone, chain in WIDTH_ACROSS:
        try:
            at[bone] = in_character_space(anim, bone, chain, 0.0)
        except Exception as problem:  # noqa: BLE001 -- the editor's own errors vary
            say("  {0} could not be read: {1}".format(bone, problem))
            return

    for left, right, what in (("upperarm_l", "upperarm_r", "shoulder to shoulder"),
                              ("thigh_l", "thigh_r", "hip to hip")):
        across = abs(float(at[left].y) - float(at[right].y))
        depth = abs(float(at[left].x) - float(at[right].x))
        say("  {0:20} {1:6.1f} cm across Y, {2:5.1f} cm across X".format(
            what, across, depth))

    shoulders = abs(float(at["upperarm_l"].y) - float(at["upperarm_r"].y))
    if shoulders > 0.0:
        wanted = DESIGNED_CAPSULE_RADIUS_CM * 2.0
        say("")
        say("  the design gives it a body {0:.0f} cm across. At the mesh's "
            "authored size its shoulders are {1:.1f} cm apart, so a scale of "
            "{2:.4f} would make the two agree."
            .format(wanted, shoulders, wanted / shoulders))


def name_the_bones():
    """Which bones the walk actually drives.

    WHY THIS IS HERE AND IS NOT IN ANY OTHER PROBE.
    `tools/measure_animation_stride.py` reads the planted foot's speed through
    the bones `ik_foot_l`, `ik_foot_r` and `ik_foot_root`, which are Epic's
    standard rig. Run against this creature on 2026-08-20 it reported **0.0 cm/s
    on every axis for both walks and for the idle** -- and a walk that reads the
    same as an idle is a failed measurement rather than a stationary clip.

    The likely reason is that this skeleton does not have those bones.
    `Minion_Lane_Core_Skeleton` carries 41 bones where Grux carries 96 and
    Rampage 207, and a minion rig that small is unlikely to include the inverse
    kinematics chain. `get_bone_pose_for_time` for a bone that does not exist
    returns an identity transform rather than raising, so the failure is silent
    and reads as zero.

    This prints the names so the next attempt uses a bone that is really there.
    """
    anim = load("Combat_JogFwd")
    if anim is None:
        say("  Combat_JogFwd was not found, so nothing can be listed")
        return

    reader = getattr(unreal.AnimationLibrary, "get_animation_track_names", None)
    if reader is None:
        say("  this engine's AnimationLibrary has no get_animation_track_names, "
            "so the bone list has to come from somewhere else")
        return

    try:
        names = [str(name) for name in reader(anim)]
    except Exception as problem:  # noqa: BLE001 -- the editor's own errors vary
        say("  the track names could not be read: {0}".format(problem))
        return

    say("  Combat_JogFwd drives {0} bones:".format(len(names)))
    for index in range(0, len(names), 4):
        say("    " + "  ".join("{0:22}".format(n)
                               for n in names[index:index + 4]))

    wanted = ["ik_foot_l", "ik_foot_r", "ik_foot_root", "root"]
    say("")
    for bone in wanted:
        say("  {0:16} {1}".format(
            bone, "present" if bone in names else "ABSENT"))


def main():
    say("=== HOW BIG THE IMP'S MESH IS ===")
    describe_the_mesh()

    say("")
    say("=== HOW WIDE EPIC THINKS IT IS ===")
    describe_the_collision()

    say("")
    say("=== WHICH BONES ITS WALK DRIVES ===")
    name_the_bones()

    say("")
    say("=== REND, THE BASIC ATTACK AND THE ONLY ABILITY ===")
    attacks = measure(ATTACKS)
    say("  the designed interval is {0:.2f} s".format(ATTACK_INTERVAL_SECONDS))
    for name in sorted(attacks):
        seconds = attacks[name]
        fits = "fits as authored" if seconds <= ATTACK_INTERVAL_SECONDS \
            else "NEEDS {0:.4f}".format(seconds / ATTACK_INTERVAL_SECONDS)
        say("  {0:30} {1}".format(name, fits))

    say("")
    say("=== STANDING AND WALKING ===")
    measure(LOCOMOTION)
    say("  the creature is designed to move at {0:.1f} m/s. Its play rate needs "
        "the speed each clip was AUTHORED for, which this cannot read -- run "
        "tools/measure_animation_stride.py for that."
        .format(MOVE_SPEED_METRES_PER_SECOND))

    say("")
    say("=== DYING ===")
    measure(DEATHS)

    say("")
    say("=== BEING HIT ===")
    measure(REACTIONS)

    say("")
    say("=== PROBE FINISHED ===")


main()
