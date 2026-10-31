"""A wand or staff skill that has been designed carries `Type.Spell`, unless it is named here as not a spell.

WHY THIS EXISTS. Issue #2012. `UCataclysmSkillEffects::ApplyHit` adds spell damage to a blow only when the skill
carries the `Type.Spell` gameplay tag, and the same tag is what marks a landed blow as a spell for every conditional
that asks. Sixty-eight authored rows grant `spell_damage` (affixes, enchantments and passive nodes), so a caster skill
designed without the tag receives none of it, and nothing said so. Measured on `development` `4aa87ab3e`, 2026-10-30:
`game/Data/WeaponSkills.csv` has 70 wand and staff rows; 10 are designed, all Demonic; 9 carry the tag, and the tenth
is "Summon Imp", which the project owner ruled on 2026-09-23 is not a spell. The other 60 are undesigned placeholders,
and the other six Cataclysms' casters will be designed from them. This makes that moment a decision: a newly designed
wand or staff skill fails here until it is tagged, or until a ruling names it below.

WHAT "DESIGNED" MEANS: a row with a `SkillName`, which is the game's own test. `UCataclysmWeaponSkills` skips a row
whose `SkillName` is empty, and an unfilled slot gets `UCataclysmUndesignedSkill`, which does nothing.
`test_designed_means_what_the_game_reads` holds the two to the same meaning.

SCOPE, as the issue asks: wand and staff rows only. A spell on another weapon is not covered, because nothing states
which melee skills are spells.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
LOADER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmWeaponSkills.cpp"

CASTER_WEAPONS = ("Wand", "Staff")
SPELL_TAG = "Type.Spell"

#: Designed wand and staff rows that are not spells: row name -> the ruling. Each needs one; a row is not added here
#: to make this test pass.
NOT_SPELLS = {
    "Demonic_Staff_Special": "Summon Imp is a summon, not a spell: the project owner, 2026-09-23 (#2012). It "
                             "receives no spell damage and stays usable inside an anti-magic zone.",
}


def rows() -> list[dict[str, str]]:
    with WEAPON_SKILLS.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def tags_of(row: dict[str, str]) -> set[str]:
    return {tag.strip() for tag in row["Tags"].split(",") if tag.strip()}


def designed_casters() -> list[dict[str, str]]:
    return [row for row in rows()
            if row["WeaponType"] in CASTER_WEAPONS and row["SkillName"].strip()]


def code_only(text: str) -> str:
    """C++ with its comments taken out, so a comment that names the check is not read as the check."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def test_every_designed_wand_or_staff_skill_is_a_spell_or_a_named_exception():
    untagged = [f"{row['Name']} ({row['SkillName']})" for row in designed_casters()
                if SPELL_TAG not in tags_of(row) and row["Name"] not in NOT_SPELLS]
    assert not untagged, (
        f"designed wand or staff skills without {SPELL_TAG}: {untagged}. Without the tag no spell damage reaches "
        "them, from any affix, enchantment or passive node, and no 'was this a spell' condition sees them. Tag the "
        "row in the design workbook and regenerate, or, if the skill is not a spell, get that ruled and add it to "
        "NOT_SPELLS with the ruling. Issue #2012.")


def test_every_named_exception_is_still_a_designed_untagged_wand_or_staff_row():
    by_name = {row["Name"]: row for row in rows()}
    stale = []
    for name in NOT_SPELLS:
        row = by_name.get(name)
        if row is None:
            stale.append(f"{name}: no such row")
        elif row["WeaponType"] not in CASTER_WEAPONS:
            stale.append(f"{name}: not a wand or staff row")
        elif not row["SkillName"].strip():
            stale.append(f"{name}: not designed")
        elif SPELL_TAG in tags_of(row):
            stale.append(f"{name}: now carries {SPELL_TAG}")
    assert not stale, (
        "a NOT_SPELLS entry excuses nothing any more, so delete it: " + "; ".join(stale))


def test_designed_means_what_the_game_reads():
    """The game skips a row with an empty `SkillName` when it hands out skills, and a check that meant anything else
    by "designed" would guard the wrong rows. Also the positive control: the rows it reads are there."""
    code = code_only(LOADER.read_text(encoding="utf-8"))
    body = code.split("UCataclysmWeaponSkills::SkillsFor(", 1)
    assert len(body) == 2, "UCataclysmWeaponSkills::SkillsFor is not in CataclysmWeaponSkills.cpp any more"
    assert re.search(r"if\s*\(\s*Row\.SkillName\.IsEmpty\(\)\s*\)\s*\{\s*return\s*;", body[1]), (
        "SkillsFor no longer skips a row whose SkillName is empty, so a row this test calls undesigned may be "
        "handed to a player. Update what 'designed' means here to match.")

    designed = designed_casters()
    assert len(designed) >= 10, f"only {len(designed)} designed wand or staff rows were read"
    assert sum(SPELL_TAG in tags_of(row) for row in designed) >= 9, "fewer than nine carry the spell tag"
