"""Make `cataclysm_sim` importable whether pytest runs from sim/ or the repo root."""

import pathlib
import sys

import pytest

SIM_ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(SIM_ROOT) not in sys.path:
    sys.path.insert(0, str(SIM_ROOT))


@pytest.fixture
def meet_the_unlock_requirement():
    """Force a run past the Cataclysm dungeon's gate, without playing to it.

    WHY IT IS A FIXTURE RATHER THAN A LINE AT EACH CALL SITE. Several tests are
    about what the boss IS once it opens -- how deep it is, how long it takes to
    walk, that it never rolls Cow Level -- and none of them are about what opens
    it. They used to force the gate with one line, `sim.objectives =
    cfg.quest_objectives_required`, because the gate was a flat total.

    THE OWNER'S RULING OF 2026-09-06 MADE THE GATE READ SOMETHING A TOTAL CANNOT
    EXPRESS: half of the ACTIVE CATACLYSMS, rounded up, must each have had their
    own objective count met. So the state those tests need has to be BUILT
    rather than the assertion weakened -- setting the total still leaves every
    Cataclysm unfinished and the boss shut, which would have made each of those
    tests fail for a reason that has nothing to do with its subject.

    IT MEETS EXACTLY THE REQUIREMENT AND NOT MORE, so a test that then asserts
    the boss opened is asserting something the rule really allows. The
    Cataclysms it finishes are the first `ceil(N/2)` of the run's own draw;
    which ones they are does not matter, because the rule does not say.
    """
    def meet(sim) -> None:
        cfg = sim.cfg
        for name in sim.active_types[:cfg.cataclysms_required()]:
            needed = cfg.quest_objectives_for(name)
            sim.objectives_by_type[name] = needed
            sim.objectives += needed

    return meet
