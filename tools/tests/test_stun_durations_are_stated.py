"""Anything that applies a stun says how long it lasts.

WHY THIS EXISTS. Issue #271. `game/Data/WeaponSkills.csv`, the generated table of
designed skills, had four skills that stun. Three stated a duration and Whip
Swing said only "briefly stuns them on your arrival".

WHY "BRIEFLY" STOPPED BEING HARMLESS. The anti-stun-lock rule was settled under
issue #216 and written into `docs/Cataclysm_GDD_v2.md`, the main design document,
under "Stun and the Anti-Stun-Lock Rule". One of its three rules is that a stunned
target cannot be stunned again for 5 seconds. A stun's duration is now a number
that interacts with another number, so there was no way to tell whether Whip
Swing's stun was shorter than Lunge's 0.75 seconds, longer, or the same. It was
also the only one of the four that no test could say anything about.

WHAT WAS DECIDED. 0.75 seconds, the same as Lunge. Both are Movement-slot skills
that stun a single target on arrival, and it is the shortest duration any designed
skill uses. That last part is not a preference: `BLUNT_STUN_SECONDS` in
`sim/cataclysm_sim/damage.py` is set to 0.75 and its comment says it is
"deliberately the shortest duration any designed skill uses, matching the Lunge
skill's 0.75 seconds". Giving Whip Swing anything shorter would have made that
comment false without anything noticing, so this file checks the relationship
rather than leaving it as a comment.

IMMUNITY IS NOT APPLICATION. Two Ultimate skills say "you cannot be stunned,
slowed or knocked back". They mention stun and correctly state no duration,
because they do not apply one. The classifier below is what separates the two,
and it is the part most likely to need extending when new wording appears.

WHAT IS ASSERTED HERE.

    every row in every generated table that applies a stun states a duration
    the four stunning weapon skills have the exact durations they were designed
      with, Whip Swing among them
    the design document's list of stunning skills matches the data
    the shortest designed stun equals the simulation constant that claims to
      match it
    a row that only grants immunity to stun is not required to state one
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: What each stunning weapon skill stuns for, in seconds. Whip Swing was the one
#: with no number; the other three are what the design already carried.
SKILL_STUN_SECONDS = {
    "War_Shield_Heavy": 1.5,        # Shield Bash
    "War_Warhammer_Movement": 1.0,  # Shockwave Leap
    "War_Sword_Movement": 0.75,     # Lunge
    "War_Whip_Movement": 0.75,      # Whip Swing. Issue #271.
}

#: Wording that mentions stun in order to say it does NOT happen. A row matching
#: one of these is not required to state a duration.
IMMUNITY = (
    r"cannot be stunned",
    r"immune to stun",
    r"immunity to stun",
)

#: A stated duration: "for 1.5 seconds", "for 0.5-1 second", "for 1 second".
DURATION = re.compile(r"stun\w*[^.]{0,60}?for\s+([\d.]+)(?:\s*-\s*[\d.]+)?\s*seconds?",
                      re.IGNORECASE)

#: Rows that DEFINE a hard stop rather than apply one, as (table, row key).
#:
#: WHY THEY NEED AN EXEMPTION. Issue #363 added Stun and Knockdown to the status
#: effect table, so that `Effect=Stun` is writable and so the anti-stun-lock
#: rules live somewhere the data can reference. Those two rows state the rules,
#: and stating a rule mentions stun without applying one. Requiring them to
#: carry a duration would ask the definition of stun to fix a number that every
#: source sets for itself: designed skills run 0.75 to 1.5 seconds and the two
#: knockdown Ultimates run 2 and 3.
#:
#: THE STUN ROW WOULD OTHERWISE HAVE PASSED FOR THE WRONG REASON. Its text
#: contains "cannot be stunned again for 5 seconds", which is the immunity
#: window rather than a duration, and the pattern above would have read it as
#: one. Naming the row here makes the exemption deliberate instead of accidental.
#:
#: THIS IS NOT A HOLE. `test_a_definition_row_says_where_its_duration_comes_from`
#: below requires each of these to say so in words, so an exempted row cannot
#: simply say nothing.
DEFINITIONS = {
    ("StatusEffects", "Debuff_Stun"),
    ("StatusEffects", "Debuff_Knockdown"),
}

#: What a definition row has to say in place of a duration.
FROM_THE_SOURCE = re.compile(
    r"stated by whatever applies it|a stated number of seconds",
    re.IGNORECASE)


def rows_of(name: str) -> list[dict[str, str]]:
    path = DATA / f"{name}.csv"
    if not path.is_file():
        pytest.skip(f"{path} is not present")
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def every_generated_row() -> list[tuple[str, str, str]]:
    """(table, row key, all of the row's text) for every generated CSV."""
    out = []
    for path in sorted(DATA.glob("*.csv")):
        with path.open(encoding="utf-8-sig", newline="") as handle:
            for row in csv.DictReader(handle):
                values = [str(v) for v in row.values() if v]
                if not values:
                    continue
                key = row.get("Name") or values[0]
                out.append((path.stem, key, " ".join(values)))
    return out


def mentions_stun(text: str) -> bool:
    return re.search(r"\bstun", text, re.IGNORECASE) is not None


def only_grants_immunity(text: str) -> bool:
    return any(re.search(p, text, re.IGNORECASE) for p in IMMUNITY)


def stun_rows() -> list[tuple[str, str, str]]:
    """Rows that apply a stun, as opposed to mentioning one to deny it."""
    return [r for r in every_generated_row()
            if mentions_stun(r[2]) and not only_grants_immunity(r[2])]


# --------------------------------------------------------------------------
# The rule
# --------------------------------------------------------------------------

def test_the_data_still_mentions_stun_somewhere():
    """Guards the tests below against passing vacuously. If the stun mechanic is
    ever removed from the data this fails loudly rather than every other check
    here quietly succeeding over an empty list."""
    assert stun_rows(), (
        "no row in any generated table under game/Data/ applies a stun. Either "
        "the stun mechanic was removed, in which case this whole file and the "
        "anti-stun-lock section of the design document should go with it, or "
        "the wording changed enough that nothing matches any more.")


def test_everything_that_applies_a_stun_states_how_long():
    """The whole of issue #271, as a rule rather than as one fixed row.

    DEFINITION ROWS ARE EXEMPT, and only the two named in DEFINITIONS. See the
    comment there for why, and for the test that stops the exemption being a
    hole."""
    missing = [(table, key) for table, key, text in stun_rows()
               if (table, key) not in DEFINITIONS and not DURATION.search(text)]
    assert not missing, (
        f"{missing} apply a stun without saying how long it lasts. Since the "
        "anti-stun-lock rule gave stun a 5 second immunity window, a duration "
        "is a number that interacts with another number and 'briefly' is not "
        "enough. Add the duration to docs/All_Things_Cataclysm.xlsx and "
        "regenerate. Issue #271.")


@pytest.mark.parametrize(("table", "key"), sorted(DEFINITIONS))
def test_a_definition_row_says_where_its_duration_comes_from(table, key):
    """What stops DEFINITIONS being a way to say nothing.

    A row exempted from stating a duration has to state that the duration is set
    by whatever applies the effect. Otherwise "Stun: a hard stop" would satisfy
    the exemption and a reader would be no better off than before issue #363,
    which is the state that made the Brute's stomp express its stun as a
    standalone rider instead of as this effect.
    """
    row = next((r for r in rows_of(table) if r["Name"] == key), None)
    assert row is not None, (
        f"game/Data/{table}.csv has no row {key}. It is exempted from stating a "
        f"stun duration on the grounds that it defines the effect, so if it is "
        f"gone the exemption in DEFINITIONS should go with it. Issue #363.")
    assert FROM_THE_SOURCE.search(row["Description"]), (
        f"{key} is exempt from stating a stun duration because it defines the "
        f"effect rather than applying one, and it does not say where the "
        f"duration comes from either. It has to say that the duration is set by "
        f"whatever applies it. Issue #363.")


def test_the_definition_rows_carry_the_three_anti_stun_lock_rules():
    """The reason for putting these effects in the data at all. `Effect=Stun`
    is only useful if a reader arriving at the row learns what a stun does, and
    the three rules are what a stun does beyond stopping the target."""
    rows = {r["Name"]: r["Description"] for r in rows_of("StatusEffects")}
    stun = rows.get("Debuff_Stun")
    assert stun, (
        "game/Data/StatusEffects.csv has no Debuff_Stun row. Issue #363 added "
        "it so that Effect=Stun is writable and the rules are findable.")
    for rule in ("10% of the target maximum health",
                 "cannot be stunned again for 5 seconds",
                 "boss cannot be stunned at all"):
        assert rule in stun, (
            f"the Stun status effect row does not state {rule!r}. All three "
            f"anti-stun-lock rules belong on it, because it is the row anything "
            f"applying a stun points at. Issues #216 and #363.")


def test_stun_and_knockdown_say_they_share_one_immunity_window():
    """The rule most easily lost, and the design document argues for it
    explicitly: two 3-second holds taken in turn is exactly the failure the
    window exists to stop, so one window rather than one each."""
    rows = {r["Name"]: r["Description"] for r in rows_of("StatusEffects")}
    for key in ("Debuff_Stun", "Debuff_Knockdown"):
        assert "share one" in rows.get(key, "") and "window" in rows.get(key, ""), (
            f"{key} does not say that Stun and Knockdown share one immunity "
            f"window rather than one each. Both rows have to, because a reader "
            f"arriving at either must not conclude they have separate windows. "
            f"Issue #363.")


@pytest.mark.parametrize(("key", "seconds"), sorted(SKILL_STUN_SECONDS.items()))
def test_each_stunning_skill_states_its_designed_duration(key, seconds):
    row = next((r for r in rows_of("WeaponSkills") if r["Name"] == key), None)
    assert row is not None, f"game/Data/WeaponSkills.csv has no row {key}"
    found = DURATION.search(row["SkillDescription"])
    assert found, (
        f"{key} ({row['SkillName']}) no longer states a stun duration. It "
        f"should stun for {seconds} seconds. Issue #271.")
    assert float(found.group(1)) == seconds, (
        f"{key} ({row['SkillName']}) stuns for {found.group(1)} seconds, not "
        f"the designed {seconds}. If that is intended, change it in "
        f"docs/All_Things_Cataclysm.xlsx and update this table.")


def test_the_stunning_weapon_skills_are_exactly_the_four_that_are_designed():
    """A fifth stunning skill is fine, but it has to be added here and to the
    design document sentence that lists them by name, or both go stale."""
    found = {key for table, key, _ in stun_rows() if table == "WeaponSkills"}
    assert found == set(SKILL_STUN_SECONDS), (
        f"the set of weapon skills that stun has changed to {sorted(found)}. "
        f"Add or remove it in SKILL_STUN_SECONDS above, and update the sentence "
        f"in docs/Cataclysm_GDD_v2.md that names them.")


def test_the_design_document_names_the_same_stunning_skills():
    """The anti-stun-lock section says which skills ignore the damage threshold,
    and it names them one by one. If a skill is added to the data and not to
    that sentence, the rule silently stops covering it."""
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    document = " ".join(GDD.read_text(encoding="utf-8").split())
    sentence = next((s for s in document.split(". ")
                     if "whose stated effect is to stun" in s), None)
    assert sentence, (
        "docs/Cataclysm_GDD_v2.md no longer says that a skill whose stated "
        "effect is to stun ignores the damage threshold. Issue #216.")
    by_key = {r["Name"]: r["SkillName"] for r in rows_of("WeaponSkills")}
    for key in SKILL_STUN_SECONDS:
        assert by_key[key] in document, (
            f"the design document does not name {by_key[key]}, which stuns. "
            f"The anti-stun-lock section lists the stunning skills by name.")


# --------------------------------------------------------------------------
# The tie to the simulation
# --------------------------------------------------------------------------

def test_the_blunt_stun_constant_still_matches_the_shortest_designed_stun():
    """`BLUNT_STUN_SECONDS` in sim/cataclysm_sim/damage.py is the stun a Blunt
    weapon applies on an ordinary hit. Its comment says it is deliberately the
    shortest duration any designed skill uses, so that a weapon sub-type
    stunning on every hit cannot outclass the skills built to stun. That claim
    was written when Whip Swing had no number at all. Now it does, and this is
    what keeps the two in step."""
    from cataclysm_sim import damage

    shortest = min(SKILL_STUN_SECONDS.values())
    assert damage.BLUNT_STUN_SECONDS == shortest, (
        f"BLUNT_STUN_SECONDS is {damage.BLUNT_STUN_SECONDS} but the shortest "
        f"designed skill stun is now {shortest}. Its comment claims the two "
        f"match. Either change the constant, or change the comment and say why "
        f"a Blunt weapon's incidental stun differs from the shortest skill.")


# --------------------------------------------------------------------------
# What the rule does not cover
# --------------------------------------------------------------------------

def test_a_skill_that_only_grants_immunity_needs_no_duration():
    """Two Ultimate skills say "you cannot be stunned, slowed or knocked back".
    They mention stun and state no duration, which is correct, and this records
    that the exclusion is deliberate rather than an oversight in the rule."""
    immune = [key for _, key, text in every_generated_row()
              if mentions_stun(text) and only_grants_immunity(text)]
    assert immune, (
        "no row grants immunity to stun any more. If that is intended, this "
        "test and the IMMUNITY patterns above can go; until then its absence "
        "means the wording changed and the classifier no longer recognises it, "
        "which would make those rows fail the duration rule for no reason.")
