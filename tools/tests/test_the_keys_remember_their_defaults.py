"""The keys remember what each held by default, and the character record keeps it (issue #837 slice 1).

WHY THIS EXISTS. The project owner, 2026-11-24, issue #837, chosen option verbatim: "Approve all three (Recommended)".
Slice 1: when a weapon goes on or off the keys stay as they are (Q1), a skill that does nothing takes a key only when
nothing built is made for it (Q2), and a choice whose weapon is off survives a load (Q7). The keys' defaults are kept
on the player state as `RememberedDefaults` and saved in the character record at version 17.

WHAT IS CHECKED, by reading the C++ with comments removed, so a comment naming a call cannot pass for one:

  1. the character record declares `RememberedDefaults` as a `SaveGame` field, its version is 17 or later, and its own
     step `Migrate_16_to_17` -- looked up inside the character record's namespace, since the run record has a step of
     the same name -- writes the field and is in its chain;
  2. `Character_v17.json` is `Character_v16.json` with version 17, one remembered default and one in its held build,
     and the fixtures README lists it;
  3. the gather writes the field, and the load puts the choices and the defaults back through `RestoreSkillChoices`
     BEFORE its one announcement and no longer through `AssignSkill`;
  4. the pawn hands the defaults to the weapon slots before the fill and remembers them after it, and the weapon slots
     hand them to `FillWithChoices`;
  5. a captured build copies them and the double's fill reads them (ruled by the coordinating session, 2026-11-24: a
     double holds the keys the player actually had).

WHAT IS NOT. What the keys hold in play: the `Cataclysm.SkillDefaults.` automation tests check that.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))

from test_the_play_loop_is_logged_at_each_step import function_body, read_code, squeeze  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
RECORDS_H = SOURCE / "Save" / "CataclysmSaveRecords.h"
RECORDS_CPP = SOURCE / "Save" / "CataclysmSaveRecords.cpp"
GATHER = SOURCE / "Save" / "CataclysmSaveGather.cpp"
APPLY = SOURCE / "Save" / "CataclysmSaveApply.cpp"
PAWN = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
BUILD = SOURCE / "Character" / "CataclysmPlayerBuild.cpp"
DOUBLE = SOURCE / "Character" / "CataclysmConsumptionDoubleCharacter.cpp"
SLOTS = SOURCE / "Items" / "CataclysmWeaponSlotsComponent.cpp"
FIXTURES = ROOT / "game" / "Tests" / "SaveFixtures"


def character_record_header() -> str:
    """The character record's class, from its declaration to the next record's."""
    code = read_code(RECORDS_H)
    start = code.index("class CATACLYSM_API UCataclysmCharacterSave")
    end = code.find("class CATACLYSM_API", start + 1)
    return code[start:end if end != -1 else len(code)]


def character_migrations() -> str:
    """The character record's migration namespace and nothing else: the run record has its own `Migrate_16_to_17`."""
    code = read_code(RECORDS_CPP)
    start = code.index("namespace CataclysmCharacterSaveMigration")
    end = code.index("namespace CataclysmRunSaveMigration")
    return code[start:end]


def test_the_record_holds_the_field_at_version_seventeen() -> None:
    header = character_record_header()
    assert re.search(r"UPROPERTY\(SaveGame[^)]*\)\s*TArray<FCataclysmSkillChoice>\s+RememberedDefaults;", header), (
        "UCataclysmCharacterSave declares no SaveGame RememberedDefaults, so the keys' defaults are not saved")
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", header)
    assert version and int(version.group(1)) >= 17, "the character record is below version 17"
    migrations = character_migrations()
    step = function_body(migrations, "bool Migrate_16_to_17(")
    assert 'Into->SetArrayField(TEXT("RememberedDefaults"), Held);' in step, (
        "the character record's Migrate_16_to_17 does not write RememberedDefaults")
    assert 'TryGetObjectField(TEXT("ConsumptionBuild"), Build)' in step, (
        "the character record's Migrate_16_to_17 does not write the held build's RememberedDefaults")
    assert '{ 16, TEXT("Migrate_16_to_17"), &Migrate_16_to_17 },' in squeeze(migrations), (
        "the character record's chain does not hold Migrate_16_to_17")


def test_the_committed_file_is_version_sixteens_with_one_remembered_default() -> None:
    old = json.loads((FIXTURES / "Character_v16.json").read_text(encoding="utf-8"))
    new = json.loads((FIXTURES / "Character_v17.json").read_text(encoding="utf-8"))
    assert new["SchemaVersion"] == 17
    assert new["RememberedDefaults"] == [{"Slot": "Special", "SkillRow": "War_Greatsword_Special"}], (
        "Character_v17.json does not hold the one remembered default the README states; an empty list is the default "
        "and would not show the field is read")
    # AND ONE IN THE HELD BUILD, which the double copies (ruled by the coordinating session on 2026-11-24).
    assert new["ConsumptionBuild"]["RememberedDefaults"] == [{"Slot": "Support", "SkillRow": "War_Greatsword_Support"}], (
        "Character_v17.json's held build does not hold the one remembered default the README states")
    build = {key: value for key, value in new["ConsumptionBuild"].items() if key != "RememberedDefaults"}
    assert build == old["ConsumptionBuild"], "Character_v17.json's held build changed a field the step does not write"
    rest = {key: value for key, value in new.items()
            if key not in ("SchemaVersion", "RememberedDefaults", "ConsumptionBuild")}
    assert rest == {key: value for key, value in old.items() if key not in ("SchemaVersion", "ConsumptionBuild")}, (
        "Character_v17.json changed a field the step does not write")
    assert "| `Character_v17.json` |" in (FIXTURES / "README.md").read_text(encoding="utf-8")


def test_a_load_puts_the_choices_back_before_its_one_fill() -> None:
    assert "Record.RememberedDefaults = State->GetRememberedDefaults();" in read_code(GATHER), (
        "the gather does not write the remembered defaults")
    apply = function_body(read_code(APPLY), "bool FCataclysmSaveApply::CharacterInto(")
    restore = "State->RestoreSkillChoices(Record.SkillAssignment, Record.RememberedDefaults,"
    announce = "Equipment->AnnounceRestored();"
    assert restore in apply, "CharacterInto does not put the choices and the defaults back through RestoreSkillChoices"
    assert apply.index(restore) < apply.index(announce), (
        "CharacterInto puts the choices back after its one announcement, so the fill that announcement makes reads none")
    assert "AssignSkill(" not in apply, (
        "CharacterInto still uses AssignSkill, which checks a choice against the pool worn now and drops one whose "
        "weapon is off (the owner's Q7 of 2026-11-24)")


def test_the_pawn_hands_the_defaults_over_and_remembers_them() -> None:
    fill = function_body(read_code(PAWN), "void ACataclysmPlayerCharacter::FillAbilitySlotsFromWornWeapon(")
    hand = "WeaponSlots->UseRememberedDefaults(Chooser ? Chooser->GetRememberedDefaults()"
    equip = "WeaponSlots->EquipWeaponTypes(WornTypes);"
    remember = "Chooser->RememberDefaults(WeaponSlots->GetAvailableSkills());"
    for part in (hand, equip, remember):
        assert part in fill, f"FillAbilitySlotsFromWornWeapon no longer does {part!r}"
    assert fill.index(hand) < fill.index(equip) < fill.index(remember), (
        "the defaults must be handed over before the fill and remembered after it")
    assert "Chooser->ForgetRememberedDefaults();" in fill, "no weapon worn no longer forgets the remembered defaults"
    assert "FillWithChoices(SkillPool, SkillChoices, RememberedDefaults)" in read_code(SLOTS), (
        "the weapon slots do not hand the remembered defaults to FillWithChoices")


def test_a_double_holds_the_keys_the_player_had() -> None:
    capture = function_body(read_code(BUILD), "FCataclysmPlayerBuild FCataclysmPlayerBuild::Of(")
    assert "Build.RememberedDefaults = State->GetRememberedDefaults();" in capture, (
        "a captured build does not copy the remembered defaults, so a double holds a fresh fill, not the player's keys")
    wear = function_body(read_code(DOUBLE), "bool ACataclysmConsumptionDoubleCharacter::WearThePlayersBuild(")
    assert "HeldBuild.SkillAssignment, HeldBuild.RememberedDefaults);" in wear, (
        "the double's fill does not read the held build's remembered defaults")
