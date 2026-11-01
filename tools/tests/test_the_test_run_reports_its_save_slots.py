"""`tools/unreal_build.py tests` prints what the tests' save slots came to, beside its "Tests:" line.

WHY THIS EXISTS. Issue #2658: Unreal tests left their save slots behind. `FCataclysmTestSaves` now deletes, at each
test's end, the slots that test made, and logs the run's totals once as "Test saves: N written and deleted, M left
behind: <names>". That line is the only evidence that the hook which deletes them ran, so the wrapper prints it; a log
without it says so instead of printing nothing. Reported only, ruled 2026-11-01: it never changes the exit code.
"""

from __future__ import annotations

import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import unreal_build  # noqa: E402


def test_the_line_is_read_from_the_log() -> None:
    log = ("[2026.11.01-17.00.00:000][  0]LogAutomationController: Automation Test Queue Empty 3 tests performed.\n"
           "[2026.11.01-17.00.01:000][  0]LogCataclysm: Display: Test saves: 1520 written and deleted, 2 left behind: "
           "Run_A, Run_B\r\n")
    assert unreal_build.test_saves_line(log) == "Test saves: 1520 written and deleted, 2 left behind: Run_A, Run_B"


def test_nothing_left_behind_is_read_whole() -> None:
    log = "LogCataclysm: Display: Test saves: 7 written and deleted, 0 left behind\n"
    assert unreal_build.test_saves_line(log) == "Test saves: 7 written and deleted, 0 left behind"


def test_a_log_without_the_line_says_so() -> None:
    line = unreal_build.test_saves_line("LogAutomationController: Automation Test Queue Empty 3 tests performed.\n")
    assert line.startswith("Test saves: (no line in the log")


def test_the_later_of_two_lines_is_reported() -> None:
    log = ("Test saves: 1 written and deleted, 0 left behind\n"
           "Test saves: 9 written and deleted, 1 left behind: Run_C\n")
    assert unreal_build.test_saves_line(log) == "Test saves: 9 written and deleted, 1 left behind: Run_C"


def test_the_log_line_the_cpp_writes_matches_what_the_wrapper_reads() -> None:
    """The C++ summary's own wording, read from its source, so a rewording there cannot pass unread here."""
    source = (pathlib.Path(__file__).resolve().parents[2] / "game" / "Source" / "Cataclysm" / "Save"
              / "CataclysmTestSaves.cpp").read_text(encoding="utf-8")
    assert 'TEXT("Test saves: %d written and deleted, %d left behind")' in source, (
        "CataclysmTestSaves.cpp no longer writes the line tools/unreal_build.py's TEST_SAVES reads.")
