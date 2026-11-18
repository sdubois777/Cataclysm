"""The Corrupted Stalker's six weapon kits, issue #2580: complete, worn, and saying what they do.

THREE CHECKS, EACH OF SOMETHING THE ENGINE TESTS CANNOT SEE WITHOUT A WINDOW.

1. Every entry `ACataclysmCorruptedStalkerCharacter::Entries` authors with `KitEntry` names a weapon and a damage type
   whose five weapon rows and Aura row in `game/Data/WeaponSkills.csv` all carry a name and a shape. A row with no
   shape is one the Stalker's brain skips, so an entry pointing at an incomplete kit would fight with fewer skills
   and nothing would say so until a window ran.
2. No row laying Necrosis says its target cannot be healed, or names a potion. The owner ruled on 2026-11-17 that "a
   Stalker's Necrosis spares potions", and the potion exemption (#2730) makes it so; a sentence promising that every
   heal is denied would be false whenever the Stalker lays it on the player.
3. An Aura row that pulls or knocks back on every pulse either pulses less often than the five-second displacement
   window, so every pulse moves its whole stated distance, or says in its words that later pushes go "half as far".
   Every displacement inside five seconds of the last moves half as far (`TakeNextDisplacementShare`, #302), so a
   sentence saying "each second ... 2 meters" without that is true for the first second only. #2730's entry asked
   the row authors to tune and word Undertow and Aegis of Dawn for it.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
STALKER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
               / "CataclysmCorruptedStalkerCharacter.cpp")

#: `UCataclysmSkillEffects::StunImmunityWindowSeconds`, which the displacement window reuses.
DISPLACEMENT_WINDOW_SECONDS = 5.0

KIT_SLOTS = ("Heavy", "Special", "Support", "Movement", "Ultimate")


@pytest.fixture(scope="module")
def rows() -> list[dict[str, str]]:
    if not TABLE.is_file():
        pytest.skip(f"{TABLE.name} is not present")
    with TABLE.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def params(row: dict[str, str]) -> dict[str, str]:
    out = {}
    for piece in row["ShapeParams"].split(";"):
        if "=" in piece:
            key, value = piece.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def kit_entries() -> list[tuple[str, str, str]]:
    """(damage type, entry name, weapon type) for every `KitEntry(...)` line in `Entries`."""
    if not STALKER_CPP.is_file():
        pytest.skip(f"{STALKER_CPP.name} is not present")
    text = STALKER_CPP.read_text(encoding="utf-8")
    return re.findall(
        r'KitEntry\(TEXT\("(\w+)"\), TEXT\("([^"]+)"\), TEXT\("(\w+)"\), TEXT\("\w+"\)\)', text)


def test_the_six_kit_entries_are_read():
    """THE CONTROL for the check below: it reads six entries, so an empty match cannot pass it vacuously."""
    found = kit_entries()
    assert sorted(t for t, _, _ in found) == sorted(
        ["Death", "Pestilence", "Famine", "Celestial", "Chaos", "Void"]), found


@pytest.mark.parametrize("entry", kit_entries(), ids=lambda e: e[0])
def test_each_kit_entry_wears_a_complete_kit(rows, entry):
    damage, name, weapon = entry
    by_slot = {r["Slot"]: r for r in rows if r["DamageType"] == damage and r["WeaponType"] == weapon}
    by_slot["Aura"] = next((r for r in rows if r["DamageType"] == damage and r["WeaponType"] == "All"
                            and r["Slot"] == "Aura"), None)
    for slot in (*KIT_SLOTS, "Aura"):
        row = by_slot.get(slot)
        assert row is not None, f"{name} carries a {damage} {weapon}, and the table has no {slot} row for it"
        assert row["SkillName"].strip() and row["Shape"].strip(), (
            f"{name} carries a {damage} {weapon}, and its {slot} row {row['Name']} has no name or no shape, so "
            f"its brain would skip that slot")


def test_no_row_laying_necrosis_promises_that_every_heal_is_denied(rows):
    necrotic = [r for r in rows if params(r).get("Ailment") == "Necrosis"]
    assert len(necrotic) >= 3, "the Death kit's three Necrosis rows are not read"
    wrong = [r["Name"] for r in necrotic
             if re.search(r"cannot be healed|no healing|potion", r["SkillDescription"], re.IGNORECASE)]
    assert not wrong, (
        f"{wrong} lay Necrosis and say their target cannot be healed or name a potion. A Stalker's Necrosis spares "
        f"potions (the owner, 2026-11-17), so the sentence would be false on the player.")


def displacing_auras(rows):
    return [r for r in rows if r["Shape"] == "Aura"
            and ("Knockback" in params(r) or "Pull" in params(r).get("ForcedMovement", ""))]


def test_an_aura_that_displaces_every_pulse_is_tuned_or_worded_for_the_halving(rows):
    found = displacing_auras(rows)
    assert {r["SkillName"] for r in found} >= {"Undertow", "Aegis of Dawn"}, (
        f"the displacing auras read are {[r['SkillName'] for r in found]}; Undertow and Aegis of Dawn are missing")
    wrong = []
    for r in found:
        interval = float(params(r).get("Interval", "1"))
        if interval <= DISPLACEMENT_WINDOW_SECONDS and "half as far" not in r["SkillDescription"]:
            wrong.append((r["Name"], interval))
    assert not wrong, (
        f"{wrong} pulse every {DISPLACEMENT_WINDOW_SECONDS} s or more often and do not say that later pushes go "
        f"half as far. Every displacement inside {DISPLACEMENT_WINDOW_SECONDS} s of the last moves half as far, so "
        f"pulse at an interval above it, or state the halving in the sentence. #2730, #2580.")
