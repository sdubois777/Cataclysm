"""The Attack Telegraphs section of docs/Cataclysm_GDD_v2.md, checked against
the numbers it is derived from.

WHY THIS EXISTS. Issue #347, the first child of the enemy design epic #29. The
design document said twice that the player "must read and dodge telegraphed enemy
attacks" and nowhere said what a telegraph is. Six sibling issues each ask "what
is this enemy's telegraph", and they would each have invented a different answer.

WHY IT IS TESTABLE AT ALL. The section does not assert its numbers; it derives
them. Every figure in it comes from somewhere else in the project:

    the walk-out speed, 3.5 m/s  the slowest of the three Demonic classes, from
                                 the class stat table in the same document
    the escape ranges            the ten Movement-shape skills in
                                 game/Data/WeaponSkills.csv, shortest is 8 m
    the Movement cooldown, 5 s   game/Data/SkillSlots.csv
    the attack intervals         ARCHETYPES in sim/cataclysm_sim/enemy_stats.py
    the shape names              the Shape column of game/Data/WeaponSkills.csv

So the per-enemy table can be recomputed rather than compared to a copy, and a
change to any of those sources fails here instead of leaving the document quietly
wrong. That is the same shape of check as
`tools/tests/test_enemy_score_formula.py`, which closes the same gap between the
power model and the section that describes it.

THE ONE NUMBER THAT IS NOT DERIVED is the 0.4 second reaction allowance, and the
0.8 second one for the larger telegraphs. Those come from published reaction time
figures rather than from anything in this project, so they are pinned here as
constants and the document is checked for stating them. If either changes, this
file is where the change is noticed.

WHAT IS ASSERTED HERE.

    the section exists and states both wind-up formulas
    the walk-out speed is the slowest class in the document's own stat table
    the per-enemy largest-radius table matches a recomputation from
      sim/cataclysm_sim/enemy_stats.py, enemy by enemy
    the Imp and the Hellhound are excluded, and the reason given is the sub-metre
      marker rather than a judgement about those creatures
    every shape the telegraph table names is a real Shape in the skill data
    the 8 metre cap is the shortest Movement-shape skill range in the skill data
    the 5 second gate is the Movement slot cooldown in the skill slot data
    the five rules about what happens during a wind-up are all present
"""

from __future__ import annotations

import csv
import math
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
SKILL_SLOTS = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"

SECTION = "## **Attack Telegraphs**"
NEXT_SECTION = "## **Enemy Modifiers**"

#: Not derived from anything in this project. Published simple visual reaction
#: time is 200-250 ms and reaction to a new on-screen stimulus is measured at
#: 300-500 ms; 0.4 sits inside that band. The larger telegraphs use 0.8 because
#: the player must also decide whether to spend a cooldown.
REACTION_ALLOWANCE = 0.4
DECISION_ALLOWANCE = 0.8

#: A marker smaller than this is smaller than the creature standing in it, so
#: there is nowhere to walk. It is what excludes the two swarm enemies.
SMALLEST_USEFUL_MARKER_METRES = 1.0


def unwrapped(text: str) -> str:
    return " ".join(text.split())


@pytest.fixture(scope="module")
def section() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    text = GDD.read_text(encoding="utf-8")
    start = text.find(SECTION)
    assert start != -1, (
        f"docs/Cataclysm_GDD_v2.md has no {SECTION} section. It is the only "
        f"place that says what a telegraph is, and six enemy design issues "
        f"depend on it. Issue #347.")
    end = text.find(NEXT_SECTION, start)
    return unwrapped(text[start:end if end != -1 else len(text)])


@pytest.fixture(scope="module")
def archetypes() -> dict:
    from cataclysm_sim.enemy_stats import ARCHETYPES
    return ARCHETYPES


@pytest.fixture(scope="module")
def skill_rows() -> list[dict]:
    if not WEAPON_SKILLS.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    with WEAPON_SKILLS.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def walk_out_speed() -> float:
    """The slowest class, read out of the document's own stat table rather than
    hard-coded here. The whole wind-up formula rests on it."""
    text = GDD.read_text(encoding="utf-8")
    row = re.search(r"\| Movement Speed \|([^\n]*)\|", text)
    assert row, (
        "the Three Demonic Class Stat Lines table in docs/Cataclysm_GDD_v2.md "
        "no longer has a Movement Speed row. The telegraph wind-up formula is "
        "derived from the slowest class in it.")
    speeds = [float(cell) for cell in row.group(1).split("|") if cell.strip()]
    return min(speeds)


# --------------------------------------------------------------------------
# The two formulas, and what they are made of
# --------------------------------------------------------------------------

def test_the_section_states_the_walk_out_wind_up_formula(section):
    """Without the formula the section is an opinion. With it, every enemy
    issue gets the same answer without asking."""
    assert "Wind-up seconds = 0.4 + Radius ÷ 3.5" in section, (
        "the Attack Telegraphs section no longer states the walk-out wind-up "
        "formula. It is 0.4 + Radius / 3.5: a reaction allowance plus the time "
        "the slowest class needs to walk clear. Issue #347.")


def test_the_walk_out_speed_is_the_slowest_class_in_the_stat_table(section):
    """3.5 is not a chosen number. It is the Ritualist, the slowest of the
    three Demonic classes, and the point of using it is that every class can
    then clear every telegraph rather than only the fast ones."""
    slowest = walk_out_speed()
    assert math.isclose(slowest, 3.5), (
        f"the slowest Demonic class now moves at {slowest}, not 3.5. The "
        f"telegraph wind-up formula in the Attack Telegraphs section is "
        f"derived from that figure and has to be re-derived. Issue #347.")
    assert "3.5 metres per second is the slowest class" in section, (
        "the section no longer says WHY the wind-up formula uses 3.5. It is "
        "the slowest class, so that every class clears every telegraph.")


def test_the_section_states_the_reaction_allowance_and_that_it_is_not_derived(
        section):
    """The one figure in the section that comes from outside the project. It is
    worth flagging as such, because everything around it is derived and a reader
    would otherwise assume this is too."""
    assert f"{REACTION_ALLOWANCE} seconds is the reaction allowance" in section, (
        "the section no longer states the reaction allowance. It is 0.4 "
        "seconds and it is the only figure here that is not derived from "
        "something else in the project.")
    assert "200 to 250 milliseconds" in section and "300 to 500 milliseconds" in section, (
        "the section states a reaction allowance without the measured figures "
        "it sits between. Simple visual reaction is 200-250 ms and reaction to "
        "a new on-screen stimulus is 300-500 ms. Without those the 0.4 reads "
        "as invented.")


def test_the_section_states_the_movement_skill_wind_up_formula(section):
    """The second tier, added because the player escapes with movement
    abilities as well as by walking. A boss area larger than a walk can clear
    is legitimate; it just costs a cooldown."""
    assert "Wind-up seconds = 0.8 + Radius ÷ 16" in section, (
        "the Attack Telegraphs section no longer states the wind-up formula "
        "for telegraphs that need a Movement skill. It is 0.8 + Radius / 16.")
    assert f"{DECISION_ALLOWANCE} rather than {REACTION_ALLOWANCE}" in section, (
        "the section no longer says why the larger telegraphs get a longer "
        "allowance. The player has to decide whether to spend a cooldown, not "
        "only react.")


# --------------------------------------------------------------------------
# The per-enemy table, recomputed rather than compared to a copy
# --------------------------------------------------------------------------

def largest_walk_out_radius(attack_interval: float, speed: float) -> float:
    """The document's own rearrangement: an attack is telegraphed when its
    wind-up fits inside HALF the attack interval."""
    return speed * (attack_interval / 2 - REACTION_ALLOWANCE)


def test_the_per_enemy_table_matches_the_enemy_stat_model(section, archetypes):
    """THE CHECK THIS FILE EXISTS FOR. The table of largest telegraphed radius
    per enemy is a derivation from the attack intervals in
    sim/cataclysm_sim/enemy_stats.py. Nothing else compares the two, so an
    interval changed in the model would leave the document silently wrong --
    which is exactly the failure tools/tests/test_enemy_score_formula.py was
    written to close one layer up."""
    speed = walk_out_speed()
    checked = 0
    for name, archetype in archetypes.items():
        if name == "Baseline":
            continue
        row = re.search(
            rf"\| {re.escape(name)} \| ([\d.]+) s \| ([\d.]+) m \|", section)
        assert row, (
            f"the Attack Telegraphs table has no row for {name}, which is one "
            f"of the seven archetypes in sim/cataclysm_sim/enemy_stats.py. "
            f"Every enemy needs a stated answer or its design issue will "
            f"invent one.")
        stated_interval = float(row.group(1))
        stated_radius = float(row.group(2))

        assert math.isclose(stated_interval, archetype.attack_interval), (
            f"{name}: the section says its attack interval is "
            f"{stated_interval} s, the model says {archetype.attack_interval}. "
            f"sim/cataclysm_sim/enemy_stats.py is authoritative.")

        # The document states the radius to one decimal place, so the check is
        # that the stated figure is within rounding distance of the true one.
        # Comparing against round(expected, 1) instead would fail on exact
        # halves: round(3.15, 1) is 3.1, because 3.15 has no exact float.
        expected = largest_walk_out_radius(archetype.attack_interval, speed)
        assert abs(stated_radius - expected) < 0.06, (
            f"{name}: the section says its largest telegraphed radius is "
            f"{stated_radius} m. Recomputed from its {archetype.attack_interval} "
            f"s attack interval at {speed} m/s with a {REACTION_ALLOWANCE} s "
            f"reaction allowance, it is {expected:.2f} m.")
        checked += 1

    assert checked == 7, (
        f"only {checked} of the seven vertical slice enemies were checked. "
        f"Either the model gained an archetype the document does not cover, or "
        f"the table's format changed and these rows stopped matching.")


def test_the_two_swarm_enemies_are_excluded_by_the_marker_size_rule(
        section, archetypes):
    """The result that matters most, and the one that has to come from the rule
    rather than from taste. Section X already asserts that an Imp is not
    individually a threat and a pack is. An Imp that telegraphed would be
    individually dangerous, so the rule has to produce the exclusion by itself.

    It does: at a 0.9 second attack interval the largest area it could telegraph
    is 0.2 metres, which is smaller than the creature standing in it."""
    speed = walk_out_speed()
    for name in ("Imp", "Hellhound"):
        radius = largest_walk_out_radius(
            archetypes[name].attack_interval, speed)
        assert radius < SMALLEST_USEFUL_MARKER_METRES, (
            f"{name} can now telegraph a {radius:.2f} m area, which is above "
            f"the {SMALLEST_USEFUL_MARKER_METRES} m floor. The section says it "
            f"cannot telegraph at all, and the whole swarm design in section X "
            f"rests on that. Either the attack interval changed in "
            f"sim/cataclysm_sim/enemy_stats.py or the formula did.")

    assert "A marker smaller than 1 metre is not a telegraph" in section, (
        "the section excludes the Imp and the Hellhound without stating the "
        "rule that excludes them. The rule is that a sub-metre marker is "
        "smaller than the creature standing in it. Without it the exclusion "
        "reads as a judgement about those two creatures and the next enemy "
        "added has no answer.")


def test_the_exclusion_is_tied_back_to_the_swarm_design(section):
    """A derived result agreeing with an asserted one is worth writing down,
    because it is the evidence that the formula is not arbitrary."""
    # Lowercased on both sides: the section quotes section X, which capitalises
    # "Common", and the quotation could reasonably be re-cased.
    assert "a single common enemy is not the threat, a pack is" in section.lower(), (
        "the section no longer connects the Imp's exclusion to the swarm "
        "design in section X. That agreement is the main evidence that the "
        "telegraph rule is the right one.")


# --------------------------------------------------------------------------
# The section reuses what the game already has, rather than inventing
# --------------------------------------------------------------------------

def test_every_telegraph_shape_is_a_real_shape_in_the_skill_data(
        section, skill_rows):
    """The section deliberately adds no shape vocabulary of its own: a
    telegraphed enemy attack is an ordinary attack in one of the shapes the
    skill system already executes, shown on the ground first. If the two lists
    drift apart, an enemy ability becomes something the skill runtime cannot
    run."""
    real = {row["Shape"] for row in skill_rows if row.get("Shape")}
    for shape in ("Strike", "Projectile", "Aura", "Movement"):
        assert f"| {shape} |" in section, (
            f"the telegraph shape table no longer lists {shape}.")
        assert shape in real, (
            f"the telegraph shape table lists {shape}, which is not a Shape "
            f"used by any row of game/Data/WeaponSkills.csv. The section's "
            f"whole point is that telegraphs reuse the existing shapes rather "
            f"than adding a parallel set.")


def test_the_eight_metre_cap_is_the_shortest_movement_skill_range(
        section, skill_rows):
    """The cap on the larger telegraphs, and the same argument as using the
    slowest walking speed: design against the shortest escape so that every
    build can make it, rather than only the long-ranged ones."""
    ranges = []
    for row in skill_rows:
        if row.get("Shape") != "Movement":
            continue
        found = re.search(r"Range=([\d.]+)", row.get("ShapeParams", ""))
        if found:
            ranges.append(float(found.group(1)))
    assert ranges, "no Movement-shape skill in game/Data/WeaponSkills.csv has a Range"
    assert min(ranges) == 8, (
        f"the shortest Movement-shape skill range is now {min(ranges)} m, not "
        f"8. The Attack Telegraphs section caps a movement-skill telegraph at "
        f"8 metres because that is the shortest escape any build has, so the "
        f"cap has to move with it.")
    assert "caps at 8 metres of radius" in section, (
        "the section no longer caps the larger telegraphs. Without a cap an "
        "area can be marked that nothing the player has can escape, which is a "
        "damage event with a warning rather than a telegraph.")


def test_the_five_second_gate_is_the_movement_slot_cooldown(section):
    """A large telegraph on a short cooldown tests whether the player's escape
    happens to be up, not whether they read the marker."""
    if not SKILL_SLOTS.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with SKILL_SLOTS.open(encoding="utf-8-sig", newline="") as handle:
        slots = {row["Slot"]: row for row in csv.DictReader(handle)}
    assert "Movement" in slots, (
        "game/Data/SkillSlots.csv no longer has a Movement slot. The whole "
        "larger-telegraph tier assumes the player has one.")
    cooldown = float(slots["Movement"]["Cooldown"])
    assert cooldown == 5.0, (
        f"the Movement slot cooldown is now {cooldown} s, not 5. The Attack "
        f"Telegraphs section gates the larger telegraphs on that figure.")
    assert "cooldown of at least 5 seconds" in section, (
        "the section no longer gates the larger telegraphs on a minimum "
        "cooldown. Without it, two large areas can land with the player's "
        "escape still recharging.")


# --------------------------------------------------------------------------
# What a wind-up actually does
# --------------------------------------------------------------------------

@pytest.mark.parametrize("rule, claim", [
    ("the area does not follow the player",
     "The area is fixed when the wind-up starts"),
    ("the player is not locked out of acting",
     "The player can do anything during it"),
    ("dodging avoids the attack completely rather than reducing it",
     "Leaving the area avoids the attack completely"),
    ("the enemy is committed once it starts",
     "The enemy is committed once the wind-up starts"),
    ("crowd control can cancel it",
     "Interrupting the enemy cancels the attack"),
])
def test_the_section_states_what_happens_during_a_wind_up(section, rule, claim):
    """Five rules, and each one closes a way the telegraph could be made
    pointless. The third is the load-bearing one: partial damage on a successful
    dodge makes ignoring telegraphs and stacking mitigation correct, which is
    the behaviour the whole section exists to prevent."""
    assert claim in section, (
        f"the Attack Telegraphs section no longer states that {rule}. "
        f"Issue #347.")


def test_the_section_answers_the_dense_pack_readability_problem(section):
    """The failure the genre is worst at, and the one #29 and #347 both name.
    The answer is not a separate mechanism: the enemies that come in packs
    cannot telegraph, because their attack intervals are too short."""
    assert "Twenty markers on screen at once" in section, (
        "the section no longer addresses telegraph readability in a dense "
        "fight. The Imp pack maths in section X guarantees twenty enemies on "
        "screen, and it is the genre's best-known telegraph failure.")
    assert "A pack cannot fill the screen with markers" in section, (
        "the section raises the dense-pack problem without stating the answer. "
        "The answer falls out of the same rule: the swarm enemies are excluded "
        "by their own attack speed, so no marker comes from a pack.")
