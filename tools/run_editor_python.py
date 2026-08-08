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

**After**: read the run's log and fail unless the commandlet reported both
starting this script and finishing without errors. That is what catches the
silent case: an editor that gave up before reaching the Python plugin never logs
`Running Python script:` at all. The log is this runner's own file,
`game/Saved/Logs/run_editor_python.log`, not the editor's default
`Cataclysm.log` — see RUN_LOG for why that matters.

    IT IS NOT ENOUGH TO LOOK FOR THE `LogPython` CATEGORY. Engine plugins run
    their own start-up scripts and log 40 lines under it before the `-script=`
    file is reached, so a run in which the script raised still has plenty of
    them. The first version of this file checked exactly that and reported a
    script that called `unreal.log_error` and raised `SystemExit(1)` as a
    success. See SCRIPT_STARTED.

**AFTER, SECOND**: report every file the run left changed. Issue #414. Running the
editor dirties the working tree in ways the script did not ask for: it rewrites
`game/Config/DefaultEditor.ini` with about 57 kilobytes of asset-viewer preview
scene profiles, and it re-saves assets it merely happened to load. Both are
committed files and `.uasset` is stored in git LFS, so an incidental re-save is a
new binary object rather than a readable diff. Somebody who runs a generator and
then types `git add -A` commits an engine version bump to a Blueprint they never
opened, inside a pull request about something else, and no reviewer can read it.

    IT REPORTS RATHER THAN REVERTS, and that is deliberate. The script's own
    output is a change to the working tree as well, and this runner has no way of
    telling the two apart -- the generators exist precisely to write assets. The
    person who ran it does know. What was missing was a list to look at.

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


#: Files the editor rewrites as its own bookkeeping, whatever the script did.
#:
#: NAMED SO THE REPORT CAN SAY WHICH IS WHICH. Everything else in the report may
#: be the script's intended output; this one never is.
#:
#: WHY IT IS REPORTED RATHER THAN GITIGNORED. Committing the expanded version was
#: the other suggestion on issue #414 and does not settle anything: the 57
#: kilobytes are `[/Script/AdvancedPreviewScene.SharedProfiles]`, written by
#: `USharedProfiles`, which is declared `UCLASS(config = Editor, defaultconfig)`
#: in Engine/Source/Editor/AdvancedPreviewScene/Public/AssetViewerSettings.h --
#: `defaultconfig` is what sends it to the PROJECT's ini rather than the user's.
#: The engine owns that section and will write it again.
#:
#: Gitignoring the file outright would drop its one committed line,
#: `bAllowMultiplePIEInstances=True`. Whether that line does anything is issue
#: #427: the property does not appear in Unreal 5.8's editor classes -- the
#: similarly named one is `bAllowMultiplePIEWorlds` -- and `UEditorEngine` is
#: declared `config=Engine`, so it would not read this file anyway. Until that is
#: confirmed by observation rather than by reading headers, this reports the file
#: instead of hiding it.
EDITOR_BOOKKEEPING_FILES = ("game/Config/DefaultEditor.ini",)


class CannotRunEditorScript(RuntimeError):
    """A precondition failed, or the script did not run. The message says which."""


def working_tree_state() -> dict[str, str]:
    """Every path git considers changed, against its status code.

    Untracked files are included, because an asset the editor wrote for the first
    time is exactly as easy to commit by accident as one it modified.

    An empty result when git cannot be run at all is deliberate: this is a
    reporting aid, and a missing git must not stop a generator from running.
    """
    try:
        finished = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=all"],
            capture_output=True, text=True, cwd=REPO_ROOT, check=False)
    except OSError:
        return {}

    if finished.returncode != 0:
        return {}

    state: dict[str, str] = {}
    for line in finished.stdout.splitlines():
        if len(line) < 4:
            continue
        # Porcelain format is two status characters, a space, then the path. A
        # renamed entry is "old -> new"; the new name is what is on disk.
        code, path = line[:2], line[3:].strip()
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        state[path.strip('"')] = code
    return state


def changes_between(before: dict[str, str],
                    after: dict[str, str]) -> list[tuple[str, str]]:
    """Paths whose git status is not what it was, with the status they now have.

    A path that was already modified before the run and is still modified is not
    reported: the run did not do that. A path whose status CHANGED is, because
    modified-then-also-staged is a different fact from untouched.
    """
    return sorted((path, code) for path, code in after.items()
                  if before.get(path) != code)


def describe_changes(changes: list[tuple[str, str]]) -> str:
    """The report printed after a run. Empty when the run changed nothing."""
    if not changes:
        return ""

    lines = [f"The editor run left {len(changes)} file(s) changed:"]
    for path, code in changes:
        note = ""
        if path in EDITOR_BOOKKEEPING_FILES:
            note = "   <- editor bookkeeping, not your script"
        elif path.endswith((".uasset", ".umap")):
            note = "   <- binary, stored in git LFS"
        lines.append(f"  {code} {path}{note}")

    lines += [
        "",
        "Some of these are what the script was for. Check the rest before",
        "committing: the editor re-saves assets it merely loaded, and a .uasset",
        "change cannot be reviewed by reading it. Issue #414.",
        "",
    ]

    # TWO DIFFERENT COMMANDS, BECAUSE NEITHER WORKS ON THE OTHER CASE.
    # `git checkout --` restores a tracked file and does nothing whatever to an
    # untracked one, which is what a newly written asset is. Printing only that
    # would send somebody to a command that reports success and leaves the file
    # exactly where it was.
    if any(code != "??" for _, code in changes):
        lines.append(
            "  git checkout -- <path>   to discard a change to a tracked file")
    if any(code == "??" for _, code in changes):
        lines.append(
            "  git clean -f <path>      to remove one that is untracked (??)")

    return "\n".join(lines)


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


def missing_binaries_message(missing: list[str], worktree: bool) -> str:
    """What to say when the project's compiled modules are not there.

    Separate from check_preconditions() so it can be tested anywhere. The engine
    is not installed on the continuous integration runner, so a test that reached
    this through check_preconditions() would get the "editor is not at" message
    instead and pass or fail for the wrong reason.
    """
    message = [
        "The editor cannot be started.",
        f"This project's compiled modules are not in {PROJECT_BINARIES}.",
        "  Missing: " + ", ".join(missing),
        "",
        "The editor cannot load a project whose C++ modules are not built. It",
        "would start, run for about twenty seconds, write nothing, and exit",
        "without saying why. That is issue #279.",
    ]
    if worktree:
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
    return "\n".join(message)


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
    if missing:
        raise CannotRunEditorScript(missing_binaries_message(missing, is_worktree()))


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


def run(script: pathlib.Path, timeout: float = 1800.0,
        changes: list[tuple[str, str]] | None = None) -> str:
    """Run one script inside the editor and return what the log said.

    The log is deleted first, so a run that writes nothing cannot be mistaken for
    the previous run's output.

    @param changes  filled in, if given, with every path whose git status the run
        changed. A list rather than a second return value so that callers which
        only want the log are unaffected, and so that a run which raises still
        leaves the caller holding what it dirtied before it failed.
    """
    check_preconditions()

    before = working_tree_state()

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

    # BEFORE THE LOG IS JUDGED, so that a run which started the editor, dirtied
    # the tree and then failed still reports what it dirtied. That is the case
    # where the reader most needs the list, because a failed run is one nobody
    # expects to have changed anything.
    if changes is not None:
        changes[:] = changes_between(before, working_tree_state())

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

    changes: list[tuple[str, str]] = []
    try:
        run(script, timeout=args.timeout, changes=changes)
    except CannotRunEditorScript as failure:
        # Not prefixed with "did not run": one of the three failures is a script
        # that ran and reported errors, and saying it did not run would be false.
        print(failure, file=sys.stderr)
        # PRINTED ON THE FAILING PATH TOO. A run that failed part way through may
        # still have left assets rewritten, and that is the case where nobody
        # thinks to look.
        report = describe_changes(changes)
        if report:
            print("", file=sys.stderr)
            print(report, file=sys.stderr)
        return 2

    print(f"{script.name} ran. Read {RUN_LOG} for what it did.")

    report = describe_changes(changes)
    if report:
        print("")
        print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
