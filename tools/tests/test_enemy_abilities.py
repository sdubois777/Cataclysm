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

#: Each designed enemy gets one `### **The <name>**` subsection, and each one is
#: sliced out at the next `###` so that a claim about one enemy cannot be
#: satisfied by wording that belongs to another.
SUBSECTION_MARKER = "### **The {name}**"

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


def subsection(name: str) -> str:
    """One enemy's subsection, cut at the next `###` heading.

    Cutting at the next heading matters. Without it every claim would be
    checked against the whole of section X, so a sentence written for one enemy
    would satisfy a test about another.
    """
    marker = SUBSECTION_MARKER.format(name=name)
    text = GDD.read_text(encoding="utf-8")
    start = text.find(marker)
    assert start != -1, (
        f"docs/Cataclysm_GDD_v2.md has no {marker} subsection. Every designed "
        "enemy gets one. Issues #348 and #349.")
    after = start + len(marker)
    ends = [position for position in (text.find("\n### ", after),
                                      text.find("\n# ", after),
                                      text.find(NEXT_SECTION, after))
            if position != -1]
    return unwrapped(text[start:min(ends) if ends else len(text)])


@pytest.fixture(scope="module")
def imp_section() -> str:
    return subsection("Imp")


@pytest.fixture(scope="module")
def succubus_section() -> str:
    return subsection("Succubus")


@pytest.fixture(scope="module")
def imp():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Imp")


@pytest.fixture(scope="module")
def succubus():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Succubus")


@pytest.fixture(scope="module")
def imp_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Imp")


@pytest.fixture(scope="module")
def succubus_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Succubus")


def assert_row_matches(section: str, ability, enemy: str) -> None:
    """The document's ability table row must match the data exactly."""
    heading = f"| {ability.name} | {ability.slot} | {ability.shape} |"
    assert heading in section, (
        f"the {enemy}'s ability table has no row reading {heading!r}, which is "
        "what ABILITIES in sim/cataclysm_sim/enemy_abilities.py holds.")
    for key, value in ability.params.items():
        text = f"{key}={value}"
        assert text in section, (
            f"the {enemy}'s ability table does not state {text}, which is in "
            f"{ability.name}'s params in sim/cataclysm_sim/enemy_abilities.py.")


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


def test_the_section_lists_exactly_the_enemies_that_are_designed(section):
    """Five of the seven are still open issues, and a section that reads as
    complete would let one of them be skipped."""
    from cataclysm_sim.enemy_abilities import ABILITIES
    from cataclysm_sim.enemy_stats import ARCHETYPES

    designed = sorted(ABILITIES)
    joined = " and ".join(f"the {name}" for name in designed).lower()
    assert f"only {joined} are designed" in section.lower(), (
        "the Vertical Slice Enemy Behaviour section no longer says that only "
        f"{joined} are designed. It has to name exactly the enemies in "
        "ABILITIES, or an undesigned enemy reads as finished. Issue #349.")

    undesigned = [name for name in ARCHETYPES
                  if name not in ABILITIES and name != "Baseline"]
    for name in undesigned:
        assert name in section, (
            f"the section no longer names the {name} as still open, so nobody "
            "reading it can tell what is left to do.")


def test_the_imp_ability_table_matches_the_data(imp_section, imp_abilities):
    """The document and sim/cataclysm_sim/enemy_abilities.py must not drift.
    The data is what an engine reads; the document is what a person reads."""
    assert len(imp_abilities) == 1, (
        f"the Imp now has {len(imp_abilities)} abilities in "
        "sim/cataclysm_sim/enemy_abilities.py. The design document states it "
        "has exactly one and gives two reasons. Update both together.")
    assert_row_matches(imp_section, imp_abilities[0], "Imp")


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
# The Succubus
# --------------------------------------------------------------------------

def test_the_succubus_ability_table_matches_the_data(succubus_section,
                                                     succubus_abilities):
    """Three abilities, and the document's table must carry all three exactly
    as the data holds them."""
    assert len(succubus_abilities) == 3, (
        f"the Succubus now has {len(succubus_abilities)} abilities in "
        "sim/cataclysm_sim/enemy_abilities.py. The design document describes "
        "three: a telegraphed bolt, a curse, and an ally aura.")
    for ability in succubus_abilities:
        assert_row_matches(succubus_section, ability, "Succubus")


def test_the_succubus_uses_the_largest_telegraph_its_interval_allows(
        succubus, succubus_abilities, succubus_section):
    """Not a radius chosen by eye. It is the cap the Attack Telegraphs
    subsection sets for a 2.6 second attack interval, used in full."""
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED,
                                               largest_telegraphed_radius)

    basic = next(a for a in succubus_abilities if a.is_basic_attack)
    largest = largest_telegraphed_radius(succubus.attack_interval)
    assert basic.params["Radius"] == pytest.approx(largest), (
        f"the Succubus's basic attack has a radius of {basic.params['Radius']} "
        f"but the largest its {succubus.attack_interval} s attack interval "
        f"allows is {largest:.2f} m. The design says it uses all of it.")

    wind_up = REACTION_ALLOWANCE + largest / WALK_OUT_SPEED
    assert wind_up == pytest.approx(succubus.attack_interval / 2.0), (
        f"the wind-up works out at {wind_up:.2f} s, which is not half the "
        f"{succubus.attack_interval} s attack interval. Using the largest "
        "allowed radius is what makes those two the same number.")
    assert f"{wind_up:.1f} s wind-up" in succubus_section, (
        f"the Succubus's ability table does not state a {wind_up:.1f} s "
        "wind-up.")


def test_the_succubus_basic_attack_is_telegraphed_and_the_curse_is_not(
        succubus, succubus_abilities):
    """Two different reasons, and both come from the rules rather than a
    per-ability judgement: the bolt fits the wind-up rule, and a Debuff has no
    marker shape at all."""
    from cataclysm_sim.enemy_abilities import TELEGRAPHED_SHAPES, is_telegraphed

    by_name = {a.name: a for a in succubus_abilities}
    assert is_telegraphed(by_name["Soulfire"], succubus), (
        "the Succubus's basic attack is no longer telegraphed. A 2.6 second "
        "attack interval allows a 3.15 m marker, which is well over the one "
        "metre floor.")
    assert not is_telegraphed(by_name["Wither the Living"], succubus), (
        "the curse is now telegraphed. Debuff is not one of the four shapes "
        f"the telegraph table draws a marker for: {list(TELEGRAPHED_SHAPES)}.")
    assert not is_telegraphed(by_name["Dominion"], succubus), (
        "the ally aura is now telegraphed. It is held on rather than cast, so "
        "it has no cycle to measure a wind-up against.")


def test_the_document_says_three_shapes_have_no_marker(succubus_section):
    from cataclysm_sim.enemy_abilities import SHAPES, TELEGRAPHED_SHAPES

    unmarked = sorted(set(SHAPES) - set(TELEGRAPHED_SHAPES))
    assert len(unmarked) == 3, (
        f"the telegraph table now draws {7 - len(unmarked)} of the seven "
        "shapes. The Succubus subsection states that three have no marker.")
    for shape in unmarked:
        assert shape in succubus_section, (
            f"the Succubus subsection no longer names {shape} as a shape with "
            "no ground marker, so a reader cannot tell which abilities are "
            "read off the caster instead. Issue #349.")


def test_the_curse_effect_is_taken_from_the_status_effect_table(
        succubus_abilities, succubus_section):
    """Chosen from game/Data/StatusEffects.csv rather than invented, which is
    what the issue asked for."""
    import csv

    effects = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
    if not effects.is_file():
        pytest.skip("game/Data/StatusEffects.csv is not present")
    with effects.open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    known = {row["EffectName"]: row["EffectKind"] for row in rows}

    for ability in succubus_abilities:
        effect = ability.params.get("Effect")
        if effect is None:
            continue
        assert effect in known, (
            f"the Succubus's {ability.name} applies {effect!r}, which is not "
            "in game/Data/StatusEffects.csv. Enemy abilities choose from that "
            "table rather than inventing an effect.")

    assert "game/Data/StatusEffects.csv" in succubus_section, (
        "the Succubus subsection no longer says where its effects come from.")


def test_no_effect_duplicates_a_modifier_its_own_cataclysm_can_roll(
        succubus_section):
    """A modifier is what an individual enemy carries one of per rarity above
    Common, drawn from its own Cataclysm's pool and the Generic one. An innate
    ability that duplicated a modifier the same creature could roll would let it
    hold the effect twice with nothing saying what that means.

    A modifier belonging to a DIFFERENT Cataclysm is not a clash, because that
    enemy can never roll it. Commander is a War modifier and the Succubus is
    Demonic, so its ally aura is free to use the effect of that name.
    """
    import csv

    from cataclysm_sim.enemy_abilities import ABILITIES
    from cataclysm_sim.enemy_stats import archetype

    modifiers = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"
    if not modifiers.is_file():
        pytest.skip("game/Data/EnemyModifiers.csv is not present")
    with modifiers.open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))

    for name, entries in ABILITIES.items():
        cataclysm = archetype(name).cataclysm
        rollable = {row["ModifierName"] for row in rows
                    if row["CataclysmType"] in (cataclysm, "Generic")}
        assert rollable, (
            f"no enemy modifiers found for the {cataclysm} Cataclysm or the "
            "Generic pool, so this check would pass without testing anything.")
        for ability in entries:
            effect = ability.params.get("Effect")
            if effect is None:
                continue
            assert effect not in rollable, (
                f"the {name}'s {ability.name} applies {effect!r}, which is "
                f"also a {cataclysm} or Generic enemy modifier in "
                "game/Data/EnemyModifiers.csv. That creature could roll the "
                "same effect as a modifier and hold it twice. Pick another.")

    assert "game/Data/EnemyModifiers.csv" in succubus_section, (
        "the Succubus subsection no longer states the rule that an innate "
        "ability must not duplicate a modifier its own Cataclysm can roll. "
        "Issue #349.")


def test_the_ally_aura_is_held_on_rather_than_cast(succubus_abilities,
                                                   succubus_section):
    """The whole lesson of the enemy. A buff that outlives its caster makes
    killing the caster pointless."""
    aura = next(a for a in succubus_abilities if a.shape == "Aura")
    assert aura.is_held_on, (
        f"the Succubus's {aura.name} is in the {aura.slot} slot rather than "
        "Aura, so it no longer ends when the creature dies.")
    assert "Duration" not in aura.params, (
        f"{aura.name} now carries a Duration, which by the design document's "
        "own rule makes an Aura timed rather than held on. A timed buff "
        "survives the Succubus's death.")
    assert "killing it first is the correct play" in succubus_section.lower(), (
        "the Succubus subsection no longer states why the buff is an aura "
        "rather than a cast.")


def test_the_ally_aura_radius_is_the_succubus_attack_range(succubus_abilities):
    """Derived, not chosen: it is exactly how far from the fight it stands."""
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    aura = next(a for a in succubus_abilities if a.shape == "Aura")
    assert aura.params["Radius"] == pytest.approx(ATTACK_REACH["Succubus"]), (
        f"the ally aura reaches {aura.params['Radius']} m and the Succubus "
        f"stands {ATTACK_REACH['Succubus']} m from the player. A smaller "
        "radius buffs nothing at the moment it matters.")


def test_the_succubus_stands_at_the_shortest_movement_skill_range(
        succubus_section):
    """Eight metres, the same anchor the Attack Telegraphs subsection uses. A
    ranged enemy beyond it cannot be closed on by every build."""
    import csv

    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        ranges = []
        for row in csv.DictReader(handle):
            if row["Shape"] != "Movement":
                continue
            params = dict(pair.strip().split("=", 1)
                          for pair in row["ShapeParams"].split(";"))
            ranges.append(float(params["Range"]))

    assert ranges, "no Movement-shape skills in game/Data/WeaponSkills.csv"
    assert ATTACK_REACH["Succubus"] == pytest.approx(min(ranges)), (
        f"the Succubus stands at {ATTACK_REACH['Succubus']} m but the shortest "
        f"Movement skill range is {min(ranges)} m. Standing further out than "
        "the shortest gap-closer puts it out of reach of some builds.")
    assert "shortest movement-shape skill range" in succubus_section.lower(), (
        "the Succubus subsection no longer says where the 8 metres comes from.")


def test_the_succubus_does_not_kite_and_the_reason_is_stated(succubus,
                                                             succubus_section):
    """It matches the slowest class and loses to the other two, so retreating
    can only produce a chase."""
    text = GDD.read_text(encoding="utf-8")
    row = re.search(r"\| Movement Speed \|([^\n]*)\|", text)
    assert row, "the Three Demonic Class Stat Lines table has no Movement Speed row"
    speeds = [float(cell) for cell in row.group(1).split("|") if cell.strip()]
    assert succubus.move_speed <= min(speeds), (
        f"the Succubus moves at {succubus.move_speed} and the slowest Demonic "
        f"class at {min(speeds)}. The subsection's reason for not kiting is "
        "that it cannot outrun anybody.")
    assert "does not retreat" in succubus_section.lower(), (
        "the Succubus subsection no longer says that it holds its ground.")


def test_the_energy_shield_answer_counts_the_burning_skills(succubus_section,
                                                            succubus):
    """The claim that a Demonic player already carries the answer. It rests on
    a count of the designed skills, so the count is recomputed here."""
    import csv

    assert succubus.energy_shield_fraction > 0.0, (
        "the Succubus no longer has an energy shield, so the subsection about "
        "how its shield behaves describes nothing.")

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        designed = [row for row in csv.DictReader(handle) if row["Shape"]]
    burning = [row for row in designed if "Burn=1" in row["ShapeParams"]]

    stated = f"{len(burning)} of the {len(designed)} designed Demonic skills"
    assert stated in succubus_section, (
        f"the Succubus subsection does not say {stated!r}. That count is what "
        "backs the claim that a Demonic player already has the answer to an "
        "energy shield, and it is recomputed from "
        "game/Data/WeaponSkills.csv.")


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
