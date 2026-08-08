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
import subprocess
import sys
import time

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from unreal_build import (  # noqa: E402
    BuildDidNothing,
    BuildOutcome,
    TestOutcome,
    exit_code_for,
    module_of,
    parse_arguments,
    parse_build_output,
    parse_test_log,
    require_compiled,
    restore_and_touch,
)

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
