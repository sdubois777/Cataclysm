"""The forge's Add Socket and Change Socket Type (issue #46, slice G9), ruled by the coordinating session on 2026-11-22.

The Crafting sheet's rows, `game/Data/ForgeOperations.csv`: Add Socket, a Shattered Core, 15 CR, 3 base days, Base Gold
750; Change Socket Type, a Focusing Lens, 10 CR, 2 base days, Base Gold 500. An added socket is empty and its type rolled
evenly among Attack, Defense and Utility (Q1), from a seed of the run's `WalkSeedMask`, the run's day, the CRC of
everything the item saves written out as text, and its socket count (Q2, option (a)); the slot's maximum caps it (Q3).
A changed socket takes the type chosen, among the two it is not (Q6); a socket holding a gem (Q4) and a socket for any
gem (Q5) are not changed.

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the sheet's two rows hold the figures the forge's constants state, and their materials are crafting materials --
     without it a changed sheet and an unchanged constant charge two prices;
  2. each rule reads its row's Base Gold and refuses for the material before the gold, gold last, as every craft;
  3. the seed text names every field the item, its affixes, enchantments and sockets save -- a field left out lets two
     items that differ only in it roll the same type;
  4. the Python copy of the seed text, the CRC, the mix and the stream gives the exact figures FS2 asserts, so the
     C++ and this copy agree on a fixed pair: a helm rolling Attack, the same helm with one affix roll changed rolling
     Utility, and the spread over 3000 days;
  5. the craft rolls the added socket's type from the run's mask and day BEFORE the craft's days pass, so the same item
     on the same day rolls the same type;
  6. the screen offers no change for a socket holding a gem or one for any gem.
"""
from __future__ import annotations

import csv
import pathlib
import re
import struct
import zlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"
ITEM_H = SOURCE / "Items" / "CataclysmItem.h"
TEST_CPP = SOURCE / "Tests" / "CataclysmForgeSocketsTests.cpp"

M32 = 0xFFFFFFFF
TYPES = ["Attack", "Defense", "Utility"]


def code(path: pathlib.Path) -> str:
    """The file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    depth, at = 0, text.index("{", start)
    for index in range(at, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[at:index + 1]
    raise AssertionError(f"{signature!r} has no closing brace")


def constant(name: str) -> float:
    found = re.search(rf"static constexpr (?:int32|float) {name} = (0x[0-9A-F]+|\d+(?:\.\d+)?)f?;", code(FORGE_H))
    assert found, f"{FORGE_H.name} declares no number {name}"
    return float(int(found.group(1), 16)) if found.group(1).startswith("0x") else float(found.group(1))


def rows(name: str) -> dict[str, dict]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


# --------------------------------------------------------------------------
# The Python copy of UCataclysmForge::AddedSocketType
# --------------------------------------------------------------------------

def f32(value: float) -> float:
    return struct.unpack("f", struct.pack("f", value))[0]


def finalise(mixed: int) -> int:
    mixed ^= mixed >> 16
    mixed = (mixed * 0x7FEB352D) & M32
    mixed ^= mixed >> 15
    mixed = (mixed * 0x846CA68B) & M32
    mixed ^= mixed >> 16
    return mixed & 0x7FFFFFFF


def mix(seed: int, salt: int, index: int) -> int:
    """`CataclysmSeedMix::Mix`."""
    mixed = ((seed & M32) * 0x9E3779B1 + (salt & M32) * 0x85EBCA6B + (index & M32) * 0xC2B2AE35) & M32
    return finalise(mixed) or 1


def roll_type(seed: int) -> str:
    """`UCataclysmGems::RollSocketType` on a fresh `FRandomStream(seed)`: `RandRange(0, 2)`."""
    state = ((seed & M32) * 196314165 + 907633515) & M32
    fraction = (state >> 9) / float(1 << 23)
    return TYPES[int(f32(fraction * f32(3.0)))]


def seed_text(item: dict) -> str:
    """`UCataclysmForge::ItemSeedText` for an item with no damage types and no enchantments."""
    affixes = "|".join(f"{a},{t},{f32(r):.6f},{'+'.join(d)}" for a, t, r, d in item["affixes"])
    sockets = "|".join(f"{t},{g},{r}" for t, g, r in item["sockets"])
    return (f"Base={item['base']};Level={item['level']};Types=;Affixes={affixes};Enchantments=0:;Sockets={sockets};"
            f"Residue={f32(item['residue']):.6f};Kills=0;Made=0")


def added_seed(item: dict, mask: int, day: int) -> int:
    """`UCataclysmForge::AddedSocketSeed`: `FCrc::StrCrc32` is the standard CRC-32 over each character as four bytes."""
    crc = zlib.crc32(seed_text(item).encode("utf-32-le"))
    signed = crc - (1 << 32) if crc >= 1 << 31 else crc
    return mix(mix(mask, int(constant("AddedSocketSalt")), day), signed, len(item["sockets"]))


def seed_helm(roll: float) -> dict:
    """FS2's `SeedHelm`."""
    return {"base": "Head_Helm", "level": 3, "affixes": [("Stat_Flat_evasion", 2, roll, [])],
            "sockets": [("Defense", "None", 0)], "residue": 40.0}


# --------------------------------------------------------------------------
# 1 and 2: the sheet's rows and the rules
# --------------------------------------------------------------------------

def test_the_sheets_two_rows_hold_the_forges_figures() -> None:
    operations = rows("ForgeOperations.csv")
    materials = rows("CraftingMaterials.csv")
    names = dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+Row) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))
    for row_field, material_field, residue, days in (
            ("AddSocketOperationRow", "ShatteredCoreRow", "AddSocketResidue", "AddSocketBaseDays"),
            ("ChangeSocketTypeOperationRow", "FocusingLensRow", "ChangeSocketTypeResidue", "ChangeSocketTypeBaseDays")):
        row = operations.get(names.get(row_field, ""))
        assert row is not None, f"ForgeOperations.csv has no row {names.get(row_field)}, which {row_field} names"
        material = materials.get(names.get(material_field, ""))
        assert material is not None, f"CraftingMaterials.csv has no row {names.get(material_field)}"
        assert row["Materials"] == material["MaterialName"], (row["Materials"], material["MaterialName"])
        assert row["CRImpact"] == f"{constant(residue):g} CR", (row["CRImpact"], constant(residue))
        assert float(row["BaseDays"]) == constant(days), (row["BaseDays"], constant(days))
        assert float(row["BaseGold"]) == constant("GoldPerCraftDay") * constant(days), row["BaseGold"]


def test_each_rule_reads_its_row_and_refuses_for_its_material_before_its_gold() -> None:
    text = code(FORGE_CPP)
    for signature, row, material, gold in (
            ("FCataclysmForgeSocketWork UCataclysmForge::AddSocketOf(", "AddSocketOperationRow",
             "Adding a socket takes a Shattered Core", 'GoldRefusal(TEXT("Adding a socket")'),
            ("FCataclysmForgeSocketWork UCataclysmForge::ChangeSocketTypeOf(", "ChangeSocketTypeOperationRow",
             "Changing a socket's type takes a Focusing Lens", "GoldRefusal(TEXT(\"Changing a socket's type\")")):
        rule = body(text, signature)
        assert f"BaseGoldOf({row}," in rule, f"{signature} does not read its row's Base Gold"
        assert "DaysFor(" in rule and "GoldFor(" in rule, f"{signature} does not use DaysFor and GoldFor"
        assert rule.find(material) < rule.find(gold), f"{signature} asks for gold before its material"
        assert rule.rfind("Out.Refusal") == rule.find("Out.Refusal = " + gold), f"{signature}: gold is not the last refusal"
    change = body(text, "FCataclysmForgeSocketWork UCataclysmForge::ChangeSocketTypeOf(")
    assert 'TEXT("Unsocket the gem first.")' in change, "a socket holding a gem can be changed (Q4)"
    assert 'TEXT("That socket fits any gem already.")' in change, "a socket for any gem can be changed (Q5)"
    add = body(text, "FCataclysmForgeSocketWork UCataclysmForge::AddSocketOf(")
    assert "Item.Sockets.Num() >= MaxSockets" in add, "an addition is not held to the slot's maximum (Q3)"


# --------------------------------------------------------------------------
# 3 and 4: the seed
# --------------------------------------------------------------------------

def saved_fields(struct_name: str) -> list[str]:
    text = ITEM_H.read_text(encoding="utf-8")
    start = text.index(f"struct CATACLYSM_API {struct_name}")
    block = text[start:text.index("\n};", start)]
    return re.findall(r"UPROPERTY\([^)]*SaveGame[^)]*\)\s*[\w<>:, ]+?\s(\w+)(?:\s*=[^;]+)?;", block)


def test_the_seed_text_names_every_field_the_item_saves() -> None:
    text = body(code(FORGE_CPP), "FString UCataclysmForge::ItemSeedText(")
    for struct_name, holder in (("FCataclysmItem", "Item."), ("FCataclysmRolledAffix", "Affix."),
                                ("FCataclysmRolledEnchantment", "Enchantment."), ("FCataclysmSocket", "Socket.")):
        fields = saved_fields(struct_name)
        assert fields, f"no SaveGame field found in {struct_name}; the check reads the wrong file"
        missing = [field for field in fields if f"{holder}{field}" not in text]
        assert not missing, f"ItemSeedText leaves out {struct_name}'s saved {missing}, so items differing there roll alike"


def test_the_python_copy_gives_the_figures_fs2_asserts() -> None:
    test = TEST_CPP.read_text(encoding="utf-8")
    a, b = seed_helm(0.5), seed_helm(0.625)
    literal = re.search(r'FString\(TEXT\("(Base=Head_Helm;[^"]*)"\s*"([^"]*)"\)\)', test)
    assert literal, "FS2 no longer asserts the helm's seed text"
    assert literal.group(1) + literal.group(2) == seed_text(a), (literal.group(0), seed_text(a))
    assert f"UCataclysmForge::AddedSocketSeed(A, Mask, Day), {added_seed(a, 12345, 7)})" in test, added_seed(a, 12345, 7)
    assert roll_type(added_seed(a, 12345, 7)) == "Attack"
    assert roll_type(added_seed(b, 12345, 7)) == "Utility", "the fixed pair no longer differs"
    counts = {kind: 0 for kind in TYPES}
    for day in range(1, 3001):
        counts[roll_type(added_seed(a, 12345, day))] += 1
    assert counts == {"Attack": 1075, "Defense": 966, "Utility": 959}, counts
    for kind, count in counts.items():
        assert f"{kind} {count}" in test, f"FS2 does not assert {kind} {count}"


# --------------------------------------------------------------------------
# 5 and 6: the craft and the screen
# --------------------------------------------------------------------------

def test_the_added_socket_is_rolled_before_the_days_pass() -> None:
    work = body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoSocketWork(")
    rolled = work.find("UCataclysmForge::AddedSocketType(Before, Run->WalkSeedMask, Run->Day())")
    advanced = work.find("Run->AdvanceDays(Plan.Days)")
    assert rolled != -1, "the added socket's type is not rolled from the item before the craft, the mask and the day"
    assert advanced != -1 and rolled < advanced, "the type is rolled after the days pass, so a reload rolls another"


def test_the_screen_offers_no_change_for_a_full_socket_or_one_for_any_gem() -> None:
    offers = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeSocketWorkAction> UCataclysmForgeLayout::SocketWorkActionsFor(")
    assert "!Socket.Gem.IsNone() || Socket.Type == ECataclysmSocketType::Any" in offers, (
        "the screen offers a change for a socket holding a gem or one for any gem")
    assert "Item.Sockets.Num() < MaxSockets" in offers, "the screen offers an addition at the maximum"
