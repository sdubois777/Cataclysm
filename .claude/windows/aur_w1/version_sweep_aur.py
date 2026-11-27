"""The written-out record version check for the window of #2741 slice 9, copied from the previous
window's version sweep (the empire tree's slices six to eight), itself from the earlier windows': every test that states
a save record's version as a written-out number, beside the committed fixture it reads.

    python -u version_sweep_fr.py        (exit 0 when no mismatch; prints "VERSION SWEEP: ..." last)

WHY. Issue #2753: a test that reads a committed fixture and states its version as a number is not moved by a bump, and
nothing finds it until a window. Development holds the account record at 6, the character record at 13 and the run
record at 14 (the gems window, #2836); slice 9 moves them to 7, 14 and 15.

WHAT IT DOES:
  1. reads the three records' versions out of `CataclysmSaveRecords.h` and requires run 22, character 18, account 8;
  2. in every `game/Source/*/Tests/*.cpp`, splits the file at each `RunTest(` and, in each test, finds every
     `SchemaVersion, <n>)` comparison and every "is version <n>" in a string; for each, the committed fixtures that test
     reads (`Run_vN.json`, `Character_vN.json`, `Account_vN.json`); a stated number that is the newest of one record
     kind and is not that record's current version, or a test whose only fixture states another number, is a mismatch;
  3. prints every line it found, so the table can be checked by eye.
"""
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
SOURCE = REPO / "game" / "Source"
HEADER = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
WANTED = {"Run": 22, "Character": 18, "Account": 8}
CLASSES = {"Run": "UCataclysmRunSave", "Character": "UCataclysmCharacterSave", "Account": "UCataclysmAccountSave"}

header = HEADER.read_text(encoding="utf-8")
current = {}
for kind, cls in CLASSES.items():
    part = header.split(f"class CATACLYSM_API {cls}")[1]
    found = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", part)
    current[kind] = int(found.group(1)) if found else None
print(f"records: run {current['Run']}, character {current['Character']}, account {current['Account']} "
      f"(wanted run {WANTED['Run']}, character {WANTED['Character']}, account {WANTED['Account']})")
problems = [f"{kind} record is at {current[kind]}, not {WANTED[kind]}" for kind in WANTED if current[kind] != WANTED[kind]]

stated = re.compile(r"SchemaVersion\s*,\s*(\d+)\s*\)|is version (\d+)")
fixture = re.compile(r"\b(Run|Character|Account|Example)_v(\d+)\.json")
lines = 0
seen = set()
for path in sorted(SOURCE.glob("*/Tests/*.cpp")):
    text = path.read_text(encoding="utf-8", errors="replace")
    starts = [m.start() for m in re.finditer(r"::RunTest\(", text)] + [len(text)]
    for begin, end in zip(starts, starts[1:], strict=False):
        body = text[begin:end]
        reads = {(m.group(1), int(m.group(2))) for m in fixture.finditer(body)}
        for match in stated.finditer(body):
            number = int(match.group(1) or match.group(2))
            line_no = text.count("\n", 0, begin + match.start()) + 1
            line = text.splitlines()[line_no - 1].strip()
            # ONE LINE ONCE: "it says it is version 9" and `SchemaVersion, 9)` are both on the usual line.
            if "SchemaVersion = " in line or "SetNumberField" in line or (path.name, line_no) in seen:
                continue
            seen.add((path.name, line_no))
            lines += 1
            kinds = sorted(reads)
            print(f"  {path.name}:{line_no}  states {number}  reads {kinds or 'no committed fixture'}  | {line[:110]}")
            game_reads = [(kind, version) for kind, version in reads if kind in current]
            newest = [(kind, version) for kind, version in game_reads if version == current[kind]]
            if len(game_reads) == 1 and game_reads[0][1] != number and game_reads[0][0] != "Example":
                kind, version = game_reads[0]
                # A MIGRATION TO THE CURRENT VERSION IS NOT A MISMATCH: a test that migrates the record's previous
                # committed file states the version it migrates TO. Allowed only when the stated number is that
                # record's current version, the file is exactly one below it, and the test migrates. Added for #2764's
                # SkillLevels test, which migrates Character_v9.json "to version 10"; the 3b copy flagged it.
                if (number == current.get(kind) and version == number - 1 and "Migrat" in body):
                    print(f"      allowed: a migration of {kind}_v{version}.json to the current {kind.lower()} "
                          f"version, {number}")
                    continue
                if newest or version == number - 1 or version == number + 1:
                    problems.append(f"{path.name}:{line_no} states {number} and reads {kind}_v{version}.json")
            for kind, version in newest:
                if number not in (version,) and not any(v == number for _, v in reads):
                    problems.append(f"{path.name}:{line_no} states {number} beside {kind}_v{version}.json, "
                                    f"the current {kind.lower()} file")

# THE PYTHON TESTS that state a fixture's or a record's version as a number, printed for the eye: a committed fixture's
# `SchemaVersion` compared with a number, or a record's version compared with one. A fixture compared with a number
# that is not the version in its own file name is a mismatch.
python_stated = re.compile(r'\[\s*"SchemaVersion"\s*\]\s*==\s*(\d+)|int\(version\.group\(1\)\)\s*(==|>=)\s*(\d+)')
for path in sorted((REPO / "tools" / "tests").glob("*.py")):
    text = path.read_text(encoding="utf-8", errors="replace")
    named = dict(re.findall(r'(\w+)\s*=\s*json\.loads\(\(FIXTURES\s*/\s*"(\w+_v\d+)\.json"\)', text))
    for line_no, line in enumerate(text.splitlines(), 1):
        match = python_stated.search(line)
        if not match:
            continue
        lines += 1
        print(f"  {path.name}:{line_no}  | {line.strip()[:110]}")
        if match.group(1):
            variable = re.search(r"(\w+)\s*\[", line)
            file = named.get(variable.group(1)) if variable else None
            if file and int(file.rsplit("_v", 1)[1]) != int(match.group(1)):
                problems.append(f"{path.name}:{line_no} states {match.group(1)} for {file}.json")

print(f"VERSION SWEEP: {lines} lines stating a version; "
      + ("NO MISMATCH" if not problems else f"{len(problems)} MISMATCH(ES): " + "; ".join(problems)))
sys.exit(0 if not problems else 1)
