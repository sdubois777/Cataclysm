"""`tools/unreal_build.py` must not report a build that did nothing as a success.

WHY THIS FILE EXISTS. Issue #139. Restoring a C++ file with a tool that preserves
its modification time gives it an mtime older than the object file built from the
broken version. UnrealBuildTool then prints `Result: Succeeded`, compiles nothing,
and leaves the broken binary in place. Every source of evidence a person would
check — the file on disk, the build output — says everything is fine.

WHAT IS CHECKED HERE. The reading, not the building. Every test runs against
build output and log text captured from real runs on this machine, so none of
them needs Unreal Engine, an editor, or a compiler, and they run in continuous
integration with the rest of the fast suite. The three real captures are below,
verbatim apart from being trimmed to the lines that matter.

WHAT IS NOT CHECKED HERE. That `build()` and `run_automation_tests()` invoke the
engine correctly. That needs the engine. The pull request for issue #139 has the
end-to-end run.
"""

from __future__ import annotations

import inspect
import os
import pathlib
import re
import subprocess
import sys
import time

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import unreal_build  # noqa: E402
from unreal_build import (  # noqa: E402
    BuildDidNothing,
    BuildOutcome,
    TestOutcome,
    exit_code_for,
    module_of,
    parse_arguments,
    parse_build_output,
    parse_test_log,
    prove_cpp_guard,
    require_compiled,
    restore_and_touch,
)

#: This checkout's root, for the check that the C++ helper and the Python reader
#: spell the skipped-half token the same way.
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

#: The script itself, for the tests below that run it as a command rather than
#: importing it.
BUILD_SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "unreal_build.py"

#: A real build that had work to do. Captured 2026-08-04 with a clean working
#: tree, which is why the compile line names the unity blob rather than a file.
BUILD_THAT_COMPILED = """\
Using 'git status' to determine working set for adaptive non-unity build (C:\\Projects\\Cataclysm).
Invalidating makefile for CataclysmEditor (working set of source files changed)
Building CataclysmEditor...
Using Unreal Build Accelerator local executor to run 4 action(s)
[1/4] Compile [x64] Module.Cataclysm.cpp
[2/4] Link [x64] UnrealEditor-Cataclysm.lib
[3/4] Link [x64] UnrealEditor-Cataclysm.dll
[4/4] WriteMetadata CataclysmEditor.target [NoUba]

Total time in Unreal Build Accelerator local executor: 8.87 seconds
Result: Succeeded
Total execution time: 10.60 seconds
"""

#: A real build immediately after that one. This is the shape issue #139 is
#: about: it succeeded and did nothing.
BUILD_THAT_DID_NOTHING = """\
Using 'git status' to determine working set for adaptive non-unity build (C:\\Projects\\Cataclysm).
Target is up to date
Using Unreal Build Accelerator local executor to run 0 action(s)
Total time in Unreal Build Accelerator local executor: 0.09 seconds
Result: Succeeded
Total execution time: 0.76 seconds
"""

#: A build that hit a compiler error. The parenthesised part after the result
#: word varies, which is why only the first word is read.
BUILD_THAT_FAILED = """\
Building CataclysmEditor...
Using Unreal Build Accelerator local executor to run 3 action(s)
[1/3] Compile [x64] CataclysmProjectile.cpp
C:\\Projects\\Cataclysm\\game\\Source\\Cataclysm\\AbilitySystem\\CataclysmProjectile.cpp(88):\
 error C2065: 'Nonsense': undeclared identifier
Result: Failed (OtherCompilationError)
Total execution time: 6.20 seconds
"""

#: The build the continuous integration runner stops. Captured 2026-09-14 in
#: another worktree while the runner was compiling a pull request; the whole
#: thing takes a quarter of a second. Issues #1577 and #1802.
BUILD_DENIED_THE_MUTEX = """\
Using 'git status' to determine working set for adaptive non-unity build (C:\\Projects\\Cataclysm).
Unhandled exception: UnauthorizedAccessException: Access to the path\
 'Global\\UnrealBuildTool_Mutex_096b8b11a8779fc55e9da244f4a8ecf62868c83d' is denied.
Result: Failed (OtherCompilationError)
Total execution time: 0.23 seconds
"""

#: The build the open editor refuses. The line is Unreal's own; CLAUDE.md says
#: to ask the editor's owner, not to wait, so this must not be retried.
BUILD_REFUSED_BY_LIVE_CODING = """\
Unable to build while Live Coding is active. Exit the editor and game, or press\
 Ctrl+Alt+F11 if iterating on code in the editor or game
Result: Failed (OtherCompilationError)
Total execution time: 4.51 seconds
"""

#: Real lines from `game/Saved/Logs/Cataclysm.log`, captured 2026-08-04. The
#: failing line is written in the same shape the runner uses for a success.
TEST_LOG = """\
LogAutomationController: Display: Test Completed. Result={Success} Name={AttributeDefaults}
LogAutomationController: Display: Test Completed. Result={Success} Name={DamageRoutesThroughMetaAttribute}
LogAutomationController: Display: Test Completed. Result={Fail} Name={AProjectileHitsWhatItPassedThrough}
LogAutomationController: Display: ...Automation Test Queue Empty 141 tests performed.
"""


def outcome(text: str, returncode: int = 0) -> BuildOutcome:
    result, compiled, up_to_date, actions = parse_build_output(text)
    return BuildOutcome(returncode, text, result, compiled, up_to_date, actions)


@pytest.mark.parametrize("function", [unreal_build.build, unreal_build.run_automation_tests])
def test_no_timeout_is_the_default_on_a_build_or_a_test_run(function) -> None:
    """Issue #1580. CLAUDE.md: never put a timeout on an Unreal build.

    `build()` defaulted to 1800 seconds and passed it to `subprocess.run`,
    which on expiry kills the command interpreter and nothing under it: the
    compile workers go on holding the build mutex, which is exactly the state
    the rule exists to prevent. A build started with `-WaitMutex` also counts
    its wait for another session's build against the same limit. The test run
    is held to the same default because `python tools/unreal_build.py tests`
    builds first and a limit on the second half is the same trap one step
    later. This reads the signature, so a finite default cannot come back
    silently; it failed against the 1800.0 default before the change.
    """
    default = inspect.signature(function).parameters["timeout"].default
    assert default is None, (
        f"{function.__name__} defaults timeout to {default!r}; CLAUDE.md forbids a "
        f"timeout on an Unreal build (issue #1580), so the default must be None")


class _Builds:
    """A stand-in for `run_build_once` that hands out outcomes in order and
    remembers how many times it was asked and what `build()` waited."""

    def __init__(self, *texts: tuple[str, int]) -> None:
        self.queue = [outcome(text, code) for text, code in texts]
        self.runs = 0
        self.waits: list[float] = []

    def run(self, target, platform, configuration, timeout) -> BuildOutcome:
        self.runs += 1
        return self.queue.pop(0)

    def wait(self, seconds: float) -> None:
        self.waits.append(seconds)


def _building_with(monkeypatch: pytest.MonkeyPatch, builds: _Builds) -> None:
    monkeypatch.setattr(unreal_build, "run_build_once", builds.run)
    monkeypatch.setattr(unreal_build, "BUILD_BATCH_FILE",
                        pathlib.Path(__file__))  # any file that exists


def test_a_mutex_denial_is_retried_and_the_second_build_is_returned(
        monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture) -> None:
    """Issues #1577 and #1802. The runner had the machine for the first
    attempt; the retry a minute later built. Two runs, one wait, the success."""
    builds = _Builds((BUILD_DENIED_THE_MUTEX, 6), (BUILD_THAT_COMPILED, 0))
    _building_with(monkeypatch, builds)

    result = unreal_build.build(wait=builds.wait)

    assert result.succeeded and result.compiled == ("Module.Cataclysm.cpp",)
    assert builds.runs == 2
    assert builds.waits == [unreal_build.MUTEX_RETRY_WAIT_SECONDS]
    assert "waiting" in capsys.readouterr().out, "the wait must be visible"


def test_a_live_coding_refusal_is_not_retried(monkeypatch: pytest.MonkeyPatch) -> None:
    """The editor is open. CLAUDE.md: ask its owner, do not wait."""
    builds = _Builds((BUILD_REFUSED_BY_LIVE_CODING, 6), (BUILD_THAT_COMPILED, 0))
    _building_with(monkeypatch, builds)

    result = unreal_build.build(wait=builds.wait)

    assert not result.succeeded
    assert builds.runs == 1 and builds.waits == []


def test_a_compile_error_is_not_retried(monkeypatch: pytest.MonkeyPatch) -> None:
    """A quick failure is not a denial; the text is what decides."""
    builds = _Builds((BUILD_THAT_FAILED, 6), (BUILD_THAT_COMPILED, 0))
    _building_with(monkeypatch, builds)

    result = unreal_build.build(wait=builds.wait)

    assert not result.succeeded
    assert builds.runs == 1 and builds.waits == []


def test_a_denial_that_never_lifts_is_given_up_on_and_named(
        monkeypatch: pytest.MonkeyPatch) -> None:
    """Bounded: `attempts` builds in all, then the last outcome comes back,
    and `require_compiled` names the runner rather than a compiler error."""
    builds = _Builds(*([(BUILD_DENIED_THE_MUTEX, 6)] * 3))
    _building_with(monkeypatch, builds)

    result = unreal_build.build(attempts=3, wait=builds.wait)

    assert not result.succeeded
    assert builds.runs == 3 and len(builds.waits) == 2
    with pytest.raises(BuildDidNothing, match="runner holds the UnrealBuildTool mutex"):
        require_compiled(result, ["game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp"])


def test_the_retry_limit_default_is_the_documented_one() -> None:
    """So a change to the constant and a change to the signature cannot drift."""
    default = inspect.signature(unreal_build.build).parameters["attempts"].default
    assert default == unreal_build.MUTEX_RETRY_ATTEMPTS


# WHAT THE TREE DECLARES, PRINTED BESIDE WHAT THE RUN PERFORMED. Issue #1707.

A_SOURCE_WITH_BOTH_MACRO_FORMS = '''\
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeDefaultsTest,
\t"Cataclysm.AbilitySystem.AttributeDefaults",
\tEAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

CATACLYSM_AILMENT_TEST(FCataclysmAilmentApplicationTest,
\t"Cataclysm.Ailments.ChanceAboveCertaintyBecomesMagnitude")

// A comment naming IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNotATest, "Cataclysm.Not.Declared") is not a declaration.
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIndentedTest, "Cataclysm.Not.AtLineStart", 0)
'''


def test_declared_tests_reads_both_macro_forms_and_only_at_line_start(
        tmp_path: pathlib.Path) -> None:
    (tmp_path / "ATests.cpp").write_text(A_SOURCE_WITH_BOTH_MACRO_FORMS, encoding="utf-8")
    assert unreal_build.declared_tests(tmp_path) == (
        "Cataclysm.AbilitySystem.AttributeDefaults",
        "Cataclysm.Ailments.ChanceAboveCertaintyBecomesMagnitude")


def test_the_real_tree_declares_a_thousand_or_more() -> None:
    """The control: a reader that found nothing would report a gap of minus
    everything. 1,926 were declared when this landed (2026-09-16)."""
    assert len(unreal_build.declared_tests()) >= 1000


def test_the_declared_line_names_what_the_run_did_not_report() -> None:
    declared = ("Cataclysm.A.Ran", "Cataclysm.B.AlsoRan", "Cataclysm.C.NeverRan")
    run = TestOutcome(2, ("Ran",), ("AlsoRan",))
    line = unreal_build.declared_line(run, declared, "abc1234")
    assert "3 tests in the tree at abc1234" in line
    assert "2 performed, gap 1" in line
    assert "not reported by the run" in line and "Cataclysm.C.NeverRan" in line
    assert "Ran" in line and "AlsoRan" not in line.split("not reported")[1]
    assert "last segment" in line, "short log names must be compared by last segment"


def test_the_declared_line_compares_full_names_when_the_log_gives_them() -> None:
    """Two creatures declare `ItWearsItsMeshAndHidesThePlaceholder`; a log that
    names the group must not let one twin stand in for the other."""
    declared = ("Cataclysm.Imp.Twin", "Cataclysm.Brute.Twin")
    run = TestOutcome(1, ("Cataclysm.Imp.Twin",), ())
    line = unreal_build.declared_line(run, declared, "abc1234")
    assert "Cataclysm.Brute.Twin" in line.split("not reported")[1]
    assert "full name" in line


def test_the_declared_line_says_so_when_nothing_is_missing() -> None:
    run = TestOutcome(1, ("Cataclysm.A.Ran",), ())
    line = unreal_build.declared_line(run, ("Cataclysm.A.Ran",), "abc1234")
    assert "every declared test was reported by the run" in line
    assert "gap 0" in line


def test_a_build_that_compiled_is_read_correctly() -> None:
    built = outcome(BUILD_THAT_COMPILED)
    assert built.succeeded
    assert built.compiled == ("Module.Cataclysm.cpp",)
    assert not built.up_to_date
    assert built.actions == 4


def test_a_build_that_did_nothing_is_not_treated_as_a_rebuild() -> None:
    """The exact failure issue #139 records. It succeeded and it built nothing."""
    built = outcome(BUILD_THAT_DID_NOTHING)
    assert built.succeeded, "UnrealBuildTool really does report success here"
    assert built.compiled == ()
    assert built.up_to_date
    assert built.actions == 0


def test_a_failed_build_is_read_as_failed_despite_the_compile_line() -> None:
    """`Result:` alone is not enough, and a compile line is not proof of success.

    This build printed `Compile [x64] CataclysmProjectile.cpp` and then failed.
    Grepping for `Result:` without reading the word after it, or taking a compile
    line as evidence, both get this wrong.
    """
    built = outcome(BUILD_THAT_FAILED, returncode=6)
    assert built.result == "Failed"
    assert not built.succeeded
    assert built.compiled == ("CataclysmProjectile.cpp",)


@pytest.mark.parametrize(("path", "expected"), [
    ("game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp", "Cataclysm"),
    ("game/Source/CataclysmEmpire/DayClock.cpp", "CataclysmEmpire"),
    ("game\\Source\\CataclysmEditor\\Import.cpp", "CataclysmEditor"),
    ("tools/prove_guard.py", None),
])
def test_the_module_is_read_from_the_path(path: str, expected: str | None) -> None:
    assert module_of(path) == expected


def test_a_file_counts_as_rebuilt_when_its_module_blob_was_compiled() -> None:
    """The unity build is why this cannot demand the filename.

    UnrealBuildTool merges a module's .cpp files into `Module.<Module>.cpp`, and
    only compiles a file under its own name while `git status` reports it
    modified. So the build made while a file is broken names the file, and the
    build made after restoring it names the blob. Both mean the code was rebuilt.
    """
    built = outcome(BUILD_THAT_COMPILED)
    assert built.compiled_the_file(
        "game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp")
    assert not built.compiled_the_file(
        "game/Source/CataclysmEmpire/DayClock.cpp"), (
        "a Cataclysm module blob must not count as rebuilding a CataclysmEmpire file")


def test_a_file_counts_as_rebuilt_when_it_was_compiled_by_name() -> None:
    built = outcome(BUILD_THAT_FAILED, returncode=0)
    assert built.compiled_the_file(
        "game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp")


def test_requiring_a_rebuild_raises_when_the_build_did_nothing() -> None:
    with pytest.raises(BuildDidNothing, match="did not rebuild"):
        require_compiled(outcome(BUILD_THAT_DID_NOTHING),
                         ["game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp"])


def test_requiring_a_rebuild_raises_when_the_build_failed() -> None:
    with pytest.raises(BuildDidNothing, match="did not succeed"):
        require_compiled(outcome(BUILD_THAT_FAILED, returncode=6),
                         ["game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp"])


def test_requiring_a_rebuild_passes_when_the_module_was_compiled() -> None:
    require_compiled(outcome(BUILD_THAT_COMPILED),
                     ["game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp"])


def test_the_test_log_is_read_rather_than_standard_output() -> None:
    tests = parse_test_log(TEST_LOG)
    assert tests.performed == 141
    assert tests.succeeded == ("AttributeDefaults", "DamageRoutesThroughMetaAttribute")
    assert tests.failed == ("AProjectileHitsWhatItPassedThrough",)
    assert tests.any_failed
    assert "AProjectileHitsWhatItPassedThrough" in tests.summary


#: A run in which two tests passed while checking half of what they are named
#: for. Both routes the reporting helper writes are present, because it writes
#: the line twice on purpose: once through the automation controller's event
#: block and once straight to the log under LogCataclysm.
TEST_LOG_WITH_SKIPS = """\
LogCataclysm: Display: CATACLYSM_SKIPPED_HALF Cataclysm.Brute.ItLobsTheRock -- \
No skeleton with a weapon_r bone. The launch height is not checked.
LogAutomationController: CATACLYSM_SKIPPED_HALF Cataclysm.Brute.ItLobsTheRock -- \
No skeleton with a weapon_r bone. The launch height is not checked.
LogAutomationController: Display: Test Completed. Result={Success} Name={ItLobsTheRock}
LogCataclysm: Display: CATACLYSM_SKIPPED_HALF Cataclysm.Warden.ItSequencesClips -- \
The Paragon Grux pack is not present, so there are no clips to sequence.
LogAutomationController: CATACLYSM_SKIPPED_HALF Cataclysm.Warden.ItSequencesClips -- \
The Paragon Grux pack is not present, so there are no clips to sequence.
LogAutomationController: Display: Test Completed. Result={Success} Name={ItSequencesClips}
LogAutomationController: Display: ...Automation Test Queue Empty 2 tests performed.
"""


class TestASkippedHalfIsReported:
    """A test that checks nothing still counts as a pass. Issue #467.

    WHAT WAS WRONG. Fifteen automation tests take a shorter path when the Paragon
    art packs are absent, and each said so in its own wording. The run's summary
    read "22 tests performed, 22 succeeded, 0 failed" whether or not any of them
    had a subject left, and finding out meant knowing all fifteen wordings and
    grepping the log by hand.
    """

    def test_a_skipped_half_is_read_out_of_the_log(self) -> None:
        tests = parse_test_log(TEST_LOG_WITH_SKIPS)
        assert tests.skipped_half == (
            "Cataclysm.Brute.ItLobsTheRock", "Cataclysm.Warden.ItSequencesClips")

    def test_the_two_routes_are_counted_once(self) -> None:
        """The helper writes each line twice on purpose; it is one skip."""
        assert len(parse_test_log(TEST_LOG_WITH_SKIPS).skipped_half) == 2

    def test_the_summary_says_so_even_though_nothing_failed(self) -> None:
        """The whole point: a clean-looking run that checked less than it says."""
        summary = parse_test_log(TEST_LOG_WITH_SKIPS).summary
        assert "2 succeeded, 0 failed" in summary
        assert "2 skipped part of what they check" in summary
        assert "Cataclysm.Brute.ItLobsTheRock" in summary

    def test_a_run_with_no_skips_says_nothing_about_them(self) -> None:
        """No noise on the ordinary case, which is every run on this machine."""
        tests = parse_test_log(TEST_LOG)
        assert tests.skipped_half == ()
        assert "skipped part of what they check" not in tests.summary

    def test_a_skipped_half_is_not_a_failure(self) -> None:
        """It is a warning to a reader, not a broken build.

        Continuous integration has no Paragon art and never will, so treating
        this as a failure would make every run there red for a reason nobody can
        fix. What was missing was the reader being told.
        """
        assert not parse_test_log(TEST_LOG_WITH_SKIPS).any_failed

    def test_python_and_cpp_spell_the_token_the_same(self) -> None:
        """The two halves of this are in different languages and cannot import
        each other, so the token is compared as text.

        A renamed token on either side would silently stop every skip being
        reported, and nothing else would notice: the tests would still pass and
        the summary would still look clean, which is the exact state this whole
        issue is about.
        """
        header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
                  / "CataclysmTestSkip.h")
        assert header.is_file(), f"{header} does not exist"

        text = header.read_text(encoding="utf-8")
        match = re.search(r'Marker\s*=\s*TEXT\("([^"]+)"\)', text)
        assert match, (
            "could not find the Marker constant in CataclysmTestSkip.h. If it "
            "was renamed, rename it here too.")

        assert match.group(1) in unreal_build.SKIPPED_HALF.pattern, (
            f"the C++ helper writes {match.group(1)!r} and the Python reader "
            f"looks for {unreal_build.SKIPPED_HALF.pattern!r}.")


#: A run in which the engine refused to register a test because another class
#: already held its name. Taken from a real log, with the surrounding module-load
#: lines kept because that is where it appears: registration happens when the
#: game module loads, not when a test is selected.
TEST_LOG_WITH_A_REFUSED_REGISTRATION = """\
LogModuleManager: InternalLoadLibrary: 'Cataclysm' ('.../UnrealEditor-Cataclysm.dll')
LogAutomationTest: Warning: Failed to register test with the name \
'FCataclysmWeaponSubTypeTest'. Test with the same name is already registered \
and will not be overridden.
LogModuleManager: InternalLoadLibrary: 'CataclysmEmpire' ('.../UnrealEditor-CataclysmEmpire.dll')
LogAutomationController: Display: Test Completed. Result={Success} Name={ItLobsTheRock}
LogAutomationController: Display: ...Automation Test Queue Empty 1 tests performed.
"""


#: The same run with two refusals, so the plural wording is executed by
#: something rather than only written. One collision is what happened in issue
#: #1666; two is what happens the moment somebody adds a second, and the message
#: has to read properly then without anybody revisiting it.
TEST_LOG_WITH_TWO_REFUSED_REGISTRATIONS = """\
LogAutomationTest: Warning: Failed to register test with the name \
'FCataclysmWeaponSubTypeTest'. Test with the same name is already registered \
and will not be overridden.
LogAutomationTest: Warning: Failed to register test with the name \
'FCataclysmArmorCurveTest'. Test with the same name is already registered \
and will not be overridden.
LogAutomationController: Display: Test Completed. Result={Success} Name={ItLobsTheRock}
LogAutomationController: Display: ...Automation Test Queue Empty 1 tests performed.
"""


class TestARefusedRegistrationIsReported:
    """A test the engine refused to register vanishes without trace. Issue #1736.

    WHAT WAS WRONG. Unreal registers an automation test under its C++ CLASS name
    in a map that compares keys without regard to case. Two classes one letter's
    capitalisation apart are one key; the second is refused, does not run, is not
    listed, and is not counted. The run then reports "15 tests performed, 15
    succeeded, 0 failed", which is what a run with nothing missing also reports.

    That happened, and the test was gone for a month -- issue #1666. The warning
    below was printed at every editor start throughout and nobody read it,
    because reading it means finding one line in a log of roughly 370,000
    characters.

    WHY THIS IS NOT THE SAME AS A SKIPPED HALF, which is the class just above.
    A skipped half is expected: the Paragon art is absent from every worktree and
    from continuous integration, and it can never be fixed there. A refused
    registration is always a defect, and it always means the count is wrong.
    """

    def test_the_refused_test_is_read_out_of_the_log(self) -> None:
        tests = parse_test_log(TEST_LOG_WITH_A_REFUSED_REGISTRATION)
        assert tests.refused_registration == ("FCataclysmWeaponSubTypeTest",)

    def test_the_summary_says_so_even_though_nothing_failed(self) -> None:
        """The whole point: a clean-looking run with a test silently missing."""
        summary = parse_test_log(TEST_LOG_WITH_A_REFUSED_REGISTRATION).summary
        assert "1 succeeded, 0 failed" in summary
        assert "FCataclysmWeaponSubTypeTest" in summary
        assert "refused to register" in summary

    def test_one_refusal_is_described_in_the_singular(self) -> None:
        """A message that cannot count is a message nobody trusts.

        THIS EXISTS BECAUSE THE FIRST VERSION GOT IT WRONG. It read "refused to
        register 1, so they did not run", which a reader meets at the moment
        they have just been told their test run is missing something. A tool
        that cannot say "one test" is not one they will believe about anything
        harder.
        """
        summary = parse_test_log(TEST_LOG_WITH_A_REFUSED_REGISTRATION).summary
        assert "refused to register 1 test," in summary, (
            f"one refusal must say '1 test', not '1'. It reads: {summary!r}")
        assert "so it never ran and is not in the counts above" in summary, (
            f"one refusal must be 'it ... is', not 'they ... are'. It reads: "
            f"{summary!r}")
        assert "That is a C++ class name" in summary

    def test_two_refusals_are_described_in_the_plural(self) -> None:
        """The reading that had never been executed at all.

        EVERY FIXTURE AND ASSERTION WRITTEN FOR THIS FEATURE HAD EXACTLY ONE
        REFUSED CLASS, so the plural branch of the sentence was text nobody had
        run -- which is the same fault as a declared test that never runs, in
        the output this file produces. Issue #1666 is that fault in C++; this is
        it in a format string.
        """
        summary = parse_test_log(TEST_LOG_WITH_TWO_REFUSED_REGISTRATIONS).summary
        assert "refused to register 2 tests," in summary, (
            f"two refusals must say '2 tests'. It reads: {summary!r}")
        assert "so they never ran and are not in the counts above" in summary, (
            f"two refusals must be 'they ... are'. It reads: {summary!r}")
        assert "Those are C++ class names" in summary
        assert "FCataclysmArmorCurveTest" in summary
        assert "FCataclysmWeaponSubTypeTest" in summary

    def test_it_names_the_class_rather_than_the_test(self) -> None:
        """Because the class name is what collides, and what has to be renamed.

        A READER GIVEN THE READABLE TEST NAME COULD NOT ACT ON IT. The engine
        does not know which readable name was lost -- the instance that would
        have carried it never registered. What it can say is which C++ class was
        refused, and that is the identifier somebody has to change.
        """
        (refused,) = parse_test_log(TEST_LOG_WITH_A_REFUSED_REGISTRATION).refused_registration
        assert refused.startswith("FCataclysm")
        assert "." not in refused, (
            "a readable test name was captured instead of the C++ class name")

    def test_a_run_with_no_refusals_says_nothing_about_them(self) -> None:
        """No noise on the ordinary case, which is every healthy run."""
        tests = parse_test_log(TEST_LOG)
        assert tests.refused_registration == ()
        assert "refused to register" not in tests.summary

    def test_a_refusal_is_not_counted_as_a_failed_test(self) -> None:
        """No test failed. A test is missing, which is a different thing.

        `any_failed` feeds `CppGuardResult`, where it means "a guard noticed the
        break". A refusal must not read as that, or a guard proof would report a
        break as caught when the only thing that happened was a name collision.
        """
        assert not parse_test_log(TEST_LOG_WITH_A_REFUSED_REGISTRATION).any_failed

    def test_python_and_the_engine_spell_the_warning_the_same(self) -> None:
        """The engine's wording is the whole interface and nothing pins it.

        UNLIKE THE SKIPPED-HALF TOKEN, WHICH THIS PROJECT WRITES, this sentence
        belongs to Unreal and can change in an engine upgrade. If it does, every
        refusal goes unreported again and nothing else notices. So the engine's
        own source is read and compared, and this fails on the upgrade that
        changes it rather than months later.
        """
        source = pathlib.Path(
            "C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Core"
            "/Private/Misc/AutomationTest.cpp")
        if not source.is_file():
            pytest.skip(f"the engine source is not present at {source}")

        text = source.read_text(encoding="utf-8", errors="replace")
        assert "Failed to register test with the name" in text, (
            "the engine no longer prints 'Failed to register test with the "
            "name', so unreal_build.REFUSED_REGISTRATION matches nothing and "
            "every refused registration is silent again. Find the new wording "
            "in FAutomationTestBase::FAutomationTestBase and update the "
            "pattern.")


def test_an_empty_log_reports_nothing_rather_than_success() -> None:
    """A run that wrote no results must not read as a clean pass.

    The automation runner writes nothing useful to standard output, so an empty
    or missing log is the shape a crashed run takes.
    """
    tests = parse_test_log("")
    assert tests.performed is None
    assert tests.succeeded == ()
    assert not tests.any_failed
    assert "unknown tests performed" in tests.summary


def test_restoring_a_file_leaves_it_newer_than_the_break(tmp_path: pathlib.Path) -> None:
    """The "touch the file after restoring it" from issue #139.

    The modification time is forced backwards first, which is exactly what
    `shutil.copy2` and `cp -p` do, and is the state that makes UnrealBuildTool
    skip the file.
    """
    source = tmp_path / "Thing.cpp"
    source.write_bytes(b"int Thing() { return 1; }\n")

    broken_at = time.time()
    stale = broken_at - 600.0
    os.utime(source, (stale, stale))
    assert source.stat().st_mtime < broken_at

    restore_and_touch(source, b"int Thing() { return 2; }\n", broken_at)

    assert source.read_bytes() == b"int Thing() { return 2; }\n"
    assert source.stat().st_mtime > broken_at, (
        "the restored file still looks older than the build made from the broken "
        "version, so UnrealBuildTool would skip it. See issue #139.")


# ---------------------------------------------------------------------------
# Running it as a command
#
# WHY THESE EXIST. Issue #436. This file had no `__main__` block, so
# `python tools/unreal_build.py --tests` imported the module, printed nothing
# and exited 0. That was run three times before anyone noticed, because an exit
# code of 0 with no output is what a successful run looks like once its output
# has been filtered. It is the same fault the rest of this file guards against
# -- work reported as done that was never done -- at the front door instead of
# in the build.
#
# NONE OF THESE START A BUILD. The two that run the script as a command only
# reach the argument parser; the rest call functions directly. The whole group
# is as fast as the tests above it and runs in continuous integration, which has
# no engine.
# ---------------------------------------------------------------------------


def run_the_script(*arguments: str) -> subprocess.CompletedProcess[str]:
    """Run `unreal_build.py` as a command, with arguments that never build."""
    return subprocess.run(
        [sys.executable, str(BUILD_SCRIPT), *arguments],
        capture_output=True, text=True, check=False)


def test_running_it_with_no_arguments_fails_instead_of_doing_nothing() -> None:
    """The exact regression issue #436 is about.

    THE FAILURE THIS EXISTS FOR is somebody removing the entry point, or moving
    these functions into another module and leaving this one importable but
    inert. Both put the file back in the state where every invocation of it
    reports success without running anything.
    """
    finished = run_the_script()

    assert finished.returncode != 0, (
        "python tools/unreal_build.py with no arguments exited 0. That is "
        "indistinguishable from a successful build-and-test run whose output "
        "was filtered away, which is how issue #436 happened: it was run three "
        "times and reported nothing three times.")

    assert (finished.stdout + finished.stderr).strip(), (
        "it exited non-zero but printed nothing at all, so a caller has no way "
        "to tell what went wrong.")


def test_an_unrecognised_command_fails_rather_than_being_ignored() -> None:
    """A misspelled subcommand must not silently do nothing."""
    finished = run_the_script("tets")

    assert finished.returncode != 0, (
        "an unrecognised subcommand exited 0, so a typo in a build script would "
        "look like a passing run.")


def test_both_spellings_of_the_two_commands_are_accepted() -> None:
    """`--tests` is the spelling recorded in issue #436, so somebody will type it.

    Parsed rather than run: the alternative is a test that starts a real Unreal
    build in order to find out whether a flag was spelled correctly.
    """
    assert parse_arguments(["tests"]).command == "tests"
    assert parse_arguments(["--tests"]).command == "tests"
    assert parse_arguments(["build"]).command == "build"
    assert parse_arguments(["--build"]).command == "build"


def test_the_test_prefix_and_target_can_be_chosen() -> None:
    """Because proving one guard should not mean running all 204 tests."""
    parsed = parse_arguments(["tests", "--prefix", "Cataclysm.Brute"])
    assert parsed.prefix == "Cataclysm.Brute"
    assert not parsed.no_build

    assert parse_arguments(["tests", "--no-build"]).no_build


def test_a_run_that_read_no_results_is_a_failure() -> None:
    """Nothing ran is not the same as nothing failed.

    `TestOutcome(None, (), ())` is what comes back when the log could not be
    read: the editor never started, the run crashed, the log was locked. This is
    issue #436 one level further in, and reporting it as a pass would be the
    same mistake in a different place.
    """
    assert exit_code_for(TestOutcome(None, (), ())) != 0
    assert exit_code_for(None) != 0


def test_a_run_that_performed_zero_tests_is_a_failure() -> None:
    """The same thing said differently, and it really happens.

    A test prefix that matches nothing produces this: the run is real, the log
    is readable, and no test was executed.
    """
    assert exit_code_for(TestOutcome(0, (), ())) != 0


def test_a_failing_test_is_a_failure() -> None:
    assert exit_code_for(TestOutcome(3, ("a", "b"), ("c",))) != 0


def test_a_clean_run_is_the_only_success() -> None:
    assert exit_code_for(TestOutcome(3, ("a", "b", "c"), ())) == 0


def test_a_refused_registration_is_a_failure() -> None:
    """Every test that ran passed, and the run is still wrong. Issue #1736.

    THIS IS THE ONE CASE WHERE EVERY NUMBER IN THE REPORT IS FINE AND THE REPORT
    IS NOT. Three performed, three succeeded, none failed -- and a fourth test
    exists in the source that the engine refused to register, so it did not run
    and is not in any of those three numbers.

    Exiting zero here is the same fault as issue #436, which was a command that
    did nothing and said nothing: a caller who checks the exit code, as
    `CLAUDE.md` tells them to, gets "everything is fine" from a run that lost a
    test.
    """
    assert exit_code_for(
        TestOutcome(3, ("a", "b", "c"), (), (), ("FSomeTest",))) != 0


def test_a_skipped_half_is_still_not_a_failure() -> None:
    """The contrast that makes the case above mean something.

    A GUARD THAT FAILED ON BOTH WOULD BE USELESS HERE, because continuous
    integration and every worktree lack the Paragon art and report skipped halves
    on every run. If this started failing too, the exit code would carry no
    information at all.
    """
    assert exit_code_for(
        TestOutcome(3, ("a", "b", "c"), (), ("Cataclysm.Brute.Whatever",))) == 0


# ---------------------------------------------------------------------------
# Proving a guard on a constant that lives in a header
#
# WHY THESE EXIST. Issue #384. Most of the constants this project guards are
# `static constexpr float` in a header, and `prove_cpp_guard` raised
# BuildDidNothing on every one of them -- on builds that had plainly rebuilt
# everything that includes the header. Two separate causes, both below.
# ---------------------------------------------------------------------------

#: A real build, captured in issue #384, after breaking a constant in
#: `CataclysmCharacterBase.h`. It rebuilt everything that includes that header
#: and `require_compiled` raised anyway.
#:
#: THE TWO THINGS TO NOTICE. No line names a `.h`, because a header is never
#: compiled. And the unity blob is SPLIT AND NUMBERED, so neither chunk equals
#: `Module.Cataclysm.cpp`, which is the only spelling the old fallback knew.
BUILD_AFTER_A_HEADER_BREAK = """\
Building CataclysmEditor...
Using Unreal Build Accelerator local executor to run 9 action(s)
[1/9] Compile [x64] CataclysmBruteCharacter.cpp
[2/9] Compile [x64] CataclysmBruteTests.cpp
[3/9] Compile [x64] CataclysmCharacterBase.cpp
[4/9] Compile [x64] CataclysmEnemyBehaviourTests.cpp
[5/9] Compile [x64] CataclysmEnemyController.cpp
[6/9] Compile [x64] Module.Cataclysm.1.cpp
[7/9] Compile [x64] Module.Cataclysm.2.cpp
[8/9] Link [x64] UnrealEditor-Cataclysm.lib
[9/9] Link [x64] UnrealEditor-Cataclysm.dll
Result: Succeeded
"""

#: The header whose constants issue #384 was found on. A real path, because
#: `compiled_a_neighbour_of` reads the directory rather than guessing.
CHARACTER_BASE_HEADER = "game/Source/Cataclysm/Character/CataclysmCharacterBase.h"


def test_a_header_counts_as_rebuilt_when_its_neighbours_were_compiled() -> None:
    """The exact case issue #384 reports, from the build output it reports.

    A header is never compiled, so it can never appear in a compile line. Before
    this, `require_compiled` raised on every header path no matter what the
    build had done.
    """
    built = outcome(BUILD_AFTER_A_HEADER_BREAK)
    assert built.succeeded

    assert built.compiled_the_file(CHARACTER_BASE_HEADER), (
        "a header break is still reported as a build that did nothing, which is "
        "issue #384: the build compiled seven files including the header's own "
        "neighbours, and this said it had not been rebuilt.")

    # AND IT GOES THROUGH require_compiled, which is what prove_cpp_guard calls
    # and where the exception was actually raised.
    require_compiled(built, [CHARACTER_BASE_HEADER])


#: A build with no unity blob line at all: every file compiled under its own
#: name. This is what the adaptive non-unity working set produces when the
#: modified files are the ones being compiled, and it is the case where the
#: blob fallback has nothing to match.
BUILD_WITH_NO_UNITY_BLOB = """\
Building CataclysmEditor...
Using Unreal Build Accelerator local executor to run 3 action(s)
[1/3] Compile [x64] CataclysmCharacterBase.cpp
[2/3] Link [x64] UnrealEditor-Cataclysm.lib
Result: Succeeded
"""


def test_a_header_is_proved_by_a_cpp_beside_it_when_there_is_no_unity_blob() -> None:
    """The half of issue #384 the numbering fix does not cover.

    WHY THIS IS A SEPARATE TEST, and it was not until the guards were proved:
    the build captured in issue #384 contains numbered unity chunks, so
    recognising those alone makes that case pass. Deleting the header handling
    left it passing, which meant nothing exercised the header handling at all.

    Here there is no blob line to fall back on. A header is never compiled, so
    the only evidence available is that a .cpp beside it was.
    """
    built = outcome(BUILD_WITH_NO_UNITY_BLOB)

    assert not built.compiled_the_unity_blob("Cataclysm"), (
        "this capture is supposed to have no unity blob line; if it gained one "
        "this test stops isolating what it is about")

    assert built.compiled_the_file(CHARACTER_BASE_HEADER), (
        "a header cannot be proved rebuilt when its module compiled without a "
        "unity blob, even though the .cpp beside it was compiled by name.")


def test_a_numbered_unity_chunk_counts_for_a_cpp_as_well() -> None:
    """The second cause, which is not limited to headers.

    UnrealBuildTool splits a large module's unity blob into numbered chunks. Any
    .cpp whose code landed in one rather than being compiled under its own name
    would have hit the same wall. It had not bitten before only because a
    modified file is compiled under its own name.
    """
    built = outcome(BUILD_AFTER_A_HEADER_BREAK)

    assert built.compiled_the_unity_blob("Cataclysm"), (
        "Module.Cataclysm.1.cpp and Module.Cataclysm.2.cpp are not recognised "
        "as the Cataclysm module's unity blob, so any file whose code landed in "
        "one is reported as not rebuilt.")

    assert built.compiled_the_file(
        "game/Source/Cataclysm/Character/SomethingNotCompiledByName.cpp")


def test_a_header_still_raises_when_the_build_did_nothing() -> None:
    """The issue #139 failure must still be caught, header or not.

    THIS IS THE ONE THAT MATTERS. Making a header pass is easy; making it pass
    without also making every header pass unconditionally is the point. A build
    that reported success and compiled nothing is exactly the fault
    `require_compiled` exists for, and it has to keep raising.
    """
    built = outcome(BUILD_THAT_DID_NOTHING)
    assert built.succeeded, "UnrealBuildTool really does report success here"

    assert not built.compiled_the_file(CHARACTER_BASE_HEADER)
    with pytest.raises(BuildDidNothing):
        require_compiled(built, [CHARACTER_BASE_HEADER])


def test_a_header_is_not_proved_by_a_different_module_being_compiled() -> None:
    """Otherwise any build of anything would prove any header.

    The compiled file below is a real file in the Cataclysm module, but the
    header under test is in a made-up module, so neither the blob nor the
    neighbour check can match it.
    """
    built = outcome(BUILD_AFTER_A_HEADER_BREAK)

    assert not built.compiled_the_file(
        "game/Source/SomeOtherModule/Thing/Thing.h"), (
        "a header in a module this build never touched is reported as rebuilt, "
        "so the check would pass for anything.")


# ---------------------------------------------------------------------------
# The order prove_cpp_guard does things in, and what it reports when a step
# fails
#
# NONE OF THESE RUN A BUILD. `prove_cpp_guard` takes `builder` and `tester`
# for exactly this: one real run of it is four builds and several minutes, so
# until issue #384 nothing checked its sequencing at all.
# ---------------------------------------------------------------------------


class RecordedBuilds:
    """Hands out prepared build outcomes and remembers how often it was asked."""

    def __init__(self, *outcomes: BuildOutcome) -> None:
        self.remaining = list(outcomes)
        self.calls = 0

    def __call__(self, target: str) -> BuildOutcome:
        self.calls += 1
        return self.remaining.pop(0) if self.remaining else self.remaining[-1]


def a_source_file(tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> pathlib.Path:
    """A file `prove_cpp_guard` can break, outside the real repository."""
    monkeypatch.setattr("unreal_build.REPO_ROOT", tmp_path)
    source = tmp_path / "Thing.cpp"
    source.write_text("float Reach() { return 250.0f; }\n", encoding="utf-8")
    return source


def build_that_compiled_thing() -> BuildOutcome:
    return outcome("Building X...\n[1/1] Compile [x64] Thing.cpp\nResult: Succeeded\n")


def test_it_breaks_builds_tests_restores_and_rebuilds_in_that_order(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Two builds and TWO test runs, and the file back as it was.

    ONE TEST RUN UNTIL ISSUE #1663, which is why this docstring said so. The
    tests now run again after the restore, because a guard proof is two claims
    and this function produced only the first.
    """
    source = a_source_file(tmp_path, monkeypatch)
    original = source.read_bytes()

    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    seen_during_the_test_run: list[str] = []

    def tester(prefix: str) -> TestOutcome:
        # READ INSIDE THE TEST RUN, because "the tests ran against the broken
        # binary" is the whole claim prove_cpp_guard makes.
        seen_during_the_test_run.append(source.read_text(encoding="utf-8"))
        return TestOutcome(2, ("a",), ("b",))

    result = prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        test_prefix="Cataclysm.Thing", builder=builds, tester=tester)

    assert result.failed, "a failing test is what proves the guard"
    assert builds.calls == 2, "one build for the break and one for the restore"
    assert seen_during_the_test_run == ["float Reach() { return 0.0f; }\n",
                                       "float Reach() { return 250.0f; }\n"], (
        "the tests must run while the file is BROKEN and then again once it is "
        "RESTORED, in that order. A failure in the first run proves nothing on "
        "its own: a test that always fails produces the same thing. Issue "
        "#1663.")
    assert source.read_bytes() == original, "the file was not restored"


def test_a_bad_restore_build_raises_when_nothing_else_went_wrong(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """It must not be swallowed. A stale binary is issue #139 all over again."""
    a_source_file(tmp_path, monkeypatch)

    builds = RecordedBuilds(build_that_compiled_thing(),
                            outcome(BUILD_THAT_DID_NOTHING))

    with pytest.raises(BuildDidNothing):
        prove_cpp_guard(
            {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
            builder=builds, tester=lambda prefix: TestOutcome(2, ("a",), ("b",)))


def test_a_bad_restore_build_does_not_hide_the_failure_that_came_first(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Issue #384 reports two tracebacks, the second hiding the first.

    The break build here did nothing, which is the failure that explains the
    run. The restore build then failed outright. The caller must be told about
    the first, because that is the one that says why the run is worthless.
    """
    a_source_file(tmp_path, monkeypatch)

    builds = RecordedBuilds(
        outcome(BUILD_THAT_DID_NOTHING),          # the break build: succeeded, built nothing
        outcome(BUILD_THAT_FAILED, returncode=6),  # the restore build: failed outright
    )

    with pytest.raises(BuildDidNothing) as raised:
        prove_cpp_guard(
            {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
            builder=builds, tester=lambda prefix: TestOutcome(0, (), ()))

    assert "did not rebuild" in str(raised.value), (
        f"the exception that reached the caller describes the restore build "
        f"rather than the break that caused the run to be worthless: "
        f"{raised.value}")

    notes = getattr(raised.value, "__notes__", [])
    assert any("restore build was also unsatisfactory" in note for note in notes), (
        "the restore build's own problem was dropped entirely. It has to reach "
        "the caller too -- a repository left with a stale binary is the fault "
        "issue #139 is about.")


# ---------------------------------------------------------------------------
# Which worktree a failed restore left behind. Issue #1657.
#
# When the restore rebuild fails, prove_cpp_guard raises the same BuildDidNothing
# for two states that differ completely in how far the worktree can be trusted:
#
#   the break build FAILED    nothing ever ran; binaries are from the last good
#                             build, from correct source.        HARMLESS
#   the break build SUCCEEDED tests ran against broken binaries, and those
#                             binaries are still there.          DANGEROUS
#
# The session that found this reported the danger correctly and attributed it to
# the wrong one of the two, because the exception carries nothing that tells them
# apart. And the obvious diagnostic does not separate them either: building again
# reports actions in BOTH cases, because restore_and_touch deliberately forces the
# restored file's modification time past the break so the next build cannot skip
# it.
# ---------------------------------------------------------------------------


class BuildsThenRaises:
    """A builder that raises on its first call and answers on later ones.

    FOR THE ONE PATH THE HAPPY CASE NEVER EXERCISES. `broken_build` is assigned
    inside the `try`; if the build command itself raises, the name is never bound,
    and anything in the `finally` that reads it raises UnboundLocalError OVER the
    original exception. That is the fault issue #384 fixed, arriving from a new
    direction: the code that reports which case you are in destroying the report.
    """

    def __init__(self, error: BaseException, *later: BuildOutcome) -> None:
        self.error = error
        self.later = list(later)
        self.calls = 0

    def __call__(self, target: str) -> BuildOutcome:
        self.calls += 1
        if self.calls == 1:
            raise self.error
        return self.later.pop(0) if self.later else self.later[-1]


def test_a_failed_restore_after_a_compiled_break_says_the_binaries_hold_it(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """THE DANGEROUS STATE. The message must say so in as many words."""
    a_source_file(tmp_path, monkeypatch)

    builds = RecordedBuilds(build_that_compiled_thing(),       # the break compiled
                            outcome(BUILD_THAT_DID_NOTHING))   # the restore did not

    with pytest.raises(BuildDidNothing) as raised:
        prove_cpp_guard(
            {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
            builder=builds, tester=lambda prefix: TestOutcome(2, ("a",), ("b",)))

    # READ FROM THE NOTES, NOT THE MESSAGE. This file's existing test for the
    # restore failure does the same, because add_note is how this function
    # attaches context without replacing the exception that explains the run.
    message = " ".join(getattr(raised.value, "__notes__", []))
    assert "CONTAIN THE BREAK" in message, (
        "the break compiled and the tests ran against those binaries, and the "
        "restore rebuild then failed, so the binaries in this worktree still "
        f"hold the break. The message does not say so: {message}")


def test_a_failed_restore_after_a_break_that_did_not_compile_says_they_are_clean(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """THE HARMLESS STATE, and it must NOT be described as the dangerous one."""
    a_source_file(tmp_path, monkeypatch)

    builds = RecordedBuilds(outcome(BUILD_THAT_FAILED, returncode=6),  # break failed
                            outcome(BUILD_THAT_DID_NOTHING))           # restore too

    with pytest.raises(BuildDidNothing) as raised:
        prove_cpp_guard(
            {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
            builder=builds, tester=lambda prefix: TestOutcome(0, (), ()))

    message = " ".join(getattr(raised.value, "__notes__", []))
    assert "CONTAIN THE BREAK" not in message, (
        "the break never compiled, so nothing ever ran and the binaries are from "
        f"the last good build. Calling this dangerous is a false alarm: {message}")
    assert "last good build" in message, (
        f"the message does not say what state the worktree IS in: {message}")


def test_a_builder_that_raises_leaves_the_original_exception_intact(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The path the happy case never exercises, and the reason for one line.

    `broken_build` is assigned inside the `try`. A build command that raises --
    the mutex denied, the wrapper gone -- leaves it unbound, and the `finally`
    now reads it to say which case the caller is in. Without an initialisation
    before the `try`, that read raises UnboundLocalError and REPLACES the failure
    that explains the run.
    """
    a_source_file(tmp_path, monkeypatch)

    builds = BuildsThenRaises(RuntimeError("the build command exploded"),
                              outcome(BUILD_THAT_DID_NOTHING))

    with pytest.raises(RuntimeError) as raised:
        prove_cpp_guard(
            {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
            builder=builds, tester=lambda prefix: TestOutcome(0, (), ()))

    assert "the build command exploded" in str(raised.value), (
        "the exception that reached the caller is not the one that explains the "
        f"run: {raised.value!r}")

    notes = getattr(raised.value, "__notes__", [])
    assert any("restore build was also unsatisfactory" in note for note in notes), (
        "the restore build's own problem was dropped")
    assert not any("UnboundLocalError" in note for note in notes), (
        "reading broken_build in the finally raised over the real failure")


# ---------------------------------------------------------------------------
# Both halves of the proof. Issue #1663.
#
# A guard proof is two claims: the test FAILS without the fix, and the test
# PASSES with it. prove_cpp_guard only ever ran the tests once, with the break
# in place, so it produced the first and never the second -- and a test that
# fails for the wrong reason, or always, gives a byte-identical result.
# ---------------------------------------------------------------------------


class RunsSeen:
    """A tester that records the file's text and the prefix at each run."""

    def __init__(self, source: pathlib.Path, *outcomes: TestOutcome) -> None:
        self.source = source
        self.remaining = list(outcomes)
        self.texts: list[str] = []
        self.prefixes: list[str] = []

    def __call__(self, prefix: str) -> TestOutcome:
        # READ INSIDE THE RUN, because "which binary did these tests see" is the
        # whole question this file exists to answer.
        self.texts.append(self.source.read_text(encoding="utf-8"))
        self.prefixes.append(prefix)
        return self.remaining.pop(0) if self.remaining else TestOutcome(1, ("a",), ())


def test_the_tests_run_again_after_the_restore_and_both_halves_are_kept(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The second run must see the RESTORED file, not the broken one."""
    source = a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    runs = RunsSeen(source,
                    TestOutcome(2, ("a",), ("b",)),   # broken: the guard fired
                    TestOutcome(2, ("a", "b"), ()))   # restored: everything passes

    result = prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        test_prefix="Cataclysm.Thing", builder=builds, tester=runs)

    assert runs.texts == ["float Reach() { return 0.0f; }\n",
                          "float Reach() { return 250.0f; }\n"], (
        "the two runs did not see the broken file and then the restored one, "
        "so the pair says nothing")
    assert builds.calls == 2, (
        "the second test run must cost a RUN and not a build. The restore "
        "build at the end of the finally already leaves a correct binary.")
    assert result.named_failures == ("b",), "the broken half is unchanged"
    assert result.tests_restored is not None, "the restored half was not kept"
    assert result.tests_restored.failed == (), "the restored half should pass"


def test_a_test_that_always_fails_is_not_a_proof(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """THE WHOLE POINT OF THE ISSUE.

    This is the result that used to be indistinguishable from a good one: the
    named test failed while the files were broken, `crashed` is False, and the
    count matches. It also fails with the files restored, so it was never
    sensitive to the break at all.
    """
    source = a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    runs = RunsSeen(source,
                    TestOutcome(2, ("a",), ("b",)),   # broken: fails
                    TestOutcome(2, ("a",), ("b",)))   # restored: fails the same way

    result = prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        test_prefix="Cataclysm.Thing", builder=builds, tester=runs)

    assert result.named_failures == ("b",), (
        "the broken half looks exactly like a successful proof, which is why "
        "this was invisible")
    assert not result.proved, (
        "a test that fails in BOTH halves proves nothing, and this is the case "
        "prove_cpp_guard could not tell from a good one")
    assert "b" in result.summary, "the summary must name what failed afterwards"


def test_a_test_that_fails_broken_and_passes_restored_is_a_proof(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The pair held, which is the only result that means the guard works."""
    source = a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    runs = RunsSeen(source,
                    TestOutcome(2, ("a",), ("b",)),
                    TestOutcome(2, ("a", "b"), ()))

    result = prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        test_prefix="Cataclysm.Thing", builder=builds, tester=runs)

    assert result.proved, (
        "failing with the break in and passing with it out is the whole pair, "
        "and nothing else counts")


def test_a_break_the_test_never_noticed_is_not_a_proof_either(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Passing both times means the test is blind to the mechanism."""
    source = a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    runs = RunsSeen(source,
                    TestOutcome(2, ("a", "b"), ()),
                    TestOutcome(2, ("a", "b"), ()))

    result = prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        test_prefix="Cataclysm.Thing", builder=builds, tester=runs)

    assert not result.proved, "nothing noticed the break, so nothing is proved"


def test_both_halves_are_run_with_the_same_prefix(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Two halves selected differently cannot be compared to each other."""
    source = a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    runs = RunsSeen(source,
                    TestOutcome(2, ("a",), ("b",)),
                    TestOutcome(2, ("a", "b"), ()))

    prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        test_prefix="Cataclysm.Thing.", builder=builds, tester=runs)

    assert runs.prefixes == ["Cataclysm.Thing.", "Cataclysm.Thing."], (
        "the halves were selected differently, so a difference between them "
        "could be the selection rather than the code")


def test_a_run_that_raised_does_not_spend_a_second_test_run(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """An edit that changed nothing raises. There is nothing to pair."""
    source = a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(build_that_compiled_thing(), build_that_compiled_thing())
    runs = RunsSeen(source)

    with pytest.raises(ValueError):
        prove_cpp_guard({"Thing.cpp": lambda text: text},
                        builder=builds, tester=runs)

    assert runs.texts == [], (
        "a proof that never got as far as breaking the file must not spend an "
        "editor start on a second run")


def test_a_break_that_did_not_compile_pairs_nothing(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """No first half means no pair, and no second run to waste on it.

    A break that stops the file compiling never runs the tests, so there is
    nothing for a restored run to be compared against.
    """
    a_source_file(tmp_path, monkeypatch)
    builds = RecordedBuilds(outcome(BUILD_THAT_FAILED, returncode=6),
                            build_that_compiled_thing())
    runs = RunsSeen(tmp_path / "Thing.cpp")

    result = prove_cpp_guard(
        {"Thing.cpp": lambda text: text.replace("250.0f", "0.0f")},
        builder=builds, tester=runs)

    assert runs.texts == [], "no tests should have run at all"
    assert result.tests_restored is None, (
        "there is no first half to pair with, so claiming a second one would "
        "invite a reader to compare it against nothing")
    assert not result.proved


# ---------------------------------------------------------------------------
# A crashed run is not a guard verdict either way. Issue #1313.
# ---------------------------------------------------------------------------

def test_a_test_run_that_never_reported_a_count_is_a_crash() -> None:
    """`performed` is None when the engine never printed "Automation Test Queue
    Empty N tests performed", which it prints when the queue drains. A run that
    died part way through never gets there."""
    from unreal_build import TestOutcome
    assert TestOutcome(None, (), ()).crashed
    assert TestOutcome(0, (), ()).crashed, (
        "a run that drained its queue without running anything is the same "
        "state said differently")
    assert not TestOutcome(22, ("a",) * 22, ()).crashed


def test_a_crashed_run_is_not_reported_as_a_guard_that_did_not_fire() -> None:
    """The defect issue #1313 records. A crashed run reports no failures, and no
    failures used to mean the guard did not notice. It means nothing was
    measured."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    built = BuildOutcome(0, "", "Succeeded", ("CataclysmProjectile.cpp",),
                         False, 12)
    result = CppGuardResult(build=built, tests=TestOutcome(None, (), ()))
    assert result.crashed
    assert not result.failed
    assert "NO MEASUREMENT" in result.summary
    assert "#1313" in result.summary


def test_a_real_failure_is_still_a_guard_firing() -> None:
    """The half that matters more: a fix that called every quiet run a crash
    would report every working guard as worthless."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    built = BuildOutcome(0, "", "Succeeded", ("CataclysmProjectile.cpp",),
                         False, 12)
    tests = TestOutcome(22, ("a",) * 21, ("Cataclysm.Skills.ItHits",))
    result = CppGuardResult(build=built, tests=tests)
    assert not result.crashed
    assert result.failed
    assert "Cataclysm.Skills.ItHits" in result.summary


def test_a_build_that_did_not_compile_is_reported_as_such_not_as_a_crash():
    """The build failing is a different state again, and it was already
    distinguished. This says the crash check did not swallow it."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    failed_build = BuildOutcome(1, "error C2065", None, (), False, 0)
    result = CppGuardResult(build=failed_build,
                            tests=TestOutcome(None, (), ()))
    assert not result.crashed, (
        "a build that never compiled is reported as a crashed test run, which "
        "hides the actual reason")
    assert result.failed
    assert "the build itself failed" in result.summary


def test_named_failures_names_the_tests_that_noticed() -> None:
    """`CLAUDE.md` tells every session to prove a C++ guard with
    `assert result.named_failures`. Until issue #1455 the attribute did not
    exist and that line raised `AttributeError`, so the proof never ran."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    built = BuildOutcome(0, "", "Succeeded", ("CataclysmProjectile.cpp",),
                         False, 12)
    tests = TestOutcome(22, ("a",) * 20,
                        ("ItHitsWhatItAimsAt", "ItStopsAtTheWall"))
    result = CppGuardResult(build=built, tests=tests)
    assert result.named_failures == ("ItHitsWhatItAimsAt", "ItStopsAtTheWall")


def test_named_failures_holds_short_names_unlike_the_python_helper() -> None:
    """The asymmetry a reader who learns one helper and applies it to the other
    walks into. `parse_test_log` records what the engine prints, which drops the
    `Cataclysm.<Group>.` prefix, where `prove_guard` returns a full pytest node
    id. A membership test written with the prefix matches nothing."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    built = BuildOutcome(0, "", "Succeeded", ("CataclysmProjectile.cpp",),
                         False, 12)
    tests = TestOutcome(1, (), ("ItHitsWhatItAimsAt",))
    result = CppGuardResult(build=built, tests=tests)
    assert result.named_failures == ("ItHitsWhatItAimsAt",)
    assert not any(name.startswith("Cataclysm.")
                   for name in result.named_failures), (
        "the engine's log holds short names; a test asserting on the full "
        "Cataclysm.Group.Name form would silently match nothing")


def test_named_failures_is_empty_when_the_build_did_not_compile() -> None:
    """THE REASON TO PREFER IT OVER `failed`. A break that stops the file
    compiling exits non-zero and `failed` is True, which reads as a guard that
    fired when no test ran at all. `named_failures` is empty, so a proof written
    against it cannot record a worthless guard as proven."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    failed_build = BuildOutcome(1, "error C2065", None, (), False, 0)
    result = CppGuardResult(build=failed_build,
                            tests=TestOutcome(None, (), ()))
    assert result.failed, "the existing weaker signal still reports a problem"
    assert result.named_failures == (), (
        "a build that never compiled ran no test, so no test noticed anything")


def test_named_failures_is_empty_for_a_crashed_run() -> None:
    """The other way a run measures nothing. Issue #1313 is that a crash used to
    read as a guard that did not fire; this says the new attribute does not
    reintroduce the opposite reading either."""
    from unreal_build import BuildOutcome, CppGuardResult, TestOutcome
    built = BuildOutcome(0, "", "Succeeded", ("CataclysmProjectile.cpp",),
                         False, 12)
    result = CppGuardResult(build=built, tests=TestOutcome(None, (), ()))
    assert result.crashed
    assert result.named_failures == ()
    assert "NO MEASUREMENT" in result.summary, (
        "an empty named_failures must not be read as a verdict; the summary is "
        "what says the run did not finish")



# --------------------------------------------------------------------------
# The command line says what the build did. Issue #1599.
# --------------------------------------------------------------------------
#
# WHY THIS IS TESTED AGAINST CAPTURED OUTPUT AND NOT A REAL BUILD. The same
# reason as everything above: the reading is what can go wrong, and a real
# build needs the engine, the machine and several minutes. The three captures
# at the top of this file are real runs, so a line derived from them is derived
# from what UnrealBuildTool actually prints.


def test_what_it_did_names_the_files_for_a_build_that_compiled() -> None:
    """`Build: Succeeded` on its own is printed identically whether a build
    compiled everything or nothing, and issue #139 is the incident where those
    two were confused. This is the line that tells them apart."""
    said = outcome(BUILD_THAT_COMPILED).what_it_did
    assert "4 actions" in said
    assert "1 file compiled" in said, (
        f"the line should say how many files were compiled. It reads: {said!r}")
    assert "Module.Cataclysm.cpp" in said, (
        f"four files or fewer are named outright. It reads: {said!r}")


def test_what_it_did_says_a_build_that_did_nothing_did_nothing() -> None:
    """The issue #139 shape: succeeded, compiled nothing, target already
    current. It has to be unmistakable rather than merely different."""
    said = outcome(BUILD_THAT_DID_NOTHING).what_it_did
    assert "up to date" in said
    assert "nothing compiled" in said
    assert "0 actions" in said, (
        f"the action count is what distinguishes this case. It reads: {said!r}")


def test_the_two_readings_do_not_produce_the_same_line() -> None:
    """The whole point, stated as one assertion. A change that made both cases
    print the same thing would pass both tests above and reintroduce the
    fault."""
    assert (outcome(BUILD_THAT_COMPILED).what_it_did
            != outcome(BUILD_THAT_DID_NOTHING).what_it_did)


def test_what_it_did_does_not_invent_an_action_count() -> None:
    """`actions` is None when the output never stated one. Printing that as 0
    would claim the build ran no actions, which is a different statement from
    not knowing."""
    said = BuildOutcome(0, "", "Succeeded", (), False, None).what_it_did
    assert "actions unknown" in said, (
        f"an unread action count must say so rather than print a number. It "
        f"reads: {said!r}")
    assert "0 action" not in said


def test_what_it_did_counts_rather_than_names_a_long_list() -> None:
    """A full rebuild compiles hundreds of files. Naming them all would bury
    the counts in front of them."""
    from unreal_build import NAMED_COMPILE_LIMIT
    many = tuple(f"File{index}.cpp" for index in range(NAMED_COMPILE_LIMIT + 1))
    said = BuildOutcome(0, "", "Succeeded", many, False, len(many)).what_it_did
    assert f"{len(many)} files compiled" in said
    assert "File0.cpp" not in said, (
        f"a list longer than {NAMED_COMPILE_LIMIT} should be counted, not "
        f"named. It reads: {said!r}")

    few = many[:NAMED_COMPILE_LIMIT]
    named = BuildOutcome(0, "", "Succeeded", few, False, len(few)).what_it_did
    assert "File0.cpp" in named, (
        f"a list of exactly {NAMED_COMPILE_LIMIT} is still named. It reads: "
        f"{named!r}")


def test_the_build_command_prints_what_it_did(monkeypatch, capsys) -> None:
    """The command line, not only the property.

    THIS IS THE HALF ISSUE #1599 IS ABOUT. `build()` has always returned the
    file list and the action count; `main` threw them away and printed the
    result word alone. Testing the property without testing the command would
    leave exactly the gap the issue reports.

    `build` IS REPLACED RATHER THAN RUN. A real build needs the engine and the
    machine, and what is being checked is what `main` does with the answer.
    """
    import unreal_build as module

    captured = outcome(BUILD_THAT_COMPILED)
    monkeypatch.setattr(module, "build", lambda *args, **kwargs: captured)

    assert module.main(["build"]) == 0
    printed = capsys.readouterr().out

    assert "Build: Succeeded" in printed
    assert "4 actions" in printed, (
        f"the command must print what the build did, not only that it "
        f"succeeded. It printed: {printed!r}")
    assert "Module.Cataclysm.cpp" in printed


def test_the_build_command_says_when_it_compiled_nothing(
        monkeypatch, capsys) -> None:
    """The other reading, through the command. A session that had just rebased
    and got `Build: Succeeded` could not tell which of these two it had."""
    import unreal_build as module

    captured = outcome(BUILD_THAT_DID_NOTHING)
    monkeypatch.setattr(module, "build", lambda *args, **kwargs: captured)

    assert module.main(["build"]) == 0
    printed = capsys.readouterr().out

    assert "up to date" in printed
    assert "nothing compiled" in printed, (
        f"the command must say the build compiled nothing. It printed: "
        f"{printed!r}")


def test_the_tests_command_reports_a_refused_registration_without_claiming_a_crash(
        monkeypatch, capsys) -> None:
    """The command line, and the message that must NOT appear. Issue #1736.

    NOTHING EXERCISED THIS BRANCH BEFORE. `main` printed "No test results were
    read" whenever the exit code was non-zero and no test had failed, and until
    now those two were the same condition. Making a refused registration
    non-zero separates them: this run performs a test, passes it, and exits
    non-zero -- and the old condition would have told the reader to go looking
    for an editor that never started.

    So this asserts on the sentence being ABSENT, which is the half a reader of
    the diff would not think to check.
    """
    import unreal_build as module

    monkeypatch.setattr(module, "build",
                        lambda *args, **kwargs: outcome(BUILD_THAT_DID_NOTHING))
    monkeypatch.setattr(
        module, "run_automation_tests",
        lambda *args, **kwargs: module.parse_test_log(
            TEST_LOG_WITH_A_REFUSED_REGISTRATION))

    assert module.main(["tests"]) == 1, (
        "a run that lost a test to a refused registration must not exit 0")
    printed = capsys.readouterr().out

    assert "FCataclysmWeaponSubTypeTest" in printed, (
        f"the command must name the class the engine refused. It printed: "
        f"{printed!r}")
    assert "No test results were read" not in printed, (
        f"results WERE read -- one test ran and passed. Saying otherwise sends "
        f"the reader after a crashed editor. It printed: {printed!r}")


def test_a_failed_build_still_prints_the_compilers_own_words(
        monkeypatch, capsys) -> None:
    """Neighbouring behaviour, so the new line cannot have displaced it. A
    compilation error is the thing the caller needs and there is no summary of
    it better than the one the compiler wrote."""
    import unreal_build as module

    captured = outcome(BUILD_THAT_FAILED, returncode=6)
    monkeypatch.setattr(module, "build", lambda *args, **kwargs: captured)

    assert module.main(["build"]) == 1
    printed = capsys.readouterr().out

    assert "Build: Failed" in printed
    assert "error C2065" in printed, (
        f"the compiler's own error must still be printed. It printed: "
        f"{printed!r}")
