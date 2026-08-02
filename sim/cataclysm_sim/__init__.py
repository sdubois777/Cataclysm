"""Headless simulation of the Cataclysm empire layer.

The purpose of this package is NOT to be the game. It is a tuning rig: it
models only the strategic layer (days, surges, dungeon timers, city health,
the loss condition) so that the five numbers the design documents never pin
down can be derived from play outcomes instead of guessed.

Those five numbers live in `config.TuningConfig` and are marked UNKNOWN.
"""

from .config import TuningConfig, EmpireTree, CityTier, DungeonType
from .world import build_empire, Empire, City
from .engine import Simulation, RunResult
from . import policies

__all__ = [
    "TuningConfig", "EmpireTree", "CityTier", "DungeonType",
    "build_empire", "Empire", "City",
    "Simulation", "RunResult", "policies",
]
