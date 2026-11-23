"""The forge charges the Crafting sheet's costs: gold on every built craft, days by level, one combine price.

WHY THIS EXISTS. Issue #2830, slice G7 of the gems work (#46). The owner, 2026-11-21: "I have costs set in the
crafting excel sheet. Something like that should be implemented"; "+0 to +10 takes 55 days per item (1+2+...+10)";
and for combining, "Gold only (Recommended)". The coordinating session, under the owner's delegation: 250 gold a base
day at tier 1, the sheet's multiplier `(CR / 50) + 1` not rounded, and a combine at 50 gold for the lowest rung,
doubled for each rung above, for stones and gems alike. `Cataclysm.ForgeCosts.`
(`game/Source/Cataclysm/Tests/CataclysmForgeCostsTests.cpp`) tests the figures; this checks the shape:

  1. the constants are the ruled ones, and the multiplier is the sheet's;
  2. the gold formula does not round its multiplier, and reads the tier's gold scale;
  3. both combines read one price, and it doubles by rung;
  4. every built craft's rule prices itself from its base days and refuses too little gold;
  5. every craft pays its gold before taking anything else, gives it back on every later failure, and writes the
     account record when the account paid;
  6. the tier a price is read at is the run's, as a city upgrade's.

THE BASE GOLD IS THE SHEET'S since the workbook stage: the Crafting sheet's operations table is generated into
`game/Data/ForgeOperations.csv`, and the forge reads each built operation's Base Gold from it
(`UCataclysmForge::BaseGoldOf`). The constants it falls back on when the table is missing must equal the sheet's.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
FORGE_H = SOURCE / "Items" / "CataclysmForge.h"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
CRAFT_CPP = SOURCE / "Player" / "CataclysmForgeCraft.cpp"
MATERIALS = REPO_ROOT / "game" / "Data" / "CraftingMaterials.csv"
OPERATIONS = REPO_ROOT / "game" / "Data" / "ForgeOperations.csv"


def code(path: pathlib.Path) -> str:
    """The file with each `//` comment taken off its line, so a mention in a comment cannot satisfy a check."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where}: the body of {signature!r} never closes")


def constant(name: str) -> float:
    found = re.search(rf"static constexpr (?:int32|float) {name} = (\d+(?:\.\d+)?)f?;", code(FORGE_H))
    assert found, f"{FORGE_H.name} declares no number {name}"
    return float(found.group(1))


def rule(signature: str) -> str:
    return squeeze(body(code(FORGE_CPP), signature, FORGE_CPP.name))


def test_the_constants_are_the_ruled_ones_and_the_multiplier_is_the_sheets() -> None:
    assert constant("GoldPerCraftDay") == 250, "250 gold a base day at tier 1, ruled 2026-11-21"
    assert constant("CombineGoldAtTheLowestRung") == 50, "a combine at the lowest rung costs 50, ruled 2026-11-21"
    with MATERIALS.open(encoding="utf-8", newline="") as handle:
        formulas = {row["CRMetric"]: row["Formula"] for row in csv.DictReader(handle)}
    assert formulas.get("Gold Cost Multiplier") == "(CR / 50) + 1", (
        f"the Crafting sheet's gold multiplier is now {formulas.get('Gold Cost Multiplier')!r}; ResiduePerGoldStep "
        "was read from '(CR / 50) + 1'")
    assert constant("ResiduePerGoldStep") == 50, "the sheet's multiplier divides the residue by 50"


def test_the_gold_formula_does_not_round_its_multiplier_and_reads_the_tiers_scale() -> None:
    gold = rule("int64 UCataclysmForge::GoldFor(")
    assert "const double Multiplier = static_cast<double>(FMath::Max(0.0f, Residue)) / ResiduePerGoldStep + 1.0;" in gold, (
        "GoldFor's multiplier is no longer the sheet's (CR / 50) + 1, unrounded")
    assert "UCataclysmDropRoll::GoldScaleFor(DifficultyTier)" in gold, "GoldFor does not read the tier's gold scale"
    multiplier = gold[gold.find("const double Multiplier"):gold.find(";", gold.find("const double Multiplier"))]
    assert not re.search(r"Round|Floor|Ceil", multiplier), "the multiplier is rounded; ruled 2026-11-21 that it is not"
    days = rule("int32 UCataclysmForge::BaseGoldFor(")
    assert "return GoldPerCraftDay * FMath::Max(1, BaseDays);" in days, (
        "an operation's base gold is no longer GoldPerCraftDay a base day, with one day's price at the least")


def test_both_combines_read_one_price_and_it_doubles_by_rung() -> None:
    price = rule("int64 UCataclysmForge::CombineGoldFor(")
    assert "BaseGoldOf(CombineOperationRow, CombineGoldAtTheLowestRung)) * FMath::Pow(2.0, FMath::Clamp(Rung, 0, 9))" in price, (
        "a combine's price no longer doubles for each rung")
    assert "UCataclysmDropRoll::GoldScaleFor(DifficultyTier)" in price, "a combine's price does not read the tier's scale"
    assert "Out.Gold = CombineGoldFor(Level - 1, DifficultyTier);" in rule(
        "FCataclysmForgeCombine UCataclysmForge::CombineOf("), "the stone combine does not read CombineGoldFor"
    assert "Out.Gold = CombineGoldFor(static_cast<int32>(Rarity), DifficultyTier);" in rule(
        "FCataclysmForgeGemCombine UCataclysmForge::GemCombineOf("), "the gem combine does not read CombineGoldFor"


@pytest.mark.parametrize("signature, priced", [
    ("FCataclysmForgeUpgrade UCataclysmForge::UpgradeOf(", "Out.Gold = GoldFor(BaseGoldOf(UpgradeOperationRow, "
                                                           "GoldPerCraftDay) * Out.ToLevel, Item.Residue, DifficultyTier);"),
    ("FCataclysmForgeSocketing UCataclysmForge::SocketGemOf(", "Out.Gold = GoldFor(BaseGoldOf(SocketGemOperationRow, "
                                                               "BaseGoldFor(SocketGemBaseDays)), Item.Residue, "
                                                               "DifficultyTier);"),
    ("FCataclysmForgeSocketing UCataclysmForge::UnsocketGemOf(", "Out.Gold = GoldFor(BaseGoldOf(UnsocketGemOperationRow, "
                                                                 "BaseGoldFor(UnsocketGemBaseDays)), Item.Residue, "
                                                                 "DifficultyTier);"),
    ("FCataclysmForgeCombine UCataclysmForge::CombineOf(", "Out.Gold = CombineGoldFor(Level - 1, DifficultyTier);"),
    ("FCataclysmForgeGemCombine UCataclysmForge::GemCombineOf(",
     "Out.Gold = CombineGoldFor(static_cast<int32>(Rarity), DifficultyTier);"),
    ("FCataclysmForgeAffixWork UCataclysmForge::AffixWorkOf(", "Out.Gold = GoldFor(BaseGold, Item.Residue, DifficultyTier);"),
    # ISSUE #47 SLICE F3: Imprint Enchantment and Change Enchantment Negative.
    ("FCataclysmForgeEnchantmentWork UCataclysmForge::ImprintOf(",
     "Out.Gold = GoldFor(BaseGoldOf(ImprintOperationRow, BaseGoldFor(ImprintBaseDays)), Item.Residue, DifficultyTier);"),
    ("FCataclysmForgeEnchantmentWork UCataclysmForge::ChangeNegativeOf(",
     "Out.Gold = GoldFor(BaseGoldOf(ChangeNegativeOperationRow, BaseGoldFor(ChangeNegativeBaseDays)), Item.Residue, "
     "DifficultyTier);"),
    ("FCataclysmForgeAffixChange UCataclysmForge::DeterministicAffixOf(",
     "Out.Gold = GoldFor(BaseGoldOf(DeterministicAffixOperationRow, GoldPerCraftDay), Item.Residue, DifficultyTier);"),
    ("FCataclysmForgeAffixChange UCataclysmForge::AffixShiftOf(",
     "Out.Gold = GoldFor(BaseGoldOf(AffixShiftingOperationRow, BaseGoldFor(AffixShiftingBaseDays)), Item.Residue, "
     "DifficultyTier);"),
    ("FCataclysmForgeDamageTypeWork UCataclysmForge::DamageTypeWorkOf(",
     "Out.Gold = GoldFor(BaseGoldOf(InfuseSpecificDamageTypeOperationRow, BaseGoldFor(InfuseSpecificDamageTypeBaseDays)), "
     "Item.Residue, DifficultyTier);"),
])
def test_every_built_craft_prices_itself_and_refuses_too_little_gold(signature: str, priced: str) -> None:
    text = rule(signature)
    assert priced in text, f"{signature} no longer prices itself as ruled: {priced}"
    refusal = text.find("if (Out.GoldHeld < Out.Gold) {")
    assert refusal != -1 and "GoldRefusal(" in text[refusal:refusal + 300], f"{signature} does not refuse too little gold"
    assert refusal < text.find("Out.bPossible = true;"), f"{signature} says it is possible before it asks the gold"


def test_an_upgrades_days_are_the_level_it_reaches() -> None:
    assert "Out.Days = DaysFor(Item.Residue, Out.ToLevel);" in rule("FCataclysmForgeUpgrade UCataclysmForge::UpgradeOf("), (
        "an upgrade's base days are no longer the level it reaches: the owner's '+0 to +10 takes 55 days'")
    assert "BaseDaysPerLevel =" not in code(FORGE_H), "the one-day-a-level constant is back"


@pytest.mark.parametrize("signature", [
    "ECataclysmForgeResult UCataclysmForgeCraft::Upgrade(",
    "bool UCataclysmForgeCraft::Combine(",
    "bool UCataclysmForgeCraft::CombineGems(",
    "ECataclysmForgeResult UCataclysmForgeCraft::DoSocketing(",
    "ECataclysmForgeResult UCataclysmForgeCraft::CraftAffix(",
    "ECataclysmForgeResult UCataclysmForgeCraft::DoEnchantmentWork(",
    "ECataclysmForgeResult UCataclysmForgeCraft::DoAffixChange(",
    "ECataclysmForgeResult UCataclysmForgeCraft::CraftDamageType(",
])
def test_every_craft_pays_first_gives_back_on_failure_and_saves_the_account(signature: str) -> None:
    text = squeeze(body(code(CRAFT_CPP), signature, CRAFT_CPP.name))
    paid = text.find("if (!PayForTheCraft(WorldContext, Plan.Gold, bGoldFromTheCharacter)) {")
    assert paid != -1, f"{signature} does not pay its gold"
    for taken in ("RemoveMaterial(", "RemoveGems(", "AdvanceDays("):
        at = text.find(taken)
        assert at == -1 or at > paid, f"{signature} takes {taken} before it pays, so a refused payment would leave it gone"
    # AFTER THE PAYMENT'S OWN REFUSAL, every refusal gives the gold back first.
    after = text[text.find("}", paid) + 1:]
    exits = [match.start() for match in re.finditer(r"return (false|ECataclysmForgeResult::Refused);", after)]
    previous = 0
    for at in exits:
        assert "GiveTheGoldBack(WorldContext, Plan.Gold);" in after[previous:at], (
            f"{signature} refuses after taking the gold without giving it back: {after[previous:at][-160:]}")
        previous = at
    assert exits, f"{signature} has no refusal after its payment, which this check expected to see"
    assert "SaveWhereTheGoldCameFrom(" in text, f"{signature} does not write the account record when the account paid"


def test_the_sheets_base_gold_is_what_the_forge_falls_back_on() -> None:
    # THE ROWS THE FORGE READS, AND THE CONSTANT EACH FALLS BACK ON, must agree, or a missing asset changes a price.
    with OPERATIONS.open(encoding="utf-8", newline="") as handle:
        rows = {row["Name"]: row for row in csv.DictReader(handle)}
    named = dict(re.findall(r'const TCHAR\* UCataclysmForge::(\w+OperationRow) = TEXT\("([^"]+)"\);', code(FORGE_CPP)))
    expected = {"UpgradeOperationRow": constant("GoldPerCraftDay"), "SocketGemOperationRow": constant("GoldPerCraftDay"),
                "UnsocketGemOperationRow": constant("GoldPerCraftDay"),
                "CombineOperationRow": constant("CombineGoldAtTheLowestRung"),
                # ISSUE #46 SLICE G9: each falls back on `BaseGoldFor` of its base days, 3 and 2.
                "AddSocketOperationRow": constant("GoldPerCraftDay") * constant("AddSocketBaseDays"),
                "ChangeSocketTypeOperationRow": constant("GoldPerCraftDay") * constant("ChangeSocketTypeBaseDays"),
                # ISSUE #47 SLICE F1: a reroll's 0 base days are priced as one, a perfecting's 5, and an upgrade of an
                # affix's tier at one day's price, the sheet's flat 250.
                "RerollAffixValueOperationRow": constant("GoldPerCraftDay"),
                "PerfectRollOperationRow": constant("GoldPerCraftDay") * constant("PerfectRollBaseDays"),
                "UpgradeAffixTierOperationRow": constant("GoldPerCraftDay"),
                # ISSUE #47 SLICE F2: an addition at one day's price, the sheet's flat 250; a shift's 1 base day.
                "DeterministicAffixOperationRow": constant("GoldPerCraftDay"),
                "AffixShiftingOperationRow": constant("GoldPerCraftDay") * constant("AffixShiftingBaseDays"),
                # ISSUE #47 SLICE F4: each at `BaseGoldFor` of its base days, 1, 5 and 0 (one day's price).
                "InfuseRandomDamageTypeOperationRow": constant("GoldPerCraftDay"),
                "InfuseSpecificDamageTypeOperationRow": constant("GoldPerCraftDay")
                * constant("InfuseSpecificDamageTypeBaseDays"),
                "ChangeDamageTypeOperationRow": constant("GoldPerCraftDay"),
                # ISSUE #47 SLICE F3: 2 and 7 base days, so 500 and 1,750.
                "ImprintOperationRow": constant("GoldPerCraftDay") * constant("ImprintBaseDays"),
                "ChangeNegativeOperationRow": constant("GoldPerCraftDay") * constant("ChangeNegativeBaseDays")}
    assert set(named) == set(expected), f"the forge names the operation rows {sorted(named)}"
    for field, figure in expected.items():
        row = rows.get(named[field])
        assert row is not None, f"ForgeOperations.csv has no row {named[field]}, which UCataclysmForge::{field} names"
        assert float(row["BaseGold"]) == figure, (
            f"{named[field]} costs {row['BaseGold']} in the sheet and {figure:g} in the constant the forge falls back on")


def test_a_price_is_read_at_the_runs_tier_as_a_city_upgrades_is() -> None:
    tier = squeeze(body(code(CRAFT_CPP), "int32 GoldTierOf(", CRAFT_CPP.name))
    assert "return Run != nullptr ? Run->DifficultyTier : ACataclysmGameMode::LowestDifficultyTier;" in tier, (
        "a forge price is no longer read at the run's difficulty tier")
