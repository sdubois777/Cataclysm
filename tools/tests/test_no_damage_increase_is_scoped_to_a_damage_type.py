"""No damage increase is scoped to a damage type.

THE OWNER'S RULE, 2026-10-25, relayed by the coordinating session: "we shouldn't have anything that says "increased X
damage". It should all be generic for this reason." The reason is the one the rule was given for: since #857 a
character's class trees and skills follow every damage type its worn weapons carry, so a bonus scoped to one damage type
would make carrying more types an unearned multiplier. Asked the same day about Burning Wrath's self buff and
Conflagration's aura on allies, which scoped their bonus to the skill's own element in C++, the owner answered "Make
them generic", and they were (the decisions-log entry "Element-scoped buffs raise all damage").

WHAT THIS REFUSES. A row of `game/Data/EnchantmentEffects.csv` or `game/Data/PassiveEffects.csv` whose stat is a damage
stat and whose RequiredTags names an `Element.*` tag; and an affix of `game/Data/Affixes.csv` whose damage stat (or
either hybrid part) names a damage type.

WHAT IT ALLOWS, AS THE OWNER RULED: the eight "Increased damage against <type> enemies" affixes (`damage_vs_<type>`),
which scope by the ENEMY's type, so a weapon's carried types do not affect them. And a damage row scoped by a skill's
own tags (`Type.Spell`, `Slot.Ultimate`, `Keyword.DoT.Burn` and the like), which the rule does not concern.

A DAMAGE STAT is one whose name contains "damage", less the damage a character TAKES (`damage_taken`, `...reduction`),
which is not an increase to what it deals. Replaced, on 2026-10-25, the deleted
`test_buff_magnitudes.py::test_every_skill_with_an_increase_carries_an_element_tag`, whose reason was the opposite rule.

The first count of 2026-10-25 (the enchantment session's `t89_element_scope.py`) found 0 such rows.
"""
import csv
import pathlib

DATA = pathlib.Path(__file__).resolve().parents[2] / "game" / "Data"
DAMAGE_TYPES = ("war", "demonic", "death", "pestilence", "famine", "celestial", "chaos", "void")


def _rows(name: str) -> list[dict]:
    with open(DATA / name, encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def _is_damage_dealt(stat: str) -> bool:
    lowered = (stat or "").lower()
    return "damage" in lowered and "damage_taken" not in lowered and "reduction" not in lowered


def _element_tags(tags: str) -> list[str]:
    return [tag.strip() for tag in (tags or "").split(",") if tag.strip().startswith("Element.")]


def _against_an_enemys_type(stat: str) -> bool:
    lowered = (stat or "").lower()
    return "damage_vs_" in lowered or "against" in lowered


def test_no_effect_row_scopes_a_damage_stat_to_a_damage_type() -> None:
    offenders = [
        f"{source}: {row['Name']} ({row['Stat']}, RequiredTags {row.get('RequiredTags')!r})"
        for source in ("EnchantmentEffects.csv", "PassiveEffects.csv")
        for row in _rows(source)
        if _is_damage_dealt(row.get("Stat", "")) and _element_tags(row.get("RequiredTags", ""))
    ]
    assert not offenders, (
        "These rows scope a damage increase to a damage type, which the owner ruled out on 2026-10-25 (\"It should "
        "all be generic for this reason.\"):\n  " + "\n  ".join(offenders))


def test_no_affix_scopes_a_damage_stat_to_a_damage_type() -> None:
    offenders = []
    for row in _rows("Affixes.csv"):
        for part in (row.get("Stat", ""), row.get("HybridPart1", ""), row.get("HybridPart2", "")):
            lowered = (part or "").lower()
            if (_is_damage_dealt(part) and not _against_an_enemys_type(part)
                    and any(kind in lowered for kind in DAMAGE_TYPES)):
                offenders.append(f"{row['Name']} ({part})")
    assert not offenders, (
        "These affixes scope a damage increase to a damage type, which the owner ruled out on 2026-10-25 (\"It should "
        "all be generic for this reason.\"):\n  " + "\n  ".join(offenders))


def test_the_check_reads_rows_and_lets_the_against_enemy_affixes_through() -> None:
    """A control: the files are read, and the eight against-enemy affixes are seen and allowed."""
    assert len(_rows("EnchantmentEffects.csv")) > 500
    assert len(_rows("PassiveEffects.csv")) > 100
    against = [row["Name"] for row in _rows("Affixes.csv") if _against_an_enemys_type(row.get("Stat", ""))
               and any(kind in row.get("Stat", "").lower() for kind in DAMAGE_TYPES)]
    assert len(against) == 8, against
