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
#: 300-500 ms; 0.4 sits inside that band.
#:
#: THERE USED TO BE A SECOND, LONGER ALLOWANCE OF 0.8 for a tier of telegraph
#: that had to be escaped with a Movement skill, on the grounds that the player
#: had to decide as well as react. That tier was deleted on 2026-08-09; see
#: issue #487.
REACTION_ALLOWANCE = 0.4

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


def test_the_section_states_the_wind_up_ceiling(section):
    """The ceiling replaced the second tier on 2026-08-09. Issue #487.

    WHAT IT IS FOR. Without a ceiling the wind-up formula hands the player back
    exactly as much ground as a bigger radius takes away, so the escape margin
    is the same at every radius and a bigger marker is not a harder one. The
    ceiling is what makes radius mean difficulty.
    """
    from cataclysm_sim.enemy_abilities import MAXIMUM_WIND_UP_SECONDS

    assert "the lesser of (0.4 + Radius ÷ 3.5) and 2.0" in section, (
        "the Attack Telegraphs section no longer states the wind-up rule as a "
        "formula held to a ceiling. Without the ceiling a bigger marker warns "
        "for proportionally longer and is no harder to escape, which is the "
        "defect issue #487 recorded.")

    assert str(MAXIMUM_WIND_UP_SECONDS) in section, (
        f"the section no longer names the {MAXIMUM_WIND_UP_SECONDS} second "
        "ceiling that sim/cataclysm_sim/enemy_abilities.py applies.")

    # THE CEILING SPECIFICALLY, NOT THE WORD ANYWHERE IN THE SECTION. Written
    # as `"judgement" in section` this could not fail: the section calls several
    # other figures judgements, so removing the label from the ceiling left the
    # word present and the check passed. Found by breaking it deliberately and
    # watching nothing fail.
    assert "ceiling is a judgement" in section, (
        "the section states the wind-up ceiling without saying it is a "
        "judgement. Nothing derives it and no shipped game publishes a "
        "telegraph duration to check it against, so presenting it as derived "
        "would be a claim the project cannot support.")


def test_the_section_no_longer_describes_a_second_telegraph_tier(section):
    """The deleted tier. The failure this catches is it coming back.

    It was unreachable -- the walk-out limit grows with the cooldown and its
    cap did not -- and it was more forgiving rather than harder: its escape
    margin was 13.7 m at every radius against the walk-out tier's 2.3.

    THE MENTION IN THE CLOSING PARAGRAPH IS ALLOWED and is why these match on
    the formula and the gate rather than on the words. The section records what
    was deleted and why, which is history rather than a live rule.
    """
    assert "Wind-up seconds = 0.8 + Radius ÷ 16" not in section, (
        "the Attack Telegraphs section states the deleted second-tier wind-up "
        "formula as a rule again. It was removed on 2026-08-09 by issue #487: "
        "it was reachable only for cooldowns between 5.00 and 5.36 seconds, "
        "and its escape margin was 13.7 m against the walk-out tier's 2.3, so "
        "it was never harder.")

    assert "caps at 8 metres of radius" not in section, (
        "the Attack Telegraphs section caps telegraphs at 8 metres again. The "
        "cap is now derived from the wind-up ceiling and is 6.5 m for a "
        "standard body.")


def test_the_section_says_the_cap_reaches_attacks_that_draw_no_marker(section):
    """ISSUE #500. The cap and the wind-up rule answer different questions, and
    only the wind-up one depends on a marker being drawn.

    `fits_its_cycle` in `sim/cataclysm_sim/enemy_abilities.py` enforces this. The
    prose is checked here because the code applying a rule the document does not
    state is how the two drift apart.
    """
    assert "whether or not it draws a marker" in section, (
        "the Attack Telegraphs section no longer says the cap applies to every "
        "attack rather than only to the marked ones. An attack escapes being "
        "marked by being FAST, so leaving the unmarked ones out exempts exactly "
        "the case the cap exists to forbid.")

    assert "aura held on" in section, (
        "the Attack Telegraphs section no longer states the one exemption from "
        "the cap. The Succubus's Dominion is an 8 metre field, over the 6.5 m "
        "cap, and is legal because a field that is simply on has no moment it "
        "lands. An exemption that is not written down reads as an oversight.")


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


def test_the_cap_stays_under_the_shortest_movement_skill_range(
        section, skill_rows):
    """A player who read the marker too late must still be able to cross it.

    THE CAP IS NO LONGER SET BY THIS FIGURE, and that changed on 2026-08-09.
    Until then the cap WAS the shortest Movement-shape skill range, 8 metres,
    on the argument that the largest marker should be exactly what the weakest
    escape can clear. The cap is now derived from the wind-up ceiling instead,
    which puts it at 6.5 m for a standard body -- so this checks the weaker
    property that the cap sits at or under the shortest escape, leaving a
    recovery for a player who reacted late. Issue #487.
    """
    from cataclysm_sim.enemy_abilities import telegraph_cap_metres

    ranges = []
    for row in skill_rows:
        if row.get("Shape") != "Movement":
            continue
        found = re.search(r"Range=([\d.]+)", row.get("ShapeParams", ""))
        if found:
            ranges.append(float(found.group(1)))
    assert ranges, "no Movement-shape skill in game/Data/WeaponSkills.csv has a Range"

    shortest_escape = min(ranges)
    cap = telegraph_cap_metres("Abyssal Warden")

    assert cap <= shortest_escape, (
        f"the telegraph cap is {cap:.2f} m and the shortest Movement-shape "
        f"skill range is {shortest_escape} m. A marker larger than the weakest "
        "escape leaves a player who reacted late with no way out at all, which "
        "the section calls a damage event rather than a telegraph.")

    assert "8 metres" in section and "shortest" in section, (
        "the section no longer records that the cap sits under the shortest "
        "Movement-shape skill range. Without it the 6.5 m cap reads as "
        "unrelated to what the player can actually cross.")


def test_the_movement_slot_cooldown_is_still_five_seconds(skill_rows):
    """Not a telegraph rule any more, and that is the point of keeping it.

    The 5 second Movement slot cooldown used to gate the deleted second tier of
    telegraph. That gate is gone -- no telegraph requires a Movement skill now
    -- but the figure still matters, because the section describes a Movement
    skill as the recovery available to a player who read a marker too late.
    """
    if not SKILL_SLOTS.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with SKILL_SLOTS.open(encoding="utf-8-sig", newline="") as handle:
        slots = {row["Slot"]: row for row in csv.DictReader(handle)}
    assert "Movement" in slots, (
        "game/Data/SkillSlots.csv no longer has a Movement slot. The Attack "
        "Telegraphs section names one as the player's recovery.")
    cooldown = float(slots["Movement"]["Cooldown"])
    assert cooldown == 5.0, (
        f"the Movement slot cooldown is now {cooldown} s, not 5. The Attack "
        f"Telegraphs section states that figure when describing the recovery.")


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
