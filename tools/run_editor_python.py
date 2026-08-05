"""Run a Python script inside the Unreal editor, and say why when it cannot.

    python tools/run_editor_python.py tools/generate_datatable_assets.py

WHY THIS EXISTS. Issue #279. `tools/generate_datatable_assets.py` and
`tools/generate_input_assets.py` do not run in the system Python. They run inside
the editor's interpreter, launched through the `pythonscript` commandlet. Loading
this project needs its three compiled C++ modules in `game/Binaries/Win64/`, and
that directory is gitignored, so a **git worktree does not have one**.

Started against a worktree, the editor runs for about twenty seconds, prints an
ordinary-looking startup log ending in

    LogShaderCompilers: Display: Exiting ShaderCompilingThread

and exits. Nothing is written, nothing says the script did not run, and
`git status` shows no change. The CSV files then disagree with the DataTable
assets built from them, and the only thing that notices is
`tools/tests/test_datatable_assets_are_current.py`, which reports it as stale
assets rather than as a generator that never ran.

WHAT THIS DOES. Two checks either side of the editor.

**Before**: refuse to start unless the engine, the `.uproject` and the project's
compiled modules are all present. A missing `game/Binaries/Win64/` is reported as
what it is, and the message says outright when this checkout is a git worktree.

**After**: read the run's log and fail unless the Python interpreter actually
logged something. This is what catches the silent case: an editor that gave up
before reaching the Python plugin writes a log with no `LogPython` line in it at
all. The log is this runner's own file, `game/Saved/Logs/run_editor_python.log`,
not the editor's default `Cataclysm.log` — see RUN_LOG for why that matters.

    THE SCRIPT MUST LOG AT LEAST ONE LINE THROUGH `unreal.log`, or the run is
    reported as not having happened. Both generators log every asset they write,
    so this costs them nothing.

WHAT THIS DOES NOT DO. It does not make a worktree work. Sharing the ordinary
checkout's `game/Binaries/` through a junction would let the editor start, but
those binaries are built from `game/Source/`, and a worktree exists precisely to
hold a different version of that tree. The editor would then load C++ that does
not match the source beside it and report nothing. That is a worse silent failure
than the one this replaces, so the answer here is to name the cause and stop.
Run the generator in the ordinary checkout instead; `game/README.md` says how.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

from unreal_build import EDITOR_CMD, ENGINE_ROOT, GAME_DIR, PROJECT_FILE, REPO_ROOT

#: Where UnrealBuildTool puts this project's compiled editor modules. Gitignored,
#: because it is build output regenerated from `game/Source/`.
PROJECT_BINARIES = GAME_DIR / "Binaries" / "Win64"

#: This runner's own log, passed to the editor with `-abslog=`.
#:
#: NOT `Cataclysm.log`, THE DEFAULT, and it cannot be. An editor that is already
#: open holds that file, so deleting it first raises
#: `PermissionError: [WinError 32] The process cannot access the file because it
#: is being used by another process`, and reading it afterwards would give this
#: runner the interactive editor's output rather than the commandlet's. A
#: dedicated file avoids both. The generators are normally run with the editor
#: open, so this is the ordinary case and not an edge one.
RUN_LOG = GAME_DIR / "Saved" / "Logs" / "run_editor_python.log"

#: One editor module DLL per entry in the `.uproject`'s Modules list. Checking
#: for named files rather than for a non-empty directory matters: a worktree that
#: has had an editor pointed at it once has a `Binaries/Win64/` holding only
#: `UnrealEditor.modules` and a `D3D12/` folder, and that is not a built project.
REQUIRED_MODULE_LIBRARIES = (
    "UnrealEditor-Cataclysm.dll",
    "UnrealEditor-CataclysmEmpire.dll",
    "UnrealEditor-CataclysmEditor.dll",
)

#: What the commandlet logs when it starts the script it was given. The path it
#: names is checked against the script that was asked for, so a `-script=` the
#: engine could not resolve is reported as the script not running rather than as
#: a success.
#:
#: NOT `LogPython`, WHICH IS NOT EVIDENCE OF ANYTHING. Several engine plugins run
#: their own `init_unreal.py` at start-up and log under `LogPython`, so that
#: category appears in the log of a run whose script never executed. Checked
#: against a real run: an editor loading this project writes 40 `LogPython` lines
#: before it reaches the `-script=` file at all.
SCRIPT_STARTED = "LogPythonScriptCommandlet: Display: Running Python script:"

#: What the commandlet logs when the script returned without raising.
SCRIPT_SUCCEEDED = ("LogPythonScriptCommandlet: Display: "
                    "Python script executed successfully")

#: What it logs when the script raised or called `unreal.log_error`. Both
#: generators call `unreal.log_error` and then `raise SystemExit(1)` on any
#: failure, which produces exactly this line.
SCRIPT_FAILED = ("LogPythonScriptCommandlet: Error: "
                 "Python script executed with errors")


class CannotRunEditorScript(RuntimeError):
    """A precondition failed, or the script did not run. The message says which."""


def is_worktree(path: pathlib.Path | None = None) -> bool:
    """Whether this checkout is a linked git worktree rather than the original.

    A worktree's `.git` is a FILE holding a `gitdir:` line. An ordinary
    checkout's is a directory. This is only used to make the message specific,
    so a repository with no `.git` at all answers False.
    """
    return ((path or REPO_ROOT) / ".git").is_file()


def missing_module_libraries() -> list[str]:
    """Which of this project's compiled editor modules are not on disk."""
    return [name for name in REQUIRED_MODULE_LIBRARIES
            if not (PROJECT_BINARIES / name).is_file()]


def check_preconditions() -> None:
    """Raise CannotRunEditorScript unless the editor can actually load the project."""
    if not EDITOR_CMD.is_file():
        raise CannotRunEditorScript(
            f"The Unreal editor is not at {EDITOR_CMD}.\n"
            f"  Set UE_ROOT to the engine installation directory. It is "
            f"currently {ENGINE_ROOT}.")

    if not PROJECT_FILE.is_file():
        raise CannotRunEditorScript(f"{PROJECT_FILE} does not exist.")

    missing = missing_module_libraries()
    if not missing:
        return

    message = [
        "The editor cannot be started.",
        f"This project's compiled modules are not in {PROJECT_BINARIES}.",
        "  Missing: " + ", ".join(missing),
        "",
        "The editor cannot load a project whose C++ modules are not built. It",
        "would start, run for about twenty seconds, write nothing, and exit",
        "without saying why. That is issue #279.",
    ]
    if is_worktree():
        message += [
            "",
            f"THIS CHECKOUT IS A GIT WORKTREE ({REPO_ROOT}).",
            "game/Binaries/ is gitignored, so a worktree never has one, and",
            "building one here is not the answer either: see the note at the top",
            "of tools/run_editor_python.py. Run the generator in the ordinary",
            "checkout and copy the result back. game/README.md says how.",
        ]
    else:
        message += [
            "",
            "Build the editor target first:",
            '  "$UE_ROOT/Engine/Build/BatchFiles/Build.bat" CataclysmEditor '
            f"Win64 Development -Project={PROJECT_FILE} -WaitMutex",
        ]
    raise CannotRunEditorScript("\n".join(message))


def _comparable(path: str) -> str:
    """A path in a form two spellings of the same file agree on.

    The engine logs Windows separators and its own capitalisation, and the
    caller's path came from the command line, so neither the slashes nor the
    case can be relied on to match.
    """
    return path.replace("\\", "/").casefold().strip()


def script_started(log_text: str, script: pathlib.Path) -> bool:
    """Whether the commandlet reported starting THIS script.

    Naming the script matters. A `-script=` path the engine cannot resolve
    produces a log that still has the plugins' own start-up chatter in it, so
    any looser check reports a run that did nothing as a success.
    """
    wanted = _comparable(str(script))
    return any(SCRIPT_STARTED in line and wanted in _comparable(line)
               for line in log_text.splitlines())


def script_outcome(log_text: str) -> str:
    """"succeeded", "failed", or "unknown", from the commandlet's own report."""
    if SCRIPT_FAILED in log_text:
        return "failed"
    if SCRIPT_SUCCEEDED in log_text:
        return "succeeded"
    return "unknown"


def run(script: pathlib.Path, timeout: float = 1800.0) -> str:
    """Run one script inside the editor and return what the log said.

    The log is deleted first, so a run that writes nothing cannot be mistaken for
    the previous run's output.
    """
    check_preconditions()

    script = script.resolve()
    if not script.is_file():
        raise CannotRunEditorScript(f"{script} does not exist.")

    RUN_LOG.parent.mkdir(parents=True, exist_ok=True)
    try:
        RUN_LOG.unlink(missing_ok=True)
    except OSError as locked:
        raise CannotRunEditorScript(
            f"{RUN_LOG} could not be deleted: {locked}\n"
            "  Another process is holding it. Close whatever is reading it and "
            "run this again.") from locked

    # `-script=` MUST be absolute. A relative path resolves from the engine's own
    # binaries directory, not from here, and fails with `Could not load Python
    # file 'C:/Program Files/.../Engine/Binaries/tools/...'`.
    subprocess.run(
        [str(EDITOR_CMD), str(PROJECT_FILE), "-run=pythonscript",
         f"-script={script}", f"-abslog={RUN_LOG}",
         "-unattended", "-nopause", "-nosplash"],
        capture_output=True, text=True, cwd=GAME_DIR, timeout=timeout, check=False)

    if not RUN_LOG.is_file():
        raise CannotRunEditorScript(
            f"{RUN_LOG} was not written. The editor produced no log at all, "
            "which usually means it failed to start.")

    log_text = RUN_LOG.read_text(encoding="utf-8", errors="replace")

    if not script_started(log_text, script):
        raise CannotRunEditorScript(
            f"The editor ran and exited without ever starting {script.name}.\n"
            f"  Read {RUN_LOG} for what it did instead.\n"
            "  The usual causes are a project whose C++ modules are not built "
            "(issue #279) and a -script= path the engine could not resolve.")

    outcome = script_outcome(log_text)
    if outcome == "failed":
        raise CannotRunEditorScript(
            f"{script.name} ran and reported errors.\n"
            f"  Read {RUN_LOG}; the failing lines are logged under LogPython.")
    if outcome == "unknown":
        raise CannotRunEditorScript(
            f"{script.name} started but the commandlet reported neither success "
            f"nor failure.\n  Read {RUN_LOG}. The editor may have been killed "
            "part way through.")
    return log_text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run a Python script inside the Unreal editor.")
    parser.add_argument("script",
                        help="path to the script, relative to the repository "
                             "root or absolute")
    parser.add_argument("--timeout", type=float, default=1800.0,
                        help="seconds to wait for the editor (default 1800)")
    args = parser.parse_args(argv)

    script = pathlib.Path(args.script)
    if not script.is_absolute():
        script = REPO_ROOT / script

    try:
        run(script, timeout=args.timeout)
    except CannotRunEditorScript as failure:
        # Not prefixed with "did not run": one of the three failures is a script
        # that ran and reported errors, and saying it did not run would be false.
        print(failure, file=sys.stderr)
        return 2

    print(f"{script.name} ran. Read {RUN_LOG} for what it did.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
