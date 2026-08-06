"""The Vertical Slice Enemy Behaviour section of docs/Cataclysm_GDD_v2.md,
checked against the numbers it is derived from.

WHY THIS EXISTS. Issue #348, the Imp, which is the second child of the enemy
design epic #29 to be worked. The seven Demonic enemies have had complete stat
blocks in `sim/cataclysm_sim/enemy_stats.py` for some time and none of them had
an ability, so nothing said what any of them does.

WHAT IS DERIVED RATHER THAN ASSERTED. Every figure in the Imp's design comes from
somewhere else in the project, so the section can be recomputed instead of
compared against a copy:

    the Imp's body radius, 0.30 m   `body_radius` on the Imp archetype in
                                    sim/cataclysm_sim/enemy_stats.py
    the player's body radius, 0.42  CapsuleRadius in game/Source/Cataclysm/
                                    Character/CataclysmPlayerCharacter.cpp
    the rank capacities 7 and 13    computed from those two by
                                    enemy_abilities.ring_capacity
    the reach, 1.32 m               the second rank's distance
    the pack, 10                    already stated in section X of the design
                                    document as the pack that kills in 4.9 s
    "not telegraphed"               the wind-up rule in the Attack Telegraphs
                                    subsection, applied to the 0.9 s attack
                                    interval in enemy_stats.py
    the class movement speeds       the class stat table in the same document

THE TWO SOURCES THAT ARE READ AS TEXT are the two C++ capsule radii. They are
constants in `.cpp` files rather than data, so this file parses them out and
fails if either moves. That is the drift this project has had before: a copied
number changing at one end only.

WHAT IS ASSERTED HERE.

    the section and the Imp subsection exist
    the ability table's Shape and parameters match ABILITIES exactly
    the ring table's three distances and three capacities are recomputed
    the reach is the second rank's distance, and reaching it admits exactly 20
    the two C++ capsule radii still match the figures the section quotes
    the Imp is stated as not telegraphed, and the wind-up rule agrees
    the attack-token alternative is named and refused
    the Imp outruns every one of the three Demonic classes
    the pack figure matches the one section X already states
    an enemy with no designed abilities raises rather than returning nothing
"""

from __future__ import annotations

import math
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
PLAYER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmPlayerCharacter.cpp")
MINION_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmMinion.cpp")

SECTION = "## **Vertical Slice Enemy Behaviour**"
NEXT_SECTION = "# **XI. Cataclysm Quest Mechanics**"
IMP_SUBSECTION = "### **The Imp**"

#: Centimetres in a metre. The design document works in metres and the C++
#: constants are in centimetres.
CM_PER_METRE = 100.0


def unwrapped(text: str) -> str:
    return " ".join(text.split())


def cpp_constant(path: pathlib.Path, name: str) -> float:
    """The value of a `constexpr float <name> = <number>f;` line."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8", errors="replace")
    found = re.search(
        rf"constexpr\s+float\s+{re.escape(name)}\s*=\s*([0-9.]+)f?\s*;", text)
    assert found, (
        f"{path.name} no longer declares a constexpr float named {name}. The "
        f"Vertical Slice Enemy Behaviour section derives the Imp's rank sizes "
        f"from it. Issue #348.")
    return float(found.group(1))


@pytest.fixture(scope="module")
def section() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    text = GDD.read_text(encoding="utf-8")
    start = text.find(SECTION)
    assert start != -1, (
        f"docs/Cataclysm_GDD_v2.md has no {SECTION} section. It is the only "
        "place that says what any of the seven vertical slice enemies does. "
        "Issue #348.")
    end = text.find(NEXT_SECTION, start)
    return unwrapped(text[start:end if end != -1 else len(text)])


@pytest.fixture(scope="module")
def imp_section() -> str:
    text = GDD.read_text(encoding="utf-8")
    start = text.find(IMP_SUBSECTION)
    assert start != -1, (
        f"docs/Cataclysm_GDD_v2.md has no {IMP_SUBSECTION} subsection. "
        "Issue #348.")
    end = text.find(NEXT_SECTION, start)
    return unwrapped(text[start:end if end != -1 else len(text)])


@pytest.fixture(scope="module")
def imp():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Imp")


@pytest.fixture(scope="module")
def imp_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Imp")


# --------------------------------------------------------------------------
# The section is there and it says what the Imp does
# --------------------------------------------------------------------------

def test_the_section_says_an_enemy_ability_uses_the_player_skill_columns(section):
    """The point of the section. If enemy abilities get their own format then
    the engine needs a second executor and the telegraph marker cannot be drawn
    from the ability's own numbers."""
    assert "game/Data/WeaponSkills.csv" in section, (
        "the Vertical Slice Enemy Behaviour section no longer says that an "
        "enemy ability is written in the same Shape and parameter columns as a "
        "player skill, in game/Data/WeaponSkills.csv. Issue #348.")
    assert "sim/cataclysm_sim/enemy_abilities.py" in section, (
        "the section no longer names sim/cataclysm_sim/enemy_abilities.py as "
        "the machine-readable copy, so a reader cannot find the data.")


def test_the_section_says_only_the_imp_is_designed(section):
    """Six of the seven are still open issues, and a section that reads as
    complete would let one of them be skipped."""
    assert "only the imp is designed" in section.lower(), (
        "the Vertical Slice Enemy Behaviour section no longer says that only "
        "the Imp is designed. Six of the seven enemies have no abilities and "
        "the section must not read as though they do. Issue #348.")


def test_the_imp_ability_table_matches_the_data(imp_section, imp_abilities):
    """The document and sim/cataclysm_sim/enemy_abilities.py must not drift.
    The data is what an engine reads; the document is what a person reads."""
    assert len(imp_abilities) == 1, (
        f"the Imp now has {len(imp_abilities)} abilities in "
        "sim/cataclysm_sim/enemy_abilities.py. The design document states it "
        "has exactly one and gives two reasons. Update both together.")
    ability = imp_abilities[0]
    assert f"| {ability.name} | {ability.shape} |" in imp_section, (
        f"the Imp's ability table does not carry a row for {ability.name} as a "
        f"{ability.shape}, which is what ABILITIES holds.")
    for key, value in ability.params.items():
        text = f"{key}={value}"
        assert text in imp_section, (
            f"the Imp's ability table does not state {text}, which is in its "
            "params in sim/cataclysm_sim/enemy_abilities.py.")


def test_the_imp_has_exactly_one_ability_and_the_document_says_why(imp_section):
    """A one-ability enemy read as an unfinished one would be redesigned by
    whoever picked it up next."""
    lowered = imp_section.lower()
    assert "one attack and nothing else" in lowered, (
        "the Imp subsection no longer states that one attack is the whole "
        "design rather than an omission. Issue #348.")
    assert "multiplied by the pack" in lowered, (
        "the Imp subsection no longer gives the pack-multiplication reason for "
        "refusing a second ability.")


# --------------------------------------------------------------------------
# The ring maths, recomputed
# --------------------------------------------------------------------------

def test_the_two_body_radii_still_match_the_cpp(imp):
    """Both figures the ring table rests on live in C++ constants. A change at
    that end and not this one is exactly the drift this file exists to catch."""
    from cataclysm_sim.enemy_abilities import PLAYER_BODY_RADIUS

    player_cm = cpp_constant(PLAYER_CPP, "CapsuleRadius")
    assert player_cm / CM_PER_METRE == pytest.approx(PLAYER_BODY_RADIUS), (
        f"CapsuleRadius in {PLAYER_CPP.name} is {player_cm} cm but "
        f"PLAYER_BODY_RADIUS in sim/cataclysm_sim/enemy_abilities.py is "
        f"{PLAYER_BODY_RADIUS} m. Every rank distance is measured from it.")

    minion_cm = cpp_constant(MINION_CPP, "MinionCapsuleRadius")
    assert minion_cm / CM_PER_METRE == pytest.approx(imp.body_radius), (
        f"MinionCapsuleRadius in {MINION_CPP.name} is {minion_cm} cm but the "
        f"Imp's body_radius is {imp.body_radius} m. The design document says "
        "they are the same because the lesser imp minion is the same creature.")


def test_the_rank_capacities_are_what_the_geometry_gives(imp):
    """The two capacities the document tabulates, recomputed from first
    principles rather than read back out of the same table."""
    from cataclysm_sim.enemy_abilities import (PLAYER_BODY_RADIUS,
                                               ring_capacity, ring_distance)

    for rank, expected_distance, expected_fits in ((0, 0.72, 7), (1, 1.32, 13),
                                                   (2, 1.92, 20)):
        distance = PLAYER_BODY_RADIUS + imp.body_radius * (1 + 2 * rank)
        fits = math.floor(math.pi / math.asin(imp.body_radius / distance))
        assert ring_distance(imp, rank) == pytest.approx(expected_distance), (
            f"rank {rank} is now {ring_distance(imp, rank):.2f} m from the "
            f"player, not {expected_distance} m as the design document's ring "
            "table states.")
        assert ring_capacity(imp, rank) == fits == expected_fits, (
            f"rank {rank} now fits {ring_capacity(imp, rank)} Imps, not "
            f"{expected_fits} as the design document's ring table states.")


def test_the_document_ring_table_states_those_numbers(imp_section, imp):
    from cataclysm_sim.enemy_abilities import ring_capacity, ring_distance

    for rank in (0, 1, 2):
        distance = f"{ring_distance(imp, rank):.2f} m"
        assert distance in imp_section, (
            f"the Imp's ring table does not state {distance}, which is where "
            f"rank {rank} stands.")
        assert f"| {ring_capacity(imp, rank)} |" in imp_section, (
            f"the Imp's ring table does not state {ring_capacity(imp, rank)}, "
            f"which is how many fit in rank {rank}.")


def test_the_reach_is_exactly_the_second_rank(imp):
    """Not a round number chosen by eye. It is where rank two stands, and one
    metre either side changes how many Imps can hit at all."""
    from cataclysm_sim.enemy_abilities import ATTACK_REACH, reach_for_rings

    assert ATTACK_REACH["Imp"] == pytest.approx(reach_for_rings(imp, 2)), (
        f"the Imp's reach is {ATTACK_REACH['Imp']} m but the second rank "
        f"stands at {reach_for_rings(imp, 2):.2f} m. The design document sets "
        "the reach to the second rank so that a pack larger than one rank does "
        "something.")


def test_that_reach_admits_exactly_twenty(imp, imp_section):
    """Twenty is the figure section X already uses for the lethal pack, and it
    is the cap the design says nothing else has to enforce."""
    from cataclysm_sim.enemy_abilities import ATTACK_REACH, attackers_within_reach

    assert attackers_within_reach(imp, ATTACK_REACH["Imp"]) == 20, (
        "the Imp's reach no longer admits exactly 20 attackers, it admits "
        f"{attackers_within_reach(imp, ATTACK_REACH['Imp'])}. Section X states "
        "that twenty Imps kill a geared character in 2.4 seconds, and the "
        "geometry is what caps a swarm at twenty.")
    assert "twenty is the cap" in imp_section.lower(), (
        "the Imp subsection no longer states that twenty is the cap on how "
        "many can hit a player at once, which is the whole result of the ring "
        "table above it.")


def test_a_shorter_reach_would_admit_only_one_rank(imp):
    """Proves the reach figure is load-bearing rather than decorative: drop it
    below rank two and the pack maths in section X stops being true."""
    from cataclysm_sim.enemy_abilities import attackers_within_reach, reach_for_rings

    one_rank = reach_for_rings(imp, 1)
    assert attackers_within_reach(imp, one_rank) == 7
    assert attackers_within_reach(imp, one_rank + 0.01) == 7


def test_the_document_names_and_refuses_the_attack_token_alternative(imp_section):
    """It is the standard answer in the genre. A design that does not say why it
    rejected it will have it reintroduced as an optimisation."""
    lowered = imp_section.lower()
    assert "attack-token" in lowered or "attack token" in lowered, (
        "the Imp subsection no longer names the attack-token rule, which is "
        "the usual way action games cap how many enemies swing at once. The "
        "section refuses it because it would contradict the 4.9 and 2.4 second "
        "figures section X states. Issue #348.")
    assert "doom" in lowered and "arkham" in lowered, (
        "the Imp subsection no longer cites the two games the attack-token "
        "rule is known from, so the claim has no evidence behind it.")


# --------------------------------------------------------------------------
# Telegraph, speed and pack
# --------------------------------------------------------------------------

def test_the_imp_is_not_telegraphed_and_the_rule_agrees(imp, imp_abilities,
                                                        imp_section):
    """The Attack Telegraphs subsection excludes the Imp by its attack speed.
    This checks the ability table agrees rather than stating it separately."""
    from cataclysm_sim.enemy_abilities import (is_telegraphed,
                                               largest_telegraphed_radius)

    ability = imp_abilities[0]
    assert not is_telegraphed(ability, imp), (
        "the telegraph rule now says the Imp's basic attack IS telegraphed. "
        f"Its cycle is {ability.cycle_seconds(imp)} s, allowing a marker of "
        f"{largest_telegraphed_radius(ability.cycle_seconds(imp)):.1f} m.")
    assert "| No |" in imp_section, (
        "the Imp's ability table no longer says No in the Telegraphed column.")


def test_the_imp_outruns_every_demonic_class(imp_section, imp):
    """The claim that walking away is never an escape. It is what makes the
    Movement slot the answer, so it must be true of all three classes."""
    text = GDD.read_text(encoding="utf-8")
    row = re.search(r"\| Movement Speed \|([^\n]*)\|", text)
    assert row, (
        "the Three Demonic Class Stat Lines table in docs/Cataclysm_GDD_v2.md "
        "no longer has a Movement Speed row.")
    speeds = [float(cell) for cell in row.group(1).split("|") if cell.strip()]
    assert speeds, "the Movement Speed row has no numbers in it"
    assert imp.move_speed > max(speeds), (
        f"the Imp moves at {imp.move_speed} and the fastest Demonic class at "
        f"{max(speeds)}, so the section's claim that walking away is never an "
        "escape is no longer true.")
    assert "never an escape" in imp_section.lower(), (
        "the Imp subsection no longer states that walking away is never an "
        "escape, which is what makes the Movement slot the answer to a pack.")


def test_the_movement_modes_that_clear_a_ring_are_named(imp_section):
    """Leap and Blink out, Charge not. Without this, being surrounded has no
    stated counter and the design is the body-block failure it cites.

    The verdict sentence is sliced out and each of the three modes checked
    against it, rather than against the whole subsection. Searching the whole
    subsection passes on a nearby sentence that happens to use the word, which
    is how a first version of this test missed a mode being dropped.
    """
    verdict = re.search(r"\*\*([^*]*clear a ring[^*]*)\*\*", imp_section)
    assert verdict, (
        "the Imp subsection no longer has a bold sentence saying which "
        "Movement modes clear a ring of bodies. Without it, being surrounded "
        "has no stated counter. Issue #348.")
    sentence = verdict.group(1).lower()
    for mode in ("leap", "blink", "charge"):
        assert mode in sentence, (
            f"the sentence {verdict.group(1)!r} does not say what a {mode} "
            "does against a ring of bodies. All three Movement modes have to "
            "be covered. Issue #348.")
    assert "path of exile 2" in imp_section.lower(), (
        "the Imp subsection no longer cites the body-block problem it is "
        "avoiding, so the rule reads as arbitrary.")


def test_the_pack_size_is_the_one_the_document_already_states(imp_section):
    """Ten is not a new number. Section X already uses it, so the design reuses
    it rather than introducing a second pack figure that could disagree."""
    from cataclysm_sim.enemy_abilities import PACK_SIZE

    assert PACK_SIZE["Imp"] == 10, (
        f"the Imp's pack is now {PACK_SIZE['Imp']}. Section X states that ten "
        "Imps kill a geared character in 4.9 seconds and the design reuses "
        "that figure rather than adding another.")
    assert "a pack is ten" in imp_section.lower(), (
        "the Imp subsection no longer states the pack size.")

    survival = unwrapped(GDD.read_text(encoding="utf-8"))
    assert "ten take 4.9 seconds" in survival, (
        "section X no longer states that ten Imps kill a geared character in "
        "4.9 seconds, which is where the pack size of ten comes from.")


def test_the_engine_note_names_detour_crowd_rather_than_rvo(imp_section):
    """The ring is not scripted, it is what crowd avoidance produces. Which
    avoidance system is not interchangeable, and running both jitters."""
    lowered = imp_section.lower()
    assert "detour crowd" in lowered, (
        "the Imp subsection no longer says which avoidance system produces the "
        "ring behaviour. Issue #348.")
    assert "rvo" in lowered, (
        "the Imp subsection no longer names RVO as the one not to use "
        "alongside it.")


# --------------------------------------------------------------------------
# The data guards themselves
# --------------------------------------------------------------------------

def test_an_undesigned_enemy_raises_rather_than_returning_nothing():
    """Six of the seven have no abilities. Returning an empty list would let a
    caller treat an undesigned enemy as a finished one that does nothing."""
    from cataclysm_sim.enemy_abilities import abilities

    with pytest.raises(ValueError, match="no designed abilities"):
        abilities("Gatekeeper")

    with pytest.raises(ValueError, match="unknown archetype"):
        abilities("Wyvern")


def test_every_designed_ability_uses_a_shape_the_skill_data_uses():
    """An enemy ability in a shape no player skill uses is a shape with no
    executor. The seven names are checked against the skill file itself."""
    import csv

    from cataclysm_sim.enemy_abilities import ABILITIES, SHAPES

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        used = {row["Shape"] for row in csv.DictReader(handle) if row["Shape"]}

    assert used <= set(SHAPES), (
        f"game/Data/WeaponSkills.csv uses shapes {sorted(used - set(SHAPES))} "
        "that sim/cataclysm_sim/enemy_abilities.py does not list.")
    for name, entries in ABILITIES.items():
        for ability in entries:
            assert ability.shape in used, (
                f"{name}'s {ability.name} is a {ability.shape}, which no "
                "player skill uses, so nothing in the engine runs it yet.")


def test_a_zero_body_radius_is_rejected():
    """The guard in enemy_stats.py. A body with no width would let any number
    of enemies stand on one point, which silently removes the swarm cap."""
    from dataclasses import replace

    from cataclysm_sim.enemy_stats import ARCHETYPES, Archetype

    broken = replace(ARCHETYPES["Imp"], body_radius=0.0)
    original = ARCHETYPES["Imp"]
    ARCHETYPES["Imp"] = broken
    try:
        from cataclysm_sim import enemy_stats

        with pytest.raises(AssertionError, match="body radius"):
            enemy_stats._check_every_body_has_a_width()
    finally:
        ARCHETYPES["Imp"] = original

    assert isinstance(ARCHETYPES["Imp"], Archetype)
