"""Copies of the design workbook and the class tree files with the aura reservation of 2026-11-27 taken out.

WHY. Several tests hold an earlier script to reproducing the repository's files from the state that script was
written against: `test_reapply_demonic_tree_rework_workbook.py`, `test_apply_demonic_tree_rework_2026_11_24.py`,
`test_reapply_death_trees_workbook.py` and `test_war_data_rows_scripts.py`. The aura reservation
(`tools/apply_aura_reservation_2026_11_27.py`) came after them and adds a row to four of their nodes, a clause to
four of their sentences, and rows after theirs. So those tests compare with copies that have it taken out -- by the
script's own `take_out`, which refuses a file that does not hold the ruling -- and the ruling itself is held by
`test_apply_aura_reservation.py`.
"""

from __future__ import annotations

import functools
import pathlib
import shutil
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_aura_reservation_2026_11_27 as aura  # noqa: E402

DOCS = REPO_ROOT / "docs"


@functools.lru_cache(maxsize=1)
def docs() -> pathlib.Path:
    """A folder holding the workbook and every class tree file, the aura reservation taken out. Made once a run."""
    folder = pathlib.Path(tempfile.mkdtemp(prefix="without_aura_reservation_"))
    shutil.copyfile(DOCS / "All_Things_Cataclysm.xlsx", folder / "All_Things_Cataclysm.xlsx")
    for tree in DOCS.glob("*_Class_Tree_*.json"):
        shutil.copyfile(tree, folder / tree.name)
    aura.take_out(folder / "All_Things_Cataclysm.xlsx", folder)
    return folder


def workbook() -> pathlib.Path:
    return docs() / "All_Things_Cataclysm.xlsx"
