"""Every condition name a data sheet may write is mentioned in `docs/DECISIONS.md`.

WHY THIS EXISTS. A condition is a rule about when a modifier applies, and adding
one is a design decision: what it asks, which direction it takes when it cannot
read its subject, and whether it compares a number at all. `CLAUDE.md` says a
design decision is not real until it is in `docs/`, and **nothing checked that a
change adding a condition wrote one down.**

IT HAD ALREADY FAILED TWICE WHEN THIS LANDED, and neither miss was noticed at the
time. Measured on `c63f01fb`, before the entry that accompanies this file:

    condition names the generator recognises          28
    of those, absent from docs/DECISIONS.md            2
        class_resource_at_maximum
        energy_shield_at_maximum

**Both are the same question about a different pool -- "is this bar full" -- so
the gap was not one change forgetting. No condition of that kind had ever been
recorded**, and every argument about how they behave existed only as a comment in
`CataclysmStatPipeline.cpp`.

WHY AN OMISSION IS THE HARD CASE. A review derives its checks from the diff: rows
added, counts moved, tests written. **An omission leaves no diff to derive a
check from**, so a control set built that way is structurally blind to it. The
pull request that added `energy_shield_at_maximum` was reviewed carefully -- the
session that reviewed it reports running 21 separate controls, a figure taken
from its report rather than measured here -- and **none of them could have asked
this question**, which is demonstrable from the outcome: the entry was missing
and the change merged anyway. This check is derived from the KIND of change
instead: if the vocabulary grew, the log must say so.

WHAT THIS DELIBERATELY DOES NOT DO. It cannot tell a real entry from the name
appearing in a passing sentence, and it does not try -- a name mentioned nowhere
is the fault that has actually happened twice, and a name mentioned dishonestly
has not happened at all. It also does not cover every kind of unrecorded
decision, only this class.

SCALE NAMES ARE THE SAME GAP AND ARE NOT CHECKED HERE. Two of the ten are absent,
`health_missing` and `health_owed`, and issue #1759 carries them. They are left
out on purpose: **a check that needs four gaps closed before it can be added
tends not to be added at all**, and this one is worth having for conditions now.
Widen it when that issue closes.
"""
from __future__ import annotations

import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

sys.path.insert(0, str(REPO_ROOT / "tools"))

from generate_datatables import CONDITIONS  # noqa: E402


@pytest.fixture(scope="module")
def decisions_text() -> str:
    return DECISIONS.read_text(encoding="utf-8")


def test_the_condition_list_was_actually_read():
    """A positive control, so an empty list cannot pass as "nothing missing".

    THE FAILURE THIS GUARDS IS SILENT. `CONDITIONS` is imported rather than
    pattern-matched, so it cannot be misread the way a regular expression can --
    but an import that resolved to an empty mapping would make the check below
    pass over nothing and report success. The count is deliberately a floor
    rather than an equality: this file should not have to be edited every time a
    condition is added, which is the very thing it exists to encourage.
    """
    assert len(CONDITIONS) >= 28, (
        f"Only {len(CONDITIONS)} condition names were read from "
        "tools/generate_datatables.py. There were 28 on c63f01fb and the list "
        "only grows, so this is a broken import rather than a real shrink.")


def test_the_decisions_log_was_actually_read(decisions_text):
    """The other half of the control: the log has to be there to search."""
    assert len(decisions_text) > 10_000, (
        f"{DECISIONS} is {len(decisions_text)} characters, which is too short "
        "to be the design log. A check searching an empty string finds nothing "
        "missing and reports success.")


def test_every_condition_name_appears_in_the_decisions_log(decisions_text):
    """The check itself.

    WHAT TO DO WHEN THIS FAILS: write the entry, not an allowance list. The
    condition it names was added without recording what it asks, which direction
    it takes when it cannot read its subject, and whether it compares a number.
    Those are the three things the next person needs and the three that only the
    author knows.
    """
    missing = sorted(name for name in CONDITIONS if name not in decisions_text)

    assert not missing, (
        f"{len(missing)} condition name(s) a data sheet may write are not "
        f"mentioned anywhere in {DECISIONS.name}:\n"
        + "\n".join(f"  {name}" for name in missing)
        + "\n\nAdding a condition is a design decision. Record what it asks, "
        "which direction it takes when its reading is unknown, and whether it "
        "compares a number, in a dated entry naming the commit. Issue #1759.")
