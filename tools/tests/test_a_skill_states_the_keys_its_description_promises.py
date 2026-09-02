"""A skill whose description names a mechanic states the key that carries it.

WHY THIS EXISTS. Issue #1162. Five Demonic rows promised something in their own
sentence and carried no parameter for it, and every one of them was found by
reading descriptions by hand rather than by any test:

  the Spear's Held Fast       "any pinned enemy within 12 meters is set alight
                              again each second it is held", with no `Burn`
  the Fist's Cinder Rush      "knocking them aside", with no `Knockback`
  the Greatsword's Inexorable "throwing aside", with no `Knockback`
  the Fist's Blood Pyre       "doubles your health regeneration", with no key
  the Greatsword's
  The Whole Weight            "you cannot act or be healed", with no key

HELD FAST IS THE ONE THAT SHOWS WHY A C++ TEST COULD NOT HAVE CAUGHT THIS.
`Cataclysm.Skills.HeldFastRelightsPinnedEnemiesEachSecond` passed for months. It
grants the skill with `Burn=1` WRITTEN INTO THE TEST, so it proved that the
repeat lights pinned enemies given a burn key and never asked whether the sheet
supplies one. It did not. The whole sentence did nothing in play.

That is the same shape as the passive tree defect in issue #1056 and the health
return found earlier on 2026-09-02: a test that hands the code the missing piece
proves the machinery and says nothing about whether the data reaches it. A check
that reads the GENERATED TABLE cannot be fooled that way, which is why this is
here and not beside the skill tests.

WHY IN PYTHON RATHER THAN IN THE AUTOMATION SUITE, and it is the same answer
`test_a_skill_deals_the_damage_its_description_states.py` gives: continuous
integration runs `python -m pytest` and nothing else, so nothing under
`game/Source/Cataclysm/Tests/` runs on a pull request and this does. It also
needs no engine -- it compares a column of prose against a column of keys.

WHAT IT CANNOT DO. It cannot tell whether a key is IMPLEMENTED, only whether the
row states one. A parameter nothing reads passes this and is caught instead by
grepping for readers, which is the audit written up in issue #1141.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

#: A phrase that promises a mechanic, and the Shape Params key that carries it.
#:
#: EACH PATTERN IS DELIBERATELY NARROW. A pattern that matched loosely would
#: produce exemptions faster than findings, and an exemption list longer than the
#: check is a check nobody trusts. "Sets alight" is a promise; "burning" on its
#: own is a description of a weapon and is not.
PROMISES: list[tuple[str, str]] = [
    (r"sets? (?:it |them |each one |every\w+ )?alight|set alight|ignites",
     "Burn"),
    (r"knock(?:s|ing)? (?:them|it) back|knocked aside|knocking them aside|"
     r"throwing aside|thrown aside",
     "Knockback"),
    (r"leaving a pyre|trail of fire|line of fire|pool of lava|"
     r"molten slag|ground burning|burning ground",
     "GroundRadius"),
    (r"pay(?:ing)? \d+(?:\.\d+)?% of your (?:current )?health",
     "HealthCostPercent"),
    (r"returns? to your hand|comes back to your hand",
     "Returns"),
]

#: Rows that match a pattern above and are right without the key.
#:
#: EACH ONE IS HERE BECAUSE A HUMAN READ IT, and the reason is recorded so a
#: later reader does not have to read it again. Adding to this map is how a new
#: row of one of these shapes is accepted; adding to it without a reason is how
#: this check stops being worth running.
ACCEPTED: dict[tuple[str, str], str] = {
    ("Ashen Edge", "Burn"):
        "A buff that widens what the Sword's OTHER skills do when they consume "
        "burn. `ConsumeRadius=4` is the key, read by "
        "`UCataclysmSkillTemplate::IgniteAroundConsumed`. It sets nothing "
        "alight itself.",
    ("Ashen Edge", "ConsumeBurn"):
        "The same reason. It is the consuming done by other skills that this "
        "widens.",
    ("Hex of Cinders", "Burn"):
        "`SpreadWhen=Burning; SpreadRadius=4` is what sets the neighbours "
        "alight, and the hex itself lays a Shred rather than a fire.",
    ("Living Pyre", "Knockback"):
        "Its sentence is 'you cannot be KNOCKED BACK', which is an immunity it "
        "has rather than a shove it applies. `Immune=Displacement` carries it.",
    ("Emberhaul", "Returns"):
        "'The axe comes back ready to throw again' is the COOLDOWN returning, "
        "not a projectile flying home. `RefundsCooldown=Self` carries it.",
}

#: Words that promise something a pattern above cannot express, and the rows
#: that use them.
#:
#: WHY THIS HALF EXISTS AT ALL. The audit that found four of the five rows above
#: compared NUMBERS, and it missed Blood Pyre entirely because "doubles" is a
#: word. A promise written in words cannot be checked mechanically -- nothing can
#: decide whether "doubles your health regeneration" is implemented by reading a
#: table -- so this does the one useful thing that is left: it FORCES A HUMAN TO
#: LOOK at any new row that uses one, by failing until the row is listed here
#: with what carries its promise.
WORD_PROMISES = re.compile(
    r"\bdoubl\w+|\bhalv\w+|\btripl\w+|\btwice\b|\bno cap\b|\bno limit\b|"
    r"\bunlimited\b|\bcannot\b|\bnever\b|\bimmune\w*",
    re.IGNORECASE,
)

#: Every Demonic row using one of those words, and what carries its promise.
REVIEWED_WORDS: dict[str, str] = {
    "Ashen Edge":
        "'the fire you spend is never wholly lost' is prose describing "
        "`ConsumeRadius`.",
    "Unbroken":
        "'cannot be staggered' is `Immune=Displacement`.",
    "Backswing":
        "'you cannot move while holding' is refused for every held swing by "
        "`ACataclysmPlayerController::PawnCannotWalk`.",
    "The Whole Weight":
        "'you cannot move, act or be healed' is the movement gate plus "
        "`HoldForbids=Acting, Healing`.",
    "Inexorable":
        "'cannot be turned aside' is the direction being taken once, and "
        "'immune to crowd control' is `Immune=CrowdControl`.",
    "Emberhurl":
        "'hitting each enemy twice, once going out and once returning' is "
        "`Pierce=99` with `Returns=1`.",
    "Nail Down":
        "'cannot move for 3 seconds' is `ForcedMovement=Pin` with "
        "`ForcedMovementDuration=3`.",
    "Living Pyre":
        "'cannot be stunned, slowed or knocked back' is "
        "`Immune=Stun, Slow, Displacement`, and 'with no cap' is the absence "
        "of a `MaxDamagePercent`.",
    "Cinder Rush":
        "'immune to crowd control during the rush' is `Immune=CrowdControl`, "
        "and it needs the `Duration` for that window to have any length.",
    "Blood Pyre":
        "'does you no harm' is already true, because a ground zone burns only "
        "its owner's enemies; 'doubles your health regeneration' is "
        "`OwnGroundRegenPercent=200`.",
    "Whisper of Madness":
        "'lasts twice as long in a mind that is already burning' is the Debuff "
        "template's duration scale.",
    "Subjugate":
        "'bosses cannot be taken' is refused by `UCataclysmCommand`.",
    "The Gathering":
        "'cannot rise for 2 seconds' is `ForcedMovement=Knockdown` with "
        "`ForcedMovementDuration=2`.",
    "Groundbreaker":
        "'no limit to how many you may open' is taken literally, and the "
        "reason is written beside the code that does it.",
    "Crater":
        "'enemies in the pit cannot charge or leap' is `Terrain=Pit`, refused "
        "in `UCataclysmSkillTemplate::CanActivateAbility`.",
}


def demonic_rows() -> list[dict[str, str]]:
    with WEAPON_SKILLS.open(newline="", encoding="utf-8") as handle:
        return [row for row in csv.DictReader(handle)
                if row["DamageType"] == "Demonic" and row["SkillName"].strip()]


def test_the_table_has_demonic_rows_to_check() -> None:
    """A check over an empty list passes and proves nothing."""
    rows = demonic_rows()
    assert len(rows) >= 50, (
        f"only {len(rows)} named Demonic rows, which is far short of the 56 "
        "the sheet carries. Regenerate with tools/generate_datatables.py.")


@pytest.mark.parametrize("pattern,key", PROMISES,
                         ids=[key for _, key in PROMISES])
def test_a_row_promising_a_mechanic_states_its_key(pattern: str,
                                                   key: str) -> None:
    matcher = re.compile(pattern, re.IGNORECASE)

    missing = []
    for row in demonic_rows():
        if not matcher.search(row["SkillDescription"]):
            continue
        if f"{key}=" in row["ShapeParams"]:
            continue
        if (row["SkillName"], key) in ACCEPTED:
            continue
        missing.append(
            f"{row['WeaponType']} {row['Slot']} {row['SkillName']}: its "
            f"description promises {key} and its Shape Params are "
            f"{row['ShapeParams']!r}")

    assert not missing, (
        f"{len(missing)} Demonic row(s) promise {key} in their own sentence "
        "and do not state it. Either add the key in "
        "docs/All_Things_Cataclysm.xlsx and regenerate, or add the row to "
        "ACCEPTED in this file WITH THE REASON:\n  " + "\n  ".join(missing))


def test_every_word_shaped_promise_has_been_read_by_somebody() -> None:
    unreviewed = []
    for row in demonic_rows():
        found = sorted({word.lower()
                        for word in WORD_PROMISES.findall(
                            row["SkillDescription"])})
        if found and row["SkillName"] not in REVIEWED_WORDS:
            unreviewed.append(
                f"{row['WeaponType']} {row['Slot']} {row['SkillName']}: says "
                f"{found} and nobody has recorded what carries it")

    assert not unreviewed, (
        f"{len(unreviewed)} Demonic row(s) promise something in WORDS rather "
        "than in a number, and no reader has said what implements it. Nothing "
        "can decide that mechanically, so read the row and add it to "
        "REVIEWED_WORDS in this file with what carries its promise:\n  "
        + "\n  ".join(unreviewed))


def test_the_reviewed_list_names_only_rows_that_still_exist() -> None:
    """A stale entry is worse than none: it silently excuses a row.

    A skill renamed in the workbook would leave its entry here matching
    nothing, and the row under its new name would then pass unreviewed.
    """
    names = {row["SkillName"] for row in demonic_rows()}
    gone = sorted(set(REVIEWED_WORDS) - names)
    assert not gone, (
        "REVIEWED_WORDS names skills the Demonic rows no longer carry, so "
        f"those entries excuse nothing and hide the renamed row: {gone}")


def test_the_accepted_list_names_only_rows_that_still_exist() -> None:
    names = {row["SkillName"] for row in demonic_rows()}
    gone = sorted({name for name, _ in ACCEPTED} - names)
    assert not gone, (
        "ACCEPTED names skills the Demonic rows no longer carry, so those "
        f"entries excuse nothing: {gone}")
