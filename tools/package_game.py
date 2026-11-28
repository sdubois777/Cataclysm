"""Cook and package the game into a playable Win64 Development build.

WHY THIS EXISTS. The project owner plays the game. An open editor blocks every
Unreal build on this machine, because Live Coding holds the binaries, so playing
in the editor holds up every test run until the editor is closed. A packaged
build is an ordinary .exe and holds nothing. The owner approved this on
2026-11-27.

    python tools/package_game.py              # cook, package, swap the new package in
    python tools/package_game.py --dry-run    # print the command and the checks, run nothing
    python tools/package_game.py --swap-only  # finish a run whose swap was refused

WHAT IT RUNS. The engine's RunUAT BuildCookRun, for the Cataclysm game target,
Win64, Development, so the ~ console and the project's console commands work.
NO TIMEOUT: CLAUDE.md forbids one on an Unreal build, because killing the
wrapper does not kill Unreal Build Accelerator's detached compile workers.

WHERE IT PACKAGES FROM, AND TO. Always from the MAIN checkout, the one git
names as the common directory's parent (C:\\Projects\\Cataclysm on the owner's
machine), whichever worktree this script is run from. The art packs (Paragon*,
Wraith, Lich, Medieval_Weapons and the rest) are gitignored and exist only
there; a worktree cooked would be a game with no creatures in it. The package
goes to `<main checkout>/Packaged`, and the game is `Packaged/Windows/Cataclysm.exe`.

A FAILED RUN KEEPS THE OLD PACKAGE. The new one is archived into
`Packaged.staging` and swapped in only after BuildCookRun exited 0 and the .exe
is there. The saves and the log live INSIDE the package for a Development build
(`Packaged/Windows/Cataclysm/Saved`; see game/README.md for the engine source
that says so), so the swap carries the old package's `Saved` folder into the new
one. Without that, every repackage would delete the owner's characters.

WHAT IS COOKED. The cooker cooks the maps it is told to and what they reference.
This game loads most of its art by a path written as text -- `TEXT("/Game/Wraith/
Mesh/SK_Wraith.SK_Wraith")` in C++, `/Game/Medieval_Weapons/...` in
game/Data/WeaponMeshes.csv -- and the cooker cannot follow text. So
game/Config/DefaultGame.ini lists the folders those paths point into, under
`DirectoriesToAlwaysCook`, and the maps the game opens under `MapsToCook`.
`uncovered_paths` sweeps the source, the data and the config for every such
path and names any that falls outside them; tools/tests/test_package_game.py
fails on one, and this script refuses to start on one. Only the folders used are
listed, not whole packs: the packs hold about 54 GB.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
from collections.abc import Callable, Iterable, Sequence

import unreal_build
import unreal_lock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The name the lock is taken under while a package runs.
LOCK_HOLDER = "package"

#: The folders, beside the game folder in the main checkout. The staging and
#: previous folders must not have a path component starting with "Windows" or
#: "Win64": UAT's DeploymentContext then archives straight into the folder named
#: instead of into `<folder>/Windows`, and the .exe is not where this looks.
PACKAGE_NAME = "Packaged"
STAGING_NAME = "Packaged.staging"
PREVIOUS_NAME = "Packaged.previous"

#: Where, inside a package, the game's .exe and its Saved folder are.
#: `Windows` is WinPlatform.Automation.cs's cook platform for a game target;
#: the launcher .exe is named after the target, Cataclysm.
PLATFORM_FOLDER = "Windows"
GAME_EXE = pathlib.PurePath(PLATFORM_FOLDER, "Cataclysm.exe")
SAVED_FOLDER = pathlib.PurePath(PLATFORM_FOLDER, "Cataclysm", "Saved")

#: Processes that mean an editor is open: the interactive editor, and the
#: command-line one an automation run or a commandlet uses.
EDITOR_PROCESSES = ("UnrealEditor.exe", "UnrealEditor-Cmd.exe")

RUN_UAT = unreal_build.ENGINE_ROOT / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"

#: The ini section and keys the cook reads.
PACKAGING_SECTION = "/Script/UnrealEd.ProjectPackagingSettings"

#: UAT's banner as each step of BuildCookRun starts and ends, from the engine's
#: AutomationTool scripts: `********** COOK COMMAND STARTED **********`.
STAGE_BANNER = re.compile(r"\*{10} ([A-Z ]+?) COMMAND (STARTED|COMPLETED|FAILED) \*{10}")


# --------------------------------------------------------------------------
# The sweep: every content path the game names as text.
# --------------------------------------------------------------------------

#: A content path inside a string: `/Game/...` or `/Engine/...`.
CONTENT_PATH = re.compile(r"/(?:Game|Engine)/[A-Za-z0-9_./-]*")


def string_literals(text: str) -> list[tuple[int, str]]:
    """The C++ string literals in `text`, with comments skipped, as (line, value).

    ADJACENT LITERALS ARE JOINED, as the compiler joins them. The creatures
    spell their paths across two lines -- `TEXT("/Game/ParagonGrux/.../Animations/"
    "Idle.Idle")` -- and the folder alone is not the asset.

    COMMENTS ARE SKIPPED so a path a comment mentions is not taken for one the
    game loads. A digit separator (`1'200`) is not read as a character literal.
    Escapes are kept as written; no content path contains one.
    """
    found: list[tuple[int, str]] = []
    i, n, line = 0, len(text), 1
    pending: list[str] = []
    pending_line = 0

    def flush() -> None:
        nonlocal pending
        if pending:
            found.append((pending_line, "".join(pending)))
            pending = []

    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
            continue
        raw = re.match(r'R"([^(\s]*)\(', text[i:i + 20]) if c == "R" else None
        if raw and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            close = ")" + raw.group(1) + '"'
            start = i + raw.end()
            end = text.find(close, start)
            end = n if end < 0 else end
            if not pending:
                pending_line = line
            pending.append(text[start:end])
            line += text.count("\n", i, end)
            i = end + len(close)
            continue
        if c == '"':
            if not pending:
                pending_line = line
            j = i + 1
            while j < n and text[j] != '"' and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            pending.append(text[i + 1:j])
            i = j + 1
            continue
        if c == "'" and not (i > 0 and text[i - 1].isalnum()):
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            flush()
            i = j + 1
            continue
        # Anything else ends a run of adjacent literals.
        flush()
        i += 1
    flush()
    return found


def content_paths_in(text: str) -> list[tuple[int, str]]:
    """Every content path inside a string literal of this C++ text."""
    return [(line, match.group(0))
            for line, literal in string_literals(text)
            for match in CONTENT_PATH.finditer(literal)]


def content_paths_in_plain_text(text: str) -> list[tuple[int, str]]:
    """Every content path in a data or config file. Only an ini comment line,
    one opening with `;`, is skipped."""
    found = []
    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith(";"):
            continue  # an ini comment
        found.extend((number, match.group(0)) for match in CONTENT_PATH.finditer(line))
    return found


def is_runtime_source(path: pathlib.Path, source_root: pathlib.Path) -> bool:
    """Whether a source file goes into the packaged game: not a test, and not the
    editor-only module, which the game target (Cataclysm.Target.cs) leaves out."""
    parts = path.relative_to(source_root).parts
    return "Tests" not in parts and (not parts or parts[0] != "CataclysmEditor")


def swept_paths(game_dir: pathlib.Path) -> dict[str, list[str]]:
    """Every content path the game names as text, with where each is named.

    Read from game/Source (less the tests and the editor module), game/Data and
    game/Config (less the editor's own ini files). The key is the path as
    written; the value is `file:line` for each place it appears, relative to
    `game_dir`.
    """
    found: dict[str, list[str]] = {}

    def add(path: pathlib.Path, pairs: Iterable[tuple[int, str]]) -> None:
        where = path.relative_to(game_dir).as_posix()
        for line, content_path in pairs:
            found.setdefault(content_path, []).append(f"{where}:{line}")

    source_root = game_dir / "Source"
    for path in sorted(source_root.rglob("*")):
        if path.suffix in (".cpp", ".h") and is_runtime_source(path, source_root):
            add(path, content_paths_in(path.read_text(encoding="utf-8", errors="replace")))
    for folder, pattern in ((game_dir / "Data", "*"), (game_dir / "Config", "*.ini")):
        for path in sorted(folder.glob(pattern)):
            # DefaultEditor*.ini is the editor's own settings, which a game never
            # reads: its asset viewer's floor and sky are editor content.
            if path.suffix == ".ini" and "Editor" in path.name:
                continue
            if path.is_file() and path.suffix in (".csv", ".json", ".ini"):
                add(path, content_paths_in_plain_text(
                    path.read_text(encoding="utf-8", errors="replace")))
    return found


def package_of(content_path: str) -> str:
    """The package or folder a content path names: `/Game/A/B.B_C` is `/Game/A/B`,
    and a trailing slash or dot is dropped, so a folder prefix names the folder."""
    path = content_path.rstrip("/.")
    head, _, last = path.rpartition("/")
    if "." in last:
        last = last.split(".", 1)[0]
    return f"{head}/{last}" if head else path


# --------------------------------------------------------------------------
# What the config cooks.
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class PackagingSettings:
    """`DirectoriesToAlwaysCook` and `MapsToCook` from DefaultGame.ini."""
    directories: tuple[str, ...]
    maps: tuple[str, ...]


DIRECTORY_ENTRY = re.compile(r'^\+DirectoriesToAlwaysCook=\(Path="([^"]+)"\)\s*$')
MAP_ENTRY = re.compile(r'^\+MapsToCook=\(FilePath="([^"]+)"\)\s*$')


def packaging_settings(ini_text: str) -> PackagingSettings:
    """Read the packaging section's two lists. Only lines inside
    `[/Script/UnrealEd.ProjectPackagingSettings]` count."""
    directories: list[str] = []
    maps: list[str] = []
    section = None
    for raw in ini_text.splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section != PACKAGING_SECTION:
            continue
        if found := DIRECTORY_ENTRY.match(line):
            directories.append(found.group(1).rstrip("/"))
        elif found := MAP_ENTRY.match(line):
            maps.append(package_of(found.group(1)))
    return PackagingSettings(tuple(directories), tuple(maps))


def is_cooked(content_path: str, settings: PackagingSettings) -> bool:
    """Whether the cook includes what this path names: a map in `MapsToCook`, or
    anything inside, or equal to, a directory in `DirectoriesToAlwaysCook`."""
    package = package_of(content_path)
    if package in settings.maps:
        return True
    return any(package == folder or package.startswith(folder + "/")
               for folder in settings.directories)


def uncovered_paths(game_dir: pathlib.Path,
                    settings: PackagingSettings | None = None) -> dict[str, list[str]]:
    """Every swept path the config does not cook, with where each is named."""
    if settings is None:
        settings = read_settings(game_dir)
    return {path: where for path, where in swept_paths(game_dir).items()
            if not is_cooked(path, settings)}


def read_settings(game_dir: pathlib.Path) -> PackagingSettings:
    return packaging_settings(
        (game_dir / "Config" / "DefaultGame.ini").read_text(encoding="utf-8"))


def missing_directories(game_dir: pathlib.Path, settings: PackagingSettings,
                        engine_content: pathlib.Path | None = None) -> list[str]:
    """Directories the config cooks that do not exist on disk.

    THE COOKER ONLY WARNS about one, and cooks nothing from it, so a misspelt
    folder ships a game with that creature invisible. Checked against the main
    checkout's Content, which is the only place the art exists.
    """
    if engine_content is None:
        engine_content = unreal_build.ENGINE_ROOT / "Engine" / "Content"
    roots = {"Game": game_dir / "Content", "Engine": engine_content}
    missing = []
    for folder in settings.directories:
        _, root, *rest = folder.split("/")
        base = roots.get(root)
        if base is None or not base.joinpath(*rest).is_dir():
            missing.append(folder)
    return missing


def missing_maps(game_dir: pathlib.Path, settings: PackagingSettings) -> list[str]:
    """Maps in `MapsToCook` with no .umap in the main checkout's Content."""
    content = game_dir / "Content"
    return [game_map for game_map in settings.maps
            if not content.joinpath(*game_map.split("/")[2:]).with_suffix(".umap").is_file()]


# --------------------------------------------------------------------------
# The command.
# --------------------------------------------------------------------------

def uat_command(project_file: pathlib.Path, staging: pathlib.Path,
                run_uat: pathlib.Path | None = None) -> list[str]:
    """The BuildCookRun command line.

    `-ubtargs=-WaitMutex` makes the builds UAT starts WAIT for another session's
    build rather than fail, as `tools/unreal_build.py` does with `-WaitMutex`;
    UnrealBuildTool otherwise aborts at once when the mutex is held.
    """
    return [
        str(run_uat or RUN_UAT),
        "BuildCookRun",
        f"-project={project_file}",
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


def main_checkout(repo_root: pathlib.Path = REPO_ROOT) -> pathlib.Path:
    """The main checkout: the parent of git's common directory, which every
    worktree reports alike."""
    common = subprocess.run(
        ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
        capture_output=True, text=True, check=True, cwd=repo_root).stdout.strip()
    return pathlib.Path(common).resolve().parent


# --------------------------------------------------------------------------
# The refusals.
# --------------------------------------------------------------------------

def running_processes() -> set[str]:
    """The image names of every running process, lower-cased. Windows only."""
    listing = subprocess.run(["tasklist", "/fo", "csv", "/nh"],
                             capture_output=True, text=True, check=False).stdout
    return {line.split('","', 1)[0].strip('"').lower()
            for line in listing.splitlines() if line.startswith('"')}


def refusal(lock_file: pathlib.Path, processes: set[str]) -> str | None:
    """Why a package must not start now, or None.

    The lock first, because the lock says who: an automation run holds it and
    also runs UnrealEditor-Cmd.exe, and naming the session is more useful than
    naming the process.
    """
    holder = unreal_lock.read_holder(lock_file)
    if holder is not None:
        return ("Refusing to package: the Unreal lock is held by {} since {}. Wait for them to "
                "release it.".format(holder.get("holder", "unknown"),
                                     holder.get("taken") or "an unrecorded time"))
    open_editors = [name for name in EDITOR_PROCESSES if name.lower() in processes]
    if open_editors:
        return ("Refusing to package: {} is running. Close the editor first; an open editor "
                "holds the binaries the build must write.".format(" and ".join(open_editors)))
    return None


# --------------------------------------------------------------------------
# The swap.
# --------------------------------------------------------------------------

class SwapRefused(RuntimeError):
    """The new package could not be moved into place. The old one is where it
    was, and the new one is still in the staging folder."""


def swap_in(staging: pathlib.Path, package: pathlib.Path, previous: pathlib.Path,
            rename: Callable[[pathlib.Path, pathlib.Path], object] = os.replace,
            remove_tree: Callable[[pathlib.Path], object] = shutil.rmtree) -> bool:
    """Put the staged package where the old one was, carrying the old saves across.

    Returns whether saves were carried. Raises `SwapRefused` -- with the old
    package back in place -- when a rename is refused, which on Windows means a
    file in it is open: usually the game, still running.
    """
    if previous.exists():
        remove_tree(previous)
    had_package = package.exists()
    if had_package:
        try:
            rename(package, previous)
        except OSError as error:
            raise SwapRefused(f"could not move the old package aside ({error}). Is the game "
                              "still running?") from error
    carried = False
    old_saved = previous / SAVED_FOLDER
    new_saved = staging / SAVED_FOLDER
    try:
        if had_package and old_saved.is_dir() and not new_saved.exists():
            new_saved.parent.mkdir(parents=True, exist_ok=True)
            rename(old_saved, new_saved)
            carried = True
        rename(staging, package)
    except OSError as error:
        # Put everything back the way it was.
        if carried and new_saved.exists():
            rename(new_saved, old_saved)
        if had_package and not package.exists():
            rename(previous, package)
        raise SwapRefused(f"could not move the new package into place ({error}).") from error
    if had_package:
        remove_tree(previous)
    return carried


def folder_size(folder: pathlib.Path) -> int:
    return sum(path.stat().st_size for path in folder.rglob("*") if path.is_file())


# --------------------------------------------------------------------------
# Running BuildCookRun.
# --------------------------------------------------------------------------

@dataclasses.dataclass
class StageClock:
    """The time each BuildCookRun step took, read from UAT's banners."""
    started: dict[str, float] = dataclasses.field(default_factory=dict)
    took: dict[str, float] = dataclasses.field(default_factory=dict)
    ended_how: dict[str, str] = dataclasses.field(default_factory=dict)

    def read(self, line: str, now: float) -> None:
        found = STAGE_BANNER.search(line)
        if not found:
            return
        stage, event = found.group(1).title(), found.group(2)
        if event == "STARTED":
            self.started[stage] = now
        elif stage in self.started:
            self.took[stage] = now - self.started[stage]
            self.ended_how[stage] = event.lower()

    def lines(self) -> list[str]:
        return [f"  {stage:<8} {format_seconds(seconds)}"
                + ("" if self.ended_how.get(stage) == "completed" else " (failed)")
                for stage, seconds in self.took.items()]


def format_seconds(seconds: float) -> str:
    minutes, rest = divmod(int(round(seconds)), 60)
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h {minutes:02d}m {rest:02d}s" if hours else f"{minutes}m {rest:02d}s"


def run_uat(command: Sequence[str], log_file: pathlib.Path, clock: StageClock,
            cwd: pathlib.Path) -> tuple[int, str]:
    """Run the command to the end, echoing and logging every line. No timeout."""
    log_file.parent.mkdir(parents=True, exist_ok=True)
    tail: list[str] = []
    with open(log_file, "w", encoding="utf-8") as log, subprocess.Popen(
            list(command), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=cwd,
            text=True, encoding="utf-8", errors="replace") as process:
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            log.write(line)
            clock.read(line, time.monotonic())
            tail.append(line)
            del tail[:-400]
        code = process.wait()
    return code, "".join(tail)


# --------------------------------------------------------------------------
# The entry point.
# --------------------------------------------------------------------------

def parse_arguments(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dry-run", action="store_true",
                        help="print the command and the checks, and run nothing")
    parser.add_argument("--swap-only", action="store_true",
                        help="move a finished Packaged.staging into place (after a refused swap)")
    parser.add_argument("--checkout", type=pathlib.Path, default=None,
                        help="the checkout to package (default: the main checkout)")
    return parser.parse_args(argv)


def preflight(game_dir: pathlib.Path) -> list[str]:
    """Problems with the cook list that would ship a game missing its art."""
    settings = read_settings(game_dir)
    problems = []
    for path, where in sorted(uncovered_paths(game_dir, settings).items()):
        problems.append(f"{path} is loaded by text ({where[0]}) but no cooked folder holds it.")
    for folder in missing_directories(game_dir, settings):
        problems.append(f"DirectoriesToAlwaysCook lists {folder}, which does not exist.")
    for game_map in missing_maps(game_dir, settings):
        problems.append(f"MapsToCook lists {game_map}, which has no .umap.")
    if not settings.maps:
        problems.append("DefaultGame.ini lists no MapsToCook.")
    return problems


def main(argv: Sequence[str] | None = None,
         *,
         lock_file: pathlib.Path | None = None,
         processes: Callable[[], set[str]] = running_processes,
         run: Callable[[Sequence[str], pathlib.Path, StageClock, pathlib.Path],
                       tuple[int, str]] = run_uat) -> int:
    args = parse_arguments(argv)
    root = (args.checkout or main_checkout()).resolve()
    game_dir = root / "game"
    project_file = game_dir / "Cataclysm.uproject"
    package, staging, previous = (root / PACKAGE_NAME, root / STAGING_NAME,
                                  root / PREVIOUS_NAME)
    command = uat_command(project_file, staging)

    if args.dry_run:
        print("Would run, from", game_dir)
        print("  " + subprocess.list2cmdline(command))
        print(f"Package: {package / GAME_EXE}")
        problems = preflight(game_dir)
        print("Cook list: " + ("every path loaded by text is cooked." if not problems
                               else f"{len(problems)} problem(s):"))
        for problem in problems:
            print("  " + problem)
        return 0 if not problems else 1

    if args.swap_only:
        if not (staging / GAME_EXE).is_file():
            print(f"Nothing to swap: {staging / GAME_EXE} does not exist.")
            return 1
        try:
            carried = swap_in(staging, package, previous)
        except SwapRefused as error:
            print(f"Swap refused: {error}")
            return 1
        print(f"Swapped in. Saves carried across: {'yes' if carried else 'none to carry'}.")
        print(f"Game: {package / GAME_EXE}")
        return 0

    if lock_file is None:
        lock_file = unreal_lock.lock_path()
    reason = refusal(lock_file, processes())
    if reason:
        print(reason)
        return 1
    if not RUN_UAT.is_file():
        print(f"{RUN_UAT} does not exist. Set UE_ROOT to the engine installation directory.")
        return 1
    problems = preflight(game_dir)
    if problems:
        print("Refusing to package: the cook list would leave out art the game loads.")
        for problem in problems:
            print("  " + problem)
        return 1

    taken, message = unreal_lock.acquire(LOCK_HOLDER, lock_file)
    print(message)
    if not taken:
        return 1
    try:
        return package_and_swap(command, game_dir, package, staging, previous, run)
    finally:
        print(unreal_lock.release(LOCK_HOLDER, lock_file)[1])


def package_and_swap(command: Sequence[str], game_dir: pathlib.Path, package: pathlib.Path,
                     staging: pathlib.Path, previous: pathlib.Path,
                     run: Callable[[Sequence[str], pathlib.Path, StageClock, pathlib.Path],
                                   tuple[int, str]]) -> int:
    """Package into the staging folder and swap it in only if that succeeded."""
    if staging.exists():
        shutil.rmtree(staging)
    log_file = game_dir / "Saved" / "Logs" / "PackageGame.log"
    print("Running:", subprocess.list2cmdline(command), flush=True)

    began = time.monotonic()
    clock = StageClock()
    code, output = run(command, log_file, clock, game_dir)
    attempt = 1
    while (code != 0 and unreal_build.is_mutex_denial(output)
           and attempt < unreal_build.MUTEX_RETRY_ATTEMPTS):
        attempt += 1
        print(f"Package: the continuous integration runner holds the UnrealBuildTool mutex; "
              f"waiting {unreal_build.MUTEX_RETRY_WAIT_SECONDS:g} seconds and retrying "
              f"({attempt} of {unreal_build.MUTEX_RETRY_ATTEMPTS}).", flush=True)
        time.sleep(unreal_build.MUTEX_RETRY_WAIT_SECONDS)
        clock = StageClock()
        code, output = run(command, log_file, clock, game_dir)
    total = time.monotonic() - began

    print()
    print("Stages:")
    for line in clock.lines() or ["  (no stage banners were read)"]:
        print(line)
    print(f"  total    {format_seconds(total)}")
    print(f"Full output: {log_file}")

    exe = staging / GAME_EXE
    if code != 0 or not exe.is_file():
        why = (f"BuildCookRun exited {code}" if code != 0
               else f"BuildCookRun exited 0 but {exe} does not exist")
        print(f"PACKAGE FAILED: {why}. The previous package, if any, is untouched at {package}.")
        return 1 if code == 0 else code

    try:
        carried = swap_in(staging, package, previous)
    except SwapRefused as error:
        print(f"PACKAGE BUILT BUT NOT SWAPPED IN: {error} The new package is in {staging}; "
              "close the game and run python tools/package_game.py --swap-only.")
        return 1
    size = folder_size(package)
    print(f"Package: {size / 1024 ** 3:.2f} GB in {package}")
    print(f"Saves carried across from the previous package: {'yes' if carried else 'none'}")
    print(f"Game: {package / GAME_EXE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
