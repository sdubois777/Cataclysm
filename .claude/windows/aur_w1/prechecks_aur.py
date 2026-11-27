"""The pre-checks for window aur-w1 (the auras' mana reservation), copied from window fr-w1's. Copied from the slice 5a and slice 4 pre-checks: the six of the r2580 template, the
simulation-process check, and a seventh -- no Unreal asset in this worktree is an LFS pointer file (first-surge-w1,
2026-11-17). Every reading is printed and written to aur_prechecks.out beside this file. The last line is
"PRECHECKS: ALL PASS" or "PRECHECKS: FAIL (<which>)". Exit 0 only when all pass.
python -u prechecks_fr.py"""
import datetime
import json
import pathlib
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# EVERY CHILD PROCESS STARTS WITH NO CONSOLE WINDOW AND IN ITS OWN PROCESS GROUP (the coordinating session's rule of
# 2026-11-19): a runner started with DETACHED_PROCESS gave its children visible consoles, and closing one ended window
# ring-w1. Every subprocess.run in this script goes through quiet_run; make_window_fr.py asserts none is bare.
NO_WINDOW = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def quiet_run(*args, **kwargs):
    return subprocess.run(*args, creationflags=NO_WINDOW, **kwargs)


HERE = pathlib.Path(__file__).resolve().parent
REPO = "sdubois777/Cataclysm"
MAIN = r"C:\Projects\Cataclysm"
OUT = HERE / "aur_prechecks.out"
# THE ONE OPEN PULL REQUEST THAT DOES NOT COUNT, ruled by the coordinating session for this window on 2026-11-27: #2961
# (the walking Sentinel), whose "Game compiles" job on the runner has already passed; only its Python check, on
# GitHub's own machines, may still run, and since #2959 a merge into development starts no Unreal build. It passes
# check 5, and check 2 for a CI run of it only while that run's "Game compiles" job is completed (read, not assumed).
# Any other open pull request touching game/ still counts.
EXEMPT_PR = 2961
WORKTREE = HERE.parents[2]
lines = []
failed = []


def say(text):
    print(text, flush=True)
    lines.append(text)


def gh(*args):
    done = quiet_run(["gh", *args, "--repo", REPO], capture_output=True, text=True, check=False)
    return done.returncode, done.stdout.strip()


def ps(command):
    return quiet_run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True,
                          check=False).stdout.strip()


say(f"prechecks measured at {datetime.datetime.now().isoformat(timespec='seconds')}")

# 1. development's head, and its latest CI and Unreal runs.
head = quiet_run(["git", "-C", MAIN, "ls-remote", "origin", "refs/heads/development"], capture_output=True,
                      text=True, check=False).stdout.split()[0]
say(f"1. development head {head}")
code, runs = gh("run", "list", "--branch", "development", "--limit", "20", "--json",
                "headSha,status,conclusion,workflowName")
runs = json.loads(runs) if code == 0 and runs else []
for name in ("CI", "Unreal"):
    latest = next((r for r in runs if r["workflowName"] == name), None)
    say(f"1. latest {name} run on development: {latest}")
    ok = latest is not None and latest["status"] == "completed" and latest["conclusion"] == "success"
    if name == "CI":
        # A CI RUN STILL GOING ON DEVELOPMENT'S OWN HEAD IS RECORDED AS PENDING, NOT A FAILURE, in window aur-w1: every
        # job of the CI workflow runs on GitHub's machines (`runs-on: ubuntu-latest` in .github/workflows/ci.yml on
        # development), so it cannot touch this machine. A failed or a stale one still fails this check.
        pending = latest is not None and latest["status"] != "completed" and latest["headSha"] == head
        if pending:
            say("1.   CI on development's head is still running on GitHub's machines: PENDING, not a failure")
        ok = (ok or pending) and latest is not None and latest["headSha"] == head
    if not ok:
        failed.append(f"1 {name}")

# CHECKS 2 AND 5 COUNT ONLY WHAT CHANGES game/: the coordinating session's ruling of 2026-11-21. They exist to stop a
# build colliding with the window's, and a change with no file under game/ starts no build on this machine (its CI's
# "Game compiles" job ends in seconds; the Unreal workflow runs only for game/). A run of the Unreal workflow always
# counts. Any answer that cannot be read counts as touching game/, so a failed lookup blocks rather than passes. Copied
# from the level-1 session's prechecks_we.py on 2026-11-22, at the coordinating session's word.
def pr_files(number):
    code, out = gh("pr", "view", str(number), "--json", "files")
    return [f["path"] for f in json.loads(out).get("files", [])] if code == 0 and out else None


def commit_files(sha):
    done = quiet_run(["gh", "api", f"repos/{REPO}/commits/{sha}", "--jq", ".files[].filename"],
                     capture_output=True, text=True, check=False)
    return done.stdout.split() if done.returncode == 0 else None


def touches_game(files):
    return files is None or any(f.startswith("game/") for f in files)


code, open_prs = gh("pr", "list", "--state", "open", "--json", "number,headRefName")
open_prs = json.loads(open_prs) if code == 0 and open_prs else None

# 2. no run in progress or queued that changes game/.
for status in ("in_progress", "queued"):
    code, listed = gh("run", "list", "--status", status, "--json", "databaseId,workflowName,headBranch,headSha")
    say(f"2. runs {status}: exit {code} {listed}")
    runs = json.loads(listed) if code == 0 and listed else None
    if runs is None or open_prs is None:
        failed.append(f"2 {status}")
        continue
    for run in runs:
        pr = next((p for p in open_prs if p["headRefName"] == run["headBranch"]), None)
        files = pr_files(pr["number"]) if pr else commit_files(run["headSha"])
        counts = run["workflowName"] == "Unreal" or touches_game(files)
        # A CI RUN COUNTS ONLY WHEN IT HOLDS A JOB OTHER THAN THE WORKFLOW'S TWO HOSTED ONES, in window aur-w1. Since
        # #2959 the CI workflow's jobs are "Simulation checks" and "Full balance sweep", both `runs-on: ubuntu-latest`;
        # only the Unreal workflow runs on this machine. The jobs are read, not assumed, and an unreadable answer counts.
        if counts and run["workflowName"] == "CI":
            code, jobs = gh("run", "view", str(run["databaseId"]), "--json", "jobs")
            names = [j.get("name", "") for j in json.loads(jobs).get("jobs", [])] if code == 0 and jobs else None
            hosted = names is not None and bool(names) and set(names) <= {"Simulation checks", "Full balance sweep"}
            say(f"2.   run {run['databaseId']} CI jobs {names}: "
                f"{'only GitHub-hosted jobs, so it cannot touch this machine' if hosted else 'COUNTS'}")
            counts = not hosted
        say(f"2.   run {run['databaseId']} {run['workflowName']} on {run['headBranch']}: "
            f"{'COUNTS' if counts else 'ignored'} (files {files if files is None else len(files)}, "
            f"game/ {'unknown' if files is None else sum(f.startswith('game/') for f in files)})")
        if counts:
            failed.append(f"2 {status}")
            break

# 3. the lock is free.
lock = quiet_run(["python", "tools/unreal_lock.py", "status"], capture_output=True, text=True, cwd=MAIN,
                      check=False)
say(f"3. lock: exit {lock.returncode} {lock.stdout.strip()}")
if lock.returncode != 0 or "is free" not in lock.stdout:
    failed.append("3 lock")

# 4. no build or editor process.
names = "UnrealEditor,UnrealEditor-Cmd,UnrealBuildTool,UbaHost,ShaderCompileWorker,cl,link,dotnet"
procs = ps(f"Get-Process -Name {names} -ErrorAction SilentlyContinue | "
           "ForEach-Object { $_.ProcessName + ' ' + $_.Id }")
say(f"4. build or editor processes: {procs or 'none'}")
if procs:
    failed.append("4 processes")

# 5. no pull request open that changes game/ (see the ruling above check 2).
say(f"5. open pull requests: {open_prs}")
if open_prs is None:
    failed.append("5 pull requests")
else:
    for pr in open_prs:
        files = pr_files(pr["number"])
        counts = touches_game(files) and pr["number"] != EXEMPT_PR
        under = "files unreadable" if files is None else f"{len(files)} files, {sum(f.startswith('game/') for f in files)} under game/"
        say(f"5.   #{pr['number']} {pr['headRefName']}: {'COUNTS' if counts else 'ignored'} ({under})"
            + (" -- exempt, as ruled for this window" if pr["number"] == EXEMPT_PR else ""))
        if counts:
            failed.append("5 pull requests")
            break

# Simulation processes: t290, probe or experiments.py.
sims = ps("Get-CimInstance Win32_Process -Filter \"Name like 'python%'\" | "
          "Where-Object { $_.CommandLine -match 't290|probe|experiments\\.py' } | "
          "ForEach-Object { [string]$_.ProcessId + ' ' + $_.CommandLine }")
say(f"simulation processes: {sims or 'none'}")
if sims:
    failed.append("simulation processes")

# 6. free physical memory, at least 9,216 MB: the coordinating session's ruling for window D of 2026-11-21, extended to
# every gate the same day, since the owner's own programs (Firefox, Discord, Steam) hold the rest; relayed to this
# session on 2026-11-22.
free_kb = ps("(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory")
free_mb = int(free_kb) // 1024 if free_kb.isdigit() else -1
say(f"6. free physical memory: {free_mb} MB (needs 9216, window D's ruling)")
if free_mb < 9216:
    failed.append("6 memory")

# 7. no Unreal asset in this worktree is an LFS pointer file.
pointers = [str(p.relative_to(WORKTREE)) for p in (WORKTREE / "game" / "Content").rglob("*")
            if p.suffix in (".uasset", ".umap") and p.is_file() and p.open("rb").read(40).startswith(b"version https://git-lfs")]
say(f"7. LFS pointer files under game/Content: {len(pointers)} {pointers[:5]}")
if pointers:
    failed.append("7 LFS pointers")

say("PRECHECKS: ALL PASS" if not failed else f"PRECHECKS: FAIL ({', '.join(failed)})")
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
raise SystemExit(0 if not failed else 1)

