"""An attribute that scales a stat no class supplies is reported, with no exemption.

WHY THIS EXISTS. Issue #2004. `validate_stat_names` in
`tools/generate_datatables.py` prints a note for every stat an attribute scales
and no class supplies a base for, because an attribute point only ever scales
and does nothing until gear, a weapon or a skill supplies the base. Cooldown
reduction was exempted from it as a "rate" whose class base of zero was
correct. Issue #2000 found that wrong: gear supplies cooldown reduction as a
FLAT value (the `Haste` affix) and the Efficacy attribute scales it, which is
exactly the case the note describes. So the exemption was removed.

Nothing tested the function before this, so the exemption could come back, or
the function could stop reporting anything, and no test would notice.
"""

from __future__ import annotations

import csv
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def read(name: str) -> list[dict]:
    with (DATA / f"{name}.csv").open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def reported(tables: dict[str, list[dict]]) -> set[str]:
    """The stat each note names, read from the quoted name the note opens with."""
    return {note.split("'")[1] for note in gen.validate_stat_names(tables)}


def test_a_stat_only_an_attribute_names_is_reported_and_one_a_class_supplies_is_not() -> None:
    tables = {
        "ClassStats": [{"Stat": "armor"}],
        "Attributes": [{"Stat": "armor"}, {"Stat": "cooldown_reduction"}],
    }
    assert reported(tables) == {"cooldown_reduction"}


def test_cooldown_reduction_is_reported_from_the_shipped_sheets() -> None:
    """The shipped data: Efficacy scales cooldown reduction and no class supplies it.

    Both halves are asserted from the sheets before the note is, so this fails
    for the right reason if the data changes rather than passing for a wrong
    one: if a class ever supplies a cooldown reduction base, the note is
    rightly silent and this test says why.
    """
    tables = {"ClassStats": read("ClassStats"), "Attributes": read("Attributes")}
    assert "cooldown_reduction" in {row["Stat"] for row in tables["Attributes"]}, (
        "no attribute scales cooldown_reduction any more")
    assert "cooldown_reduction" not in {row["Stat"] for row in tables["ClassStats"]}, (
        "a class now supplies a cooldown_reduction base, so the note is rightly "
        "silent for it; this test's premise no longer holds")
    assert "cooldown_reduction" in reported(tables), (
        "cooldown_reduction is scaled by Efficacy and supplied by no class, and "
        "the generator no longer says so. Issue #2004 removed the exemption "
        "that hid it; check validate_stat_names for one put back")
