"""A dungeon rule gives a creature attack damage as a part, never by writing the attribute. Issue #2723.

WHAT WENT WRONG. Soul Harvest, Nothing Is Forgotten and Vengeful Wraiths read a
creature's attack damage attribute, added to it and wrote it back. Every damage
multiplier a floor rule sets -- Famished Beasts, Grim Totems, Ravenous Hoard on the
beat -- ends in `ACataclysmEnemyCharacter::WriteAttackDamage`, which rewrites the
attribute from the creature's own figure. So a creature that took souls and was then
made hungry lost the souls' damage, and a load did the same, because
`RestoreCreatureRoles` gives the souls back before it writes the multipliers.

THE FIX IS ONE FORMULA. Each rule writes its own part with
`SetRuleDamagePart`, in its bucket, and `WriteAttackDamage` builds the figure from
the creature's own damage and every part through `AttackDamageFromParts`:
`(own + flat) x (1 + increases) x more`, the project's three buckets.

WHY PYTHON READS C++ HERE. Continuous integration compiles no C++. The Unreal tests
in `Cataclysm.RuleDamageParts.` hold the figures; these hold the shape, so a rule
written later the old way -- straight onto the attribute -- fails a pull request
rather than waiting for a window.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
GAME_MODE = SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp"
CREATURE = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"
CREATURE_HEADER = SOURCE / "Character" / "CataclysmEnemyCharacter.h"


def without_comments(text: str) -> str:
    """The text with every `//` comment and `/* */` block taken out.

    BY SYNTAX AND NOT BY A LINE'S FIRST CHARACTER: a code line can open with `*`
    (a multiplication continued from the line above), and a comment can follow code
    on the same line. Neither file holds `//` or `/*` inside a string literal that
    this would cut, which the counts below would show.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def body_of(text: str, opening: str) -> str:
    """The braced body that follows `opening`, by counting braces.

    ANOTHER COPY OF THE SUITE'S HELPER: `test_dungeon_modifier_rules_are_the_rows.py`
    records why there is no shared module. A regex cannot do this, because every
    function it is pointed at contains nested braces.
    """
    start = text.index(opening)
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"braces never balanced after {opening!r}")


def calls_of(text: str, name: str) -> list[str]:
    """Every call `name(...)` in `text`, arguments and all, by counting parentheses."""
    found = []
    for match in re.finditer(re.escape(name) + r"\s*\(", text):
        depth = 0
        for index in range(match.end() - 1, len(text)):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    found.append(text[match.start():index + 1])
                    break
    return found


def test_no_dungeon_rule_writes_a_creatures_attack_damage_attribute():
    """The class of the fault: a rule writing the attribute is dropped by the next multiplier.

    EVERY `SetNumericAttributeBase` CALL IN THE GAME MODE IS READ, and none may name the
    attack damage attribute. The count of calls read is asserted too, so a parse that
    found nothing cannot pass. Measured 2026-11-17: 26 calls, every occurrence of the
    name in the file; 30 before #2723, whose four attack damage writes this removed.
    """
    code = without_comments(GAME_MODE.read_text(encoding="utf-8"))
    writes = calls_of(code, "SetNumericAttributeBase")
    assert len(writes) >= 20, (
        f"Only {len(writes)} SetNumericAttributeBase calls were found in "
        "CataclysmDungeonGameMode.cpp; the reader has stopped finding them, so this "
        "check would pass whatever the file held.")
    onto_damage = [one for one in writes if "AttackDamage" in one]
    assert not onto_damage, (
        "A dungeon rule writes a creature's attack damage attribute straight onto it:\n  "
        + "\n  ".join(" ".join(one.split()) for one in onto_damage)
        + "\nThe next damage multiplier any rule sets rewrites that attribute from the "
        "creature's own figure through WriteAttackDamage, and what was written is lost "
        "(issue #2723). Give the creature a part with SetRuleDamagePart instead.")


def test_each_rule_that_gives_damage_writes_its_own_part_in_its_bucket():
    """The three rules and the buckets they were ruled into on 2026-11-17.

    SOUL HARVEST a "more" of 1 + 10% a soul; NOTHING IS FORGOTTEN flat damage, on
    the feeding and on a load; VENGEFUL WRAITHS an increase, the row's own word.
    """
    code = without_comments(GAME_MODE.read_text(encoding="utf-8"))
    expected = (
        ("void ACataclysmDungeonGameMode::ApplySoulHarvestFigures(", "SoulHarvestDamageSource",
         r"Souls\.More\s*=\s*1\.0f\s*\+\s*Effects::SoulHarvestDamageAdded\(1\.0f,\s*Held->Souls\)"),
        ("void ACataclysmDungeonGameMode::ApplyNothingIsForgottenFigures(", "NothingIsForgottenDamageSource",
         r"Void\.Added\s*=\s*Effects::NothingIsForgottenDamageAdded\(\s*NothingIsForgottenDamage,\s*"
         r"Creature->OwnAttackDamage\(\)\)"),
        ("void ACataclysmDungeonGameMode::RestoreCreatureRoles(", "NothingIsForgottenDamageSource",
         r"Void\.Added\s*=\s*NothingIsForgottenDamageGiven"),
        ("void ACataclysmDungeonGameMode::ApplyVengefulWraithFigures(", "VengefulWraithsDamageSource",
         r"Increase\.IncreasedPercent\s*=\s*Effects::VengefulWraithsIncreasePercent"),
    )
    for opening, key, bucket in expected:
        body = body_of(code, opening)
        parts = [one for one in calls_of(body, "SetRuleDamagePart") if key in one]
        assert len(parts) == 1, (
            f"{opening.split('::')[1].rstrip('(')} writes its damage part under "
            f"{key} {len(parts)} times, not once. Without it, the rule's damage reaches "
            "the creature some other way, which a damage multiplier written later drops "
            "(issue #2723).")
        assert re.search(bucket, body), (
            f"{opening.split('::')[1].rstrip('(')} no longer fills its part as ruled on "
            f"2026-11-17 (expected {bucket!r}). The bucket a rule's damage sits in decides "
            "what every multiplier does to it; see docs/DECISIONS.md, issue #2723.")


def test_the_creature_builds_its_attack_damage_from_its_parts_in_the_three_buckets():
    """One formula, and the one write that uses it for every creature not holding a build."""
    code = without_comments(CREATURE.read_text(encoding="utf-8"))
    formula = body_of(code, "float ACataclysmEnemyCharacter::AttackDamageFromParts(")
    assert re.search(
        r"\(\s*Own\s*\+\s*Added\s*\)\s*\*\s*\(\s*1\.0f\s*\+\s*IncreasedPercent\s*/\s*100\.0f\s*\)\s*\*\s*More",
        formula), (
        "AttackDamageFromParts no longer computes (Own + Added) x (1 + IncreasedPercent / "
        "100) x More, the project's three buckets.")

    write = body_of(code, "void ACataclysmEnemyCharacter::WriteAttackDamage(")
    assert "SumRuleDamageParts(" in write and "AttackDamageFromParts(" in write, (
        "WriteAttackDamage no longer builds the attack damage from the rule parts, so the "
        "next damage multiplier drops Soul Harvest's, Nothing Is Forgotten's and a "
        "wraith's damage again (issue #2723).")
    assert re.search(r"More\s*\*\s*DamageMultiplierProduct\(\)", write), (
        "WriteAttackDamage no longer multiplies the rule parts' \"more\" by the floor's "
        "multipliers in one product.")

    summed = body_of(code, "void ACataclysmEnemyCharacter::SumRuleDamageParts(")
    for line in ("OutAdded += One.Value.Added;",
                 "OutIncreasedPercent += One.Value.IncreasedPercent;",
                 "OutMore *= One.Value.More;"):
        assert line in summed, (
            f"SumRuleDamageParts no longer says {line!r}: flat and increases add within "
            "their buckets, and each \"more\" multiplies on its own.")


def test_the_three_part_keys_are_distinct():
    """Two rules sharing a key would overwrite each other's part."""
    header = CREATURE_HEADER.read_text(encoding="utf-8")
    keys = {}
    for name in ("SoulHarvestDamageSource", "NothingIsForgottenDamageSource",
                 "VengefulWraithsDamageSource"):
        found = re.search(rf'{name}\s*=\s*TEXT\("(\w+)"\)', header)
        assert found, f"{name} is no longer declared in CataclysmEnemyCharacter.h."
        keys[name] = found.group(1)
    assert len(set(keys.values())) == len(keys), f"Two rule parts share a key: {keys}"
