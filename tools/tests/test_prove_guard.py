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
