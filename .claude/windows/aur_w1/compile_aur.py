"""The three builds for window aur-w1 (the auras' mana reservation), copied from window fr-w1's, under the Unreal
lock aur-w1-compile, as the coordinating session ruled for slice 2 on 2026-11-18: no editor, the lock taken so no window starts in the middle of them, and
released when the third build ends.

  1. the branch as committed, every file in its unity file;
  2. each changed .cpp alone: `git read-tree origin/development` makes `git status` list the branch's files as changed
     while HEAD stays where it is, `Makefile.bin` is moved aside so the build tool works its adaptive unity set out
     again, and the build tool's "[Adaptive Build] Excluded from Cataclysm unity file" line names the files compiled
     alone; then `git reset -q` puts the index back;
  3. the unity files again, `Makefile.bin` moved aside again.

The build tool's own log (%LOCALAPPDATA%/UnrealBuildTool/Log.txt) is copied beside this file after each build, before
another session's build can write over it. NO TIMEOUT on any build (CLAUDE.md). THE LOCK IS RELEASED IN EVERY CASE,
in a `finally`, once this script acquired it, and the index is put back there too.

python -u compile_aur.py <expected head>  > aur_compile.out 2>&1      (start detached; read the last line)

Prints one line per step and "AUR COMPILE FINAL LINE" at the end, whatever happened.

Copied from the slice 5a compile script, which ran slice 5a's builds. Every child process is started through quiet_run.
"""
import datetime
import os
import pathlib
import shutil
import subprocess
import sys

# EVERY CHILD PROCESS STARTS WITH NO CONSOLE WINDOW AND IN ITS OWN PROCESS GROUP (the coordinating session's rule of
# 2026-11-19): a runner started with DETACHED_PROCESS gave its children visible consoles, and closing one ended window
# ring-w1. Every subprocess.run in this script goes through quiet_run; none is bare.
NO_WINDOW = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def quiet_run(*args, **kwargs):
    return subprocess.run(*args, creationflags=NO_WINDOW, **kwargs)


HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]  # this worktree: .claude/windows/aur_w1 is three levels below it
LOCK_NAME = "aur-w1-compile"
UBT_LOG = pathlib.Path(os.environ.get("LOCALAPPDATA", "")) / "UnrealBuildTool" / "Log.txt"
# THE x64 FOLDER IS IN THE PATH: the first run of this script looked one folder up, found nothing, moved nothing, and
# builds 2 and 3 reported "target already up to date" with no file compiled alone (2026-11-18).
MAKEFILE = REPO / "game" / "Intermediate" / "Build" / "Win64" / "x64" / "CataclysmEditor" / "Development" / "Makefile.bin"
UNITY = REPO / "game" / "Intermediate" / "Build" / "Win64" / "x64" / "UnrealEditor" / "Development" / "Cataclysm"


def say(text):
    print(text, flush=True)


def git(*args):
    done = quiet_run(["git", "-C", str(REPO), *args], capture_output=True, text=True, check=False)
    return done.returncode, (done.stdout + done.stderr).strip()


def ps(command):
    return quiet_run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True,
                          check=False).stdout.strip()


def lock(action):
    done = quiet_run([sys.executable, "tools/unreal_lock.py", action, LOCK_NAME], capture_output=True, text=True,
                          cwd=str(REPO), check=False)
    say(f"lock {action}: exit {done.returncode} {(done.stdout + done.stderr).strip()[:300]}")
    return done.returncode == 0


def move_makefile_aside(tag):
    if MAKEFILE.exists():
        moved = MAKEFILE.with_name(f"Makefile.bin.aur_{tag}")
        if moved.exists():
            moved.unlink()
        MAKEFILE.rename(moved)
        say(f"Makefile.bin moved aside as {moved.name}")
        return moved
    say(f"Makefile.bin not found at {MAKEFILE}; nothing moved")
    return None


def build(tag):
    log = HERE / f"aur_build_{tag}.out"
    began = datetime.datetime.now()
    say(f"build {tag}: began {began.isoformat(timespec='seconds')}")
    with log.open("w", encoding="utf-8") as out:
        code = quiet_run([sys.executable, "-u", "tools/unreal_build.py", "build"], stdout=out,
                              stderr=subprocess.STDOUT, cwd=str(REPO), check=False).returncode
    ended = datetime.datetime.now()
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    line = next((l for l in lines if l.startswith("Build:") and "waiting" not in l), "(no Build: line)")
    say(f"build {tag}: ended {ended.isoformat(timespec='seconds')}, {(ended - began).total_seconds():.0f} s")
    say(f"build {tag}: {line[:900]} | exit {code}")
    for waited in (l for l in lines if "waiting" in l and l.startswith("Build:")):
        say(f"build {tag}: {waited[:200]}")
    if UBT_LOG.exists():
        kept = HERE / f"aur_build_{tag}_ubt.log"
        shutil.copyfile(UBT_LOG, kept)
        text = kept.read_text(encoding="utf-8", errors="replace").splitlines()
        for l in text:
            if "[Adaptive Build]" in l:
                say(f"build {tag}: {l.strip()[:900]}")
        warnings = [l.strip() for l in text if "warning C" in l or ": error" in l]
        say(f"build {tag}: {len(warnings)} compiler warning or error lines in the build tool's log")
        for l in warnings[:20]:
            say(f"build {tag}:   {l[:400]}")
    else:
        say(f"build {tag}: the build tool's log was not found at {UBT_LOG}")
    if code != 0:
        for l in lines[-30:]:
            say(f"build {tag} tail: {l[:400]}")
    return code == 0


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    expected = sys.argv[1] if len(sys.argv) > 1 else ""
    head = git("rev-parse", "--short=9", "HEAD")[1]
    dirty = git("status", "--porcelain", "--untracked-files=no")[1]
    say(f"head {head}, expected {expected}, tracked tree dirty={dirty!r}")
    if not expected or not head.startswith(expected[:8]) or dirty:
        say("STOPPED: not the expected head, or the tracked tree is dirty; the lock was not taken")
        say("AUR COMPILE FINAL LINE")
        return
    free_kb = ps("(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory")
    free_mb = int(free_kb) // 1024 if free_kb.isdigit() else -1
    say(f"free physical memory: {free_mb} MB (needs 9216, the standing figure)")
    procs = ps("Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue | "
               "ForEach-Object { $_.ProcessName + ' ' + $_.Id }")
    say(f"editor processes: {procs or 'none'}")
    if free_mb < 9216 or procs:
        say("STOPPED: too little free memory, or an editor is open; the lock was not taken")
        say("AUR COMPILE FINAL LINE")
        return
    if not lock("acquire"):
        say("STOPPED: the lock could not be taken")
        say("AUR COMPILE FINAL LINE")
        return
    index_moved = False
    moved = []
    try:
        if "--from-2" in sys.argv:
            say("build 1_branch: skipped (--from-2): it ran and succeeded in the first run of this script")
        elif not build("1_branch"):
            say("STOPPED: the branch's build failed")
            return
        code, out = git("read-tree", "origin/development")
        index_moved = code == 0
        say(f"read-tree origin/development: exit {code} {out[:200]}")
        changed = git("status", "--porcelain", "--untracked-files=no")[1].splitlines()
        say(f"git status now lists {len(changed)} changed tracked files: {[c[3:] for c in changed]}")
        moved.append(move_makefile_aside("alone"))
        if moved[-1] is None:
            say("STOPPED: no makefile was moved, so the build tool would not work out which files to compile alone")
            return
        ok_alone = build("2_alone")
        code, out = git("reset", "-q")
        index_moved = code != 0
        say(f"reset: exit {code} {out[:200]}; status {git('status', '--porcelain', '--untracked-files=no')[1]!r}; "
            f"HEAD {git('rev-parse', '--short=9', 'HEAD')[1]}")
        if not ok_alone:
            say("STOPPED: the build of each changed file alone failed")
            return
        moved.append(move_makefile_aside("unity"))
        if not build("3_unity"):
            say("STOPPED: the unity build after it failed")
            return
        say("ALL THREE BUILDS SUCCEEDED")
    finally:
        if index_moved:
            say(f"reset in finally: {git('reset', '-q')}")
        for path in moved:
            if path is not None and path.exists():
                path.unlink()
                say(f"deleted {path.name}")
        lock("release")
        say(f"tracked tree after: {git('status', '--porcelain', '--untracked-files=no')[1]!r}; HEAD "
            f"{git('rev-parse', '--short=9', 'HEAD')[1]}")
        say("AUR COMPILE FINAL LINE")


if __name__ == "__main__":
    main()
