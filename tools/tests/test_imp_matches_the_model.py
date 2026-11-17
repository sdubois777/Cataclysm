"""The Imp's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmImpCharacter.h` hard-codes the
creature's whole stat block, and every one of those numbers also lives in the
Python model. Two copies of one number drift, and in this repository they have.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Imp"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ABILITIES["Imp"]` and
`ATTACK_REACH["Imp"]` in `sim/cataclysm_sim/enemy_abilities.py` are where the
creature is designed. When this file fails, the usual fix is to change the C++.

WHY IT EXISTS AT ALL RATHER THAN AN ENGINE TEST. Continuous integration never
builds the C++ and never opens the editor, so an automation test cannot run on a
pull request. This reads the source text, which is present either way.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second because
the design document does; Unreal works in centimetres. The factor of 100 is
stated at each conversion rather than hidden in a helper.

THE ONE NUMBER HERE THAT CANNOT COME FROM THE MODEL is the creature's height.
The design gives it a body radius and no height at all, so the capsule's
half-height comes from the art, and the checks on it are against
`game/docs/enemy-source-assets.md` rather than against the simulation.
"""

from __future__ import annotations

import math
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SIM = REPO_ROOT / "sim"
if str(SIM) not in sys.path:
    sys.path.insert(0, str(SIM))

CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
IMP_HEADER = CHARACTER_DIR / "CataclysmImpCharacter.h"
IMP_SOURCE = CHARACTER_DIR / "CataclysmImpCharacter.cpp"
PLAYER_SOURCE = CHARACTER_DIR / "CataclysmPlayerCharacter.cpp"
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")
GAME_MODE_SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.cpp")
ASSET_NOTES = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
LEVEL_SCRIPT = REPO_ROOT / "tools" / "generate_input_assets.py"

CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path = IMP_HEADER) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(
            f"{path.name} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, this guard has "
            f"nothing left to check and the number is unguarded.")
    return float(match.group(1))


def whole_number_constant(name: str, path: pathlib.Path = IMP_HEADER) -> int:
    """The value of a `static constexpr int32 <name> = <number>;` line."""
    match = re.search(
        rf"static\s+constexpr\s+int32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no "
                    f"'static constexpr int32 {name} = <number>;' line.")
    return int(match.group(1))


def local_constant(name: str, path: pathlib.Path) -> float:
    """The value of a file-local `constexpr float <name> = <number>f;`."""
    match = re.search(
        rf"constexpr\s+float\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'constexpr float {name}' line.")
    return float(match.group(1))


def property_default(name: str, path: pathlib.Path = GAME_MODE_HEADER) -> float:
    """The value of a `float <name> = <number>f;` property default."""
    match = re.search(
        rf"\bfloat\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'float {name} = <number>f;' line.")
    return float(match.group(1))


def whole_number_property(name: str,
                          path: pathlib.Path = GAME_MODE_HEADER) -> int:
    """The value of an `int32 <name> = <number>;` property default."""
    match = re.search(
        rf"\bint32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;", source(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'int32 {name} = <number>;' line.")
    return int(match.group(1))


def imp():
    from cataclysm_sim.enemy_stats import archetype

    return archetype("Imp")


def rend():
    from cataclysm_sim.enemy_abilities import abilities

    for entry in abilities("Imp"):
        if entry.name == "Rend":
            return entry
    pytest.fail(
        "the Imp has no ability called 'Rend' in ABILITIES in "
        "sim/cataclysm_sim/enemy_abilities.py. If it was renamed, rename it "
        "here too.")


# --------------------------------------------------------------------------
# The profile, straight off the archetype
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cpp_name, model_value, scale, what", [
    ("DesignedAttackIntervalSeconds", "attack_interval", 1.0,
     "seconds between claw swipes"),
    ("DesignedResistancePercent", "resistance", 1.0,
     "percent of all incoming damage resisted"),
    ("DesignedCritChancePercent", "crit_chance", 1.0, "critical strike chance"),
    ("DesignedCritMultiplierPercent", "crit_multiplier", 1.0,
     "critical strike multiplier"),
    ("DesignedEvasionPercent", "evasion", 1.0, "evasion"),
    ("DesignedWalkSpeedCmPerSecond", "move_speed", CM_PER_METRE,
     "how fast it moves"),
    ("DesignedTurnRateDegreesPerSecond", "turn_rate_degrees", 1.0,
     "how fast it turns"),
    ("DesignedEnergyShieldFraction", "energy_shield_fraction", 1.0,
     "what fraction of its health is an energy shield"),
])
def test_the_profile_matches_the_archetype(cpp_name, model_value, scale, what):
    designed = getattr(imp(), model_value) * scale
    assert constant(cpp_name) == pytest.approx(designed), (
        f"{cpp_name} in CataclysmImpCharacter.h is {constant(cpp_name)} and "
        f"ARCHETYPES['Imp'].{model_value} in sim/cataclysm_sim/enemy_stats.py "
        f"gives {designed} for {what}. The model is authoritative.")


def test_the_reach_matches_the_ability_table():
    """Rend's own radius is the creature's reach, which is unusual and designed.

    Every other creature's reach is a separate figure from any ability's radius.
    Here they are one number, because the design derives the reach from crowd
    geometry and then gives the attack that radius.
    """
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    designed = ATTACK_REACH["Imp"] * CM_PER_METRE
    assert constant("DesignedMeleeReachCm") == pytest.approx(designed)

    from_the_ability = rend().params["Radius"] * CM_PER_METRE
    assert constant("DesignedMeleeReachCm") == pytest.approx(from_the_ability), (
        f"DesignedMeleeReachCm is {constant('DesignedMeleeReachCm')} and Rend's "
        f"own Radius is {from_the_ability} cm. The design says they are the "
        f"same number: 'Its radius is its attack reach, so the front two ranks "
        f"of a pack both connect.'")


def test_the_capsule_radius_is_the_designed_body_radius():
    """And it is the ONLY measured body radius in the roster.

    Six of the seven take the dataclass default of 0.48 m, which is issue #366.
    This one is 0.30 and the design document says where it came from: the lesser
    imp minion's capsule, 'because it is the same creature'.
    """
    designed = imp().body_radius * CM_PER_METRE
    assert constant("ImpCapsuleRadius") == pytest.approx(designed), (
        f"ImpCapsuleRadius is {constant('ImpCapsuleRadius')} and the model's "
        f"body_radius is {imp().body_radius} m. This one is load-bearing: the "
        f"ring arithmetic that sets the creature's reach is computed from it.")

    from cataclysm_sim.enemy_stats import archetype

    assert imp().body_radius != archetype("Brute").body_radius, (
        "the Imp's body radius is now the same as the Brute's, which means it "
        "has fallen back to the 0.48 m default that issue #366 tracks. It was "
        "the one measured radius in the roster.")


def test_the_reach_is_the_second_rank_of_a_crowd():
    """The whole derivation, recomputed rather than quoted.

    A body of radius r standing D from the centre covers 2 x arcsin(r / D) of
    the circle, so a ring holds pi / arcsin(r / D). The design puts the Imp's
    reach at the second rank because ten Imps have to be able to reach a player
    at once, and one rank is seven.
    """
    player_radius = local_constant("CapsuleRadius", PLAYER_SOURCE)
    imp_radius = constant("ImpCapsuleRadius")

    first_rank = player_radius + imp_radius
    second_rank = first_rank + 2.0 * imp_radius

    assert constant("DesignedMeleeReachCm") == pytest.approx(second_rank), (
        f"the second rank stands {second_rank:.0f} cm from the player's centre "
        f"-- the player's {player_radius:.0f} cm capsule, then one Imp's "
        f"{imp_radius:.0f} cm body, then another's -- and the creature's reach "
        f"is {constant('DesignedMeleeReachCm'):.0f} cm. The design sets the "
        f"reach at the second rank so that more than seven can swing at once.")

    # EACH RANK IS FLOORED BEFORE THEY ARE ADDED, because a rank holds whole
    # bodies. Adding the two fractions first and flooring once gives 21, since
    # 7.31 and 13.70 carry a spare body between them that neither ring has room
    # for. The design's own table floors each: 7 then 13.
    fits_in_first = math.floor(math.pi / math.asin(imp_radius / first_rank))
    fits_in_second = math.floor(math.pi / math.asin(imp_radius / second_rank))

    assert fits_in_first == 7, (
        f"{fits_in_first} Imps now fit in the first rank and the design says "
        f"seven. The bodies changed size.")
    assert fits_in_first + fits_in_second == 20, (
        f"{fits_in_first + fits_in_second} Imps now fit in the two ranks the "
        f"creature can reach across, and the design says twenty. Twenty is the "
        f"figure the document uses for the lethal pack, and the geometry is the "
        f"only thing enforcing it -- there is no attack-token rule.")


def test_it_has_no_separate_chase_speed():
    """It moves at one speed whether or not it has seen anything."""
    assert imp().chase_speed == 0.0, (
        "the model now gives the Imp a chase speed, so the class needs a second "
        "speed and the code that switches between them. The Brute is the only "
        "creature with one.")


def test_it_outruns_every_player_class():
    """'Walking away from an Imp is never an escape' is a mechanical claim.

    It is what makes the Movement slot the answer to being surrounded, which is
    the design's stated way out of a pack.
    """
    from cataclysm_sim.character import DEFAULT_STAT_LINE
    from cataclysm_sim.classes import MASOCHIST, RAVAGER, RITUALIST

    # THE THREE DEMONIC CLASSES, read off their own definitions rather than
    # copied. A class that does not override its movement speed uses the base
    # stat line's, which is why this falls back rather than indexing -- only two
    # of the three state one. The same helper the Abyssal Warden's test uses.
    def speed_of(cls):
        scaling = cls.overrides.get("movement_speed")
        if scaling is None:
            scaling = DEFAULT_STAT_LINE["movement_speed"]
        return scaling.base

    fastest = max(speed_of(cls) for cls in (RAVAGER, RITUALIST, MASOCHIST))

    assert imp().move_speed > fastest, (
        f"the Imp moves at {imp().move_speed} m/s and the fastest player class "
        f"moves at {fastest}. A player who can walk away from a pack does not "
        f"need the Movement slot, which is the design's stated way out of being "
        f"surrounded.")


def test_it_is_the_second_fastest_creature_in_the_slice():
    """Behind the Hellhound and ahead of everything else."""
    from cataclysm_sim.enemy_stats import ARCHETYPES

    speeds = sorted(
        ((a.move_speed, name) for name, a in ARCHETYPES.items()
         if name != "Baseline"),
        reverse=True)

    assert speeds[1][1] == "Imp", (
        f"the Imp is no longer the second fastest creature; the order is now "
        f"{[name for _, name in speeds]}. The class comment says it is behind "
        f"the Hellhound and ahead of every player class.")


# --------------------------------------------------------------------------
# Its one attack, and the second ability it deliberately does not have
# --------------------------------------------------------------------------

def test_the_model_designs_exactly_one_ability():
    """'The Imp has one attack and nothing else, and that is the design rather
    than an omission.' -- docs/Cataclysm_GDD_v2.md."""
    from cataclysm_sim.enemy_abilities import abilities

    designed = abilities("Imp")
    assert len(designed) == 1, (
        f"the Imp now has {len(designed)} designed abilities: "
        f"{[a.name for a in designed]}. The design refuses a second one for two "
        f"stated reasons -- whatever an Imp does is multiplied by ten, and a "
        f"second ability could not be quick enough to belong on this creature. "
        f"If one was added on purpose, the class needs an EnemyAbilities "
        f"override, which it deliberately does not have.")


def test_the_class_offers_no_abilities_to_the_brain():
    """Rend is the basic attack, not an entry in EnemyAbilities.

    A basic attack in this project is `MeleeReachCm` plus
    `AttackIntervalSeconds`; the array is for the extra things a creature can
    do, and this creature has none. `ACataclysmCharacterBase::EnemyAbilities`
    already returns an empty array, so the class does not override it.
    """
    assert "EnemyAbilities" not in source(IMP_SOURCE), (
        "CataclysmImpCharacter.cpp now overrides EnemyAbilities. The design "
        "gives this creature one attack and nothing else, so the brain should "
        "be offered nothing to choose from. If a second ability was designed, "
        "test_the_model_designs_exactly_one_ability above should have failed "
        "first.")


def test_it_cannot_telegraph_and_that_falls_out_of_being_fast():
    """The telegraph rule caps a marker at 3.5 x (interval / 2 - 0.4) metres.

    At an 0.9 second interval that is 0.2 m, which is smaller than the creature
    standing in it. The design is explicit that this is not a choice made for
    the Imp: it falls out of the enemy being fast, and it is what stops a pack
    filling the screen with markers.
    """
    interval = constant("DesignedAttackIntervalSeconds")
    largest_marker = 3.5 * (interval / 2.0 - 0.4)

    smallest_useful_marker = 1.0
    assert largest_marker < smallest_useful_marker, (
        f"at a {interval} s interval the telegraph rule now allows a marker of "
        f"{largest_marker:.2f} m, which is at least the {smallest_useful_marker} "
        f"m the design calls the smallest useful one. The Imp could then "
        f"telegraph, and the design says an Imp that could telegraph would be "
        f"individually dangerous and would stop being swarm fodder.")


def test_the_swipe_fits_inside_the_interval_between_swipes():
    """Nothing rate-scales a swipe up, so the interval is a hard floor.

    `PlayOneShot` clamps the rate to at least 1.0, so a clip longer than its
    window plays at its authored speed and runs past the start of the next one.
    """
    clip = constant("RendAnimationSeconds")
    interval = constant("DesignedAttackIntervalSeconds")

    assert clip < interval, (
        f"Attack_A_SetA is {clip} s and the designed interval is {interval} s, "
        f"so one swipe would still be playing when the next began. The pack's "
        f"_SetB variants are 0.8333 and its four unsuffixed attacks are 1.0000, "
        f"which is why the class uses the _SetA five.")


def test_it_draws_between_five_swipes_because_it_arrives_in_tens():
    """One clip is right for a creature you meet alone and wrong for a pack."""
    swipes = whole_number_constant("RendAnimationCount")
    pack = whole_number_property("ImpCount")

    assert swipes > 1, (
        "the Imp now plays a single claw swipe. Ten of them swinging the same "
        "0.8 second clip is the one place in this project where a single clip "
        "is visibly wrong, and the Paragon Minions pack ships five that all fit "
        "the interval.")
    assert swipes <= pack, (
        f"the Imp draws between {swipes} swipes and only {pack} of it are "
        f"spawned, so some of them cannot be seen at all.")


def test_it_has_five_ways_to_fall_over():
    """More than any other creature in the project, and for a reason."""
    assert whole_number_constant("DeathAnimationCount") == 5, (
        "the Imp no longer loads five death clips. It is the creature that dies "
        "ten at a time, and the Paragon Minions pack ships five deaths where "
        "Rampage ships one.")


# --------------------------------------------------------------------------
# Its defence, which is not armour
# --------------------------------------------------------------------------

def test_its_armour_share_is_exactly_zero_and_it_is_the_only_one():
    """Evasion is its designed defence, which is why area damage answers a pack.

    Evasion avoids direct attacks only, so a single-target build fighting twenty
    misses a quarter of its swings and an area skill misses none.
    """
    from cataclysm_sim.enemy_stats import ARCHETYPES

    assert imp().armor_share == 0.0, (
        f"the Imp's armor_share is now {imp().armor_share}. The design gives it "
        f"none at all and makes 25% evasion its defence, and the sandbox "
        f"spawner deliberately does not call SetArmour because of it.")

    others = [name for name, a in ARCHETYPES.items()
              if name not in ("Baseline", "Imp") and a.armor_share == 0.0]
    assert not others, (
        f"something other than the Imp now has no armour at all: {others}. That "
        f"is fine, and the comments saying the Imp is the only one need "
        f"changing.")


def test_it_has_the_highest_evasion_in_the_slice():
    from cataclysm_sim.enemy_stats import ARCHETYPES

    highest = max((a.evasion, name) for name, a in ARCHETYPES.items()
                  if name != "Baseline")

    assert highest[1] == "Imp", (
        f"{highest[1]} now has the highest evasion in the roster at "
        f"{highest[0]}%, not the Imp at {imp().evasion}%.")


# --------------------------------------------------------------------------
# Its art, and the one thing about it the model cannot decide
# --------------------------------------------------------------------------

def test_the_walk_needs_a_play_rate_the_project_allows():
    """MEASURED, NOT GUESSED, AND THE MEASUREMENT NEEDED FIXING FIRST.

    `tools/measure_animation_stride.py` read the planted foot through Epic's
    `ik_foot_l` and `ik_foot_r`, and this rig has neither -- so both of the
    Imp's walks and its idle all measured 0.0 cm/s and the walks looked like
    idles. The tool now picks the rig from the bones a clip really drives.

    A SMALLER MESH NEEDS A HIGHER RATE, which is the part that is easy to get
    backwards: a scaled mesh takes a proportionally shorter stride, so its
    planted foot travels more slowly and the clip has to run faster to keep up
    with the same body.
    """
    scale = constant("ImpMeshScale")
    authored = constant("AuthoredJogSpeedCmPerSecond") * scale
    needed = constant("DesignedWalkSpeedCmPerSecond") / authored
    ceiling = constant("MaximumPlayRate")

    assert needed <= ceiling, (
        f"the Imp's walk needs a play rate of {needed:.3f} at a mesh scale of "
        f"{scale} and the ceiling is {ceiling}. Above it the clip is clamped "
        f"and its feet slide. NonCombat_JogFwd_B at 382.6 cm/s is already the "
        f"fastest walk in the pack, so the answers are a larger mesh, an "
        f"animation Blueprint, or a slower designed speed. Issue #760.")


def test_the_combat_walk_is_too_slow_for_this_creature_to_use():
    """Which is why it wears a clip named for not being in combat.

    Stated as a test rather than only as a comment, because it looks like a
    mistake and is not one.
    """
    combat_walk_authored = 241.1
    needed = constant("DesignedWalkSpeedCmPerSecond") / combat_walk_authored

    assert needed > constant("MaximumPlayRate"), (
        f"Combat_JogFwd measures {combat_walk_authored} cm/s and the creature "
        f"moves at {constant('DesignedWalkSpeedCmPerSecond')}, which now needs "
        f"a play rate of {needed:.3f} -- inside the "
        f"{constant('MaximumPlayRate')} ceiling. If the designed speed dropped, "
        f"the combat walk is usable again and the class should wear it.")

    assert "NonCombat_JogFwd_B" in source(IMP_SOURCE), (
        "the Imp no longer wears NonCombat_JogFwd_B, which is the only walk in "
        "the pack fast enough for it.")


def test_the_capsule_half_height_comes_from_the_mesh():
    """The design gives this creature a width and no height at all."""
    mesh_height = 175.9
    assert constant("ImpCapsuleHalfHeight") == pytest.approx(mesh_height / 2.0), (
        f"ImpCapsuleHalfHeight is {constant('ImpCapsuleHalfHeight')} and the "
        f"mesh measures {mesh_height} cm tall, so half of it is "
        f"{mesh_height / 2.0}. The mesh is dropped by exactly this in "
        f"ResolveBody, which is what puts its feet on the capsule's bottom.")


def test_the_asset_notes_record_what_was_measured():
    """The durable record of a measurement is the notes, not a comment in one
    class. A figure in the C++ that the notes do not carry is one nobody can
    check without opening the editor again.
    """
    notes = source(ASSET_NOTES)

    for figure in ("382.6", "63.5", "175.9", "0.80"):
        assert figure in notes, (
            f"game/docs/enemy-source-assets.md does not record {figure}, which "
            f"CataclysmImpCharacter.h depends on. Measurements belong in the "
            f"notes as well as in the class.")


# --------------------------------------------------------------------------
# The sandbox scaffolding
# --------------------------------------------------------------------------

def sandbox_stat_block():
    """The one encounter the sandbox's health figure comes from.

    The same encounter every other creature's test file reads, and the same one
    the comment block in `CataclysmGameMode.h` names. Issue #525.
    """
    from cataclysm_sim.enemy_stats import stats_on_floor

    return stats_on_floor("Common", 1, "Cataclysm", total_floors=50, floor=50,
                          kind="Imp")


def test_the_sandbox_spawns_a_pack_rather_than_a_creature():
    """'A pack is ten' is a heading in the design document.

    One Imp takes 48 seconds to kill a geared character and ten take 4.9, so a
    single one in the level cannot demonstrate the only thing this creature is
    for.
    """
    assert whole_number_property("ImpCount") == 10, (
        f"ImpCount is {whole_number_property('ImpCount')} and the design has a "
        f"subsection headed 'A pack is ten'. Ten is three more than one full "
        f"ring of seven, which is what makes the second ring -- and therefore "
        f"this creature's reach -- matter in an ordinary encounter.")

    assert "SpawnImps();" in source(GAME_MODE_SOURCE), (
        "ACataclysmGameMode::StartPlay does not call SpawnImps, so the spawner "
        "exists and nothing runs it.")


def test_the_sandbox_health_is_the_models_tier_one_figure():
    designed = sandbox_stat_block().health
    written = property_default("ImpHealth")

    assert written == pytest.approx(round(designed)), (
        f"ImpHealth is {written} and the design model gives a Common Imp at "
        f"tier 1, on the last floor of a 50-floor Cataclysm dungeon, "
        f"{designed:.2f}. The model is authoritative.")


def test_the_sandbox_damage_is_the_dummys_times_the_designed_share():
    dummy = property_default("TrainingDummyAttackDamage")
    expected = dummy * imp().damage_share
    written = property_default("ImpAttackDamage")

    assert written == pytest.approx(expected), (
        f"ImpAttackDamage is {written} and the training dummy's {dummy} times "
        f"the designed damage share of {imp().damage_share} is {expected}.")


def test_the_sandbox_gives_the_pack_no_armour():
    """Because the design gives this creature none, and calling SetArmour(0)
    would look like a figure somebody chose."""
    assert not re.search(r"Imp->SetArmour", source(GAME_MODE_SOURCE)), (
        "SpawnImps now calls SetArmour. The Imp's designed armour share is "
        "exactly zero and it is the only creature in the roster with none; its "
        "defence is 25% evasion. If the model gave it armour, "
        "test_its_armour_share_is_exactly_zero_and_it_is_the_only_one above "
        "should have failed first.")

    assert not re.search(r"\bfloat\s+ImpArmour\s*=", source(GAME_MODE_HEADER)), (
        "CataclysmGameMode.h now has an ImpArmour setting, and the Imp is "
        "designed to have none.")


def test_the_pack_stands_beyond_its_own_notice_radius():
    """So it does not set off at a player who has only just appeared.

    Measured at the near edge of the pack rather than at its middle, which is
    the Imp that would notice first.
    """
    near_edge = (property_default("ImpDistanceCm")
                 - property_default("ImpPackRadiusCm"))
    notices_at = constant("ImpNoticeRadiusCm")

    assert near_edge > notices_at, (
        f"the nearest Imp stands {near_edge} cm from the player start and the "
        f"creature notices at {notices_at} cm, so the pack charges the player "
        f"the instant the level opens.")


def test_the_pack_is_spread_widely_enough_that_they_do_not_start_inside_each_other():
    """Ten on a circle, and the arc between neighbours has to clear a body."""
    count = whole_number_property("ImpCount")
    radius = property_default("ImpPackRadiusCm")
    body = constant("ImpCapsuleRadius")

    gap = 2.0 * radius * math.sin(math.pi / count)

    assert gap > 2.0 * body, (
        f"{count} Imps on a circle of radius {radius} cm stand {gap:.0f} cm "
        f"apart and each is {2.0 * body:.0f} cm across, so they start inside "
        f"one another and the movement component has to push them apart before "
        f"anything happens.")


def test_the_pack_is_inside_the_sandbox_floor():
    floor_extent = re.search(r"^FLOOR_EXTENT\s*=\s*(\d+(?:\.\d+)?)",
                             source(LEVEL_SCRIPT), re.MULTILINE)
    assert floor_extent is not None, (
        "tools/generate_input_assets.py no longer defines FLOOR_EXTENT, so how "
        "far the sandbox floor reaches cannot be read.")

    reach = float(floor_extent.group(1)) / 2.0
    furthest = (property_default("ImpDistanceCm")
                + property_default("ImpPackRadiusCm")
                + constant("ImpCapsuleRadius"))

    assert furthest < reach, (
        f"the far edge of the pack reaches {furthest} cm and the sandbox floor "
        f"only reaches {reach} cm. Those Imps would stand over the edge, where "
        f"there is no navigation mesh and they cannot path.")


def test_the_pack_stands_clear_of_the_other_three_creatures():
    """Four creatures cannot share one line on a floor 4000 cm across."""
    pack_bearing = math.radians(property_default("ImpBearingDegrees"))
    pack_distance = property_default("ImpDistanceCm")
    pack = (pack_distance * math.cos(pack_bearing),
            pack_distance * math.sin(pack_bearing))

    hellhound_bearing = math.radians(property_default("HellhoundBearingDegrees"))
    hellhound_distance = property_default("HellhoundDistanceCm")

    others = {
        "the Brute": (property_default("BruteDistanceCm"), 0.0),
        "the Abyssal Warden": (property_default("AbyssalWardenDistanceCm"), 0.0),
        "the Hellhound": (
            hellhound_distance * math.cos(hellhound_bearing),
            hellhound_distance * math.sin(hellhound_bearing)),
    }

    # Far enough that neither creature is standing inside the other's notice
    # radius before the player has done anything.
    needed = property_default("ImpPackRadiusCm") + constant("ImpNoticeRadiusCm")

    for name, where in others.items():
        gap = math.dist(pack, where)
        assert gap > needed, (
            f"the middle of the Imp pack is {gap:.0f} cm from {name}, and the "
            f"pack is {property_default('ImpPackRadiusCm'):.0f} cm across with "
            f"a {constant('ImpNoticeRadiusCm'):.0f} cm notice radius, so they "
            f"would start fighting each other's neighbours rather than waiting "
            f"to be walked up to.")
