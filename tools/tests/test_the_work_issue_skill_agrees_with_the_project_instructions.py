"""The work-issue skill does not tell a session something `CLAUDE.md` denies.

WHAT THIS GUARDS. `.claude/skills/work-issue/SKILL.md` is the instruction file a
session follows when it picks up an issue, and nothing checked any claim in it.
Issue #1275 found three instructions that contradicted `CLAUDE.md`, each of which
every session following the skill got wrong:

- it sent every new test to `sim/tests/`, when on 2026-09-05 `tools/tests/` held
  171 test files against that directory's 24, and `sim/tests/` is for the Python
  empire simulation alone;
- it said to write `Closes #N` in the pull request body "so it closes on merge",
  when GitHub only honours that keyword for a pull request merging into the
  repository's default branch. The default branch here is `main` and these merge
  into `development`, so the keyword did nothing and the issue stayed open;
- it said to prove a new guard by feeding it bad input by hand, which is the one
  procedure `CLAUDE.md` tells you not to use, because a break-and-restore that
  leaves a file's modification time and size unchanged makes the run read a stale
  compiled copy.

Issues #1278 and #1279 then found two gaps rather than contradictions, and each
left a session doing something `CLAUDE.md` forbids:

- the file gave no Unreal guidance at all, though 94 of the 186 open issues were
  labelled `unreal` on 2026-09-05. Step 4 is titled "Test it thoroughly" and
  offered only `pytest` and `ruff`, neither of which compiles a line of C++, so a
  session could follow it to the letter on a C++ issue and open a pull request
  having never built what it changed;
- it never said to delete the branch when the pull request merged. Issue #1276
  measured the result on 2026-09-05: 92 of 95 local branches already merged and
  never deleted. That issue carries the clean-up; this file holds the
  instruction that let it happen.

Issue #1325 found a third gap of the same kind. `CLAUDE.md` requires looking up
how Path of Exile, Last Epoch, Torchlight Infinite and Diablo solve a problem
before proposing any formula, affix or mechanic, naming the sources, and
recording them in `docs/DECISIONS.md`. The skill's reconnaissance step covered
only reading this repository, so a session answering a design question from its
own judgement was following the documented process correctly. An invented
formula compiles, passes tests, and leaves no trace of why it was chosen.

WHAT THIS DOES NOT GUARD. Whether the rest of the skill's prose is good advice,
and whether the counts quoted in it are still current. Those still need reading.
This checks the claims that can be checked mechanically.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SKILL = REPO_ROOT / ".claude" / "skills" / "work-issue" / "SKILL.md"
INSTRUCTIONS = REPO_ROOT / "CLAUDE.md"

#: A GitHub closing keyword pointing at an issue, as it would appear in a pull
#: request body. `Fixed in #<PR>` does not match, and must not: a word between
#: the keyword and the number stops GitHub honouring it, which is why the manual
#: close comment is phrased that way.
CLOSING_KEYWORD = re.compile(
    r"\b(?:clos(?:e|es|ed)|fix(?:es|ed)?|resolv(?:e|es|ed))\s+#(?:\d+|N)",
    re.IGNORECASE,
)

#: Any of these, appearing soon after a closing keyword, means the skill is
#: naming the keyword in order to say it does not work.
DENIALS = ("does nothing", "does not fire", "never fires",
           "is not honoured", "does not close")

#: How far after a closing keyword a denial may sit and still be read as
#: belonging to it. Long enough for the sentence that explains why.
DENIAL_WINDOW = 400


def skill_text() -> str:
    return SKILL.read_text(encoding="utf-8")


def collapsed(text: str) -> str:
    """The same text with every run of whitespace reduced to one space.

    MATCH A MULTI-WORD PHRASE AGAINST THIS, NOT AGAINST THE RAW FILE. Both
    files are hard-wrapped prose, so a name can sit across a line break. On
    2026-09-05 the skill wrapped Path of Exile between the words 'of' and
    'Exile', and the first version of the guard below could not see it and
    passed on the other three games instead. Rewrapping a paragraph must
    not change what these tests find.
    """
    return re.sub(r"\s+", " ", text)


def test_the_skill_file_was_found_and_is_the_real_one() -> None:
    """The tests below read the real file rather than an empty string.

    Without this, moving or renaming the skill would make every other test in
    this file pass by having nothing to check.
    """
    assert SKILL.is_file(), (
        f"{SKILL} is not there. The work-issue skill is the instruction file a "
        "session follows when it picks up an issue; if it moved, point this "
        "test at the new location."
    )

    text = skill_text()
    for heading in ("## 4. Test it thoroughly", "## 6. Open the pull request"):
        assert heading in text, (
            f"{SKILL.name} has no '{heading}' heading, so it is not the file "
            "these tests were written against. Either the step numbering "
            "changed or the file was rewritten; re-read it and update this test."
        )


def test_the_skill_names_both_test_directories_and_both_exist() -> None:
    """A session is told which directory suits which kind of test.

    THIS IS ONE THAT FOUND SOMETHING. The skill named only `sim/tests/` until
    issue #1275, so a test of a generator, a data pipeline or a repository guard
    was sent to the directory reserved for the empire simulation.
    """
    text = skill_text()

    for named in ("sim/tests/", "tools/tests/"):
        assert f"`{named}`" in text, (
            f"{SKILL.name} does not name `{named}`. It has to name both test "
            "directories and say which suits which kind of test: `sim/tests/` "
            "for the Python empire simulation under `sim/cataclysm_sim/`, and "
            "`tools/tests/` for everything else."
        )
        assert (REPO_ROOT / named).is_dir(), (
            f"{SKILL.name} sends tests to `{named}`, which does not exist."
        )


def test_the_skill_gives_the_manual_issue_close_step() -> None:
    """The step that actually closes an issue is in the file.

    `CLAUDE.md` is checked alongside it, so that this test starts failing if the
    project's rule about closing ever changes, rather than quietly guarding a
    rule that no longer holds.
    """
    assert "gh issue close" in INSTRUCTIONS.read_text(encoding="utf-8"), (
        "CLAUDE.md no longer names `gh issue close`. If the project's rule "
        "about how an issue gets closed has changed, this test and the "
        "work-issue skill both need re-reading."
    )

    assert "gh issue close" in skill_text(), (
        f"{SKILL.name} does not name `gh issue close`. Nothing closes an issue "
        "on merge in this repository, so the skill has to tell the session to "
        "run the close itself and then confirm the issue reads as closed."
    )


def test_the_skill_never_presents_a_closing_keyword_as_the_mechanism() -> None:
    """`Closes #N` is only ever mentioned as something that does not work.

    THIS IS THE OTHER ONE THAT FOUND SOMETHING. The skill said to reference the
    issue with `Closes #N` "so it closes on merge". It never fires here, so a
    session watched its pull request merge and left the issue open believing it
    had closed.
    """
    text = skill_text()

    presented_as_working = []
    for match in CLOSING_KEYWORD.finditer(text):
        following = text[match.end():match.end() + DENIAL_WINDOW].lower()
        if not any(denial in following for denial in DENIALS):
            presented_as_working.append(match.group(0))

    assert not presented_as_working, (
        f"{SKILL.name} mentions {', '.join(presented_as_working)} without "
        "saying it does not work. GitHub only honours a closing keyword when "
        "the pull request merges into the repository's default branch, which "
        "here is `main`, and these merge into `development`. Either drop the "
        f"mention or follow it, within {DENIAL_WINDOW} characters, with one of: "
        f"{', '.join(DENIALS)}."
    )


def test_the_skill_points_at_the_guard_proving_helpers() -> None:
    """A guard is proved with the helpers, not with a script written on the spot.

    `CLAUDE.md` requires this and explains why: a hand-rolled break-and-restore
    usually leaves a file's modification time and size unchanged, so the run
    reads a stale compiled copy and reports something that did not happen.
    """
    text = skill_text()

    for helper in ("tools/prove_guard.py", "tools/unreal_build.py"):
        assert f"`{helper}`" in text, (
            f"{SKILL.name} does not name `{helper}`. CLAUDE.md requires a guard "
            "be proved with these helpers rather than by breaking and restoring "
            "the file with a script written on the spot, because that route "
            "produces misleading evidence rather than an obvious error."
        )
        assert (REPO_ROOT / helper).is_file(), (
            f"{SKILL.name} points at `{helper}`, which does not exist."
        )


def test_the_skill_gives_the_unreal_build_and_test_commands() -> None:
    """A session working a C++ issue is told how to compile and test it.

    THIS IS ONE THAT FOUND SOMETHING, issue #1278. The skill named
    `tools/unreal_build.py` once, as the helper that proves a C++ guard, and
    nowhere told a session to build or to run the automation tests at all. Step 4
    offered `pytest` and `ruff` and nothing else, and neither compiles any C++.

    Asserting on the whole command rather than on the file name is the point: the
    file name was already there and a guard that checked only for it would have
    passed against the version this issue was written about.
    """
    text = skill_text()

    for command in ("python tools/unreal_build.py build",
                    "python tools/unreal_build.py tests"):
        assert command in text, (
            f"{SKILL.name} does not give `{command}`. `pytest` compiles no C++ "
            "and runs no automation test, so a session that follows the skill "
            "literally on a change under `game/` never builds what it changed."
        )

    assert (REPO_ROOT / "tools" / "unreal_build.py").is_file(), (
        f"{SKILL.name} gives commands that run `tools/unreal_build.py`, which "
        "does not exist."
    )


def test_the_skill_says_to_claim_the_editor_before_driving_it() -> None:
    """The editor lock is named, with both halves of it.

    `CLAUDE.md` says one session at a time may drive the editor and that nothing
    enforces it. The interactive editor, the automation tests and the DataTable
    regeneration all drive one editor on one machine, so two sessions following a
    skill that never mentions the lock will drive it at once.

    `release` is checked as well as `acquire` because a session that takes the
    lock and never gives it back blocks every other session until it exits.
    """
    text = skill_text()

    for command in ("python tools/unreal_lock.py acquire",
                    "python tools/unreal_lock.py release"):
        assert command in text, (
            f"{SKILL.name} does not give `{command}`. Nothing enforces one "
            "session at a time on the editor, so the skill has to say to claim "
            "it before driving it and release it the moment the work is done."
        )

    assert (REPO_ROOT / "tools" / "unreal_lock.py").is_file(), (
        f"{SKILL.name} gives commands that run `tools/unreal_lock.py`, which "
        "does not exist."
    )


def test_the_skill_says_to_regenerate_the_datatable_assets() -> None:
    """Changing a CSV under `game/Data/` is not enough on its own.

    The automation tests load the generated DataTable asset rather than the CSV,
    so a suite that passes over a stale asset has tested the previous data and
    says so nowhere.
    """
    text = skill_text()

    runner = "python tools/run_editor_python.py tools/generate_datatable_assets.py"
    assert runner in text, (
        f"{SKILL.name} does not give `{runner}`. The Unreal tests read the "
        "generated DataTable asset and not the CSV, so a change under "
        "`game/Data/` that is never regenerated is a change the tests cannot "
        "see."
    )

    assert "`game/Data/`" in text, (
        f"{SKILL.name} gives the regeneration command without naming "
        "`game/Data/`, so nothing says when to run it."
    )

    for script in ("tools/run_editor_python.py",
                   "tools/generate_datatable_assets.py"):
        assert (REPO_ROOT / script).is_file(), (
            f"{SKILL.name} points at `{script}`, which does not exist."
        )


def test_the_skill_says_to_delete_the_branch_when_the_pull_request_merges() -> None:
    """Both halves of the deletion are given, and with the right flag.

    THIS IS THE OTHER ONE THAT FOUND SOMETHING, issue #1279. The skill ended
    after closing the issue and never mentioned the branch again. Issue #1276
    measured what that costs: 92 of 95 local branches already merged and never
    deleted, on 2026-09-05.

    `-D` rather than `-d` is checked because the flag is the trap. A squash merge
    leaves the branch commit outside `development`'s history, so `git branch -d`
    refuses, and that refusal reads like "not merged yet" to a session that was
    not told to expect it.

    `CLAUDE.md` is checked alongside, so this starts failing if the project's
    rule ever changes rather than quietly guarding a rule that no longer holds.
    """
    instructions = INSTRUCTIONS.read_text(encoding="utf-8")
    assert "git branch -D" in instructions, (
        "CLAUDE.md no longer says to delete a merged branch with `git branch "
        "-D`. If the project's rule has changed, this test and the work-issue "
        "skill both need re-reading."
    )

    text = skill_text()

    assert "git branch -D" in text, (
        f"{SKILL.name} does not name `git branch -D`. CLAUDE.md requires the "
        "branch be deleted as part of merging, not in a later cleanup pass, and "
        "`git branch -d` refuses a squash-merged branch."
    )

    assert "git push origin --delete" in text, (
        f"{SKILL.name} deletes the local branch but not the one on GitHub. "
        "CLAUDE.md says to delete it in both places as part of merging."
    )


#: The games `CLAUDE.md` names as having solved these problems in public. The
#: skill may name a subset; it may not name something else instead, because a
#: source the project has not vouched for is not the evidence this rule asks for.
RESEARCHED_GAMES = ("Path of Exile", "Last Epoch", "Torchlight Infinite",
                    "Diablo")


def test_the_skill_says_to_research_the_genre_before_proposing_a_formula() -> None:
    """A design question is answered from shipped games, not from judgement.

    THIS IS THE THIRD ONE THAT FOUND SOMETHING, issue #1325. Step 2 is titled
    "Deep reconnaissance" and covered only reading this repository: the code, its
    callers, its git history. Nothing told a session to look outside it, and
    nothing told it to record what it read. `CLAUDE.md` says the best structural
    decision in this project came from exactly the route the skill omitted.

    `docs/DECISIONS.md` is checked as well as the research itself, because a
    source named in a pull request and nowhere else is gone as soon as the
    conversation is.
    """
    instructions = INSTRUCTIONS.read_text(encoding="utf-8")
    assert "Research the genre" in instructions, (
        "CLAUDE.md no longer requires researching the genre before proposing a "
        "formula. If the project's rule has changed, this test and the "
        "work-issue skill both need re-reading."
    )

    text = skill_text()

    flat = collapsed(text)
    flat_instructions = collapsed(instructions)

    for game in RESEARCHED_GAMES:
        assert game in flat_instructions, (
            f"CLAUDE.md no longer names {game} among the games whose "
            "solutions count as evidence. Update RESEARCHED_GAMES to match "
            "it, and the skill with it."
        )
        assert game in flat, (
            f"{SKILL.name} does not name {game}. It has to carry the same "
            f"list CLAUDE.md does -- {', '.join(RESEARCHED_GAMES)} -- because "
            "a shortened list is a session looking in fewer places than the "
            "project asked for."
        )

    assert "`docs/DECISIONS.md`" in flat, (
        f"{SKILL.name} does not name `docs/DECISIONS.md`. CLAUDE.md requires "
        "the sources be recorded there beside the decision, so the next person "
        "can see why a shape was chosen and not only what was chosen."
    )
    assert (REPO_ROOT / "docs" / "DECISIONS.md").is_file(), (
        f"{SKILL.name} sends sources to `docs/DECISIONS.md`, which does not "
        "exist."
    )


def test_the_skill_says_what_to_do_where_the_research_settles_nothing() -> None:
    """The part research cannot answer is labelled, and still answered.

    `CLAUDE.md` asks for two things here and they pull in opposite directions,
    which is why both are checked. Anything genuinely specific to this game has
    to be labelled a judgement rather than presented as derived; and the session
    still has to state the single recommendation it landed on rather than list
    options. A skill that carried only the first would licence handing every
    unresearched question back.
    """
    instructions = INSTRUCTIONS.read_text(encoding="utf-8")
    for word in ("judgement", "recommendation"):
        assert word in instructions, (
            f"CLAUDE.md no longer says '{word}' where it describes what to do "
            "with the part of a design question the research does not settle. "
            "Re-read that rule before trusting this test."
        )

    text = skill_text()
    for word, why in (
        ("judgement",
         "what is specific to this game has to be labelled a judgement rather "
         "than presented as derived"),
        ("recommendation",
         "the session still has to land on the single recommendation it "
         "reached, not hand back a list of options"),
    ):
        assert word in text, (
            f"{SKILL.name} does not say '{word}'. CLAUDE.md requires it: {why}."
        )
