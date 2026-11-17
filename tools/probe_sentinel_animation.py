"""Measure the Corrupted Sentinel's clip lengths inside the editor.

    python tools/run_editor_python.py tools/probe_sentinel_animation.py

WHY THIS EXISTS. Issue #369 asks what to do about a 2.40 second firing animation
against a 2.0 second attack interval, and the answer is a play rate computed from
those two numbers. A derived figure is only as good as the measurement under it,
and the 2.40 was read on 2026-08-07 and written into
`game/docs/enemy-source-assets.md` by hand. This re-reads it from the asset.

WHAT IT CHANGES. Nothing. It loads each clip and prints its play length.

The Corrupted Sentinel is played by the siege lane minion,
`Minion_Lane_Siege_Dawn`, from the Paragon Minions pack. Its animations live
under the Siege folder below.
"""

import unreal

ANIMATIONS = ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
              "/Animations/Siege")

#: Every clip `game/docs/enemy-source-assets.md` states a length for, plus the
#: rooted idle it names without one. Measuring the whole list rather than only
#: `Fire_Planted` is what makes a wrong folder or a renamed asset visible: one
#: missing clip could be a typo, seven missing clips is the wrong path.
INTERESTING = [
    "PlantedIntro",
    "Idle_Planted",
    "Fire_Planted",
    "Fire_Planted_B",
    "PlantedExit",
    "Fire_A",
    "Fire_B",
    "Fire_C",
    "MeleeAttack_A",
]

#: The four rooted hit reactions. The design rests on the creature being able to
#: take a hit without leaving its rooted state, so their presence is part of the
#: measurement rather than a detail.
HIT_REACTIONS = [
    "HitReact_Front_Planted",
    "HitReact_Back_Planted",
    "HitReact_Left_Planted",
    "HitReact_Right_Planted",
]

#: EIGHT DEATHS, WHICH IS THE MOST IN THE PROJECT. The Brute ships one, the
#: Abyssal Warden and the Hellhound two, the Imp five.
#: `ACataclysmEnemyCharacter::PlayDeathAnimation` draws one of however many it is
#: given, and every one has to be inside
#: `UCataclysmEnemyDeath::LongestCorpseSeconds`, so their lengths are needed
#: before the creature can be dressed. Added 2026-08-20 for issue #39.
DEATHS = ["Death_A", "Death_B", "Death_C", "Death_D",
          "Death_E", "Death_F", "Death_G", "Death_H"]

#: The two idles. `Idle_Planted` is the rooted one and is a single pose; `Idle`
#: is the standing one, which this creature never uses because it never stands
#: up. Both measured so the difference is on the record.
IDLES = ["Idle_Planted", "Idle"]

#: The mesh, so the capsule's half-height comes from a measurement rather than
#: from the figure in `game/docs/enemy-source-assets.md` being trusted twice.
MESH = ("/Game/ParagonMinions/Characters/Minions/Down_Minions"
        "/Meshes/Minion_Lane_Siege_Dawn.Minion_Lane_Siege_Dawn")

#: What the model designs, from `ARCHETYPES` in
#: `sim/cataclysm_sim/enemy_stats.py`. Written here rather than imported because
#: this runs inside the editor's own Python, which does not have `sim/` on its
#: path. `tools/tests/test_enemy_animation_fits_its_interval.py` is what holds
#: the two copies together.
ATTACK_INTERVAL_SECONDS = 2.0


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
            say("  {0:26} NOT FOUND".format(name))
            continue
        seconds = float(anim.get_play_length())
        lengths[name] = seconds
        say("  {0:26} {1:>8.4f} s".format(name, seconds))
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
    say("=== HOW BIG THE CORRUPTED SENTINEL'S MESH IS ===")
    describe_the_mesh()

    say("")
    say("=== THE CORRUPTED SENTINEL'S CLIPS ===")
    lengths = measure(INTERESTING)

    say("")
    say("=== STANDING, ROOTED AND NOT ===")
    measure(IDLES)

    say("")
    say("=== DYING, EIGHT WAYS ===")
    deaths = measure(DEATHS)
    say("  UCataclysmEnemyDeath::LongestCorpseSeconds is 4.0, and every clip "
        "above has to be inside it or the body is removed mid-fall.")
    for name in sorted(deaths):
        if deaths[name] > 4.0:
            say("  {0:26} IS LONGER THAN THE CORPSE IS KEPT".format(name))

    say("")
    say("=== ROOTED HIT REACTIONS ===")
    measure(HIT_REACTIONS)

    say("")
    say("=== ISSUE #369: THE PLAY RATE EACH FIRING CLIP NEEDS ===")
    say("  attack interval {0:.2f} s, from sim/cataclysm_sim/enemy_stats.py"
        .format(ATTACK_INTERVAL_SECONDS))
    for name in ("Fire_Planted", "Fire_Planted_B", "Fire_A", "Fire_B", "Fire_C",
                 "MeleeAttack_A"):
        seconds = lengths.get(name)
        if seconds is None:
            continue
        rate = seconds / ATTACK_INTERVAL_SECONDS
        say("  {0:26} {1:>8.4f} s -> play rate {2:.4f}".format(
            name, seconds, max(1.0, rate)))

    say("")
    say("=== PROBE FINISHED ===")


main()
