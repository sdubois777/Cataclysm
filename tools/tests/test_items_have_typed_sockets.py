"""Items carry typed sockets, a drop rolls each socket's type evenly, and only a worn item's gems reach the character.

WHY THIS EXISTS. Issue #46, slice G3. `FCataclysmItem::Sockets` was a count, and nothing could sit in a socket. The
coordinating session ruled on 2026-11-21, from the design document's socket types (Change Socket Type, the enchantment
"Sockets on this equipment fit any gem type", and each gem's GemType):

  - each socket is Attack, Defense or Utility, rolled evenly on a drop, and a gem fits a socket of its own type;
  - a socket saved before sockets had types fits any gem;
  - a gem in a worn item adds its modifier; in a carried item it does nothing;
  - one effect per gem, and a two-handed weapon does not double its gems (its 6 sockets already equal a pair's 3 + 3).

Continuous integration compiles no C++, so the shape is checked from the source; `Cataclysm.ItemSockets.`
(`game/Source/Cataclysm/Tests/CataclysmItemSocketsTests.cpp`) tests what it does.

WHAT IS CHECKED:

  1. `FCataclysmSocket`'s three fields and the item's socket list are saved (`SaveGame`), and a socket's type defaults
     to Any, so a socket saved before types came back fits any gem;
  2. the socket types are exactly the gem types `game/Data/Gems.csv` names, and Any;
  3. a new socket's type is rolled among the three and never Any;
  4. the drop roll takes the types from a stream of their own, seeded from the drop's stream without drawing from it,
     so no later roll of a drop moves;
  5. `AccumulateInto` adds an item's socketed gems, and only the worn-equipment reading calls it outside the tests;
  6. a socketed gem is added once, with nothing doubling it;
  7. the item tooltip lists each socket;
  8. nothing in the C++ still reads `Sockets` as a number.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
ITEM_H = SOURCE / "Items" / "CataclysmItem.h"
ITEM_CPP = SOURCE / "Items" / "CataclysmItem.cpp"
GEMS_CPP = SOURCE / "Items" / "CataclysmGems.cpp"
DROP_CPP = SOURCE / "Items" / "CataclysmDropRoll.cpp"
TOOLTIP_CPP = SOURCE / "Interface" / "CataclysmItemTooltip.cpp"
GEMS_CSV = REPO_ROOT / "game" / "Data" / "Gems.csv"


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


def saved(struct_body: str, declaration: str) -> bool:
    found = re.search(r"UPROPERTY\(([^)]*)\) " + re.escape(declaration), squeeze(struct_body))
    return bool(found) and "SaveGame" in found.group(1)


def test_a_socket_and_the_items_socket_list_are_saved_and_a_socket_defaults_to_any() -> None:
    text = code(ITEM_H)
    socket = body(text, "struct CATACLYSM_API FCataclysmSocket", ITEM_H.name)
    for declaration in ("ECataclysmSocketType Type = ECataclysmSocketType::Any;", "FName Gem;",
                        "ECataclysmRarity GemRarity = ECataclysmRarity::Everyday;"):
        assert saved(socket, declaration), f"FCataclysmSocket does not save {declaration!r}"
    item = body(text, "struct CATACLYSM_API FCataclysmItem", ITEM_H.name)
    assert saved(item, "TArray<FCataclysmSocket> Sockets;"), "FCataclysmItem does not save its list of sockets"


def test_the_socket_types_are_the_gem_types_and_any() -> None:
    enum = body(code(ITEM_H), "enum class ECataclysmSocketType : uint8", ITEM_H.name)
    types = set(re.findall(r"\b([A-Z][a-z]+)\s+UMETA", enum))
    with GEMS_CSV.open(encoding="utf-8", newline="") as handle:
        gem_types = {row["GemType"] for row in csv.DictReader(handle)}
    assert types == gem_types | {"Any"}, (
        f"the socket types are {sorted(types)} and the gems' types {sorted(gem_types)}: a gem of a type with no "
        f"socket type fits nothing but an Any socket")


def test_a_new_sockets_type_is_one_of_the_three_and_never_any() -> None:
    rolling = body(code(GEMS_CPP), "ECataclysmSocketType UCataclysmGems::RollSocketType(", GEMS_CPP.name)
    for kind in ("Attack", "Defense", "Utility"):
        assert f"ECataclysmSocketType::{kind}" in rolling, f"RollSocketType never rolls {kind}"
    assert "ECataclysmSocketType::Any" not in rolling, "RollSocketType can roll Any, which no drop rolls"
    assert "Stream.RandRange(0, 2)" in rolling, "RollSocketType does not choose evenly among three"


def test_the_drop_roll_takes_socket_types_from_a_stream_of_their_own() -> None:
    text = squeeze(code(DROP_CPP))
    assert "FRandomStream TypeStream(Stream.GetCurrentSeed()" in text, (
        "the drop roll does not seed a stream of its own for socket types from the drop's stream")
    assert "UCataclysmGems::RollSocketType(TypeStream)" in text, "socket types are not rolled from that stream"
    assert "RollSocketType(Stream)" not in text, (
        "a socket type is drawn from the drop's own stream, which moves every roll after it")


def test_only_worn_items_reach_the_character_with_their_gems() -> None:
    accumulating = body(code(ITEM_CPP), "void UCataclysmItemModifiers::AccumulateInto(", ITEM_CPP.name)
    assert "UCataclysmGems::AddSocketedGems(Item," in accumulating, "AccumulateInto does not add an item's gems"
    callers = sorted(
        path.relative_to(SOURCE).as_posix() for path in SOURCE.rglob("*.cpp")
        if "/Tests/" not in path.as_posix()
        and "UCataclysmItemModifiers::AccumulateInto(" in code(path) and path != ITEM_CPP)
    assert callers == ["Items/CataclysmEquipmentComponent.cpp"], (
        f"AccumulateInto is called from {callers}; only the worn-equipment reading may call it, or a carried item's "
        f"gems would reach the character")


def test_a_socketed_gem_is_added_once_and_not_doubled() -> None:
    adding = body(code(GEMS_CPP), "void UCataclysmGems::AddSocketedGems(", GEMS_CPP.name)
    assert "AddGemModifier(*Row, Socket.GemRarity, Totals);" in adding, "AddSocketedGems does not add each gem"
    assert not re.search(r"TwoHand|\* ?2\b|\bHands\b", adding), (
        "AddSocketedGems reads the hands or multiplies by two; a two-handed weapon does not double its gems")


def test_the_item_tooltip_lists_each_socket() -> None:
    assert "Lines.Append(UCataclysmGems::SocketLinesFor(Item));" in code(TOOLTIP_CPP), (
        "the item tooltip does not list each socket's type and gem")


def test_nothing_reads_sockets_as_a_number() -> None:
    offenders = []
    for path in SOURCE.rglob("*.*"):
        if path.suffix not in (".cpp", ".h"):
            continue
        for number, line in enumerate(code(path).splitlines(), 1):
            if re.search(r"(\.|->)Sockets\s*(=|<|>|==|!=|>=|<=)\s*\d", line):
                offenders.append(f"{path.relative_to(SOURCE).as_posix()}:{number}: {line.strip()}")
    assert not offenders, "these read Sockets as a count, which is a list since slice G3:\n" + "\n".join(offenders)
