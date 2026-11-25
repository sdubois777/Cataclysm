"""Gold: creatures drop it, the account holds it once per lethality mode, and the empire map shows it.

WHY THIS EXISTS. Issue #2742, slice 1. The project owner ruled on 2026-11-18 that gold comes from creature kills,
"larger for rarer creatures and deeper floors, as gear and materials drop today", and the coordinating session ruled
the figures the same day (G1 to G6, every one a judgement). The `Cataclysm.Gold.` automation tests check what the game
does; continuous integration compiles no C++, so this file checks, by reading it, that the pieces are wired where the
rulings put them, and that the design document, the workbook and the generated table state the same figures.

WHAT IS CHECKED:

  1. the design document's gold table states the Gold sheet's figures, and `game/Data/Gold.csv` holds them;
  2. a kill's expected gold piles are its expected gear (G1), in the generated table;
  3. `ExpectedGoldDrops` reads no loot quantity (G2);
  4. a creature's death drops gold inside the block that pays for it, after its items, and only where
     `GoldDropsIn` says a kill drops gold, which is where the character playing has a balance;
  5. the player's sweep banks gold, and asks `ComesAutomatically` whether a drop is a quantity (G3);
  6. a pile is named at any distance, as a material is (G3);
  7. the save gather writes a pile's gold and the restore reads it back (run record version 7);
  8. one function says which balance a character has -- a Solo Self-Found character's own, which its character
     record saves, or its lethality mode's in the account (the owner's ruling of 2026-11-18, reversing G5) -- and the
     empire map's status line, the inventory screen's header and the PLAY LOOP line all read it (G6, and the owner's
     request of 2026-11-18 for the inventory screen);
  9. a walk's end says the gold it picked up, after the clear's or the death's line, and a walk's start zeroes it;
 10. Famished Beasts eats no gold: its row says "dropped items", and gold is not an item.

WHAT IS NOT. That any of it does what it says when run; the automation tests do.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
DATA = REPO_ROOT / "game" / "Data"
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

GOLD_TABLE_HEADER = "| Difficulty tier | Gold per pile on floor 1 | More per floor deeper |"
GOLD_TABLE_ROW = re.compile(r"^\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)%\s*\|\s*$")


def strip_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, string literals kept."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j == -1 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def code(relative: str) -> str:
    path = SOURCE / relative
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. If it moved, move it here too.")
    return squeeze(strip_comments(path.read_bytes().decode("utf-8")))


def braced_from(text: str, start: int, what: str) -> str:
    open_at = text.find("{", start)
    assert open_at != -1, f"{what} has no opening brace"
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    raise AssertionError(f"{what} is never closed")


def body(relative: str, signature: str) -> str:
    text = code(relative)
    signature = squeeze(signature)
    assert text.count(signature) == 1, (
        f"{signature} should be defined once in {relative}; it appears {text.count(signature)} times")
    return braced_from(text, text.find(signature), signature)


def rows(name: str) -> list[dict[str, str]]:
    path = DATA / name
    if not path.is_file():
        pytest.fail(f"game/Data/{name} does not exist; run tools/generate_datatables.py")
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


# --------------------------------------------------------------------------
# 1 and 2: the figures, in the document, the workbook and the generated tables
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def stated() -> dict[int, tuple[float, float]]:
    """Tier against (gold per pile on floor 1, the share more per floor deeper), from the design document."""
    document = DESIGN_DOCUMENT.read_bytes().decode("utf-8")
    assert GOLD_TABLE_HEADER in document, (
        f"{DESIGN_DOCUMENT.name} no longer has a table headed {GOLD_TABLE_HEADER!r}, the gold paragraphs of What a "
        "Kill Drops; the comparisons below would pass having compared nothing.")
    out: dict[int, tuple[float, float]] = {}
    for line in document[document.index(GOLD_TABLE_HEADER):].splitlines()[1:]:
        match = GOLD_TABLE_ROW.match(line)
        if not match:
            if line.startswith("|"):
                continue          # the alignment row
            break                 # the table has ended
        out[int(match.group(1))] = (float(match.group(2)), float(match.group(3)) / 100.0)
    return out


def test_the_document_states_every_tiers_pile_as_the_generated_table_holds_it(stated) -> None:
    table = {int(row["DifficultyTier"]): (float(row["GoldPerPile"]), float(row["MorePerFloor"]))
             for row in rows("Gold.csv")}
    assert sorted(table) == list(range(1, 9)), f"Gold.csv names tiers {sorted(table)}, not 1 to 8"
    assert stated == pytest.approx(table), (
        f"{DESIGN_DOCUMENT.name} states gold piles {stated} and game/Data/Gold.csv holds {table}. The workbook's Gold "
        "sheet is the authority; regenerate the table, or correct the document's table, so a reader plans against the "
        "figures the game uses.")


def test_the_workbook_gold_sheet_is_what_the_table_was_generated_from() -> None:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")
    book = openpyxl.load_workbook(WORKBOOK, data_only=True, read_only=True)
    assert "Gold" in book.sheetnames, "the workbook has no Gold sheet; issue #2742 added it"
    sheet = list(book["Gold"].iter_rows(values_only=True))
    headers = [str(h).strip() if h is not None else "" for h in sheet[0]]
    tier = headers.index("Difficulty Tier")
    pile = headers.index("Gold Per Pile")
    floor = headers.index("More Per Floor")
    workbook = {int(row[tier]): (float(row[pile]), float(row[floor])) for row in sheet[1:] if row[tier] is not None}
    table = {int(row["DifficultyTier"]): (float(row["GoldPerPile"]), float(row["MorePerFloor"]))
             for row in rows("Gold.csv")}
    assert workbook == pytest.approx(table), (
        f"the Gold sheet holds {workbook} and game/Data/Gold.csv {table}: run tools/generate_datatables.py")


#: Gold piles a kill, as G1 ruled them on 2026-11-18: the gear column of that day.
GOLD_PILES_AS_RULED = {"Common": 0.16, "Elite": 0.5, "Legendary": 1.0, "Herald": 2.0, "Boss": 5.0,
                       "Cataclysm Boss": 12.0}


def test_a_kills_gold_piles_are_the_gear_ladder_g1_ruled() -> None:
    """G1, ruled 2026-11-18: the gear ladder, from the owner's words "as gear and materials drop today".

    THE GEAR COLUMN HALVED ON 2026-11-24 AND THE GOLD COLUMN DID NOT (issue #2853): the owner ruled "Halve gear drops
    (Recommended)" for the gear column, and the drop supply proposal it adopted said the gold and material columns do
    not move. So a rung's gold piles are still the gear ladder of 2026-11-18, which is twice its gear drops today: the
    ladder's shape, and G1's figures. Reading gold as "equal to whatever gear is" would have halved every kill's gold, a
    change to the economy nobody ruled (a judgement, recorded in `docs/DECISIONS.md`)."""
    table = {row["EnemyRarity"]: row for row in rows("EnemyDrops.csv")}
    assert {rarity: float(row["GoldDrops"]) for rarity, row in table.items()} == pytest.approx(GOLD_PILES_AS_RULED), (
        "game/Data/EnemyDrops.csv's gold piles are not the figures G1 ruled (issue #2742); a change is a new ruling")
    wrong = [f"{rarity}: gold {row['GoldDrops']}, gear {row['GearDrops']}" for rarity, row in table.items()
             if float(row["GoldDrops"]) != pytest.approx(2.0 * float(row["GearDrops"]))]
    assert not wrong, (
        "game/Data/EnemyDrops.csv gives a rung gold piles that are not twice its gear drops, so the two ladders no "
        "longer have one shape: " + "; ".join(wrong))


# --------------------------------------------------------------------------
# 3 and 4: a kill
# --------------------------------------------------------------------------

def test_no_loot_quantity_reaches_the_gold_piles() -> None:
    step = body("Items/CataclysmDropRoll.cpp",
                "float UCataclysmDropRoll::ExpectedGoldDrops(const UDataTable* EnemyDropTable, FName EnemyRarity)")
    assert "GoldDrops" in step, "ExpectedGoldDrops no longer reads the row's GoldDrops"
    for word in ("LootQuantity", "ScaledByLootQuantity", "MagicFind"):
        assert word not in step, (
            f"ExpectedGoldDrops reads {word}. Ruled 2026-11-18 (G2): neither magic find nor loot quantity raises gold.")


def test_a_paid_death_drops_gold_after_its_items_and_only_where_gold_drops() -> None:
    death = code("Character/CataclysmEnemyCharacter.cpp")
    paid = "if (bPays) {"
    assert death.count(paid) >= 1, "the death no longer gates its drops on bPays"
    block = braced_from(death, death.index(paid), "the paid death")
    # THE CALL'S OPENING, since #2741 slice 6a passes the empire upgrade tree's gold multiplier after `Stream`;
    # `tools/tests/test_empire_tree_gold_nodes.py` holds that argument.
    spawn = "UCataclysmDropSpawner::SpawnGoldFor(World, RarityStepItPaysAs(), GetActorLocation(), Stream,"
    gate = "if (UCataclysmDropSpawner::GoldDropsIn(World))"
    worn = "DropWhatItWore(World, GetActorLocation(), Stream);"
    for piece in (spawn, gate, worn):
        assert piece in block, f"a paid death no longer does {piece!r}"
    assert block.index(worn) < block.index(gate) < block.index(spawn), (
        "a paid death rolls its gold before its items, or outside GoldDropsIn: the gold must draw on the kill's stream "
        "after every item, so the items stay as they were, and none drops where nobody has a balance for it.")


def test_gold_drops_only_where_the_character_playing_has_a_balance() -> None:
    gate = body("Items/CataclysmDroppedItem.cpp", "bool UCataclysmDropSpawner::GoldDropsIn(const UWorld* World)")
    assert "return UCataclysmGameInstance::GoldHeldBy(World, Held);" in gate, (
        "GoldDropsIn no longer asks GoldHeldBy, the one place that says which balance the character playing has")
    assert "bSoloSelfFound" not in gate, (
        "GoldDropsIn decides on Solo Self-Found again. The owner ruled on 2026-11-18 that such a character earns gold.")


# --------------------------------------------------------------------------
# 8: which balance, read through one function
# --------------------------------------------------------------------------

def test_one_function_says_which_balance_a_character_has() -> None:
    held = body("Player/CataclysmGameInstance.cpp",
                "bool UCataclysmGameInstance::GoldHeldBy(const UObject* WorldContext, int64& OutGold)")
    own = "OutGold = State->GetOwnGold();"
    mode = "Instance->AccountFor(Partition)"
    assert "if (Partition.bSoloSelfFound)" in held and own in held and mode in held, (
        "GoldHeldBy no longer reads a Solo Self-Found character's own gold and every other character's mode's")
    assert held.index("if (Partition.bSoloSelfFound)") < held.index(own) < held.index(mode), (
        "GoldHeldBy reads the account before deciding whether the character is Solo Self-Found")
    add = body("Player/CataclysmGameInstance.cpp",
               "bool UCataclysmGameInstance::AddGoldFor(const UObject* WorldContext, int64 Gold, "
               "bool& bOutIntoTheCharacter)")
    for piece in ("if (State->GetPartition().bSoloSelfFound)", "State->AddOwnGold(Gold);", "bOutIntoTheCharacter = true;",
                  "Instance->BankGold(State->GetPartition(), Gold)"):
        assert piece in add, f"AddGoldFor no longer does {piece!r}"


@pytest.mark.parametrize("relative, signature, reads", [
    ("Interface/CataclysmEmpireMapLayout.cpp", "FString UCataclysmEmpireMapLayout::GoldLineIn(const UObject* WorldContext)",
     "UCataclysmGameInstance::GoldBalanceText(WorldContext)"),
    ("Player/CataclysmPlayLoopLog.cpp",
     "FString FCataclysmPlayLoopLog::GoldLineIn(const UObject* WorldContext, int32 DungeonId)",
     "UCataclysmGameInstance::GoldHeldBy(WorldContext, Held)"),
    ("Items/CataclysmDroppedItem.cpp",
     "int64 UCataclysmDropPickup::BankGold(UWorld* World, const TArray<ACataclysmDroppedItem*>& Piles)",
     # Since issue #2741 slice 6e the pickup pays a debt down first, in the one function beside AddGoldFor that does.
     "UCataclysmGameInstance::AddPickedUpGoldFor(World, Total, bIntoTheCharacter, Repaid)"),
], ids=["map line", "play loop line", "pickup"])
def test_every_reader_of_the_balance_goes_through_the_one_function(relative: str, signature: str, reads: str) -> None:
    assert reads in body(relative, signature), (
        f"{signature} no longer reads the balance through {reads!r}, so it could show or add to the wrong one")


def test_the_inventory_screen_shows_the_balance() -> None:
    widget = code("Interface/CataclysmInventoryWidget.cpp")
    assert "UCataclysmGameInstance::GoldBalanceText(this)" in widget, (
        "the inventory screen's header no longer shows the balance, which the owner asked for on 2026-11-18")
    header = body("Interface/CataclysmInventoryScreen.cpp",
                  "FString UCataclysmInventoryScreen::HeaderTextFor(int32 Used, int32 Capacity, const FString& HeldName, "
                  "const FString& Gold)")
    assert "*Gold" in header, "HeaderTextFor no longer writes the balance it is given"


def test_a_pickup_writes_the_records_that_changed() -> None:
    bank = body("Items/CataclysmDroppedItem.cpp",
                "int64 UCataclysmDropPickup::BankGold(UWorld* World, const TArray<ACataclysmDroppedItem*>& Piles)")
    account = ("if (!bIntoTheCharacter) { UCataclysmSaveWriter::NoteTriggerIn(World, "
               "ECataclysmSaveTrigger::AccountChanged); }")
    always = "UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::InventoryChanged);"
    assert account in bank, "a pickup into the account no longer writes the account record"
    assert always in bank, (
        "a pickup no longer writes the run and character records: the floor would keep piles the balance already "
        "holds until the next clock write, and a Solo Self-Found character's balance would not be saved")


def test_a_solo_self_found_characters_gold_is_saved_with_it() -> None:
    gather = code("Save/CataclysmSaveGather.cpp")
    assert "Record.Gold = State->GetOwnGold();" in gather, "the character record's gather no longer writes its gold"
    restore = code("Save/CataclysmSaveApply.cpp")
    assert "State->SetOwnGold(Record.Gold);" in restore, "the character record's restore no longer reads its gold"


# --------------------------------------------------------------------------
# 5 and 6: picking it up, and seeing it
# --------------------------------------------------------------------------

def test_the_players_sweep_banks_gold_as_a_quantity() -> None:
    sweep = body("Player/CataclysmPlayerController.cpp", "void ACataclysmPlayerController::CollectMaterialsNearby()")
    assert "Drop->IsAQuantity(), Standing, Drop->GetActorLocation()" in sweep, (
        "the sweep no longer asks ComesAutomatically about a quantity -- a material or gold -- so gold would never "
        "come without a click (G3)")
    assert "UCataclysmDropPickup::BankGold(World, GoldComing);" in sweep, "the sweep no longer banks the gold it gathers"


def test_a_click_on_a_pile_banks_it_rather_than_carrying_it() -> None:
    take = body("Items/CataclysmDroppedItem.cpp",
                "bool UCataclysmDropPickup::TakeInto(UCataclysmInventoryComponent* Inventory, "
                "ACataclysmDroppedItem* Drop, bool bByHand)")
    banks = "if (Drop->IsGold())"
    carries = "Inventory->AddMaterial("
    assert banks in take and carries in take, "TakeInto no longer has both a gold branch and the carried one"
    assert take.index(banks) < take.index(carries), "TakeInto would carry a gold pile before asking whether it is gold"


def test_a_pile_is_named_at_any_distance_as_a_material_is() -> None:
    naming = body("Items/CataclysmDroppedItem.cpp",
                  "void UCataclysmDropPickup::DropsToName(const UWorld* World, const FVector& Standing, "
                  "TArray<ACataclysmDroppedItem*>& OutDrops)")
    assert "if (!Drop->IsAQuantity() && !IsWithinNameRange(Standing, Drop->GetActorLocation()))" in naming, (
        "DropsToName no longer exempts gold from the 10 metre naming range, and it is swept from 15 metres, so its "
        "name would never be drawn -- the fault issue #1117 found for materials.")


# --------------------------------------------------------------------------
# 7: the floor's save
# --------------------------------------------------------------------------

def test_a_piles_gold_is_saved_and_restored() -> None:
    gather = body("Save/CataclysmSaveGather.cpp",
                  "FCataclysmSavedGroundItem FCataclysmSaveGather::GroundItemFrom( const ACataclysmDroppedItem& Drop)")
    assert "Saved.Gold = Drop.Gold;" in gather, "the gather no longer writes a pile's gold"
    restore = body("Save/CataclysmSaveApply.cpp",
                   "ACataclysmDroppedItem* FCataclysmSaveApply::GroundItemInto( UWorld& World, "
                   "const FCataclysmSavedGroundItem& Saved)")
    # A GEM'S THREE FIELDS FOLLOW THE GOLD since issue #46's slice G5, so the gold is matched up to its comma.
    assert "Saved.MaterialQuantity, Saved.Gold," in restore, "the restore no longer puts a pile's gold back"


# --------------------------------------------------------------------------
# 8 and 9: what the player and the log read
# --------------------------------------------------------------------------

def test_the_empire_maps_status_line_shows_the_balance() -> None:  # G6
    widget = code("Interface/CataclysmEmpireMapWidget.cpp")
    assert "UCataclysmEmpireMapLayout::GoldLineIn(this)" in widget, (
        "the empire map's status line no longer reads the gold balance (G6, issue #2742)")


@pytest.mark.parametrize("signature, before", [
    ("bool ACataclysmDungeonGameMode::ClearEmpireDungeon()", "FCataclysmPlayLoopLog::ClearLine("),
    ("bool ACataclysmDungeonGameMode::EndTheDungeonForADeath()", "FCataclysmPlayLoopLog::DeathLine("),
], ids=["clear", "death"])
def test_a_walks_end_says_the_gold_it_picked_up(signature: str, before: str) -> None:
    walk_end = body("Dungeon/CataclysmDungeonGameMode.cpp", signature)
    said = "FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::GoldLineIn(this,"
    assert said in walk_end and before in walk_end, f"{signature} no longer says both {before!r} and the gold line"
    assert walk_end.index(before) < walk_end.index(said), f"{signature} says the gold line before {before!r}"


def test_a_walks_start_zeroes_the_gold_it_has_picked_up() -> None:
    start = body("Dungeon/CataclysmDungeonGameMode.cpp", "bool ACataclysmDungeonGameMode::EnterEmpireDungeon(")
    zeroed = "Instance->GoldPickedUpThisWalk = 0;"
    entered = "FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::EnteredLine("
    assert zeroed in start and entered in start, "EnterEmpireDungeon no longer zeroes the walk's gold and says it entered"
    assert start.index(zeroed) < start.index(entered), "the walk's gold is zeroed after the walk is said to begin"


# --------------------------------------------------------------------------
# 10: no dungeon rule acts on gold
# --------------------------------------------------------------------------

def test_famished_beasts_eat_no_gold() -> None:
    eats = body("Dungeon/CataclysmDungeonModifierEffects.cpp",
                "bool UCataclysmDungeonModifierEffects::FamishedBeastsEats(const ACataclysmDroppedItem* Drop)")
    assert "!Drop->IsGold()" in eats, (
        "Famished Beasts would eat gold. Its row says \"consume dropped items\", gold is not an item, and no dungeon "
        "rule acts on gold in issue #2742's work (ruled 2026-11-18).")
