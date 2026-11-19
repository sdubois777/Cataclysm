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


def test_the_figures_cover_every_ring_at_tier_one_for_every_creature_and_rarity():
    """ISSUE #2749's RING SLICE: tier 1 damage rises by the ring the dungeon was rolled on, so the game is checked on
    every ring for every creature and rarity, and on a ring at tier 2, where the figure must be the Outpost's."""
    import csv

    rows = list(csv.DictReader(fixture.text().splitlines()))
    rings = {row["CityTier"] for row in rows}
    assert rings == {"Outpost", "Bulwark", "Sanctuary", "Pillar"}, rings
    for ring in ("Bulwark", "Sanctuary", "Pillar"):
        on_it = [row for row in rows if row["CityTier"] == ring and row["Tier"] == "1"]
        assert {(row["Archetype"], int(row["RarityStep"])) for row in on_it} == {
            (key, step) for key, _ in fixture.KINDS for step in range(6)}, ring
    flat = [row for row in rows if row["Tier"] == "2" and row["CityTier"] != "Outpost"]
    assert len(flat) == len(fixture.KINDS), flat


def test_a_rings_row_differs_from_its_outposts_in_damage_alone():
    """The same creature on the same floor, on a Bulwark and on an Outpost: the score, health and armour equal, the
    damage 0.11 / 0.05 times as much. At tier 2 on a Sanctuary, the damage equal as well."""
    import csv

    rows = list(csv.DictReader(fixture.text().splitlines()))
    key = ("Archetype", "RarityStep", "Tier", "DungeonType", "TotalFloors", "Floor", "ModifierScore")
    outpost = {tuple(row[k] for k in key): row for row in rows if row["CityTier"] == "Outpost"}
    compared = 0
    for row in rows:
        if row["CityTier"] == "Outpost":
            continue
        same = outpost[tuple(row[k] for k in key)]
        assert (row["StrengthScore"], row["Health"], row["Armour"]) == (
            same["StrengthScore"], same["Health"], same["Armour"]), row
        ratio = {"Bulwark": 0.11 / 0.05, "Sanctuary": 0.13 / 0.05, "Pillar": 0.10 / 0.05}[row["CityTier"]]
        expected = float(same["Damage"]) * (ratio if row["Tier"] == "1" else 1.0)
        assert abs(float(row["Damage"]) - expected) <= 1e-3 * max(1.0, expected), (row, same)
        compared += 1
    assert compared == 133
