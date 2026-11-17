"""Measure the Succubus's clip lengths and its mesh inside the editor.

    python tools/run_editor_python.py tools/probe_succubus_animation.py

WHY THIS EXISTS. `game/docs/enemy-source-assets.md` records nine of the
Countess's clip lengths, read on 2026-08-07 and written in by hand, and every one
of them is an ability clip. It records no idle, no walk and no death, which are
exactly the three a creature has to have before it can be dressed. Issue #39
builds this creature and needs all of them.

WHAT IT CHANGES. Nothing. It loads each asset and prints what it reads.

THE PACK SHIPS ONE DEATH CLIP AND THAT IS THE FEWEST IN THE PROJECT. The Brute
has one too, the Abyssal Warden and the Hellhound two, the Imp five, the
Corrupted Sentinel eight. It is listed below on its own so that a rename shows up
as a missing clip rather than as a creature that silently cannot die.
"""

import unreal

ANIMATIONS = "/Game/ParagonCountess/Characters/Heroes/Countess/Animations"

#: The mesh, so the capsule's half-height is measured here rather than read out
#: of `game/docs/enemy-source-assets.md`, which would be trusting one hand-typed
#: figure twice.
MESH = ("/Game/ParagonCountess/Characters/Heroes/Countess"
        "/Meshes/SM_Countess.SM_Countess")

#: The clips `game/docs/enemy-source-assets.md` already states a length for, so
#: this run either confirms the 2026-08-07 figures or contradicts them. Measuring
#: the whole list rather than only the one clip the creature uses is what makes a
#: wrong folder visible: one missing clip is a typo, nine is the wrong path.
ALREADY_RECORDED = [
    "Primary_Attack_Fast_V1",
    "Primary_Attack_Normal",
    "Primary_Attack_Slow",
    "Primary_Attack_Slow_Recovery",
    "Ability_Q",
    "Ability_E",
    "Ability_RMB",
    "Ability_Ultimate",
    "Cast",
]

#: Standing. `Idle_Relaxed` and `Idle_Straight` are the two full clips; the pack
#: also ships `Idle_Pose`, `Idle_Additive` and `Idle_Facial`, which are a single
#: pose and two additive layers rather than something to loop, and are measured
#: only so the difference is on the record.
IDLES = [
    "Idle_Relaxed",
    "Idle_Straight",
    "Idle_Pose",
    "Idle_Additive",
    "Idle_Facial",
]

#: Walking. `Jog_Fwd` is what every other creature here uses; the combat variant
#: and the sprint are measured so that choosing between them is a decision made
#: against numbers.
WALKS = [
    "Jog_Fwd",
    "Jog_Fwd_Combat",
    "Sprint_Fwd",
]

#: Dying. ONE CLIP, which is the fewest any creature in the project has.
DEATHS = ["Death"]

#: Hit reactions, measured because nothing plays one yet and issue #745 is that
#: gap. Their presence is part of what the pack offers.
HIT_REACTIONS = [
    "Hitreact_Fwd",
    "Hitreact_Bwd",
    "Hitreact_Left",
    "Hitreact_Right",
]

#: What the model designs, from `ARCHETYPES["Succubus"]` in
#: `sim/cataclysm_sim/enemy_stats.py`. Written out here rather than imported
#: because this runs inside the editor's own Python, which does not have `sim/`
#: on its path. `tools/tests/test_enemy_animation_fits_its_interval.py` is what
#: holds the two copies together.
ATTACK_INTERVAL_SECONDS = 2.6

#: `UCataclysmEnemyDeath::LongestCorpseSeconds`. A death clip longer than this is
#: cut off part way through by the body being removed.
LONGEST_CORPSE_SECONDS = 4.0


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
            say("  {0:32} NOT FOUND".format(name))
            continue
        seconds = float(anim.get_play_length())
        lengths[name] = seconds
        say("  {0:32} {1:>8.4f} s".format(name, seconds))
    return lengths


def describe_the_mesh():
    mesh = unreal.load_asset(MESH)
    if mesh is None:
        say("  THE MESH WAS NOT FOUND AT " + MESH)
        return

    extent = mesh.get_bounds().box_extent
    say("  reference-pose bounds, full width in centimetres:")
    say("    X {0:.1f}   Y {1:.1f}   Z (height) {2:.1f}".format(
        extent.x * 2.0, extent.y * 2.0, extent.z * 2.0))
    say("  so half its height, which is what a capsule wants, is {0:.2f}"
        .format(float(extent.z)))

    slots = mesh.get_editor_property("materials")
    say("  material slots: {0}".format(len(slots)))
    skeleton = mesh.get_editor_property("skeleton")
    say("  skeleton: {0}".format(skeleton.get_name() if skeleton else "(none)"))


def main():
    say("=== HOW BIG THE SUCCUBUS'S MESH IS ===")
    describe_the_mesh()

    say("")
    say("=== THE NINE CLIPS enemy-source-assets.md ALREADY RECORDS ===")
    recorded = measure(ALREADY_RECORDED)

    say("")
    say("=== STANDING ===")
    measure(IDLES)

    say("")
    say("=== WALKING ===")
    measure(WALKS)

    say("")
    say("=== DYING, ONE WAY ===")
    deaths = measure(DEATHS)
    say("  UCataclysmEnemyDeath::LongestCorpseSeconds is {0:.1f}, and a clip "
        "longer than that is cut off by the body being removed."
        .format(LONGEST_CORPSE_SECONDS))
    for name in sorted(deaths):
        if deaths[name] > LONGEST_CORPSE_SECONDS:
            say("  {0:32} IS LONGER THAN THE CORPSE IS KEPT".format(name))

    say("")
    say("=== HIT REACTIONS, WHICH NOTHING PLAYS YET (issue #745) ===")
    measure(HIT_REACTIONS)

    say("")
    say("=== ISSUE #369: THE PLAY RATE EACH ATTACK CLIP WOULD NEED ===")
    say("  attack interval {0:.2f} s, from sim/cataclysm_sim/enemy_stats.py"
        .format(ATTACK_INTERVAL_SECONDS))
    say("  a clip SHORTER than the interval is played as authored and the "
        "creature waits out the rest, so a rate of 1.0000 means no scaling.")
    for name in ("Primary_Attack_Normal", "Primary_Attack_Fast_V1",
                 "Primary_Attack_Slow", "Cast", "Ability_Q", "Ability_E"):
        seconds = recorded.get(name)
        if seconds is None:
            continue
        rate = max(1.0, seconds / ATTACK_INTERVAL_SECONDS)
        say("  {0:32} {1:>8.4f} s -> play rate {2:.4f}".format(
            name, seconds, rate))

    say("")
    say("=== PROBE FINISHED ===")


main()
