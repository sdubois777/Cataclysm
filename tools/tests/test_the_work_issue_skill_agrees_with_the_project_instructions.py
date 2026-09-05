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

WHAT THIS DOES NOT GUARD. Whether the rest of the skill's prose is good advice,
and whether the two counts quoted in it are still current. Those still need
reading. This checks the claims that can be checked mechanically.
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
