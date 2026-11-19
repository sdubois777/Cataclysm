"""The game's seed mix against the simulation's copy, and a sweep that no stream is seeded from related seeds again.

ISSUE #2749, THE PLACEMENT SEEDING SLICE, ruled by the coordinating session on 2026-11-19. Until it, the game derived
random streams by XOR with a salt plus an index, by XOR with a creature's identity and the clock, and by
`UCataclysmRoster::MixedSeed`, which is linear; Unreal's `FRandomStream` is a linear congruential generator, so each
made related streams, and a floor's Elites drew their first modifiers from a band of a few adjacent rows. Every such
stream now takes its seed from `CataclysmSeedMix::Mix` (`game/Source/CataclysmEmpire/Empire/CataclysmSeedMix.h`).

WHY PYTHON READS C++ HERE. Continuous integration compiles no C++. `sim/analyse_placement_seeds.py` measured the fault
and the fix with `planned_seed`; these hold the header's arithmetic equal to it, and fail the moment a stream is seeded
the old way again. The Unreal tests in `Cataclysm.Seeding.` measure the game's draws themselves.
"""
from __future__ import annotations

import contextlib
import io
import os
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
MIX_HEADER = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmSeedMix.h"
FLOOR_GENERATOR = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmFloorGenerator.cpp"
DUNGEON_GAME_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"

M32 = 0xFFFFFFFF


def without_comments(text: str) -> str:
    """The text with every `//` comment and `/* */` block taken out, by syntax and not by a line's first character."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


@pytest.fixture(scope="module")
def replay():
    """`sim/analyse_placement_seeds.py` as a module, on its smallest run, its printing kept off the test's output."""
    sys.path.insert(0, str(REPO_ROOT / "sim"))
    previous = os.environ.get("CATACLYSM_PLACEMENT_FLOORS")
    os.environ["CATACLYSM_PLACEMENT_FLOORS"] = "8"
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            import analyse_placement_seeds
    finally:
        if previous is None:
            os.environ.pop("CATACLYSM_PLACEMENT_FLOORS", None)
        else:
            os.environ["CATACLYSM_PLACEMENT_FLOORS"] = previous
    return analyse_placement_seeds


def header_mix():
    """`CataclysmSeedMix::Mix`, built from the header's own constants and steps, read as text."""
    text = without_comments(MIX_HEADER.read_text(encoding="utf-8"))
    factors = {name: int(value, 16) for name, value in
               re.findall(r"constexpr\s+uint32\s+(SeedFactor|SaltFactor|IndexFactor)\s*=\s*(0x[0-9A-Fa-f]+)u\s*;", text)}
    finalise = text[text.index("Finalise(uint32 Mixed)"):text.index("inline int32 Mix(")]
    steps = re.findall(r"Mixed\s*(\^=\s*Mixed\s*>>\s*(\d+)|\*=\s*(0x[0-9A-Fa-f]+)u)\s*;", finalise)
    assert set(factors) == {"SeedFactor", "SaltFactor", "IndexFactor"}, factors
    assert len(steps) == 5, steps

    def mix(seed: int, salt: int, index: int) -> int:
        mixed = ((seed & M32) * factors["SeedFactor"] + (salt & M32) * factors["SaltFactor"]
                 + (index & M32) * factors["IndexFactor"]) & M32
        for _, shift, factor in steps:
            mixed = mixed ^ (mixed >> int(shift)) if shift else (mixed * int(factor, 16)) & M32
        mixed &= 0x7FFFFFFF
        return mixed or 1

    return mix, steps


def test_the_games_mix_is_the_replays_planned_seed(replay):
    mix, _ = header_mix()
    cases = [(seed, salt, index) for seed in (1, 7, 123456789, 0x7FFFFFFF, -5, -0x80000000)
             for salt in (0, 0x72720000, 0x6D6D0000, 0x9A7D, -1) for index in (0, 1, 2, 249, 100000, -3)]
    differing = [case for case in cases if mix(*case) != replay.planned_seed(*case)]
    assert not differing, f"{len(differing)} of {len(cases)} inputs differ; the first: {differing[:5]}"
    assert len(cases) == 180  # 6 seeds, 5 salts, 6 indices


def test_the_mixs_finaliser_is_seed_for_floors():
    """The header says its finaliser is the one `FCataclysmFloorGenerator::SeedForFloor` ends with; the same five
    steps in the same order."""
    _, steps = header_mix()
    source = without_comments(FLOOR_GENERATOR.read_text(encoding="utf-8"))
    body = source[source.index("int32 FCataclysmFloorGenerator::SeedForFloor("):]
    body = body[:body.index("return")]
    floor_steps = re.findall(r"Mixed\s*(\^=\s*Mixed\s*>>\s*(\d+)|\*=\s*(0x[0-9A-Fa-f]+)u)\s*;", body)
    assert floor_steps == steps, (floor_steps, steps)


def test_a_placements_two_seeds_are_mixed_and_not_xored():
    source = without_comments(DUNGEON_GAME_MODE.read_text(encoding="utf-8"))
    rarity = source[source.index("int32 ACataclysmDungeonGameMode::RaritySeedForPlacementOn("):]
    rarity = rarity[:rarity.index("}")]
    modifiers = source[source.index("int32 ACataclysmDungeonGameMode::ModifierSeedForPlacementOn("):]
    modifiers = modifiers[:modifiers.index("}")]
    assert re.search(r"CataclysmSeedMix::Mix\(\s*FloorSeed\s*,\s*RarityFromTheFloorSalt\s*,\s*Index\s*\)", rarity), rarity
    assert re.search(r"CataclysmSeedMix::Mix\(\s*FloorSeed\s*,\s*Salt\s*,\s*Index\s*\)", modifiers), modifiers
    assert "^" not in rarity and "^" not in modifiers


#: THE STREAMS STILL SEEDED BY `MixedSeed`, ruled safe on 2026-11-19 (see `UCataclysmRoster::MixedSeed`'s comment):
#: `OrderFor`'s one stream a character. Matched by file and by the text of the seed.
ALLOWED_MIXED_SEED = {("CataclysmRoster.cpp", "MixedSeed(Seed, OrderSalt)")}


def stream_seeds() -> list[tuple[str, int, str]]:
    """Every seed the game's non-test code gives a stream, as (file, line, the seed's text): `FRandomStream Name(...)`,
    `FRandomStream(...)`, `X.Initialize(...)` and `PandorasBoxSeeds.Add(...)`, read by balancing brackets."""
    found = []
    opening = re.compile(r"FRandomStream\s+\w+\s*\(|FRandomStream\s*\(|\.Initialize\s*\(|PandorasBoxSeeds\.Add\s*\(")
    for path in sorted(SOURCE.rglob("*.cpp")):
        if "Tests" in path.parts:
            continue
        text = without_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in opening.finditer(text):
            depth, at = 1, match.end()
            while depth and at < len(text):
                depth += {"(": 1, ")": -1}.get(text[at], 0)
                at += 1
            seed = " ".join(text[match.end():at - 1].split())
            # A DECLARATION, NOT A CONSTRUCTION: `FRandomStream F(int32 DungeonSeed, ...)` names parameters.
            if re.match(r"^(const\s+)?(int32|uint32|FRandomStream)\b", seed):
                continue
            found.append((path.name, text.count("\n", 0, match.start()) + 1, seed))
    return found


def test_no_stream_is_seeded_from_related_seeds():
    """THE SWEEP GUARD. A seed made with XOR, or with `MixedSeed`, gives streams a fixed distance apart; every stream
    takes its seed from `CataclysmSeedMix::Mix`, `SeedForFloor`, a hash, a save's stored state or a single seed of its
    own. THE FIRST ASSERTION SHOWS THE SCAN FOUND THE STREAMS: it read 49 stream seeds at the slice, measured."""
    seeds = stream_seeds()
    assert len(seeds) > 40, f"the scan found only {len(seeds)} stream seeds; it no longer reads the code"
    assert any("CataclysmSeedMix::Mix" in seed for _, _, seed in seeds), "the scan sees no mixed seed at all"
    related = [(name, line, seed) for name, line, seed in seeds
               if ("^" in seed and "CataclysmSeedMix::Mix" not in seed.split("^")[0])
               or ("MixedSeed(" in seed and (name, seed) not in ALLOWED_MIXED_SEED
                   and not any(seed.endswith(allowed) for file, allowed in ALLOWED_MIXED_SEED if file == name))]
    assert not related, (
        f"{len(related)} of {len(seeds)} stream seeds are made by XOR or MixedSeed, which give related streams; seed "
        f"them with CataclysmSeedMix::Mix (issue #2749's placement seeding slice): {related}")
