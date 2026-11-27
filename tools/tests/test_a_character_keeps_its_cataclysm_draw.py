"""A character keeps its Cataclysm draw across runs. Issue #1324 slice 6a.

`docs/Cataclysm_GDD_v2.md`, section II, Ending a Run: "The same tier means the
same Cataclysms, by name and not only by count." Ruled by the coordinating
session on 2026-11-02: the character record carries a Cataclysm order seed,
drawn once when the character is created, and every run's Cataclysm list is the
first N of `UCataclysmRoster::OrderFor(that seed)`. A character file from before
the field migrates with the seed its `CharacterId` gives.

WHY THIS IS CHECKED FROM PYTHON AS WELL. Continuous integration compiles no C++,
so the automation tests in `CataclysmCataclysmDrawTests.cpp` and
`CataclysmSaveLoadTests.cpp` never run on a pull request. This file checks what
can be checked from the text, and it is the independent half of two figures the
Unreal tests compare with:

- the seed the committed character files' identifier gives, which this file
  works out in Python with the same 32-bit FNV-1a the game uses and compares with
  the number `CataclysmSaveFixtures.h` states;
- the three seeds `Cataclysm.CataclysmDraw.TheSeedAnIdentifierGivesIsFixed`
  pins.

THE SIMULATION NEEDS NO CHANGE AND THIS FILE SAYS WHY RATHER THAN TESTING IT
AGAIN. `sim/cataclysm_sim/engine.py`'s `cataclysm_order_for` already draws the
order from the campaign's seed alone ("THE SEED IS THE CHARACTER"), and
`sim/tests/test_the_cataclysms_drawn_belong_to_the_character.py` already holds
the same seed to the same Cataclysms and a tier higher to those plus one. The
game was the half that drew from each run's clock seed.
"""

from __future__ import annotations

import json
import pathlib
import re
import struct

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
SAVE_RECORDS_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp"
SAVE_FIXTURES_H = SOURCE / "Cataclysm" / "Tests" / "CataclysmSaveFixtures.h"
DRAW_TESTS = SOURCE / "Cataclysm" / "Tests" / "CataclysmCataclysmDrawTests.cpp"
GAME_MODE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameMode.cpp"
SAVE_LOAD = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveLoad.cpp"
EMPIRE_RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"

FNV_OFFSET_BASIS = 2166136261
FNV_PRIME = 16777619


def seed_for(identifier: str) -> int:
    """`UCataclysmCharacterSave::CataclysmOrderSeedFor`, in Python.

    32-bit FNV-1a over the identifier's sixteen bytes in the order its 32 hex
    digits are written -- which is the order `A`, `B`, `C`, `D` are each read
    most significant byte first -- then the sign bit cleared.
    """
    assert re.fullmatch(r"[0-9A-Fa-f]{32}", identifier), identifier
    value = FNV_OFFSET_BASIS
    for byte in bytes.fromhex(identifier):
        value ^= byte
        value = (value * FNV_PRIME) & 0xFFFFFFFF
    return value & 0x7FFFFFFF


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed, so a comment naming a call cannot pass for the call."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def constant(name: str) -> int:
    match = re.search(rf"inline constexpr int32 {name} = (\d+);", SAVE_FIXTURES_H.read_text(encoding="utf-8"))
    assert match, f"CataclysmSaveFixtures.h no longer states {name}"
    return int(match[1])


def character_fixtures() -> dict[int, dict]:
    found = {}
    for path in sorted(FIXTURES.glob("Character_v*.json")):
        record = json.loads(path.read_text(encoding="utf-8"))
        found[record["SchemaVersion"]] = record
    return found


def test_the_fnv_hash_here_is_fnv_1a():
    """A control for `seed_for`: FNV-1a's published value for the empty input is its offset basis,
    and for the one byte 'a' it is 0xE40C292C."""
    value = FNV_OFFSET_BASIS
    for byte in b"a":
        value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFF
    assert value == 0xE40C292C


def test_every_committed_character_file_has_one_identifier():
    """`FixtureIdentifiersOrderSeed` is said to be what every older file migrates to, which needs one identifier."""
    identifiers = {record["CharacterId"] for record in character_fixtures().values()}
    assert identifiers == {"5C0B1A4E5B7F4D2E9A3C6F81D24E7B03"}, identifiers


def test_the_seed_the_tests_expect_a_migrated_file_to_hold_is_what_its_identifier_gives():
    assert constant("FixtureIdentifiersOrderSeed") == seed_for("5C0B1A4E5B7F4D2E9A3C6F81D24E7B03") == 1412640493


def test_the_seeds_the_unreal_test_pins_are_what_the_hash_gives():
    tests = DRAW_TESTS.read_text(encoding="utf-8")
    for identifier, seed in (("0" * 32, 1768495365), ("00000001000000020000000300000004", 1069075193)):
        assert seed_for(identifier) == seed
        assert f", {seed});" in tests, f"{DRAW_TESTS.name} no longer pins {seed}"


def test_the_games_hash_is_written_with_fnv_1a_constants():
    """Not `GetTypeHash`, which an engine upgrade may change."""
    text = code(SAVE_RECORDS_CPP)
    body = re.search(r"int32 UCataclysmCharacterSave::CataclysmOrderSeedFor\(const FGuid& Identifier\)\s*\{.*?\n\}",
                     text, re.S)
    assert body, "CataclysmOrderSeedFor is not where it was looked for"
    for needed in ("2166136261u", "16777619u", "0x7FFFFFFFu", "Identifier.A, Identifier.B, Identifier.C, Identifier.D",
                   "Shift = 24", "Shift -= 8"):
        assert needed in body[0], needed
    assert "GetTypeHash" not in body[0]


def test_a_characters_runs_are_begun_from_the_characters_seed():
    """Both play paths that begin a character's run hand over the character's seed, and the run draws from it."""
    assert "BeginACharactersEmpireRun(Seed, State->GetCataclysmOrderSeed()," in code(GAME_MODE), (
        "a new character's run no longer draws from the seed the character drew")
    assert "BeginACharactersEmpireRun(Seed, SavedCharacter->CataclysmOrderSeed," in code(SAVE_LOAD), (
        "a loaded character's new run no longer draws from the seed its record keeps")
    assert "ActiveCataclysms = UCataclysmRoster::ActiveFor(InCataclysmOrderSeed, InDifficultyTier);" in code(EMPIRE_RUN), (
        "UCataclysmEmpireRun::BeginForCharacter no longer draws the run's Cataclysms from the character's seed")


# ---------------------------------------------------------------------------
# The seeds the Unreal tests chose, checked against a Python copy of the engine's generator
# ---------------------------------------------------------------------------
#
# `FRandomStream` (Engine/Source/Runtime/Core/Public/Math/RandomStream.h): the seed mutates as
# `Seed * 196314165 + 907633515`, a fraction is the top 23 bits under a float exponent of 1, and `RandRange` truncates
# the fraction times the range. This copy is how the seeds were picked; it is NOT evidence about the game, whose own
# assertions and CONTROL lines measure the real generator, and whose info lines print what it drew.

ROSTER = ("Demonic", "Death", "War", "Pestilence", "Famine", "Celestial", "Chaos", "Void")


def _int32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def _mixed_seed(seed: int, salt: int) -> int:
    salted = ((seed & 0xFFFFFFFF) + salt * 2654435769) & 0xFFFFFFFF
    return _int32(salted * 2654435761 + 2654435769)


#: `UCataclysmRoster::Built`, the project owner's ruling of 2026-11-26. Read out of the game's source rather than
#: written here, so this copy cannot drift from it.
BUILT = tuple(re.findall(r"ECataclysmType::(\w+)", re.search(
    r"UCataclysmRoster::Built\(\)\s*\{(.*?)\n\}",
    (SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmRoster.cpp").read_text(encoding="utf-8"), re.S).group(1)))


def whole_roster_shuffle(seed: int) -> list[str]:
    """`UCataclysmRoster::OrderFor` before the ruling of 2026-11-26: Fisher-Yates, backwards, on a stream of the mixed
    seed, over all eight."""
    state = _mixed_seed(seed, 1) & 0xFFFFFFFF
    order = list(ROSTER)
    for index in range(len(order) - 1, 0, -1):
        state = (state * 196314165 + 907633515) & 0xFFFFFFFF
        fraction = struct.unpack("<f", struct.pack("<I", 0x3F800000 | (state >> 9)))[0] - 1.0
        product = struct.unpack("<f", struct.pack("<f", fraction * float(index + 1)))[0]
        swap = int(product)
        order[index], order[swap] = order[swap], order[index]
    return order


def order_for(seed: int) -> list[str]:
    """`UCataclysmRoster::OrderFor`, in Python: the whole roster's shuffle, then the built ones moved to the front, each
    part keeping the order the shuffle gave it."""
    whole = whole_roster_shuffle(seed)
    return [name for name in whole if name in BUILT] + [name for name in whole if name not in BUILT]


def test_the_copy_reads_some_built_cataclysms_and_only_real_ones():
    """A read that found nothing would make every order below the whole roster's shuffle, unnoticed."""
    assert BUILT and set(BUILT) <= set(ROSTER), BUILT


def test_the_seeds_the_unreal_tests_chose_draw_what_their_comments_say():
    """Since the ruling of 2026-11-26. The lists before it are what `whole_roster_shuffle` gives, checked below."""
    # TWO BUILT SINCE THE OWNER TOOK WAR OFF THE LIST, play-testing on 2026-11-26.
    assert order_for(24681357)[:3] == ["Demonic", "Death", "Pestilence"]
    assert order_for(11)[:3] == ["Death", "Demonic", "Chaos"]
    assert order_for(12)[:3] == ["Demonic", "Death", "War"]
    assert order_for(7)[:4] == ["Death", "Demonic", "Celestial", "Void"]
    assert order_for(8)[:4] == ["Demonic", "Death", "Void", "Chaos"]


def test_the_lists_before_the_ruling_are_what_the_comments_say_they_were():
    assert whole_roster_shuffle(24681357)[:3] == ["Demonic", "Pestilence", "War"]
    assert whole_roster_shuffle(11)[:3] == ["Death", "Chaos", "Void"]
    assert whole_roster_shuffle(12)[:3] == ["War", "Famine", "Demonic"]
    assert whole_roster_shuffle(7)[:3] == ["Celestial", "Death", "Void"]
    assert whole_roster_shuffle(8)[:3] == ["Void", "Demonic", "Death"]


def test_every_order_the_copy_draws_is_the_whole_roster():
    for seed in range(200):
        assert sorted(order_for(seed)) == sorted(ROSTER)


def test_the_figures_the_roster_tests_quote_are_what_the_copy_gives():
    """`Cataclysm.Roster.AdjacentSeedsDrawUnrelatedOrders` quotes 205 and 84 shared firsts in 400 neighbouring pairs:
    the first Cataclysm, and the first unbuilt one. With two built (Demonic and Death, since 2026-11-26); 130 and 105
    with the three first built."""
    built = len(BUILT)
    assert sum(order_for(s)[0] == order_for(s + 1)[0] for s in range(400)) == 205
    assert sum(order_for(s)[built] == order_for(s + 1)[built] for s in range(400)) == 84
