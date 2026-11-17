"""Every tag an effect row requires is carried by something the game grants.

WHY THIS EXISTS. Issue #1785. A row's `RequiredTags` scopes it: the row applies only
to a skill, hit or healing that carries the tag, or a tag below it (the hierarchy is
`UCataclysmStatPipeline`'s; a requirement of `Type.AOE` is met by
`Type.AOE.PointBlank`). A tag that is declared in the Tags sheet passes the
generator, so a row requiring a tag that NOTHING carries is accepted, reads
correctly, and applies to nothing, silently. `Keyword.DoT.Generic` was described as
"Any DoT effect" while nothing carried it; requiring it would have matched no damage
over time. Its description now says what carries it.

WHAT CARRIES A TAG, for this check:

- a skill row of `game/Data/WeaponSkills.csv`: its `Tags`, and `Slot.<its Slot>`,
  which the generator derives from the Slot column (`tags_with_slot`);
- a minion row of `game/Data/MinionTypes.csv`: its `Tags`;
- ENGINE_GRANTED below: a tag the game code puts on a hit, an effect or a healing,
  each entry naming the file that grants it. Each was read in that code before it
  was written here (issue #1785, 2026-10-28), and the test checks that the file
  still names the tag, so an entry cannot outlive its grant unnoticed. A name in code
  is NOT taken as a grant on its own, because code also names a tag to ASK for it.

Ruled by the coordinating session on 2026-10-28. A failure names each row and tag.
"""

from __future__ import annotations

import collections
import csv
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"

#: The tables whose `RequiredTags` column scopes a row.
REQUIRING = ("EnchantmentEffects.csv", "PassiveEffects.csv")

#: TAGS THE GAME CODE GRANTS, each with the file that grants it and how. Read in
#: the code on 2026-10-28 for issue #1785.
ENGINE_GRANTED = {
    "Keyword.DoT.Poison": (
        "AbilitySystem/CataclysmAilments.cpp",
        "the Poison ailment's own tag in the ailment table, requested by name for its effect"),
    "Keyword.Leech": (
        "AbilitySystem/CataclysmFervour.cpp",
        "UCataclysmFervour::LeechTag, added to leech healing in CataclysmLeech.cpp"),
    "Keyword.Regeneration": (
        "AbilitySystem/CataclysmFervour.cpp",
        "UCataclysmFervour::RegenerationTag, added to regeneration in CataclysmRegeneration.cpp"),
    "Slot.Basic": (
        "AbilitySystem/CataclysmGameplayAbility.cpp",
        "the basic attack's slot tag, put on a granted skill through CataclysmAbilitySlots::Tag"),
    "Keyword.DoT.Generic": (
        "AbilitySystem/CataclysmMinion.cpp",
        "the skill trap's own damage over time, ACataclysmMinion::LaySkillTrapsDamageOverTime (F9)"),
}


def rows_of(name: str) -> list[dict[str, str]]:
    with (DATA / name).open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def tags_in(cell: str) -> list[str]:
    return [tag.strip() for tag in (cell or "").split(",") if tag.strip()]


def carried_tags() -> set[str]:
    """Every tag a skill, a minion or the engine carries."""
    carried = set(ENGINE_GRANTED)
    for row in rows_of("WeaponSkills.csv"):
        carried.update(tags_in(row["Tags"]))
        if row.get("Slot", "").strip():
            carried.add("Slot." + row["Slot"].strip())
    for row in rows_of("MinionTypes.csv"):
        carried.update(tags_in(row["Tags"]))
    return carried


def is_met(required: str, carried: set[str]) -> bool:
    """A requirement is met by the tag itself or by any tag below it."""
    return any(tag == required or tag.startswith(required + ".") for tag in carried)


def required_tags() -> dict[str, list[str]]:
    """Every required tag, with the rows that require it."""
    found: dict[str, list[str]] = collections.defaultdict(list)
    for table in REQUIRING:
        for row in rows_of(table):
            for tag in tags_in(row["RequiredTags"]):
                found[tag].append(f"{table} {row['Name']}")
    return found


def test_the_check_reads_required_tags():
    """THE SCOPE, read first: both tables are read and hold requirements, so the
    test below cannot pass by reading nothing."""
    found = required_tags()
    for table in REQUIRING:
        assert any(where.startswith(table + " ") for rows in found.values() for where in rows), (
            f"No row of {table} requires a tag; the column was renamed or emptied, and "
            "this check would be reading nothing there.")


def test_every_engine_grant_is_still_in_its_code():
    """An entry of ENGINE_GRANTED names the file that grants its tag; that file
    must still name the tag, or the entry is stale."""
    stale = []
    for tag, (path, how) in ENGINE_GRANTED.items():
        text = (SOURCE / path).read_text(encoding="utf-8", errors="replace")
        if f'"{tag}"' not in text:
            stale.append(f"{tag}: {path} no longer names it ({how})")
    assert not stale, "\n  ".join(["Stale ENGINE_GRANTED entries:"] + stale)


def test_every_required_tag_is_carried_by_something():
    carried = carried_tags()
    wrong = [f"{tag}, required by {', '.join(sorted(rows))}"
             for tag, rows in sorted(required_tags().items())
             if not is_met(tag, carried)]
    assert not wrong, (
        "These required tags are carried by no skill, no minion and nothing in "
        "ENGINE_GRANTED, so their rows apply to nothing. Require a tag something "
        "carries (Keyword.DoT for any damage over time), or, if the game code "
        "grants it, add it to ENGINE_GRANTED with the file that does:\n  "
        + "\n  ".join(wrong))


def test_a_requirement_is_met_by_itself_or_a_tag_below_it():
    """The hierarchy, on made-up tags, so no change to the real tables can hide a
    mistake in it."""
    carried = {"Type.AOE.PointBlank", "Keyword.DoT.Bleed"}
    assert is_met("Type.AOE", carried)
    assert is_met("Type.AOE.PointBlank", carried)
    assert is_met("Keyword.DoT", carried)
    # A SIBLING IS NOT A PARENT: the case #1785 was about.
    assert not is_met("Keyword.DoT.Generic", carried)
    # AND A PREFIX OF A NAME IS NOT A PARENT.
    assert not is_met("Type.AO", carried)
