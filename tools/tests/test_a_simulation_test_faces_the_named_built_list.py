"""A simulation a test builds faces the built list the tests name, not whatever the config lists that day. Issue #2928.

The root `conftest.py` holds `BUILT_ON_2026_11_26` (as `engine.cataclysm_order_for.built_list`) in the simulation's draw for every test that leaves
`TuningConfig.BUILT_CATACLYSMS` at its default. These check that the hold reaches a `Simulation`, that a test's own
built list is kept, and that a test marked `real_built_list` meets the config's own list.

THE CHANGE OF BUILT LIST IS SIMULATED by changing the held list for one test, since the config's default cannot be
changed from a test: a simulation then follows the held list, which is what keeps every other test still when the
config's list changes.
"""

from __future__ import annotations

from dataclasses import replace

import pytest
from cataclysm_sim import engine
from cataclysm_sim.config import TuningConfig
from cataclysm_sim.engine import Simulation


def test_a_simulation_left_at_the_default_faces_the_held_list(monkeypatch):
    """With the held list changed to Death alone, a tier 1 simulation faces Death on every seed."""
    monkeypatch.setattr(engine.cataclysm_order_for, "built_list", ("Death",))
    faced = {Simulation(TuningConfig(), seed=seed).active_types for seed in range(20)}
    assert faced == {("Death",)}


def test_the_control_with_the_held_list_unchanged_more_than_one_cataclysm_is_faced():
    """CONTROL: with the held list as written, Demonic and Death, twenty seeds face both, so the check above is not
    passing because every seed faces Death anyway."""
    faced = {Simulation(TuningConfig(), seed=seed).active_types[0] for seed in range(20)}
    assert faced == set(engine.cataclysm_order_for.built_list)


def test_a_test_naming_its_own_built_list_keeps_it(monkeypatch):
    """A test that sets its own built list is never replaced, even while the held list is something else."""
    monkeypatch.setattr(engine.cataclysm_order_for, "built_list", ("Death",))
    own = replace(TuningConfig(), BUILT_CATACLYSMS=("War",))
    faced = {Simulation(own, seed=seed).active_types for seed in range(20)}
    assert faced == {("War",)}


@pytest.mark.real_built_list
def test_a_test_marked_real_built_list_meets_the_configs_own_draw():
    """A test whose subject is the draw meets the engine's own function, unreplaced."""
    assert engine.cataclysm_order_for.__name__ == "cataclysm_order_for"


def test_every_other_test_meets_the_held_draw():
    """CONTROL for the one above: an unmarked test meets the replacement."""
    assert engine.cataclysm_order_for.__name__ == "held"
