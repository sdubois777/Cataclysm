"""The Brute's C++ constants must agree with the simulation.

`game/Source/Cataclysm/Character/CataclysmBruteCharacter.h` hard-codes four
numbers that also live in the Python model. Two copies of the same number drift,
and in this repository they have: `sim/cataclysm_sim/scoring.py` silently drifted
from its own source twice, which is why CLAUDE.md carries a rule about it.

WHICH IS AUTHORITATIVE. The Python. `ARCHETYPES["Brute"]` in
`sim/cataclysm_sim/enemy_stats.py` and `ATTACK_REACH["Brute"]` in
`sim/cataclysm_sim/enemy_abilities.py` are where the Brute is designed and tuned.
When this test fails, the usual fix is to change the C++ to match, not the other
way round.

WHY THE C++ HOLDS A COPY AT ALL. There is no enemy stat DataTable. Nothing
carries `enemy_stats.py` into the engine: `game/Data/` has fourteen CSVs and none
of them is about enemy statistics, and the generator has no handler for one.
Building that pipeline is issue #355 and is a change of its own. Until it lands,
the engine needs the numbers written down somewhere, and a second copy guarded by
a test is the shape this repository already uses for class stats and affixes.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second because
the design document does. Unreal works in centimetres. The factor of 100 is
applied here in the open rather than hidden on either side.

WHAT IS NOT COMPARED, AND WHY. `health_share`, `damage_share`, `armor_share`,
`resistance`, `crit_chance` and `crit_multiplier` are all designed and all absent
from the C++. `ACataclysmEnemyCharacter::ApplyStartingAttributes` writes only
maximum health, current health and attack damage, so armour, resistance and crit
have nowhere to go: issue #372. The sandbox health and damage figures on
`ACataclysmGameMode` are scaffolding derived from the training dummy's numbers
rather than from the model, and are checked separately below only for the share
ratio they claim to apply.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")

sys.path.insert(0, str(REPO_ROOT / "sim"))


def brute_archetype():
    from cataclysm_sim.enemy_stats import ARCHETYPES

    return ARCHETYPES["Brute"]


def brute_reach_metres() -> float:
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    return ATTACK_REACH["Brute"]


def constant(header: pathlib.Path, name: str) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    if not header.is_file():
        pytest.fail(f"{header.relative_to(REPO_ROOT)} does not exist")

    text = header.read_text(encoding="utf-8")
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        text,
    )
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, this guard has "
            f"nothing left to check and the number is unguarded."
        )
    return float(match.group(1))


def uproperty_default(header: pathlib.Path, name: str) -> float:
    """The value of a `float <name> = <number>f;` property default."""
    if not header.is_file():
        pytest.fail(f"{header.relative_to(REPO_ROOT)} does not exist")

    text = header.read_text(encoding="utf-8")
    match = re.search(
        rf"\bfloat\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;", text)
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'float {name} = <number>f;' line.")
    return float(match.group(1))


def test_attack_interval_matches() -> None:
    assert constant(BRUTE_HEADER, "DesignedAttackIntervalSeconds") == pytest.approx(
        brute_archetype().attack_interval
    ), (
        "The Brute's attack interval in the C++ has drifted from "
        "ARCHETYPES['Brute'].attack_interval in "
        "sim/cataclysm_sim/enemy_stats.py, which is authoritative."
    )


def test_walk_speed_matches_in_centimetres() -> None:
    assert constant(
        BRUTE_HEADER, "DesignedWalkSpeedCmPerSecond"
    ) == pytest.approx(brute_archetype().move_speed * 100.0), (
        "The Brute's walk speed in the C++ has drifted from "
        "ARCHETYPES['Brute'].move_speed. The model is in metres per second and "
        "the engine is in centimetres per second."
    )


def test_turn_rate_matches() -> None:
    assert constant(
        BRUTE_HEADER, "DesignedTurnRateDegreesPerSecond"
    ) == pytest.approx(brute_archetype().turn_rate_degrees), (
        "The Brute's turn rate in the C++ has drifted from "
        "ARCHETYPES['Brute'].turn_rate_degrees. This is the number that makes "
        "'can be outmanoeuvred' real, so it is not cosmetic."
    )


def test_melee_reach_matches_in_centimetres() -> None:
    assert constant(BRUTE_HEADER, "DesignedMeleeReachCm") == pytest.approx(
        brute_reach_metres() * 100.0
    ), (
        "The Brute's melee reach in the C++ has drifted from "
        "ATTACK_REACH['Brute'] in sim/cataclysm_sim/enemy_abilities.py."
    )


def test_the_capsule_radius_is_the_designed_body_radius() -> None:
    """The engine capsule and the model's body radius are the same number.

    This is the one that makes contact reach work. The model derives the Brute's
    reach as PLAYER_BODY_RADIUS + body_radius, so if the engine's capsule is not
    the model's body radius then the reach is a distance the Brute cannot stand
    at, and it chases the player for ever without attacking.
    """
    from cataclysm_sim.enemy_abilities import PLAYER_BODY_RADIUS

    archetype = brute_archetype()
    capsule_radius_cm = constant(BRUTE_HEADER, "BruteCapsuleRadius")

    assert capsule_radius_cm == pytest.approx(archetype.body_radius * 100.0), (
        "The Brute's collision capsule is not the body radius the model uses. "
        "The Rampage mesh is much wider than this on purpose -- its own physics "
        "asset models the torso as an 82 cm sphere -- but the collision must "
        "match the model or the reach arithmetic below stops holding. See "
        "issue #366."
    )

    reach_cm = constant(BRUTE_HEADER, "DesignedMeleeReachCm")
    player_radius_cm = PLAYER_BODY_RADIUS * 100.0

    assert reach_cm == pytest.approx(capsule_radius_cm + player_radius_cm), (
        "The Brute's reach is no longer exactly the two capsule radii, so it is "
        "no longer contact reach. Either it can now hit from outside touching "
        "distance, or it can never close far enough to hit at all."
    )


def test_the_sandbox_figures_apply_the_designed_shares() -> None:
    """Sandbox health and damage are the dummy's times the Brute's shares.

    Not the designed absolute figures -- those come from the enemy score model,
    which has no port into the engine (#355). What is guarded is that the
    scaffolding still scales by the ratio the design gives, so a Brute stays as
    much tougher than a training dummy as it is supposed to be.
    """
    archetype = brute_archetype()

    dummy_health = uproperty_default(GAME_MODE_HEADER, "TrainingDummyHealth")
    dummy_damage = uproperty_default(GAME_MODE_HEADER, "TrainingDummyAttackDamage")
    brute_health = uproperty_default(GAME_MODE_HEADER, "BruteHealth")
    brute_damage = uproperty_default(GAME_MODE_HEADER, "BruteAttackDamage")

    assert brute_health == pytest.approx(dummy_health * archetype.health_share), (
        "The sandbox Brute's health is no longer the training dummy's health "
        "times ARCHETYPES['Brute'].health_share."
    )
    assert brute_damage == pytest.approx(dummy_damage * archetype.damage_share), (
        "The sandbox Brute's attack damage is no longer the training dummy's "
        "damage times ARCHETYPES['Brute'].damage_share."
    )


def test_the_brute_is_slower_than_the_enemy_defaults_it_overrides() -> None:
    """A Brute that matched the base enemy would not be a Brute.

    The C++ automation test asserts this against a live actor. This asserts it
    against the source, so it fails in continuous integration, which never opens
    the editor and so never runs the automation tests.
    """
    enemy_header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                    / "CataclysmEnemyCharacter.h")

    base_interval = uproperty_default(enemy_header, "AttackIntervalSeconds")
    brute_interval = constant(BRUTE_HEADER, "DesignedAttackIntervalSeconds")

    assert brute_interval > base_interval, (
        "A Brute attacks no less often than an ordinary enemy, so nothing about "
        "it reads as slow."
    )

    base_reach = uproperty_default(enemy_header, "MeleeReachCm")
    brute_reach = constant(BRUTE_HEADER, "DesignedMeleeReachCm")

    assert brute_reach < base_reach, (
        "A Brute reaches at least as far as an ordinary enemy. Its reach is "
        "contact reach by design, which is shorter."
    )


def test_the_notice_radius_is_one_attack_cycle_of_walking() -> None:
    """The Brute's notice radius is derived, and this is the derivation.

    WHAT IT IS. The distance the Brute covers in one attack cycle, so that
    noticing the player leads to a fight rather than to a walk it never
    finishes: `move_speed x attack_interval`, 2.5 m/s x 2.8 s = 7 m = 700 cm.

    WHY IT IS GUARDED HERE RATHER THAN ONLY IN C++. Both inputs come from
    `ARCHETYPES['Brute']`, so a change to either should move the notice radius
    with it. Continuous integration never opens the editor and so never runs the
    automation tests; without this the derivation would be a claim in a comment.

    WHAT IT DOES NOT CHECK. That one attack cycle is the right rule. Nothing in
    `docs/Cataclysm_GDD_v2.md` states a notice radius for any enemy, and this
    derivation uses two numbers only the Brute has, so it says nothing about the
    other six. Issue #383 asks for the general rule.
    """
    archetype = brute_archetype()
    expected_cm = archetype.move_speed * 100.0 * archetype.attack_interval

    assert constant(BRUTE_HEADER, "BruteNoticeRadiusCm") == pytest.approx(
        expected_cm
    ), (
        "The Brute's notice radius is no longer one attack cycle of walking. "
        "It is derived from ARCHETYPES['Brute'].move_speed and .attack_interval, "
        "so if one of those changed, this should have moved with it."
    )


def test_the_brute_notices_later_than_the_enemy_default() -> None:
    """Shrinking the notice radius was the point, so the inequality is the test.

    The base enemy's 1500 cm is itself derived -- `docs/DECISIONS.md` records it
    as the longest range a designed Demonic skill reaches -- but that is a rule
    for a caster. A melee enemy moving at 2.5 m/s against a player moving at 3.5
    to 4.6 cannot close a fifteen metre gap against a player who does not want it
    closed, so it would start a chase that never ends.
    """
    enemy_header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                    / "CataclysmEnemyCharacter.h")

    base_notice = uproperty_default(enemy_header, "NoticeRadiusCm")
    brute_notice = constant(BRUTE_HEADER, "BruteNoticeRadiusCm")

    assert brute_notice < base_notice, (
        "A Brute notices from at least as far as an ordinary enemy, so the "
        "change that shrank it has been undone."
    )

    brute_reach = constant(BRUTE_HEADER, "DesignedMeleeReachCm")
    assert brute_notice > brute_reach * 2.0, (
        "The Brute's notice radius is close to its own reach, so it could be "
        "walked up to and stood beside without ever waking up."
    )


def test_the_brute_roams_less_far_than_it_sees_and_stays_on_the_floor() -> None:
    """Two bounds on the roam radius, both of them arithmetic rather than taste.

    SMALLER THAN THE NOTICE RADIUS, or a Brute could wander off the ground it is
    meant to be holding and have no way of noticing that it had.

    INSIDE THE NAVIGATION BOUNDS. The sandbox floor is `FLOOR_EXTENT` cm across
    centred on the world origin, so nothing can path further than half of that
    from the centre. `ACataclysmGameMode` puts the Brute `BruteDistanceCm` out,
    and Recast insets the walkable surface by the agent radius, which for this
    character is its capsule radius. What is left is the headroom the roam radius
    has to fit inside.

    All four inputs are read from the files that define them, so moving the
    spawn distance or the floor size fails this rather than silently producing a
    Brute that tries to walk off the world.
    """
    import re as _re

    notice = constant(BRUTE_HEADER, "BruteNoticeRadiusCm")
    roam = constant(BRUTE_HEADER, "BruteRoamRadiusCm")

    assert roam < notice, (
        "The Brute roams further from its anchor than it can see, so it can "
        "leave the area it is guarding without knowing."
    )

    generator = REPO_ROOT / "tools" / "generate_input_assets.py"
    if not generator.is_file():
        pytest.fail(f"{generator.relative_to(REPO_ROOT)} does not exist")

    floor_match = _re.search(
        r"^FLOOR_EXTENT\s*=\s*([\d.]+)", generator.read_text(encoding="utf-8"), _re.M)
    if floor_match is None:
        pytest.fail(
            "tools/generate_input_assets.py has no FLOOR_EXTENT line. It is what "
            "sizes both the sandbox floor and the navigation bounds, so without "
            "it the roam radius has nothing to be checked against."
        )
    floor_extent_cm = float(floor_match.group(1))

    spawn_distance_cm = uproperty_default(GAME_MODE_HEADER, "BruteDistanceCm")
    capsule_radius_cm = constant(BRUTE_HEADER, "BruteCapsuleRadius")

    headroom_cm = floor_extent_cm / 2.0 - spawn_distance_cm - capsule_radius_cm

    assert roam < headroom_cm, (
        f"A Brute spawned {spawn_distance_cm:.0f} cm from the centre of a "
        f"{floor_extent_cm:.0f} cm floor has only {headroom_cm:.0f} cm to the "
        f"navigation bounds once the {capsule_radius_cm:.0f} cm agent inset is "
        f"taken off, but it roams up to {roam:.0f} cm. Roam targets would fall "
        "outside the navigation mesh, where the fallback walks it in a straight "
        "line off the floor."
    )


def test_roaming_is_opt_in_so_nothing_else_starts_wandering() -> None:
    """The base class must not roam, or every enemy in the project does.

    THE REGRESSION THIS EXISTS FOR. `RoamRadiusCm` is a virtual on
    `ACataclysmCharacterBase` with a default of zero, and that default is the
    only thing keeping a summoned imp from strolling away from the fight it was
    summoned into, and a Corrupted Sentinel -- designed stationary -- from
    walking about. Four automation tests assert that a character with nothing in
    sight is Idle; they pass unedited only because of this default.

    The automation tests check it against live actors. This checks the source, so
    it fails in continuous integration, which never opens the editor.
    """
    base_header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                   / "CataclysmCharacterBase.h")
    if not base_header.is_file():
        pytest.fail(f"{base_header.relative_to(REPO_ROOT)} does not exist")

    text = base_header.read_text(encoding="utf-8")
    match = re.search(
        r"virtual\s+float\s+RoamRadiusCm\s*\(\s*\)\s*const\s*"
        r"\{\s*return\s+(-?\d+(?:\.\d+)?)f\s*;\s*\}",
        text,
    )
    if match is None:
        pytest.fail(
            "game/Source/Cataclysm/Character/CataclysmCharacterBase.h no longer "
            "has an inline 'virtual float RoamRadiusCm() const { return <n>f; }'. "
            "If it moved to a .cpp, this guard cannot see it any more and "
            "roaming being opt-in is unguarded in continuous integration."
        )

    assert float(match.group(1)) == 0.0, (
        "ACataclysmCharacterBase::RoamRadiusCm no longer defaults to zero, so "
        "every character the enemy controller drives now roams -- including "
        "summoned imps, which should stay with the fight."
    )


def test_the_walk_play_rate_matches_the_recorded_figure() -> None:
    """The authored walk speed in the C++ matches the figure written down.

    WHY THIS EXISTS. `AuthoredWalkSpeedCmPerSecond` decides how fast the Brute's
    walk animation plays. It has been wrong twice: first as an outright guess of
    500, then as 373.7 from a measuring script using an estimator that was 66%
    high. The value in use now, 225, was set by the project owner watching the
    creature walk.

    A guess, a bad measurement and a good one all look identical in the source.
    This ties the number to the one recorded in `game/docs/enemy-source-assets.md`,
    the reference listing which art plays each enemy and what was measured from
    it, so changing one without the other fails rather than quietly bringing foot
    sliding back.

    WHAT IT DOES NOT CHECK. That 225 is right. Only that the C++ and the document
    agree, and that the resulting play rate is sane. Whether it looks right is a
    judgement made by watching, and the console variable
    `Cataclysm.Brute.AuthoredWalkSpeed` is how that is done.
    """
    import re as _re

    reference = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
    if not reference.is_file():
        pytest.fail(f"{reference.relative_to(REPO_ROOT)} does not exist")

    text = reference.read_text(encoding="utf-8")

    # The locomotion table's Jog_Biped_Fwd row. The figure in use is the bold one;
    # the unbold one beside it is the script's estimate, which is deliberately
    # allowed to differ.
    row = _re.search(r"^\|\s*`Jog_Biped_Fwd`\s*\|.*$", text, _re.M)
    if row is None:
        pytest.fail(
            "game/docs/enemy-source-assets.md has no Jog_Biped_Fwd row in its "
            "locomotion table. If the table moved, update this test; if it was "
            "deleted, the C++ constant is unrecorded again."
        )
    in_use = _re.search(r"\*\*([\d.]+) cm/s\*\*", row.group(0))
    if in_use is None:
        pytest.fail(
            "The Jog_Biped_Fwd row records no figure in use, which should be the "
            f"bold one. The row reads: {row.group(0)}"
        )
    documented = float(in_use.group(1))

    in_code = uproperty_default(BRUTE_HEADER, "AuthoredWalkSpeedCmPerSecond")

    assert in_code == pytest.approx(documented), (
        f"CataclysmBruteCharacter.h plays the walk as though it were authored "
        f"for {in_code} cm/s, but game/docs/enemy-source-assets.md records "
        f"{documented} cm/s as the figure in use. Make both agree, and if the "
        f"value changed because somebody re-judged it by eye, say so there."
    )

    # The play rate that falls out of it has to be usable. Outside the clamp the
    # animation is either frozen or a blur, and the number is wrong.
    designed_speed_cm = brute_archetype().move_speed * 100.0
    play_rate = designed_speed_cm / in_code
    minimum = constant(BRUTE_HEADER, "MinimumPlayRate")
    maximum = constant(BRUTE_HEADER, "MaximumPlayRate")

    assert minimum < play_rate < maximum, (
        f"At the Brute's designed {designed_speed_cm} cm/s the walk would play "
        f"at {play_rate:.2f}, which is outside the {minimum} to {maximum} range "
        f"the class clamps to, so the clamp would be hiding a wrong number."
    )
