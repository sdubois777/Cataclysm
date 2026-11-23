"""The forge's three damage-type crafts (issue #47, slice F4), ruled by the coordinating session on 2026-11-22.

The Crafting sheet's rows, `game/Data/ForgeOperations.csv`: Infuse Random Damage Type, an Elemental Cinder, 10 CR, 1 base
day, Base Gold 250; Infuse Specific Damage Type, a Sovereign Ingot, 50 CR, 5 base days, Base Gold 1,250; Change Damage
Type, a Prismatic Catalyst, 20 CR, 0 base days, Base Gold 250. Weapons only (Q19). The two infusions are refused at the
drop's maximum, `UCataclysmDropRoll::MaxDamageTypesFor` (Q20); the types an enchantment adds are already in the weapon's
list, so counting the list counts them (Q21, resolved 2026-11-22); a change works at the maximum and draws its new type
among those the weapon lacks (Q22). The weapon made at creation is refused (option (a), issue #2845).

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the sheet's three rows hold the figures the forge's constants state;
  2. the rule reads each row's Base Gold and refuses for the material before the gold, gold last;
  3. the infusions are held to the drop's own maximum and a change is not;
  4. the weapon made at creation is refused by the rule and offered nothing by the screen, so a craft every load would
     undo cannot be bought;
  5. the Python copy of the open types and the draw gives the exact figures FD2 asserts;
  6. the craft writes the type the plan drew or chose, a change in the replaced type's place, and draws nothing again;
  7. the screen offers the infusions only below the maximum.
"""
from __future__ import annotations

import csv
import pathlib
import re
import struct
import sys
import zlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

# THE SOCKET CRAFTS' COPY OF THE SEED MIX, THE COMMENT STRIPPER AND THE CONSTANT READER, and slice F2's damage types.
import test_the_forge_adds_and_changes_sockets as sockets  # noqa: E402
import test_the_forge_adds_and_shifts_affixes as affixes  # noqa: E402

DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"
TEST_CPP = SOURCE / "Tests" / "CataclysmForgeDamageTypesTests.cpp"

M32 = 0xFFFFFFFF
code = sockets.code
body = sockets.body
constant = sockets.constant
TYPES = affixes.TYPES


def rows(name: str) -> list[dict]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def designed(weapon_type: str) -> list[str]:
    """`UCataclysmDropRoll::DamageTypesAvailableTo`, in `DamageTypeNames` order."""
    present = {row["DamageType"] for row in rows("WeaponSkills.csv") if row["WeaponType"] == weapon_type}
    return [each for each in TYPES if each in present]


def seed_text(weapon: dict) -> str:
    return (f"Base={weapon['base']};Level={weapon['level']};Types={'+'.join(weapon['types'])};Affixes=;Enchantments=0:;"
            f"Sockets=;Residue={sockets.f32(weapon['residue']):.6f};Kills=0;Made=0")


def craft_seed(weapon: dict, salt: int, index: int, mask: int, day: int) -> int:
    crc = zlib.crc32(seed_text(weapon).encode("utf-32-le"))
    signed = crc - (1 << 32) if crc >= 1 << 31 else crc
    return sockets.mix(sockets.mix(mask, salt, day), signed, index)


def drawn(seed: int, pool: list[str]) -> str:
    """`FRandomStream(Seed).RandRange(0, N - 1)` over the open types."""
    state = ((seed & M32) * 196314165 + 907633515) & M32
    fraction = (state >> 9) / float(1 << 23)
    return pool[int(struct.unpack("f", struct.pack("f", fraction * float(len(pool))))[0])]


SWORD = {"base": "Weapon_Sword", "level": 3, "types": ["War", "Death"], "residue": 40.0}


# --------------------------------------------------------------------------
# 1 and 2: the sheet's rows and the rule
# --------------------------------------------------------------------------

def test_the_sheets_three_rows_hold_the_forges_figures() -> None:
    operations = {row["Name"]: row for row in rows("ForgeOperations.csv")}
    materials = {row["Name"]: row for row in rows("CraftingMaterials.csv")}
    names = dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+Row) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))
    per_day = constant("GoldPerCraftDay")
    for row_field, material_field, residue, days in (
            ("InfuseRandomDamageTypeOperationRow", "ElementalCinderRow", "InfuseRandomDamageTypeResidue",
             "InfuseRandomDamageTypeBaseDays"),
            ("InfuseSpecificDamageTypeOperationRow", "SovereignIngotRow", "InfuseSpecificDamageTypeResidue",
             "InfuseSpecificDamageTypeBaseDays"),
            ("ChangeDamageTypeOperationRow", "PrismaticCatalystRow", "ChangeDamageTypeResidue", "ChangeDamageTypeBaseDays")):
        row = operations.get(names.get(row_field, ""))
        assert row is not None, f"ForgeOperations.csv has no row {names.get(row_field)}, which {row_field} names"
        material = materials.get(names.get(material_field, ""))
        assert material is not None, f"CraftingMaterials.csv has no row {names.get(material_field)}"
        assert row["Materials"] == material["MaterialName"], (row["Materials"], material["MaterialName"])
        assert row["CRImpact"] == f"{constant(residue):g} CR", (row_field, row["CRImpact"])
        assert float(row["BaseDays"]) == constant(days), (row_field, row["BaseDays"])
        assert float(row["BaseGold"]) == per_day * max(1.0, constant(days)), (row_field, row["BaseGold"])


def test_the_rule_reads_each_row_and_refuses_for_the_material_before_the_gold() -> None:
    rule = body(code(FORGE_CPP), "FCataclysmForgeDamageTypeWork UCataclysmForge::DamageTypeWorkOf(")
    for row, days in (("InfuseRandomDamageTypeOperationRow", "InfuseRandomDamageTypeBaseDays"),
                      ("InfuseSpecificDamageTypeOperationRow", "InfuseSpecificDamageTypeBaseDays"),
                      ("ChangeDamageTypeOperationRow", "ChangeDamageTypeBaseDays")):
        assert f"BaseGoldOf({row}, BaseGoldFor({days}))" in rule, f"DamageTypeWorkOf does not read {row}'s Base Gold"
    assert "Out.Days = DaysFor(Item.Residue, BaseDays);" in rule, "DamageTypeWorkOf does not use DaysFor"
    material = rule.find("if (Out.MaterialHeld < 1)")
    gold = rule.find("Out.Refusal = GoldRefusal(Doing, Out.Gold, Out.GoldHeld);")
    assert -1 < material < gold, "DamageTypeWorkOf asks for gold before its material"
    assert rule.rfind("Out.Refusal") == gold, "gold is not the last refusal"


# --------------------------------------------------------------------------
# 3 and 4: the maximum and the weapon made at creation
# --------------------------------------------------------------------------

def test_the_infusions_are_held_to_the_drops_maximum_and_a_change_is_not() -> None:
    text = code(FORGE_CPP)
    most = body(text, "int32 UCataclysmForge::MaxDamageTypesOf(")
    assert "UCataclysmDropRoll::MaxDamageTypesFor(" in most and "UCataclysmDropRoll::DamageTypesAvailableTo(" in most, (
        "the forge's maximum is no longer the drop's")
    rule = body(text, "FCataclysmForgeDamageTypeWork UCataclysmForge::DamageTypeWorkOf(")
    change = rule.find("if (Craft == ECataclysmForgeDamageTypeCraft::Change)")
    at_most = rule.find("else if (Item.DamageTypes.Num() >= Out.MaxTypes)")
    assert -1 < change < at_most, "the maximum is not refused in the branch a change does not take"
    assert "That weapon carries its most damage types, %d." in rule, "the refusal at the maximum does not name it"
    assert "DamageTypesCarriedBy" not in rule, (
        "the rule counts an enchantment's damage types apart from the weapon's list; they are already in it (Q21)")


def test_the_weapon_made_at_creation_is_refused_and_offered_nothing() -> None:
    rule = body(code(FORGE_CPP), "FCataclysmForgeDamageTypeWork UCataclysmForge::DamageTypeWorkOf(")
    refused = rule.find("if (Item.bMadeAtCreation)")
    assert refused != -1 and "Out.Refusal = CreationWeaponRefusal;" in rule[refused:refused + 120], (
        "the weapon made at creation is not refused, so a craft every load undoes can be bought (#2845)")
    assert refused < rule.find("if (Out.MaterialHeld < 1)"), "the creation weapon is refused only after the material"
    offers = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeDamageTypeAction> UCataclysmForgeLayout::DamageTypeActionsFor(")
    assert "|| Item.bMadeAtCreation)" in offers, "the screen offers damage-type crafts on the weapon made at creation"


# --------------------------------------------------------------------------
# 5: the draw
# --------------------------------------------------------------------------

def test_the_python_copy_gives_the_figures_fd2_asserts() -> None:
    test = TEST_CPP.read_text(encoding="utf-8")
    pool = [each for each in designed("Sword") if each not in SWORD["types"]]
    assert pool == ["Demonic", "Pestilence", "Famine", "Celestial", "Chaos"], pool
    assert f'FString(TEXT("{", ".join(pool)}"))' in test, "FD2 no longer asserts the sword's open types"
    infuse = craft_seed(SWORD, int(constant("InfusedDamageTypeSalt")), 2, 12345, 7)
    change = craft_seed(SWORD, int(constant("ChangedDamageTypeSalt")), 0, 12345, 7)
    assert f"InfuseSeed, {infuse});" in test and f"ChangeSeed, {change});" in test, (infuse, change)
    assert drawn(infuse, pool) == "Chaos" and drawn(change, pool) == "Famine", (drawn(infuse, pool), drawn(change, pool))
    counts = {each: 0 for each in pool}
    for day in range(1, 3001):
        counts[drawn(craft_seed(SWORD, int(constant("InfusedDamageTypeSalt")), 2, 12345, day), pool)] += 1
    assert counts == {"Demonic": 592, "Pestilence": 605, "Famine": 601, "Celestial": 585, "Chaos": 617}, counts
    for each, count in counts.items():
        assert f"{each} {count}" in test, f"FD2 does not assert {each} {count}"
    assert designed("Shield") == ["War", "Celestial", "Chaos"], "FD3 and FD4's full shield is no longer full"


# --------------------------------------------------------------------------
# 6 and 7: the craft and the screen
# --------------------------------------------------------------------------

def test_the_craft_writes_the_plans_type_in_place_and_draws_nothing_again() -> None:
    craft = body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::CraftDamageType(")
    for redraw in ("AffixCraftSeed(", "DamageTypeWorkOf(", "FRandomStream", "RandRange("):
        assert redraw not in craft, f"CraftDamageType draws again with {redraw}"
    assert "Changed.DamageTypes[Replaced] = Plan.ToType;" in craft, "a change no longer puts the new type in the old one's place"
    assert "Changed.DamageTypes.Add(Plan.ToType);" in craft, "an infusion no longer adds the type"
    written = craft.find("Changed.DamageTypes[Replaced] = Plan.ToType;")
    assert -1 < written < craft.find("Run->AdvanceDays(Plan.Days)"), "the weapon is written after the days pass"
    plan = body(code(CRAFT_CPP), "FCataclysmForgeDamageTypeWork UCataclysmForgeCraft::DamageTypeWorkPlanFor(")
    assert "Item->DamageTypes.IndexOfByKey(Chosen) : Item->DamageTypes.Num()" in plan, (
        "a change's seed is no longer the replaced type's place, or an infusion's the new type's")


def test_the_screen_offers_the_infusions_only_below_the_maximum() -> None:
    offers = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeDamageTypeAction> UCataclysmForgeLayout::DamageTypeActionsFor(")
    below = offers.find("if (Item.DamageTypes.Num() < MaxTypes && !Open.IsEmpty())")
    assert -1 < below < offers.find("ECataclysmForgeDamageTypeCraft::InfuseRandom"), (
        "the screen offers an infusion at the maximum")
