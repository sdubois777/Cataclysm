"""The forge's Deterministic Affix and Affix Shifting (issue #47, slice F2), ruled by the coordinating session on 2026-11-22.

The Crafting sheet's rows, `game/Data/ForgeOperations.csv`: Deterministic Affix, Schematic Fragments, "5 CR per tier of
affix", "1 day per tier of affix. (Max of 8)", Base Gold 250; Affix Shifting (Stat Type), a Crystal of Instability,
15 CR, 1 base day, Base Gold 250. An addition takes the affix and tier chosen, T1 to T7 (Q8), among those a drop could
put on the slot, prefix or suffix (Q10), never one of a stat group the piece holds; its roll is drawn in the tier
(Q11); it needs a free slot of the four and promotes the piece. A shift draws another affix from the same position's
pool, never itself and never one of a group the piece holds (Q3), and keeps the tier and the roll (Q4); a resistance's
damage types are drawn again (Q5).

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the sheet's two rows hold the figures the forge's constants state -- without it a changed sheet and an unchanged
     constant charge two prices;
  2. each rule reads its row's Base Gold and refuses for the material before the gold, gold last;
  3. an addition needs a free slot of the four and promotes the piece by the definition of rarity;
  4. a shift keeps the tier and the roll, never draws the affix it was, and never one of a group the piece holds;
  5. the Python copy of the pool and the addition's draw gives the exact figures FC2 asserts;
  6. the craft writes the item the plan drew, drawing nothing again, and passes the days after;
  7. the screen offers the chooser only for a piece with a free slot.
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

# THE SOCKET CRAFTS' COPY OF THE SEED MIX, THE COMMENT STRIPPER AND THE CONSTANT READER, one copy for every slice.
import test_the_forge_adds_and_changes_sockets as sockets  # noqa: E402

DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"
ITEM_CPP = SOURCE / "Items" / "CataclysmItem.cpp"
TEST_CPP = SOURCE / "Tests" / "CataclysmForgeAffixChangeTests.cpp"

M32 = 0xFFFFFFFF
code = sockets.code
body = sockets.body
constant = sockets.constant


def rows(name: str) -> list[dict]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


AFFIXES = rows("Affixes.csv")
BY_KEY = {row["Name"]: row for row in AFFIXES}
BY_NAME = {row["AffixName"]: row for row in AFFIXES}


def damage_types() -> list[str]:
    """`UCataclysmItemModifiers::DamageTypeNames`, in its order, read from the C++."""
    text = code(ITEM_CPP)
    block = text[text.index("UCataclysmItemModifiers::DamageTypeNames()"):]
    block = block[block.index("{"):block.index("};")]
    found = re.findall(r'TEXT\("(\w+)"\)', block)
    assert len(found) == 8, found
    return found


TYPES = damage_types()


# --------------------------------------------------------------------------
# The Python copy of the pool and the addition's draw
# --------------------------------------------------------------------------

def groups(row: dict, types: list[str]) -> set[str]:
    """`UCataclysmDropRoll::GroupsOf`."""
    kind = row["AffixKind"].lower()
    if kind == "resistance":
        return {f"resistance_{t.lower()}.flat" for t in (types or TYPES)}
    if kind == "ailment":
        return {f"ailment.{row['Ailment']}"}
    if kind == "hybrid":
        return {f"{BY_NAME[p]['Stat']}.{BY_NAME[p]['ValueKind']}" for p in (row["HybridPart1"], row["HybridPart2"])
                if p in BY_NAME}
    return {f"{row['Stat']}.{row['ValueKind']}"}


def chooses_types(row: dict) -> bool:
    return row["AffixKind"].lower() == "resistance" and 0 < int(row["Breadth"]) < len(TYPES)


def held(item: dict) -> set[str]:
    out: set[str] = set()
    for affix, _tier, _roll, types in item["affixes"]:
        out |= groups(BY_KEY[affix], types)
    return out


def addable(item: dict, slot: str) -> list[str]:
    """`UCataclysmForge::AddableAffixesFor`, in the table's order."""
    taken = held(item)
    out = []
    for row in AFFIXES:
        if slot not in [each.strip() for each in row["AllowedSlots"].split(",")]:
            continue
        if chooses_types(row):
            if len([t for t in TYPES if not groups(row, [t]) & taken]) >= int(row["Breadth"]):
                out.append(row["Name"])
        elif not groups(row, []) & taken:
            out.append(row["Name"])
    return out


def seed_text(item: dict) -> str:
    affixes = "|".join(f"{a},{t},{sockets.f32(r):.6f},{'+'.join(d)}" for a, t, r, d in item["affixes"])
    return (f"Base={item['base']};Level={item['level']};Types=;Affixes={affixes};Enchantments=0:;Sockets=;"
            f"Residue={sockets.f32(item['residue']):.6f};Kills=0;Made=0")


def craft_seed(item: dict, salt: int, index: int, mask: int, day: int) -> int:
    """`UCataclysmForge::AffixCraftSeed`."""
    crc = zlib.crc32(seed_text(item).encode("utf-32-le"))
    signed = crc - (1 << 32) if crc >= 1 << 31 else crc
    return sockets.mix(sockets.mix(mask, salt, day), signed, index)


class Stream:
    """`FRandomStream`: `GetFraction` and `RandRange`."""

    def __init__(self, seed: int) -> None:
        self.state = seed & M32

    def fraction(self) -> float:
        self.state = (self.state * 196314165 + 907633515) & M32
        return (self.state >> 9) / float(1 << 23)

    def rand_range(self, low: int, high: int) -> int:
        span = high - low + 1
        return low + int(struct.unpack("f", struct.pack("f", self.fraction() * float(span)))[0])


def addition(item: dict, row_key: str, seed: int) -> tuple[list[str], float]:
    """The draw `UCataclysmForge::DeterministicAffixOf` makes: a resistance's unheld types, then the roll."""
    stream = Stream(seed)
    row = BY_KEY[row_key]
    types: list[str] = []
    if chooses_types(row):
        taken = held(item)
        pool = [t for t in TYPES if not groups(row, [t]) & taken]
        for index in range(int(row["Breadth"])):
            swap = stream.rand_range(index, len(pool) - 1)
            pool[index], pool[swap] = pool[swap], pool[index]
            types.append(pool[index])
    return types, stream.fraction()


HELM = {"base": "Head_Helm", "level": 3, "residue": 40.0,
        "affixes": [("Stat_Flat_evasion", 2, 0.5, []), ("Stat_Flat_armor", 4, 0.25, [])]}
RESISTANT = {"base": "Head_Helm", "level": 3, "residue": 40.0,
             "affixes": [("Stat_Flat_evasion", 2, 0.5, []), ("Resistance_Single_resistance", 1, 0.5, ["War"])]}


# --------------------------------------------------------------------------
# 1 and 2: the sheet's rows and the rules
# --------------------------------------------------------------------------

def test_the_sheets_two_rows_hold_the_forges_figures() -> None:
    operations = {row["Name"]: row for row in rows("ForgeOperations.csv")}
    materials = {row["Name"]: row for row in rows("CraftingMaterials.csv")}
    names = dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+Row) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))
    per_day = constant("GoldPerCraftDay")
    for row_field, material_field, impact, days, gold in (
            ("DeterministicAffixOperationRow", "SchematicFragmentsRow",
             f"{constant('DeterministicAffixResiduePerTier'):g} CR per tier of affix",
             f"1 day per tier of affix. (Max of {constant('DeterministicAffixMaxBaseDays'):g})", per_day),
            ("AffixShiftingOperationRow", "CrystalOfInstabilityRow", f"{constant('AffixShiftingResidue'):g} CR",
             f"{constant('AffixShiftingBaseDays'):g}", per_day * constant("AffixShiftingBaseDays"))):
        row = operations.get(names.get(row_field, ""))
        assert row is not None, f"ForgeOperations.csv has no row {names.get(row_field)}, which {row_field} names"
        material = materials.get(names.get(material_field, ""))
        assert material is not None, f"CraftingMaterials.csv has no row {names.get(material_field)}"
        assert row["Materials"] == material["MaterialName"], (row["Materials"], material["MaterialName"])
        assert row["CRImpact"] == impact, (row_field, row["CRImpact"], impact)
        assert row["BaseDays"] == days, (row_field, row["BaseDays"], days)
        assert float(row["BaseGold"]) == gold, (row_field, row["BaseGold"], gold)


def test_each_rule_reads_its_row_and_refuses_for_its_material_before_its_gold() -> None:
    text = code(FORGE_CPP)
    for signature, read, material, gold in (
            ("FCataclysmForgeAffixChange UCataclysmForge::DeterministicAffixOf(",
             "BaseGoldOf(DeterministicAffixOperationRow, GoldPerCraftDay)",
             "Adding an affix takes Schematic Fragments", 'GoldRefusal(TEXT("Adding an affix")'),
            ("FCataclysmForgeAffixChange UCataclysmForge::AffixShiftOf(",
             "BaseGoldOf(AffixShiftingOperationRow, BaseGoldFor(AffixShiftingBaseDays))",
             "Shifting an affix takes a Crystal of Instability", 'GoldRefusal(TEXT("Shifting an affix")')):
        rule = body(text, signature)
        assert read in rule, f"{signature} does not read its row's Base Gold: {read}"
        assert "DaysFor(" in rule and "GoldFor(" in rule, f"{signature} does not use DaysFor and GoldFor"
        assert -1 < rule.find(material) < rule.find(gold), f"{signature} asks for gold before its material"
        assert rule.rfind("Out.Refusal") == rule.find("Out.Refusal = " + gold), f"{signature}: gold is not the last refusal"
    add = body(text, "FCataclysmForgeAffixChange UCataclysmForge::DeterministicAffixOf(")
    assert "DaysFor(Item.Residue, FMath::Min(Tier, DeterministicAffixMaxBaseDays))" in add, (
        "an addition's base days are no longer its tier, at most 8")
    assert "DeterministicAffixResiduePerTier * static_cast<float>(Tier)" in add, "an addition's residue is not 5 a tier"


# --------------------------------------------------------------------------
# 3 and 4: what each craft does
# --------------------------------------------------------------------------

def test_an_addition_needs_a_free_slot_and_promotes_the_piece() -> None:
    add = body(code(FORGE_CPP), "FCataclysmForgeAffixChange UCataclysmForge::DeterministicAffixOf(")
    assert "Item.Affixes.Num() + Item.EnchantmentCount >= UCataclysmItemValues::SlotsPerPiece" in add, (
        "an addition is not refused when the piece's four slots are full")
    assert "RarityOf(Item.EnchantmentCount, Item.Affixes.Num() + 1, Out.ToRarity)" in add, (
        "an addition's rarity is not one more affix in the same slots")
    assert "Tier < 1 || Tier > UCataclysmItemValues::MaxAffixTier" in add, "an addition's tier is not held to 1 to 7"
    assert "AddableAffixesFor(Item, AffixTable, BaseTable).Contains(AffixRow)" in add, (
        "an addition is not held to the affixes the piece can take")


def test_a_shift_keeps_tier_and_roll_and_draws_neither_itself_nor_a_held_group() -> None:
    shift = body(code(FORGE_CPP), "FCataclysmForgeAffixChange UCataclysmForge::AffixShiftOf(")
    assert "Out.Tier = Rolled.Tier;" in shift and "Out.Roll = Rolled.Roll;" in shift, "a shift no longer keeps tier and roll"
    assert len(re.findall(r"Out\.(Tier|Roll) = ", shift)) == 2, "a shift writes its tier or roll a second time"
    assert "Candidate.Affix == Rolled.Affix" in shift, "a shift can draw the affix it was"
    assert "UCataclysmDropRoll::CandidatesFor(AffixTable, Slot, FromRow->Position, Stream, Candidates)" in shift, (
        "a shift no longer draws from the same position's pool")
    assert "Groups.Intersect(Held).Num() == 0" in shift, "a shift can draw a stat group the piece holds"


# --------------------------------------------------------------------------
# 5: the draw
# --------------------------------------------------------------------------

def test_the_python_copy_gives_the_figures_fc2_asserts() -> None:
    test = TEST_CPP.read_text(encoding="utf-8")
    pool = addable(HELM, "Head")
    assert len(pool) == 27 and "TEXT(\"the helm can take 27 affixes, as computed\"), Addable.Num(), 27);" in test, len(pool)
    assert pool[0] == "Stat_Flat_maximum_health" and pool[-1] == "Stat_Flat_energy_shield_regeneration", (pool[0], pool[-1])
    assert len(addable(RESISTANT, "Head")) == 28 and "Bases).Num(), 28);" in test
    salt = int(constant("DeterministicAffixSalt"))
    seed = craft_seed(HELM, salt, 2, 12345, 7)
    assert f"Seed, {seed});" in test, seed
    types, roll = addition(HELM, "Stat_Flat_block_chance", seed)
    assert types == [] and f"{roll!r}f" in test, roll
    resistant_seed = craft_seed(RESISTANT, salt, 2, 12345, 7)
    assert f"ResistantSeed, {resistant_seed});" in test, resistant_seed
    types, roll = addition(RESISTANT, "Resistance_Two_resistances", resistant_seed)
    assert types == ["Famine", "Celestial"], types
    assert f"{roll!r}f" in test, roll
    for day in range(1, 301):
        drawn, _ = addition(RESISTANT, "Resistance_Two_resistances", craft_seed(RESISTANT, salt, 2, 12345, day))
        assert len(drawn) == 2 and "War" not in drawn, (day, drawn)


# --------------------------------------------------------------------------
# 6 and 7: the craft and the screen
# --------------------------------------------------------------------------

def test_the_craft_writes_what_the_plan_drew_and_passes_the_days_after() -> None:
    work = body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::DoAffixChange(")
    for redraw in ("AffixCraftSeed(", "DeterministicAffixOf(", "AffixShiftOf(", "FRandomStream"):
        assert redraw not in work, f"DoAffixChange draws again with {redraw}, so it can write what the plan did not ask"
    written = work.find("NewAffix.Affix = Plan.ToRow;")
    advanced = work.find("Run->AdvanceDays(Plan.Days)")
    assert -1 < written < advanced, "the item is not written from the plan before the days pass"
    for craft in ("ECataclysmForgeResult UCataclysmForgeCraft::AddAffix(", "ECataclysmForgeResult UCataclysmForgeCraft::ShiftAffix("):
        assert "PlanFor(WorldContext, Target" in body(code(CRAFT_CPP), craft), f"{craft} does not plan before it crafts"


def test_the_screen_offers_the_chooser_only_with_a_free_slot() -> None:
    offers = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeAffixChangeAction> UCataclysmForgeLayout::AddAffixActionsFor(")
    guard = offers.find("Item.Affixes.Num() + Item.EnchantmentCount >= UCataclysmItemValues::SlotsPerPiece")
    assert -1 < guard < offers.find("AddAffixButtonName()"), "the screen offers an addition to a piece with no free slot"
