"""Every swept relocation in the game goes through the one move that is not refused for starting on the floor.

Issues #2203 and #2505, ruled by the coordinating session on 2026-10-26. The game stands a pawn at exactly its
capsule's half height, touching the floor's ground, and a plain swept `SetActorLocation` or `AddActorWorldOffset`
begun there is refused by the engine: the character moves 0 cm. `UCataclysmMovement::MoveWithoutSticking` makes the
move through the character's movement component, which lifts it clear and moves it again. Seven places made the plain
move; this keeps an eighth from being added.

FOUND FROM THE CODE, NOT TYPED HERE. Every `.cpp` under `game/Source` but the tests is read, comments stripped, and a
call to either function whose arguments end in `true` -- the sweep flag -- is a failure anywhere but inside the helper.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source"
HELPER = SOURCE / "Cataclysm" / "AbilitySystem" / "CataclysmMovement.cpp"
SWEPT = re.compile(r"\b(?:SetActorLocation|AddActorWorldOffset)\s*\(([^;]*?)\btrue\s*\)\s*;", re.S)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def game_sources():
    return [p for p in SOURCE.rglob("*.cpp") if "Tests" not in p.parts]


def swept_moves(path):
    return SWEPT.findall(strip_comments(path.read_text(encoding="utf-8")))


def test_the_check_reads_the_game_and_finds_the_helper():
    """Its scope, so an empty read cannot pass: the game's sources are found, the helper makes its one plain sweep for
    an actor that is not a character, and at least the eight call sites that replaced the old moves call it."""
    sources = game_sources()
    assert len(sources) >= 100, len(sources)
    assert len(swept_moves(HELPER)) == 1
    helper = strip_comments(HELPER.read_text(encoding="utf-8"))
    assert "SafeMoveUpdatedComponent(" in helper
    callers = sum(len(re.findall(r"UCataclysmMovement::MoveWithoutSticking\s*\(", strip_comments(p.read_text(
        encoding="utf-8")))) for p in sources if p != HELPER)
    assert callers >= 8, callers


def test_no_plain_swept_move_outside_the_helper():
    found = {str(p.relative_to(ROOT)): len(swept_moves(p)) for p in game_sources() if p != HELPER and swept_moves(p)}
    assert not found, (
        f"{found}: a plain swept SetActorLocation or AddActorWorldOffset is refused when the character starts on the "
        "floor and moves 0 cm. Call UCataclysmMovement::MoveWithoutSticking instead. Issue #2203.")
