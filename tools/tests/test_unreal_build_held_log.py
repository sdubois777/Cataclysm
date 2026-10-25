"""A log another process still holds open is waited for, not crashed on. Issue #2459.

WHY THIS FILE EXISTS. `run_automation_tests` deletes `game/Saved/Logs/Cataclysm.log`
before it starts the editor, so a run that writes nothing cannot be mistaken for
the previous run's results. Twice (2026-10-21, window enchant-tt3; 2026-10-24,
window t74x) that delete raised `PermissionError: [WinError 32]` because another
process still held the log open, and a guard proof window was lost each time.
`delete_when_released` retries the delete instead.

EVERY FILE HERE IS A TEMPORARY ONE. No test touches the real
`game/Saved/Logs/Cataclysm.log`: another session's test run may be writing it.
The test of `run_automation_tests` points `TEST_LOG` at a file under `tmp_path`
and replaces the editor command and `subprocess.run`, so no editor starts.

HOW A FILE IS HELD OPEN. By this process, with `open()`. On Windows CPython's
`open()` asks for read and write sharing and not for delete sharing, so while the
handle is open a delete of the file is refused with `PermissionError` (WinError
32), which is the error of both incidents. That is checked first in each test that
relies on it, as its control. On Linux and macOS an open handle does not stop a
delete, so those tests are marked Windows-only; continuous integration runs on
`ubuntu-latest` and skips them, and the tests that replace the file with
`_HeldPath` run the same retry logic there. A child process holding the file was
not used: it would need a wait for the child to have opened the file, and the
same-process handle gives the same refusal without one.

THE WAITS ARE NOT REAL. Each test passes its own `wait` (and, where the limit is
reached, its own `clock`), so nothing here sleeps.
"""

from __future__ import annotations

import inspect
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import unreal_build  # noqa: E402
from unreal_build import FileStillHeld, delete_when_released  # noqa: E402

ON_WINDOWS = sys.platform == "win32"

windows_only = pytest.mark.skipif(
    not ON_WINDOWS,
    reason="only Windows refuses to delete a file another handle holds open")


class _Waits:
    """A stand-in for `time.sleep` and `time.monotonic` together: each wait
    is recorded and moves the clock on by the seconds asked for, and an
    optional action runs at a given wait (closing the handle, say)."""

    def __init__(self, at_wait: int | None = None, action=None) -> None:
        self.waits: list[float] = []
        self.now = 1000.0
        self.at_wait = at_wait
        self.action = action

    def wait(self, seconds: float) -> None:
        self.waits.append(seconds)
        self.now += seconds
        if self.action is not None and len(self.waits) == self.at_wait:
            self.action()

    def clock(self) -> float:
        return self.now


class _HeldPath:
    """A path whose delete is refused `refusals` times and then succeeds, as
    Windows refuses while another process holds the file. For platforms where
    a real open handle does not stop a delete."""

    def __init__(self, refusals: int) -> None:
        self.refusals = refusals
        self.attempts = 0
        self.deleted = False

    def unlink(self, missing_ok: bool = False) -> None:
        self.attempts += 1
        if self.attempts <= self.refusals:
            raise PermissionError(
                13, "The process cannot access the file because it is being used "
                    "by another process", "HeldByAnother.log")
        self.deleted = True

    def __str__(self) -> str:
        return "HeldByAnother.log"


def test_a_file_nobody_holds_is_deleted_at_once_and_a_missing_one_is_no_error(
        tmp_path: pathlib.Path) -> None:
    """The control: with nothing holding the file there is no wait at all,
    and a file that is not there is not an error (the log is absent on a
    fresh checkout)."""
    log = tmp_path / "Cataclysm.log"
    log.write_text("the previous run's results", encoding="utf-8")
    waits = _Waits()

    waited = delete_when_released(log, wait=waits.wait, clock=waits.clock)

    assert not log.exists(), "a file nobody holds must be deleted"
    assert waits.waits == [] and waited == 0, "and without any wait"

    waited = delete_when_released(log, wait=waits.wait, clock=waits.clock)

    assert waits.waits == [] and waited == 0, (
        "a missing file must be no error and no wait")


@windows_only
def test_a_file_held_open_is_waited_for_and_deleted_once_it_is_released(
        tmp_path: pathlib.Path) -> None:
    """The fault of both incidents, on a temporary file: the handle is closed
    at the third wait, and the call must then delete the file and return,
    having waited three times the retry interval."""
    log = tmp_path / "Cataclysm.log"
    log.write_text("the previous run's results", encoding="utf-8")
    handle = open(log, "a", encoding="utf-8")  # closed by the wait, or by the finally
    try:
        with pytest.raises(PermissionError):
            log.unlink()  # the control: the handle really blocks a delete here
        waits = _Waits(at_wait=3, action=handle.close)

        waited = delete_when_released(log, wait=waits.wait, clock=waits.clock)
    finally:
        handle.close()

    assert not log.exists(), "the file must be deleted once the handle is closed"
    assert waits.waits == [unreal_build.HELD_FILE_RETRY_SECONDS] * 3, (
        f"three waits of the retry interval, then the delete; waited {waits.waits}")
    assert waited == 3 * unreal_build.HELD_FILE_RETRY_SECONDS


@windows_only
def test_a_file_held_past_the_limit_raises_an_error_naming_it(
        tmp_path: pathlib.Path) -> None:
    """Bounded: a file held for longer than the limit is given up on, with
    an error naming the file, the time waited and the issue, and the file is
    left where it was."""
    log = tmp_path / "Cataclysm.log"
    log.write_text("the previous run's results", encoding="utf-8")
    waits = _Waits()
    with open(log, "a", encoding="utf-8"):
        with pytest.raises(PermissionError):
            log.unlink()  # the control, as above
        with pytest.raises(FileStillHeld) as raised:
            delete_when_released(log, wait=waits.wait, clock=waits.clock)

    message = str(raised.value)
    assert str(log) in message, f"the error must name the file: {message!r}"
    assert "120 seconds" in message and "#2459" in message, message
    expected = int(unreal_build.HELD_FILE_LIMIT_SECONDS
                   / unreal_build.HELD_FILE_RETRY_SECONDS)
    assert len(waits.waits) == expected, (
        f"{expected} waits of {unreal_build.HELD_FILE_RETRY_SECONDS:g} s fill the "
        f"limit; there were {len(waits.waits)}")
    assert log.exists(), "a file that could not be deleted is still there"


def test_a_refused_delete_is_retried_on_any_platform() -> None:
    """The same retry as above with the refusal made by `_HeldPath`, so that
    continuous integration, which runs on Linux, runs it too."""
    path = _HeldPath(refusals=2)
    waits = _Waits()

    waited = delete_when_released(path, wait=waits.wait, clock=waits.clock)

    assert path.deleted and path.attempts == 3
    assert waits.waits == [unreal_build.HELD_FILE_RETRY_SECONDS] * 2
    assert waited == 2 * unreal_build.HELD_FILE_RETRY_SECONDS


def test_a_delete_refused_past_the_limit_raises_on_any_platform() -> None:
    """The limit, with `_HeldPath` refusing every attempt."""
    path = _HeldPath(refusals=10**6)
    waits = _Waits()

    with pytest.raises(FileStillHeld, match="HeldByAnother.log"):
        delete_when_released(path, wait=waits.wait, clock=waits.clock)

    assert not path.deleted
    assert len(waits.waits) == int(unreal_build.HELD_FILE_LIMIT_SECONDS
                                   / unreal_build.HELD_FILE_RETRY_SECONDS)


@windows_only
def test_run_automation_tests_waits_for_a_held_log_instead_of_crashing(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The call site of both incidents. `TEST_LOG` is a temporary file held
    open; the editor is replaced by a stand-in that checks the old log is
    gone and writes a new one. The run must wait, then read the new log."""
    log = tmp_path / "Saved" / "Logs" / "Cataclysm.log"
    log.parent.mkdir(parents=True)
    log.write_text("the previous run's results", encoding="utf-8")
    monkeypatch.setattr(unreal_build, "TEST_LOG", log)
    monkeypatch.setattr(unreal_build, "EDITOR_CMD", pathlib.Path(__file__))
    seen_before_the_run: list[bool] = []

    def editor(*args, **kwargs):
        seen_before_the_run.append(log.exists())
        log.write_text(
            "LogAutomationController: Display: Test Completed. Result={Success} "
            "Name={AttributeDefaults}\n"
            "LogAutomationController: Display: ...Automation Test Queue Empty "
            "1 tests performed.\n", encoding="utf-8")
    monkeypatch.setattr(unreal_build.subprocess, "run", editor)

    handle = open(log, "a", encoding="utf-8")  # closed by the wait, or by the finally
    try:
        waits: list[float] = []

        def wait(seconds: float) -> None:
            waits.append(seconds)
            handle.close()

        tests = unreal_build.run_automation_tests("Cataclysm.Held.", wait=wait)
    finally:
        handle.close()

    assert waits == [unreal_build.HELD_FILE_RETRY_SECONDS], (
        f"the held log must be waited for once, not crashed on; waited {waits}")
    assert seen_before_the_run == [False], (
        "the old log must be deleted before the editor starts")
    assert tests.performed == 1 and tests.succeeded == ("AttributeDefaults",)


def test_the_held_file_defaults_are_the_documented_ones() -> None:
    """Every two seconds for up to two minutes, as issue #2459 and brief 78
    say, and the signature's defaults are those constants."""
    assert unreal_build.HELD_FILE_RETRY_SECONDS == 2.0
    assert unreal_build.HELD_FILE_LIMIT_SECONDS == 120.0
    parameters = inspect.signature(delete_when_released).parameters
    assert parameters["retry_seconds"].default == unreal_build.HELD_FILE_RETRY_SECONDS
    assert parameters["limit_seconds"].default == unreal_build.HELD_FILE_LIMIT_SECONDS
