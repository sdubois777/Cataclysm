"""Build the Unreal project, run its tests, and prove a C++ guard fails.

WHY THIS EXISTS. `tools/prove_guard.py` does break-and-restore for Python and
says plainly that it does not run the Unreal build. This is that missing half.
Three traps live here and every one of them produces *misleading evidence*
rather than an obvious error.

**A build that reports success and compiles nothing (issue #139).** Restoring a
C++ file with a tool that preserves the modification time — `shutil.copy2`,
`cp -p`, `robocopy` without `/COPY:DAT` adjusted — gives the restored file an
mtime OLDER than the broken version it just replaced. UnrealBuildTool then
decides the target is up to date, prints

    Result: Succeeded

and compiles nothing, so the binary still contains the broken code. Observed
exactly that: after restoring `CataclysmAbilitySystemComponent.cpp` the suite
reported 58 passing and 1 failing with "Expected The ability in the pressed slot
activated to be true", while the restored file plainly contained the
`TryActivateAbility` call. Reading the source and reading the build output both
said everything was fine.

`Result: Succeeded` is therefore not evidence that anything was built.
`build()` returns which files were compiled, `BuildOutcome.up_to_date` says
whether the build did nothing at all, and `prove_cpp_guard()` refuses to report a
result from a build that compiled nothing.

**The unity build hides the filename.** UnrealBuildTool merges a module's .cpp
files into `Module.<Module>.cpp`, so a compile line usually names the blob rather
than the file. It also uses `git status` to pick an "adaptive non-unity working
set", which means a file compiles under its own name exactly while it is modified
and goes back into the blob once it is restored. So a break build names the file
and the restore build names the blob, and a check that demanded the filename both
times would fail on a correct run. `compiled_the_file()` accepts either.

**The test command writes nothing useful to standard output.** Redirecting
`UnrealEditor-Cmd.exe` captures only the software development kit validation
banner. The results go to `game/Saved/Logs/Cataclysm.log`. `run_automation_tests()`
reads that file rather than the pipe.

## Using it from the command line

    python tools/unreal_build.py build            # compile the editor target
    python tools/unreal_build.py tests            # compile, then run the tests
    python tools/unreal_build.py tests --prefix Cataclysm.Brute
    python tools/unreal_build.py tests --no-build

It exits non-zero when the build fails, when a test fails, and when no test
results could be read at all. `--build` and `--tests` are accepted as spellings
of the first two. Until issue #436 there was no entry point here and every one
of those commands exited 0 having done nothing, which is the fourth trap and the
worst of them, because it looks exactly like success.

## Using it as a library

    import sys
    sys.path.insert(0, "tools")
    from unreal_build import prove_cpp_guard

    result = prove_cpp_guard(
        {"game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp":
            lambda text: text.replace("TryActivateAbility(Spec.Handle)",
                                      "false")},
        test_prefix="Cataclysm.Input",
    )
    print(result.summary)
    assert result.failed          # the guard noticed

Every build takes tens of seconds and the whole sequence runs four of them, so
this is minutes rather than the seconds `prove_guard.py` takes. The editor must
be closed: Live Coding holds the binaries and `Build.bat` refuses to start.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import subprocess
import sys
import time
from collections.abc import Callable, Mapping, Sequence

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME_DIR = REPO_ROOT / "game"
PROJECT_FILE = GAME_DIR / "Cataclysm.uproject"
TEST_LOG = GAME_DIR / "Saved" / "Logs" / "Cataclysm.log"

ENGINE_ROOT = pathlib.Path(os.environ.get("UE_ROOT", r"C:\Program Files\Epic Games\UE_5.8"))
BUILD_BATCH_FILE = ENGINE_ROOT / "Engine" / "Build" / "BatchFiles" / "Build.bat"
EDITOR_CMD = ENGINE_ROOT / "Engine" / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe"

DEFAULT_TARGET = "CataclysmEditor"
DEFAULT_PLATFORM = "Win64"
DEFAULT_CONFIGURATION = "Development"

#: `[3/12] Compile [x64] CataclysmProjectile.cpp`. The action count prefix is
#: optional because a single-action build omits it in some engine versions.
COMPILE_LINE = re.compile(r"^(?:\[\d+/\d+\]\s*)?Compile\s+\[[^\]]+\]\s+(\S+\.cpp)\s*$",
                          re.MULTILINE)

#: `Result: Succeeded`, or `Result: Failed (OtherCompilationError)`. Reading only
#: the first word is deliberate: the parenthesised part varies and grepping for
#: `Result:` alone is how a real compiler error gets missed.
RESULT_LINE = re.compile(r"^Result:\s*(\w+)", re.MULTILINE)

#: Printed when UnrealBuildTool decided there was nothing to do.
UP_TO_DATE = "Target is up to date"

#: `Using Unreal Build Accelerator local executor to run 4 action(s)`.
ACTION_COUNT = re.compile(r"to run (\d+) action\(s\)")

#: How the automation runner reports one test, in `game/Saved/Logs/Cataclysm.log`:
#: `... Test Completed. Result={Success} Name={Cataclysm.Skills.AThing} ...`
TEST_RESULT = re.compile(r"Test Completed\. Result=\{(\w+)\}\s+Name=\{([^}]+)\}")

#: `... Automation Test Queue Empty 141 tests performed.`
TESTS_PERFORMED = re.compile(r"Automation Test Queue Empty\s+(\d+) tests performed")


class BuildDidNothing(RuntimeError):
    """A build reported success without compiling anything that was asked for.

    This is the failure issue #139 records. Raising rather than returning is
    deliberate: a caller that ignored a return value would go on to report test
    results produced by a binary that does not match the source on disk.
    """


@dataclasses.dataclass(frozen=True)
class BuildOutcome:
    """What one run of `Build.bat` did."""

    returncode: int
    stdout: str
    result: str | None
    compiled: tuple[str, ...]
    up_to_date: bool
    actions: int | None

    @property
    def succeeded(self) -> bool:
        return self.returncode == 0 and self.result == "Succeeded"

    def compiled_the_file(self, source_path: str) -> bool:
        """Whether this build compiled `source_path`, directly or in its module blob.

        A module's .cpp files are normally merged into `Module.<Module>.cpp`, and
        UnrealBuildTool only compiles a file under its own name while `git status`
        reports it modified. So both answers are correct evidence that the file's
        code was rebuilt, and demanding the filename would fail on the restore
        build of a correct sequence.
        """
        name = pathlib.PurePath(source_path).name
        if name in self.compiled:
            return True
        module = module_of(source_path)
        return module is not None and f"Module.{module}.cpp" in self.compiled

    @property
    def summary(self) -> str:
        if self.up_to_date:
            return f"Result: {self.result}, target up to date, nothing compiled"
        compiled = ", ".join(self.compiled) if self.compiled else "nothing"
        return f"Result: {self.result}, compiled {compiled}"


@dataclasses.dataclass(frozen=True)
class TestOutcome:
    """What one automation test run reported, read from the log rather than stdout."""

    #: NOT A TEST CLASS, despite the name. pytest collects anything called
    #: `Test*`, and warns that it cannot because this has a constructor. The
    #: warning is harmless and the noise is not: a real collection error looks
    #: the same in a run's output.
    __test__ = False

    performed: int | None
    succeeded: tuple[str, ...]
    failed: tuple[str, ...]

    @property
    def any_failed(self) -> bool:
        return bool(self.failed)

    @property
    def summary(self) -> str:
        performed = "unknown" if self.performed is None else self.performed
        line = (f"{performed} tests performed, {len(self.succeeded)} succeeded, "
                f"{len(self.failed)} failed")
        if self.failed:
            line += ": " + ", ".join(self.failed)
        return line


@dataclasses.dataclass(frozen=True)
class CppGuardResult:
    """What happened while the C++ files were broken."""

    build: BuildOutcome
    tests: TestOutcome

    @property
    def failed(self) -> bool:
        """Whether the guard noticed, which is what proves it works.

        A build that failed to compile counts. Breaking a file so it no longer
        compiles is a blunt way to prove a guard, but it is not evidence the
        guard fired, so callers should check `build.succeeded` too. `summary`
        says which of the two happened.
        """
        return self.tests.any_failed or not self.build.succeeded

    @property
    def summary(self) -> str:
        if not self.build.succeeded:
            return f"the build itself failed: {self.build.summary}"
        return self.tests.summary


def module_of(source_path: str) -> str | None:
    """The Unreal module a source file belongs to, from its path.

    `game/Source/Cataclysm/AbilitySystem/Thing.cpp` is in module `Cataclysm`.
    Returns None for a path that is not under `game/Source/<Module>/`.
    """
    parts = pathlib.PurePath(source_path.replace("\\", "/")).parts
    try:
        source_index = parts.index("Source")
    except ValueError:
        return None
    if source_index + 1 >= len(parts):
        return None
    return parts[source_index + 1]


def parse_build_output(text: str) -> tuple[str | None, tuple[str, ...], bool, int | None]:
    """Pull the result word, the compiled files, up-to-date and action count out."""
    result_match = RESULT_LINE.search(text)
    actions_match = ACTION_COUNT.search(text)
    return (
        result_match.group(1) if result_match else None,
        tuple(COMPILE_LINE.findall(text)),
        UP_TO_DATE in text,
        int(actions_match.group(1)) if actions_match else None,
    )


def build(target: str = DEFAULT_TARGET,
          platform: str = DEFAULT_PLATFORM,
          configuration: str = DEFAULT_CONFIGURATION,
          timeout: float = 1800.0) -> BuildOutcome:
    """Run `Build.bat` for one target and report what it actually compiled.

    The editor must be closed. With it open the build refuses to start, because
    Live Coding holds the binaries.
    """
    if not BUILD_BATCH_FILE.is_file():
        raise FileNotFoundError(
            f"{BUILD_BATCH_FILE} does not exist. Set UE_ROOT to the engine "
            "installation directory if it is not at the default location.")

    completed = subprocess.run(
        [str(BUILD_BATCH_FILE), target, platform, configuration,
         f"-Project={PROJECT_FILE}", "-WaitMutex"],
        capture_output=True, text=True, cwd=GAME_DIR, timeout=timeout, check=False)

    combined = completed.stdout + completed.stderr
    result, compiled, up_to_date, actions = parse_build_output(combined)
    return BuildOutcome(completed.returncode, combined, result, compiled, up_to_date, actions)


def require_compiled(outcome: BuildOutcome, source_paths: Sequence[str]) -> None:
    """Raise unless the build succeeded and rebuilt every named file.

    This is the check issue #139 asks for. `Result: Succeeded` on its own is not
    evidence: a build with a stale timestamp prints exactly that and compiles
    nothing.
    """
    if not outcome.succeeded:
        raise BuildDidNothing(
            f"The build did not succeed. {outcome.summary}\n"
            "Read the whole tail of the build output, not only the Result line: "
            "a compiler error appears above it.")

    not_rebuilt = [path for path in source_paths if not outcome.compiled_the_file(path)]
    if not_rebuilt:
        raise BuildDidNothing(
            "The build reported success but did not rebuild:\n  "
            + "\n  ".join(not_rebuilt)
            + f"\n{outcome.summary}\n"
            "The binary therefore does not match the source on disk, and any "
            "test result from it is meaningless. The usual cause is a restored "
            "file whose modification time was preserved, so it looks older than "
            "the object built from the broken version. See issue #139.")


def run_automation_tests(prefix: str = "Cataclysm",
                         timeout: float = 1800.0) -> TestOutcome:
    """Run the Unreal automation tests and read the results out of the log.

    Standard output carries only the software development kit validation banner,
    so the pipe is ignored and `game/Saved/Logs/Cataclysm.log` is read instead.
    The log is deleted first, so a run that writes nothing cannot be mistaken for
    the previous run's results.
    """
    if not EDITOR_CMD.is_file():
        raise FileNotFoundError(
            f"{EDITOR_CMD} does not exist. Set UE_ROOT to the engine "
            "installation directory if it is not at the default location.")

    TEST_LOG.parent.mkdir(parents=True, exist_ok=True)
    TEST_LOG.unlink(missing_ok=True)

    subprocess.run(
        [str(EDITOR_CMD), str(PROJECT_FILE),
         f"-ExecCmds=Automation RunTests {prefix}",
         "-unattended", "-nopause", "-nosplash", "-nullrhi",
         "-testexit=Automation Test Queue Empty", "-log"],
        capture_output=True, text=True, cwd=GAME_DIR, timeout=timeout, check=False)

    if not TEST_LOG.is_file():
        raise RuntimeError(
            f"{TEST_LOG} was not written. The test run produced no log at all, "
            "which usually means the editor failed to start.")
    return parse_test_log(TEST_LOG.read_text(encoding="utf-8", errors="replace"))


def parse_test_log(text: str) -> TestOutcome:
    """Pull the per-test results and the total out of an automation run's log."""
    succeeded: list[str] = []
    failed: list[str] = []
    for result, name in TEST_RESULT.findall(text):
        (succeeded if result == "Success" else failed).append(name)

    performed_match = TESTS_PERFORMED.search(text)
    return TestOutcome(
        int(performed_match.group(1)) if performed_match else None,
        tuple(succeeded),
        tuple(failed),
    )


def restore_and_touch(path: pathlib.Path, content: bytes, not_before: float) -> None:
    """Write the original bytes back and make sure the file looks newer than the build.

    Writing normally gives the file the current time, which is already after the
    object file built from the broken version. `not_before` is a belt: if the
    filesystem's timestamp granularity leaves the restored file looking no newer
    than the moment the break was made, the modification time is pushed forward
    explicitly. That is the "touch the file after restoring it" in issue #139,
    done every time rather than only when someone remembers.
    """
    path.write_bytes(content)
    if path.stat().st_mtime <= not_before:
        stamp = not_before + 1.0
        os.utime(path, (stamp, stamp))


def prove_cpp_guard(edits: Mapping[str, Callable[[str], str]],
                    test_prefix: str = "Cataclysm",
                    target: str = DEFAULT_TARGET) -> CppGuardResult:
    """Break C++ files, rebuild, run the tests, restore, and rebuild again.

    @param edits        path relative to the repository root, and a function
                        taking the file's text and returning the broken text. An
                        edit that changes nothing raises, because a break that
                        did not break anything makes a working guard look
                        worthless.
    @param test_prefix  which automation tests to run, for example
                        `Cataclysm.Skills`. Narrower is much faster.

    Four builds' worth of time. Every file is restored in a `finally`, and the
    restore is followed by a rebuild, so an exception or an interrupt cannot
    leave a binary that disagrees with the source.
    """
    if not edits:
        raise ValueError("prove_cpp_guard needs at least one edit to make.")

    originals: dict[pathlib.Path, bytes] = {}
    broken_at = time.time()
    try:
        for relative, edit in edits.items():
            path = REPO_ROOT / relative
            if not path.is_file():
                raise FileNotFoundError(f"{relative} does not exist.")

            originals[path] = path.read_bytes()
            before = path.read_text(encoding="utf-8")
            after = edit(before)
            if after == before:
                raise ValueError(
                    f"The edit to {relative} changed nothing. A break that does "
                    "not break anything makes a working guard look worthless.")
            # No `newline=` argument. `read_text` normalised the file's CRLF line
            # endings to LF, and the default here converts them back on Windows,
            # so the break changes only what the edit changed. Passing
            # `newline=""` would rewrite every line ending in the file as well.
            path.write_text(after, encoding="utf-8")

        broken_at = time.time()
        broken_build = build(target)
        if broken_build.succeeded:
            # Only demand a rebuild when the build worked. A break that fails to
            # compile is a legitimate, if blunt, way to reach the same evidence,
            # and it never reaches a Compile line for the file.
            require_compiled(broken_build, list(edits))
            tests = run_automation_tests(test_prefix)
        else:
            tests = TestOutcome(None, (), ())

        return CppGuardResult(broken_build, tests)
    finally:
        for path, content in originals.items():
            restore_and_touch(path, content, broken_at)
        if originals:
            restored_build = build(target)
            require_compiled(restored_build, list(edits))


# ---------------------------------------------------------------------------
# Running this from the command line
#
# WHY THIS SECTION EXISTS. Issue #436. Until it was added this file was a
# library with no `__main__` block, so
#
#     python tools/unreal_build.py --tests
#
# imported the module, defined these functions, printed nothing and exited 0.
# An exit code of 0 with no output cannot be told apart from a successful run
# whose output was filtered away, which is what a caller doing `| tail -8` sees.
# It was run that way three times -- twice through one shell, once through
# another with output redirected to a file -- and reported `exit=0` and an empty
# file every time. Nothing had run at all.
#
# THAT IS THE SAME CLASS OF FAULT THIS WHOLE FILE EXISTS TO PREVENT: a report of
# success from something that did no work. Issue #139 is the build version of
# it, `BuildDidNothing` is the guard against that, and this was the same hole in
# the front door.
# ---------------------------------------------------------------------------

#: What a caller may write instead of the subcommand.
#:
#: BOTH SPELLINGS ARE ACCEPTED ON PURPOSE. `--tests` is how the command was
#: written in issue #436 and in the notes that led to it, so somebody will type
#: it. A usage error would at least be loud, but accepting it costs one line.
COMMAND_ALIASES = {"--build": "build", "--tests": "tests"}


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    """Read the command line. Raises SystemExit(2) on anything unrecognised.

    SEPARATE FROM `main` SO IT CAN BE TESTED, because the alternative is a test
    that starts a real Unreal build in order to find out whether a flag was
    spelled correctly.
    """
    normalised = [COMMAND_ALIASES.get(word, word) for word in argv]

    parser = argparse.ArgumentParser(
        prog="python tools/unreal_build.py",
        description="Build the Unreal project and run its automation tests.")
    parser.add_argument(
        "command", choices=("build", "tests"),
        help="build: compile the editor target. "
             "tests: compile it and then run the automation tests.")
    parser.add_argument(
        "--prefix", default="Cataclysm",
        help="only run tests whose name starts with this (default: Cataclysm)")
    parser.add_argument(
        "--target", default=DEFAULT_TARGET,
        help=f"the build target (default: {DEFAULT_TARGET})")
    parser.add_argument(
        "--no-build", action="store_true",
        help="with `tests`, run against the binaries already built")

    return parser.parse_args(normalised)


def exit_code_for(tests: TestOutcome | None) -> int:
    """0 only when tests actually ran and every one of them passed.

    NOTHING RAN IS A FAILURE, NOT A PASS, and that is the whole point of this
    function. `TestOutcome(None, (), ())` is what comes back when the log could
    not be read: the editor never started, the run crashed, the log was locked.
    Zero tests performed is the same thing said differently. Reporting either as
    success is exactly the fault issue #436 was opened about, one level further
    in.
    """
    if tests is None or tests.performed is None or tests.performed == 0:
        return 1
    return 1 if tests.any_failed else 0


def main(argv: Sequence[str] | None = None) -> int:
    """Build, or build and test, reporting what happened and why."""
    arguments = parse_arguments(
        list(argv if argv is not None else sys.argv[1:]))

    if not (arguments.command == "tests" and arguments.no_build):
        outcome = build(arguments.target)
        print(f"Build: {outcome.result}"
              f"{' (nothing to do)' if outcome.up_to_date else ''}")

        if not outcome.succeeded:
            # THE COMPILER'S OWN WORDS, NOT A SUMMARY OF THEM. A compilation
            # error is the thing the caller needs and there is no reading of it
            # better than the one the compiler already wrote.
            print(outcome.stdout)
            return 1

    if arguments.command == "build":
        return 0

    tests = run_automation_tests(arguments.prefix)
    print(f"Tests: {tests.summary}")

    code = exit_code_for(tests)
    if code != 0 and not tests.any_failed:
        print(f"No test results were read from {TEST_LOG}. Either the run did "
              f"not happen or its log could not be read. That is reported as a "
              f"failure rather than a pass on purpose; see issue #436.")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
