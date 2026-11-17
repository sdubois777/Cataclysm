"""`tools/unreal_build.py tests` prints what the tests' save slots came to, beside its "Tests:" line, and fails on it.

WHY THIS EXISTS. Issue #2658: Unreal tests left their save slots behind. `FCataclysmTestSaves` now deletes, at each
test's end, the slots that test made, and logs the run's totals once as "Test saves: N written and deleted, M left
behind: <names>". That line is the only evidence that the hook which deletes them ran, so the wrapper prints it; a log
without it says so instead of printing nothing.

AND THE EXIT CODE, SINCE ISSUE #2673. #2658 reported the line only, until a window had shown M = 0; window w129B showed
it over the whole suite. Ruled 2026-11-01 by the coordinating session: M > 0 fails the `tests` command, naming the slots;
a missing line fails a run that performed at least one test; a run that performed none, or crashed, keeps its own reason
first; a narrow run (`--prefix`) follows the same rule; and a guard proof reports exactly what it did before, because it
reads test results only.
"""

from __future__ import annotations

import sys
import pathlib

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import unreal_build  # noqa: E402
from unreal_build import TestOutcome  # noqa: E402


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


# ---------------------------------------------------------------------------
# Only a line written after the run's last test started is the run's. Issue #2677.
# ---------------------------------------------------------------------------

#: The engine's smoke tests at start-up, each ending with a line of its own; window w132 measured 607.
START_UP = "[2026.11.01-21.04.10:092][  0]LogCataclysm: Display: Test saves: 0 written and deleted, 0 left behind\n" * 3
STARTED = ("[2026.11.01-21.04.17:198][598]LogAutomationController: Display: Test Started. Name={EachTest} "
           "Path={Cataclysm.SaveStorage.EachTest}\n")
EXITING = "[2026.11.01-21.04.17:269][601]LogCore: Engine exit requested (reason: Win RequestExit)\n"


def test_start_up_lines_alone_count_as_missing_when_tests_were_performed() -> None:
    """A run whose hook logged nothing for its own tests and whose exit line was lost: the start-up lines say
    "0 left behind" and must not pass it."""
    line = unreal_build.test_saves_line(START_UP + STARTED + EXITING)
    assert line.startswith("Test saves: (no line in the log"), line
    assert unreal_build.saves_failure(PASSED, line) is not None
    assert unreal_build.exit_code_for(PASSED, line) != 0


def test_the_line_after_the_last_test_started_is_the_one_read() -> None:
    log = (START_UP + STARTED
           + "[2026.11.01-21.04.17:268][600]LogCataclysm: Display: Test saves: 4 written and deleted, 1 left behind: "
             "Run_D\n" + EXITING)
    assert unreal_build.test_saves_line(log) == "Test saves: 4 written and deleted, 1 left behind: Run_D"


def test_a_lost_exit_line_passes_when_the_last_tests_line_is_there() -> None:
    """Window w131's case: the log ends at "Engine exit requested", and the line the last test's end logged is read.
    The test's own quotation of the line, logged after it by the automation controller, is not read."""
    line = unreal_build.test_saves_line(
        START_UP + STARTED
        + "[2026.11.01-21.04.17:268][600]LogCataclysm: Display: Test saves: 1 written and deleted, 0 left behind\n"
        + "[2026.11.01-21.04.17:268][600]LogAutomationController: TEST SAVES LINE: called at each test's end yes; "
          "returned 'Test saves: 1 written and deleted, 0 left behind'; logged 1\n"
        + EXITING)
    assert line == "Test saves: 1 written and deleted, 0 left behind"
    assert unreal_build.saves_failure(PASSED, line) is None
    assert unreal_build.exit_code_for(PASSED, line) == 0


def test_the_log_line_the_cpp_writes_matches_what_the_wrapper_reads() -> None:
    """The C++ summary's own wording, read from its source, so a rewording there cannot pass unread here."""
    source = (pathlib.Path(__file__).resolve().parents[2] / "game" / "Source" / "Cataclysm" / "Save"
              / "CataclysmTestSaves.cpp").read_text(encoding="utf-8")
    assert 'TEXT("Test saves: %d written and deleted, %d left behind")' in source, (
        "CataclysmTestSaves.cpp no longer writes the line tools/unreal_build.py's TEST_SAVES reads.")
    # AND THE TAIL `TEST_SAVES_PARTS` READS THE NAMES FROM, which `saves_failure` quotes (issue #2673).
    assert 'Line += TEXT(": ") + FString::Join(First, TEXT(", "));' in source, (
        "CataclysmTestSaves.cpp no longer writes the names after ': ', which saves_failure quotes.")
    assert 'TEXT(" and %d more")' in source, (
        "CataclysmTestSaves.cpp no longer writes ' and K more' after the first ten names.")


# ---------------------------------------------------------------------------
# The exit code. Issue #2673.
# ---------------------------------------------------------------------------

#: Three tests performed and passed, so any non-zero exit below comes from the save check alone.
PASSED = TestOutcome(3, ("a", "b", "c"), ())

CLEAN = "Test saves: 7 written and deleted, 0 left behind"
LEFT_TWO = "Test saves: 1520 written and deleted, 2 left behind: Run_A, Run_B"
MISSING = unreal_build.test_saves_line("")


def test_nothing_left_behind_passes() -> None:
    assert unreal_build.saves_failure(PASSED, CLEAN) is None
    assert unreal_build.exit_code_for(PASSED, CLEAN) == 0


def test_slots_left_behind_fail_the_run_and_are_named() -> None:
    assert unreal_build.exit_code_for(PASSED, LEFT_TWO) == 1
    message = unreal_build.saves_failure(PASSED, LEFT_TWO)
    assert message is not None
    assert "2 save slots behind" in message, message
    assert "Run_A, Run_B" in message, message


def test_one_slot_left_behind_is_said_in_the_singular_and_fails() -> None:
    line = "Test saves: 9 written and deleted, 1 left behind: Run_C"
    assert unreal_build.exit_code_for(PASSED, line) == 1
    message = unreal_build.saves_failure(PASSED, line)
    assert message is not None and "1 save slot behind" in message and "Run_C" in message, message


def test_more_than_ten_left_behind_quotes_the_count_of_the_rest() -> None:
    names = ", ".join(f"Run_{index}" for index in range(10))
    line = f"Test saves: 40 written and deleted, 15 left behind: {names} and 5 more"
    assert unreal_build.exit_code_for(PASSED, line) == 1
    message = unreal_build.saves_failure(PASSED, line)
    assert message is not None and f"{names} and 5 more" in message, message


def test_a_missing_line_fails_a_run_that_performed_tests() -> None:
    assert unreal_build.exit_code_for(PASSED, MISSING) == 1
    message = unreal_build.saves_failure(PASSED, MISSING)
    assert message is not None
    assert "the clean-up's summary was not logged" in message, message
    assert "has not shown its tests' saves were deleted" in message, message


@pytest.mark.parametrize("tests", [TestOutcome(None, (), ()), TestOutcome(0, (), ()), None],
                         ids=["crashed", "zero-performed", "no-outcome"])
def test_a_missing_line_adds_nothing_to_a_run_that_performed_no_tests(tests) -> None:
    """The run fails for its own reason, and the save check says nothing over it: with no test started, nothing took
    the folder and nothing could log the line."""
    assert unreal_build.exit_code_for(tests, MISSING) == 1
    assert unreal_build.saves_failure(tests, MISSING) is None


def test_a_failing_test_with_slots_left_still_fails_and_still_names_them() -> None:
    failing = TestOutcome(3, ("a", "b"), ("c",))
    assert unreal_build.exit_code_for(failing, LEFT_TWO) == 1
    assert "Run_A, Run_B" in (unreal_build.saves_failure(failing, LEFT_TWO) or "")


def test_the_save_check_has_no_default_so_no_caller_can_leave_it_out() -> None:
    with pytest.raises(TypeError):
        unreal_build.exit_code_for(PASSED)  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# The same rules through the command line, with no Unreal: the run is a stand-in that writes the log.
# ---------------------------------------------------------------------------

def a_log(performed: int, failed: tuple[str, ...] = (), saves: str | None = None) -> str:
    passed = [f"Passes{index}" for index in range(performed - len(failed))]
    lines = [f"LogAutomationController: Display: Test Completed. Result={{Success}} Name={{{name}}}" for name in passed]
    lines += [f"LogAutomationController: Display: Test Completed. Result={{Fail}} Name={{{name}}}" for name in failed]
    if performed:
        lines.append(f"LogAutomationController: Display: ...Automation Test Queue Empty {performed} tests performed.")
    if saves is not None:
        lines.append(f"[2026.11.01-17.00.01:000][  0]LogCataclysm: Display: {saves}")
    return "\r\n".join(lines) + "\r\n"


@pytest.fixture
def run_tests(tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch):
    """`main(["tests", "--no-build", ...])` against a stand-in run that writes `log_text` to a log outside the
    repository. Returns (exit code, printed text, the prefixes the run was asked for)."""
    log = tmp_path / "Logs" / "Cataclysm.log"
    log.parent.mkdir(parents=True)
    monkeypatch.setattr(unreal_build, "TEST_LOG", log)
    monkeypatch.setattr(unreal_build, "declared_tests", lambda: ())
    monkeypatch.setattr(unreal_build, "current_commit", lambda: "0" * 40)

    def run(log_text: str, *arguments: str, capsys: pytest.CaptureFixture[str]):
        prefixes: list[str] = []

        def stand_in(prefix: str = "Cataclysm", *args, **kwargs) -> TestOutcome:
            prefixes.append(prefix)
            log.write_text(log_text, encoding="utf-8", newline="")
            return unreal_build.parse_test_log(log_text)

        monkeypatch.setattr(unreal_build, "run_automation_tests", stand_in)
        capsys.readouterr()
        code = unreal_build.main(["tests", "--no-build", *arguments])
        return code, capsys.readouterr().out, prefixes

    return run


def test_the_command_passes_a_clean_run_with_nothing_left_behind(run_tests, capsys) -> None:
    code, printed, _ = run_tests(a_log(3, saves=CLEAN), capsys=capsys)
    assert code == 0, printed
    assert CLEAN in printed
    assert "FAILED" not in printed, printed


def test_the_command_fails_a_run_that_left_slots_and_names_them(run_tests, capsys) -> None:
    code, printed, _ = run_tests(a_log(3, saves=LEFT_TWO), capsys=capsys)
    assert code == 1, printed
    assert "Tests: 3 tests performed, 3 succeeded, 0 failed" in printed, printed
    assert "FAILED: the tests left 2 save slots behind in game/Saved/SaveGames: Run_A, Run_B." in printed, printed
    assert "No test results were read" not in printed, printed


def test_the_command_fails_a_run_that_performed_tests_and_logged_no_line(run_tests, capsys) -> None:
    code, printed, _ = run_tests(a_log(3), capsys=capsys)
    assert code == 1, printed
    assert "FAILED: the clean-up's summary was not logged" in printed, printed
    assert "No test results were read" not in printed, printed


def test_the_command_keeps_the_no_tests_reason_for_a_run_that_performed_none(run_tests, capsys) -> None:
    code, printed, _ = run_tests(a_log(0), capsys=capsys)
    assert code == 1, printed
    assert "No test results were read" in printed, printed
    assert "FAILED: the clean-up's summary was not logged" not in printed, printed


def test_the_command_says_the_no_tests_reason_before_slots_a_crashed_run_left(run_tests, capsys) -> None:
    code, printed, _ = run_tests(a_log(0, saves=LEFT_TWO), capsys=capsys)
    assert code == 1, printed
    assert "No test results were read" in printed and "Run_A, Run_B." in printed, printed
    assert printed.index("No test results were read") < printed.index("FAILED: the tests left"), printed


@pytest.mark.parametrize("saves, expected", [(LEFT_TWO, 1), (CLEAN, 0), (None, 1)],
                         ids=["left-behind", "clean", "missing"])
def test_a_narrow_run_follows_the_same_rule(run_tests, capsys, saves, expected) -> None:
    code, printed, prefixes = run_tests(a_log(2, saves=saves), "--prefix", "Cataclysm.SaveLoad.", capsys=capsys)
    assert prefixes == ["Cataclysm.SaveLoad."], prefixes
    assert code == expected, printed


# ---------------------------------------------------------------------------
# A guard proof reports what it reported before. Ruling 4 of issue #2673.
# ---------------------------------------------------------------------------

def test_the_test_outcome_a_proof_reads_is_the_same_with_or_without_a_leftover_line() -> None:
    """`prove_cpp_guard` judges its halves on the `TestOutcome` its tester returns, which `parse_test_log` builds.
    A leftover line must change nothing in it, or a proof whose broken half leaves slots would read differently."""
    without = unreal_build.parse_test_log(a_log(4, failed=("Notices",)))
    for saves in (LEFT_TWO, CLEAN, None):
        assert unreal_build.parse_test_log(a_log(4, failed=("Notices",), saves=saves)) == without


def test_a_cpp_guard_proof_is_unchanged_by_slots_its_halves_left(tmp_path: pathlib.Path,
                                                                 monkeypatch: pytest.MonkeyPatch) -> None:
    """A proof whose two halves each log slots left behind is PROVED, with the same named failures, exactly as the
    same proof whose halves logged none -- and as one whose halves logged no line at all."""
    monkeypatch.setattr(unreal_build, "RECOVERY_FILE", tmp_path / "recovery" / "cpp_guard_originals.json")
    monkeypatch.setattr(unreal_build, "REPO_ROOT", tmp_path)
    source = tmp_path / "Thing.cpp"

    def prove(saves: str | None) -> unreal_build.CppGuardResult:
        source.write_text("float Reach() { return 250.0f; }\n", encoding="utf-8")
        text = "Building X...\n[1/1] Compile [x64] Thing.cpp\nResult: Succeeded\n"
        result, compiled, up_to_date, actions = unreal_build.parse_build_output(text)
        built = unreal_build.BuildOutcome(0, text, result, compiled, up_to_date, actions)
        halves = [a_log(2, failed=("Notices",), saves=saves), a_log(2, saves=saves)]
        return unreal_build.prove_cpp_guard(
            {"Thing.cpp": lambda t: t.replace("250.0f", "0.0f")}, test_prefix="Cataclysm.Thing.",
            builder=lambda target: built,
            tester=lambda prefix: unreal_build.parse_test_log(halves.pop(0)))

    clean = prove(CLEAN)
    assert clean.proved, clean.summary
    assert clean.named_failures == ("Notices",)
    for saves in (LEFT_TWO, None):
        result = prove(saves)
        assert (result.proved, result.named_failures, result.failed, result.crashed, result.summary) == (
            clean.proved, clean.named_failures, clean.failed, clean.crashed, clean.summary), saves
        assert result.tests == clean.tests and result.tests_restored == clean.tests_restored, saves
