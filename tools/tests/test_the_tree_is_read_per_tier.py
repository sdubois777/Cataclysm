"""No production file reads a per-tier tree field directly. Issue #1397.

**THE DEFECT THIS EXISTS TO PREVENT HAS ALREADY HAPPENED TWICE**, in the same
shape both times: a number that looks complete and is not.

  * Issue #1288 found a factor in `TREE_ARCHITECT_AS_DESIGNED` that matched no
    node in the design document at all.
  * Issue #1386 found `TREE_EXPLORER_AS_DESIGNED` removing 70 flat days where
    the branch removes 60, and crediting it with none of its depth nodes.

Issue #1397 makes a third instance possible and cheap to write by accident.
Three nodes in `docs/Empire_Development_Tree_Final.json` pay per ACTIVE CATACLYSM
TYPE, so `EmpireTree.run_days_flat`, `.floor_delta` and `.city_damage_mult` are
each now only the tier-INDEPENDENT half of an answer. A caller that reads one
directly gets a smaller number and no warning:

    days = base - cfg.tree.run_days_flat          # WRONG: misses Sovereign's Haste
    days = base - cfg.tree.days_removed(active)   # right

**A TEST OF BEHAVIOUR CANNOT COVER THIS AND THAT IS WHY THIS FILE IS A SOURCE
SEARCH.** A new call site added tomorrow in a branch nobody has written a
campaign test for would read the raw field, be wrong at every tier above 1, and
pass everything. `tools/tests/test_the_explorer_preset_matches_the_tree.py`
covers the two readers that exist today, end to end through the engine; this
covers the ones that do not exist yet.

WHAT IT DELIBERATELY DOES NOT COVER. Tests, which construct trees with keyword
arguments of the same names and legitimately assert on the raw fields, and
`config.py` itself, which is where the accessors live. The keyword arguments are
NOT renamed on purpose: `EmpireTree(run_days_flat=10)` still means a flat ten
days at every tier, which is exactly what a test that writes it wants.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

#: The fields that are half an answer, and the accessor that gives the whole
#: one. Named rather than derived, so adding a fourth per-type field without
#: adding it here is a decision somebody makes rather than an omission.
HALF_ANSWERS = {
    "run_days_flat": "days_removed(active_types)",
    "floor_delta": "floors_added(active_types)",
    "city_damage_mult": "damage_taken(active_types)",
}

#: Where `EmpireTree` and its accessors are defined. The arithmetic has to read
#: the fields somewhere, and this is that somewhere.
DEFINITION = REPO_ROOT / "sim" / "cataclysm_sim" / "config.py"

#: Every Python file the simulation actually runs. Tests are excluded above.
def production_files() -> list[pathlib.Path]:
    sim = REPO_ROOT / "sim"
    files = sorted(p for p in sim.rglob("*.py")
                   if "tests" not in p.parts
                   and "__pycache__" not in p.parts
                   and p != DEFINITION)
    assert files, "found no production files to search; the layout moved"
    return files


#: `something.run_days_flat` where `something` is not a keyword argument. A
#: keyword argument is `run_days_flat=`, with no dot before it, so requiring the
#: dot is what separates a read from a construction.
def reads_of(text: str, field: str) -> list[str]:
    """Lines reading `<something>.<field>`, EXCEPT `self.<field>`.

    `self.floor_delta` is a class reading its own attribute. `EmpireTree` does
    that inside its accessors and is excluded from the search anyway; so does
    `Shape` in `sim/analyse_explorer_shape.py`, which holds a floor delta of its
    own and passes it to a tree as a keyword argument. Neither is a read OF a
    tree. `self.tree.floor_delta` still matches, because the dot before the
    field follows `tree` and not `self`.
    """
    pattern = re.compile(rf"(?<!self)\.\s*{re.escape(field)}\b")
    return [line.strip() for line in text.splitlines() if pattern.search(line)]


@pytest.mark.parametrize("field,accessor", sorted(HALF_ANSWERS.items()))
def test_no_production_file_reads_it_directly(field, accessor):
    offenders = []
    for path in production_files():
        for line in reads_of(path.read_text(encoding="utf-8"), field):
            offenders.append(f"  {path.relative_to(REPO_ROOT).as_posix()}: {line}")

    assert not offenders, (
        f"`{field}` is read directly in production code:\n"
        + "\n".join(offenders)
        + f"\n\nIt is only the tier-INDEPENDENT half of the answer since issue "
          f"#1397. Three nodes in the design document pay per active Cataclysm "
          f"type, so read `EmpireTree.{accessor}` instead, passing "
          f"`cfg.active_cataclysm_count()`. Every existing call site already "
          f"has a config in scope.")


def test_the_search_finds_a_read_when_there_is_one():
    """**THE CONTROL.** A search that matched nothing because it was broken
    would report a clean tree, which is the failure this whole file is about.
    """
    sample = chr(10).join([
        "days = base - cfg.tree.run_days_flat",
        "tree = EmpireTree(name='x', run_days_flat=10)",
    ])
    hits = reads_of(sample, "run_days_flat")

    assert len(hits) == 1, (
        f"the search found {len(hits)} reads in a sample that holds exactly "
        "one read and one keyword argument")
    assert "cfg.tree.run_days_flat" in hits[0]
    assert "EmpireTree" not in hits[0], (
        "the search counted a constructor keyword argument as a read; those "
        "are legitimate and are how every preset and test builds a tree")


def test_the_accessors_exist_and_take_the_active_count():
    """The names this file tells people to use are the names that exist."""
    import inspect

    from cataclysm_sim.config import EmpireTree

    for accessor in ("days_removed", "floors_added", "damage_taken"):
        method = getattr(EmpireTree, accessor, None)
        assert callable(method), (
            f"EmpireTree has no {accessor}. This file's failure message tells "
            "people to call it; if it was renamed, rename it here too.")
        params = list(inspect.signature(method).parameters)
        assert params == ["self", "active_types"], (
            f"EmpireTree.{accessor} takes {params}, and this file tells people "
            "to pass the active Cataclysm count")


def test_the_definition_file_is_the_one_place_that_reads_them():
    """`config.py` is excluded from the search above, so this states why and
    checks the exclusion is still earning itself."""
    text = DEFINITION.read_text(encoding="utf-8")

    # `self.<field>`, WHICH IS THE FORM `reads_of` DELIBERATELY IGNORES. The
    # accessors are methods on `EmpireTree`, so that is how they read their own
    # fields, and it is the one place in the project that should.
    for field, _accessor in sorted(HALF_ANSWERS.items()):
        assert f"self.{field}" in text, (
            f"`self.{field}` no longer appears in {DEFINITION.name}, so either "
            "the accessor stopped using the field or the field was removed. If "
            "it is gone, take it out of HALF_ANSWERS here.")
