"""The content relief at tier 2 and up: the same three numbers in the model and
the game, and a visible statement that the game does not apply them yet.

WHAT THE RELIEF IS. Issue #1425. The project owner, 2026-10-29: "Weaken tier
content", then "Relief grows by tier"; 2026-10-30: "Wins fall every tier", which
moved its start from tier 4 down to tier 2 on the same line. The model lowers
every enemy score by

    content_relief_tier_widths + content_relief_per_tier_widths x (tier - 2)

tier widths in its comparison with the player's power, at difficulty tier 2 and
up (`TuningConfig.content_relief`, read by `Simulation.death_chance` and
`Simulation.dungeon_power`). The game carries the same numbers as
`UCataclysmEnemyScore::ContentRelief*` in
`game/Source/Cataclysm/Dungeon/CataclysmEnemyScore.h`.

WHY THE GAME DOES NOT APPLY THEM. Creature health and damage in the game are
fixed settings that do not follow the enemy score until issue #1569 builds that
conversion, and the score itself is also the experience a kill grants, which the
ruling does not lower. So the constants have no reader in the game, and
`TestTheGameDoesNotApplyItYet` says so. When #1569's conversion reads them, that
class fails, and the right change is to replace it with a check that the
conversion subtracts the relief -- not to delete it.

THE PRECEDENT. `docs/DECISIONS.md`, 2026-10-16: the model's surge and damage
rows moved first, a check pinned the game's old values until the game followed,
and the check was then changed to hold the two equal.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_SOURCE = REPO_ROOT / "game" / "Source"
HEADER = GAME_SOURCE / "Cataclysm" / "Dungeon" / "CataclysmEnemyScore.h"

#: Game constant -> `TuningConfig` field.
NUMBERS = {
    "ContentReliefTierWidths": "content_relief_tier_widths",
    "ContentReliefPerTierWidths": "content_relief_per_tier_widths",
    "ContentReliefFromTier": "content_relief_from_tier",
}


def code_only(text: str) -> str:
    """C++ with its comments taken out, so a comment that names a constant is
    not read as code that uses it."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


@pytest.fixture(scope="module")
def header() -> str:
    if not HEADER.is_file():
        pytest.skip(f"{HEADER.name} is not present")
    return HEADER.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def constant(header: str, name: str) -> float:
    found = re.search(
        rf"static\s+constexpr\s+[\w:]+\s+{name}\s*=\s*([-\d.]+)\s*;", header)
    if not found:
        pytest.fail(f"could not find {name} in {HEADER.name}; has it been renamed?")
    return float(found.group(1))


class TestTheNumbersMatch:
    def test_every_relief_number_matches(self, header, model):
        for game_name, model_name in NUMBERS.items():
            game = constant(header, game_name)
            expected = float(getattr(model, model_name))
            assert game == pytest.approx(expected), (
                f"{game_name}: the game has {game}, TuningConfig.{model_name} "
                f"has {expected}. The two are one ruling (#1425) and move "
                "together.")

    def test_both_give_the_ruled_relief_at_every_tier(self, header, model):
        """The formula, worked through from the game's numbers and compared
        with the model's own method, so a game constant that matched by
        accident in one field would still be caught here."""
        base = constant(header, "ContentReliefTierWidths")
        step = constant(header, "ContentReliefPerTierWidths")
        first = int(constant(header, "ContentReliefFromTier"))
        for tier in range(1, 9):
            game = 0.0 if tier < first else base + step * (tier - first)
            assert game == pytest.approx(model.content_relief(tier)), tier
        assert [round(model.content_relief(t), 2) for t in range(1, 9)] == [
            0.0, 0.19, 0.32, 0.45, 0.58, 0.71, 0.84, 0.97], (
            "the relief the coordinating session ruled on 2026-10-29, continued "
            "down to tier 2 on 2026-10-30, has moved")


class TestTheGameDoesNotApplyItYet:
    """THE GAP, STATED. Issue #1569 has not landed, so nothing in the game
    reads the relief, and a creature at tier 2 is as strong as before."""

    def test_no_game_code_reads_the_relief(self):
        if not GAME_SOURCE.is_dir():
            pytest.skip("game/Source is not present")
        readers = []
        for path in sorted(GAME_SOURCE.rglob("*")):
            if path.suffix not in (".h", ".cpp") or path == HEADER:
                continue
            code = code_only(path.read_text(encoding="utf-8", errors="replace"))
            if re.search(r"\bContentRelief\w*", code):
                readers.append(path.relative_to(REPO_ROOT).as_posix())
        assert not readers, (
            f"{readers} now read the content relief. If that is issue #1569's "
            "conversion from enemy score to creature strength, the gap this "
            "class states has closed: replace it with a check that the "
            "conversion subtracts the relief, and update the comment beside "
            "the constants in CataclysmEnemyScore.h.")

    def test_the_header_reads_it_only_where_it_declares_it(self, header):
        """`ScoreFor` and the rest of the header name no relief outside the
        three declarations, so the experience a kill grants is untouched."""
        code = code_only(header)
        uses = re.findall(r"\bContentRelief\w*", code)
        assert sorted(uses) == sorted(NUMBERS), (
            f"{HEADER.name} names the relief {len(uses)} times in code; "
            f"only its three declarations were expected: {uses}")
