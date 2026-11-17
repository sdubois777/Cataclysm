"""The balance sweep counts the campaigns it runs and prints the count. Issue #693.

`CLAUDE.md` and `header_lines` said the sweep ran "about 25,000" campaigns; read from `main()`'s loops on 2026-10-29
it runs 31,820 or 33,920, depending on the dungeons per surge calibration picks. So the count is now kept by the run:
every campaign goes through `experiments.batch`, which adds what it ran to `experiments.campaigns_run`, and the
report's last line prints it. These tests never run the sweep, which takes twenty minutes or more.
"""
from __future__ import annotations

import ast
import inspect
import pathlib
import sys
import textwrap

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import experiments  # noqa: E402
from cataclysm_sim import policies  # noqa: E402
from cataclysm_sim.config import TuningConfig  # noqa: E402
from cataclysm_sim.engine import Simulation  # noqa: E402


def test_a_batch_moves_the_count_by_the_campaigns_it_ran():
    before = experiments.campaigns_run
    results = experiments.batch(TuningConfig(), policies.triage, trials=2)
    assert len(results) == 2
    assert experiments.campaigns_run == before + 2, (
        f"a batch of 2 campaigns moved the count from {before} to {experiments.campaigns_run}")


def test_working_out_a_walk_length_runs_no_campaign_and_counts_none():
    """CONTROL. `run_days_for` is arithmetic on a simulation and runs no campaign; the explorer section calls it
    beside its batches, so a count that rose here would be counting something other than campaigns."""
    before = experiments.campaigns_run
    Simulation(TuningConfig(), seed=0).run_days_for(30)
    assert experiments.campaigns_run == before


def test_the_reports_last_line_prints_the_count():
    """Read from `main()`'s code, not its text, so a comment naming the counter does not satisfy it: the call that
    prints "Done." must use `campaigns_run` in the same f-string."""
    tree = ast.parse(textwrap.dedent(inspect.getsource(experiments.main)))
    done = [node for node in ast.walk(tree)
            if isinstance(node, ast.JoinedStr)
            and any(isinstance(part, ast.Constant) and "Done." in str(part.value) for part in node.values)]
    assert len(done) == 1, f"expected one f-string printing Done., found {len(done)}"
    names = {node.id for node in ast.walk(done[0]) if isinstance(node, ast.Name)}
    assert "campaigns_run" in names, f"the Done. line prints {sorted(names)} and not campaigns_run"
