"""A skill whose description states a percentage of weapon damage deals it.

WHY THIS EXISTS. Issue #1142. Ten rows of `game/Data/WeaponSkills.csv` said one
number in their own sentence and dealt another, because the `DamagePercent`
column was empty on every row of the sheet and `GetDamagePercent` fell back to
the slot's figure. The worst was the Axe's Butcher's Bill: "thirty burning axes
... each one dealing 14% weapon damage", where thirty axes at the Ultimate slot's
400% is 12,000% of weapon damage against a row asking for 420%.

NOTHING CAUGHT IT FOR AS LONG AS IT EXISTED, and nothing could have: the fallback
is correct behaviour, the slot's figure is a real number, and no test compared
either against the prose. It was found by reading the descriptions one at a time.

WHAT THIS CHECKS. Every row whose description contains "N% weapon damage" states a
Damage Percent, and that figure is N. Four rows match the same phrase and are not
faults, listed below by name with the reason, so a later reader does not have to
work them out again.

WHY IN PYTHON RATHER THAN IN THE AUTOMATION SUITE. Continuous integration runs
`python -m pytest` and nothing else, so nothing under
`game/Source/Cataclysm/Tests/` runs on a pull request and this does. It is also
the right shape for the check: it compares two columns of a generated table and
needs no engine at all.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
SKILL_SLOTS = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"

#: What a row means by stating no figure of its own.
#:
#: `UCataclysmSkillTemplate::GetDamagePercent` answers the skill's own figure and
#: falls back to the slot's when the skill states none. Issue #836 records why
#: that order is right, and most rows take the fallback. AN AURA TAKES NOUGHT
#: INSTEAD, ruled 2026-11-24 (#2875); that rule has its own file,
#: `test_an_aura_deals_damage_only_where_its_row_states_it.py`.
#:
#: THIS SAID "393 OF THE 403 ROWS" AND NEITHER NUMBER WAS RIGHT. Issue #1243.
#: Measured 2026-09-12: `game/Data/WeaponSkills.csv` holds 403 rows, 12 state a
#: damage percentage and 391 sit at this sentinel. Writing the designed skills'
#: numbers is the rest of issue #836, so both figures move whenever one is
#: filled in, and no replacement count is written here.
#: `test_both_kinds_of_row_exist` below reads the file instead of restating it.
NO_FIGURE_STATED = -1.0

#: A percentage of weapon damage, as the descriptions write it.
#:
#: THE UNIT IS PART OF THE PATTERN. "350% weapon damage" is a figure this checks
#: and "30% attack speed" is not, so matching a bare percentage would drag in
#: every attack speed, resistance and chance in the sheet.
STATES_A_PERCENT = re.compile(r"(\d+(?:\.\d+)?)%\s+weapon damage", re.IGNORECASE)

#: Rows whose stated percentage is a real number that is not the blow's own.
#:
#: EACH ONE IS HERE BECAUSE A HUMAN READ IT, and the reason is recorded so that a
#: later reader does not have to read it again. Issue #1142 lists the same four.
#: Adding to this set is how a new row of this shape is accepted; doing it without
#: a reason in the comment is how this check stops being worth running.
NOT_THE_BLOW = {
    # Pyroclasm spins for a duration and then lands a closing hit. The 300% is
    # that closing hit, carried by `FinalHitPercent=300` in its Shape Params.
    "Pyroclasm",
    # Martyr's Ember stores damage taken and returns it. The 200% is the cap on
    # the stored amount, not what any blow deals.
    "Martyr's Ember",
    # Braced Guard is the same shape: 200% caps the blocked damage it stores.
    "Braced Guard",
    # Haymaker's 100% is an extra dealt when the target is thrown into a wall,
    # on top of the blow, rather than the blow itself.
    "Haymaker",
}


#: A range of weapon damage, as a row that rolls its damage writes it: "50% to
#: 450% weapon damage". Issue #2580, [N3].
STATES_A_RANGE = re.compile(
    r"(\d+(?:\.\d+)?)%\s+to\s+(\d+(?:\.\d+)?)%\s+weapon damage", re.IGNORECASE)


def roll_of(row: dict[str, str]) -> tuple[float, float] | None:
    """The two ends a row's damage is drawn between, or None for a row that
    states no roll. Read out of its Shape Params, `DamageRollMin` and
    `DamageRollMax`, which `tools/generate_datatables.py` holds to both or
    neither."""
    found = {}
    for piece in (row.get("ShapeParams") or "").split(";"):
        key, _, value = piece.partition("=")
        if key.strip() in ("DamageRollMin", "DamageRollMax"):
            found[key.strip()] = float(value.strip())
    if "DamageRollMax" not in found:
        return None
    return found.get("DamageRollMin", 0.0), found["DamageRollMax"]


def rolled_row_problems(skill_rows: list[dict[str, str]]) -> list[str]:
    """What is wrong with each row that rolls its damage. Issue #2580, [N3].

    THE JUDGEMENT OF 2026-11-17, recorded in docs/DECISIONS.md: a rolled row
    states no Damage Percent of its own, because the roll IS its damage percent;
    if its description gives the range as "N% to M% weapon damage", the two
    figures are the roll's two ends; and it states no single "N% weapon damage"
    outside such a range, since it deals no one figure.
    """
    problems = []
    for row in skill_rows:
        roll = roll_of(row)
        if roll is None:
            continue
        name = row["SkillName"]
        if float(row["DamagePercent"]) != NO_FIGURE_STATED:
            problems.append(f"{name}: rolls {roll[0]}-{roll[1]} and also states "
                            f"a Damage Percent of {row['DamagePercent']}")
        prose = row["SkillDescription"] or ""
        for low, high in STATES_A_RANGE.findall(prose):
            if (abs(float(low) - roll[0]) > 0.01
                    or abs(float(high) - roll[1]) > 0.01):
                problems.append(f"{name}: says {low}% to {high}% and rolls "
                                f"{roll[0]} to {roll[1]}")
        single = STATES_A_PERCENT.findall(STATES_A_RANGE.sub("", prose))
        if single:
            problems.append(f"{name}: rolls its damage and says "
                            f"{', '.join(single)}% weapon damage as one figure")
    return problems


def rows() -> list[dict[str, str]]:
    text = WEAPON_SKILLS.read_bytes().decode("utf-8-sig")
    return list(csv.DictReader(text.splitlines()))


@pytest.fixture(scope="module")
def skill_rows() -> list[dict[str, str]]:
    return rows()


def test_the_weapon_skills_table_is_readable(skill_rows) -> None:
    """The other tests below say nothing if the table failed to load."""
    assert len(skill_rows) > 300, (
        f"{WEAPON_SKILLS} holds {len(skill_rows)} rows, which is far fewer than "
        "the 403 the sheet has. Something is wrong with the file rather than "
        "with any skill.")
    assert "DamagePercent" in skill_rows[0], (
        "WeaponSkills.csv has no DamagePercent column, so this check cannot "
        "compare anything. Run tools/generate_datatables.py.")


def test_both_kinds_of_row_exist(skill_rows) -> None:
    """Some rows state a damage percentage of their own and some take the
    slot's. Both have to exist or half the tests in this file compare nothing.

    WHY THIS REPLACED A COUNT IN A COMMENT. Issue #1243. Three comments in
    `tools/generate_datatables.py` and one here stated how many rows were blank,
    and every one of them had gone stale, because writing the designed skills'
    numbers is ongoing work under issue #836 and each one filled in moves both
    figures. This asserts the shape instead of the count, so it stays true while
    the work continues and still fails if either kind disappears.

    NO UPPER OR LOWER BOUND ON THE SPLIT beyond one of each, deliberately. A
    bound would be another number going stale for the same reason.
    """
    stated = [row for row in skill_rows
              if float(row["DamagePercent"]) != NO_FIGURE_STATED]
    fallback = [row for row in skill_rows
                if float(row["DamagePercent"]) == NO_FIGURE_STATED]

    assert stated, (
        "no row in WeaponSkills.csv states a damage percentage of its own, so "
        "`test_every_row_stating_a_percentage_of_weapon_damage_deals_it` below "
        "compares nothing at all and passes whatever the sheet says.")
    assert fallback, (
        "every row in WeaponSkills.csv now states its own damage percentage, "
        "so nothing takes the slot's figure any more. That may be the end of "
        "issue #836 rather than a fault, but the fallback in "
        "`UCataclysmSkillTemplate::GetDamagePercent` is then unreached and the "
        "comments describing it need rewriting.")
    assert len(stated) + len(fallback) == len(skill_rows), (
        "some row's DamagePercent is neither a stated figure nor the "
        "no-figure marker")


def test_every_row_stating_a_percentage_of_weapon_damage_deals_it(
        skill_rows) -> None:
    """The check issue #1142 was opened for."""
    wrong = []
    for row in skill_rows:
        name = row["SkillName"]
        if name in NOT_THE_BLOW:
            continue

        # A ROW THAT ROLLS ITS DAMAGE HAS NO ONE FIGURE, and is held to its
        # range by `rolled_row_problems` below instead. Issue #2580, [N3].
        if roll_of(row) is not None:
            continue

        found = STATES_A_PERCENT.search(row["SkillDescription"] or "")
        if not found:
            continue

        stated = float(found.group(1))
        actual = float(row["DamagePercent"])
        if actual == NO_FIGURE_STATED or abs(actual - stated) > 0.01:
            deals = ("nothing of its own, so it takes its slot's figure"
                     if actual == NO_FIGURE_STATED else f"{actual}")
            wrong.append(f"{name} ({row['WeaponType']} {row['DamageType']} "
                         f"{row['Slot']}): says {stated}%, states {deals}")

    assert not wrong, (
        "these rows state a percentage of weapon damage in their description "
        "that they will not deal. Fill the Damage Percent column of the Weapon "
        "Skills sheet in docs/All_Things_Cataclysm.xlsx, or add the row to "
        "NOT_THE_BLOW above with a reason if its percentage is not the blow's. "
        "Issue #1142. -- " + "; ".join(sorted(wrong)))


def test_the_exemptions_all_name_a_row_that_exists(skill_rows) -> None:
    """An exemption for a skill that no longer exists hides the next fault.

    A row renamed or removed leaves its name in NOT_THE_BLOW, where it silently
    exempts nothing and reads as though it still does. Worse, a future row given
    the same name inherits an exemption nobody chose for it.
    """
    names = {row["SkillName"] for row in skill_rows}
    missing = sorted(NOT_THE_BLOW - names)
    assert not missing, (
        "NOT_THE_BLOW exempts skills that are not in the Weapon Skills sheet, "
        "so those entries exempt nothing: " + ", ".join(missing))


def test_the_exemptions_are_all_still_needed(skill_rows) -> None:
    """An exemption that no longer exempts anything should go.

    If a row's description stops stating a percentage of weapon damage, its
    entry above is dead weight that a reader has to reason about. This is the
    other half of the check directly above: one catches a name that no longer
    exists, this one catches a name that no longer matches.
    """
    unneeded = []
    for row in skill_rows:
        if row["SkillName"] not in NOT_THE_BLOW:
            continue
        if not STATES_A_PERCENT.search(row["SkillDescription"] or ""):
            unneeded.append(row["SkillName"])

    assert not unneeded, (
        "these rows are exempted in NOT_THE_BLOW and no longer state a "
        "percentage of weapon damage, so the exemption does nothing and should "
        "be removed: " + ", ".join(sorted(unneeded)))


def test_a_stated_figure_is_never_negative_except_the_no_figure_marker(
        skill_rows) -> None:
    """-1 means "take the slot's figure" and any other negative is a mistake.

    Worth checking because the marker is a magic number rather than an empty
    cell: a row given -2 or -350 by a slip would read as "states nothing" to the
    engine and as "states something" to a person reading the sheet.
    """
    odd = [f"{row['SkillName']}: {row['DamagePercent']}"
           for row in skill_rows
           if float(row["DamagePercent"]) < 0.0
           and float(row["DamagePercent"]) != NO_FIGURE_STATED]

    assert not odd, (
        "these rows carry a negative Damage Percent that is not the -1 marker "
        "meaning 'take the slot's figure': " + ", ".join(sorted(odd)))


def test_every_row_that_rolls_its_damage_states_its_range(skill_rows) -> None:
    """Issue #2580, [N3]. No row of the table rolls its damage on 2026-11-17,
    so this compares nothing until the Chaos Whip's rows are authored; the
    test below shows the check it runs can fail."""
    problems = rolled_row_problems(skill_rows)
    assert not problems, (
        "these rows roll their damage and their prose or their Damage Percent "
        "says otherwise: " + "; ".join(problems))


def _rolled(description: str, params: str = "DamageRollMin=50; "
            "DamageRollMax=450", damage_percent: str = "-1.0"
            ) -> dict[str, str]:
    return {"SkillName": "Wild Lash", "WeaponType": "Whip",
            "DamageType": "Chaos", "Slot": "Heavy",
            "SkillDescription": description, "ShapeParams":
            f"Radius=5; Angle=45; {params}".strip("; "),
            "DamagePercent": damage_percent}


def test_the_rolled_row_check_refuses_each_wrong_row_and_passes_the_right_one(
        ) -> None:
    """What the check above would say if a rolled row were wrong, one fault at
    a time. THE CONTROL is the first: a row stating its range correctly, and a
    row stating no roll at all, are both passed."""
    right = _rolled("Crack the whip for 50% to 450% weapon damage.")
    unrolled = _rolled("A plain lash for 250% weapon damage.", params="",
                       damage_percent="250.0")
    assert rolled_row_problems([right, unrolled]) == []
    assert roll_of(unrolled) is None
    assert roll_of(right) == (50.0, 450.0)

    for wrong, said in (
            (_rolled("Crack the whip for 50% to 400% weapon damage."),
             "says 50% to 400%"),
            (_rolled("Crack the whip for 250% weapon damage."),
             "as one figure"),
            (_rolled("Crack the whip for 50% to 450% weapon damage.",
                     damage_percent="250.0"),
             "also states a Damage Percent")):
        problems = rolled_row_problems([wrong])
        assert len(problems) == 1, (wrong, problems)
        assert said in problems[0], (wrong, problems)
