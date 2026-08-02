"""Make `cataclysm_sim` importable whether pytest runs from sim/ or the repo root."""

import pathlib
import sys

SIM_ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(SIM_ROOT) not in sys.path:
    sys.path.insert(0, str(SIM_ROOT))
