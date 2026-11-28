"""Copies of the design workbook and the class tree files with the War trees' rows of 2026-11-27 taken out.

WHY. Several tests hold an earlier script to reproducing the repository's files from the state that script was
written against (`test_apply_aura_reservation.py`, and the tests that use `without_the_aura_reservation.py`). The War
trees' data rows (`tools/reapply_war_trees_data_rows_workbook.py`) and their two ruled sentences
(`tools/apply_war_trees_sentences_2026_11_27.py`) came after them and append rows after theirs. So those tests compare
with copies that have them taken out -- by each script's own `take_out`, which refuses a file that does not hold what
the script wrote -- and the two scripts themselves are held by `test_war_trees_data_rows_scripts.py`.
"""

from __future__ import annotations

import functools
import pathlib
import shutil
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_war_trees_sentences_2026_11_27 as sentences  # noqa: E402
import reapply_war_trees_data_rows_workbook as rows  # noqa: E402

DOCS = REPO_ROOT / "docs"


@functools.lru_cache(maxsize=1)
def docs() -> pathlib.Path:
    """A folder holding the workbook and every class tree file, the War trees' rows taken out. Made once a run."""
    folder = pathlib.Path(tempfile.mkdtemp(prefix="without_war_trees_rows_"))
    shutil.copyfile(DOCS / "All_Things_Cataclysm.xlsx", folder / "All_Things_Cataclysm.xlsx")
    for tree in DOCS.glob("*_Class_Tree_*.json"):
        shutil.copyfile(tree, folder / tree.name)
    rows.take_out(folder / "All_Things_Cataclysm.xlsx")
    sentences.take_out(folder)
    return folder


def workbook() -> pathlib.Path:
    return docs() / "All_Things_Cataclysm.xlsx"
