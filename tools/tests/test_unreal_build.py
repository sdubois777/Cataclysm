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
    """Two builds and one test run, and the file back as it was."""
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
    assert seen_during_the_test_run == ["float Reach() { return 0.0f; }\n"], (
        "the tests did not run while the file was broken, so a failure among "
        "them proves nothing")
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
