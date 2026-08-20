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
ENEMY_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmEnemyCharacter.cpp")
ENEMY_CONTROLLER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                        / "CataclysmEnemyController.cpp")

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


def parameters_the_document_states(ability) -> dict[str, str]:
    """The parameter cell of this ability's row in the document, parsed.

    READ FROM THE FILE RATHER THAN FROM THE SECTION PASSED IN, because
    `unwrapped` collapses every newline to a space, so the section handed to
    `assert_row_matches` is one long line with no rows left in it. That is
    right for the prose checks it was written for and useless for a table.

    A row reads `| Name | Slot | Shape | <parameters> | runs on |
    telegraphed |` with the parameters in backticks, and a creature that has
    phases carries an extra phase column. So the cell is found by looking for
    the backticked one rather than by counting columns.

    AN ABILITY NAME IS UNIQUE ACROSS THE DOCUMENT, which is what makes
    searching the whole file safe rather than only one enemy's subsection. No
    two of the twenty designed abilities share a name.
    """
    text = GDD.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if not line.startswith(f"| {ability.name} |"):
            continue
        for cell in (c.strip() for c in line.strip("|").split("|")):
            if cell.startswith("`") and "=" in cell:
                out = {}
                for part in cell.strip("`").split(";"):
                    key, _, value = part.partition("=")
                    if key.strip():
                        out[key.strip()] = value.strip()
                return out
        return {}
    return {}


def assert_row_matches(section: str, ability, enemy: str) -> None:
    """The document's ability table row must match the data exactly.

    **BOTH DIRECTIONS, SINCE 2026-08-20.** Until then this walked the MODEL's
    parameters and checked each appeared in the document, and never the
    reverse -- so a parameter the document stated and the model lacked passed
    in silence. That is what happened to the Gatekeeper's Soulfall, which
    lost five riders including the burning ground the whole ability exists to
    leave behind. Issue #774. A guard that checks one direction is not
    checking the thing its name claims.
    """
    heading = f"| {ability.name} |"
    assert heading in section, (
        f"the {enemy}'s ability table has no row for {ability.name!r}, which "
        "is in ABILITIES in sim/cataclysm_sim/enemy_abilities.py.")

    for column in (ability.slot, ability.shape):
        assert f"| {column} |" in section, (
            f"the {enemy}'s ability table does not state {column!r} for "
            f"{ability.name}, which is what the model holds.")

    stated = parameters_the_document_states(ability)
    model = {key: str(value) for key, value in ability.params.items()}

    # THE MODEL INTO THE DOCUMENT.
    for key, value in model.items():
        assert key in stated, (
            f"the {enemy}'s ability table does not state {key} for "
            f"{ability.name}, which is in its params in "
            f"sim/cataclysm_sim/enemy_abilities.py.")
        assert stated[key] == value, (
            f"the {enemy}'s ability table says {key}={stated[key]} for "
            f"{ability.name} and the model says {key}={value}.")

    # AND THE DOCUMENT INTO THE MODEL, which is the half that was missing.
    for key, value in stated.items():
        assert key in model, (
            f"the {enemy}'s ability table states {key}={value} for "
            f"{ability.name} and the model does not carry it at all. The "
            f"document is authoritative, so add it to ABILITIES in "
            f"sim/cataclysm_sim/enemy_abilities.py. Issue #774 is this "
            f"happening to Soulfall's burning ground.")


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


def test_the_default_enemy_capsule_is_the_default_body_radius() -> None:
    """The width six of the seven vertical slice enemies actually have.

    WHY THIS WAS MISSING. `test_the_two_body_radii_still_match_the_cpp` above
    checks the player's capsule and the minion's, and
    `tools/tests/test_brute_matches_the_model.py` checks the Brute's. Nothing
    checked `EnemyCapsuleRadius` in `CataclysmEnemyCharacter.cpp`, which is the
    width of every enemy that has no class of its own -- which today is six of
    the seven.

    WHY IT MATTERS. A contact reach is the player's body radius plus the
    creature's, and the Hellhound's 0.90 m is exactly that sum. That only works
    while the engine's capsule really is the model's body radius. Issue #373
    says the two "agree by coincidence of both being right; nothing enforces
    it", and for this pair nothing did.
    """
    from cataclysm_sim.enemy_stats import Archetype

    default_body_radius = Archetype.__dataclass_fields__["body_radius"].default
    engine_cm = cpp_constant(ENEMY_CPP, "EnemyCapsuleRadius")

    assert engine_cm / CM_PER_METRE == pytest.approx(default_body_radius), (
        f"EnemyCapsuleRadius in {ENEMY_CPP.name} is {engine_cm} cm but the "
        f"design model's default body_radius is {default_body_radius} m. Every "
        f"contact reach is the player's body radius plus the creature's, so "
        f"the two have to be the same number or a creature standing against "
        f"the player measures outside its own reach.")


def test_a_contact_reach_is_reachable_for_every_enemy_that_has_one() -> None:
    """Contact reach has no margin, so it has to be exact for all of them.

    WHAT ISSUE #373 LEFT OPEN. It fixed the Brute and asked whether reach should
    account for the two capsule radii explicitly rather than relying on the
    model and the engine happening to use the same numbers. This is that check,
    for every enemy whose reach is contact rather than a chosen distance.

    THE SUCCUBUS AND THE IMP ARE NOT CONTACT and are excluded by name below
    rather than by a rule, because their reaches are design figures with their
    own reasons: the Imp's is the second rank, and the Succubus holds at the
    shortest movement-skill range.
    """
    from cataclysm_sim.enemy_abilities import ATTACK_REACH, ring_distance

    contact_reach_enemies = ("Hellhound", "Brute")

    for name in contact_reach_enemies:
        contact = ring_distance(name, 0)
        assert ATTACK_REACH[name] == pytest.approx(contact), (
            f"{name} reaches {ATTACK_REACH[name]} m but standing against the "
            f"player is {contact:.2f} m. A contact reach has no margin at all, "
            f"so any difference means it either cannot touch the player or "
            f"reaches through them.")

    for name in ("Imp", "Succubus"):
        assert ATTACK_REACH[name] != pytest.approx(ring_distance(name, 0)), (
            f"{name}'s reach is now exactly contact, so it belongs in the list "
            f"above and this test is no longer checking what it says it is.")


def test_the_engine_measures_reach_on_the_floor_plane() -> None:
    """Which is what makes a contact reach reachable at all.

    WHY A SOURCE CHECK AND NOT A BEHAVIOUR TEST, said plainly because it is a
    weaker guard and the reason matters. Two capsule centres at contact are 90
    cm apart on the floor and 91.08 cm apart in three dimensions, because a
    Brute's capsule half-height is 110 cm against a player's 96. The controller
    chases only when the distance exceeds the reach plus
    `ContactToleranceCm`, and that tolerance is 2 cm -- wider than the 1.08 cm
    the height costs.

    SO NO BEHAVIOUR TEST CAN CATCH THIS TODAY, and that was measured rather than
    assumed: on 2026-08-08, changing `FVector::Dist2D` back to `FVector::Dist`
    in the controller left all 204 automation tests passing. The tolerance did
    not exist when issue #373 was written; it arrived in pull requests #385 and
    #388, after the floor-plane change in #367.

    `Cataclysm.Brute.EveryHeightGapFitsInsideTheContactTolerance` is the
    tripwire for the day a taller or wider creature makes the difference exceed
    the tolerance again.
    """
    if not ENEMY_CONTROLLER_CPP.is_file():
        pytest.fail(f"{ENEMY_CONTROLLER_CPP.name} does not exist")

    text = ENEMY_CONTROLLER_CPP.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"//[^\n]*", "", text)

    assert re.search(r"const\s+float\s+Distance\s*=\s*FVector::Dist2D\s*\(",
                     text), (
        "ACataclysmEnemyController::Think no longer measures the distance to "
        "its target on the floor plane. A contact reach is the two capsule "
        "radii, which is a floor-plane quantity; charging a creature for a "
        "height difference nobody chose is not what any designed reach means. "
        "See issue #373.")


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


def test_the_document_says_four_shapes_have_no_marker(succubus_section):
    from cataclysm_sim.enemy_abilities import SHAPES, TELEGRAPHED_SHAPES

    unmarked = sorted(set(SHAPES) - set(TELEGRAPHED_SHAPES))
    assert len(unmarked) == 4, (
        f"the telegraph table now draws {len(SHAPES) - len(unmarked)} of the "
        f"{len(SHAPES)} shapes. The Succubus subsection states that four have "
        "no marker: SelfBuff, Summon, Deployable and Debuff. Deployable was "
        "the fourth, added with issue #338.")
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


def test_no_enemy_modifier_is_named_after_an_enemy() -> None:
    """A creature and a modifier must not share a name. Issue #358.

    WHAT WENT WRONG. A Demonic enemy modifier was called Succubus, and so is one
    of the seven vertical slice enemies. A modifier is rolled onto an individual
    enemy, one per rarity above Common, so an Elite Succubus could roll the
    Succubus modifier and be named "Succubus Succubus" anywhere both are shown.
    That is not a display problem to hide: the two mean different things, and the
    modifier does something the creature does not do innately. It was renamed to
    Beguiling.

    THE SAME SHAPE AS THE TEST BELOW, one level up. That one stops an innate
    ability duplicating a modifier's EFFECT; this stops a modifier taking a
    creature's NAME.

    EVERY CATACLYSM, NOT ONLY THE CREATURE'S OWN, which is stricter than the
    test below and deliberately so. That one can argue a War modifier is safe on
    a Demonic enemy because it can never be rolled there. A name is read by a
    person, and two different things called the same thing are confusing whether
    or not one creature can hold both.

    A SHARED WORD IS NOT A SHARED NAME. Abyssal Aura is a modifier and the
    Abyssal Warden is an enemy; the two are compared whole, so that passes.
    Issue #358 raises it and reaches the same conclusion.
    """
    import csv

    from cataclysm_sim.enemy_stats import ARCHETYPES

    modifiers = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"
    if not modifiers.is_file():
        pytest.fail("game/Data/EnemyModifiers.csv is not present, so nothing "
                    "here is checked. Regenerate it with "
                    "python tools/generate_datatables.py")

    with modifiers.open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))

    assert rows, "game/Data/EnemyModifiers.csv has no rows to check."

    creatures = set(ARCHETYPES)
    clashes = sorted(
        (row["ModifierName"], row["CataclysmType"])
        for row in rows if row["ModifierName"] in creatures)

    assert not clashes, (
        f"these enemy modifiers are named after an enemy archetype: {clashes}. "
        f"A modifier is rolled onto an individual enemy, so a creature of that "
        f"name carrying a modifier of the same name is named twice over, and "
        f"the two mean different things. Rename the modifier: the archetype "
        f"name comes from the design document's own enemy table and is the more "
        f"expensive side to move. Issue #358.")


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
    a count of the designed skills, so the count is recomputed here.

    IT COUNTS DEMONIC ROWS ONLY, AND IT USED NOT TO. While every designed skill
    happened to be Demonic the two counts were the same number, so the missing
    filter was invisible. Issue #338 gave Bolt Turret, Ballista and Iron
    Fortress a Shape and all three are War, which took the unfiltered count to
    54 while the sentence in the design document -- which is about what a
    DEMONIC player carries -- was still correct at 51. The test was wrong, not
    the document.
    """
    import csv

    assert succubus.energy_shield_fraction > 0.0, (
        "the Succubus no longer has an energy shield, so the subsection about "
        "how its shield behaves describes nothing.")

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        designed = [row for row in csv.DictReader(handle)
                    if row["Shape"] and row["DamageType"] == "Demonic"]
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
    """Three abilities, and the count is pinned deliberately.

    THE ART CARRIES FIVE. The imported Paragon pack has animations for a rock
    throw, a second heavy smash and two self-buffs on top of the slam and the
    stomp. The Brute takes three, decided by the project owner on 2026-08-07:
    it is a common enemy, and a common enemy that opens with five abilities
    leaves nothing for rarity and modifiers to add.

    So this asserts three rather than "at least two". Adding a fourth to this
    creature should be a decision somebody makes on purpose, not something that
    happens because an animation existed. Issue #382.
    """
    assert len(brute_abilities) == 3, (
        f"the Brute now has {len(brute_abilities)} abilities. It is designed "
        "with three: a slam, a stomp and a thrown rock. The art carries five "
        "and the other two were declined on purpose -- extra abilities belong "
        "to rarities and modifiers, not to a basic mob.")

    names = {ability.name for ability in brute_abilities}
    assert names == {"Slam", "Stomp", "Rip and Toss"}, (
        f"the Brute's abilities are now {sorted(names)}.")

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


def test_the_stomp_cooldown_clears_the_stun_immunity_window(brute_abilities,
                                                            brute_section):
    """Not the Heavy slot's cooldown, which sits entirely inside the window.

    AT OR ABOVE, RATHER THAN EQUAL TO, SINCE 2026-08-09. The cooldown was
    exactly the window until then, so equality and "clears it" were the same
    assertion. The project owner raised it to 8 seconds by playing it. A stomp
    arriving LATER than the window is refused by nothing; arriving sooner is the
    failure, so that is what this checks.
    """
    import csv

    from cataclysm_sim.enemy_abilities import STUN_IMMUNITY_WINDOW

    stomp = next(a for a in brute_abilities if "StunSeconds" in a.params)
    assert stomp.cooldown >= STUN_IMMUNITY_WINDOW, (
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


def test_the_slam_gets_no_marker_while_the_stomp_does(
        brute, brute_abilities, brute_section):
    """The clarification this enemy forced: the telegraph table's Yes column is
    about the largest marker an enemy could draw, not about every attack.

    TWO INDEPENDENT REASONS THE SLAM GETS NONE, SINCE 2026-08-09. Its own 0.9
    metre reach was always under the one metre floor. That day the attack
    interval moved from 1.6 seconds to 1.2, and the largest marker a 1.2 second
    cycle allows is 0.70 metres, so the cycle is under the floor as well.

    THIS TEST USED TO OPT OUT WHEN THAT HAPPENED. Its first assertion was that
    the interval still allowed a marker at all, with the message "this test has
    nothing to say". A test that opts out reads in a run exactly like a test
    that passed, so it now asserts what is true instead and names both reasons.
    """
    from cataclysm_sim.enemy_abilities import (SMALLEST_USEFUL_MARKER_METRES,
                                               is_telegraphed,
                                               largest_telegraphed_radius)

    by_name = {a.name: a for a in brute_abilities}
    slam = by_name["Slam"]
    allowed = largest_telegraphed_radius(brute.attack_interval)

    assert not is_telegraphed(slam, brute), (
        f"the Brute's slam is now telegraphed. It reaches "
        f"{slam.params['Radius']} m and its {brute.attack_interval} s attack "
        f"interval allows {allowed:.2f} m, against a "
        f"{SMALLEST_USEFUL_MARKER_METRES} m floor. Both were under the floor "
        f"when this was written, so something has grown.")

    assert slam.params["Radius"] < SMALLEST_USEFUL_MARKER_METRES, (
        f"the slam now reaches {slam.params['Radius']} m, at or over the "
        f"{SMALLEST_USEFUL_MARKER_METRES} m floor. It is the example the "
        f"Brute's subsection uses for an attack too small to mark, so if it has "
        f"grown, that prose needs rewriting too.")

    # AND THE STOMP STILL DOES GET ONE, which is what makes the slam's absence
    # a statement about the slam rather than about the creature. The stomp is
    # telegraphed against its own cooldown, so shortening the attack interval
    # cannot take its marker away.
    assert is_telegraphed(by_name["Stomp"], brute), (
        "the Brute's stomp is no longer telegraphed, so the creature draws no "
        "marker at all and its entry in the telegraph table's Yes column is "
        "wrong.")

    assert "a marker under one metre is not drawn" in brute_section.lower(), (
        "the Brute subsection no longer states the rule its slam is the "
        "example of. Issue #351.")


def test_the_stomp_is_telegraphed_against_its_own_cooldown(
        brute, brute_abilities, brute_section):
    """The stomp's marker is sized by its cooldown, not the attack interval.

    THE RULE, docs/Cataclysm_GDD_v2.md: "An ability on a cooldown is
    telegraphed against its own cooldown, not the attack interval. Substitute
    the cooldown for the attack interval in the formula above."

    THIS TEST USED TO ASSERT THE OPPOSITE, and was right to at the time. When
    the stomp was designed the Brute's attack interval was 2.8 s, which allows
    a 3.5 m marker, and the stomp took exactly that -- so sizing it by the
    interval and by the 5 s cooldown were indistinguishable in the direction
    that mattered, and the interval was the tighter of the two.

    The attack interval moved to 1.6 s on 2026-08-07 because 2.8 played as too
    slow to be a threat. That interval allows only 1.4 m, and the stomp did NOT
    shrink with it, because the stomp never ran on the attack interval: it runs
    on its own 5 s cooldown, which allows 7.35 m. The stomp is unchanged and
    this test now says why that is correct rather than accidental.
    """
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED,
                                               largest_telegraphed_radius)

    stomp = next(a for a in brute_abilities if "StunSeconds" in a.params)
    radius = float(stomp.params["Radius"])

    allowed = largest_telegraphed_radius(stomp.cooldown)
    assert radius <= allowed + 1e-9, (
        f"the stomp draws {radius} m and its {stomp.cooldown} s cooldown "
        f"allows {allowed:.2f} m, so the player cannot clear it in time.")

    # NOT THE MAXIMUM, AND DELIBERATELY. The cooldown would allow 7.35 m with a
    # 2.5 s wind-up. That is legal and wrong: the Brute gets the walk-out kind
    # of telegraph, and the test below checks a player at its reach can walk
    # clear. A marker that large on a creature this slow is unmissable rather
    # than dodgeable.
    assert radius < allowed, (
        "the stomp takes the largest marker its cooldown allows. That is legal "
        "but it is not what was designed: see the walk-out test below.")

    # AND IT IS STILL BIGGER THAN THE BASIC SWING COULD EVER DRAW, which is the
    # whole reason it is the ability with the telegraph.
    from_interval = largest_telegraphed_radius(brute.attack_interval)
    assert radius > from_interval, (
        f"the stomp's {radius} m marker is no bigger than the {from_interval:.2f} m "
        f"the Brute's {brute.attack_interval} s attack interval would allow, so "
        "it is no longer the ability worth telegraphing.")

    wind_up = REACTION_ALLOWANCE + radius / WALK_OUT_SPEED
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
# The Corrupted Sentinel, issue #352
# --------------------------------------------------------------------------
#
# Everything here is recomputed from somewhere else rather than compared against
# a copy: the reach from the player skill table, the bolt's speed from the
# wind-up rule and that reach, the mortar's minimum range from its own marker
# and the creature's body, and its landing speed from its arc and real gravity.


@pytest.fixture(scope="module")
def sentinel_section() -> str:
    return subsection("Corrupted Sentinel")


@pytest.fixture(scope="module")
def sentinel():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Corrupted Sentinel")


@pytest.fixture(scope="module")
def sentinel_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Corrupted Sentinel")


def player_attack_ranges() -> list[float]:
    """Every `Range` a damaging player skill states, in metres.

    DEBUFF AND SUMMON ARE EXCLUDED, and that is the point of the function rather
    than an oversight. Two rows reach 15 metres -- Subjugate, a Debuff, and Open
    the Rift, a Summon -- and neither is an attack. The Sentinel's reach is set
    against the furthest anything can HIT it from.
    """
    import csv

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")

    attacking = {"Projectile", "Strike", "Movement"}
    ranges = []
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["Shape"] not in attacking:
                continue
            params = dict(pair.strip().split("=", 1)
                          for pair in row["ShapeParams"].split(";")
                          if "=" in pair)
            if "Range" in params:
                ranges.append(float(params["Range"]))
    assert ranges, "no player attack in game/Data/WeaponSkills.csv states a Range"
    return ranges


def player_projectile_speeds() -> list[float]:
    """Every non-zero `Speed` a player Projectile states, in centimetres/second."""
    import csv

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")

    speeds = []
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["Shape"] != "Projectile":
                continue
            params = dict(pair.strip().split("=", 1)
                          for pair in row["ShapeParams"].split(";")
                          if "=" in pair)
            speed = float(params.get("Speed", 0.0))
            if speed > 0.0:
                speeds.append(speed)
    assert speeds, "no player Projectile states a non-zero Speed"
    return speeds


def test_the_sentinel_ability_table_matches_the_data(sentinel_section,
                                                     sentinel_abilities):
    """Two abilities, and the document's table must carry both exactly as the
    data holds them."""
    assert len(sentinel_abilities) == 2, (
        f"the Corrupted Sentinel now has {len(sentinel_abilities)} abilities "
        "in sim/cataclysm_sim/enemy_abilities.py. The design document describes "
        "two: a bolt down a marked lane and a shell lobbed over cover.")
    for ability in sentinel_abilities:
        assert_row_matches(sentinel_section, ability, "Corrupted Sentinel")


def test_the_sentinel_never_moves_which_is_what_its_reach_rests_on(sentinel):
    """Every argument in its subsection starts here.

    A creature that could walk would not need the longest reach in the game,
    would not need a mortar to answer cover, and would not have to survive being
    stood on. If this figure ever stops being zero the subsection has to be
    re-argued rather than adjusted.
    """
    assert sentinel.move_speed == 0.0, (
        f"the Corrupted Sentinel now moves at {sentinel.move_speed} m/s. Its "
        "reach, its mortar and its answer to a melee character are all "
        "arguments from it being unable to move at all. Issue #352.")
    assert sentinel.chase_speed == 0.0, (
        f"the Corrupted Sentinel now has a chase speed of "
        f"{sentinel.chase_speed} m/s, so it moves once it has noticed the "
        "player. Same problem.")


def test_the_sentinel_reach_is_the_longest_range_any_player_attack_has():
    """Read out of the skill table, not written down here.

    THE FAILURE THIS CATCHES: somebody gives a player skill a longer range and
    the Sentinel silently becomes free to kill from outside its own reach, which
    is exactly what issue #352 asked this design to prevent.
    """
    from cataclysm_sim.enemy_abilities import ATTACK_REACH

    longest = max(player_attack_ranges())
    assert ATTACK_REACH["Corrupted Sentinel"] == pytest.approx(longest), (
        f"the Corrupted Sentinel reaches "
        f"{ATTACK_REACH['Corrupted Sentinel']} m and the longest range any "
        f"player attack has in game/Data/WeaponSkills.csv is {longest} m. A "
        "creature that cannot move and can be out-ranged is a free kill.")


def test_the_bolt_uses_the_largest_telegraph_its_interval_allows(
        sentinel, sentinel_abilities):
    """Its wind-up is then exactly half its attack interval, which is the most
    the rule permits. The same place the Succubus sits."""
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED,
                                               largest_telegraphed_radius)

    bolt = next(a for a in sentinel_abilities if a.is_basic_attack)
    largest = largest_telegraphed_radius(sentinel.attack_interval)

    assert float(bolt.params["Radius"]) == pytest.approx(largest), (
        f"the Siege Bolt marks {bolt.params['Radius']} m and a "
        f"{sentinel.attack_interval} s attack interval allows {largest:.2f} m. "
        "The design takes all of it, because a creature whose role is forcing "
        "the player to move should mark as much ground as the rule permits.")

    wind_up = REACTION_ALLOWANCE + float(bolt.params["Radius"]) / WALK_OUT_SPEED
    assert wind_up == pytest.approx(sentinel.attack_interval / 2.0), (
        f"the Siege Bolt's wind-up is {wind_up:.2f} s against a "
        f"{sentinel.attack_interval} s interval, so it is no longer exactly "
        "half. Taking the largest allowed radius is what makes those two the "
        "same number.")


def test_the_bolt_lands_exactly_as_the_next_one_is_marked(sentinel,
                                                          sentinel_abilities):
    """The whole of its rhythm, and the reason its speed is what it is.

    The cycle is one second of marker plus one second of flight. A longer flight
    puts a shot in the air while the next marker is already on the ground; a
    shorter one leaves a gap in which nothing is happening, and the design of
    this creature is that there is not one.
    """
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED)

    bolt = next(a for a in sentinel_abilities if a.is_basic_attack)
    wind_up = REACTION_ALLOWANCE + float(bolt.params["Radius"]) / WALK_OUT_SPEED
    flight = (float(bolt.params["Range"]) * CM_PER_METRE
              / float(bolt.params["Speed"]))

    assert wind_up + flight == pytest.approx(sentinel.attack_interval), (
        f"the Siege Bolt winds up for {wind_up:.2f} s and flies for "
        f"{flight:.2f} s, which is {wind_up + flight:.2f} s against a "
        f"{sentinel.attack_interval} s attack interval. The two are meant to "
        "add up to exactly the interval.")


def test_the_bolt_speed_is_the_slowest_in_the_table_that_still_lands_in_time():
    """It is a floor met exactly, not a number picked off a list.

    NOT A COMPARISON AGAINST ITS OWN DEFINITION. The floor is recomputed from
    the range and the flight budget, and then every speed the player skill table
    offers is tested against it. A slower one that also worked would fail this,
    because the design claims 1400 is the slowest that does.
    """
    from cataclysm_sim.enemy_abilities import (REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED, abilities)
    from cataclysm_sim.enemy_stats import archetype

    kind = archetype("Corrupted Sentinel")
    bolt = next(a for a in abilities("Corrupted Sentinel") if a.is_basic_attack)
    wind_up = REACTION_ALLOWANCE + float(bolt.params["Radius"]) / WALK_OUT_SPEED
    budget = kind.attack_interval - wind_up
    floor = float(bolt.params["Range"]) * CM_PER_METRE / budget

    workable = [speed for speed in player_projectile_speeds() if speed >= floor]
    assert workable, (
        f"no speed in game/Data/WeaponSkills.csv reaches the {floor:.0f} cm/s "
        f"the Siege Bolt needs to cross {bolt.params['Range']} m in "
        f"{budget:.2f} s.")

    assert float(bolt.params["Speed"]) == pytest.approx(min(workable)), (
        f"the Siege Bolt is {bolt.params['Speed']} cm/s and the slowest speed "
        f"in game/Data/WeaponSkills.csv that still crosses "
        f"{bolt.params['Range']} m inside {budget:.2f} s is {min(workable)}. A "
        "slower readable option existing means the design should be using it.")


def test_the_bolt_has_no_minimum_range_and_the_mortar_has_one(
        sentinel, sentinel_abilities):
    """The distinction the whole melee answer rests on.

    A lane starts at the caster, so the caster is at its origin. A circle can
    cover the caster, so it needs a floor. Asking a flat shot for a minimum
    range raises rather than returning a number, because a number returned for
    one is a limit somebody could apply by mistake.
    """
    from cataclysm_sim.enemy_abilities import (PLAYER_BODY_RADIUS, is_lobbed,
                                               lob_minimum_range)

    bolt = next(a for a in sentinel_abilities if a.is_basic_attack)
    mortar = next(a for a in sentinel_abilities if not a.is_basic_attack)

    assert not is_lobbed(bolt), (
        "the Siege Bolt now carries an Arc, so it is a lob. The design says a "
        "melee character standing against the Sentinel is standing in its "
        "lane, which is only true of a flat shot.")
    with pytest.raises(ValueError):
        lob_minimum_range(bolt, sentinel)

    assert is_lobbed(mortar), (
        "the Brimstone Mortar no longer carries an Arc, so it is a flat shot "
        "and cannot reach over cover, which is the only reason it exists.")

    minimum = lob_minimum_range(mortar, sentinel)
    assert minimum == pytest.approx(float(mortar.params["Radius"])
                                    + sentinel.body_radius), (
        f"the mortar's minimum range is {minimum} m and its marked circle plus "
        f"the creature's body radius is {mortar.params['Radius']} + "
        f"{sentinel.body_radius}. Below that sum it stands inside its own "
        "blast.")

    contact = PLAYER_BODY_RADIUS + sentinel.body_radius
    assert minimum > contact, (
        f"the mortar's minimum range is {minimum} m and the two bodies already "
        f"touch at {contact} m, so it refuses nothing. That is the state issue "
        "#475 was filed about on the Brute.")


def test_the_mortar_lands_slower_than_the_ceiling_the_rock_is_held_to(
        sentinel_abilities):
    """A lob is fastest as it lands, so that is where the ceiling bites.

    The ceiling is the Succubus's Soulfire, which is the slowest projectile any
    player skill uses. It is read out of the design rather than written here, so
    retuning the Succubus moves it.
    """
    import math

    from cataclysm_sim.enemy_abilities import abilities

    ceiling = float(next(a for a in abilities("Succubus")
                         if a.is_basic_attack).params["Speed"])
    mortar = next(a for a in sentinel_abilities if not a.is_basic_attack)

    gravity = 980.0
    reach = float(mortar.params["Range"]) * CM_PER_METRE
    flight = math.sqrt(8.0 * float(mortar.params["Arc"]) * reach / gravity)
    landing = math.hypot(reach / flight, gravity * flight / 2.0)

    assert landing <= ceiling, (
        f"the Brimstone Mortar lands at {landing:.0f} cm/s at its full "
        f"{mortar.params['Range']} m and the slowest projectile any player "
        f"skill uses is {ceiling:.0f} cm/s. An enemy shot arriving faster than "
        "anything the player has ever seen is not readable.")


def test_the_two_sentinel_markers_are_different_sizes(sentinel_abilities):
    """A player has to tell them apart at a glance, and radius is the only
    thing that distinguishes them: both are Projectiles from one creature."""
    bolt = next(a for a in sentinel_abilities if a.is_basic_attack)
    mortar = next(a for a in sentinel_abilities if not a.is_basic_attack)

    assert float(mortar.params["Radius"]) > float(bolt.params["Radius"]), (
        f"the mortar marks {mortar.params['Radius']} m and the bolt marks "
        f"{bolt.params['Radius']} m. The heavier ability drawing the smaller or "
        "equal marker means the player cannot tell which one is coming.")


def test_both_sentinel_abilities_are_telegraphed(sentinel, sentinel_abilities):
    """Nothing this creature does is read off its body, which is the answer to
    the question issue #352 asks about its telegraph."""
    from cataclysm_sim.enemy_abilities import fits_its_cycle, is_telegraphed

    for ability in sentinel_abilities:
        assert is_telegraphed(ability, sentinel), (
            f"the Corrupted Sentinel's {ability.name} no longer draws a ground "
            "marker. Its whole design is that the player reads the ground.")
        assert fits_its_cycle(ability, sentinel), (
            f"the Corrupted Sentinel's {ability.name} draws a marker too large "
            "for its cycle, which by the design document's own words makes it "
            "a damage event rather than a telegraph.")


def test_the_mortar_cooldown_is_inside_the_special_slot_band(
        sentinel_abilities):
    import csv

    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    if not slots.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with slots.open(encoding="utf-8-sig", newline="") as handle:
        special = next(row for row in csv.DictReader(handle)
                       if row["Slot"] == "Special")

    mortar = next(a for a in sentinel_abilities if not a.is_basic_attack)
    lowest = float(special["CooldownLowest"])
    highest = float(special["CooldownHighest"])

    assert lowest <= mortar.cooldown <= highest, (
        f"the Brimstone Mortar's cooldown is {mortar.cooldown} s and the "
        f"Special slot's band in game/Data/SkillSlots.csv is {lowest} to "
        f"{highest} s.")


def test_the_document_says_cover_works_and_that_the_bolt_does_not_track(
        sentinel_section):
    """Two claims a reader needs and no number carries.

    Neither can be recomputed, so what is checked is that the section still
    makes them. Losing either changes what the creature is: one that tracks
    makes its own marker decorative, and one that shoots through walls removes
    the only counterplay a stationary creature offers.
    """
    lowered = sentinel_section.lower()

    assert "fixed when the wind-up starts" in lowered, (
        "the Corrupted Sentinel's subsection no longer says its lane is fixed "
        "when the wind-up starts. An attack that follows the player cannot be "
        "walked out of, which makes the telegraph decorative.")

    assert "line of sight" in lowered and "geometry blocks" in lowered, (
        "the subsection no longer says geometry blocks the bolt and that it "
        "does not fire without line of sight. Breaking line of sight is the "
        "counterplay a creature that cannot move is supposed to have.")


def test_a_projectile_may_not_state_both_a_speed_and_an_arc():
    """The two describe different motion, so an ability carrying both states
    two trajectories and whichever the engine reads first silently wins.

    The exclusivity has been in the `SHAPE_PARAMS` docstring since issue #474
    and nothing checked it until the Corrupted Sentinel arrived with one of
    each.
    """
    import dataclasses

    from cataclysm_sim import enemy_abilities

    both = dataclasses.replace(
        next(a for a in enemy_abilities.abilities("Corrupted Sentinel")
             if a.is_basic_attack),
        params={"Range": 14, "Radius": 2.1, "Speed": 1400, "Arc": 0.25})

    original = enemy_abilities.ABILITIES["Corrupted Sentinel"]
    enemy_abilities.ABILITIES["Corrupted Sentinel"] = (both,)
    try:
        with pytest.raises(AssertionError, match="Speed"):
            enemy_abilities._check_every_projectile_states_a_speed_or_an_arc_but_not_both()
    finally:
        enemy_abilities.ABILITIES["Corrupted Sentinel"] = original


# --------------------------------------------------------------------------
# The Abyssal Warden, issue #353
# --------------------------------------------------------------------------
#
# Two facts carry this creature's whole design and both are recomputed here
# rather than asserted: it is the hardest thing in the slice to hurt, and it is
# the only designed enemy that cannot catch the player.


@pytest.fixture(scope="module")
def warden_section() -> str:
    return subsection("Abyssal Warden")


@pytest.fixture(scope="module")
def warden():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Abyssal Warden")


@pytest.fixture(scope="module")
def warden_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Abyssal Warden")


def test_the_warden_ability_table_matches_the_data(warden_section,
                                                   warden_abilities):
    """Three abilities, and the document's table must carry all three exactly
    as the data holds them."""
    assert len(warden_abilities) == 3, (
        f"the Abyssal Warden now has {len(warden_abilities)} abilities in "
        "sim/cataclysm_sim/enemy_abilities.py. The design document describes "
        "three: a swing, a charge and a ring at its feet.")
    for ability in warden_abilities:
        assert_row_matches(warden_section, ability, "Abyssal Warden")


#: An enemy that has to close to attack. Every ranged enemy in the slice reaches
#: 8 metres or more -- the Succubus 8, the Corrupted Sentinel 14 -- and every
#: melee one 1.32 or less, so the gap is wide and this sits in it.
MELEE_REACH_CEILING_METRES = 2.0


def test_exactly_two_melee_enemies_cannot_catch_the_player():
    """The reason the Warden has a charge and the Gatekeeper a mortar.

    RECOMPUTED ACROSS EVERY ENEMY, because the claim in the Warden's subsection
    is that these two are the only ones, and the two answers exist because of
    it. A third enemy joining this list without its own answer would be one the
    player walks away from and never fights.

    MELEE, WHICH IS THE WORD THIS TEST ADDED. A first version of this checked
    every designed enemy and failed, because the Succubus also moves at 3.5 m/s
    and cannot catch the fastest class. It does not need to: it reaches 8 metres.
    Being unable to close only matters for a creature that has to.
    """
    from cataclysm_sim.enemy_abilities import ABILITIES, ATTACK_REACH
    from cataclysm_sim.enemy_stats import archetype

    # The fastest of the three Demonic classes, from the class stat table in
    # the design document. An enemy has to beat it to catch anybody.
    fastest_class = 4.6

    cannot_catch = []
    for name in ABILITIES:
        if ATTACK_REACH.get(name, 0.0) > MELEE_REACH_CEILING_METRES:
            continue
        kind = archetype(name)
        if max(kind.move_speed, kind.chase_speed) < fastest_class:
            cannot_catch.append(name)

    assert cannot_catch == ["Abyssal Warden", "Gatekeeper"], (
        f"these designed melee enemies cannot catch the fastest Demonic class "
        f"at {fastest_class} m/s: {cannot_catch}. The design says exactly two "
        "-- the Abyssal Warden, whose answer is its charge, and the "
        "Gatekeeper, whose answer is its mortar. A new name here needs its own "
        "answer or it can be walked away from and never fought.")


def test_the_wardens_charge_goes_further_than_it_could_simply_walk(
        warden, warden_abilities):
    """The test the Hellhound's charge design sets, applied here.

    A gap-closer that covers less ground than the creature would have covered by
    walking during its own wind-up is strictly worse than not winding up at all.
    """
    from cataclysm_sim.enemy_abilities import REACTION_ALLOWANCE, WALK_OUT_SPEED

    charge = next(a for a in warden_abilities if a.shape == "Movement")
    wind_up = (REACTION_ALLOWANCE
               + float(charge.params["Radius"]) / WALK_OUT_SPEED)
    walked = warden.move_speed * wind_up

    assert float(charge.params["Range"]) > walked, (
        f"the Abyssal Warden's charge covers {charge.params['Range']} m and it "
        f"could walk {walked:.2f} m during its own {wind_up:.2f} s wind-up. A "
        "charge shorter than that is worse than not winding up.")


def test_the_wardens_charge_range_is_the_shortest_movement_skill_range(
        warden_abilities):
    """The shortest, because this is the slowest creature in the slice.

    Read out of the player skill table rather than written here, so adding a
    shorter Movement skill fails this rather than silently making the sentence
    in the design document false.
    """
    import csv

    skills = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
    if not skills.is_file():
        pytest.skip("game/Data/WeaponSkills.csv is not present")
    ranges = []
    with skills.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["Shape"] != "Movement":
                continue
            params = dict(pair.strip().split("=", 1)
                          for pair in row["ShapeParams"].split(";")
                          if "=" in pair)
            if "Range" in params:
                ranges.append(float(params["Range"]))

    assert ranges, "no Movement-shape skill in game/Data/WeaponSkills.csv"
    charge = next(a for a in warden_abilities if a.shape == "Movement")
    assert float(charge.params["Range"]) == pytest.approx(min(ranges)), (
        f"the Abyssal Warden's charge covers {charge.params['Range']} m and the "
        f"shortest Movement-shape skill range is {min(ranges)} m. The shortest "
        "is the figure the design uses, because this is the slowest creature.")


def test_the_ring_is_the_largest_marker_any_enemy_draws(warden_abilities):
    """The claim its subsection makes, recomputed against every other ability.

    A second enemy drawing something larger would make the sentence false, and
    the Gatekeeper is still to be designed.

    ONLY TELEGRAPHED ABILITIES COUNT, which is the correction this test needed.
    A first version compared raw radii and failed on the Succubus's Dominion,
    an 8 metre Aura. Dominion is held on rather than cast, so it has no cycle
    and draws no marker at all. A radius that no marker is drawn from is not a
    telegraph, and the claim is about telegraphs.
    """
    from cataclysm_sim.enemy_abilities import ABILITIES, is_telegraphed
    from cataclysm_sim.enemy_stats import archetype

    ring = next(a for a in warden_abilities if a.slot == "Ultimate")
    mine = float(ring.params["Radius"])

    for name, entries in ABILITIES.items():
        kind = archetype(name)
        for ability in entries:
            if ability is ring or not is_telegraphed(ability, kind):
                continue
            other = float(ability.params.get("Radius", 0.0))
            assert other <= mine, (
                f"{name}'s {ability.name} draws a {other} m marker and the "
                f"Abyssal Warden's ring draws {mine} m. Its subsection calls "
                "the ring the largest telegraph in the game.")


def test_the_ring_is_bigger_than_the_wardens_own_interval_allows(
        warden, warden_abilities):
    """The lower bound on the judgement, and the reason the number is not free.

    Below the largest marker its ordinary attack interval could draw, the ring
    is not categorically different from what the creature does every 2.4
    seconds, which is the whole argument for it.
    """
    from cataclysm_sim.enemy_abilities import (largest_telegraphed_radius,
                                               telegraph_cap_metres)

    ring = next(a for a in warden_abilities if a.slot == "Ultimate")
    radius = float(ring.params["Radius"])
    interval_allows = largest_telegraphed_radius(warden.attack_interval)

    assert radius > interval_allows, (
        f"the ring marks {radius} m and the Abyssal Warden's "
        f"{warden.attack_interval} s attack interval already allows "
        f"{interval_allows:.2f} m. Below that it is not a different kind of "
        "attack from its ordinary swing.")

    # AT THE CAP, NOT MERELY UNDER IT. Since 2026-08-09 the ring sits exactly at
    # the largest marker the rules permit, which is what makes it the hardest
    # telegraph in the game to escape rather than only the widest. Issues #487
    # and #496.
    cap = telegraph_cap_metres(warden)
    assert radius == pytest.approx(cap), (
        f"the ring marks {radius} m and the cap for this creature is "
        f"{cap:.2f} m. The ring is designed to sit exactly at the cap: above it "
        "the slowest class cannot both react and walk clear, and below it the "
        "ring is not the hardest telegraph the rules allow.")


def test_the_ring_cooldown_is_five_swings_and_the_bottom_of_its_slot_band(
        warden, warden_abilities):
    """Two independent derivations of the same 12 seconds.

    The design says it is how long the creature needs to kill the reference
    geared character -- 5 hits at a 2.4 second interval -- and that it is the
    bottom of the Ultimate slot's band. Both are checked, because either one
    alone could be a coincidence.
    """
    import csv

    ring = next(a for a in warden_abilities if a.slot == "Ultimate")

    assert ring.cooldown == pytest.approx(5 * warden.attack_interval), (
        f"the ring comes round every {ring.cooldown} s and five of the "
        f"Warden's {warden.attack_interval} s swings is "
        f"{5 * warden.attack_interval} s. The design derives the cooldown from "
        "the five hits it takes to kill the reference geared character.")

    slots = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
    if not slots.is_file():
        pytest.skip("game/Data/SkillSlots.csv is not present")
    with slots.open(encoding="utf-8-sig", newline="") as handle:
        ultimate = next(row for row in csv.DictReader(handle)
                        if row["Slot"] == "Ultimate")

    assert ring.cooldown == pytest.approx(float(ultimate["CooldownLowest"])), (
        f"the ring's cooldown is {ring.cooldown} s and the bottom of the "
        f"Ultimate slot's band in game/Data/SkillSlots.csv is "
        f"{ultimate['CooldownLowest']} s.")


def test_the_warden_is_the_first_enemy_to_use_the_ultimate_slot():
    """A claim its subsection makes, and the kind that goes stale silently."""
    from cataclysm_sim.enemy_abilities import ABILITIES

    users = sorted({name for name, entries in ABILITIES.items()
                    for a in entries if a.slot == "Ultimate"})

    # THE WARDEN WAS FIRST AND THE GATEKEEPER FOLLOWED, which its subsection's
    # word "first" still allows. Both rings are the 6.5 metre cap; what differs
    # is the cooldown and what else is on the floor when they land.
    assert users == ["Abyssal Warden", "Gatekeeper"], (
        f"these enemies use the Ultimate slot: {users}. The design gives it to "
        "exactly two: the Abyssal Warden first, then the Gatekeeper's phase 3.")


def test_the_margin_for_walking_out_is_the_same_at_every_radius(warden):
    """The property the design document now states, computed rather than quoted.

    The wind-up is 0.4 + Radius / 3.5, so the slowest class walks 1.4 + Radius
    metres during it, while a player at contact crosses Radius - 0.9. The
    difference is constant. If the reaction allowance or the walk speed ever
    changes, the constant changes and the document's figure goes stale, which is
    what this catches.
    """
    from cataclysm_sim.enemy_abilities import (PLAYER_BODY_RADIUS,
                                               REACTION_ALLOWANCE,
                                               WALK_OUT_SPEED)

    contact = PLAYER_BODY_RADIUS + warden.body_radius
    margins = []
    for radius in (1.0, 2.1, 3.5, 5.6, 8.0):
        wind_up = REACTION_ALLOWANCE + radius / WALK_OUT_SPEED
        margins.append(WALK_OUT_SPEED * wind_up - (radius - contact))

    assert all(m == pytest.approx(margins[0]) for m in margins), (
        f"the margin for walking out of a marker is no longer constant across "
        f"radii: {[round(m, 3) for m in margins]}. The design document states "
        "it is the same at every radius.")

    stated = f"{margins[0]:.1f} metres"
    text = GDD.read_text(encoding="utf-8")
    assert stated in text, (
        f"the design document no longer states the {stated} margin that the "
        "wind-up formula gives at every radius. It is the thing that makes "
        "'a bigger marker is not harder to escape' true.")


def test_the_wardens_swing_is_not_telegraphed_but_its_interval_would_allow_one(
        warden, warden_abilities):
    """The distinction from the Brute, which fails both conditions.

    The Warden's swing is refused a marker by its own 0.9 m reach, not by its
    cycle. That is what the subsection claims, and it is a different sentence
    from the Brute's.
    """
    from cataclysm_sim.enemy_abilities import (SMALLEST_USEFUL_MARKER_METRES,
                                               is_telegraphed,
                                               largest_telegraphed_radius)

    swing = next(a for a in warden_abilities if a.is_basic_attack)

    assert not is_telegraphed(swing, warden), (
        "the Abyssal Warden's swing now draws a marker. A 0.9 m marker is "
        "smaller than the creature standing in it.")
    assert (largest_telegraphed_radius(warden.attack_interval)
            >= SMALLEST_USEFUL_MARKER_METRES), (
        f"the Abyssal Warden's {warden.attack_interval} s attack interval no "
        "longer allows a marker of at least "
        f"{SMALLEST_USEFUL_MARKER_METRES} m, so it now fails the same two "
        "conditions the Brute does and its subsection's distinction is gone.")


def test_no_vertical_slice_enemy_is_described_as_positionally_vulnerable():
    """The project owner ruled positional weak points out on 2026-08-09.

    THE FAILURE THIS CATCHES is the description coming back, in this table or in
    a new creature's row. Nothing in the project implements damage that varies
    by where a creature is hit, so a description that promises one is a promise
    the game does not keep.
    """
    text = GDD.read_text(encoding="utf-8")
    start = text.find("## **Vertical Slice Enemies (Demonic Cataclysm)**")
    assert start != -1, (
        "docs/Cataclysm_GDD_v2.md no longer has a Vertical Slice Enemies "
        "section, which is where each of the seven is described in one line.")
    table = text[start:text.find("\n## ", start + 1)]

    for phrase in ("vulnerable at", "weak point", "weak spot"):
        assert phrase not in table.lower(), (
            f"the Vertical Slice Enemies table describes an enemy with "
            f"{phrase!r}. Positional weak points were ruled out on 2026-08-09: "
            '"we don\'t do positional weak points. That\'s too tedious in a '
            'diablo like arpg". Nothing implements hit-location damage.')


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
    rather than a telegraph.

    THE SECOND TIER IS GONE, and the middle case below is where it used to be.
    A 7 metre marker on a 5 second cooldown was legal until 2026-08-09 "because
    that is the tier the Movement slot answers"; it is now illegal because it is
    past the cap. Issue #487 recorded that the tier was unreachable for almost
    every cooldown and that it was more forgiving rather than harder.
    """
    from cataclysm_sim.enemy_abilities import (Ability, fits_its_cycle,
                                               telegraph_cap_metres)
    from cataclysm_sim.enemy_stats import archetype

    brute = archetype("Brute")
    cap = telegraph_cap_metres(brute)

    # TOO SHORT A CYCLE FOR ITS OWN WIND-UP. A 3 second cooldown allows a 1.5
    # second wind-up and 7 metres needs the full 2.0.
    too_big = Ability(name="Test", shape="Strike", slot="Heavy",
                      params={"Radius": 7.0}, cooldown=3.0)
    assert not fits_its_cycle(too_big, brute)

    # AND THE SAME RADIUS ON A LONGER COOLDOWN IS STILL ILLEGAL, because it is
    # past the cap. This is the case that changed.
    assert 7.0 > cap
    former_movement_tier = Ability(name="Test", shape="Strike", slot="Heavy",
                                   params={"Radius": 7.0}, cooldown=5.0)
    assert not fits_its_cycle(former_movement_tier, brute)

    # EITHER SIDE OF THE CAP, on a cooldown long enough that only the cap can be
    # the thing refusing it. This is what makes the cap the operative rule
    # rather than the half-cycle test.
    at_the_cap = Ability(name="Test", shape="Strike", slot="Ultimate",
                         params={"Radius": cap}, cooldown=12.0)
    assert fits_its_cycle(at_the_cap, brute)

    just_over = Ability(name="Test", shape="Strike", slot="Ultimate",
                        params={"Radius": cap + 0.1}, cooldown=12.0)
    assert not fits_its_cycle(just_over, brute)

    # And 9 metres is not legal at any cooldown: nothing the player has crosses
    # it.
    beyond_reach = Ability(name="Test", shape="Strike", slot="Heavy",
                           params={"Radius": 9.0}, cooldown=30.0)
    assert not fits_its_cycle(beyond_reach, brute)


def test_the_cap_applies_to_an_ability_that_draws_no_marker():
    """ISSUE #500. The cap used to sit after the telegraph test, so anything
    untelegraphed skipped it and could state any radius at all.

    THAT EXEMPTED THE DANGEROUS CASE. An ability escapes being telegraphed by
    being fast -- the Imp's Rend and the Hellhound's Maul are both under the
    threshold -- so a fast, huge, unannounced area was legal, and an unannounced
    nine metre ring is strictly worse than an announced one.

    Three ways an ability avoids drawing a marker, and the cap now reaches all
    three: a cycle too short to telegraph, a shape with no marker in the table,
    and a radius under the one metre marker floor. The last cannot break the cap
    on its own, so it is not listed below.
    """
    from cataclysm_sim.enemy_abilities import (Ability, fits_its_cycle,
                                               is_telegraphed,
                                               telegraph_cap_metres)
    from cataclysm_sim.enemy_stats import archetype

    imp = archetype("Imp")
    cap = telegraph_cap_metres(imp)
    assert 9.0 > cap

    # A CYCLE TOO SHORT TO TELEGRAPH. The Imp's attack interval allows a marker
    # well under the one metre floor, so a Basic on it draws nothing.
    too_fast = Ability(name="Test", shape="Strike", slot="Basic",
                       params={"Radius": 9.0, "Angle": 360})
    assert not is_telegraphed(too_fast, imp)
    assert not fits_its_cycle(too_fast, imp)

    # A SHAPE WITH NO MARKER. Summon is one of the four the telegraph table
    # draws nothing for.
    no_marker = Ability(name="Test", shape="Summon", slot="Special",
                        params={"Radius": 9.0}, cooldown=10.0)
    assert not is_telegraphed(no_marker, imp)
    assert not fits_its_cycle(no_marker, imp)

    # The control: the same untelegraphed shapes are fine under the cap, so this
    # is a cap being applied and not a blanket refusal.
    under_the_cap = Ability(name="Test", shape="Strike", slot="Basic",
                            params={"Radius": 1.32, "Angle": 360})
    assert not is_telegraphed(under_the_cap, imp)
    assert fits_its_cycle(under_the_cap, imp)


def test_an_aura_held_on_is_exempt_from_the_cap_and_one_on_a_cooldown_is_not():
    """THE DECISION ISSUE #500 HAD TO MAKE, and the Succubus's Dominion is what
    forced it: an 8.00 metre field, over the 6.50 metre cap, that would be
    refused outright by a cap applied to everything.

    The cap is about a moment. It asks whether the player can be clear by the
    time an attack lands, and a field that is simply on has no moment it lands --
    the player may walk out whenever they choose, and the design's stated counter
    is killing the caster, which ends it at once.

    AN AURA ON A COOLDOWN IS NOT EXEMPT. It fires at a moment like anything else.
    Nothing designed does that today, which is why the second half of this test
    is written against a constructed ability.
    """
    from cataclysm_sim.enemy_abilities import (Ability, abilities,
                                               fits_its_cycle,
                                               telegraph_cap_metres)
    from cataclysm_sim.enemy_stats import archetype

    succubus = archetype("Succubus")
    cap = telegraph_cap_metres(succubus)

    dominion = next(a for a in abilities("Succubus") if a.slot == "Aura")
    assert dominion.is_held_on
    assert float(dominion.params["Radius"]) > cap, (
        "Dominion no longer exceeds the cap, so this test is checking nothing. "
        "The exemption only means something while a held-on aura is over it.")
    assert fits_its_cycle(dominion, succubus)

    on_a_cooldown = Ability(name="Test", shape="Aura", slot="Ultimate",
                            params={"Radius": float(dominion.params["Radius"])},
                            cooldown=12.0)
    assert not on_a_cooldown.is_held_on
    assert not fits_its_cycle(on_a_cooldown, succubus)

def test_an_undesigned_enemy_raises_rather_than_returning_nothing():
    """Returning an empty list would let a caller treat an undesigned enemy as
    a finished one that does nothing.

    ALL SEVEN SLICE ENEMIES ARE NOW DESIGNED, so the undesigned example is the
    Baseline archetype -- the model's stand-in for an enemy nobody has worked
    on, which is exactly the thing this guard protects."""
    from cataclysm_sim.enemy_abilities import abilities

    with pytest.raises(ValueError, match="no designed abilities"):
        abilities("Baseline")

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

# --------------------------------------------------------------------------
# The Gatekeeper
#
# **IT HAD NO COMPARISON AT ALL UNTIL 2026-08-20**, and that is exactly why
# its Soulfall lost five riders without anything noticing. Six of the seven
# designed enemies had a table check and the boss did not. Issue #774.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def gatekeeper_section() -> str:
    return subsection("Gatekeeper")


@pytest.fixture(scope="module")
def gatekeeper():
    from cataclysm_sim.enemy_stats import archetype
    return archetype("Gatekeeper")


@pytest.fixture(scope="module")
def gatekeeper_abilities():
    from cataclysm_sim.enemy_abilities import abilities
    return abilities("Gatekeeper")


def test_the_gatekeeper_ability_table_matches_the_data(gatekeeper_section,
                                                       gatekeeper_abilities):
    """Four abilities, and the document's table must carry all four exactly as
    the data holds them -- in both directions.

    **THIS IS THE TEST THAT DID NOT EXIST**, and its absence is why the
    Gatekeeper's Soulfall carried `Range`, `Radius`, `Pierce` and `Arc` in the
    model while the document stated those four plus `Burn`, `GroundRadius`,
    `GroundDuration`, `GroundPercent` and `GroundHitsAllies`. The burning
    ground the whole ability exists to leave behind was in the design and
    absent from the data. Issue #774."""
    assert len(gatekeeper_abilities) == 4, (
        f"the Gatekeeper now has {len(gatekeeper_abilities)} abilities in "
        "sim/cataclysm_sim/enemy_abilities.py. The design document describes "
        "four: a telegraphed sweep, a lobbed gout, a summon and a ring.")
    for ability in gatekeeper_abilities:
        assert_row_matches(gatekeeper_section, ability, "Gatekeeper")


def test_soulfall_leaves_burning_ground_that_lasts_its_whole_cooldown(
        gatekeeper_abilities):
    """The design's claim is that the arena shrinks by one patch per cycle and
    that one patch is always down: "GroundDuration equals the cooldown, so in
    steady state one patch of burning ground is always down". That only holds
    while the two numbers are equal."""
    soulfall = next(a for a in gatekeeper_abilities if a.name == "Soulfall")

    assert "GroundDuration" in soulfall.params, (
        "Soulfall states no GroundDuration, so it leaves no burning ground at "
        "all and the boss has no answer to a player who stands off beyond one "
        "burst. Issue #774 is this happening once already.")

    assert soulfall.params["GroundDuration"] == pytest.approx(
            soulfall.cooldown), (
        f"Soulfall's ground lasts {soulfall.params['GroundDuration']} s on a "
        f"{soulfall.cooldown} s cooldown. The design requires them equal: "
        f"shorter and the arena stops shrinking, longer and the patches "
        f"accumulate faster than they expire.")


def test_soulfalls_burning_ground_costs_one_full_hit_over_its_life(
        gatekeeper_abilities):
    """The project's stated rule for a burning patch is `100 / GroundDuration`
    per second, so standing in one for its whole life costs exactly one hit.
    The Hellhound's 25 over 4 seconds is the same arithmetic."""
    soulfall = next(a for a in gatekeeper_abilities if a.name == "Soulfall")

    duration = soulfall.params.get("GroundDuration")
    percent = soulfall.params.get("GroundPercent")
    assert duration and percent, (
        "Soulfall states no GroundDuration or no GroundPercent, so how much "
        "its burning ground is worth is undefined.")

    assert percent == pytest.approx(100.0 / duration), (
        f"Soulfall's ground deals {percent}% a second for {duration} s, which "
        f"is {percent * duration}% of a hit over its life. The rule is "
        f"100 / GroundDuration, which here is {100.0 / duration}.")


def test_soulfalls_ground_burns_the_gatekeepers_own_summons(
        gatekeeper_abilities):
    """The design's own words: it "also burns the Gatekeeper's own summons".

    That is the counterplay phase 2 depends on -- the Imps chase the player
    through the fire and burn in it -- so a Soulfall that spared allies would
    make the summons strictly better rather than a mixed blessing."""
    soulfall = next(a for a in gatekeeper_abilities if a.name == "Soulfall")

    assert soulfall.params.get("GroundHitsAllies"), (
        "Soulfall's ground does not hit allies, and the design says it burns "
        "the Gatekeeper's own summons. Without it, Call the Damned in phase 2 "
        "has no cost at all.")

    assert "burns the Gatekeeper's own summons" in soulfall.note, (
        "Soulfall's note no longer says its ground burns the summons, so the "
        "rider above and the prose beside it disagree.")
