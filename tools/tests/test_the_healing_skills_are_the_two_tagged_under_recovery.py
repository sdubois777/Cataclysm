"""The healing skills are Living Pyre and Blood Pyre, and the two places the
game pays a healing skill's bonus are those two skills' own.

WHY THIS EXISTS. The owner decided on 2026-10-09 that "the healing skills are
Living Pyre and Blood Pyre." The ruling built on it the same day says a healing
skill is a skill tagged under `Stat.Recovery`, and that no new tag is added to
say so. Two stats were built on that ruling, `healing_skill_health_restored`
and `healing_skill_health_as_energy_shield`. The game does not look for the tag
when it reads them. It reads them at two places only: where an aura returns
health from a blow its holder took, which is the parameter `HealthFromHitTaken`,
and where the regeneration step pays the extra regeneration of a character's
own ground, which is the parameter `OwnGroundRegenPercent`.

SO THREE THINGS HAVE TO STAY THE SAME SET, and nothing in the game compares
them: the skills tagged under `Stat.Recovery`, the skills stating one of those
two parameters, and the two skills the owner named. A third skill given a
recovery tag would be a healing skill by the ruling and get nothing from either
stat. A third skill given one of the parameters would get both stats without
being a healing skill. This file fails in either case.

WHAT IT READS. `game/Data/WeaponSkills.csv` for the tags and the parameters,
and the C++ source as text for where the two reading functions are called.

WHAT IT DOES NOT CHECK. That either function does the right arithmetic; the
automation tests under `Cataclysm.HealingSkills.` do that, and continuous
integration does not run them. Whether a skill granted some other way than a
row of `WeaponSkills.csv` carries a recovery tag.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
SOURCE = REPO_ROOT / "game" / "Source"
ABILITY_SYSTEM = SOURCE / "Cataclysm" / "AbilitySystem"
SKILL_TEMPLATES_CPP = ABILITY_SYSTEM / "CataclysmSkillTemplates.cpp"
REGENERATION_CPP = ABILITY_SYSTEM / "CataclysmRegeneration.cpp"

#: The tag a healing skill is tagged under, by the ruling of 2026-10-09.
RECOVERY_ROOT = "Stat.Recovery"

#: The two skills the owner named, by row name and by the name a player sees.
HEALING_SKILLS = {
    "Demonic_Fist_Ultimate": "Living Pyre",
    "Demonic_Fist_Special": "Blood Pyre",
}

#: The parameter each of the two places reads, and the skill whose row states it.
PARAMETER_OF = {
    "HealthFromHitTaken": "Demonic_Fist_Ultimate",
    "OwnGroundRegenPercent": "Demonic_Fist_Special",
}

#: The two functions that read the two stats.
READERS = ("HealingSkillAmount", "GiveHealingSkillShield")

#: The function each place is, as its definition opens, and what in its body
#: shows it is the place that pays that skill's health.
PLACES = (
    (SKILL_TEMPLATES_CPP,
     "float UCataclysmAuraSkill::NoteBlowTaken(float DealtToHealth)",
     "Params.HealthFromHitTaken"),
    (REGENERATION_CPP,
     "void UCataclysmRegeneration::ApplyStep(AActor* Character, float SecondsInStep,",
     "ACataclysmGroundZone::RegenerationScaleFor("),
)


def skill_rows() -> list[dict]:
    with WEAPON_SKILLS.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def tags_of(row: dict) -> list[str]:
    return [tag.strip() for tag in str(row.get("Tags") or "").split(",")
            if tag.strip()]


def is_under(tag: str, root: str) -> bool:
    """Whether a tag is the root or one of its children. `Stat.RecoveryRate`
    is not under `Stat.Recovery`: a child begins after a dot."""
    return tag == root or tag.startswith(root + ".")


def skills_tagged_under(rows: list[dict], root: str) -> dict[str, str]:
    """Row name to skill name, for every row carrying a tag under the root."""
    return {row["Name"]: row["SkillName"] for row in rows
            if any(is_under(tag, root) for tag in tags_of(row))}


def parameter_names(row: dict) -> set[str]:
    """The keys a row's `ShapeParams` cell states, as `Key=Value; Key=Value`."""
    return {part.split("=", 1)[0].strip()
            for part in str(row.get("ShapeParams") or "").split(";")
            if "=" in part}


def skills_stating(rows: list[dict], parameter: str) -> set[str]:
    return {row["Name"] for row in rows if parameter in parameter_names(row)}


BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")


def code_only(text: str) -> str:
    """The text with its comments taken out, so a function named in a comment
    is not counted as a call. A `//` inside a string literal would be cut as
    well; neither reading function is called beside one."""
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="replace")


def body_of(text: str, opening: str) -> str:
    """A function's text from the line its definition opens on to the first
    closing brace at the start of a line after it."""
    start = text.find(opening)
    assert start >= 0, f"could not find the definition opening {opening!r}"
    assert text.find(opening, start + 1) < 0, (
        f"{opening!r} opens more than one definition")
    end = text.find("\n}", start)
    assert end >= 0, f"could not find the end of {opening!r}"
    return text[start:end]


def calls_in(text: str, function: str) -> int:
    """How many times the text names the function followed by an opening
    bracket: a call, a declaration or a definition."""
    return len(re.findall(rf"\b{re.escape(function)}\(", text))


def source_files_outside_the_tests() -> list[pathlib.Path]:
    return sorted(path for path in SOURCE.rglob("*")
                  if path.suffix in (".cpp", ".h") and "Tests" not in path.parts)


def test_the_skills_tagged_under_recovery_are_exactly_the_two_the_owner_named():
    rows = skill_rows()
    # THE SCOPE, so an empty read cannot pass: the sheet holds hundreds of rows.
    assert len(rows) > 100, f"only {len(rows)} skill rows were read"
    assert skills_tagged_under(rows, RECOVERY_ROOT) == HEALING_SKILLS


def test_each_place_reads_a_parameter_only_its_own_healing_skill_states():
    rows = skill_rows()
    for parameter, skill in PARAMETER_OF.items():
        assert skills_stating(rows, parameter) == {skill}, parameter
    assert set(PARAMETER_OF.values()) == set(HEALING_SKILLS)


def test_the_two_stats_are_read_at_those_two_places_and_nowhere_else():
    """Each reading function is called once in each of the two places, and
    the whole of the game's source outside its tests names each four times:
    its declaration, its definition and the two calls."""
    for path, opening, shows in PLACES:
        body = code_only(body_of(read(path), opening))
        assert shows in body, (path.name, shows)
        for reader in READERS:
            assert calls_in(body, reader) == 1, (path.name, reader)

    files = source_files_outside_the_tests()
    # THE SCOPE: the two places' files are among the ones counted.
    assert SKILL_TEMPLATES_CPP in files and REGENERATION_CPP in files
    for reader in READERS:
        named = {path.name: calls_in(code_only(read(path)), reader)
                 for path in files}
        named = {name: count for name, count in named.items() if count}
        assert named == {"CataclysmRegeneration.h": 1,
                         "CataclysmRegeneration.cpp": 2,
                         "CataclysmSkillTemplates.cpp": 1}, (reader, named)


def test_the_tag_check_can_fail():
    """The control. Made-up rows, so nothing here depends on the sheet: a
    third skill tagged under the root is found, a tag that only begins with
    the root's letters is not, and a skill that lost its tag is missed."""
    two = [
        {"Name": "A", "SkillName": "First", "Tags": "Type.AOE.Aura, Stat.Recovery.Leech"},
        {"Name": "B", "SkillName": "Second", "Tags": "Stat.Recovery.Regen"},
        {"Name": "C", "SkillName": "Third", "Tags": "Type.Melee, Stat.RecoveryRate"},
    ]
    assert skills_tagged_under(two, RECOVERY_ROOT) == {"A": "First", "B": "Second"}

    third = [*two, {"Name": "D", "SkillName": "Fourth", "Tags": "Stat.Recovery"}]
    assert skills_tagged_under(third, RECOVERY_ROOT) == {
        "A": "First", "B": "Second", "D": "Fourth"}

    lost = [dict(two[0], Tags="Type.AOE.Aura"), *two[1:]]
    assert skills_tagged_under(lost, RECOVERY_ROOT) == {"B": "Second"}


def test_the_parameter_and_call_checks_can_fail():
    """The control for the other two checks, on made-up text."""
    rows = [
        {"Name": "A", "ShapeParams": "Radius=4; HealthFromHitTaken=25"},
        {"Name": "B", "ShapeParams": "Radius=4; HealthFromHitTakenTwice=25"},
        {"Name": "C", "ShapeParams": ""},
    ]
    assert skills_stating(rows, "HealthFromHitTaken") == {"A"}
    assert skills_stating([*rows, {"Name": "D", "ShapeParams": "HealthFromHitTaken=5"}],
                          "HealthFromHitTaken") == {"A", "D"}

    text = (
        "// HealingSkillAmount(a, b) is named in this comment.\n"
        "/* and GiveHealingSkillShield(a, b)\n"
        "   in this one. */\n"
        "float F()\n"
        "{\n"
        "\treturn HealingSkillAmount(a, b) + NotHealingSkillAmount(a, b);\n"
        "}\n")
    assert calls_in(text, "HealingSkillAmount") == 2
    assert calls_in(code_only(text), "HealingSkillAmount") == 1
    assert calls_in(code_only(text), "GiveHealingSkillShield") == 0
    assert "HealingSkillAmount(a, b) +" in body_of(text, "float F()")
