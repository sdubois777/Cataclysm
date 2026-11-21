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


#: Issue #2787, branch B: the four enchantments that raised or lowered a count
#: cap, by the name an item saved before the change holds, and the row each
#: means now. The owner's ruling of 2026-11-20, "Lower the reserve instead".
COUNT_CAP_RENAMES = {
    "Positive_Add_2_4_to_your_maximum_minion_count":
        "Positive_Your_minions_reserve_15_30_reduced_Fervour",
    "Negative_Minus_2_4_to_your_max_minion_count":
        "Negative_Your_minions_reserve_25_65_increased_Fervour",
    "Positive_Add_1_3_to_your_max_deployable_count":
        "Positive_Your_deployables_reserve_15_30_reduced_Fervour",
    "Positive_Your_trap_skills_get_1_6_to_max_trap_count":
        "Positive_Your_traps_reserve_5_25_reduced_Fervour",
}

#: The names the first wording would have given them, "less" and "more", before
#: the coordinating session ruled "reduced" and "increased" on 2026-11-21
#: because the percentages reaching a kind add. Nothing shipped them, so no
#: alias may name them: an old save goes straight to the final row.
NEVER_SHIPPED = (
    "Positive_Your_minions_reserve_15_30_less_Fervour",
    "Negative_Your_minions_reserve_25_65_more_Fervour",
    "Positive_Your_deployables_reserve_15_30_less_Fervour",
    "Positive_Your_traps_reserve_5_25_less_Fervour",
)


def test_the_four_count_enchantments_of_an_old_save_resolve(current):
    """An item saved before issue #2787 holds the count enchantment's old name;
    the alias table, which the loader applies to every record it reads, takes
    it to the reserve row in one step, and that row is a current one."""
    aliases = gen.enchantment_aliases()
    for old, new in COUNT_CAP_RENAMES.items():
        assert old not in current, f"{old} is still a current row"
        assert aliases.get(old) == new, f"{old} resolves to {aliases.get(old)!r}, not {new!r}"
        assert new in current, f"{new} is not a current row"


def test_no_alias_names_the_wording_that_never_shipped(current):
    """No middle step: the first wording's names are neither current rows nor
    the source or target of any alias."""
    aliases = gen.enchantment_aliases()
    for name in NEVER_SHIPPED:
        assert name not in current, f"{name} is a current row"
        assert name not in aliases, f"{name} is an old name in the alias table"
        assert name not in aliases.values(), f"{name} is an alias target"
