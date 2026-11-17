"""A run whose engine log stops after its last test's events is counted from its "Test Completed" lines.

WHY THIS EXISTS. Issue #2679. The engine log sometimes stops right after the last test's events, before "Automation
Test Queue Empty N tests performed". `parse_test_log` read N from that line alone, so a run whose every test had
finished read as crashed, and a guard proof as NO MEASUREMENT. Three runs on 2026-11-01; one of them, window w131's
proof P3, cost a follow-up window, and its log is the fixture here.

THE RULE, ruled 2026-11-01: without the queue-empty line, the run's tests are the engine's own list for the run -- the
LAST "Found N automation tests based on '<prefix>'" block and its N paths -- and the run counts as performed when every
listed test has a "Test Completed" line after that block. `counted_from_completed_lines` says the count came from
there, and `summary` prints it. Anything less leaves `performed` None, so `crashed` stays True.
"""

from __future__ import annotations

import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import unreal_build  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent

#: Window w131's proof P3, its broken half's last 19 lines, verbatim. See the file's own header.
W131_P3 = (HERE / "w131_p3_cut_off_engine_log.txt").read_text(encoding="utf-8")


def found(*paths: str) -> list[str]:
    """The engine's list for a run of `paths`, as it prints it."""
    return ([f"[2026.11.01-23.14.30:001][598]LogAutomationCommandLine: Display: Found {len(paths)} automation tests "
             f"based on 'Cataclysm.Thing.'"]
            + [f"[2026.11.01-23.14.30:001][598]LogAutomationCommandLine: Display: \t{path}" for path in paths])


def completed(path: str, result: str = "Success") -> list[str]:
    """One test's start, result and events, as the controller prints them."""
    name = path.rsplit(".", 1)[-1]
    return [f"[2026.11.01-23.14.30:006][598]LogAutomationController: Display: Test Started. Name={{{name}}} "
            f"Path={{{path}}}",
            f"[2026.11.01-23.14.30:301][600]LogAutomationController: Display: Test Completed. Result={{{result}}} "
            f"Name={{{name}}} Path={{{path}}}",
            f"[2026.11.01-23.14.30:301][600]LogAutomationController: BeginEvents: {path}",
            f"[2026.11.01-23.14.30:301][600]LogAutomationController: EndEvents: {path}"]


QUEUE_EMPTY = "[2026.11.01-23.14.30:302][601]LogAutomationCommandLine: Display: ...Automation Test Queue Empty {} tests performed."
FIRST, SECOND = "Cataclysm.Thing.AFirstThing", "Cataclysm.Thing.ASecondThing"


def a_log(lines: list[str]) -> str:
    return "\r\n".join(lines) + "\r\n"


# ---------------------------------------------------------------------------
# The real case.
# ---------------------------------------------------------------------------

def test_window_w131s_proof_p3_reads_as_one_named_failure_and_not_a_crash() -> None:
    assert "Automation Test Queue Empty" not in W131_P3, "the fixture is no longer the cut-off log"
    tests = unreal_build.parse_test_log(W131_P3)
    assert tests.performed == 1
    assert tests.failed == ("ACharacterFileFromBeforeTheTierMigratesToTierOne",)
    assert tests.succeeded == ()
    assert not tests.crashed
    assert tests.counted_from_completed_lines
    assert "counted from their Test Completed lines" in tests.summary, tests.summary


# ---------------------------------------------------------------------------
# Made-up logs, one case each.
# ---------------------------------------------------------------------------

def test_a_log_cut_off_after_the_last_events_is_counted_from_its_completed_lines() -> None:
    tests = unreal_build.parse_test_log(a_log(found(FIRST, SECOND) + completed(FIRST) + completed(SECOND, "Fail")))
    assert (tests.performed, tests.failed, tests.crashed, tests.counted_from_completed_lines) == (
        2, ("ASecondThing",), False, True)


def test_a_log_cut_off_during_a_test_is_still_a_crash() -> None:
    cut = completed(SECOND)[:1]  # the second test started; nothing after
    tests = unreal_build.parse_test_log(a_log(found(FIRST, SECOND) + completed(FIRST) + cut))
    assert tests.crashed and tests.performed is None and not tests.counted_from_completed_lines


def test_a_log_cut_off_before_a_listed_test_started_is_still_a_crash() -> None:
    tests = unreal_build.parse_test_log(a_log(found(FIRST, SECOND) + completed(FIRST)))
    assert tests.crashed and not tests.counted_from_completed_lines


def test_a_list_cut_off_short_of_its_count_is_still_a_crash() -> None:
    short = found(FIRST, SECOND)[:2]  # "Found 2", one path
    tests = unreal_build.parse_test_log(a_log(short + completed(FIRST)))
    assert tests.crashed


def test_a_log_with_neither_line_nor_list_is_still_a_crash() -> None:
    tests = unreal_build.parse_test_log(a_log(completed(FIRST)))
    assert tests.crashed and tests.performed is None


def test_a_list_of_none_is_still_a_crash() -> None:
    nothing = ["[2026.11.01-23.14.30:001][598]LogAutomationCommandLine: Display: Found 0 automation tests based on "
               "'Cataclysm.Nothing.'"]
    assert unreal_build.parse_test_log(a_log(nothing)).crashed


def test_the_queue_empty_line_is_read_first_and_the_fallback_is_not_said() -> None:
    tests = unreal_build.parse_test_log(a_log(found(FIRST) + completed(FIRST) + [QUEUE_EMPTY.format(1)]))
    assert (tests.performed, tests.crashed, tests.counted_from_completed_lines) == (1, False, False)
    assert "counted from" not in tests.summary


def test_only_the_last_list_counts_when_a_log_holds_two() -> None:
    """Not seen in any log: a log holds one editor run, and each run printed one list. Tested because the rule names
    the last list, so that a second session's tests are what decides it."""
    earlier = found(FIRST) + completed(FIRST)
    later_finished = unreal_build.parse_test_log(a_log(earlier + found(SECOND) + completed(SECOND)))
    assert (later_finished.performed, later_finished.counted_from_completed_lines) == (1, True)
    later_cut = unreal_build.parse_test_log(a_log(found(SECOND) + completed(SECOND) + found(FIRST)))
    assert later_cut.crashed, "the earlier list's finished test stood in for the later list's unstarted one"


# ---------------------------------------------------------------------------
# Through a guard proof.
# ---------------------------------------------------------------------------

def prove(tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch, broken: str, restored: str):
    monkeypatch.setattr(unreal_build, "RECOVERY_FILE", tmp_path / "recovery" / "cpp_guard_originals.json")
    monkeypatch.setattr(unreal_build, "REPO_ROOT", tmp_path)
    source = tmp_path / "Thing.cpp"
    source.write_text("float Reach() { return 250.0f; }\n", encoding="utf-8")
    text = "Building X...\n[1/1] Compile [x64] Thing.cpp\nResult: Succeeded\n"
    result, compiled, up_to_date, actions = unreal_build.parse_build_output(text)
    built = unreal_build.BuildOutcome(0, text, result, compiled, up_to_date, actions)
    halves = [broken, restored]
    return unreal_build.prove_cpp_guard(
        {"Thing.cpp": lambda t: t.replace("250.0f", "0.0f")}, test_prefix="Cataclysm.SaveRecords.",
        builder=lambda target: built,
        tester=lambda prefix: unreal_build.parse_test_log(halves.pop(0)))


#: The same test passing, with a log that finished.
RESTORED = a_log(found("Cataclysm.SaveRecords.ACharacterFileFromBeforeTheTierMigratesToTierOne")
                 + completed("Cataclysm.SaveRecords.ACharacterFileFromBeforeTheTierMigratesToTierOne")
                 + [QUEUE_EMPTY.format(1)])


def test_a_proof_whose_broken_half_was_cut_off_after_its_events_is_proved(tmp_path, monkeypatch) -> None:
    result = prove(tmp_path, monkeypatch, W131_P3, RESTORED)
    assert result.proved, result.summary
    assert result.named_failures == ("ACharacterFileFromBeforeTheTierMigratesToTierOne",)
    assert not result.crashed
    assert result.summary.startswith("PROVED: "), result.summary
    assert "counted from their Test Completed lines" in result.summary


def test_a_proof_whose_restored_half_was_cut_off_after_its_events_is_proved_too(tmp_path, monkeypatch) -> None:
    cut_restored = RESTORED.replace(QUEUE_EMPTY.format(1) + "\r\n", "")
    assert "Queue Empty" not in cut_restored
    result = prove(tmp_path, monkeypatch, W131_P3, cut_restored)
    assert result.proved, result.summary


def test_a_proof_whose_broken_half_was_cut_off_during_its_test_is_still_no_measurement(tmp_path, monkeypatch) -> None:
    during = W131_P3.split("[2026.11.01-23.14.30:301][600]LogAutomationController: Error: Test Completed.")[0]
    result = prove(tmp_path, monkeypatch, during, RESTORED)
    assert not result.proved and result.crashed
    assert result.summary.startswith("NO MEASUREMENT"), result.summary
