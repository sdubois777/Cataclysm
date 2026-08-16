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

WHY THE C++ STILL HOLDS A COPY. There is now an enemy stat DataTable:
`game/Data/EnemyArchetypes.csv` and `game/Data/EnemyRarities.csv` are generated
from `enemy_stats.py` and imported as `DT_EnemyArchetypes` and `DT_EnemyRarities`,
which is issue #355. **Publishing the numbers is not the same as reading them.**
Nothing in the engine loads either table yet: `ACataclysmBruteCharacter` still
takes its figures from the constants in its own header, and moving it onto the
DataTable is the wiring #39 asks for.

So this test keeps its job unchanged for now. It compares the header against the
model, which is the same comparison whether the number also exists in a CSV. When
the Brute reads the table instead, these constants go away and so does this file.

UNITS DIFFER ON PURPOSE. The model works in metres and metres per second because
the design document does. Unreal works in centimetres. The factor of 100 is
applied here in the open rather than hidden on either side.

WHAT IS NOT COMPARED, AND WHY. `resistance`, `crit_chance` and `crit_multiplier`
used to be absent from the C++ and are not any more: issue #372 added them and
they are compared below. What remains absent is the three SHARES --
`health_share`, `damage_share` and `armor_share`. A share is a multiplier on a
score-scaled base, and nothing in the engine knows an encounter's Power Score, so
a figure for one declared on a C++ class would be invented rather than designed.
They reach the engine through `DT_EnemyArchetypes` instead, which is issue #355,
and whatever finally reads that table is what will use them.

The sandbox health and damage figures on `ACataclysmGameMode` are scaffolding
derived from the training dummy's numbers rather than from the model, and are
checked separately below only for the share ratio they claim to apply.
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


def gait_threshold_from_the_asset() -> float:
    """The speed ABP_Brute changes gait at, as read out of the Blueprint.

    NOT A LITERAL TYPED HERE, which is what it was until issue #430. The number
    lives inside a binary asset, so it was written down in this file on trust
    and nothing compared the two. `tools/read_animation_graph.py` now walks the
    event graph back from `Set bChasing` to the Make Literal Float feeding the
    comparison, and records the result.

    THIS FAILS RATHER THAN SKIPS when the record is missing, because a skip here
    would quietly remove the only check on the relationship this file exists to
    hold. The record is committed, so continuous integration has it even though
    it has no editor and no art.
    """
    import json

    record = REPO_ROOT / "game" / "Data" / "animation_graph_readings.json"
    if not record.is_file():
        pytest.fail(
            f"{record.relative_to(REPO_ROOT)} does not exist, so the gait "
            f"threshold inside ABP_Brute cannot be read. Regenerate it with:\n"
            f"  python tools/run_editor_python.py tools/read_animation_graph.py")

    data = json.loads(record.read_text(encoding="utf-8"))
    for graph in data.get("graphs", []):
        for entry in graph.get("event_graph_variables", []):
            if entry.get("variable") != "bChasing":
                continue
            values = list(entry.get("computed_from", {}).values())
            if len(values) == 1:
                return float(values[0])
            pytest.fail(
                f"the recorded reading of ABP_Brute gives {len(values)} numbers "
                f"for how bChasing is computed ({values}), so which one is the "
                f"gait threshold cannot be said.")

    pytest.fail(
        "the recorded reading of ABP_Brute has no bChasing in its event graph, "
        "so the gait threshold is unknown. Regenerate it with:\n"
        "  python tools/run_editor_python.py tools/read_animation_graph.py")


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


def test_chase_speed_matches_in_centimetres() -> None:
    """The speed it moves at once it has noticed you, against the model.

    A SECOND SPEED, ADDED 2026-08-07. The Brute wanders at 2.5 metres per second
    and commits at 5.0. Diablo II gives every monster both a `Velocity` and a
    `Runvelocity`, so this is an ordinary shape rather than an invention.

    WHY IT IS A DESIGN FIGURE AND NOT A TUNING VALUE: it decides whether the
    player can disengage at all, which is a statement about what fighting this
    creature is like. See the note on `chase_speed` in enemy_stats.py, including
    that since issue #391 landed the Brute is faster than the player rather than
    slower, which is issue #417.

    THIS TEST DELIBERATELY DOES NOT COMPARE IT AGAINST THE PLAYER. Doing so would
    fail today, and it would be a failure with nothing to fix: no chase speed
    satisfies both the 375 cm/s gait threshold inside ABP_Brute and being slower
    than the Ritualist's designed 350. The assertion belongs in the change that
    settles #417, not before it.
    """
    archetype = brute_archetype()

    assert constant(
        BRUTE_HEADER, "DesignedChaseSpeedCmPerSecond"
    ) == pytest.approx(archetype.chase_speed * 100.0), (
        "The Brute's chase speed in the C++ has drifted from "
        "ARCHETYPES['Brute'].chase_speed. The model is in metres per second and "
        "the engine is in centimetres per second."
    )


def test_it_commits_faster_than_it_patrols() -> None:
    """Chasing must be faster than wandering, or the second speed does nothing.

    Stated as an inequality rather than a value, so the test says what the two
    speeds are for instead of repeating them.
    """
    archetype = brute_archetype()

    assert archetype.chase_speed > archetype.move_speed, (
        "The Brute's chase speed is no faster than its patrol speed, so "
        "noticing the player changes nothing about how it moves and the "
        "four-legged chase animation is claiming speed it does not have."
    )

    chase_cm = constant(BRUTE_HEADER, "DesignedChaseSpeedCmPerSecond")
    walk_cm = constant(BRUTE_HEADER, "DesignedWalkSpeedCmPerSecond")
    assert chase_cm > walk_cm, (
        "The C++ chase speed is no faster than the C++ walk speed."
    )


def test_turn_rate_matches() -> None:
    assert constant(
        BRUTE_HEADER, "DesignedTurnRateDegreesPerSecond"
    ) == pytest.approx(brute_archetype().turn_rate_degrees), (
        "The Brute's turn rate in the C++ has drifted from "
        "ARCHETYPES['Brute'].turn_rate_degrees. This is the number that makes "
        "'can be outmanoeuvred' real, so it is not cosmetic."
    )


def test_the_defences_that_do_not_depend_on_the_encounter_match() -> None:
    """Resistance and the two crit figures, which are the same at every rarity.

    WHY THESE THREE AND NOT ARMOUR, which is the Brute's defining trait. The
    design model splits its stat block: `stats_for` scales health, damage and
    armour by the encounter's score and the enemy's rarity, and takes these
    "unchanged from the archetype". So these belong on the class and armour does
    not -- nothing in the engine knows a score, and a figure invented to stand in
    for one would look designed and be nothing of the kind. Issue #372.

    WHAT WENT WRONG BEFORE. All three were inert. The engine held three
    attributes out of roughly twenty, so a Brute had extra health, a slow walk
    and no defences at all.
    """
    kind = brute_archetype()

    for constant_name, designed in (
            ("DesignedResistancePercent", kind.resistance),
            ("DesignedCritChancePercent", kind.crit_chance),
            ("DesignedCritMultiplierPercent", kind.crit_multiplier)):
        assert constant(BRUTE_HEADER, constant_name) == pytest.approx(designed), (
            f"CataclysmBruteCharacter.h says {constant_name} is "
            f"{constant(BRUTE_HEADER, constant_name)} and "
            f"sim/cataclysm_sim/enemy_stats.py designs {designed}. The model is "
            f"authoritative.")


def test_the_brutes_crit_multiplier_is_not_the_baseline_one() -> None:
    """Otherwise the test above passes while the Brute inherits the default.

    A HIT HARDER THAN EVERY OTHER ENEMY'S IS THE DESIGN. The model gives an
    undesigned creature 150 and the Brute 200, because a slow, heavily armoured
    thing that swings rarely should hurt when it lands. If the two ever became
    the same number, the check above would still pass and the Brute would have
    quietly lost a designed trait.
    """
    import dataclasses

    from cataclysm_sim.enemy_stats import Archetype

    baseline = {field.name: field.default
                for field in dataclasses.fields(Archetype)}

    assert brute_archetype().crit_multiplier != pytest.approx(
        baseline["crit_multiplier"]), (
        f"the Brute's crit multiplier is now {brute_archetype().crit_multiplier}, "
        f"the same as the model's default for an undesigned creature. Either it "
        f"lost a designed trait, or the default moved onto it. Both make the "
        f"engine's DesignedCritMultiplierPercent a copy of something it should "
        f"be distinguishable from.")


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

    WHAT "SLOW" RESTS ON, AND WHAT IT NO LONGER RESTS ON. The archetype's role
    line is "Heavily armored slow melee. Can be outmaneuvered", and until
    2026-08-09 three properties carried it: the Brute moved more slowly than a
    generic enemy, turned more slowly, and swung more slowly. The project owner
    settled the swing at 1.2 seconds by playing it, against the generic default
    of 1.5, so it now swings FASTER than anything unconfigured. That was
    deliberate and it is recorded in docs/DECISIONS.md.

    So this asserts the two properties that still carry the role, and asserts
    the third in the direction it was deliberately moved -- which is a guard,
    not a hole: putting the swing back above the default would fail here and
    send whoever did it to the reasoning.
    """
    enemy_header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                    / "CataclysmEnemyCharacter.h")

    base_interval = uproperty_default(enemy_header, "AttackIntervalSeconds")
    brute_interval = constant(BRUTE_HEADER, "DesignedAttackIntervalSeconds")

    assert brute_interval < base_interval, (
        f"A Brute swings every {brute_interval} seconds against the generic "
        f"enemy default of {base_interval}, so it is no longer the faster of "
        f"the two. If that is intended, the decision on 2026-08-09 that made it "
        f"faster has been reversed and docs/DECISIONS.md needs to say so. If it "
        f"is not intended, sim/cataclysm_sim/enemy_stats.py is authoritative."
    )

    # THE ORDINARY ENEMY'S TURN RATE IS A LITERAL IN ITS CONSTRUCTOR, not a
    # header default like the others, so it is read out of the source rather
    # than through uproperty_default.
    enemy_cpp = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                 / "CataclysmEnemyCharacter.cpp")
    turn = re.search(
        r"RotationRate\s*=\s*FRotator\(\s*[\d.]+f\s*,\s*([\d.]+)f",
        enemy_cpp.read_text(encoding="utf-8"))
    if turn is None:
        pytest.fail(
            "CataclysmEnemyCharacter.cpp no longer sets RotationRate as an "
            "FRotator literal, so the ordinary enemy's turn rate cannot be read "
            "here. If it moved to a named constant, read that instead.")
    base_turn = float(turn.group(1))
    brute_turn = constant(BRUTE_HEADER, "DesignedTurnRateDegreesPerSecond")

    assert brute_turn < base_turn, (
        f"A Brute turns at {brute_turn} degrees per second against an ordinary "
        f"enemy's {base_turn}. Turn rate is what 'can be outmaneuvered' means: "
        f"a player circling at the Brute's own reach sweeps 223 degrees per "
        f"second even in the slowest class, so anything at or above that cannot "
        f"be got behind."
    )

    base_reach = uproperty_default(enemy_header, "MeleeReachCm")
    brute_reach = constant(BRUTE_HEADER, "DesignedMeleeReachCm")

    assert brute_reach < base_reach, (
        "A Brute reaches at least as far as an ordinary enemy. Its reach is "
        "contact reach by design, which is shorter."
    )


def test_the_notice_radius_lets_the_sandbox_brute_be_seen_wandering() -> None:
    """The one property of the notice radius that is arithmetic, not taste.

    THE FIGURE ITSELF IS NOT DERIVED, and this test does not pretend otherwise.
    1000 cm was set by playing on 2026-08-07. It replaced 700, which WAS derived
    -- `move_speed x attack_interval` -- and that derivation stopped being true
    the same day, twice: the attack interval moved to 1.6, and the Brute now
    chases at 500 rather than the 250 the arithmetic used.

    WHAT IS STILL CHECKABLE is the thing the number has to do. The sandbox spawns
    the Brute `BruteDistanceCm` from the player start, so a notice radius at or
    above that distance means it notices the player as the level opens, never
    wanders, and the whole roaming behaviour is invisible. That was the state at
    the inherited 1500 and it is the fault worth guarding against.

    Issue #383 asks for the general rule across all seven enemies. Until it is
    answered this is a play-tested figure with one hard bound.
    """
    notice = constant(BRUTE_HEADER, "BruteNoticeRadiusCm")
    spawn = uproperty_default(GAME_MODE_HEADER, "BruteDistanceCm")

    assert notice < spawn, (
        f"The Brute notices from {notice:.0f} cm and the sandbox spawns it "
        f"{spawn:.0f} cm from the player start, so it notices the player the "
        "instant the level opens, never wanders, and none of its roaming can "
        "be seen. That is exactly the state the inherited 1500 produced."
    )

    reach = constant(BRUTE_HEADER, "DesignedMeleeReachCm")
    assert notice > reach * 2.0, (
        "The Brute's notice radius is close to its own reach, so it could be "
        "walked up to and stood beside without ever waking up."
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


def _locomotion_row(animation: str) -> str:
    """The locomotion table row for one animation in the asset reference."""
    import re as _re

    reference = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
    if not reference.is_file():
        pytest.fail(f"{reference.relative_to(REPO_ROOT)} does not exist")

    row = _re.search(rf"^\|\s*`{_re.escape(animation)}`\s*\|.*$",
                     reference.read_text(encoding="utf-8"), _re.M)
    if row is None:
        pytest.fail(
            f"game/docs/enemy-source-assets.md has no {animation} row in its "
            "locomotion table. If the table moved, update this test; if the row "
            "was deleted, the C++ constant is unrecorded again."
        )
    return row.group(0)


def _play_rate_in_the_header(animation: str) -> float:
    """The play rate the Brute's header records for one gait animation.

    WHY THE HEADER AND NOT THE CODE. Until 2026-08-08 the rate was computed in
    C++ from a constant this test could read. It is now baked into two sequence
    player nodes inside `game/Content/Enemies/Demonic/Brute/ABP_Brute.uasset`,
    which is binary and which nothing here can open. The header records both
    figures in a comment so that they stay in reviewable text; this reads that
    comment.

    THE ASSET IS CHECKED TOO, in a different file. This function pins the
    comment against the design model; `test_animation_graph_reading_is_current.py`
    pins the same comment against a reading taken out of `ABP_Brute` itself, and
    pins that reading against the asset's SHA-256 so it cannot describe an older
    graph. The three together run from the design model to the bytes on disk.

    That was not true when this was written. Issue #406 said the figures were
    "no longer numbers a reviewer can see or a test can read", and at the time
    they were not; the reader that changed it arrived with issue #408.
    """
    import re as _re

    text = BRUTE_HEADER.read_text(encoding="utf-8")
    found = _re.search(rf"{animation}\s+([\d.]+)", text)
    if found is None:
        pytest.fail(
            f"CataclysmBruteCharacter.h no longer records a play rate for "
            f"{animation}. It should be in the comment above MinimumPlayRate. "
            f"If the animation Blueprint stopped using that clip, update this "
            f"test; if the comment was deleted, the figure is now recorded "
            f"nowhere in text at all."
        )
    return float(found.group(1))


def _authored_speed_in_the_document(animation: str) -> float:
    """The bold in-use figure from the locomotion table for one animation.

    The unbold figure beside it is `tools/measure_animation_stride.py`'s own
    estimate, which is deliberately allowed to differ.
    """
    import re as _re

    row = _locomotion_row(animation)
    in_use = _re.search(r"\*\*([\d.]+) cm/s\*\*", row)
    if in_use is None:
        pytest.fail(
            f"The {animation} row records no figure in use, which should be "
            f"the bold one. The row reads: {row}"
        )
    return float(in_use.group(1))


def test_the_chase_play_rate_matches_the_recorded_figure() -> None:
    """The chase gait's play rate is the one its authored speed implies.

    WHY THIS EXISTS. Getting it wrong is what the project owner saw. The chase
    animation `Jog_Quad_Fwd` is treated as authored for 350 cm/s, set by eye,
    and the Brute chases at 500. The first version shipped with the authored
    speed left at the Brute's own speed, so the play rate was 1.0 and the feet
    travelled 22% further than the ground -- reported as "it is like he isn't
    moving far enough within the animation".

    WHAT IT DOES NOT CHECK. That 350 is right; whether it looks right is judged
    by watching.

    THAT ABP_BRUTE CARRIES THE RATE IS CHECKED ELSEWHERE, by
    `test_animation_graph_reading_is_current.py`. This test is the link from the
    design model to the header comment; that file holds the two links from the
    comment to the asset's own bytes.
    """
    documented = _authored_speed_in_the_document("Jog_Quad_Fwd")
    recorded = _play_rate_in_the_header("Jog_Quad_Fwd")

    chase_speed = brute_archetype().chase_speed * 100.0
    expected = chase_speed / documented

    assert recorded == pytest.approx(expected, rel=1e-4), (
        f"CataclysmBruteCharacter.h says ABP_Brute plays the chase gait at "
        f"{recorded}, but the Brute chases at {chase_speed} cm/s and "
        f"game/docs/enemy-source-assets.md records the clip as authored for "
        f"{documented} cm/s, which is {expected:.6f}. One of the three moved "
        f"without the others, and the visible result is sliding feet."
    )

    minimum = constant(BRUTE_HEADER, "MinimumPlayRate")
    maximum = constant(BRUTE_HEADER, "MaximumPlayRate")
    assert minimum < recorded < maximum, (
        f"The chase animation plays at {recorded:.2f}, outside the {minimum} "
        f"to {maximum} the Brute treats as usable, so it is either frozen or a "
        f"blur."
    )


def test_the_walk_play_rate_matches_the_recorded_figure() -> None:
    """The wandering gait's play rate is the one its authored speed implies.

    WHY THIS EXISTS. The authored walk speed has been wrong twice: first as an
    outright guess of 500, then as 373.7 from a measuring script using an
    estimator that was 66% high. The value in use now, 225, was set by the
    project owner watching the creature walk. A guess, a bad measurement and a
    good one all look identical once they are a play rate.

    WHAT IT DOES NOT CHECK. That 225 is right. Only that the header, the
    document and the designed speed agree; that ABP_Brute really carries the
    rate is the other half of the chain, held in
    `test_animation_graph_reading_is_current.py`.
    """
    documented = _authored_speed_in_the_document("Jog_Biped_Fwd")
    recorded = _play_rate_in_the_header("Jog_Biped_Fwd")

    wander_speed = brute_archetype().move_speed * 100.0
    expected = wander_speed / documented

    assert recorded == pytest.approx(expected, rel=1e-4), (
        f"CataclysmBruteCharacter.h says ABP_Brute plays the wandering gait at "
        f"{recorded}, but the Brute wanders at {wander_speed} cm/s and "
        f"game/docs/enemy-source-assets.md records the clip as authored for "
        f"{documented} cm/s, which is {expected:.6f}. Make all three agree, and "
        f"if the value changed because somebody re-judged it by eye, say so in "
        f"the document."
    )

    minimum = constant(BRUTE_HEADER, "MinimumPlayRate")
    maximum = constant(BRUTE_HEADER, "MaximumPlayRate")
    assert minimum < recorded < maximum, (
        f"At the Brute's designed {wander_speed} cm/s the walk plays at "
        f"{recorded:.2f}, outside the {minimum} to {maximum} range the class "
        f"treats as usable, so the number is wrong."
    )


def test_the_gait_threshold_sits_between_the_two_designed_speeds() -> None:
    """ABP_Brute picks the gait by speed, so the two speeds must straddle it.

    WHY THIS IS A TEST. Choosing the gait moved out of C++ and into ABP_Brute on
    2026-08-08. The graph reads the creature's own ground speed and compares it
    against 375 cm/s: below that it plays the two-legged wandering jog, above it
    the four-legged chase. That works only because the Brute really does move at
    two different speeds. If the wander and chase speeds were ever tuned to the
    same side of 375, the Brute would use one gait for both states and nothing
    else would notice.

    THE THRESHOLD IS READ OUT OF THE ASSET NOW, not written here as a literal.
    Until issue #430 it was a 375.0 typed into this file, believed because
    somebody had once opened the graph and looked at it; changing the comparison
    inside ABP_Brute would have left this and everything else passing.
    `tools/read_animation_graph.py` walks the event graph back from `Set
    bChasing` to the Make Literal Float feeding the comparison, and records what
    it finds.
    """
    gait_threshold_in_animation_blueprint = gait_threshold_from_the_asset()

    kind = brute_archetype()
    wander = kind.move_speed * 100.0
    chase = kind.chase_speed * 100.0

    assert wander < gait_threshold_in_animation_blueprint, (
        f"The Brute wanders at {wander} cm/s, which is at or above the "
        f"{gait_threshold_in_animation_blueprint} cm/s threshold ABP_Brute "
        f"switches gait at, so it would drop onto all fours to patrol."
    )
    assert chase > gait_threshold_in_animation_blueprint, (
        f"The Brute chases at {chase} cm/s, which is at or below the "
        f"{gait_threshold_in_animation_blueprint} cm/s threshold ABP_Brute "
        f"switches gait at, so noticing the player would not change its "
        f"posture."
    )


# ---------------------------------------------------------------------------
# How hard the rock throw's animation has to be compressed
#
# WHY THESE EXIST. Issue #416. The rock does not leave the creature's hand until
# 1.672 seconds into its montage and the telegraph allows 1.000, so
# ACataclysmBruteCharacter::MontageRateFor plays the whole rip and throw at 1.67
# times its authored speed. Nothing rejects that -- it is inside the 2.5 ceiling
# the class treats as usable -- and nothing recorded it either.
#
# WHAT THEY DO AND DO NOT SETTLE. They do not say whether 1.67 looks right; that
# needs somebody to watch it. They make any change to either input recompute the
# consequence and fail until the recorded figure is updated with it, so the
# decision cannot be made by accident.
# ---------------------------------------------------------------------------


def _rock_throw_release_in_the_document() -> float:
    """The moment the throwing hand reaches the top of its arc, from the doc.

    The bold figure in the `Ability_RipNToss_Toss` row of the locomotion
    reference. Held in two places on purpose -- there and on the C++ constant --
    because the C++ is what the game runs on and the document is what a person
    reads, and this repository has had those drift before.
    """
    import re as _re

    row = _locomotion_row("Ability_RipNToss_Toss")
    found = _re.search(r"\*\*([\d.]+) s\*\*", row)
    if found is None:
        pytest.fail(
            "The Ability_RipNToss_Toss row in game/docs/enemy-source-assets.md "
            "no longer records when the rock is released. It should be the bold "
            "figure. Without it the measured 0.539 s exists only in the C++."
        )
    return float(found.group(1))


def _clip_length_in_the_document(animation: str) -> float:
    """The Length column of one row of the locomotion reference."""
    row = _locomotion_row(animation)
    cells = [c.strip() for c in row.strip().strip("|").split("|")]
    if len(cells) < 2:
        pytest.fail(f"the {animation} row has no Length column: {row}")
    try:
        return float(cells[1])
    except ValueError:
        pytest.fail(f"the {animation} row's Length is not a number: {row}")


def test_the_rock_throw_wind_up_is_the_designed_telegraph() -> None:
    """The same rule the stomp is held to, which nothing was holding this to.

    docs/Cataclysm_GDD_v2.md gives the wind-up as 0.4 + Radius / 3.5, where 0.4
    is the reaction allowance and 3.5 m/s is the slowest class's walk speed. The
    stomp has been pinned against that since the stun was built and the rock
    throw never was, which is how a telegraph could be lengthened to suit an
    animation without anybody having to argue for it.

    Issue #416 proposes exactly that change. This test is what makes it a
    decision rather than an edit: lengthening the telegraph fails here, and the
    fix is to change the design rule and this test together, deliberately.
    """
    from cataclysm_sim.enemy_abilities import REACTION_ALLOWANCE, WALK_OUT_SPEED

    radius = brute_ability("Rip and Toss").params["Radius"]
    designed = REACTION_ALLOWANCE + radius / WALK_OUT_SPEED

    assert constant(BRUTE_HEADER, "RockThrowWindUpSeconds") == pytest.approx(
        designed
    ), (
        f"The rock throw telegraphs for a different length of time in the C++ "
        f"than the wind-up rule gives for its {radius} m radius, which is "
        f"{designed:.2f} seconds. If this was changed on purpose to fit the "
        f"animation -- issue #416 -- then the rule needs an exception written "
        f"down and this test needs to check the exception instead."
    )


def test_the_release_moment_is_recorded_in_both_places() -> None:
    """The C++ constant and the asset document must agree about the measurement.

    Neither can be derived from the asset: the Paragon clips carry no animation
    notifies, so the only record of when the rock leaves the hand is this
    measured figure, written down twice.
    """
    assert constant(
        BRUTE_HEADER, "RockThrowStrikeIntoReleaseSeconds"
    ) == pytest.approx(_rock_throw_release_in_the_document()), (
        "CataclysmBruteCharacter.h and game/docs/enemy-source-assets.md "
        "disagree about when the throwing hand reaches the top of its arc. "
        "Re-measure with tools/measure_animation_impact.py and update both."
    )


def test_the_recorded_compression_is_what_the_two_inputs_give() -> None:
    """The play rate the document states must follow from the wind-up and the
    release, so changing either forces the consequence to be written down.

    THE FAILURE THIS EXISTS FOR is lengthening the telegraph, or re-measuring
    the release, and leaving the document saying the throw still plays at 1.67.
    The number a reader would then use to judge whether it looks hurried would be
    the old one.
    """
    import re as _re

    reference = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
    if not reference.is_file():
        pytest.fail(f"{reference.relative_to(REPO_ROOT)} does not exist")

    stated = _re.search(r"a play rate of \*\*([\d.]+)\*\*",
                        reference.read_text(encoding="utf-8"))
    if stated is None:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer states the play rate "
            "the throw montage is compressed to. Issue #416 is about that "
            "figure, so it has to be written down somewhere a person reads."
        )
    recorded = float(stated.group(1))

    rip = _clip_length_in_the_document("Ability_RipNToss_Rip")
    release = constant(BRUTE_HEADER, "RockThrowStrikeIntoReleaseSeconds")
    wind_up = constant(BRUTE_HEADER, "RockThrowWindUpSeconds")

    expected = (rip + release) / wind_up

    # A hundredth, because the clip lengths in the document are recorded to two
    # decimal places and the real rip clip is 1.1333 seconds.
    assert recorded == pytest.approx(expected, abs=0.01), (
        f"game/docs/enemy-source-assets.md says the throw montage plays at "
        f"{recorded}, but its rip clip is {rip} s, the rock leaves the hand "
        f"{release} s into the clip after that, and the telegraph allows "
        f"{wind_up} s -- which is {expected:.3f}. One of the three moved without "
        f"the others."
    )


def test_the_compression_is_within_what_the_class_treats_as_usable() -> None:
    """A play rate above the ceiling would be rejected at runtime and clamped.

    MontageRateFor clamps to MaximumPlayRate, so a montage needing more
    compression than that does not play faster -- it silently stops arriving
    when the damage does, and the animation and the damage come apart again.
    That is the failure this catches, and 1.672 against a ceiling of 2.5 is
    closer to it than it looks: any telegraph shorter than 0.67 seconds would
    cross it.
    """
    rip = _clip_length_in_the_document("Ability_RipNToss_Rip")
    release = constant(BRUTE_HEADER, "RockThrowStrikeIntoReleaseSeconds")
    wind_up = constant(BRUTE_HEADER, "RockThrowWindUpSeconds")
    needed = (rip + release) / wind_up

    ceiling = constant(BRUTE_HEADER, "MaximumPlayRate")

    assert needed <= ceiling, (
        f"The rock throw would have to play at {needed:.2f} to fit its "
        f"{wind_up} s telegraph, and MontageRateFor clamps at {ceiling}. The "
        f"montage would be clamped, so the rock would leave the hand after the "
        f"projectile had already been fired."
    )


# ---------------------------------------------------------------------------
# The Stomp
#
# EVERY ONE OF THESE WAS UNGUARDED UNTIL THE STUN WAS BUILT. The four Stomp
# constants have been in the header since PR #394 in exactly the right shape for
# the reader above, and nothing compared them to anything. The stun is the fifth
# and the reason the rest are now pinned: it is the first Brute number whose
# being wrong costs the player control of the character rather than health.
# ---------------------------------------------------------------------------


SKILL_EFFECTS_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm"
                        / "AbilitySystem" / "CataclysmSkillEffects.h")


def brute_ability(name: str):
    """One entry of ABILITIES['Brute'] by name."""
    from cataclysm_sim.enemy_abilities import abilities

    for ability in abilities("Brute"):
        if ability.name == name:
            return ability
    raise AssertionError(
        f"sim/cataclysm_sim/enemy_abilities.py has no Brute ability named "
        f"{name!r}. It holds: "
        f"{[a.name for a in abilities('Brute')]}"
    )


def test_the_stomp_stun_matches_the_model() -> None:
    stun = brute_ability("Stomp").params["StunSeconds"]

    assert constant(BRUTE_HEADER, "StompStunSeconds") == pytest.approx(stun), (
        "The Brute's stomp holds the player for a different length of time in "
        "the C++ than ABILITIES['Brute'] in sim/cataclysm_sim/enemy_abilities.py "
        "designs, and the Python is authoritative."
    )


def test_the_stomp_radius_matches_the_model_in_centimetres() -> None:
    radius = brute_ability("Stomp").params["Radius"]

    assert constant(BRUTE_HEADER, "StompRadiusCm") == pytest.approx(
        radius * 100.0
    ), (
        "The stomp's ring is a different size in the C++ than the model "
        "designs. It is also what the telegraph's wind-up is computed from, so "
        "a disagreement here makes the wind-up wrong as well as the damage."
    )


def test_the_stomp_cooldown_matches_the_model() -> None:
    assert constant(BRUTE_HEADER, "StompCooldownSeconds") == pytest.approx(
        brute_ability("Stomp").cooldown
    ), (
        "The stomp comes round at a different rate in the C++ than the model "
        "designs."
    )


def test_the_rock_throw_cooldown_matches_the_model() -> None:
    """The other half of the pair, which had no check at all until 2026-08-09.

    HOW THAT WAS FOUND. Issue #452 changed both cooldowns, and the guard-proving
    run for it broke the C++ copy of this one on purpose to watch a test notice.
    Nothing did. The stomp's copy was checked by the test above and the rock
    throw's was not, so the C++ could carry any figure at all and only a person
    reading both files would see it.
    """
    assert constant(BRUTE_HEADER, "RockThrowCooldownSeconds") == pytest.approx(
        brute_ability("Rip and Toss").cooldown
    ), (
        "The rock throw comes round at a different rate in the C++ than the "
        "model designs. sim/cataclysm_sim/enemy_abilities.py is authoritative."
    )


def test_the_stomp_wind_up_is_the_designed_telegraph() -> None:
    """The wind-up is a formula, not a taste, so it is checked as one.

    docs/Cataclysm_GDD_v2.md:3016 gives the rule as

        Wind-up seconds = 0.4 + Radius / 3.5

    where 0.4 is the reaction allowance and 3.5 m/s is the slowest class's walk
    speed. The forward form is written nowhere in sim/, only the rearranged cap,
    so it is composed here from the two constants the model does hold.
    """
    from cataclysm_sim.enemy_abilities import REACTION_ALLOWANCE, WALK_OUT_SPEED

    radius = brute_ability("Stomp").params["Radius"]
    designed = REACTION_ALLOWANCE + radius / WALK_OUT_SPEED

    assert constant(BRUTE_HEADER, "StompWindUpSeconds") == pytest.approx(
        designed
    ), (
        f"The stomp telegraphs for a different length of time in the C++ than "
        f"the wind-up rule gives for its {radius} m radius, which is "
        f"{designed:.2f} seconds. The rule is the player's guarantee that the "
        f"ring can be walked out of, so this is not a number to tune by eye."
    )


def test_the_stomp_damage_is_its_slot() -> None:
    """Damage is the slot, not a number on the ability."""
    from cataclysm_sim.character import SKILL_SLOTS

    slot = brute_ability("Stomp").slot

    assert constant(BRUTE_HEADER, "StompDamagePercent") == pytest.approx(
        SKILL_SLOTS[slot].typical_damage
    ), (
        f"The stomp deals a different percentage in the C++ than the {slot} "
        f"slot's typical damage in sim/cataclysm_sim/character.py. Nothing in "
        f"ABILITIES stores a damage figure, because the slot IS the figure."
    )


def test_the_stomp_cooldown_clears_the_stun_immunity_window() -> None:
    """Why the cooldown is not a figure from the Heavy band.

    The whole Heavy band in game/Data/SkillSlots.csv is 1 to 4 seconds, and all
    of it sits inside the 5 second stun immunity window. A stomp coming round
    sooner than the window would be refused by the window rather than limited by
    its slot, so the window is the floor and the slot is not.

    AT OR ABOVE, RATHER THAN EQUAL TO, SINCE 2026-08-09. This asserted equality
    until then, because the cooldown was exactly the window. The project owner
    raised it to 8 seconds by playing it, which is legal: a stomp arriving later
    than the window is refused by nothing. Going under is what is not legal, so
    that is what this checks now.
    """
    from cataclysm_sim.enemy_abilities import STUN_IMMUNITY_WINDOW

    cooldown = constant(BRUTE_HEADER, "StompCooldownSeconds")

    assert cooldown >= STUN_IMMUNITY_WINDOW, (
        f"The stomp's cooldown is {cooldown} seconds, under the "
        f"{STUN_IMMUNITY_WINDOW} second stun immunity window. The creature will "
        f"attempt stuns the window silently refuses, and the stomp's rate ends "
        f"up set by something other than what the design says sets it."
    )

    assert cooldown == pytest.approx(brute_ability("Stomp").cooldown), (
        "The stomp's cooldown in the C++ has drifted from the one in "
        "ABILITIES['Brute'] in sim/cataclysm_sim/enemy_abilities.py. The model "
        "is authoritative; copy it rather than editing the C++ alone."
    )


def test_the_engine_carries_the_same_stun_rules_as_the_model() -> None:
    """The two anti-stun-lock numbers the engine enforces.

    sim/cataclysm_sim/damage.py is the model these are ported from. It says in
    terms that it resolves one hit with no clock and that the GAME enforces the
    immunity window, so the C++ is not a second copy of a working
    implementation -- it is the only implementation, checked against the design
    figures the model holds.
    """
    from cataclysm_sim import damage

    assert constant(
        SKILL_EFFECTS_HEADER, "StunDamageThresholdPercent"
    ) == pytest.approx(damage.STUN_DAMAGE_THRESHOLD), (
        "The share of maximum health a hit must take to stun has drifted from "
        "STUN_DAMAGE_THRESHOLD in sim/cataclysm_sim/damage.py."
    )

    assert constant(
        SKILL_EFFECTS_HEADER, "StunImmunityWindowSeconds"
    ) == pytest.approx(damage.STUN_IMMUNITY_SECONDS), (
        "The stun immunity window has drifted from STUN_IMMUNITY_SECONDS in "
        "sim/cataclysm_sim/damage.py."
    )


def test_the_two_copies_of_the_immunity_window_in_the_model_agree() -> None:
    """The model holds the same five seconds twice, and nothing compared them.

    `STUN_IMMUNITY_WINDOW` in enemy_abilities.py sets every stunning ability's
    cooldown; `STUN_IMMUNITY_SECONDS` in damage.py is the rule itself. They are
    two independent copies of one design figure in a repository whose CLAUDE.md
    records that copies here have silently drifted twice before.
    """
    from cataclysm_sim import damage
    from cataclysm_sim.enemy_abilities import STUN_IMMUNITY_WINDOW

    assert STUN_IMMUNITY_WINDOW == pytest.approx(damage.STUN_IMMUNITY_SECONDS), (
        "sim/cataclysm_sim/enemy_abilities.py and sim/cataclysm_sim/damage.py "
        "disagree about how long stun immunity lasts."
    )


def test_no_stun_outlasts_the_immunity_it_grants() -> None:
    """Otherwise the window stops nothing.

    A stun longer than its own immunity window would leave the target eligible
    for the next stun before it had finished recovering from the last, which is
    the stun-lock the rule exists to prevent.
    """
    from cataclysm_sim import damage

    assert constant(BRUTE_HEADER, "StompStunSeconds") < constant(
        SKILL_EFFECTS_HEADER, "StunImmunityWindowSeconds"
    ), (
        "The Brute's stomp holds the player for at least as long as the "
        "immunity it grants, so a second Brute could take over the instant the "
        "first let go."
    )
    assert damage.INCIDENTAL_STUN_SECONDS < damage.STUN_IMMUNITY_SECONDS, (
        "The incidental blunt-weapon stun outlasts the immunity window."
    )


# --------------------------------------------------------------------------
# How far off a creature may be pointed and still throw
# --------------------------------------------------------------------------

CONTROLLER_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                     / "CataclysmEnemyController.h")


def test_the_facing_tolerance_keeps_a_thrown_rock_on_its_target() -> None:
    """A creature may start a directional attack while slightly off-target, and
    "slightly" has to be small enough that the attack still covers what it was
    aimed at.

    WHY THIS IS CHECKED HERE AS WELL AS IN THE ENGINE.
    `Cataclysm.AI.TheFacingToleranceCoversEveryDirectionalAbility` makes the
    same comparison against every ability of every enemy and is the better test,
    and it needs the engine. `.github/workflows/ci.yml` is a single Linux job
    that builds no C++ at all, so nothing checks this on a pull request. This
    reads the same three numbers out of the headers as text.

    THE GEOMETRY. At a range R with a marked half-width W, being off by an angle
    A displaces the marked area sideways by R * sin(A), so the target leaves it
    once that exceeds W. Issue #457.
    """
    import math

    tolerance = constant(CONTROLLER_HEADER, "FacingToleranceDegrees")
    reach = constant(BRUTE_HEADER, "RockThrowRangeCm")
    half_width = constant(BRUTE_HEADER, "RockThrowRadiusCm")

    drift = reach * math.sin(math.radians(tolerance))

    assert drift <= half_width, (
        f"a creature may be up to {tolerance} degrees off its target before it "
        f"has to turn, which at the rock throw's {reach} cm range moves the "
        f"thrown rock {drift:.1f} cm sideways. The marked area is only "
        f"{half_width} cm wide, so the rock would land outside the ground "
        f"marker that warned about it. Lower FacingToleranceDegrees in "
        f"game/Source/Cataclysm/Character/CataclysmEnemyController.h to at most "
        f"{math.degrees(math.asin(half_width / reach)):.1f}."
    )


def test_the_facing_tolerance_is_not_zero() -> None:
    """Zero would mean a creature that turns for ever.

    A creature turns a whole number of degrees per frame and its target moves
    between frames, so "pointed exactly at it" is a condition that comes true
    only by coincidence. A tolerance of zero would leave a Brute turning on the
    spot and never throwing, which reads as the creature being broken rather
    than as a number being wrong.
    """
    assert constant(CONTROLLER_HEADER, "FacingToleranceDegrees") > 0.0, (
        "FacingToleranceDegrees is zero or negative, so no creature can ever "
        "satisfy it and no directional ability will ever be used."
    )


# --------------------------------------------------------------------------
# The ability cooldowns can be judged by playing
# --------------------------------------------------------------------------

BRUTE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmBruteCharacter.cpp")


def test_the_ability_table_reads_the_cooldown_overrides() -> None:
    """The console variables must reach the decision, not just exist.

    WHAT CAN GO WRONG SILENTLY. `ACataclysmEnemyController::IsAbilityReady`
    reads whatever `EnemyAbilities()` puts in `CooldownSeconds`. An accessor that
    honours the console variable, and an ability table that fills itself from the
    constant instead, both compile and run. The console command is accepted, the
    variable holds the new value, and the creature carries on at its designed
    cooldown. Issue #452.

    WHY IT IS CHECKED HERE. There is an engine test that sets the variables and
    reads the table back, and it is the better test.
    `.github/workflows/ci.yml` is a single Linux job that builds no C++, so
    nothing checks this on a pull request. This is a text comparison.
    """
    text = BRUTE_CPP.read_text(encoding="utf-8")

    for ability, accessor in (
        ("Stomp", "StompCooldownSecondsInUse"),
        ("RockThrow", "RockThrowCooldownSecondsInUse"),
    ):
        assert re.search(
            rf"{ability}\.CooldownSeconds\s*=\s*{accessor}\(\)\s*;", text
        ), (
            f"CataclysmBruteCharacter.cpp fills {ability}.CooldownSeconds from "
            f"something other than {accessor}(), so setting "
            f"Cataclysm.Brute.{'StompCooldown' if ability == 'Stomp' else 'RockThrowCooldown'} "
            "would change nothing. Nothing reports an error: the console "
            "accepts the value and the creature ignores it."
        )


def test_an_unset_cooldown_override_means_the_designed_figure() -> None:
    """Zero must mean "use the design", never a cooldown of zero.

    Every console override on this creature defaults to zero and treats zero as
    "not set". Reading one straight through would give an ability with no
    cooldown at all, which is always ready, and the creature would do nothing but
    use abilities. Six existing tests fail when this is broken, which is what
    says the convention is load-bearing rather than decorative.
    """
    text = BRUTE_CPP.read_text(encoding="utf-8")

    for accessor, constant in (
        ("StompCooldownSecondsInUse", "StompCooldownSeconds"),
        ("RockThrowCooldownSecondsInUse", "RockThrowCooldownSeconds"),
    ):
        body = re.search(
            rf"float ACataclysmBruteCharacter::{accessor}\(\) const\s*\{{(.*?)\n\}}",
            text, re.S)
        assert body is not None, (
            f"CataclysmBruteCharacter.cpp no longer defines {accessor}."
        )
        assert re.search(r"Override\s*>\s*0\.0f\s*\?", body.group(1)), (
            f"{accessor} does not test its override against zero before using "
            f"it, so an unset console variable would be read as a cooldown of "
            f"zero and {constant} would never apply. That makes the ability "
            f"always ready."
        )
