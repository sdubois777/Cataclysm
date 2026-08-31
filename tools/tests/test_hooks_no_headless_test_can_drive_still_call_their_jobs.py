"""Work hung on a hook no automation test can reach is actually called.

WHY THIS EXISTS. Three functions in this project are where work gets hung that
happens because something occurred rather than because a character acted:

  ACataclysmCharacterBase::RegenerationStep     time passed
  ACataclysmEnemyCharacter::HandleDeath         a creature died
  UCataclysmVitalAttributeSet::NotifyHealthChanged   health moved

Each job on them is a free function taking the character, so
`game/Source/Cataclysm/Tests/` can call one directly and check what it does.
None of those tests says anything about whether the hook calls it.

NEITHER HOOK CAN BE WATCHED BY AN AUTOMATION TEST, which is the reason this file
reads source text instead. `RegenerationStep` is driven by a timer, and a world
built by `UWorld::CreateWorld` is never ticked --
`ACataclysmCharacterBase::IsRegenerating`'s own comment says a test "can ask
whether the timer is running but can never watch it fire". `HandleDeath` needs a
possessed player pawn, a loot table and an enemy score before it reaches its last
few lines, so getting there costs far more scaffolding than the one line each job
occupies.

Deleting a call from either would therefore leave every test passing while the
feature did nothing in play. That is exactly what issue #1054 turned out to be:
twenty tests of the passive tree each performed by hand the step the game was
missing, so a spent passive point changed nothing and all twenty passed.

It has the second benefit every check in this directory has: continuous
integration compiles no C++, so nothing under `game/Source/Cataclysm/Tests/` runs
on a pull request and this does.

WHAT IS NOT CHECKED HERE. Whether each job does the right thing when it runs.
That is what the automation tests are for, and they are thorough about it. This
file checks one thing only: that the call is present, inside the hook, and not
commented out.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CHARACTER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
ABILITY_SYSTEM = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"

#: Each hook, the file it lives in, and every job it is expected to run.
#:
#: THE CALL AS IT IS WRITTEN, not the function name alone, so a mention in a
#: comment cannot satisfy it. Each entry is matched against the hook's body with
#: comment lines stripped out first.
#:
#: `questions` NAMES WHAT THE HOOK ASKS RATHER THAN WHAT IT DOES, so that the
#: completeness check below can tell a job nobody listed from a lookup.
HOOKS = {
    "ACataclysmCharacterBase::RegenerationStep": {
        "file": CHARACTER / "CataclysmCharacterBase.cpp",
        "jobs": {
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
                "the aura that puts a debuff on whatever stands near, #1057",
            "UCataclysmDebuffs::HoldStep":
                "the debuffs on a character being held still rather than "
                "counting down, issue #1070",
        },
        "questions": {
            "UCataclysmSkillEffects::IsDead",
            "UCataclysmTargeting::AbilitySystemOf",
        },
    },
    "ACataclysmEnemyCharacter::HandleDeath": {
        "file": CHARACTER / "CataclysmEnemyCharacter.cpp",
        "jobs": {
            "UCataclysmSaveWriter::NoteTriggerIn":
                "recording that a creature died, for the save",
            "UCataclysmDropSpawner::SpawnDropsFor":
                "what the creature dropped",
            "UCataclysmHealthDebt::ClearOnKill":
                "a kill clearing what the killer owes, issue #997",
            "UCataclysmStacks::NoteEnemyKilled":
                "the stack a kill may build, issue #1004",
            "UCataclysmContagion::SpreadOnDeath":
                "this creature's debuffs passing to whatever stands by its "
                "body, issue #1060",
        },
        "questions": {
            "UCataclysmSkillEffects::MarkDead",
            "UCataclysmDropSpawner::PlayerLootStats",
            "UCataclysmEnemyScore::ScoreFor",
            "UCataclysmEnemyScore::FloorIn",
        },
    },
    # A BUTTON'S CLICK HANDLER IS THE SAME KIND OF HOOK. Issue #1064. It is bound
    # to the button by reflection and the automation command passes `-nullrhi`,
    # so no widget draws and no test can press one. `TouchNode` is public and is
    # covered by an automation test; this is what says the button reaches it.
    #
    # WHAT WAS WRONG BEFORE. This handler read `SpendInto(Node)`, so clicking a
    # capstone always tried to spend a point, which is refused until one of its
    # three options is taken. No capstone in any tree could be taken from the
    # screen at all.
    #
    # ITS BODY CALLS NO FREE FUNCTION, so `test_the_jobs_are_pinned` below finds
    # nothing to complain about and passes on an empty set. That is correct
    # rather than a hole: the check exists to catch a job nobody listed, and a
    # one-line handler has no room for one.
    "UCataclysmPassiveTreeWidget::HandleNodeClicked": {
        "file": (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
                 / "CataclysmPassiveTreeWidget.cpp"),
        "jobs": {
            "TouchNode(Node)":
                "the decision between spending a point and offering a "
                "capstone's three options, issue #1064",
        },
        "questions": set(),
    },
    # A FOURTH HOOK, AND IT IS AN ATTRIBUTE SET'S NOTIFICATION. Issue #1070. It
    # fires on every write to health, which is what a health THRESHOLD BEING
    # CROSSED needs, and two capstone options now hang off it: one watches half
    # health and one watches a fifth of it.
    #
    # A HEADLESS TEST CAN DRIVE IT SINCE ISSUE #1072, AND THIS ENTRY IS STILL
    # WORTH KEEPING. The paragraph that stood here said no test could reach it,
    # because both calls were inside `PostGameplayEffectExecute` and getting
    # there meant building and applying a real gameplay effect spec. The hook
    # below now also runs it on any direct write, so the crossing tests do reach
    # it. What those tests cannot see is one of these two jobs being deleted
    # while the other still runs, which is what this checks.
    "UCataclysmVitalAttributeSet::NotifyHealthChanged": {
        "file": ABILITY_SYSTEM / "CataclysmVitalAttributeSet.cpp",
        "jobs": {
            "UCataclysmDamageConversion::NoteHealthChanged":
                "the drop below half health that turns damage into Bleeding, "
                "issue #985",
            "UCataclysmLowHealthRelief::NoteHealthChanged":
                "the drop to low health that clears a debt and grants "
                "Fervour, issue #1069",
        },
        "questions": set(),
    },
    # A FIFTH HOOK, AND IT IS WHAT MAKES THE FOURTH TRUE. Issues #971 and #1072.
    # The engine calls this for every write to an attribute's base value, which
    # is where `ApplyModToAttribute` and `SetNumericAttributeBase` both end up.
    # Until it existed, a health cost and a debt falling due wrote health without
    # telling anything: the character crossed every threshold in silence, and
    # reaching zero did not kill it. The project owner played that on 2026-08-31
    # and reported standing at zero health with every skill refused.
    #
    # ITS TWO JOBS ARE MEMBER CALLS AND NOT FREE FUNCTIONS, unlike every other
    # entry in this file, so they are written with their brackets the way
    # `HandleNodeClicked`'s job above is. `test_the_jobs_are_pinned` finds
    # nothing for the same reason -- it looks for `UCataclysm...::` calls -- and
    # passing on an empty set is correct rather than a hole.
    #
    # WHY THIS IS NOT COVERED BY THE AUTOMATION TESTS ALONE. Deleting either
    # call fails several of them, which is the guard proof recorded in the pull
    # request. Continuous integration compiles no C++, so nothing under
    # `game/Source/Cataclysm/Tests/` runs on a pull request and this does.
    "UCataclysmVitalAttributeSet::PostAttributeBaseChange": {
        "file": ABILITY_SYSTEM / "CataclysmVitalAttributeSet.cpp",
        "jobs": {
            "NotifyIfHealthReachedZero()":
                "a character brought to zero health by anything other than a "
                "blow dying, issue #971",
            "NotifyHealthChanged()":
                "a health threshold crossed by anything other than a blow "
                "being noticed at all, issue #1072",
        },
        "questions": set(),
    },
}


def body_of(path: pathlib.Path, signature: str, returns: str = "void") -> str:
    """The body of one function, with every line comment removed.

    COMMENTS ARE STRIPPED BECAUSE THESE FUNCTIONS ARE MOSTLY COMMENT. Each job
    carries a paragraph saying why it belongs on the hook rather than on a timer
    of its own, and those paragraphs name the neighbouring jobs. A plain search
    would find `UCataclysmNova::Step` in the paragraph that explains the aura and
    pass with the call itself deleted.

    `returns` DEFAULTS TO `void` BECAUSE EVERY HOOK ABOVE IS ONE. The death-log
    check below reads `KillIfDebtExceedsHealth`, which answers `bool`, and
    without this it reported the function as missing rather than as the wrong
    shape -- which reads like the function was renamed.
    """
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")

    text = path.read_text(encoding="utf-8")

    # THE OPENING BRACKET AND NOT AN EMPTY PAIR, because a hook may take an
    # argument. `HandleNodeClicked` takes the node that was clicked, and
    # searching for "()" found nothing and reported the function as missing.
    opening = text.find(f"{returns} {signature}(")
    assert opening != -1, (
        f"{path.name} no longer defines {signature}. If it was renamed, rename "
        "it here; if the hook is gone, every job listed for it needs a new home "
        "and this check needs rewriting.")

    brace = text.find("{", opening)
    assert brace != -1, f"{signature} has no body"

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
        pytest.fail(f"{signature}'s body is never closed")

    # Line comments only. There are no block comments in either function, and a
    # naive block-comment strip would be more machinery than this needs.
    return "\n".join(re.sub(r"//.*$", "", line)
                     for line in text[brace:end].splitlines())


#: (hook, call, description) for every job, flattened so each is its own test.
EVERY_JOB = [
    (hook, call, what)
    for hook, spec in sorted(HOOKS.items())
    for call, what in sorted(spec["jobs"].items())
]


@pytest.mark.parametrize("hook,call,what_it_does", EVERY_JOB)
def test_the_hook_runs_this_job(hook: str, call: str, what_it_does: str) -> None:
    body = body_of(HOOKS[hook]["file"], hook)
    assert call in body, (
        f"{hook} does not call {call}, which is {what_it_does}. Nothing else "
        f"calls it, so the feature does nothing in play. Every automation test "
        f"for it calls it directly and would still pass.")


@pytest.mark.parametrize("hook", sorted(HOOKS))
def test_the_jobs_are_pinned(hook: str) -> None:
    """Each hook runs the jobs this file knows about and no others.

    A JOB ADDED WITHOUT AN ENTRY HERE IS THE CASE THIS CATCHES. The whole point
    of the file is that a deleted call fails a test; a job nobody listed gets no
    such protection, and the person adding the next one should be told to add it
    here rather than finding out later.
    """
    spec = HOOKS[hook]
    body = body_of(spec["file"], hook)

    called = {name.rstrip("( \t")
              for name in re.findall(r"\bUCataclysm\w+::\w+\s*\(", body)}

    unexpected = called - set(spec["jobs"]) - spec["questions"]
    assert not unexpected, (
        f"{hook} calls something this file does not know about: "
        f"{sorted(unexpected)}. Add it to `jobs` with a plain description of "
        "what it does, so that deleting the call fails a test, or to "
        "`questions` if it only decides whether a job should run.")


# ---------------------------------------------------------------------------
# A log line is the same kind of thing as a hook's job
# ---------------------------------------------------------------------------

#: (file, return type, function, the wording, the state it must name, why)
DEATHS_THAT_MUST_BE_LOGGED = [
    (CHARACTER / "CataclysmPlayerCharacter.cpp", "void",
     "ACataclysmPlayerCharacter::HandleDeath",
     "died at",
     "owing",
     "a player dying, with the health it had and the health it owed"),
    (ABILITY_SYSTEM / "CataclysmHealthDebt.cpp", "bool",
     "UCataclysmHealthDebt::KillIfDebtExceedsHealth",
     "died of it",
     "owed",
     "a character killed by owing more health than it had"),
]


@pytest.mark.parametrize("path,returns,signature,phrase,detail,what_it_says",
                         DEATHS_THAT_MUST_BE_LOGGED,
                         ids=[entry[2] for entry in DEATHS_THAT_MUST_BE_LOGGED])
def test_a_death_is_written_to_the_log_at_a_level_play_shows(
        path: pathlib.Path, returns: str, signature: str, phrase: str,
        detail: str, what_it_says: str) -> None:
    """A death says so in the log, and at a level a play session records.

    WHY THIS IS IN THIS FILE. It is the same failure the rest of the file
    guards, one step further out: work that only the running game does, that no
    automation test can watch, and whose absence is silent. A test cannot read
    the log, so nothing else would notice this being deleted or quietly dropped
    back to `Verbose`.

    WHAT WENT WRONG. Issue #1101. On 2026-08-31 the project owner asked what had
    killed their character and the play session log could not say: nothing
    recorded a player death at all, and the message saying a debt had killed
    somebody was at `Verbose`, which is dropped. `LogCataclysm` is declared
    `Log, All` in `game/Source/Cataclysm/Cataclysm.h` and no configuration file
    raises it, so `Verbose` reaches nobody who is playing.
    """
    body = body_of(path, signature, returns)

    assert "UE_LOG(LogCataclysm, Log," in body, (
        f"{signature} does not log at `Log` level. It has to say {what_it_says},"
        " and `Verbose` is dropped in a play session, so anything below `Log`"
        " means the next person asking why they died reads the save file, the"
        " passive tree data and the class stat line instead. Issue #1101.")

    assert phrase in body, (
        f"{signature} no longer says \"{phrase}\". That wording is what somebody"
        f" greps a play session log for.")

    assert detail in body, (
        f"{signature} logs a death without saying \"{detail}\", so the line"
        " names the event and not the state that caused it. Health owed is the"
        " number that kills a character holding The Reckoning, and with that"
        " keystone the health bar reads full the whole way down. Issues #1098"
        " and #1100.")
