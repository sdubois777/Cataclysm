"""Every named row of the Weapon Skills sheet carries exactly one `Element.*` tag,
the one its `DamageType` column names, and no row carries two.

WHY THIS EXISTS. Issue #2005. `UCataclysmSkillTemplate::ElementTag` returns the
FIRST `Element.*` tag in a skill's tags and ignores the rest, and its comment
said a test named `Cataclysm.Data.EverySkillRowCarriesOneElementTag` held the
sheet to one. No test of that name existed anywhere in the project, so nothing
held it.

WHERE A SKILL'S TAGS COME FROM, read 2026-09-23 on `development` at 38089da3.
`UCataclysmWeaponSkills` builds a skill's tags from the row's `Tags` column
alone (`UCataclysmSkillShapes::TagsFromCell(Row.Tags)`), and
`UCataclysmWeaponSlotsComponent` stamps them on the granted template unchanged.
Nothing adds a tag from the `DamageType` column. So the `Tags` column is the
whole of what `ElementTag` reads, and it is what this file checks.

WHAT THE SHEET HOLDS, measured the same day over all 403 rows: 142 carry one
element tag and 261 carry none. **The 261 are the rows with no skill name and no
shape** -- placeholder cells of the weapon-against-damage-type matrix, granted as
the undesigned placeholder ability, which reads no element. Every row with a
skill name carries exactly one, and it names the row's own damage type.

SO TWO RULES, AND NEITHER IS "EXACTLY ONE ON EVERY ROW", which the old comment
said and the sheet does not hold:

- no row carries two or more, because the second would be ignored in silence;
- a row with a skill name carries exactly one, and it is `Element.<DamageType>`,
  because a named skill with none has no element to be read, and one naming
  another damage type would scale with the wrong type's stats.

THE RULES ARE PROVED TO FIRE on hand-made rows below, as well as run on the
shipped sheet: a check that reads a sheet which already passes cannot show
that it would notice a sheet that did not.
"""

from __future__ import annotations

import csv
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS_CSV = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

ELEMENT_TAG_PREFIX = "Element."


def element_tags(row: dict[str, str]) -> list[str]:
    """The `Element.*` tags in a row's `Tags` cell, in the order written."""
    return [tag.strip() for tag in row.get("Tags", "").split(",")
            if tag.strip().startswith(ELEMENT_TAG_PREFIX)]


def element_problems(rows: list[dict[str, str]]) -> list[str]:
    """One line per row breaking either rule, naming the row and what it holds."""
    problems = []
    for row in rows:
        tags = element_tags(row)
        name = row.get("Name", "?")
        if len(tags) > 1:
            problems.append(
                f"{name}: {len(tags)} element tags {tags}; the engine reads only "
                f"the first")
        elif row.get("SkillName", "").strip():
            wanted = f"{ELEMENT_TAG_PREFIX}{row.get('DamageType', '').strip()}"
            if tags != [wanted]:
                problems.append(
                    f"{name} ({row['SkillName']}): element tags {tags}, "
                    f"expected [{wanted!r}] from its DamageType column")
    return problems


def shipped_rows() -> list[dict[str, str]]:
    with WEAPON_SKILLS_CSV.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def test_the_sheet_has_named_rows_to_check():
    """The control. A reader that found no named rows would pass the check
    below by checking nothing; 142 named rows were measured on 2026-09-23."""
    named = [row for row in shipped_rows() if row["SkillName"].strip()]
    assert len(named) >= 100, (
        f"only {len(named)} named rows were read from {WEAPON_SKILLS_CSV}")


def test_every_named_weapon_skill_row_carries_its_own_element_and_no_row_two():
    """The check itself, on the shipped sheet.

    WHAT TO DO WHEN THIS FAILS: correct the row's `Tags` cell in the Weapon Skills
    sheet of docs/All_Things_Cataclysm.xlsx and regenerate. Do not teach
    `ElementTag` to choose among several; the design gives a skill one type.
    """
    problems = element_problems(shipped_rows())
    assert not problems, (
        f"{len(problems)} row(s) of {WEAPON_SKILLS_CSV.name} break the element "
        f"tag rules:\n" + "\n".join(f"  {line}" for line in problems))


def test_the_rules_fire_on_rows_that_break_them():
    """Each rule, on a row made to break it, and a row that keeps both."""
    good = {"Name": "Sword_Demonic_Heavy", "SkillName": "Cleave",
            "DamageType": "Demonic", "Tags": "Element.Demonic, Type.Melee"}
    placeholder = {"Name": "Sword_Void_Heavy", "SkillName": "",
                   "DamageType": "Void", "Tags": ""}
    two = dict(good, Name="two", Tags="Element.Demonic, Element.War")
    none = dict(good, Name="none", Tags="Type.Melee")
    wrong = dict(good, Name="wrong", Tags="Element.War")
    unnamed_two = dict(placeholder, Name="unnamed_two",
                       Tags="Element.Void, Element.War")

    assert element_problems([good, placeholder]) == []
    for row in (two, none, wrong, unnamed_two):
        problems = element_problems([row])
        assert len(problems) == 1 and problems[0].startswith(row["Name"]), (
            row["Name"], problems)
