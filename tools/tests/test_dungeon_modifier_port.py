"""The simulation's dungeon modifier table must match the game's data file.

WHY THIS EXISTS AT ALL. `sim/cataclysm_sim/modifiers.py` is a hand-maintained
copy of `game/Data/DungeonModifiers.csv`. Two copies of a table are two tables,
and this one had already drifted: it held **116 rows against the data file's
117** from the day issue #504 added the Corrupted Stalker until issue #1282, and
nothing in the repository compared them. `tools/tests/test_corrupted_stalker_
modifier.py` checked the data file, the workbook and the design document;
`game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp` pinned the data file's
row count; no test looked at the Python side at all.

`CLAUDE.md` carries a rule about exactly this arrangement, because the power
model in `sim/cataclysm_sim/scoring.py` silently drifted from its own source
twice. Four guards already existed for it and its neighbours --
`sim/verify_scoring_port.py`, `test_day_clock_port.py`, `test_surge_port.py` and
`test_empire_map_port.py`. The modifier table had none. This is the fifth.

WHY IT COMPARES TRIPLES AND NOT COUNTS. A count catches a row added or removed
and misses a row renamed or reweighted, which is the drift that does damage
quietly: a modifier's weight is summed into `modifier_score`, which raises a
dungeon's difficulty in `scoring.dungeon_score`. Every
`(CataclysmType, ModifierName, Weight)` is compared, in both directions.

WHAT IT DOES NOT CHECK. What the `Weight` column MEANS. This module reads it as
a danger score and `game/Source/Cataclysm/Data/CataclysmDataRows.h` calls it
"Selection weight. Higher is more common."; `docs/DECISIONS.md` settles neither
and that is issue #1298. The comparison here is numeric and holds whichever way
that is answered.

APOSTROPHES ARE COMPARED AS WRITTEN. Four names carry a straight apostrophe and
one, `Heaven's Quake`, carries a typographic one, on both sides. Normalising them
would hide a rename that changed only that character, which is precisely the kind
of edit nobody notices.
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

MODIFIER_CSV = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"

#: The Cataclysm Type that means "every Cataclysm draws this". Read from the
#: model rather than written out, so a rename there cannot leave this pointing
#: at a value nothing uses.
from cataclysm_sim.modifiers import (  # noqa: E402
    CATACLYSM_TYPES, GENERIC, MODIFIERS, pool_for,
)


def csv_rows() -> list[dict[str, str]]:
    if not MODIFIER_CSV.is_file():
        pytest.skip(f"{MODIFIER_CSV.name} is not present")
    with MODIFIER_CSV.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def rows() -> list[dict[str, str]]:
    return csv_rows()


@pytest.fixture(scope="module")
def csv_triples(rows) -> set[tuple[str, str, float]]:
    return {(r["CataclysmType"], r["ModifierName"], float(r["Weight"]))
            for r in rows}


@pytest.fixture(scope="module")
def model_triples() -> set[tuple[str, str, float]]:
    return {(cataclysm, name, float(weight))
            for cataclysm, entries in MODIFIERS.items()
            for name, weight in entries}


def describe(triples) -> str:
    return ", ".join(f"{c}/{n} at {w:g}" for c, n, w in sorted(triples))


class TestTheTwoTablesHoldTheSameRows:

    def test_the_model_is_missing_no_row_the_data_file_has(
            self, csv_triples, model_triples):
        """The failure this guard was written for. Issue #1282."""
        missing = csv_triples - model_triples
        assert not missing, (
            f"game/Data/DungeonModifiers.csv has {len(missing)} row(s) that "
            f"sim/cataclysm_sim/modifiers.py does not: {describe(missing)}. "
            "Add them to MODIFIERS with the same weight.")

    def test_the_model_has_no_row_the_data_file_lacks(
            self, csv_triples, model_triples):
        extra = model_triples - csv_triples
        assert not extra, (
            f"sim/cataclysm_sim/modifiers.py has {len(extra)} row(s) that "
            f"game/Data/DungeonModifiers.csv does not: {describe(extra)}. "
            "Either the data file lost them or the model invented them.")

    def test_neither_side_has_a_duplicate_row(self, rows, csv_triples):
        """A set comparison cannot see a row written twice, so count first."""
        assert len(rows) == len(csv_triples), (
            f"game/Data/DungeonModifiers.csv has {len(rows)} rows but only "
            f"{len(csv_triples)} distinct ones")
        model_count = sum(len(v) for v in MODIFIERS.values())
        distinct = len({(c, n) for c, entries in MODIFIERS.items()
                        for n, _ in entries})
        assert model_count == distinct, (
            f"MODIFIERS holds {model_count} entries but only {distinct} "
            "distinct (Cataclysm, name) pairs, so one is written twice")

    def test_the_row_counts_match(self, rows):
        model_count = sum(len(v) for v in MODIFIERS.values())
        assert model_count == len(rows), (
            f"sim/cataclysm_sim/modifiers.py holds {model_count} modifiers, "
            f"game/Data/DungeonModifiers.csv holds {len(rows)}")

    def test_every_cataclysm_type_holds_the_same_number_on_both_sides(
            self, rows):
        """Narrows a mismatch to one column rather than to the whole table."""
        wanted: dict[str, int] = {}
        for row in rows:
            wanted[row["CataclysmType"]] = wanted.get(
                row["CataclysmType"], 0) + 1
        got = {k: len(v) for k, v in MODIFIERS.items()}
        assert got == wanted, (
            f"per Cataclysm the model has {dict(sorted(got.items()))} and the "
            f"data file has {dict(sorted(wanted.items()))}")

    def test_a_weight_is_never_rounded_away(self, rows):
        """Weights are written as integers in the model and as decimals in the
        data file. The comparison is numeric, so 20 and 20.0 agree -- but a
        weight that is not a whole number would be lost by writing it as one,
        and this says so if that day comes."""
        for row in rows:
            weight = float(row["Weight"])
            assert weight == int(weight), (
                f"{row['ModifierName']} has a fractional weight of {weight}. "
                "The model writes weights as integers; write it as a float "
                "there and delete this test.")


class TestTheGenericColumn:
    """`Generic` means every Cataclysm draws it. Issue #504 chose that word and
    #1282 made the simulation honour it."""

    def test_the_data_file_has_a_generic_row(self, rows):
        assert [r for r in rows if r["CataclysmType"] == GENERIC], (
            "game/Data/DungeonModifiers.csv has no Generic modifier, so the "
            "pooling rule in modifiers.pool_for guards nothing. If the column "
            "was retired, retire that rule with it.")

    def test_the_model_has_the_same_generic_rows(self, rows):
        wanted = {r["ModifierName"] for r in rows
                  if r["CataclysmType"] == GENERIC}
        got = {name for name, _ in MODIFIERS.get(GENERIC, [])}
        assert got == wanted, (
            f"the model's Generic modifiers are {sorted(got)}, the data "
            f"file's are {sorted(wanted)}")

    def test_generic_is_a_cataclysm_type_of_the_model(self):
        assert GENERIC in CATACLYSM_TYPES

    def test_generic_is_not_in_the_roster_a_run_draws_from(self):
        """`CATACLYSM_ROSTER` is the eight a run activates. If Generic were in
        it, `pool_for` would add those rows twice."""
        from cataclysm_sim.config import TuningConfig
        assert GENERIC not in TuningConfig().CATACLYSM_ROSTER

    def test_a_generic_modifier_is_never_drawable(self):
        """THE RULING. The project owner said on 2026-09-05 that the Corrupted
        Stalker is "granted separately" and does not compete for one of a
        dungeon's modifier slots. A dungeon draws its modifiers from this pool,
        so anything in it competes for a slot by construction.

        This check used to assert the opposite, under issue #1282, on the
        reading that "drawable by every Cataclysm" meant "in every pool". Issue
        #1303 is that reading being rejected.
        """
        from cataclysm_sim.config import TuningConfig
        for count in (1, 2, 4, 8):
            pool = pool_for(TuningConfig().CATACLYSM_ROSTER[:count])
            for name, weight in MODIFIERS[GENERIC]:
                assert (name, weight) not in pool, (
                    f"{name} is drawable with {count} Cataclysm(s) active, so "
                    "it competes for a modifier slot. The owner ruled it is "
                    "granted separately. Issue #1303.")

    def test_the_pool_is_every_row_except_the_generic_ones(self, rows):
        """Stated as a count so a row lost from either side is caught here as
        well as by the triple comparison above."""
        from cataclysm_sim.config import TuningConfig
        pool = pool_for(TuningConfig().CATACLYSM_ROSTER)
        generic = len([r for r in rows if r["CataclysmType"] == GENERIC])
        assert len(pool) == len(rows) - generic, (
            f"with all eight Cataclysms active the pool holds {len(pool)} "
            f"modifiers; the data file holds {len(rows)} rows of which "
            f"{generic} are Generic, so the pool should hold "
            f"{len(rows) - generic}")

    def test_the_row_is_still_in_the_table_it_is_only_not_drawn(self, rows):
        """The distinction that is easy to get wrong. The table still has 117
        rows and the data file still has 117 rows; what changed is that one of
        them is no longer DRAWN. Deleting the row instead would break the
        comparison above and would misdescribe the design."""
        assert MODIFIERS.get(GENERIC), (
            "the Generic row was deleted from the model rather than left out "
            "of the pool. The design has 117 dungeon modifiers. Issue #1303.")
        assert sum(len(v) for v in MODIFIERS.values()) == len(rows)

    def test_nothing_else_grants_it(self):
        """Issue #1308. Taking it out of the pool leaves it granted by nothing,
        which is deliberate: the design says what the modifier does and never
        what causes it to appear, so a trigger here would be invented design.

        This check exists so that when a grant rule IS built, whoever builds it
        finds a test saying the gap was known rather than assuming an oversight.
        `_make_dungeon` is the only place a dungeon is given modifiers.
        """
        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py").read_text(
            encoding="utf-8")
        body = source[source.index("def _make_dungeon"):]
        body = body[:body.index("\n    def ")]
        assert "pool_for" in body or "modifier_pool" in body, (
            "_make_dungeon no longer draws its modifiers from the pool, so "
            "this check cannot tell whether the Corrupted Stalker is granted.")
        assert GENERIC not in body, (
            "_make_dungeon now mentions the Generic Cataclysm type, so "
            "something may grant the Corrupted Stalker after all. If a grant "
            "rule was built, issue #1308 is done and this check should say "
            "what the rule is instead of that there is none.")


class TestNothingStatesTheWrongRowCount:
    """A count written into prose is a third copy of the table's size.

    `game/Source/CataclysmEmpire/DayClock/CataclysmDayClock.h` said "the 116
    dungeon modifiers" for as long as the model did, because the row was added
    to the data file and to nothing else. Issue #1282 lists three such counts;
    two were already right by the time it was worked, and this is what keeps
    them that way.
    """

    #: Files that state the modifier count in prose, and must state it
    #: correctly. Add a file here rather than letting a fourth copy drift.
    SPEAKS_THE_COUNT = (
        "game/Source/CataclysmEmpire/DayClock/CataclysmDayClock.h",
        "game/Source/CataclysmEmpire/Empire/CataclysmSurge.h",
    )

    @pytest.mark.parametrize("relative", SPEAKS_THE_COUNT)
    def test_the_count_it_states_is_the_count_the_data_file_holds(
            self, relative, rows):
        import re
        path = REPO_ROOT / relative
        if not path.is_file():
            pytest.skip(f"{relative} is not present")
        text = path.read_text(encoding="utf-8")

        stated = re.findall(r"(\d+)\s+dungeon modifiers", text)
        assert stated, (
            f"{relative} no longer states a dungeon modifier count. If the "
            "sentence was removed, drop the file from SPEAKS_THE_COUNT; a "
            "check over a file that says nothing passes trivially.")

        for value in stated:
            assert int(value) == len(rows), (
                f"{relative} says there are {value} dungeon modifiers and "
                f"game/Data/DungeonModifiers.csv holds {len(rows)}")


class TestTheHeaderTheGuardReads:
    """If the data file's columns are renamed this guard stops comparing
    anything, and a guard that silently compares nothing is worse than none."""

    def test_the_columns_this_test_reads_are_present(self, rows):
        for column in ("CataclysmType", "ModifierName", "Weight"):
            assert column in rows[0], (
                f"game/Data/DungeonModifiers.csv no longer has a {column!r} "
                "column, so this guard cannot compare the tables. Update it.")

    def test_the_file_is_not_empty(self, rows):
        assert len(rows) > 100, (
            f"game/Data/DungeonModifiers.csv has only {len(rows)} rows, which "
            "is too few to be the modifier table. Comparing against it would "
            "pass trivially.")
