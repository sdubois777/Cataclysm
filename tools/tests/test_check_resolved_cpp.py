"""Tests for tools/check_resolved_cpp.py, the checker for hand-resolved C++.

Issue #1610. Both directions are tested, because a checker is only worth its
word if a healthy tree comes out clean AND a broken file does not:

- **the healthy-tree control** runs the checker over every C++ file under
  `game/Source` and requires zero complaints, and a lower bound on the files it
  read, so a reader that finds no files cannot pass;
- **each fault** it looks for, on a small hand-made file;
- **the two false alarms #1610 measured** on the real tree -- a digit separator
  and a bracket written as a character literal -- which must come out clean;
- **the fault #1610 is about, on the real header it happened in**:
  `CataclysmStatPipeline.h` with one documentation comment's opening removed.
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import check_resolved_cpp as checker  # noqa: E402

STAT_PIPELINE_H = (checker.SOURCE / "Cataclysm" / "AbilitySystem"
                   / "CataclysmStatPipeline.h")

#: Measured 2026-09-23 on `development` at 9ff0de2e: 457 files. A lower bound,
#: so the tree may grow; it exists so a reader that found nothing cannot pass.
AT_LEAST_THIS_MANY_FILES = 400


def complaints(text: str) -> list[str]:
    return checker.check_text(text).complaints


def test_the_healthy_tree_has_no_complaints():
    """The control: every C++ file under game/Source, which compiles."""
    paths = checker.control_files()
    assert len(paths) >= AT_LEAST_THIS_MANY_FILES, (
        f"only {len(paths)} C++ files were found under {checker.SOURCE}, so the "
        f"control checked too little to mean anything")
    found = {path.name: checker.check_file(path).complaints for path in paths}
    faulty = {name: said for name, said in found.items() if said}
    assert not faulty, (
        "the checker complains about C++ files that compile, so a complaint "
        "about a resolved conflict cannot be trusted: " + repr(faulty))


def test_the_command_line_control_says_how_many_files_it_read():
    finished = subprocess.run(
        [sys.executable, str(pathlib.Path(checker.__file__)), "--control"],
        capture_output=True, text=True, encoding="utf-8")
    assert finished.returncode == 0, finished.stdout + finished.stderr
    read = re.search(r"(\d+) files read, 0 with complaints", finished.stdout)
    assert read and int(read.group(1)) >= AT_LEAST_THIS_MANY_FILES, finished.stdout


def test_reading_no_file_is_not_a_pass(tmp_path, monkeypatch):
    monkeypatch.setattr(checker, "control_files", lambda: [])
    assert checker.main(["--control"]) == 2


def test_a_conflict_marker_is_found_with_its_line():
    said = complaints("int a;\n<<<<<<< HEAD\nint b;\n=======\nint c;\n"
                      ">>>>>>> 0123456 (a commit being replayed)\n")
    assert said == ["line 2: a conflict marker, '<<<<<<< HEAD'"], said


def test_a_block_comment_never_closed_is_found():
    assert complaints("int a; /* never closed\nint b;\n") == [
        "line 1: a block comment opened and never closed"]


def test_a_brace_left_open_is_found():
    said = complaints("void F()\n{\n\tif (X)\n\t{\n\t\tY();\n}\n")
    assert said == ["1 more { than its partner, outside comments and literals"], said


def test_two_function_halves_joined_by_a_boundary_are_found():
    """The second fault #1610 records: one helper's opening joined to another's
    body, leaving the first with no closing brace."""
    joined = ("int First()\n{\n\tint Field = 1;\n"
              "int Second()\n{\n\tint Other = 2;\n\treturn Other;\n}\n")
    assert complaints(joined) == [
        "1 more { than its partner, outside comments and literals"]


def test_the_1610_shape_keep_both_with_one_shared_opening_is_found():
    """A shared `/**` outside the conflict, and both sides' bodies kept."""
    resolved = ("enum class E\n{\n"
                "\t/**\n\t * The first side's entry.\n\t */\n\tFirst,\n"
                "\t * The second side's entry, THE EFFECT'S CAUSER.\n\t */\n"
                "\tSecond,\n};\n")
    said = complaints(resolved)
    assert said == ["line 8: a '*/' outside any comment, so a comment's opening "
                    "'/*' is missing above it"], said


def test_the_same_resolution_with_the_opening_restored_is_clean():
    resolved = ("enum class E\n{\n"
                "\t/**\n\t * The first side's entry.\n\t */\n\tFirst,\n"
                "\t/**\n\t * The second side's entry, THE EFFECT'S CAUSER.\n\t */\n"
                "\tSecond,\n};\n")
    assert complaints(resolved) == []


def test_digit_separators_are_not_character_literals():
    """#1610: 28 false complaints over 439 healthy files came from this alone."""
    assert complaints("void F() { Set(10'000'000.0f); G(0xFF'FF); H(1'0); }\n") == []


def test_quotes_and_brackets_written_as_character_literals_are_one_character():
    """#1610: `TEXT('"')` and `TEXT('(')` are in the real tree."""
    assert complaints("void F() { A(TEXT('(')); B(TEXT('\"')); C('}'); }\n") == []


def test_braces_and_comment_openings_inside_strings_count_for_nothing():
    assert complaints('void F() { S(TEXT("{ ( // /* */")); }\n') == []
    assert complaints('auto S = R"x(unbalanced { ( */ here)x";\n') == []


def test_the_fault_on_the_real_header_it_happened_in():
    """`CataclysmStatPipeline.h` with the opening of the documentation comment
    above its last condition removed, which is what keeping both sides of that
    conflict does. The unbroken header is the control."""
    text = STAT_PIPELINE_H.read_text(encoding="utf-8")
    assert complaints(text) == [], "the unbroken header already has complaints"

    enum = re.search(r"enum class ECataclysmStatCondition\b.*?\n\};", text, re.S)
    assert enum, "no ECataclysmStatCondition in the header"
    last_opening = enum.group(0).rfind("\t/**\n")
    assert last_opening > 0, "no documentation comment inside the enum"
    at = enum.start() + last_opening
    broken = text[:at] + text[at + len("\t/**\n"):]

    said = complaints(broken)
    assert any("'*/' outside any comment" in line for line in said), said
