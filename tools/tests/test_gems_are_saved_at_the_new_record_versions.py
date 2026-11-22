"""The gems window's save steps (issue #46, slices G2, G3 and G5): the character record at version 13, the run record at 14.

An item's `Sockets` was a count and is a list of typed sockets; a carried or stashed slot gained `Gem` and `GemRarity`;
a ground item gained `Gem`, `GemRarity` and `GemQuantity`. The coordinating session ruled on 2026-11-21 that "sockets
migrated from today's count fit any type", so each step writes a saved count of N as N empty sockets of type Any.

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the two records are at 13 and 14 and each chain has the new step, which writes sockets of type Any -- without it a
     saved count would refuse the whole record, because a list cannot be read from a number;
  2. `Character_v13.json` is `Character_v12.json` with the step's writes, apart from the hand-written sockets that show
     the fields are read, and `Run_v14.json` is `Run_v13.json` with the step's writes;
  3. every Unreal test that runs an older file through the chain adds what the new step writes, and only the two
     `Cataclysm.GemSave.` tests read the files left behind;
  4. the fixtures README lists both new files.
"""
from __future__ import annotations

import copy
import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SAVE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save"
TESTS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"

EMPTY_ANY = {"Type": "Any", "Gem": "None", "GemRarity": "Everyday"}


def code(path: pathlib.Path) -> str:
    """The file with its comments removed, so a name written only in a comment does not count."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def version_of(record: str) -> int:
    match = re.search(rf"class\s+CATACLYSM_API\s+{record}\s*:.*?static\s+constexpr\s+int32\s+SchemaVersionNow\s*=\s*"
                      r"(\d+)\s*;", code(SAVE / "CataclysmSaveRecords.h"), re.DOTALL)
    assert match, f"no SchemaVersionNow for {record}"
    return int(match.group(1))


def namespace(text: str, name: str) -> str:
    start = text.find(f"namespace {name}")
    assert start != -1, f"CataclysmSaveRecords.cpp has no namespace {name}"
    following = text.find("\nnamespace ", start + 1)
    return text[start:following if following != -1 else len(text)]


def sockets_as_lists(value):
    """What the step writes: each count of N as N empty sockets of type Any, at any depth."""
    if isinstance(value, dict):
        if isinstance(value.get("Sockets"), int):
            value["Sockets"] = [dict(EMPTY_ANY) for _ in range(value["Sockets"])]
        for inner in value.values():
            sockets_as_lists(inner)
    elif isinstance(value, list):
        for inner in value:
            sockets_as_lists(inner)
    return value


def no_gem(entries: list, quantity: bool) -> None:
    for entry in entries:
        entry["Gem"] = "None"
        entry["GemRarity"] = "Everyday"
        if quantity:
            entry["GemQuantity"] = 0


def fixture(name: str) -> dict:
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


# --------------------------------------------------------------------------
# 1: the versions and the steps
# --------------------------------------------------------------------------

def test_the_character_record_is_at_13_and_the_run_record_at_14_each_with_its_step() -> None:
    assert version_of("UCataclysmCharacterSave") == 13
    assert version_of("UCataclysmRunSave") == 14
    records = code(SAVE / "CataclysmSaveRecords.cpp")
    character = namespace(records, "CataclysmCharacterSaveMigration")
    run = namespace(records, "CataclysmRunSaveMigration")
    assert re.search(r'\{\s*12\s*,\s*TEXT\("Migrate_12_to_13"\)\s*,\s*&Migrate_12_to_13\s*\}', character)
    assert re.search(r'\{\s*13\s*,\s*TEXT\("Migrate_13_to_14"\)\s*,\s*&Migrate_13_to_14\s*\}', run)
    for step, text in (("Migrate_12_to_13", character), ("Migrate_13_to_14", run)):
        assert f"bool {step}(" in text, f"{step} is not defined where its chain is"
        body = text[text.find(f"bool {step}("):]
        body = body[:body.find("\n\t}") + 3]
        assert "CataclysmSaveGems::WriteTheSocketLists(Record, OutError)" in body, f"{step} leaves socket counts as numbers"
        assert "CataclysmSaveGems::WriteNoGemOnEach(" in body, f"{step} writes no gem nowhere"
    gems = namespace(records, "CataclysmSaveGems")
    assert 'SetStringField(TEXT("Type"), TEXT("Any"))' in gems, (
        "a migrated socket is not of type Any, the ruling of 2026-11-21")


def test_the_account_record_stays_at_6_because_its_stash_is_empty() -> None:
    """The account's `Stash` is a list of the slot that gained the gem fields, so moving the account would be needed the
    day something fills it. Nothing does yet: the committed account file holds it empty."""
    assert version_of("UCataclysmAccountSave") == 6
    assert fixture("Account_v6.json")["Stash"] == [], (
        "Account_v6.json holds a stash: an account can now hold a socket count, so the account record must move too")


# --------------------------------------------------------------------------
# 2: the committed files
# --------------------------------------------------------------------------

def test_character_v13_is_v12_with_the_steps_writes_and_its_own_socket_types() -> None:
    before = fixture("Character_v12.json")
    after = fixture("Character_v13.json")
    assert after["SchemaVersion"] == 13
    expected = sockets_as_lists(copy.deepcopy(before))
    expected["SchemaVersion"] = 13
    no_gem(expected["CarriedSlots"], quantity=False)
    no_gem(expected["PrivateStash"], quantity=False)
    carried = after["CarriedSlots"][0]["Item"]["Sockets"]
    worn = after["WornGear"][0]["Item"]["Sockets"]
    assert [s["Type"] for s in carried] == ["Attack", "Defense"], carried
    assert carried[0]["Gem"] == "Gem_Of_The_Viper" and carried[0]["GemRarity"] == "Quality", carried
    assert [s["Type"] for s in worn] == ["Attack", "Defense", "Utility"], worn
    # THE HAND-WRITTEN SOCKETS ARE WHAT SHOWS A READ; everything else is the step's writes.
    expected["CarriedSlots"][0]["Item"]["Sockets"] = carried
    expected["WornGear"][0]["Item"]["Sockets"] = worn
    assert after == expected, "Character_v13.json differs from Character_v12.json beyond the step's writes and its sockets"


def test_run_v14_is_v13_with_the_steps_writes() -> None:
    before = fixture("Run_v13.json")
    after = fixture("Run_v14.json")
    expected = sockets_as_lists(copy.deepcopy(before))
    expected["SchemaVersion"] = 14
    no_gem(expected["Floor"]["GroundItems"], quantity=True)
    assert after == expected, "Run_v14.json differs from Run_v13.json beyond its version and the step's writes"


# --------------------------------------------------------------------------
# 3 and 4: the tests that read them, and the README
# --------------------------------------------------------------------------

def test_every_chain_test_adds_the_new_step_and_only_the_gem_save_tests_read_the_old_files() -> None:
    chain = 0
    for path in sorted(TESTS.glob("*.cpp")):
        text = code(path)
        for older, newer in (("AddTheCharactersVersion12Defaults(", "AddTheCharactersVersion13Defaults("),
                             ("AddTheRunsVersion13Defaults(", "AddTheRunsVersion14Defaults(")):
            chain += text.count(older)
            assert text.count(older) <= text.count(newer), (
                f"{path.name} runs an older file through the chain without what the gems window's step writes")
        for left in ("Character_v12.json", "Run_v13.json"):
            for match in re.finditer(rf'TEXT\("{re.escape(left)}"\)', text):
                names = re.findall(r'"(Cataclysm\.[\w.]+)"', text[:match.start()])
                assert names and names[-1].startswith("Cataclysm.GemSave."), (
                    f"{path.name} reads {left} in {names[-1] if names else 'a test with no name'}: it is a migration "
                    "input since the gems window, and the committed file is the next one")
    # 19 MEASURED ON 2026-11-22: 7 character chain tests and 12 run chain tests.
    assert chain >= 19, f"only {chain} chain tests were found; the sweep reads the wrong folder"
    readme = (FIXTURES / "README.md").read_text(encoding="utf-8")
    assert "| `Character_v13.json` |" in readme and "| `Run_v14.json` |" in readme
