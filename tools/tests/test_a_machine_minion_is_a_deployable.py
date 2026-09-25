"""A minion type is a Machine exactly when it is tagged Type.Deployable.

WHY THIS EXISTS. Issue #1833, deployable Part 1. The game reads one fact about
a minion type two ways: `ACataclysmMinion::bIsMachine` from the row's Family
column (A Second Self leaves machines out), and `ACataclysmMinion::IsDeployable`
from its Tags column (the gadget enchantment rows are scoped to
`Type.Deployable`). Both are kept, ruled 2026-09-25 by the coordinating session
under the owner's delegation. A row that gained one and not the other would make
a turret that A Second Self skips and the gadget rows miss, or the reverse, with
no error anywhere. This check keeps the two columns in step in
`game/Data/MinionTypes.csv`.
"""

from __future__ import annotations

import csv
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
MINION_TYPES = ROOT / "game" / "Data" / "MinionTypes.csv"


def rows() -> list[dict]:
    with MINION_TYPES.open(encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def is_machine(row: dict) -> bool:
    return row["Family"].strip().lower() == "machine"


def is_deployable(row: dict) -> bool:
    return "Type.Deployable" in [t.strip() for t in row["Tags"].split(",")]


def disagreeing(table: list[dict]) -> list[str]:
    """Each row whose Family and Tags disagree, with what each column says."""
    return [f"{r['Name']}: Family {r['Family']!r}, "
            f"{'tagged' if is_deployable(r) else 'not tagged'} Type.Deployable"
            for r in table if is_machine(r) != is_deployable(r)]


def test_family_machine_goes_with_the_deployable_tag():
    wrong = disagreeing(rows())
    assert not wrong, (
        "these minion types are a Machine by Family and not Type.Deployable by "
        "Tags, or the reverse; the game reads both: " + "; ".join(wrong))


def test_the_table_holds_both_kinds():
    """THE SCOPE: a table of only creatures would pass the check above and say
    nothing. Today three machines and two creatures."""
    table = rows()
    assert any(is_machine(r) for r in table), "no Machine row to check"
    assert any(not is_machine(r) for r in table), "no non-Machine row to check"


def test_a_row_with_the_family_and_no_tag_is_named():
    """THE CHECK CAN FAIL: a Machine with no Type.Deployable is named."""
    broken = [{"Name": "TestTurret", "Family": "Machine", "Tags": "Type.Minion"}]
    assert disagreeing(broken) == [
        "TestTurret: Family 'Machine', not tagged Type.Deployable"]
