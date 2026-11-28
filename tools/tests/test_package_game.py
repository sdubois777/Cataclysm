"""tools/package_game.py: the command it runs, when it refuses, the swap, and the cook list.

WHY THIS EXISTS. A packaged build is how the project owner plays without an
open editor holding every build on the machine. Four things about it can go
wrong without anything saying so, and each has a test here:

  - the command line drifts from what was approved (Win64, Development, no
    timeout, archived into a staging folder);
  - it starts while a session holds the Unreal lock or an editor is open, and
    collides with a test run;
  - a failed run deletes the package the owner was playing, or a good run
    deletes the owner's saves, which a Development build keeps inside the
    package folder;
  - a path the game loads by text -- a creature's mesh, a weapon, a screen --
    falls outside the folders game/Config/DefaultGame.ini cooks, and that
    creature is invisible in the package while every editor test passes.

NOTHING HERE TOUCHES THE REAL LOCK, THE REAL PACKAGE OR THE ENGINE. The lock is
a file under the test's own temporary folder, the process list is a stub, and
the BuildCookRun run is a function that writes a pretend .exe.
"""

from __future__ import annotations

import inspect
import json
import os
import pathlib
import re

import package_game
import pytest
import unreal_lock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_DIR = REPO_ROOT / "game"


# --------------------------------------------------------------------------
# A pretend checkout: one source file that loads one mesh by text, a config
# that cooks its folder and one map, and the content on disk.
# --------------------------------------------------------------------------

PACKAGING_INI = """\
[/Script/EngineSettings.GeneralProjectSettings]
ProjectName=Cataclysm

[/Script/UnrealEd.ProjectPackagingSettings]
+MapsToCook=(FilePath="/Game/Maps/L_Test")
+DirectoriesToAlwaysCook=(Path="/Game/Art/Mesh")
"""


@pytest.fixture
def checkout(tmp_path):
    root = tmp_path / "checkout"
    game = root / "game"
    (game / "Source" / "Cataclysm").mkdir(parents=True)
    (game / "Source" / "Cataclysm" / "Thing.cpp").write_text(
        'const TCHAR* Path = TEXT("/Game/Art/Mesh/SK_Thing.SK_Thing");\n', encoding="utf-8")
    (game / "Config").mkdir()
    (game / "Config" / "DefaultGame.ini").write_text(PACKAGING_INI, encoding="utf-8")
    (game / "Data").mkdir()
    (game / "Content" / "Art" / "Mesh").mkdir(parents=True)
    (game / "Content" / "Maps").mkdir(parents=True)
    (game / "Content" / "Maps" / "L_Test.umap").write_bytes(b"")
    (game / "Cataclysm.uproject").write_text("{}", encoding="utf-8")
    return root


@pytest.fixture
def lock(tmp_path):
    """A lock file of this test's own. The real one is never read or written."""
    return tmp_path / "unreal-editor.lock"


@pytest.fixture
def engine(tmp_path, monkeypatch):
    """A RunUAT.bat that exists, so the script gets past its engine check."""
    run_uat = tmp_path / "engine" / "RunUAT.bat"
    run_uat.parent.mkdir()
    run_uat.write_text("", encoding="utf-8")
    monkeypatch.setattr(package_game, "RUN_UAT", run_uat)
    return run_uat


def staging_of(command):
    """The archive folder a command names."""
    (found,) = [arg for arg in command if arg.startswith("-archivedirectory=")]
    return pathlib.Path(found.split("=", 1)[1])


def a_build_that_succeeds(content="new", seen=None):
    """A stand-in for BuildCookRun that archives a pretend game and exits 0."""
    def run(command, log_file, clock, cwd):
        exe = staging_of(command) / package_game.GAME_EXE
        exe.parent.mkdir(parents=True, exist_ok=True)
        exe.write_text(content, encoding="utf-8")
        if seen is not None:
            seen.append(command)
        return 0, "AutomationTool exiting with ExitCode=0 (Success)"
    return run


def never_run(command, log_file, clock, cwd):
    raise AssertionError("BuildCookRun was started when it should have been refused")


def no_editor():
    return set()


def an_old_package(root, content="old"):
    package = root / package_game.PACKAGE_NAME
    exe = package / package_game.GAME_EXE
    exe.parent.mkdir(parents=True)
    exe.write_text(content, encoding="utf-8")
    saves = package / package_game.SAVED_FOLDER / "SaveGames"
    saves.mkdir(parents=True)
    (saves / "Character_1.sav").write_text("my character", encoding="utf-8")
    return package


# --------------------------------------------------------------------------
# The command line.
# --------------------------------------------------------------------------

def test_the_command_is_buildcookrun_for_a_win64_development_package(tmp_path):
    project = tmp_path / "game" / "Cataclysm.uproject"
    staging = tmp_path / "Packaged.staging"
    run_uat = tmp_path / "RunUAT.bat"
    assert package_game.uat_command(project, staging, run_uat) == [
        str(run_uat),
        "BuildCookRun",
        f"-project={project}",
        "-platform=Win64",
        "-clientconfig=Development",
        "-build",
        "-cook",
        "-stage",
        "-pak",
        "-archive",
        f"-archivedirectory={staging}",
        "-nop4",
        "-utf8output",
        "-ubtargs=-WaitMutex",
    ]


def test_the_command_runs_the_engines_runuat_by_default(tmp_path):
    command = package_game.uat_command(tmp_path / "a.uproject", tmp_path / "s")
    assert command[0] == str(package_game.RUN_UAT)
    assert package_game.RUN_UAT.name == "RunUAT.bat"


def test_the_build_is_run_with_no_timeout():
    """CLAUDE.md: never put a timeout on an Unreal build. Killing the wrapper
    leaves Unreal Build Accelerator's workers holding the build mutex."""
    for function in (package_game.run_uat, package_game.package_and_swap):
        assert not re.search(r"\btimeout\s*=", inspect.getsource(function))
    assert "process.wait()" in inspect.getsource(package_game.run_uat)


def test_the_staging_folder_does_not_look_like_a_platform_folder():
    """UAT archives straight into a folder whose path has a component starting
    with "Windows" or "Win64", instead of into `<folder>/Windows`, and the .exe
    would then not be where the script looks for it (DeploymentContext.cs)."""
    for name in (package_game.PACKAGE_NAME, package_game.STAGING_NAME,
                 package_game.PREVIOUS_NAME):
        assert not name.lower().startswith(("windows", "win64"))


def test_a_dry_run_prints_the_command_and_runs_nothing(checkout, lock, capsys):
    code = package_game.main(["--dry-run", "--checkout", str(checkout)],
                             lock_file=lock, processes=no_editor, run=never_run)
    printed = capsys.readouterr().out
    assert code == 0
    assert "BuildCookRun" in printed
    assert "-archivedirectory=" + str((checkout / "Packaged.staging").resolve()) in printed
    assert str(checkout.resolve() / "Packaged" / "Windows" / "Cataclysm.exe") in printed
    assert not lock.exists()


# --------------------------------------------------------------------------
# The refusals.
# --------------------------------------------------------------------------

def test_it_refuses_while_another_session_holds_the_lock(checkout, lock, engine, capsys):
    assert unreal_lock.acquire("someone-else", lock)[0]
    code = package_game.main(["--checkout", str(checkout)],
                             lock_file=lock, processes=no_editor, run=never_run)
    assert code != 0
    assert "someone-else" in capsys.readouterr().out
    assert unreal_lock.read_holder(lock)["holder"] == "someone-else"


@pytest.mark.parametrize("process", ["UnrealEditor.exe", "UnrealEditor-Cmd.exe"])
def test_it_refuses_while_an_editor_is_open(checkout, lock, engine, capsys, process):
    code = package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=lambda: {process.lower()}, run=never_run)
    assert code != 0
    assert process in capsys.readouterr().out
    assert not lock.exists(), "a refused run must not take the lock"


def test_the_process_check_reads_names_without_regard_to_case():
    assert package_game.refusal(pathlib.Path("no-such.lock"), {"unrealeditor.exe"})
    assert package_game.refusal(pathlib.Path("no-such.lock"), {"notepad.exe"}) is None


def test_it_takes_the_lock_as_package_and_releases_it_afterwards(checkout, lock, engine):
    holders = []

    def run(command, log_file, clock, cwd):
        holders.append(unreal_lock.read_holder(lock)["holder"])
        return a_build_that_succeeds()(command, log_file, clock, cwd)

    assert package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=run) == 0
    assert holders == ["package"]
    assert not lock.exists()


def test_it_releases_the_lock_when_the_build_raises(checkout, lock, engine):
    def run(command, log_file, clock, cwd):
        raise KeyboardInterrupt

    with pytest.raises(KeyboardInterrupt):
        package_game.main(["--checkout", str(checkout)], lock_file=lock,
                          processes=no_editor, run=run)
    assert not lock.exists()


# --------------------------------------------------------------------------
# The swap.
# --------------------------------------------------------------------------

def test_a_good_run_replaces_the_package_and_keeps_the_saves(checkout, lock, engine):
    package = an_old_package(checkout)
    assert package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=a_build_that_succeeds("new")) == 0
    assert (package / package_game.GAME_EXE).read_text(encoding="utf-8") == "new"
    save = package / package_game.SAVED_FOLDER / "SaveGames" / "Character_1.sav"
    assert save.read_text(encoding="utf-8") == "my character"
    assert not (checkout / package_game.STAGING_NAME).exists()
    assert not (checkout / package_game.PREVIOUS_NAME).exists()


def test_a_first_run_with_no_package_before_it_puts_one_in_place(checkout, lock, engine):
    assert package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=a_build_that_succeeds("new")) == 0
    exe = checkout / package_game.PACKAGE_NAME / package_game.GAME_EXE
    assert exe.read_text(encoding="utf-8") == "new"


def test_a_failed_run_leaves_the_old_package_playable(checkout, lock, engine):
    package = an_old_package(checkout)

    def fails(command, log_file, clock, cwd):
        partial = staging_of(command) / "Windows"
        partial.mkdir(parents=True)  # a cook that died half way through
        return 25, "ERROR: Cook failed."

    assert package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=fails) == 25
    assert (package / package_game.GAME_EXE).read_text(encoding="utf-8") == "old"
    assert (package / package_game.SAVED_FOLDER / "SaveGames" / "Character_1.sav").is_file()


def test_a_run_that_exits_0_with_no_exe_is_a_failure(checkout, lock, engine):
    package = an_old_package(checkout)
    code = package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=lambda *a: (0, "done"))
    assert code != 0
    assert (package / package_game.GAME_EXE).read_text(encoding="utf-8") == "old"


def test_a_refused_swap_puts_the_old_package_and_its_saves_back(tmp_path):
    """Windows refuses to rename a folder with an open file in it: the game,
    still running. The old package must come back whole."""
    package = an_old_package(tmp_path)
    staging = tmp_path / package_game.STAGING_NAME
    (staging / package_game.GAME_EXE).parent.mkdir(parents=True)
    (staging / package_game.GAME_EXE).write_text("new", encoding="utf-8")
    previous = tmp_path / package_game.PREVIOUS_NAME

    def rename(source, target):
        if source == staging:
            raise PermissionError("The process cannot access the file")
        os.replace(source, target)

    with pytest.raises(package_game.SwapRefused):
        package_game.swap_in(staging, package, previous, rename=rename)
    assert (package / package_game.GAME_EXE).read_text(encoding="utf-8") == "old"
    assert (package / package_game.SAVED_FOLDER / "SaveGames" / "Character_1.sav").is_file()
    assert (staging / package_game.GAME_EXE).read_text(encoding="utf-8") == "new"


def test_swap_only_finishes_a_refused_swap(checkout, lock, capsys):
    package = an_old_package(checkout)
    staging = checkout / package_game.STAGING_NAME
    (staging / package_game.GAME_EXE).parent.mkdir(parents=True)
    (staging / package_game.GAME_EXE).write_text("new", encoding="utf-8")
    assert package_game.main(["--swap-only", "--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=never_run) == 0
    assert (package / package_game.GAME_EXE).read_text(encoding="utf-8") == "new"
    assert (package / package_game.SAVED_FOLDER / "SaveGames" / "Character_1.sav").is_file()


def test_the_stage_clock_reads_uats_banners():
    clock = package_game.StageClock()
    clock.read("********** COOK COMMAND STARTED **********", 10.0)
    clock.read("LogCook: Display: Cooked packages 120", 20.0)
    clock.read("********** COOK COMMAND COMPLETED **********", 75.0)
    clock.read("********** STAGE COMMAND STARTED **********", 80.0)
    clock.read("********** STAGE COMMAND FAILED **********", 81.0)
    assert clock.took == {"Cook": 65.0, "Stage": 1.0}
    assert clock.lines() == ["  Cook     1m 05s", "  Stage    0m 01s (failed)"]


# --------------------------------------------------------------------------
# The sweep and the cook list.
# --------------------------------------------------------------------------

def test_adjacent_literals_are_joined_and_comments_are_skipped():
    text = (
        '// TEXT("/Game/Commented/Out.Out")\n'
        '/* "/Game/Also/Out" */\n'
        'const TCHAR* A = TEXT("/Game/Pack/Animations/"\n'
        '                      "Idle.Idle");\n'
        "const float B = 1'200.0f; const TCHAR* C = TEXT(\"/Game/After/Separator\");\n"
    )
    assert package_game.content_paths_in(text) == [
        (3, "/Game/Pack/Animations/Idle.Idle"),
        (5, "/Game/After/Separator"),
    ]


def test_a_package_is_named_without_its_object_or_class_suffix():
    assert package_game.package_of("/Game/A/B.B_C") == "/Game/A/B"
    assert package_game.package_of("/Game/A/B.B") == "/Game/A/B"
    assert package_game.package_of("/Game/A/Folder/") == "/Game/A/Folder"
    assert package_game.package_of("/Game/A/Folder") == "/Game/A/Folder"


def test_only_the_packaging_section_is_read():
    settings = package_game.packaging_settings(
        '[/Script/Other]\n+DirectoriesToAlwaysCook=(Path="/Game/Wrong")\n'
        + PACKAGING_INI)
    assert settings.directories == ("/Game/Art/Mesh",)
    assert settings.maps == ("/Game/Maps/L_Test",)


def test_a_folder_covers_what_is_inside_it_and_not_its_neighbours():
    settings = package_game.PackagingSettings(("/Game/Art/Mesh",), ("/Game/Maps/L_Test",))
    assert package_game.is_cooked("/Game/Art/Mesh/SK_A.SK_A", settings)
    assert package_game.is_cooked("/Game/Art/Mesh/Sub/SK_B", settings)
    assert package_game.is_cooked("/Game/Art/Mesh", settings)
    assert package_game.is_cooked("/Game/Maps/L_Test.L_Test", settings)
    assert not package_game.is_cooked("/Game/Art/MeshExtra/SK_C", settings)
    assert not package_game.is_cooked("/Game/Art/Animations/Idle", settings)
    assert not package_game.is_cooked("/Game/Maps/L_Other", settings)


def test_the_sweep_leaves_out_tests_and_the_editor_module(checkout):
    source = checkout / "game" / "Source"
    for folder in (source / "Cataclysm" / "Tests", source / "CataclysmEditor"):
        folder.mkdir(parents=True)
        (folder / "X.cpp").write_text('auto P = TEXT("/Game/Elsewhere/X");\n', encoding="utf-8")
    assert package_game.uncovered_paths(checkout / "game") == {}


def test_a_path_outside_the_cooked_folders_is_named_with_where_it_is(checkout):
    data = checkout / "game" / "Data" / "WeaponMeshes.csv"
    data.write_text("Name,Mesh\nWeapon_Axe,/Game/Weapons/SM_Axe\n", encoding="utf-8")
    assert package_game.uncovered_paths(checkout / "game") == {
        "/Game/Weapons/SM_Axe": ["Data/WeaponMeshes.csv:2"]}


def test_a_misspelt_cooked_folder_or_map_is_caught(checkout, tmp_path):
    settings = package_game.PackagingSettings(
        ("/Game/Art/Mesh", "/Game/Art/Mesh_Typo", "/Engine/BasicShapes"),
        ("/Game/Maps/L_Test", "/Game/Maps/L_Gone"))
    engine_content = tmp_path / "EngineContent"
    (engine_content / "BasicShapes").mkdir(parents=True)
    game = checkout / "game"
    assert package_game.missing_directories(game, settings, engine_content) == [
        "/Game/Art/Mesh_Typo"]
    assert package_game.missing_maps(game, settings) == ["/Game/Maps/L_Gone"]


def test_it_refuses_to_package_a_cook_list_that_misses_a_path(checkout, lock, engine, capsys):
    (checkout / "game" / "Source" / "Cataclysm" / "New.cpp").write_text(
        'auto P = TEXT("/Game/NewPack/SK_New.SK_New");\n', encoding="utf-8")
    code = package_game.main(["--checkout", str(checkout)], lock_file=lock,
                             processes=no_editor, run=never_run)
    assert code != 0
    assert "/Game/NewPack/SK_New.SK_New" in capsys.readouterr().out
    assert not lock.exists()


# --------------------------------------------------------------------------
# The real game: every path it loads by text is cooked.
# --------------------------------------------------------------------------

def test_the_sweep_finds_the_paths_it_exists_to_find():
    """A sweep that found nothing would pass the check below vacuously. These
    are one of each kind: a C++ path split over two lines, a Death creature's
    mesh, a weapon from the data, a screen, a map the game travels to, and a
    path from the config."""
    found = {package_game.package_of(path) for path in package_game.swept_paths(GAME_DIR)}
    for expected in (
        "/Game/ParagonGrux/Characters/Heroes/Grux/Animations/Idle",
        "/Game/Wraith/Mesh/SK_Wraith",
        "/Game/Medieval_Weapons/Meshes/VOL2/SM_Sword_1",
        "/Game/Interface/WBP_CharacterSelect",
        "/Game/Maps/L_Dungeon",
        "/Game/Input/IMC_KeyboardMovement",
        "/Game/Data/DT_WeaponSkills",
    ):
        assert expected in found, f"the sweep no longer finds {expected}"


def test_every_path_the_game_loads_by_text_is_cooked():
    """THE GUARD. A path here that no cooked folder holds is an asset missing
    from the package: the creature it belongs to stands invisible, or a screen
    does not open, while every editor test passes. Add its folder to
    DirectoriesToAlwaysCook (or its map to MapsToCook) in
    game/Config/DefaultGame.ini."""
    uncovered = package_game.uncovered_paths(GAME_DIR)
    assert not uncovered, "Loaded by text but not cooked:\n" + "\n".join(
        f"  {path}  ({', '.join(where)})" for path, where in sorted(uncovered.items()))


def test_every_map_the_game_opens_is_cooked():
    settings = package_game.read_settings(GAME_DIR)
    engine_ini = (GAME_DIR / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    default_map = re.search(r"^GameDefaultMap=(\S+)", engine_ini, re.M).group(1)
    assert package_game.package_of(default_map) in settings.maps
    on_disk = {"/Game/Maps/" + path.stem for path in (GAME_DIR / "Content" / "Maps").glob("*.umap")}
    assert on_disk <= set(settings.maps), "a map in game/Content/Maps is not in MapsToCook"


def test_the_editor_module_is_not_in_the_game_target():
    target = (GAME_DIR / "Source" / "Cataclysm.Target.cs").read_text(encoding="utf-8")
    modules = re.search(r"ExtraModuleNames\.AddRange\(new string\[\] \{([^}]*)\}", target).group(1)
    assert "CataclysmEditor" not in modules
    assert "TargetType.Game" in target
    project = json.loads((GAME_DIR / "Cataclysm.uproject").read_text(encoding="utf-8"))
    (editor,) = [m for m in project["Modules"] if m["Name"] == "CataclysmEditor"]
    assert editor["Type"] == "Editor"
