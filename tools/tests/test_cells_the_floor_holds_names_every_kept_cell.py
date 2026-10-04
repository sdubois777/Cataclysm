"""Every cell or position the dungeon game mode keeps for later is held from obstacles, or named harmless with a reason.

Issues #1820 and #41. A runtime floor obstacle (`ACataclysmFloorObstacle`) closes cells of the floor plan during play,
and the placement rule refuses every cell `ACataclysmDungeonGameMode::CellsTheFloorHolds` returns. Some of the game mode
keeps cells chosen earlier and uses them later without asking the plan again -- `SpawnPlacedCreature` puts a creature
on whatever cell it is handed -- so a kept cell an obstacle could close would put a creature, the player or a zone into
it. The automation test `FloorObstacleRefusesEveryCellTheFloorStillHoldsAUseFor` places sources that exist today; it
cannot notice a list added tomorrow. This does.

WHAT COUNTS AS KEEPING A CELL, read from the header: a member whose type is a cell (`FIntPoint`), a TArray, TSet or
TMap of them, a container of a struct carrying one, or a TArray or TSet of positions (`FVector`) or of a struct
carrying a position field named `Location`, `Where`, `At`, `Point` or `Middle`. Positions are counted as well as cells
because Luxury Hoarders, Raw Sewage and the Infection Bloom keep positions, and a pin limited to cells could not see
them -- wider than the ruling asked, and said so.
"""
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DUNGEON = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
HEADER = DUNGEON / "CataclysmDungeonGameMode.h"
SOURCE = DUNGEON / "CataclysmDungeonGameMode.cpp"
HOLDS = "TSet<FIntPoint> ACataclysmDungeonGameMode::CellsTheFloorHolds() const"

# EVERY KEPT CELL OR POSITION THAT IS NOT HELD, AND WHY THAT IS SAFE. A name here must be a member the search finds, and
# must not also be read by CellsTheFloorHolds.
HARMLESS = {
    "LuxuryHoards": "read only by the floor panel's count; nothing is placed or routed from it",
    "PlagueHarbingerTrails": "LastPatchAt is where a Harbinger last laid a patch; it lays new ones only where it "
                             "walks, and it cannot walk into an obstacle",
    "PandorasBoxWaves": "At is a box's own position, already held through the floor object standing there; its waves "
                        "arrive through BringCreaturesNear, which reads the plan",
    "GatedShortcuts": "a shortcut's corridor is held from pillars and pits by CellsHeldOrWarned, which every obstacle "
                      "placement asks; it is not in CellsTheFloorHolds because a gate asks that when it closes, and "
                      "would then refuse its own cells",
}

POSITION_FIELD = re.compile(r"\bFVector\s+\w*(Location|Where|At|Point|Middle)\b")
DECLARATION = re.compile(r"^\s*(?:mutable\s+)?((?:TArray|TSet|TMap)<[^;()]*>|F\w+)\s+(\w+)\s*(?:=[^;]*)?;", re.M)


def struct_blocks(text):
    """(name, start, end, body) of every struct definition, nested or not, by brace matching."""
    for found in re.finditer(r"\bstruct\s+(?:CATACLYSM_API\s+)?(F\w+)[^;{(]*\{", text):
        depth, index = 1, found.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(text[index], 0)
            index += 1
        yield found.group(1), found.start(), index, text[found.end():index]


def without_comments(text):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


def kept_members():
    """The game mode's members that keep a cell or a position, by name."""
    carriers = set()
    bodies = {}
    for header in DUNGEON.glob("*.h"):
        for name, _, _, body in struct_blocks(header.read_text(encoding="utf-8")):
            bodies[name] = body
            if re.search(r"\bFIntPoint\b", body) or POSITION_FIELD.search(body):
                carriers.add(name)
    # AND A STRUCT THAT CARRIES A CARRIER, to any depth: `FGatedShortcut` holds an `FCataclysmFloorShortcut`, which
    # holds the cells. Without this the game mode's list of shortcuts was invisible to the check.
    grew = True
    while grew:
        grew = False
        for name, body in bodies.items():
            if name not in carriers and any(re.search(rf"\b{carrier}\b", body) for carrier in carriers):
                carriers.add(name)
                grew = True
    text = HEADER.read_text(encoding="utf-8")
    outside, cursor = [], 0
    for _, start, end, _ in struct_blocks(text):
        if start >= cursor:
            outside.append(text[cursor:start])
            cursor = end
    outside.append(text[cursor:])
    members = {}
    for found in DECLARATION.finditer(without_comments("".join(outside))):
        kind, name = found.group(1), found.group(2)
        if (re.search(r"\bFIntPoint\b", kind) or re.match(r"(TArray|TSet)<FVector>", kind)
                or any(re.search(rf"\b{carrier}\b", kind) for carrier in carriers)):
            members[name] = kind
    return members


def holds_body():
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index(HOLDS)
    return without_comments(text[start:text.index("\n}\n", start)])


def test_every_kept_cell_is_held_from_obstacles_or_named_harmless():
    members = kept_members()
    # A POSITIVE CONTROL FIRST: lists known to keep cells and positions are found, so "nothing missing" below means
    # something rather than a search that found nothing at all.
    for known in ("WaveStillToArrive", "RealityRiftCells", "VoidParasiteLightCell", "InfestedVeins", "LuxuryHoards",
                  "GatedShortcuts"):
        assert known in members, f"the search no longer finds {known}; it is blind and the check below proves nothing"
    body = holds_body()
    read = {name for name in members if re.search(rf"\b{name}\b", body)}
    missing = sorted(set(members) - read - set(HARMLESS))
    assert not missing, (
        f"{HEADER.name} keeps {missing} for later, and CellsTheFloorHolds does not read them. An obstacle raised "
        f"during play could close one of their cells. Hold them in CellsTheFloorHolds, or add each to HARMLESS in "
        f"this file with the reason it is safe.")
    stale = sorted(set(HARMLESS) - set(members))
    assert not stale, f"HARMLESS names {stale}, which the header no longer keeps; remove them."
    both = sorted(set(HARMLESS) & read)
    assert not both, f"{both} are both held and named harmless; say which."
