"""Measure the Abyssal Warden's clip lengths inside the editor.

    python tools/run_editor_python.py tools/probe_warden_animation.py

WHY THIS EXISTS. Issue #353 designs the Abyssal Warden, and two of its three
abilities need an animation that nobody has measured. `game/docs/enemy-source-assets.md`
records lengths for the seven `PrimaryAttack_*` clips and for nothing else, so
the leap and the ultimate were proposed from asset NAMES read off the folder
listing. `CLAUDE.md` says to measure the asset before saying anything about art.

WHAT IT CHANGES. Nothing. It loads each clip and prints its play length.

The Abyssal Warden is played by `GruxMolten` from the Paragon Grux pack. Every
Grux animation lives in the one folder below, and `GruxMolten` shares
`Grux_Skeleton` with the base mesh, so every clip plays on it without
retargeting.
"""

import unreal

ANIMATIONS = "/Game/ParagonGrux/Characters/Heroes/Grux/Animations"

#: The attack clips. The seven the asset record already states a length for,
#: which re-measuring checks, plus the two recovery clips it does not.
ATTACKS = [
    "PrimaryAttack_Start",
    "PrimaryAttack_LA",
    "PrimaryAttack_RA",
    "PrimaryAttack_LA_Fast",
    "PrimaryAttack_RA_Fast",
    "PrimaryAttack_LB",
    "PrimaryAttack_RB",
    "PrimaryAttack_FourStrikes",
    "PrimaryAttack_LA_Recovery",
    "PrimaryAttack_RA_Recovery",
]

#: Candidates for the leap. The design proposes a Movement ability in Leap mode
#: because the creature moves at 2.8 m/s with no chase speed and cannot catch
#: anybody. `Bound` and the jump set are the leap candidates; `Stampede` is the
#: charge alternative, measured so the comparison is against numbers.
LEAP_CANDIDATES = [
    "Bound",
    "Jump_Start",
    "Jump_Up",
    "JumpApex",
    "Jump_Mid",
    "Jump_Loop",
    "Jump_Fall",
    "Jump_Land",
    "Attack_Melee_Air",
    "Stampede",
    "Stampede_Knockup",
    "LaunchPad",
]

#: Candidates for the ring at its feet. The design proposes an Ultimate-slot
#: Strike with a 2.0 second wind-up, so the clip has to be long enough to read
#: as a wind-up or short enough to be compressed to one.
ULTIMATE_CANDIDATES = [
    "Ultimate_Roar",
    "Ultimate_Roar_MSA",
    "Cast",
    "LevelStart",
]

#: Reactions and locomotion, for the record. The four hit reactions are reported
#: to be additive, which decides whether the shipped one-clip-at-a-time playback
#: path can use them at all.
OTHER = [
    "Idle",
    "Jog_Fwd",
    "Run_Fwd",
    "TravelMode_Fwd",
    "HitReact_Front",
    "HitReact_Back",
    "Knock_Up",
    "Knock_Back",
    "Stun_Idle",
    "Death_A",
    "Turn_Left_90",
    "Turn_Right_90",
]

#: What the model designs, from `ARCHETYPES` in
#: `sim/cataclysm_sim/enemy_stats.py`. Written here rather than imported because
#: this runs inside the editor's own Python, which does not have `sim/` on its
#: path. `tools/tests/test_enemy_animation_fits_its_interval.py` holds the two
#: copies together.
ATTACK_INTERVAL_SECONDS = 2.4

#: The ceiling both `PlayOneShot` and `MontageRateFor` clamp to, from
#: `game/Source/Cataclysm/Character/CataclysmBruteCharacter.h`. Above it a clip
#: is played at the ceiling and still overruns whatever window it was given.
MAXIMUM_PLAY_RATE = 2.50

#: The two wind-ups the design proposes, in seconds. A clip longer than its
#: wind-up is compressed to fit; a clip shorter than it holds its last pose.
LEAP_WIND_UP_SECONDS = 0.83
ULTIMATE_WIND_UP_SECONDS = 2.00


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


def rate_report(lengths, window, label):
    """What play rate each clip needs to fit `window`, and whether it clears
    the ceiling the engine clamps to."""
    say("")
    say("  To fit {0} ({1:.2f} s):".format(label, window))
    for name in sorted(lengths):
        seconds = lengths[name]
        rate = max(1.0, seconds / window)
        verdict = "fits" if rate <= MAXIMUM_PLAY_RATE else "TOO LONG"
        say("    {0:30} {1:>8.4f} s -> play rate {2:.2f}  {3}".format(
            name, seconds, rate, verdict))


def main():
    say("=== THE ABYSSAL WARDEN'S ATTACK CLIPS ===")
    attacks = measure(ATTACKS)
    rate_report(attacks, ATTACK_INTERVAL_SECONDS, "the 2.4 s attack interval")

    say("")
    say("=== CAN IT SWING TWICE IN ONE INTERVAL AT AUTHORED SPEED? ===")
    left = attacks.get("PrimaryAttack_LA")
    right = attacks.get("PrimaryAttack_RA")
    if left is not None and right is not None:
        pair = left + right
        say("  PrimaryAttack_LA + PrimaryAttack_RA = {0:.4f} s against a "
            "{1:.2f} s interval: {2}".format(
                pair, ATTACK_INTERVAL_SECONDS,
                "YES, with {0:.4f} s to spare".format(
                    ATTACK_INTERVAL_SECONDS - pair)
                if pair <= ATTACK_INTERVAL_SECONDS else "NO"))
    else:
        say("  one of the two clips is missing, so nothing was compared")

    say("")
    say("=== LEAP AND CHARGE CANDIDATES ===")
    leaps = measure(LEAP_CANDIDATES)
    rate_report(leaps, LEAP_WIND_UP_SECONDS, "the leap's proposed wind-up")

    say("")
    say("=== ULTIMATE CANDIDATES ===")
    ultimates = measure(ULTIMATE_CANDIDATES)
    rate_report(ultimates, ULTIMATE_WIND_UP_SECONDS,
                "the ring's proposed wind-up")

    say("")
    say("=== LOCOMOTION, REACTIONS AND DEATH, FOR THE RECORD ===")
    measure(OTHER)

    say("")
    say("=== ARE THE HIT REACTIONS ADDITIVE? ===")
    say("  An additive clip cannot be played on its own by the shipped "
        "one-clip-at-a-time path.")
    for name in ("HitReact_Front", "HitReact_Back", "Knock_Up",
                 "Jump_Land_Additive"):
        anim = load(name)
        if anim is None:
            say("  {0:30} NOT FOUND".format(name))
            continue
        try:
            setting = anim.get_editor_property("additive_anim_type")
            say("  {0:30} {1}".format(name, setting))
        except Exception as error:                              # noqa: BLE001
            say("  {0:30} could not read additive_anim_type: {1}".format(
                name, error))

    say("")
    say("=== PROBE FINISHED ===")


main()
