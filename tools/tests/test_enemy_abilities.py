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

    designed = [f"the {name}" for name in sorted(ABILITIES)]
    joined = (designed[0] if len(designed) == 1
              else ", ".join(designed[:-1]) + " and " + designed[-1]).lower()
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
# The Hellhound
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def hellhound_section() -> str:
    return subsection("Hellhound")


@pytest.fixture(scope="module")
def hellhound():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Hellhound")


@pytest.fixture(scope="module")
def hellhound_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Hellhound")


def test_the_hellhound_ability_table_matches_the_data(hellhound_section,
                                                      hellhound_abilities):
    assert len(hellhound_abilities) == 2, (
        f"the Hellhound now has {len(hellhound_abilities)} abilities. The "
        "design document describes two: a bite and a charge. The fire trail is "
        "a rider on the charge, not a third ability.")
    for ability in hellhound_abilities:
        assert_row_matches(hellhound_section, ability, "Hellhound")


def test_the_bite_reaches_contact_and_fits_five(hellhound, hellhound_section):
    """Derived from the two body radii, the same way the Imp's reach is, and it
    is what separates a charger from a swarm."""
    from cataclysm_sim.enemy_abilities import (ATTACK_REACH, PLAYER_BODY_RADIUS,
                                               attackers_within_reach,
                                               ring_capacity)

    contact = PLAYER_BODY_RADIUS + hellhound.body_radius
    assert ATTACK_REACH["Hellhound"] == pytest.approx(contact), (
        f"the Hellhound reaches {ATTACK_REACH['Hellhound']} m but contact is "
        f"{contact:.2f} m: the player's {PLAYER_BODY_RADIUS} plus its own "
        f"{hellhound.body_radius}.")

    fits = ring_capacity(hellhound, 0)
    assert attackers_within_reach(hellhound, contact) == fits == 5, (
        f"{fits} Hellhounds now fit around one player, not 5. The subsection "
        "states five, against the Imp's twenty, and that is what makes this a "
        "charger rather than a swarm.")
    assert "one rank of hellhounds is five" in hellhound_section.lower(), (
        "the Hellhound subsection no longer states how many fit around a "
        "player. Issue #350.")


def test_the_charge_is_telegraphed_and_the_bite_is_not(hellhound,
                                                       hellhound_abilities):
    """Both verdicts come from the existing wind-up rule rather than a
    judgement, and the Attack Telegraphs subsection names this enemy as the
    example of an ability telegraphed against its cooldown."""
    from cataclysm_sim.enemy_abilities import is_telegraphed

    by_name = {a.name: a for a in hellhound_abilities}
    assert not is_telegraphed(by_name["Maul"], hellhound), (
        "the Hellhound's bite is now telegraphed. Its 1.1 second attack "
        "interval allows a 0.5 metre marker, below the one metre floor.")
    assert is_telegraphed(by_name["Hellrush"], hellhound), (
        "the Hellhound's charge is no longer telegraphed. It runs on its own "
        "cooldown rather than the attack interval, which is what the Attack "
        "Telegraphs subsection uses it as the example of.")


def test_the_charge_wind_up_is_stated_and_computed(hellhound_abilities,
                                                   hellhound_section):
    from cataclysm_sim.enemy_abilities import REACTION_ALLOWANCE, WALK_OUT_SPEED

    charge = next(a for a in hellhound_abilities if a.shape == "Movement")
    wind_up = REACTION_ALLOWANCE + float(charge.params["Radius"]) / WALK_OUT_SPEED
    assert f"{wind_up:.2f} s wind-up" in hellhound_section, (
        f"the Hellhound's ability table does not state a {wind_up:.2f} s "
        "wind-up, which is what its corridor radius gives.")


def test_the_charge_goes_further_than_walking_would(hellhound,
                                                    hellhound_abilities,
                                                    hellhound_section):
    """The test a charge has to pass to be worth having: it must beat simply
    walking for the same time it spends standing still winding up."""
    from cataclysm_sim.enemy_abilities import REACTION_ALLOWANCE, WALK_OUT_SPEED

    charge = next(a for a in hellhound_abilities if a.shape == "Movement")
    wind_up = REACTION_ALLOWANCE + float(charge.params["Radius"]) / WALK_OUT_SPEED
    walked = hellhound.move_speed * wind_up
    assert float(charge.params["Range"]) > walked, (
        f"the charge covers {charge.params['Range']} m but the Hellhound could "
        f"walk {walked:.2f} m during the {wind_up:.2f} s it spends winding up. "
        "A charge shorter than that is worse than not winding up at all.")
    assert f"{walked:.1f} metres" in hellhound_section, (
        f"the Hellhound subsection does not state the {walked:.1f} metres it "
        "could walk instead, which is the whole reason for the charge's range.")


def test_the_charge_corridor_is_the_narrowest_a_player_charge_uses(
        hellhound_abilities):
    """A wide corridor is a wall rather than a lane. The narrowest player charge
    is the anchor, the same way the shortest Movement range anchors the
    Succubus."""
    import csv

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    radii = []
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["Shape"] != "Movement":
                continue
            params = dict(pair.strip().split("=", 1)
                          for pair in row["ShapeParams"].split(";"))
            if params.get("Mode") == "Charge":
                radii.append(float(params["Radius"]))

    assert radii, "no Charge-mode skills in game/Data/WeaponSkills.csv"
    charge = next(a for a in hellhound_abilities if a.shape == "Movement")
    assert float(charge.params["Radius"]) == pytest.approx(min(radii)), (
        f"the Hellhound's corridor is {charge.params['Radius']} m and the "
        f"narrowest player charge is {min(radii)} m. Anything wider is a wall "
        "rather than a lane to step out of.")


def test_the_charge_cooldown_is_the_movement_slot_cooldown(hellhound_abilities):
    import csv

    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    if not slots.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with slots.open(encoding="utf-8-sig", newline="") as handle:
        movement = next(row for row in csv.DictReader(handle)
                        if row["Slot"] == "Movement")

    charge = next(a for a in hellhound_abilities if a.shape == "Movement")
    assert charge.cooldown == pytest.approx(float(movement["Cooldown"])), (
        f"the charge's cooldown is {charge.cooldown} s and the Movement slot's "
        f"is {movement['Cooldown']} s in game/Data/SkillSlots.csv.")


def test_the_trail_is_riders_on_the_charge_not_a_third_ability(
        hellhound_abilities, hellhound_section):
    """Exactly the shape the player's Flamedart already uses, plus the one new
    rider."""
    charge = next(a for a in hellhound_abilities if a.shape == "Movement")
    for rider in ("Burn", "GroundRadius", "GroundDuration", "GroundHitsAllies"):
        assert rider in charge.params, (
            f"the Hellhound's charge no longer carries {rider}. The fire trail "
            "is riders on the charge rather than an ability of its own.")
    assert charge.params["GroundRadius"] == charge.params["Radius"], (
        "the trail is no longer as wide as the lane the Hellhound charged "
        "through, which is what it is meant to be: the ground it burned.")
    assert "not a third ability" in hellhound_section.lower(), (
        "the Hellhound subsection no longer says the trail is a rider.")


def test_the_new_rider_is_declared_in_the_shared_vocabulary(hellhound_section):
    """GroundHitsAllies has to exist in section V's rider list and in the
    generator that validates player skills, or an enemy would be the only thing
    that knows about it."""
    from cataclysm_sim.enemy_abilities import RIDERS

    assert "GroundHitsAllies" in RIDERS

    # Sliced out of section V, not searched for in the whole document. The
    # Hellhound's own subsection names the rider several times, so a whole-file
    # search passes even when section V has dropped it.
    text = GDD.read_text(encoding="utf-8")
    start = text.find("**A burning patch of ground is a rider, not a shape.**")
    assert start != -1, (
        "docs/Cataclysm_GDD_v2.md section V no longer has the paragraph that "
        "lists the riders any shape may carry.")
    riders_text = unwrapped(text[start:text.find("# **VI.", start)])

    # The enumeration sentence alone, not the whole paragraph. The paragraph
    # after it also names the rider, so checking the paragraph passes even when
    # the list itself has dropped it.
    listing = re.search(r"riders work the same way:(.*?)\.\s", riders_text)
    assert listing, (
        "section V no longer enumerates the riders that work like "
        "GroundRadius and GroundDuration.")
    assert "`GroundHitsAllies`" in listing.group(1), (
        "section V's rider list no longer includes GroundHitsAllies. The "
        "Hellhound's fire trail is the only thing that sets it, and a rider "
        "only that enemy knows about is a rider nothing validates. Issue #350.")
    assert "no player skill does" in riders_text, (
        "section V no longer says that GroundHitsAllies is off unless set and "
        "that no player skill sets it.")
    assert "burns the hellhound too" in hellhound_section.lower(), (
        "the Hellhound subsection no longer says its own trail burns it, which "
        "is what makes the rule one line rather than an exception.")


def test_the_trail_tick_rate_is_the_ground_zone_class_constant(
        hellhound_section):
    """Answered from the code rather than chosen: a ground zone already ticks
    once a second, and the trail's per-tick damage follows from that."""
    ground_zone = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                   / "CataclysmGroundZone.h")
    tick = cpp_constant(ground_zone, "TickSeconds")
    assert tick == pytest.approx(1.0), (
        f"ACataclysmGroundZone::TickSeconds is now {tick}. The Hellhound "
        "subsection says the trail deals damage once a second and that "
        "standing in it for its whole 4 seconds costs one bite, which is four "
        "ticks of a quarter each.")
    assert "once a second" in hellhound_section.lower(), (
        "the Hellhound subsection no longer states how often the trail deals "
        "damage.")


def test_the_document_says_what_killing_it_does_to_the_trail(hellhound_section):
    """Both cases, because they follow from two different existing rules and a
    reader who only sees one will guess the other wrong."""
    lowered = hellhound_section.lower()
    assert "killed during the wind-up" in lowered, (
        "the Hellhound subsection no longer says what killing it during the "
        "wind-up does. Issue #350.")
    assert "killed during the charge" in lowered, (
        "the Hellhound subsection no longer says what killing it mid-charge "
        "does to the trail it has already laid.")


# --------------------------------------------------------------------------
# The Brute
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def brute_section() -> str:
    return subsection("Brute")


@pytest.fixture(scope="module")
def brute():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Brute")


@pytest.fixture(scope="module")
def brute_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Brute")


def test_the_brute_ability_table_matches_the_data(brute_section,
                                                  brute_abilities):
    assert len(brute_abilities) == 2, (
        f"the Brute now has {len(brute_abilities)} abilities. The design "
        "document describes two: a slam and a stomp.")
    for ability in brute_abilities:
        assert_row_matches(brute_section, ability, "Brute")


def test_the_slam_lands_exactly_on_the_stun_damage_threshold(brute_section):
    """The reason the slam does not stun, recomputed against the reference build
    rather than quoted. An Elite Brute's ordinary hit is 10.0% of that
    character's effective health and the threshold is 10%."""
    from cataclysm_sim import damage as dmg
    from cataclysm_sim import enemy_stats as es
    from cataclysm_sim import reference_build as rb

    enemy = es.stats_on_floor("Elite", 8, "Cataclysm", kind="Brute")
    hits = dmg.hits_to_kill(
        dmg.Attacker(damage=enemy.average_damage_per_hit,
                     damage_type=enemy.damage_type),
        rb.defender(8))
    share = 100.0 / hits
    assert share == pytest.approx(10.0, abs=0.5), (
        f"an Elite Brute's ordinary hit is now {share:.1f}% of the reference "
        "build's effective health, not the 10% the design document states. "
        "That figure is the whole reason the slam does not stun.")
    assert "exactly 10% of the reference build" in brute_section, (
        "the Brute subsection no longer states that its slam lands exactly on "
        "the stun damage threshold. Issue #351.")


def test_the_stomp_clears_the_threshold_at_the_heavy_slot_percent(brute_section):
    """250% of a hit that is 10% of the pool is 25%, which is two and a half
    times the threshold. Both numbers are read out of the data."""
    import csv

    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    if not slots.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with slots.open(encoding="utf-8-sig", newline="") as handle:
        heavy = next(row for row in csv.DictReader(handle)
                     if row["Slot"] == "Heavy")

    percent = float(heavy["DamagePercent"])
    assert percent == pytest.approx(250.0), (
        f"the Heavy slot is now {percent}% in game/Data/SkillSlots.csv. The "
        "Brute subsection says the stomp lands at 25% of the reference build's "
        "effective health, which is that percent of a 10% hit.")
    assert f"the Heavy slot's {percent:.0f}%" in brute_section, (
        "the Brute subsection no longer states which slot percent the stomp "
        "uses.")


def test_the_stomp_cooldown_is_the_stun_immunity_window(brute_abilities,
                                                        brute_section):
    """Not the Heavy slot's cooldown, which sits entirely inside the window."""
    import csv

    from cataclysm_sim.enemy_abilities import STUN_IMMUNITY_WINDOW

    stomp = next(a for a in brute_abilities if "StunSeconds" in a.params)
    assert stomp.cooldown == pytest.approx(STUN_IMMUNITY_WINDOW), (
        f"the stomp comes round every {stomp.cooldown} s and the stun immunity "
        f"window is {STUN_IMMUNITY_WINDOW} s. A stun inside the window lands "
        "on a target that cannot be stunned.")

    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    if not slots.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with slots.open(encoding="utf-8-sig", newline="") as handle:
        heavy = next(row for row in csv.DictReader(handle)
                     if row["Slot"] == "Heavy")
    assert float(heavy["CooldownHighest"]) < STUN_IMMUNITY_WINDOW, (
        "the Heavy slot's cooldown band now reaches "
        f"{heavy['CooldownHighest']} s, which is no longer entirely inside the "
        f"{STUN_IMMUNITY_WINDOW} s immunity window. The Brute subsection's "
        "reason for overriding the slot's cooldown rests on that.")
    assert "slot-independent rule" in brute_section, (
        "the Brute subsection no longer states that any stunning ability sits "
        "at least the immunity window apart whatever slot it is in.")


def test_no_enemy_stun_outlasts_the_longest_designed_player_stun(brute_section):
    """1.5 seconds is Shield Bash's, recomputed from the skill descriptions
    rather than trusted."""
    import csv
    import re as regex

    from cataclysm_sim.enemy_abilities import ABILITIES, LONGEST_DESIGNED_STUN

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    stated = []
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            for seconds in regex.findall(
                    r"stun(?:ning|s)?[^.]*?for ([0-9.]+) seconds?",
                    row["SkillDescription"], regex.IGNORECASE):
                stated.append(float(seconds))

    assert stated, (
        "no skill in game/Data/WeaponSkills.csv states a stun duration any "
        "more, so the ceiling on an enemy's stun has nothing behind it.")
    assert max(stated) == pytest.approx(LONGEST_DESIGNED_STUN), (
        f"the longest stun a designed player skill grants is now {max(stated)} "
        f"s, not the {LONGEST_DESIGNED_STUN} s the enemy data caps against.")

    for name, entries in ABILITIES.items():
        for ability in entries:
            seconds = float(ability.params.get("StunSeconds", 0.0))
            assert seconds <= max(stated), (
                f"{name}'s {ability.name} stuns for {seconds} s, longer than "
                "anything the player can do.")

    assert "longest any designed player skill grants" in brute_section, (
        "the Brute subsection no longer states where its 1.5 seconds comes "
        "from.")


def test_the_brute_turns_slower_than_a_player_can_circle_it(brute,
                                                            brute_section):
    """The number behind 'can be outmanoeuvred'. The ceiling is derived from
    the player's own speed at the Brute's own reach."""
    import math

    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    text = GDD.read_text(encoding="utf-8")
    row = re.search(r"\| Movement Speed \|([^\n]*)\|", text)
    assert row, "the Three Demonic Class Stat Lines table has no Movement Speed row"
    slowest = min(float(cell) for cell in row.group(1).split("|") if cell.strip())

    circling = math.degrees(slowest / ATTACK_REACH["Brute"])
    assert brute.turn_rate_degrees < circling, (
        f"the Brute turns at {brute.turn_rate_degrees} degrees per second and "
        f"a player circling at its {ATTACK_REACH['Brute']} m reach turns at "
        f"{circling:.0f}, even in the slowest class. It can no longer be got "
        "behind by every build.")
    assert f"{circling:.0f} degrees per second" in brute_section, (
        f"the Brute subsection does not state the {circling:.0f} degrees per "
        "second a player circles at, which is the ceiling on its turn rate.")

    # Every angular figure in the subsection is checked, not only the first
    # one found. The subsection states the circling rate twice, in a table and
    # in prose, and a test that accepts either passes while the other is wrong.
    engine_default = cpp_constant_int(
        REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
        / "CataclysmEnemyCharacter.cpp")
    allowed = {round(circling), round(brute.turn_rate_degrees),
               round(engine_default)}
    stated = {int(found) for found
              in re.findall(r"(\d+) degrees per second", brute_section)}
    assert stated <= allowed, (
        f"the Brute subsection states {sorted(stated - allowed)} degrees per "
        f"second, which is none of the three real figures {sorted(allowed)}: "
        "the player circling, the Brute turning, and the engine default.")


def test_every_other_enemy_still_turns_at_the_engine_default(brute):
    """The Brute is the exception. If the default moved, the subsection's
    comparison is wrong."""
    from cataclysm_sim.enemy_stats import ARCHETYPES

    default = cpp_constant_int(
        REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
        / "CataclysmEnemyCharacter.cpp")
    for name, kind in ARCHETYPES.items():
        if name == "Brute":
            continue
        assert kind.turn_rate_degrees == pytest.approx(default), (
            f"{name} turns at {kind.turn_rate_degrees} and the engine builds "
            f"every enemy at {default}. Only the Brute is meant to differ.")


def test_the_slam_gets_no_marker_even_though_the_brute_can_telegraph(
        brute, brute_abilities, brute_section):
    """The clarification this enemy forced: the telegraph table's Yes column is
    about the largest marker an enemy could draw, not about every attack."""
    from cataclysm_sim.enemy_abilities import (SMALLEST_USEFUL_MARKER_METRES,
                                               is_telegraphed,
                                               largest_telegraphed_radius)

    by_name = {a.name: a for a in brute_abilities}
    allowed = largest_telegraphed_radius(brute.attack_interval)
    assert allowed >= SMALLEST_USEFUL_MARKER_METRES, (
        "the Brute's attack interval no longer allows a marker at all, so it "
        "is not in the telegraph table's Yes column and this test has nothing "
        "to say.")
    assert not is_telegraphed(by_name["Slam"], brute), (
        "the Brute's slam is now telegraphed. It reaches "
        f"{by_name['Slam'].params['Radius']} m, under the "
        f"{SMALLEST_USEFUL_MARKER_METRES} m floor, so it has no marker to draw "
        "however much its attack interval would allow.")
    assert is_telegraphed(by_name["Stomp"], brute)
    assert "a marker under one metre is not drawn" in brute_section.lower(), (
        "the Brute subsection no longer states the rule its slam is the "
        "example of. Issue #351.")


def test_the_stomp_takes_the_largest_marker_its_attack_interval_allows(
        brute, brute_abilities, brute_section):
    """The same choice the Succubus's bolt makes, and sized by the attack
    interval rather than by the longer cooldown on purpose."""
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED,
                                               largest_telegraphed_radius)

    stomp = next(a for a in brute_abilities if "StunSeconds" in a.params)
    allowed = largest_telegraphed_radius(brute.attack_interval)
    assert float(stomp.params["Radius"]) == pytest.approx(allowed), (
        f"the stomp draws {stomp.params['Radius']} m and the Brute's "
        f"{brute.attack_interval} s attack interval allows {allowed:.2f} m.")

    wind_up = REACTION_ALLOWANCE + allowed / WALK_OUT_SPEED
    assert wind_up == pytest.approx(brute.attack_interval / 2.0)
    assert f"{wind_up:.1f} s wind-up" in brute_section, (
        f"the Brute's ability table does not state a {wind_up:.1f} s wind-up.")

    assert float(stomp.params["Angle"]) == 360, (
        "the stomp is no longer a ring. A cone on an enemy that turns at half "
        "speed is answered once and never again.")


def test_the_stomp_can_be_walked_out_of_from_contact(brute, brute_abilities,
                                                     brute_section):
    from cataclysm_sim.enemy_abilities import (ATTACK_REACH, REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED)

    stomp = next(a for a in brute_abilities if "StunSeconds" in a.params)
    to_cover = float(stomp.params["Radius"]) - ATTACK_REACH["Brute"]
    seconds = to_cover / WALK_OUT_SPEED
    budget = (REACTION_ALLOWANCE + float(stomp.params["Radius"])
              / WALK_OUT_SPEED) - REACTION_ALLOWANCE
    assert seconds < budget, (
        f"walking clear of the stomp from contact takes {seconds:.2f} s and "
        f"the wind-up budgets {budget:.2f} s of walking.")
    assert f"{to_cover:.1f} metres" in brute_section, (
        f"the Brute subsection does not state the {to_cover:.1f} metres a "
        "player at its reach has to cover.")
    assert f"{seconds:.2f} seconds" in brute_section, (
        f"the Brute subsection does not state the {seconds:.2f} seconds that "
        "takes.")


# --------------------------------------------------------------------------
# The data guards themselves
# --------------------------------------------------------------------------

def cpp_constant_int(path: pathlib.Path) -> float:
    """The yaw out of `RotationRate = FRotator(0.0f, 480.0f, 0.0f)`."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8", errors="replace")
    found = re.search(
        r"RotationRate\s*=\s*FRotator\(\s*[0-9.]+f?\s*,\s*([0-9.]+)f?\s*,",
        text)
    assert found, (
        f"{path.name} no longer sets RotationRate on the enemy character. The "
        "Brute's turn rate is stated against that default. Issue #351.")
    return float(found.group(1))


def test_the_shape_vocabulary_matches_the_datatable_generator():
    """sim/cataclysm_sim/enemy_abilities.py holds a copy of the shape and rider
    tables that tools/generate_datatables.py validates player skills with.

    Two copies of one vocabulary is exactly the drift CLAUDE.md warns about, and
    this project has had it twice. Enemy abilities cannot import the generator,
    because the simulation package is standalone, so they are compared instead.
    """
    import sys

    sys.path.insert(0, str(REPO_ROOT / "tools"))
    import generate_datatables as gen

    from cataclysm_sim.enemy_abilities import RIDERS, SHAPE_PARAMS

    assert set(SHAPE_PARAMS) == set(gen.SHAPE_PARAMS), (
        "the shapes in sim/cataclysm_sim/enemy_abilities.py and "
        "tools/generate_datatables.py no longer agree: "
        f"{sorted(set(SHAPE_PARAMS) ^ set(gen.SHAPE_PARAMS))}")
    for shape, params in SHAPE_PARAMS.items():
        assert set(params) == gen.SHAPE_PARAMS[shape], (
            f"the parameters for {shape} differ between the two: "
            f"{sorted(set(params) ^ gen.SHAPE_PARAMS[shape])}")
    assert set(RIDERS) == set(gen.SHAPE_RIDERS), (
        "the riders in the two files no longer agree: "
        f"{sorted(set(RIDERS) ^ set(gen.SHAPE_RIDERS))}")


def test_the_movement_modes_match_the_datatable_generator():
    import sys

    sys.path.insert(0, str(REPO_ROOT / "tools"))
    import generate_datatables as gen

    from cataclysm_sim.enemy_abilities import MOVEMENT_MODES

    assert set(MOVEMENT_MODES) == gen.MOVEMENT_MODES, (
        "the Movement modes in the two files no longer agree: "
        f"{sorted(set(MOVEMENT_MODES) ^ gen.MOVEMENT_MODES)}")


def test_a_marker_too_big_for_its_cycle_is_rejected():
    """The guard in enemy_abilities.py. A marker larger than the wind-up can
    cover cannot be escaped, which the design document calls a damage event
    rather than a telegraph."""
    from cataclysm_sim.enemy_abilities import Ability, fits_its_cycle
    from cataclysm_sim.enemy_stats import archetype

    brute = archetype("Brute")
    too_big = Ability(name="Test", shape="Strike", slot="Heavy",
                      params={"Radius": 7.0}, cooldown=3.0)
    assert not fits_its_cycle(too_big, brute)

    # The same radius on a 5 second cooldown is legal, because that is the tier
    # the Movement slot answers.
    movement_tier = Ability(name="Test", shape="Strike", slot="Heavy",
                            params={"Radius": 7.0}, cooldown=5.0)
    assert fits_its_cycle(movement_tier, brute)

    # And 9 metres is not legal at any cooldown: nothing the player has crosses
    # it.
    beyond_reach = Ability(name="Test", shape="Strike", slot="Heavy",
                           params={"Radius": 9.0}, cooldown=30.0)
    assert not fits_its_cycle(beyond_reach, brute)

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
