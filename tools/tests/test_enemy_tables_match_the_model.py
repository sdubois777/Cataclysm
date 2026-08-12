"""`game/Data/EnemyArchetypes.csv` and `EnemyRarities.csv` must say what the
simulation's enemy model says.

WHY THIS IS NOT THE SAME CHECK AS `--check`, and why both are needed.
`python tools/generate_datatables.py --check` rebuilds the tables from
`sim/cataclysm_sim/enemy_stats.py` and compares the result with the files on
disk. That catches somebody editing the model without regenerating, and somebody
editing a CSV by hand. It cannot catch a defect in the GENERATOR, because it
compares the generator's output against the generator's output: change
`enemy_archetypes` to write `crit_chance` into the CritMultiplierPercent column
and `--check` stays green, because both sides move together.

That is the shape of a guard that cannot fail, which this project has been bitten
by twice. So this file reads the CSV as text and compares it against the model
directly, with the generator on neither side of the comparison.

THE STRONGEST TEST HERE IS THE FIELD ONE.
`test_every_field_of_the_archetype_dataclass_has_a_column` walks the `Archetype`
dataclass and fails when a field has no column. A designed number that never
reaches the engine is the failure mode this whole table exists to end, and it is
silent by nature: nothing anywhere raises when a field is simply not written out.

UNITS. The model works in metres and seconds and so do these CSVs, matching
`game/Data/ClassStats.csv`, which already states the player's movement speed in
metres per second. The engine multiplies by `CentimetresPerMetre` when it reads
one. Nothing here converts, so nothing here can convert wrongly.

ROUNDING. The generator writes six decimal places, because a rarity multiplier
like 0.5 * 1.85 ** 3 otherwise needs seventeen significant digits to round-trip.
The comparisons below allow for exactly that and no more.
"""

from __future__ import annotations

import csv
import dataclasses
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "game" / "Data"
ARCHETYPES_CSV = DATA_DIR / "EnemyArchetypes.csv"
RARITIES_CSV = DATA_DIR / "EnemyRarities.csv"

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: What the generator rounds to. A comparison looser than this would pass on a
#: table written at the wrong precision; tighter would fail on a correct one.
TOLERANCE = 5e-7


def model():
    from cataclysm_sim import enemy_stats
    return enemy_stats


#: Each field of the `Archetype` dataclass against the column that carries it.
#:
#: THIS MAPPING IS ITSELF CHECKED, by the test below, against the dataclass's own
#: field list. A mapping maintained by hand is a second copy of the model and
#: would drift like any other; what stops it is that adding a field to
#: `Archetype` without adding a line here fails, and so does removing one.
#:
#: The column names differ from the field names on purpose: a column says its
#: unit, because the metre-against-centimetre confusion is the one this pipeline
#: is most likely to make and the least likely to notice.
FIELD_TO_COLUMN = {
    "name": "ArchetypeName",
    "role": "Role",
    "cataclysm": "Cataclysm",
    "health_share": "HealthShare",
    "damage_share": "DamageShare",
    "armor_share": "ArmorShare",
    "attack_interval": "AttackIntervalSeconds",
    "crit_chance": "CritChancePercent",
    "crit_multiplier": "CritMultiplierPercent",
    "move_speed": "MoveSpeedMetresPerSecond",
    "chase_speed": "ChaseSpeedMetresPerSecond",
    "evasion": "EvasionPercent",
    "energy_shield_fraction": "EnergyShieldFraction",
    "body_radius": "BodyRadiusMetres",
    "turn_rate_degrees": "TurnRateDegreesPerSecond",
    "resistance": "ResistancePercent",
}

#: The two fields that are words rather than numbers, so a value comparison
#: compares strings for these and floats for everything else.
TEXT_FIELDS = {"name", "role", "cataclysm"}


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    assert path.is_file(), (
        f"{path.relative_to(REPO_ROOT)} does not exist. Run "
        "`python tools/generate_datatables.py`.")
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def archetype_rows() -> dict[str, dict[str, str]]:
    return {row["ArchetypeName"]: row for row in read_csv(ARCHETYPES_CSV)}


@pytest.fixture(scope="module")
def rarity_rows() -> dict[str, dict[str, str]]:
    return {row["RarityName"]: row for row in read_csv(RARITIES_CSV)}


# --------------------------------------------------------------------------
# the archetype table
# --------------------------------------------------------------------------

def test_every_field_of_the_archetype_dataclass_has_a_column() -> None:
    """A designed number with no column never reaches the engine, silently.

    This is the whole point of the table. Nothing raises when a field is simply
    not written out: the CSV is well-formed, the import succeeds, the DataTable
    loads, and the number is just absent. Adding a field to `Archetype` and
    forgetting the generator is the easiest possible version of that mistake,
    so it fails here.
    """
    designed = {field.name for field in dataclasses.fields(model().Archetype)}
    mapped = set(FIELD_TO_COLUMN)

    assert designed == mapped, (
        "the Archetype dataclass and this file's FIELD_TO_COLUMN disagree.\n"
        f"  designed but not carried: {sorted(designed - mapped)}\n"
        f"  carried but not designed: {sorted(mapped - designed)}\n"
        "Add the field to FIELD_TO_COLUMN, to `enemy_archetypes` in "
        "tools/generate_datatables.py, and to FCataclysmEnemyArchetypeRow in "
        "game/Source/Cataclysm/Data/CataclysmDataRows.h, then regenerate.")


def test_every_mapped_column_is_in_the_written_table(archetype_rows) -> None:
    """The mapping above is only worth anything if the columns really exist."""
    written = set(next(iter(archetype_rows.values())))
    missing = sorted(set(FIELD_TO_COLUMN.values()) - written)

    assert not missing, (
        f"EnemyArchetypes.csv has no column(s) {missing}, which the Archetype "
        "dataclass designs. Run `python tools/generate_datatables.py`.")


def test_every_archetype_in_the_model_has_a_row(archetype_rows) -> None:
    designed = set(model().ARCHETYPES)
    written = set(archetype_rows)

    assert designed == written, (
        "EnemyArchetypes.csv does not hold the archetypes the model designs.\n"
        f"  designed but not written: {sorted(designed - written)}\n"
        f"  written but not designed: {sorted(written - designed)}\n"
        "Run `python tools/generate_datatables.py`.")


def test_every_archetype_value_matches_the_model(archetype_rows) -> None:
    """Field by field, every creature, with the generator on neither side."""
    for name, kind in model().ARCHETYPES.items():
        row = archetype_rows[name]
        for field, column in FIELD_TO_COLUMN.items():
            designed = getattr(kind, field)
            written = row[column]

            if field in TEXT_FIELDS:
                assert written == designed, (
                    f"{name}: EnemyArchetypes.csv column {column} reads "
                    f"{written!r} and enemy_stats.py designs {designed!r}")
            else:
                assert float(written) == pytest.approx(designed, abs=TOLERANCE), (
                    f"{name}: EnemyArchetypes.csv column {column} reads "
                    f"{written} and enemy_stats.py designs {designed}. The "
                    "model is authoritative; regenerate rather than editing "
                    "the CSV.")


def test_the_brutes_chase_speed_survives_into_the_table(archetype_rows) -> None:
    """One creature checked by name, because a loop over a model compared
    against itself can pass while carrying nothing anyone recognises.

    The Brute is the one enemy in the slice with a second speed, so it is the
    one whose ChaseSpeedMetresPerSecond is not the sentinel zero. If this
    column ever became a copy of MoveSpeed, or were dropped, every other
    assertion in this file would still pass on six of the seven creatures.
    """
    row = archetype_rows["Brute"]
    chase = float(row["ChaseSpeedMetresPerSecond"])
    walk = float(row["MoveSpeedMetresPerSecond"])

    assert chase > walk, (
        f"the Brute's chase speed reads {chase} and its patrol speed {walk}. "
        "The Brute is designed to lumber until it notices the player and then "
        "commit, so the chase speed must be the larger of the two.")
    assert chase == pytest.approx(model().ARCHETYPES["Brute"].chase_speed,
                                  abs=TOLERANCE)


def test_every_other_creature_uses_the_sentinel_chase_speed(archetype_rows) -> None:
    """Zero means "the same as MoveSpeed", so it must not be read as a speed.

    Written out unchanged rather than expanded to the patrol speed, because
    expanding it in the generator would be the generator deciding what the model
    meant. Whatever reads this table has to know the sentinel; this records that
    it is still a sentinel and not an accident.
    """
    for name, kind in model().ARCHETYPES.items():
        if name == "Brute":
            continue
        written = float(archetype_rows[name]["ChaseSpeedMetresPerSecond"])
        assert written == 0.0, (
            f"{name} writes a chase speed of {written}. Only the Brute has a "
            "second speed today; every other creature carries the zero "
            "sentinel meaning it uses MoveSpeed in both states.")
        assert kind.chase_speed == 0.0


def test_the_baseline_row_is_the_neutral_one(archetype_rows) -> None:
    """Baseline is not a creature. Its three shares are 1 by definition, which
    is what makes the rarity ladder readable without an archetype on top."""
    row = archetype_rows["Baseline"]
    for column in ("HealthShare", "DamageShare", "ArmorShare"):
        assert float(row[column]) == pytest.approx(1.0), (
            f"the Baseline row's {column} reads {row[column]}. Baseline exists "
            "so the rarity ladder can be read with every share at 1; a share "
            "of anything else makes it a creature rather than a reference.")


# --------------------------------------------------------------------------
# the rarity table
# --------------------------------------------------------------------------

def test_every_rarity_in_the_ladder_has_a_row(rarity_rows) -> None:
    designed = list(model().RARITY_ORDER)
    written = list(rarity_rows)

    assert designed == written, (
        f"EnemyRarities.csv holds {written} and the model's ladder is "
        f"{designed}. Run `python tools/generate_datatables.py`.")


def test_the_engines_first_boss_step_is_the_models() -> None:
    """`ACataclysmEnemyCharacter::FirstBossRarityStep` against the ladder.

    THE STUN RULE HANGS ON THIS ONE INTEGER. "A boss cannot be stunned at all"
    is checked in the engine as `RarityStep >= FirstBossRarityStep`, and
    continuous integration builds no C++, so a drifted copy would silently
    move which rarities are stun-immune. Read out of the header as text, the
    way every other C++ constant is pinned. Issue #395.
    """
    import re

    from cataclysm_sim.enemy_stats import (FIRST_BOSS_RARITY, is_boss_rarity,
                                           rarity_step)

    header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmEnemyCharacter.h").read_text(encoding="utf-8")
    found = re.search(
        r"static\s+constexpr\s+int32\s+FirstBossRarityStep\s*=\s*(\d+)\s*;",
        header)
    assert found, (
        "CataclysmEnemyCharacter.h no longer carries "
        "'static constexpr int32 FirstBossRarityStep = <n>;'. If it was "
        "renamed, rename it here; if deleted, the boss stun rule is unpinned "
        "and can drift from the rarity ladder unnoticed.")

    designed = rarity_step(FIRST_BOSS_RARITY)
    assert int(found.group(1)) == designed, (
        f"FirstBossRarityStep is {found.group(1)} in the engine and the model "
        f"puts {FIRST_BOSS_RARITY!r} at step {designed}. The stun rule's boss "
        "line has drifted between the two copies.")

    # AND THE BOUNDARY MEANS WHAT THE DESIGN SAYS: Herald -- the Abyssal
    # Warden's reference rarity, a mini-boss -- is stunnable, and both Boss
    # tiers are not.
    assert not is_boss_rarity("Herald")
    assert is_boss_rarity("Boss") and is_boss_rarity("Cataclysm Boss")


def test_the_ladder_has_no_rare_tier(rarity_rows) -> None:
    """Six rarities, with Herald and Cataclysm Boss and no Rare.

    The design document's own list is the superseded one; `scoring.RARITY_WEIGHTS`
    is authoritative and issue #30 is where that was settled. Worth pinning
    because anyone reading the design document alone would add Rare back.
    """
    assert "Rare" not in rarity_rows, (
        "EnemyRarities.csv has a Rare row. The ladder is Common, Elite, "
        "Legendary, Herald, Boss, Cataclysm Boss, matching "
        "scoring.RARITY_WEIGHTS. See issue #30.")
    assert len(rarity_rows) == 6


def test_each_rarity_records_its_step(rarity_rows) -> None:
    """A DataTable is a map and its rows have no order, so the ladder's order
    only survives into the engine if each row carries its own position."""
    for index, rarity in enumerate(model().RARITY_ORDER):
        written = int(rarity_rows[rarity]["Step"])
        assert written == index, (
            f"{rarity} records step {written} and sits at position {index} in "
            "the model's ladder.")


def test_every_multiplier_is_the_models_arithmetic(rarity_rows) -> None:
    """The three scaled stats, at every rarity, recomputed from the constants.

    `stats_for` computes each as `score * <stat>PerScore * archetype.<stat>Share`,
    so this is the whole of what the rarity layer contributes.
    """
    stats = model()
    expected = {
        "HealthPerScore": (stats.HEALTH_AT_COMMON, stats.HEALTH_PER_STEP),
        "DamagePerScore": (stats.DAMAGE_AT_COMMON, stats.DAMAGE_PER_STEP),
        "ArmorPerScore": (stats.ARMOR_AT_COMMON, stats.ARMOR_PER_STEP),
    }

    for rarity in stats.RARITY_ORDER:
        step = stats.rarity_step(rarity)
        for column, (at_common, per_step) in expected.items():
            written = float(rarity_rows[rarity][column])
            designed = at_common * per_step ** step
            assert written == pytest.approx(designed, abs=TOLERANCE), (
                f"{rarity}: EnemyRarities.csv column {column} reads {written} "
                f"and the model computes {designed} from {at_common} times "
                f"{per_step} to the power {step}.")


def test_a_written_multiplier_reproduces_the_models_own_stat_block() -> None:
    """The table times a share must equal what `stats_for` returns.

    The test above compares the multiplier against the two constants it was
    built from, which shares an assumption with the generator: that
    `score * AT_COMMON * PER_STEP ** step * share` is how the model combines
    them. This one removes that assumption by calling `stats_for` itself and
    comparing the answer, so a change to how the model combines its layers
    fails here even though both files would still agree on the constants.
    """
    stats = model()
    rows = {row["RarityName"]: row for row in read_csv(RARITIES_CSV)}
    shares = {row["ArchetypeName"]: row for row in read_csv(ARCHETYPES_CSV)}

    score = 1000.0
    for rarity in stats.RARITY_ORDER:
        for name in stats.ARCHETYPES:
            block = stats.stats_for(rarity, score, name, tier=8)

            from_table = (score
                          * float(rows[rarity]["DamagePerScore"])
                          * float(shares[name]["DamageShare"]))
            assert from_table == pytest.approx(block.damage_per_hit, rel=1e-5), (
                f"{rarity} {name}: the two tables give {from_table} damage per "
                f"hit at score {score} and enemy_stats.stats_for gives "
                f"{block.damage_per_hit}.")

            from_table = (score
                          * float(rows[rarity]["ArmorPerScore"])
                          * float(shares[name]["ArmorShare"]))
            assert from_table == pytest.approx(block.armor, rel=1e-5), (
                f"{rarity} {name}: the two tables give {from_table} armour at "
                f"score {score} and enemy_stats.stats_for gives {block.armor}.")
