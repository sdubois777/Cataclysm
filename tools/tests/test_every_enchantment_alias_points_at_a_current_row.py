"""Every renamed enchantment row points at a row that exists, in one step.

Issue #1799, the owner's decision of 2026-09-30: build a name alias.
`FCataclysmEnchantmentRenames::Aliases()` maps the name a saved item may hold
to the row it means now, and `FCataclysmSaveStorage::FromJson` applies it to
every record it reads. Three mistakes in that table would each leave a saved
item carrying a name that finds nothing, silently:

- a target that is not a current row, which moves the item from one dead name
  to another;
- a target that is itself an old name, a chain, which the loader follows only
  one step;
- an old name that is still a current row, which would move an item off a row
  that still exists.

Each is refused here, and each is exercised on a made-up table as well, so the
checks cannot pass by reading a table that happens to be clean.
"""
import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLES = (REPO_ROOT / "game" / "Data" / "EnchantmentsNegative.csv",
          REPO_ROOT / "game" / "Data" / "EnchantmentsPositive.csv")

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def faults(aliases: dict[str, str], current: set[str]) -> list[str]:
    """What is wrong with this alias table against these current row names."""
    out = []
    for old, new in sorted(aliases.items()):
        if new not in current:
            out.append(f"{old} -> {new}: the target is not a current row")
        if new in aliases:
            out.append(f"{old} -> {new}: the target is itself renamed, a chain")
        if old in current:
            out.append(f"{old} -> {new}: the old name is still a current row")
    return out


@pytest.fixture(scope="module")
def current() -> set[str]:
    names: set[str] = set()
    for path in TABLES:
        if not path.is_file():
            pytest.skip(f"{path.name} has not been generated")
        with path.open(encoding="utf-8-sig", newline="") as handle:
            names |= {row["Name"] for row in csv.DictReader(handle)}
    assert names, "the enchantment tables are empty"
    return names


def test_every_alias_points_at_a_current_row_in_one_step(current):
    aliases = gen.enchantment_aliases()
    assert not faults(aliases, current), "; ".join(faults(aliases, current))


def test_the_checks_refuse_each_mistake():
    """On a made-up table, so the three refusals are exercised whatever the real
    one holds."""
    current = {"Positive_New", "Positive_Kept"}
    assert faults({"Positive_Old": "Positive_New"}, current) == []
    assert faults({"Positive_Old": "Positive_Gone"}, current) == [
        "Positive_Old -> Positive_Gone: the target is not a current row"]
    assert "Positive_Old -> Positive_Mid: the target is itself renamed, a chain" in faults(
        {"Positive_Old": "Positive_Mid", "Positive_Mid": "Positive_New"},
        current | {"Positive_Mid"})
    assert faults({"Positive_Kept": "Positive_New"}, current) == [
        "Positive_Kept -> Positive_New: the old name is still a current row"]
