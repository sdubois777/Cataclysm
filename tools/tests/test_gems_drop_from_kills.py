"""Gems drop from kills: the shape of the C++ that rolls them, lays them down, picks them up and saves them.

WHY THIS EXISTS. Issue #46, slice G5. The coordinating session ruled on 2026-11-21 that gems drop from kills at a rate
measured in the simulation (`sim/analyse_gem_drops.py`, `loot.GEM_DROPS_PER_GEAR_DROP`), with the gear's rarity roll
and an even choice among the 28 gems. `Cataclysm.GemDrops.` (`game/Source/Cataclysm/Tests/CataclysmGemDropsTests.cpp`)
tests what the code does; this checks what the Unreal tests cannot see:

  1. the rate the Unreal test holds the Enemy Drops table to is the simulation's;
  2. a kill's gems come from a stream of their own, seeded through `CataclysmSeedMix::Mix`, and draw nothing from the
     kill's stream, so every other drop a kill rolls is as it was;
  3. a creature's death rolls its gems after everything else it drops, only when it pays, marked when a rule raised it;
  4. a gem drop goes into the stack of its gem and rarity, and is a quantity collected without a click;
  5. the run record keeps a gem lying on the ground, both ways;
  6. a gem drop's border is its rarity's;
  7. a kill's gems are the column times its tier's share, three quarters of the tier above's, as the simulation's
     (issue #2853).
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
DROPPED_H = SOURCE / "Items" / "CataclysmDroppedItem.h"
DROPPED_CPP = SOURCE / "Items" / "CataclysmDroppedItem.cpp"
GEMS_CPP = SOURCE / "Items" / "CataclysmGems.cpp"
ENEMY_CPP = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"
RECORDS_H = SOURCE / "Save" / "CataclysmSaveRecords.h"
GATHER_CPP = SOURCE / "Save" / "CataclysmSaveGather.cpp"
APPLY_CPP = SOURCE / "Save" / "CataclysmSaveApply.cpp"
GEM_DROPS_TESTS = SOURCE / "Tests" / "CataclysmGemDropsTests.cpp"

sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import loot  # noqa: E402


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


def spawner() -> str:
    return squeeze(body(code(DROPPED_CPP), "int32 UCataclysmDropSpawner::SpawnGemsFor(", DROPPED_CPP.name))


def test_the_unreal_test_holds_the_table_to_the_simulations_rate() -> None:
    found = re.search(r"constexpr float GemsPerGearDrop = (\d*\.\d+)f;", GEM_DROPS_TESTS.read_text(encoding="utf-8"))
    assert found, f"{GEM_DROPS_TESTS.name} declares no GemsPerGearDrop"
    assert float(found.group(1)) == loot.GEM_DROPS_PER_GEAR_DROP, (
        f"{GEM_DROPS_TESTS.name} holds the Enemy Drops table to {found.group(1)} gems a gear drop; the simulation "
        f"measured {loot.GEM_DROPS_PER_GEAR_DROP}")


def test_the_tiers_share_of_the_gem_column_is_the_simulations() -> None:
    """Issue #2853, ruled by the owner on 2026-11-24: a lower tier drops three quarters of the tier above's gems.
    `loot.gem_drop_share` and `UCataclysmDropRoll::GemDropShareAt` must hold the same step and multiply it the same
    way, the expected gems must read it, and the spawner must ask it at the tier being played."""
    header = squeeze(code(SOURCE / "Items" / "CataclysmDropRoll.h"))
    found = re.search(r"static constexpr float GemDropShareATierDown = (\d*\.\d+)f;", header)
    assert found and float(found.group(1)) == loot.GEM_DROP_SHARE_A_TIER_DOWN, (
        f"the game's step is {found.group(1) if found else 'missing'}; the simulation's "
        f"{loot.GEM_DROP_SHARE_A_TIER_DOWN}")
    roll = squeeze(code(SOURCE / "Items" / "CataclysmDropRoll.cpp"))
    share = squeeze(body(roll, "float UCataclysmDropRoll::GemDropShareAt(", "CataclysmDropRoll.cpp"))
    assert "FMath::Clamp(DifficultyTier, 1, DifficultyTiers)" in share
    assert "for (int32 Below = Tier; Below < DifficultyTiers; ++Below) { Share *= GemDropShareATierDown; }" in share, (
        "GemDropShareAt no longer multiplies the step once for each tier below 8")
    expected = squeeze(body(roll, "float UCataclysmDropRoll::ExpectedGemDrops(", "CataclysmDropRoll.cpp"))
    assert "ScaledByLootQuantity(Row->GemDrops * GemDropShareAt(DifficultyTier), LootQuantity)" in expected, (
        "ExpectedGemDrops does not take the tier's share of the column")
    text = spawner()
    asked = text.find("const int32 DifficultyTier = ACataclysmGameMode::DifficultyTierIn(World);")
    counted = text.find("UCataclysmDropRoll::ExpectedGemDrops(Drops, EnemyRarity, LootQuantity, DifficultyTier)")
    assert -1 < asked < counted, "SpawnGemsFor does not count a kill's gems at the tier being played"
    tests = GEM_DROPS_TESTS.read_text(encoding="utf-8")
    one = re.search(r"constexpr float TierOneShare = (\d*\.\d+)f;", tests)
    assert one and float(one.group(1)) == loot.gem_drop_share(1), (
        f"{GEM_DROPS_TESTS.name} holds tier 1 to {one.group(1) if one else 'nothing'}; the simulation's share is "
        f"{loot.gem_drop_share(1)}")


def test_a_kills_gems_come_from_a_stream_of_their_own() -> None:
    text = spawner()
    assert "FRandomStream Stream(CataclysmSeedMix::Mix(KillStream.GetCurrentSeed(), GemStreamSalt, 0));" in text, (
        "SpawnGemsFor no longer seeds its own stream from the kill's through CataclysmSeedMix::Mix (issue #2749)")
    reads = re.findall(r"KillStream\.(\w+)", text)
    assert reads == ["GetCurrentSeed"], (
        f"SpawnGemsFor uses the kill's stream for {reads}; it may only read its seed, or a kill's other drops change")
    for draw in ("RollDropCount(", "RollRarity(", "RollGem("):
        call = text[text.find(draw):]
        assert text.find(draw) != -1 and "Stream)" in call[:call.find(";")], (
            f"SpawnGemsFor's {draw} does not draw on the gems' own stream")


def test_a_death_rolls_its_gems_last_only_when_it_pays_and_marked_when_raised() -> None:
    text = squeeze(code(ENEMY_CPP))
    paying = text.find("if (bPays) {")
    extras = text.find("UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(", paying)
    gems = text.find("UCataclysmDropSpawner::SpawnGemsFor(", paying)
    assert paying != -1 and extras != -1, "the death path no longer pays inside if (bPays) or rolls the tree's extras"
    assert gems > extras, "a death rolls its gems before the tree's extra drops, or not at all"
    branch = text[paying:]
    assert gems - paying < len(body(branch, "if (bPays)", ENEMY_CPP.name)), "a death rolls gems when it pays nothing"
    call = text[gems:text.find(";", gems)]
    assert "/*bMarked=*/bRaisedByARule" in call, (
        "a raised creature's gems are not marked, so Trick or Treat would roll for them")


def test_a_gem_drop_goes_onto_its_stack_and_is_a_quantity() -> None:
    taking = squeeze(body(code(DROPPED_CPP), "bool UCataclysmDropPickup::TakeInto(", DROPPED_CPP.name))
    assert "Drop->IsGem() ? Inventory->AddGems(Drop->Gem, Drop->GemRarity, Drop->GemQuantity)" in taking, (
        "TakeInto no longer puts a gem drop onto the stack of its gem and rarity")
    header = squeeze(code(DROPPED_H))
    assert "return IsMaterial() || IsGold() || IsGem();" in header, (
        "IsAQuantity leaves gems out, so a gem would need a click and could not be read from across the room")


def test_the_run_record_keeps_a_gem_lying_on_the_ground() -> None:
    record = squeeze(body(code(RECORDS_H), "struct CATACLYSM_API FCataclysmSavedGroundItem", RECORDS_H.name))
    for field in ("FName Gem;", "ECataclysmRarity GemRarity = ECataclysmRarity::Everyday;", "int32 GemQuantity = 0;"):
        assert f"SaveGame, Category = \"Cataclysm|Save\") {field}" in record, (
            f"FCataclysmSavedGroundItem does not save {field}")
    gathering = squeeze(body(code(GATHER_CPP), "FCataclysmSavedGroundItem FCataclysmSaveGather::GroundItemFrom(",
                             GATHER_CPP.name))
    for field in ("Gem", "GemRarity", "GemQuantity"):
        assert f"Saved.{field} = Drop.{field};" in gathering, f"GroundItemFrom does not copy {field}"
    applying = squeeze(body(code(APPLY_CPP), "ACataclysmDroppedItem* FCataclysmSaveApply::GroundItemInto(",
                            APPLY_CPP.name))
    assert "Saved.Gold, Saved.Gem, Saved.GemRarity, Saved.GemQuantity)" in applying, (
        "GroundItemInto does not put a saved gem back on the floor")


def test_a_gem_drops_border_is_its_raritys() -> None:
    describing = squeeze(body(code(DROPPED_CPP), "void ACataclysmDroppedItem::DescribeItself()", DROPPED_CPP.name))
    branch = describing[describing.find("if (IsGem())"):]
    assert describing.find("if (IsGem())") != -1, "DescribeItself has no branch for a gem"
    assert "Rarity = GemRarity;" in branch[:branch.find("return;")], (
        "DescribeItself does not set Rarity to the gem's, so NameBorderThicknessOf draws every gem at Everyday's")


def test_which_gem_is_drawn_evenly_among_the_rows() -> None:
    rolling = squeeze(body(code(GEMS_CPP), "FName UCataclysmGems::RollGem(", GEMS_CPP.name))
    assert "Gems[Stream.RandRange(0, Gems.Num() - 1)]" in rolling, (
        "RollGem no longer draws one row evenly from all of them (ruled 2026-11-21, uniform among the 28)")
