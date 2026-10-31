"""The figures the game's creature-strength test checks against are the simulation's current ones.

Issue #1569. `game/Tests/EnemyStrength/ExpectedFigures.csv` is written by
`tools/generate_enemy_strength_fixture.py` from `enemy_stats.strength_on_floor`, and the
automation test `Cataclysm.EnemyStrength.EveryFigureIsTheModels` builds every row in the
game and compares. That only holds the two to one model while the file is current: a
change to the conversion, the relief, the minimum or the damage scale in the simulation
that is not regenerated would leave the game checked against the old model, passing.
"""
from __future__ import annotations

import generate_enemy_strength_fixture as fixture


def test_the_committed_figures_are_what_the_model_gives_now():
    assert fixture.FIXTURE.is_file(), (
        f"{fixture.FIXTURE} is missing. Run python tools/generate_enemy_strength_fixture.py.")
    committed = fixture.FIXTURE.read_text(encoding="utf-8").replace("\r\n", "\n")
    assert committed == fixture.text(), (
        "game/Tests/EnemyStrength/ExpectedFigures.csv is not what the simulation's conversion gives now, so the game "
        "would be checked against a stale model. Run python tools/generate_enemy_strength_fixture.py and commit it. "
        "Issue #1569.")


def test_the_figures_cover_every_creature_a_floor_places_at_every_rarity():
    lines = fixture.text().splitlines()[1:]
    kinds = {line.split(",")[0] for line in lines}
    steps = {int(line.split(",")[1]) for line in lines}
    assert kinds == {row for row, _ in fixture.KINDS}
    assert steps == set(range(6))
