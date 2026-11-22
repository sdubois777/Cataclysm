"""The forge's three crafts that change one affix (issue #47, slice F1), ruled by the coordinating session on 2026-11-22.

The Crafting sheet's rows, `game/Data/ForgeOperations.csv`: Reroll Affix Value, a Corrupted Mote, 2 CR, 0 base days,
Base Gold 250; Perfect Roll Attempt, a Primal Spark, 50 CR, 5 base days, Base Gold 1,250; Upgrade Affix Tier, a Potency
Crystal, "10 CR*(Current affix tier)", "1(current affix tier)" base days, Base Gold 250. Each acts on one affix the
player chooses (Q1, Q7). A reroll draws the roll again evenly, and it can fall (Q2); a perfecting makes the roll 1,
certainly (Q6); an upgrade raises the tier by one, to 7, and keeps the roll (Q12), at the sheet's flat Base Gold (Q13).

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the sheet's three rows hold the figures the forge's constants state, and their materials are crafting materials --
     without it a changed sheet and an unchanged constant charge two prices;
  2. the rule reads each row's Base Gold, falls back on the constant the row mirrors, and refuses for the material
     before the gold, gold last, as every craft;
  3. a perfecting sets the roll to 1 and an upgrade raises the tier by one, refused at 7, and leaves the roll alone;
  4. the Python copy of the reroll's seed and draw gives the exact figures FA2 asserts, so the C++ and this copy agree;
  5. the craft draws a reroll's roll from the run's mask and day BEFORE the craft's days pass, so a reload draws the
     same roll, and it changes the affix's tier and roll and nothing else of it;
  6. the screen offers no perfecting of a roll at the top and no raise of a tier 7 affix.
"""
from __future__ import annotations

import csv
import pathlib
import re
import sys
import zlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

# THE SOCKET CRAFTS' COPY OF THE SEED MIX, THE COMMENT STRIPPER AND THE CONSTANT READER, one copy for both slices.
import test_the_forge_adds_and_changes_sockets as sockets  # noqa: E402

DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"
TEST_CPP = SOURCE / "Tests" / "CataclysmForgeAffixesTests.cpp"

M32 = 0xFFFFFFFF
code = sockets.code
body = sockets.body
constant = sockets.constant


def rows(name: str) -> dict[str, dict]:
    with (DATA / name).open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def named_rows() -> dict[str, str]:
    return dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+Row) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))


# --------------------------------------------------------------------------
# The Python copy of UCataclysmForge::RerolledAffixRoll
# --------------------------------------------------------------------------

def seed_text(item: dict) -> str:
    """`UCataclysmForge::ItemSeedText` for an item with no damage types, no enchantments and no sockets."""
    affixes = "|".join(f"{a},{t},{sockets.f32(r):.6f},{'+'.join(d)}" for a, t, r, d in item["affixes"])
    return (f"Base={item['base']};Level={item['level']};Types=;Affixes={affixes};Enchantments=0:;Sockets=;"
            f"Residue={sockets.f32(item['residue']):.6f};Kills=0;Made=0")


def rerolled_seed(item: dict, affix: int, mask: int, day: int) -> int:
    """`UCataclysmForge::RerolledAffixSeed`: an added socket's two mixes, this craft's salt and the affix's place."""
    crc = zlib.crc32(seed_text(item).encode("utf-32-le"))
    signed = crc - (1 << 32) if crc >= 1 << 31 else crc
    return sockets.mix(sockets.mix(mask, int(constant("RerolledAffixSalt")), day), signed, affix)


def rerolled_roll(item: dict, affix: int, mask: int, day: int) -> float:
    """`FRandomStream::FRand` on a fresh stream: one mutation, then the top 23 bits as a fraction below 1."""
    state = ((rerolled_seed(item, affix, mask, day) & M32) * 196314165 + 907633515) & M32
    return (state >> 9) / float(1 << 23)


def helm(first_roll: float) -> dict:
    """FA2's `Helm`: flat evasion at tier 2 and flat armor at tier 4 rolled 0.25, gear level 3, residue 40."""
    return {"base": "Head_Helm", "level": 3, "residue": 40.0,
            "affixes": [("Stat_Flat_evasion", 2, first_roll, []), ("Stat_Flat_armor", 4, 0.25, [])]}


# --------------------------------------------------------------------------
# 1 and 2: the sheet's rows and the rule
# --------------------------------------------------------------------------

def test_the_sheets_three_rows_hold_the_forges_figures() -> None:
    operations = rows("ForgeOperations.csv")
    materials = rows("CraftingMaterials.csv")
    names = named_rows()
    per_day = constant("GoldPerCraftDay")
    for row_field, material_field, impact, days, gold in (
            ("RerollAffixValueOperationRow", "CorruptedMoteRow", f"{constant('RerollAffixValueResidue'):g} CR",
             f"{constant('RerollAffixValueBaseDays'):g}", per_day * max(1.0, constant("RerollAffixValueBaseDays"))),
            ("PerfectRollOperationRow", "PrimalSparkRow", f"{constant('PerfectRollResidue'):g} CR",
             f"{constant('PerfectRollBaseDays'):g}", per_day * constant("PerfectRollBaseDays")),
            ("UpgradeAffixTierOperationRow", "PotencyCrystalRow",
             f"{constant('UpgradeAffixTierResiduePerTier'):g} CR*(Current affix tier)", "1(current affix tier)",
             per_day)):
        row = operations.get(names.get(row_field, ""))
        assert row is not None, f"ForgeOperations.csv has no row {names.get(row_field)}, which {row_field} names"
        material = materials.get(names.get(material_field, ""))
        assert material is not None, f"CraftingMaterials.csv has no row {names.get(material_field)}"
        assert row["Materials"] == material["MaterialName"], (row["Materials"], material["MaterialName"])
        assert row["CRImpact"] == impact, (row_field, row["CRImpact"], impact)
        assert row["BaseDays"] == days, (row_field, row["BaseDays"], days)
        assert float(row["BaseGold"]) == gold, (row_field, row["BaseGold"], gold)


def test_the_rule_reads_each_row_and_refuses_for_the_material_before_the_gold() -> None:
    rule = body(code(FORGE_CPP), "FCataclysmForgeAffixWork UCataclysmForge::AffixWorkOf(")
    for read in ("BaseGoldOf(PerfectRollOperationRow, BaseGoldFor(PerfectRollBaseDays))",
                 "BaseGoldOf(UpgradeAffixTierOperationRow, GoldPerCraftDay)",
                 "BaseGoldOf(RerollAffixValueOperationRow, BaseGoldFor(RerollAffixValueBaseDays))",
                 "Out.Days = DaysFor(Item.Residue, BaseDays);", "Out.Gold = GoldFor(BaseGold, Item.Residue, DifficultyTier);"):
        assert read in rule, f"AffixWorkOf no longer reads {read}"
    material = rule.find("if (Out.MaterialHeld < 1)")
    gold = rule.find("Out.Refusal = GoldRefusal(Doing, Out.Gold, Out.GoldHeld);")
    assert -1 not in (material, gold) and material < gold, "AffixWorkOf asks for gold before its material"
    assert rule.rfind("Out.Refusal") == gold, "gold is not the last refusal"


# --------------------------------------------------------------------------
# 3: what each craft does to the affix
# --------------------------------------------------------------------------

def test_a_perfecting_makes_the_roll_one_and_an_upgrade_raises_the_tier_and_keeps_the_roll() -> None:
    rule = body(code(FORGE_CPP), "FCataclysmForgeAffixWork UCataclysmForge::AffixWorkOf(")
    perfect = rule[rule.find("case ECataclysmForgeAffixCraft::PerfectRoll:"):rule.find("case ECataclysmForgeAffixCraft::UpgradeTier:")]
    upgrade = rule[rule.find("case ECataclysmForgeAffixCraft::UpgradeTier:"):rule.find("case ECataclysmForgeAffixCraft::RerollValue:")]
    assert "Out.ToRoll = 1.0f;" in perfect, "a perfecting no longer makes the roll the top (Q6, certain)"
    assert "Out.ToTier = Rolled.Tier + 1;" in upgrade, "an upgrade no longer raises the tier by one"
    assert "ToRoll" not in upgrade, "an upgrade changes the roll; Q12 keeps it"
    assert "UpgradeAffixTierResiduePerTier * static_cast<float>(Rolled.Tier)" in upgrade and "BaseDays = Rolled.Tier;" in upgrade, (
        "an upgrade's residue and days are no longer the affix's tier before it")
    assert "Rolled.Tier >= UCataclysmItemValues::MaxAffixTier" in rule, "an upgrade is no longer refused at tier 7"
    assert "Rolled.Roll >= 1.0f" in rule, "a perfecting of a roll at the top is no longer refused"


# --------------------------------------------------------------------------
# 4: the draw
# --------------------------------------------------------------------------

def test_the_python_copy_gives_the_figures_fa2_asserts() -> None:
    test = TEST_CPP.read_text(encoding="utf-8")
    a, b = helm(0.5), helm(0.625)
    literal = re.search(r'FString\(TEXT\("(Base=Head_Helm;[^"]*)"\s*"([^"]*)"\)\)', test)
    assert literal, "FA2 no longer asserts the helm's seed text"
    assert literal.group(1) + literal.group(2) == seed_text(a), (literal.group(0), seed_text(a))
    assert f"RerolledAffixSeed(A, 0, Mask, Day), {rerolled_seed(a, 0, 12345, 7)})" in test, rerolled_seed(a, 0, 12345, 7)
    assert f"RerolledAffixSeed(A, 1, Mask, Day), {rerolled_seed(a, 1, 12345, 7)})" in test, rerolled_seed(a, 1, 12345, 7)
    for item, affix in ((a, 0), (a, 1), (b, 0)):
        roll = rerolled_roll(item, affix, 12345, 7)
        assert f"{roll!r}f" in test, f"FA2 does not assert the roll {roll!r} for affix {affix + 1}"
    quarters = [0, 0, 0, 0]
    for day in range(1, 3001):
        quarters[min(3, int(rerolled_roll(a, 0, 12345, day) * 4))] += 1
    assert quarters == [750, 703, 746, 801], quarters
    for count in quarters:
        assert f"Quarters[{quarters.index(count)}], {count});" in test, f"FA2 does not assert {count}"
    assert all(abs(count - 750) <= 75 for count in quarters), f"the draw is not even: {quarters}"


# --------------------------------------------------------------------------
# 5 and 6: the craft and the screen
# --------------------------------------------------------------------------

def test_a_reroll_is_drawn_before_the_days_pass_and_only_the_affix_changes() -> None:
    craft = body(code(CRAFT_CPP), "ECataclysmForgeResult UCataclysmForgeCraft::CraftAffix(")
    drawn = craft.find("UCataclysmForge::RerolledAffixRoll(Before, Plan.Affix, Run.WalkSeedMask, Run.Day())")
    # SLICE F5: THE DAYS PASS IN `DoItemCraft`, after the change it is handed, which this draw is inside;
    # `test_every_item_craft_goes_through_one_sequence.py` holds that order.
    handed = craft.find("DoItemCraft(")
    assert drawn != -1, "a reroll's roll is not drawn from the item before the craft, the mask and the day"
    assert -1 < handed < drawn, "the roll is not drawn inside the change DoItemCraft makes before the days pass"
    changed = re.findall(r"Affixed\.(\w+) = ", craft)
    assert sorted(changed) == ["Roll", "Tier"], f"the craft changes the affix's {changed}; only its tier and roll"


def test_the_screen_offers_no_perfecting_at_the_top_and_no_raise_at_tier_seven() -> None:
    offers = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeAffixAction> UCataclysmForgeLayout::AffixActionsFor(")
    perfect, raise_ = offers.find("if (Rolled.Roll < 1.0f)"), offers.find("if (Rolled.Tier < UCataclysmItemValues::MaxAffixTier)")
    assert perfect != -1 and perfect < offers.find("ECataclysmForgeAffixCraft::PerfectRoll"), (
        "the screen offers a perfecting of a roll at the top")
    assert raise_ != -1 and raise_ < offers.find("ECataclysmForgeAffixCraft::UpgradeTier"), (
        "the screen offers a raise of a tier 7 affix")
