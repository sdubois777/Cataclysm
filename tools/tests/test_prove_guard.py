"""`tools/prove_guard.py` must not be able to load stale compiled Python.

WHAT IS BEING TESTED. The failure mode issue #159 records. Python decides whether
a compiled copy in `__pycache__` is current from the source's modification time
and size, and a break-and-restore usually changes neither: writing `0.0` where
`5.0` was keeps the length identical, and the two writes land inside the
filesystem's modification time granularity. So a run imports compiled code that
does not match the source on disk.

IT GOES WRONG IN BOTH DIRECTIONS, and the quieter one is worse. Issue #159
records a misattributed failure: a broken module's cache survived its restore and
the NEXT case's failure was blamed on the wrong break. The direction reproduced
below is the other one: the break itself is invisible, the run reads the OLD
compiled code, the tests pass, and the report says the guard did not fire — when
the guard was never given the bad input at all. That reads as "this check is
worthless" and it is wrong.

HOW IT IS MADE DETERMINISTIC. `os.utime` puts the modification time back
explicitly rather than relying on the two writes happening close together, so
the collision happens every run instead of sometimes.

WHY THE FIRST TEST DELIBERATELY REPRODUCES THE BUG. A test that only showed the
fixed path working would pass just as happily on a machine or a Python version
where the trap does not exist. Showing the naive path being fooled first is what
proves the rest is testing something. It skips rather than fails if this Python
notices the edit anyway, because that is a working environment, not a fault.
"""

from __future__ import annotations

import dataclasses
import os
import pathlib
import subprocess
import sys
import textwrap

import pytest

# `tools` is on the path via pythonpath in pyproject.toml, so this is a
# plain module import rather than a package one.
from prove_guard import (
    GuardResult,
    break_and_run,
    clear_bytecode_caches,
    run_without_bytecode,
)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


@pytest.fixture
def module_and_caller(tmp_path: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    """A module holding one number, and a script that prints it.

    The script imports the module, so running it writes a `__pycache__` beside
    the module. That cache is what the tests below are about.
    """
    module = tmp_path / "answer.py"
    module.write_text("VALUE = 5.0\n", encoding="utf-8")

    caller = tmp_path / "ask.py"
    caller.write_text(
        textwrap.dedent(
            """
            import answer
            print(answer.VALUE)
            """
        ).strip()
        + "\n",
        encoding="utf-8",
    )
    return module, caller


def ask(caller: pathlib.Path, *, write_bytecode: bool) -> str:
    """Run the script and return what it printed."""
    environment = dict(os.environ)
    argv = [sys.executable]
    if write_bytecode:
        environment.pop("PYTHONDONTWRITEBYTECODE", None)
    else:
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        argv.append("-B")
    argv.append(str(caller))

    completed = subprocess.run(argv, capture_output=True, text=True,
                               cwd=caller.parent, env=environment, check=True)
    return completed.stdout.strip()


def break_and_hide_it(module: pathlib.Path) -> None:
    """Break the module in a way Python's cache check cannot see.

    THE CHECK IS ONLY MODIFICATION TIME AND SIZE. `VALUE = 0.0` is the same
    length as `VALUE = 5.0`, and setting the modification time back to what it
    was is what a fast edit-and-run produces by accident when both writes land
    inside the filesystem's granularity. With both matching, Python treats the
    compiled copy of the OLD source as current and never looks at the new one.
    """
    stat = module.stat()
    module.write_text("VALUE = 0.0\n", encoding="utf-8")
    os.utime(module, (stat.st_atime, stat.st_mtime))


def test_the_naive_path_really_is_fooled_by_a_stale_cache(
    module_and_caller: tuple[pathlib.Path, pathlib.Path],
) -> None:
    """Issue #159's bug, reproduced.

    THIS IS THE DIRECTION THAT MAKES A GUARD LOOK WORTHLESS. The broken source
    is on disk and the run does not see it, so the tests pass, and the report
    says the guard did not fire when in fact the guard was never given the bad
    input. It is the same mechanism as a misattributed failure and it is quieter.
    """
    module, caller = module_and_caller

    # The first run compiles and caches the good value.
    assert ask(caller, write_bytecode=True) == "5.0"

    break_and_hide_it(module)
    assert module.read_text(encoding="utf-8") == "VALUE = 0.0\n", (
        "The module on disk is broken at this point."
    )

    stale = ask(caller, write_bytecode=True)
    if stale == "0.0":
        pytest.skip(
            "This Python or filesystem noticed the edit anyway, so there is "
            "nothing here for tools/prove_guard.py to protect against. The "
            "tests below still check it does the right thing."
        )
    assert stale == "5.0", (
        "Expected the run to print the OLD value from the cache while the "
        f"source on disk says 0.0. It printed {stale!r}."
    )


def test_clearing_the_caches_makes_the_broken_source_win(
    module_and_caller: tuple[pathlib.Path, pathlib.Path],
) -> None:
    """The fix: clear the caches, and the source on disk is read again."""
    module, caller = module_and_caller

    ask(caller, write_bytecode=True)
    break_and_hide_it(module)

    removed = clear_bytecode_caches(module.parent)
    assert removed >= 1, "Expected to find a __pycache__ to remove."

    assert ask(caller, write_bytecode=True) == "0.0", (
        "After clearing the caches the run must read the broken source, which "
        "is what gives a guard the bad input it is meant to notice."
    )


def test_running_with_bytecode_off_writes_no_cache(
    module_and_caller: tuple[pathlib.Path, pathlib.Path],
) -> None:
    """The other half. A run that writes no cache cannot poison the next case."""
    module, caller = module_and_caller

    clear_bytecode_caches(module.parent)
    assert ask(caller, write_bytecode=False) == "5.0"

    assert not (module.parent / "__pycache__").exists(), (
        "Running with bytecode writing off should leave no __pycache__ behind, "
        "so a later case cannot import this run's compiled copy."
    )


def test_break_and_run_restores_the_file_byte_for_byte() -> None:
    """Whatever the command does, the repository is left as it was found."""
    target = "tools/prove_guard.py"
    before = (REPO_ROOT / target).read_bytes()

    result = break_and_run(
        {target: lambda text: text.replace("REPO_ROOT", "REPO_ROOT_BROKEN")},
        [sys.executable, "-c", "print('ran while broken')"],
    )

    assert "ran while broken" in result.stdout
    assert (REPO_ROOT / target).read_bytes() == before, (
        "break_and_run must put the original bytes back."
    )


def test_break_and_run_restores_even_when_the_command_fails() -> None:
    target = "tools/prove_guard.py"
    before = (REPO_ROOT / target).read_bytes()

    result = break_and_run(
        {target: lambda text: text.replace("REPO_ROOT", "REPO_ROOT_BROKEN")},
        [sys.executable, "-c", "raise SystemExit(3)"],
    )

    assert result.failed
    assert result.returncode == 3
    assert (REPO_ROOT / target).read_bytes() == before


def test_break_and_run_restores_even_when_a_later_edit_raises() -> None:
    """One good edit and one impossible one. The good file still comes back."""
    target = "tools/prove_guard.py"
    before = (REPO_ROOT / target).read_bytes()

    with pytest.raises(FileNotFoundError):
        break_and_run(
            {
                target: lambda text: text.replace("REPO_ROOT", "BROKEN"),
                "tools/there_is_no_such_file.py": lambda text: text,
            },
            [sys.executable, "-c", "pass"],
        )

    assert (REPO_ROOT / target).read_bytes() == before


def test_an_edit_that_changes_nothing_is_refused() -> None:
    """A break that does not break makes a working guard look worthless."""
    with pytest.raises(ValueError, match="changed nothing"):
        break_and_run(
            {"tools/prove_guard.py": lambda text: text},
            [sys.executable, "-c", "pass"],
        )


def test_the_result_summary_is_the_last_line_printed() -> None:
    """What a report quotes, so it has to be the line that says the outcome."""
    result = GuardResult(1, "collected 3 items\n\n1 failed, 2 passed in 0.10s\n", "")
    assert result.summary == "1 failed, 2 passed in 0.10s"
    assert result.failed

    assert GuardResult(0, "", "").summary == "(no output)"
    assert not GuardResult(0, "", "").failed


def test_run_without_bytecode_reports_a_real_failure() -> None:
    """The whole point: a broken guard has to come back as failed."""
    result = run_without_bytecode(
        [sys.executable, "-c", "import sys; print('nope'); sys.exit(1)"]
    )
    assert result.failed
    assert result.summary == "nope"


# ---------------------------------------------------------------------------
# A run whose files moved underneath it. Issue #598.
# ---------------------------------------------------------------------------


def test_a_run_disturbed_by_something_else_is_not_reported_as_a_guard_firing(tmp_path):
    """The failure issue #598 recorded, made detectable.

    `break_and_run` breaks the real repository for the duration of the command,
    so anything else working in the same checkout can overwrite what it wrote.
    On 2026-08-14 that produced five failures in tests unrelated to the break,
    every one of which passed on a rerun.

    Here the command itself plays the part of the other process: it writes to
    the broken file while it runs, then exits non-zero. Without the check that
    reads as a guard firing.
    """
    target = REPO_ROOT / "tools" / "tests" / "_disturbed_probe.txt"
    target.write_text("original\n", encoding="utf-8")

    # NO ESCAPE SEQUENCES IN THE GENERATED SOURCE. The first version of this
    # test put a newline escape into the meddling script and it reached the file
    # as a real line break, so the script did not parse, nothing was written,
    # and the test failed for a reason unrelated to what it checks.
    meddler = tmp_path / "meddle.py"
    meddler.write_text(
        "import pathlib, sys\n"
        f"pathlib.Path({str(target)!r}).write_text('somebody else', encoding='utf-8')\n"
        "sys.exit(1)\n",
        encoding="utf-8")

    try:
        result = break_and_run(
            {"tools/tests/_disturbed_probe.txt": lambda t: t.replace("original", "broken")},
            [sys.executable, str(meddler)])

        assert result.returncode != 0, "the probe command was supposed to fail"
        assert result.disturbed == ("tools/tests/_disturbed_probe.txt",), (
            "break_and_run did not notice that the file it broke was rewritten "
            "underneath the run. Issue #598.")
        assert not result.failed, (
            "a run whose files moved underneath it is being reported as a guard "
            "firing. It did not test what it was asked to test. Issue #598.")
        assert "EVIDENCE COMPROMISED" in result.summary
        assert "_disturbed_probe.txt" in result.summary
    finally:
        target.unlink(missing_ok=True)


def test_an_undisturbed_failing_run_is_still_a_guard_firing(tmp_path):
    """The check must not swallow ordinary proofs, which is the whole point.

    Same shape as the test above with the meddling removed.
    """
    target = REPO_ROOT / "tools" / "tests" / "_undisturbed_probe.txt"
    target.write_text("original\n", encoding="utf-8")

    failing = tmp_path / "fail.py"
    failing.write_text("import sys\nsys.exit(1)\n", encoding="utf-8")

    try:
        result = break_and_run(
            {"tools/tests/_undisturbed_probe.txt": lambda t: t.replace("original", "broken")},
            [sys.executable, str(failing)])

        assert result.disturbed == ()
        assert result.failed, (
            "an ordinary failing run is no longer reported as a guard firing, so "
            "the issue #598 check has broken every proof in the project.")
    finally:
        target.unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# A crashed run is not a guard that fired. Issue #1314.
# ---------------------------------------------------------------------------

#: What pytest actually prints when a break stops the module importing, under
#: the `-q` that pyproject.toml sets. CAPTURED, NOT WRITTEN: the first version of
#: the fix looked for "collected" and the session-starts banner, and this output
#: contains NEITHER, so the crash fell through to the exit code and read exactly
#: as it had before. Keeping the real text is what stops that recurring.
COLLECTION_ERROR = (
    "ImportError while importing test module.\n"
    "sim\\cataclysm_sim\\modifiers.py:23: in <module>\n"
    "    import nonexistent_module_xyz\n"
    "E   ModuleNotFoundError: No module named 'nonexistent_module_xyz'\n"
    "=========================== short test summary info "
    "===========================\n"
    "ERROR tools/tests/test_dungeon_modifier_port.py\n"
    "!!!!!!!!!!!!!!!!! Interrupted: 1 error during collection "
    "!!!!!!!!!!!!!!!!!\n"
)

#: A run where a test really did fail, for the same shape of comparison.
REAL_FAILURE = (
    "collected 3 items\n"
    "FAILED tools/tests/test_x.py::test_y - AssertionError: no\n"
    "1 failed, 2 passed in 0.10s\n"
)


def test_a_crashed_run_is_not_counted_as_a_guard_firing() -> None:
    """The defect issue #1314 records. A collection error exits 2 and runs no
    test at all; `failed` used to say True and a proof recorded the guard as
    proven."""
    result = GuardResult(2, COLLECTION_ERROR, "")
    assert result.crashed
    assert not result.failed, (
        "a run in which no test executed is being reported as a guard that "
        "fired. Issue #1314.")


def test_a_crashed_run_says_no_measurement_rather_than_nothing() -> None:
    """A bare False would read as the guard not noticing, which is the mistake
    in the other direction and is issue #1313."""
    summary = GuardResult(2, COLLECTION_ERROR, "").summary
    assert "NO MEASUREMENT" in summary
    assert "#1314" in summary


def test_a_real_failure_is_still_counted() -> None:
    """The half that matters more. A fix that called every non-zero exit a crash
    would report every working guard as worthless."""
    result = GuardResult(1, REAL_FAILURE, "")
    assert not result.crashed
    assert result.failed
    assert result.named_failures == ("tools/tests/test_x.py::test_y",)


def test_a_failure_without_a_short_summary_is_still_counted() -> None:
    """The count and the named lines are two independent signals, and either is
    enough. pytest's short summary can be suppressed; requiring it would call a
    real failure a crash."""
    result = GuardResult(1, "collected 3 items\n1 failed, 2 passed in 0.1s\n", "")
    assert not result.crashed
    assert result.failed
    assert result.named_failures == ()


def test_a_command_that_is_not_a_test_runner_still_falls_back_to_its_exit_code():
    """`break_and_run` takes ANY command. One that prints nothing has no
    failures to report, so the exit code is all there is to go on -- and an
    exit-code-only rule would have broken this."""
    result = GuardResult(3, "", "")
    assert not result.crashed
    assert result.failed


def test_named_failures_reads_the_tests_that_noticed() -> None:
    """`assert result.named_failures` is the stronger proof: it says which tests
    caught the break, where `failed` says only that the command exited
    non-zero."""
    result = GuardResult(
        1,
        "FAILED a.py::test_one - AssertionError: x\n"
        "FAILED b.py::test_two - ValueError\n"
        "2 failed in 0.2s\n", "")
    assert result.named_failures == ("a.py::test_one", "b.py::test_two")



# ---------------------------------------------------------------------------
# Both halves of a guard proof. Issue #1735.
# ---------------------------------------------------------------------------

#: A broken half that did what a guard proof needs: a named test failed.
BROKE_AND_A_TEST_NOTICED = GuardResult(
    1, "FAILED tools/tests/test_x.py::test_y - AssertionError\n"
       "1 failed, 2 passed in 0.10s\n", "")

#: A restored half where everything passed.
RESTORED_AND_CLEAN = GuardResult(0, "3 passed in 0.10s\n", "")


def paired(broken: GuardResult, restored: GuardResult) -> GuardResult:
    return dataclasses.replace(broken, restored=restored)


def test_a_failing_half_alone_is_not_a_proof() -> None:
    """The defect issue #1735 is about, stated as a test.

    A named test failed with the break in. That is the first of the two claims a
    guard proof makes and it is the only one `break_and_run` could ever produce
    before this. `proved` must not be True on it.
    """
    assert BROKE_AND_A_TEST_NOTICED.named_failures
    assert BROKE_AND_A_TEST_NOTICED.failed
    assert not BROKE_AND_A_TEST_NOTICED.proved, (
        "a result with no second half cannot claim the pair held")


def test_both_halves_holding_is_a_proof() -> None:
    result = paired(BROKE_AND_A_TEST_NOTICED, RESTORED_AND_CLEAN)
    assert result.proved
    assert result.summary.startswith("PROVED: ")
    assert "restored: 3 passed" in result.summary


def test_a_test_that_fails_in_both_halves_is_not_a_proof() -> None:
    """THE CASE THE WHOLE ISSUE EXISTS FOR, and it is invisible without pairing.

    A test that was already failing, or that fails for a reason unrelated to the
    break, produces a first half byte-identical to a real proof: a name in
    `named_failures`, a plausible summary, a non-zero exit. The only thing that
    tells it apart is running the same command with the files put back.

    Note the broken half here IS a genuine-looking first half -- `failed` and
    `named_failures` both say what a caller following `CLAUDE.md` would assert
    on -- and the verdict is still not a proof.
    """
    result = paired(BROKE_AND_A_TEST_NOTICED, BROKE_AND_A_TEST_NOTICED)

    assert result.failed, "the half a caller used to see still looks convincing"
    assert result.named_failures, "and so does the list they were told to assert"
    assert not result.proved
    assert "files PUT BACK" in result.summary, (
        f"the report must say the tests were failing before the break. It "
        f"reads: {result.summary!r}")


def test_a_run_that_failed_without_naming_a_test_is_not_a_proof() -> None:
    """Different from nothing failing, and the report must say which.

    A count with no names has measured something and the question is why the
    names are missing -- a second `-q` does this. Nothing failing has measured
    that the break does not bite, and the answer is a different break. Telling a
    reader the wrong one of those sends them the wrong way.
    """
    no_names = GuardResult(1, "collected 3 items\n\n1 failed, 2 passed in 0.1s\n", "")
    result = paired(no_names, RESTORED_AND_CLEAN)

    assert not result.proved
    assert "named no test" in result.summary
    assert "-q" in result.summary, "the likeliest cause is worth naming"


def test_nothing_failing_with_the_break_in_is_reported_as_that() -> None:
    clean = GuardResult(0, "3 passed in 0.1s\n", "")
    result = paired(clean, RESTORED_AND_CLEAN)

    assert not result.proved
    assert "nothing failed with the break in" in result.summary


def test_a_restored_half_that_crashed_proves_nothing() -> None:
    """An unusable second half is not a passing second half."""
    crashed = GuardResult(2, "collected 0 items\nERROR tools/tests/test_x.py\n", "")
    result = paired(BROKE_AND_A_TEST_NOTICED, crashed)

    assert not result.proved
    assert "no usable result" in result.summary


def test_a_summary_with_no_second_half_is_exactly_what_it_always_was() -> None:
    """The contract this property already had, kept.

    `summary` is documented as the last line of output and two tests assert it
    by exact equality. The verdict is appended only when there is a second half
    to report, so a caller who never asked for pairing sees the same line.
    """
    assert BROKE_AND_A_TEST_NOTICED.summary == "1 failed, 2 passed in 0.10s"


def test_opting_out_says_so_and_names_what_supplies_the_other_half() -> None:
    """A sentence, not a flag, and it has to reach the report."""
    result = dataclasses.replace(
        BROKE_AND_A_TEST_NOTICED,
        restored_half_supplied_by="the enclosing pytest run")

    assert not result.proved, (
        "opting out says a passing half exists elsewhere; it does not let this "
        "object claim to have seen one")
    assert "NOT PAIRED BY THIS CALL" in result.summary
    assert "the enclosing pytest run" in result.summary


def test_the_inner_result_never_carries_its_own_second_half() -> None:
    """The rule that keeps the type from nesting without end.

    Both halves are a `GuardResult` here, unlike the C++ version whose two
    halves are different types and which therefore stops after one level on its
    own. If this ever fails, something has started building an inner result the
    long way round and the field will nest.
    """
    result = paired(BROKE_AND_A_TEST_NOTICED, RESTORED_AND_CLEAN)
    assert result.restored is not None
    assert result.restored.restored is None


def test_break_and_run_really_runs_the_command_twice() -> None:
    """END TO END, because every test above builds its result by hand.

    THOSE TESTS CHECK THE VERDICT LOGIC AND NOTHING ELSE. Delete the second run
    from `break_and_run` and all of them still pass, because they never call it.
    This one does: it breaks a real file, runs a real pytest selection, and
    requires that the result carries a second half the function fetched itself.

    THE TARGET IS DELIBERATELY NOT `prove_guard.py`. Breaking the module the
    inner run has to import risks a collection error, which is a crash rather
    than a guard firing -- issue #1314 -- and would make this test prove the
    opposite of what it says. `unreal_build.py` has tests that do not recurse.
    """
    target = "tools/unreal_build.py"
    before = (REPO_ROOT / target).read_bytes()

    result = break_and_run(
        {target: lambda text: text.replace(
            "CATACLYSM_SKIPPED_HALF", "CATACLYSM_NO_SUCH_TOKEN")},
        [sys.executable, "-m", "pytest",
         "tools/tests/test_unreal_build.py", "-k", "skipped_half",
         "-p", "no:randomly"],
    )

    assert (REPO_ROOT / target).read_bytes() == before, (
        "the file must be back before the second run, and after it")

    assert result.named_failures, (
        f"the break should have fired a test. Summary: {result.summary!r}")
    assert result.restored is not None, (
        "break_and_run must fetch a second half itself. If this is None the "
        "run after the restore did not happen, which is issue #1735 back.")
    assert not result.restored.reported_a_failing_test, (
        f"the same selection must pass with the file restored. Restored half: "
        f"{result.restored.summary!r}")
    assert result.proved, f"both halves held, so: {result.summary!r}"
    assert result.summary.startswith("PROVED: ")


def test_a_command_that_is_not_a_test_run_gets_no_second_half() -> None:
    """`break_and_run` takes ANY command, and a second run of one that is not a
    test runner says nothing the exit code did not.

    `looks_like_a_test_run` already decided this question for `failed`; it
    decides this one too rather than a new concept being invented for it.
    """
    result = break_and_run(
        {"tools/prove_guard.py": lambda text: text.replace(
            "REPO_ROOT", "REPO_ROOT_BROKEN")},
        [sys.executable, "-c", "print('not a test run')"],
    )

    assert not result.looks_like_a_test_run
    assert result.restored is None, (
        "a command with no test output has no second half worth taking")
