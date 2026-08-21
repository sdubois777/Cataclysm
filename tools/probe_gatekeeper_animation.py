"""Measure the Gatekeeper's clip lengths and its mesh inside the editor.

    python tools/run_editor_python.py tools/probe_gatekeeper_animation.py

WHY THIS EXISTS. `game/docs/enemy-source-assets.md` records twelve of Sevarog's
clip lengths, read on 2026-08-07, and every one of them is a swing or an ability.
It records **no idle, no walk and no death** -- the three a creature must have
before it can be dressed. The same gap the Succubus had. Issue #759 builds this
creature and needs all of them.

WHAT IT CHANGES. Nothing. It loads each asset and prints what it reads.

THE PACK SHIPS ONE DEATH CLIP, `Death_front`, which is the fewest in the project
along with the Brute's and the Succubus's. It is listed on its own below so that
a rename shows up as a missing clip rather than as a creature that silently
cannot die.

AND FOUR `Stage_N` POSES WHOSE PURPOSE IS UNKNOWN. `enemy-source-assets.md` says
so: "Whether they are usable as the boss's four phases has not been checked."
This creature has THREE phases, not four, so at best three of the four would be
used. They are measured here so the question can be answered from numbers.
"""

import unreal

ANIMATIONS = "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations"

#: The mesh, so the capsule's half-height is measured rather than read out of
#: `game/docs/enemy-source-assets.md`, which would be trusting one hand-typed
#: figure twice. **This is the tallest creature in the project by far.**
MESH = ("/Game/ParagonSevarog/Characters/Heroes/Sevarog"
        "/Meshes/Sevarog.Sevarog")

#: The clips `game/docs/enemy-source-assets.md` already states a length for, so
#: this run either confirms the 2026-08-07 figures or contradicts them.
ALREADY_RECORDED = [
    "Swing1_FAST_v2", "Swing2_FAST_v2", "Swing3_FAST_v2",
    "Swing1_120fps", "Swing2_120fps", "Swing3_120fps",
    "Swing1_Medium", "Swing2_Medium", "Swing3_Medium",
    "Swing1_Slow", "Swing2_Slow", "Swing3_Slow",
    "Ultimate_Targeting",
    "Ultimate_Targeting_Loop",
    "Ultimate_Swing_120fps",
    "Soul_Siphon",
    "Subjugation",
    "Knock_back",
]

#: Standing.
IDLES = ["Idle", "Idle_additive"]

#: Walking. The pack ships a walk, a jog and a sprint; all three are read before
#: any is chosen, because the designed speed is 3.0 m/s which is slow for a jog.
WALKS = ["Walk_Fwd", "Jog_Fwd", "Run_Fwd", "Sprint_Fwd"]

#: Dying. ONE CLIP.
DEATHS = ["Death_front"]

#: Hit reactions, measured because nothing plays one yet and issue #745 is that
#: gap.
HIT_REACTIONS = [
    "Hitreact_Front", "Hitreact_Back", "Hitreact_Left", "Hitreact_Right",
]

#: The four marker poses. Whether they are usable as phase poses has never been
#: checked, and this creature has three phases rather than four.
STAGES = ["Stage_1", "Stage_2", "Stage_3", "Stage_4"]

#: What the model designs, from `ARCHETYPES["Gatekeeper"]` in
#: `sim/cataclysm_sim/enemy_stats.py`. Written out here rather than imported
#: because this runs inside the editor's own Python, which does not have `sim/`
#: on its path. `tools/tests/test_enemy_animation_fits_its_interval.py` is what
#: holds the two copies together.
ATTACK_INTERVAL_SECONDS = 3.0

#: The wind-ups the design gives its two telegraphed abilities, from
#: `ABILITIES["Gatekeeper"]`. A clip that plays across a wind-up has to fit it.
DREAD_CLEAVE_WIND_UP = 0.9714
SOUL_HARVEST_WIND_UP = 2.0

#: `UCataclysmEnemyDeath::LongestCorpseSeconds`.
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
            say("  {0:28} NOT FOUND".format(name))
            continue
        seconds = float(anim.get_play_length())
        lengths[name] = seconds
        say("  {0:28} {1:>8.4f} s".format(name, seconds))
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
    say("=== HOW BIG THE GATEKEEPER'S MESH IS ===")
    describe_the_mesh()

    say("")
    say("=== THE CLIPS enemy-source-assets.md ALREADY RECORDS ===")
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
            say("  {0:28} IS LONGER THAN THE CORPSE IS KEPT".format(name))

    say("")
    say("=== HIT REACTIONS, WHICH NOTHING PLAYS YET (issue #745) ===")
    measure(HIT_REACTIONS)

    say("")
    say("=== THE FOUR MARKER POSES, PURPOSE UNKNOWN ===")
    say("  This creature has THREE phases, not four, so at best three of these")
    say("  would be used. enemy-source-assets.md says their usefulness has")
    say("  never been checked.")
    measure(STAGES)

    say("")
    say("=== WHICH SWING FITS WHICH WIND-UP ===")
    say("  Dread Cleave marks its arc for {0:.4f} s and the clip IS the wind-up,"
        .format(DREAD_CLEAVE_WIND_UP))
    say("  so a clip longer than that has to be sped up to fit.")
    say("  Soul Harvest marks its ring for {0:.4f} s.".format(SOUL_HARVEST_WIND_UP))
    say("  The play rate ceiling is 2.50; a clip needing more than that does not")
    say("  fit at all.")
    for name in sorted(recorded):
        seconds = recorded[name]
        cleave = max(1.0, seconds / DREAD_CLEAVE_WIND_UP)
        harvest = max(1.0, seconds / SOUL_HARVEST_WIND_UP)
        say("  {0:28} {1:>7.4f} s -> cleave {2:.4f}, harvest {3:.4f}{4}".format(
            name, seconds, cleave, harvest,
            "  CLEAVE DOES NOT FIT" if cleave > 2.5 else ""))

    say("")
    say("=== PROBE FINISHED ===")


main()
