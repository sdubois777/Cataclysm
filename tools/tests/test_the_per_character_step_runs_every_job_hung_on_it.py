"""Everything that happens because time passed is actually called.

WHY THIS EXISTS. `ACataclysmCharacterBase::RegenerationStep` is where this
project puts work that happens because time passed rather than because a
character acted. It now carries seven jobs, and the two newest each reach other
actors: the nova a character at very low health releases (issue #1050) and the
aura that puts a debuff on whatever stands near it (issue #1057).

WHAT IT GUARDS, AND WHY THE AUTOMATION TESTS CANNOT. Each job is a free function
taking the character, so `game/Source/Cataclysm/Tests/` can call one directly and
check what it does. None of those tests says anything about whether the step
calls it. The step is driven by `RegenerationTimer`, and a world built by
`UWorld::CreateWorld` is never ticked, so no automation test can watch it fire --
`ACataclysmCharacterBase::IsRegenerating`'s own comment says a test "can ask
whether the timer is running but can never watch it fire".

Deleting a call from the step would therefore leave every test passing while the
feature did nothing in play. That is exactly what issue #1054 turned out to be:
twenty tests of the passive tree each performed by hand the step the game was
missing, so a spent passive point changed nothing and all twenty passed.

Reading the source as text is what catches it, and it has the second benefit
every check in this directory has: continuous integration compiles no C++, so
nothing under `game/Source/Cataclysm/Tests/` runs on a pull request and this
does.

WHAT IS NOT CHECKED HERE. Whether each job does the right thing when it runs.
That is what the automation tests are for, and they are thorough about it. This
file checks one thing only: that the call is present, inside the step, and not
commented out.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STEP_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
            / "CataclysmCharacterBase.cpp")

#: Every job the step is expected to run, and the module each lives in.
#:
#: THE CALL AS IT IS WRITTEN, not the function name alone, so a mention in a
#: comment cannot satisfy it. Each entry is matched against the step's body with
#: comment lines stripped out first.
JOBS = {
    "UCataclysmRegeneration::ApplyStep":
        "health, mana and energy shield coming back",
    "UCataclysmLeech::PayOutStep":
        "what leech owes the character being paid out",
    "UCataclysmHealthDebt::SettleIfDue":
        "a deferred health cost falling due",
    "UCataclysmHealthDebt::KillIfDebtExceedsHealth":
        "a character dying when it owes more health than it has",
    "UCataclysmFervour::GainPerSecondStep":
        "the Fervour that arrives from the passage of time",
    "UCataclysmNova::Step":
        "the nova a character at very low health releases, issue #1050",
    "UCataclysmContagion::AuraStep":
        "the aura that puts a debuff on whatever stands near, issue #1057",
}


@pytest.fixture(scope="module")
def step_body() -> str:
    """The body of `RegenerationStep`, with every comment line removed.

    COMMENTS ARE STRIPPED BECAUSE THE FUNCTION IS MOSTLY COMMENT. Each job in it
    carries a paragraph saying why it is a job on this step rather than a timer
    of its own, and those paragraphs name the neighbouring jobs. A plain search
    would find `UCataclysmNova::Step` in the paragraph that explains the aura and
    pass with the call itself deleted.
    """
    if not STEP_CPP.is_file():
        pytest.skip(f"{STEP_CPP.name} is not present")

    text = STEP_CPP.read_text(encoding="utf-8")

    opening = text.find("void ACataclysmCharacterBase::RegenerationStep()")
    assert opening != -1, (
        f"{STEP_CPP.name} no longer defines RegenerationStep. If it was renamed,"
        " rename it here; if the per-character step is gone, every job listed in"
        " this file needs a new home and this check needs rewriting.")

    brace = text.find("{", opening)
    assert brace != -1, "RegenerationStep has no body"

    depth = 0
    end = brace
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index
                break
    else:  # pragma: no cover - only reachable on an unbalanced file
        pytest.fail("RegenerationStep's body is never closed")

    body = text[brace:end]

    # Line comments only. There are no block comments in this function, and a
    # naive block-comment strip would be more machinery than the file needs.
    return "\n".join(re.sub(r"//.*$", "", line) for line in body.splitlines())


@pytest.mark.parametrize("call,what_it_does", sorted(JOBS.items()))
def test_the_step_runs_this_job(step_body: str, call: str,
                                what_it_does: str) -> None:
    assert call in step_body, (
        f"ACataclysmCharacterBase::RegenerationStep does not call {call}, which "
        f"is {what_it_does}. Nothing else calls it on a timer, so the feature "
        f"does nothing in play. Every automation test for it calls it directly "
        f"and would still pass.")


def test_the_count_is_pinned(step_body: str) -> None:
    """The step carries the jobs this file knows about and no others.

    A JOB ADDED WITHOUT AN ENTRY HERE IS THE CASE THIS CATCHES. The whole point
    of the file is that a deleted call fails a test; a job nobody listed gets no
    such protection, and the person adding the eighth one should be told to add
    it here rather than finding out later.
    """
    found = re.findall(r"\bUCataclysm\w+::\w+\s*\(", step_body)
    called = {name.rstrip("( \t") for name in found}

    # `UCataclysmSkillEffects::IsDead` and `UCataclysmTargeting::AbilitySystemOf`
    # are asked inside the step to decide whether a job should run. They are
    # questions rather than jobs, so they are named here rather than in JOBS.
    questions = {
        "UCataclysmSkillEffects::IsDead",
        "UCataclysmTargeting::AbilitySystemOf",
    }

    unexpected = called - set(JOBS) - questions
    assert not unexpected, (
        "ACataclysmCharacterBase::RegenerationStep calls something this file "
        f"does not know about: {sorted(unexpected)}. Add it to JOBS with a plain "
        "description of what it does, so that deleting the call fails a test.")
