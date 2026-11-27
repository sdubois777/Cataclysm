"""Window aur-w1 (auras reserve maximum mana while on and drain none, branch feat/aura-reservation): pre-checks, a
recount of the base log, the written-out record version check, the lock, one build, THE EDITOR STEP (the six tables,
committed alone, the tested head becoming that commit), the one new group alone, then THE WHOLE UNREAL SUITE AND THE
PYTHON SUITE IN PARALLEL, ruff, and the one COMBINED proof G (the three breaks of aur_w1_cpp_breaks.py).
Adapted from window fr-w1's run_fr.py, which it follows step for step.

REFUSES TO TAKE THE LOCK unless prechecks_aur.py printed "PRECHECKS: ALL PASS", the base log holds the registered
base total, the branch's head is exactly the registered head, and the tracked tree is clean.

STOPS on a build that fails, a moved head, or an asset step that changes a file other than the expected ones or does
not change all of them (another asset it re-saves is reverted and recorded). A GROUP ALONE THAT DOES NOT PASS ITS
REGISTERED COUNT IS RECORDED AND THE WINDOW GOES ON to the whole suite, Python and ruff. THE PROOF RUNS ONLY WHEN the
group alone and the whole suite passed their registered counts.

THE LOCK IS RELEASED IN EVERY CASE, in a `finally`, once it was acquired by this script.

python start_detached.py run_aur.py aur_run.out aur_run.err aur_run.pid      (then poll for the last line)

Prints one line per step, and "AUR FINAL LINE" at the end, whatever happened.
"""
import os
import pathlib
import re
import subprocess
import sys

# EVERY CHILD PROCESS STARTS WITH NO CONSOLE WINDOW AND IN ITS OWN PROCESS GROUP (the coordinating session's rule of
# 2026-11-19): a runner started with DETACHED_PROCESS gave its children visible consoles, and closing one ended window
# ring-w1. Every subprocess.run in this script goes through quiet_run; none is bare.
NO_WINDOW = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def quiet_run(*args, **kwargs):
    return subprocess.run(*args, creationflags=NO_WINDOW, **kwargs)


HERE = pathlib.Path(__file__).resolve().parent
REPO = r"C:\aur"
BRANCH = "feat/aura-reservation"
ASSETS_ALREADY_COMMITTED = False  # six DataTable sources changed; the editor step makes and commits their assets
SKIP_ALONE = False  # the one new group runs alone first
EXPECTED_HEAD = "4b19e69d"  # the aura reservation with development 20b6f44ff merged (the merge commit)
LOCK_NAME = "aur-w1"
PROOFS = ['G']
ASSET_COMMAND = [sys.executable, "-u", "tools/run_editor_python.py", "tools/generate_datatable_assets.py"]
ASSET_FILES = {"game/Content/Data/DT_SkillSlots.uasset", "game/Content/Data/DT_EnchantmentEffects.uasset", "game/Content/Data/DT_EnchantmentsPositive.uasset", "game/Content/Data/DT_EnchantmentsNegative.uasset", "game/Content/Data/DT_PassiveEffects.uasset", "game/Content/Data/DT_PassiveNodes.uasset", "game/Data/datatable_asset_sources.json"}
ASSET_MESSAGE = """The six DataTable assets the aura reservation's CSVs changed, rebuilt in the editor (window aur-w1)

DT_SkillSlots (the Mana Reserved column), DT_EnchantmentsPositive, DT_EnchantmentsNegative, DT_EnchantmentEffects,
DT_PassiveEffects and DT_PassiveNodes, made by tools/generate_datatable_assets.py, with the record of their sources.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
"""
# THIS SESSION'S UNTRACKED WINDOW FILES, never the generator's doing and never touched.
NOT_OURS = (".claude/",)
# THE BASE: the newest passing whole-suite engine log, named by the coordinating session for this window: window bc-w1's
# bc_whole_engine.log (adoring-jennings-f8c84c), 6105 tests, its "Test Completed. Result={" lines copied to
# aur_base_completed.log.
BASE_LOG = HERE / "aur_base_completed.log"
# ONLY NEW GROUPS RUN ALONE (the owner, 2026-11-25). The one new group, counted from its registration strings.
ALONE = {
    "Cataclysm.AuraReservation.": 13,
}
NEW_GROUPS = {"Cataclysm.AuraReservation."}
ADDED = {}
# THE WHOLE SUITE: the base's 6105, and 18 more by name since its tree (efd26843a): development's Soul Collector rework
# took 14 tests out and put 18 in (net +4), and this branch adds 14 (13 Cataclysm.AuraReservation. and
# Cataclysm.Remains.TheBoneColossusAndTheAbominationReserveLessUnderTheReserveRows), removing none. Counted by name
# from the registration macros on efd26843a, origin/development 20b6f44ff and the head.
NEW_TESTS = 18
SUITE_TOTAL = 6123
# THE TEST SAVES: the base window's, 893 written and deleted. A different figure is printed as MISMATCH and recorded,
# and does not stop the window.
SAVES_WRITTEN = 893
SKIPPED_HALVES = 62
ENGINE_LOG = pathlib.Path(REPO) / "game" / "Saved" / "Logs" / "Cataclysm.log"


#: THE BASE RUN'S FIGURES, read from bc_whole_engine.log (window bc-w1, 6105 tests): first "Test Started" at
#: 17:47:00.505, last "Test Completed" at 18:03:01.985, and its CataclysmTestMemorySummary line.
BASE_SUITE_SECONDS = 961.480
BASE_PEAK_MB = 9222
BASE_COLLECTIONS = 308
BASE_COLLECTION_SECONDS = 44.638
STAMP = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]")


def stamp_seconds(line):
    """A log line's own time, in seconds since midnight of its day, or None."""
    found = STAMP.match(line)
    if not found:
        return None
    hours, minutes, seconds, millis = (int(found.group(i)) for i in (4, 5, 6, 7))
    return hours * 3600 + minutes * 60 + seconds + millis / 1000.0


def suite_seconds_line(text):
    """The suite's time from its first "Test Started" to its last "Test Completed", against the base run's."""
    lines = text.splitlines()
    first = next((stamp_seconds(l) for l in lines if "Test Started." in l), None)
    last = next((stamp_seconds(l) for l in reversed(lines) if "Test Completed." in l), None)
    if first is None or last is None:
        return "suite time: (could not read the first and last test times)"
    seconds = last - first if last >= first else last + 86400 - first
    return (f"suite time: {seconds:.1f} s ({seconds / 60:.2f} min) from the first test started to the last completed; "
            f"base {BASE_SUITE_SECONDS:.1f} s ({BASE_SUITE_SECONDS / 60:.2f} min): saved {BASE_SUITE_SECONDS - seconds:.1f} s")


def memory_against_base(line):
    """The memory summary's peak and collections, against the base run's."""
    def field(name):
        found = re.search(rf"\b{name}=([\d.]+)", line)
        return float(found.group(1)) if found else None

    return (f"memory against base: peak {field('peak_used_virtual_mb')} MB (base {BASE_PEAK_MB}); collections "
            f"{field('collections')} (base {BASE_COLLECTIONS}) taking {field('collection_total_s')} s "
            f"(base {BASE_COLLECTION_SECONDS})")


def say(text):
    print(text, flush=True)


def run(args, log, env=None, cwd=REPO):
    with log.open("w", encoding="utf-8") as out:
        return quiet_run(args, stdout=out, stderr=subprocess.STDOUT, cwd=cwd, env=env, check=False).returncode


def git(*args):
    done = quiet_run(["git", "-C", REPO, *args], capture_output=True, text=True, check=False)
    return done.returncode, (done.stdout + done.stderr).strip()


def lock(action):
    done = quiet_run([sys.executable, "tools/unreal_lock.py", action, LOCK_NAME], capture_output=True, text=True,
                          cwd=REPO, check=False)
    say(f"lock {action}: exit {done.returncode} {(done.stdout + done.stderr).strip()[:300]}")
    return done.returncode == 0


def head_now():
    return git("rev-parse", "--short=8", "HEAD")[1]


def tracked_dirty():
    return git("status", "--porcelain", "--untracked-files=no")[1]


def still_the_tested_head(tested, step):
    """True when HEAD is the tested commit and the tracked tree is clean; otherwise says so and returns False."""
    now, dirty = head_now(), tracked_dirty()
    if now != tested or dirty:
        say(f"STOPPED before {step}: HEAD is {now}, the tested head is {tested}; dirty={dirty!r}")
        return False
    return True


def recount_the_base():
    """True when every registered count of an unchanged group equals its "Test Completed" lines in the base log.

    COUNTED ON "Test Completed. Result={" LINES ONLY, and also by distinct path, which must agree: a test's path is on
    its "Test Started" line too, and counting every path is what doubled this slice's first registration.
    """
    if not BASE_LOG.exists():
        say(f"recount: the base log is not at {BASE_LOG}")
        return False
    completed = {}
    distinct = {}
    total = 0
    with BASE_LOG.open(encoding="utf-8", errors="replace") as log:
        for line in log:
            if "Test Completed. Result={" not in line:
                continue
            total += 1
            found = re.search(r"Path=\{([^}]*)\}", line)
            if not found:
                continue
            path = found.group(1)
            for group in ALONE:
                if path.lower().startswith(group.lower()):
                    completed[group] = completed.get(group, 0) + 1
                    distinct.setdefault(group, set()).add(path)
    say(f"recount: {total} Test Completed lines in the base log (the base is registered at {SUITE_TOTAL - NEW_TESTS})")
    ok = total == SUITE_TOTAL - NEW_TESTS
    for group, count in ALONE.items():
        if group in NEW_GROUPS:
            say(f"recount: {group} is new; registered {count}, base {completed.get(group, 0)}")
            ok = ok and completed.get(group, 0) == 0
            continue
        lines, paths = completed.get(group, 0), len(distinct.get(group, ()))
        base = count - ADDED.get(group, 0)
        match = lines == base and paths == base
        say(f"recount: {group} base {lines} lines, {paths} distinct paths; registered {count} less {ADDED.get(group, 0)} "
            f"this slice adds: {'MATCH' if match else 'MISMATCH'}")
        ok = ok and match
    return ok


def changed_paths():
    """Every path `git status` reports, tracked or not, except the coordinating session's, as (status, path)."""
    # Not git(): it strips the output, which takes the leading space off a first line such as " M game/...", and then
    # line[3:] loses the path's first letter. Window stalker-rows-w1 stopped on exactly that (2026-11-17).
    done = quiet_run(["git", "-C", REPO, "status", "--porcelain", "--untracked-files=all"], capture_output=True,
                          text=True, check=False)
    code, out = done.returncode, done.stdout
    found = []
    for line in out.splitlines():
        status, path = line[:2], line[3:].strip().strip('"')
        if not path.startswith(NOT_OURS):
            found.append((status, path))
    return code, found


def make_the_asset(head):
    """The asset step. Returns the new tested head's id, or None after saying why the window stops."""
    log = HERE / f"aur_assets_{head}.log"
    code = run(ASSET_COMMAND, log)
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    for l in lines[-12:]:
        say(f"assets: {l[:300]}")
    say(f"assets: exit {code}")
    if code != 0:
        say("STOPPED: the DataTable asset generator did not exit 0")
        return None

    code, found = changed_paths()
    say(f"assets: git status exit {code}, changed: {found}")
    paths = {path for _, path in found}
    unexpected = [(status, path) for status, path in found if path not in ASSET_FILES]

    # ANOTHER ASSET RE-SAVED IS REVERTED AND RECORDED, AND THE WINDOW GOES ON: the coordinating session's ruling of
    # 2026-11-18 for slice 2's window. A generator re-saving assets it merely loaded is known (issue #414's warning). A
    # new untracked asset other than the expected ones is left where it is, recorded, and not committed.
    def is_asset(path):
        return path.startswith("game/Content/") and path.endswith((".uasset", ".umap"))

    for status, path in [(s, p) for s, p in unexpected if is_asset(p)]:
        if status == "??":
            say(f"assets: ANOTHER NEW ASSET left alone and not committed: {path}")
            continue
        reverted = git("checkout", "--", path)
        say(f"assets: ANOTHER ASSET RE-SAVED, reverted: {status} {path} (exit {reverted[0]} {reverted[1][:200]})")

    # A NON-ASSET FILE CHANGED STOPS THE WINDOW: reverted when tracked, left alone and named when not.
    not_assets = [(s, p) for s, p in unexpected if not is_asset(p)]
    if not_assets:
        for status, path in not_assets:
            if status == "??":
                say(f"assets: UNEXPECTED untracked non-asset file left alone: {path}")
                continue
            reverted = git("checkout", "--", path)
            say(f"assets: UNEXPECTED non-asset change reverted: {status} {path} (exit {reverted[0]} "
                f"{reverted[1][:200]})")
        say(f"assets: after reverting, git status: {changed_paths()[1]}")
        say("STOPPED: the generator changed a file that is not an asset")
        return None
    if not ASSET_FILES <= paths:
        say(f"STOPPED: the generator did not change all the expected files; missing {sorted(ASSET_FILES - paths)}")
        return None
    if unexpected:
        say(f"assets: after reverting the other assets, git status: {changed_paths()[1]}")

    added = git("add", "--", *sorted(ASSET_FILES))
    say(f"assets: git add exit {added[0]} {added[1][:200]}")
    staged = set(git("diff", "--cached", "--name-only")[1].splitlines())
    if added[0] != 0 or staged != ASSET_FILES:
        say(f"STOPPED: staged {sorted(staged)}, not exactly the expected files")
        return None
    committed = quiet_run(["git", "-C", REPO, "commit", "-q", "-F", "-"], input=ASSET_MESSAGE, text=True,
                               capture_output=True, check=False)
    say(f"assets: git commit exit {committed.returncode} {(committed.stdout + committed.stderr).strip()[:300]}")
    new_head = head_now()
    parent = git("rev-parse", "--short=8", "HEAD~1")[1]
    in_commit = set(git("show", "--name-only", "--format=", "HEAD")[1].splitlines())
    subject = git("log", "-1", "--format=%s", "HEAD")[1]
    say(f"assets: committed {new_head} on {parent}: {subject!r}, files {sorted(in_commit)}")
    if committed.returncode != 0 or parent != head or in_commit != ASSET_FILES or tracked_dirty():
        say("STOPPED: the asset commit is not exactly the expected files on top of the registered head, or the "
            "tree is dirty after it")
        return None

    pushed = git("push", "origin", BRANCH)
    remote = git("ls-remote", "origin", f"refs/heads/{BRANCH}")[1].split()
    on_remote = bool(remote) and remote[0].startswith(new_head)
    say(f"assets: git push exit {pushed[0]}; origin's {BRANCH} is {remote[0][:8] if remote else '(none)'}: "
        f"{'the tested head' if on_remote else 'NOT THE TESTED HEAD; the window goes on with a local commit'}")
    say(f"assets: THE TESTED HEAD IS NOW {new_head}")
    return new_head


def window(head):
    env = dict(os.environ, REPO=REPO)
    build_log = HERE / f"aur_build_{head}.log"
    code = run([sys.executable, "-u", "tools/unreal_build.py", "build"], build_log)
    line = next((l for l in build_log.read_text(encoding="utf-8", errors="replace").splitlines()
                 if l.startswith("Build:") and "waiting" not in l), "(no Build: line)")
    say(f"build: {line} | exit {code}")
    if code != 0:
        say("STOPPED: build failed")
        return

    # THE EFFECT TABLE'S ASSET, made and committed; the new commit is the tested head from here on.
    tested = head if ASSETS_ALREADY_COMMITTED else make_the_asset(head)
    if tested is None:
        return

    # EACH GROUP ALONE, by its prefix, against its registered count. A group that does not pass is recorded and the
    # next one runs (the coordinating session's ruling of 2026-11-19). The engine log is kept.
    alone_failed = []
    # THE ALONE STEP RUNS in window aur-w1, the new group only (the owner, 2026-11-25). SKIP_ALONE is for a
    # later pass the coordinating session rules carried over; the whole suite below still runs them.
    if SKIP_ALONE:
        say(f"groups alone: SKIPPED by SKIP_ALONE, as the coordinating session ruled for this pass; "
            f"the {len(ALONE)} groups run in the whole suite below.")
    # EVERY GROUP IN ONE EDITOR START, SINCE ISSUE #2928 SLICE 6. Until then each group started an editor of its own,
    # about 18 s each (the survey on issue #2928, section 3). The groups are joined with "+", which the engine's
    # `Automation RunTests` takes as several filters, and `tools/unreal_build.py tests` prints one "Group <prefix>:"
    # line per group after its `Tests:` line, read from the run's "Test Completed" lines. Each group is held to its
    # registered count from its own line, as it was from its own run before. ONLY NEW GROUPS RUN ALONE (2026-11-25).
    if ALONE and not SKIP_ALONE:
        if not still_the_tested_head(tested, "the groups alone"):
            return
        joined = "+".join(ALONE)
        alone_log = HERE / f"aur_alone_{tested}.log"
        code = run([sys.executable, "-u", "tools/unreal_build.py", "tests", "--prefix", joined], alone_log)
        lines = alone_log.read_text(encoding="utf-8", errors="replace").splitlines()
        tests = [l for l in lines if l.startswith("Tests:")]
        say(f"alone, {len(ALONE)} groups in one editor start: {tests[-1] if tests else '(no Tests: line)'} | exit {code}")
        if ENGINE_LOG.exists():
            (HERE / f"aur_alone_engine_{tested}.log").write_bytes(ENGINE_LOG.read_bytes())
        for alone, count in ALONE.items():
            group = next((l for l in lines if l.startswith(f"Group {alone}:")), None)
            # ONE GROUP PRINTS NO "Group" LINE: `tools/unreal_build.py` prints one per group only when the prefix names
            # two or more (`if len(prefixes) > 1`), so a single group is read from the run's own `Tests:` line. Found in
            # window aur-w1's first run, which marked "13 tests performed, 13 succeeded, 0 failed" as FAILED.
            if group is None and len(ALONE) == 1 and tests:
                group = tests[-1]

            # THE NUMBERS ARE READ, NOT THE LITERAL TEXT: performed and succeeded must each equal the registered count
            # and failed must be 0. A group with no line at all fails.
            def number(pattern, group=group):
                found = re.search(pattern, group) if group else None
                return int(found.group(1)) if found else None

            performed = number(r"\b(\d+) tests performed")
            succeeded = number(r"\b(\d+) succeeded")
            failed = number(r"\b(\d+) failed")
            say(f"alone {alone}: {group or '(no Group line)'}; read performed={performed} succeeded={succeeded} "
                f"failed={failed}, expected {count}")
            if not (performed == count and succeeded == count and failed == 0):
                alone_failed.append(alone)
                say(f"FAILED: {alone} alone did not pass; recorded, and the window goes on")
        # THE RUN'S OWN EXIT CODE TOO, as each group's run was held to it: a crash or a refused registration fails the
        # step even when every group line reads right.
        if code != 0 and not alone_failed:
            alone_failed.append(f"(the groups' run exited {code})")
            say(f"FAILED: the groups' run exited {code}; recorded, and the window goes on")
    if not SKIP_ALONE:
        say(f"groups alone: {len(ALONE) - len(alone_failed)} of {len(ALONE)} passed"
            + (f"; FAILED: {', '.join(alone_failed)}" if alone_failed else ""))

    # THE WHOLE SUITE. No stall guard that ends processes by name: nothing here ends any process.
    if not still_the_tested_head(tested, "the whole suite"):
        return
    # THE PYTHON SUITE STARTS NOW AND RUNS BESIDE THE WHOLE UNREAL SUITE (the coordinating session's procedure of
    # 2026-11-21). No -q, with a JUnit file. It is waited for after the Unreal suite, and the proof runs after both.
    junit = HERE / f"aur_junit_{tested}.xml"
    py_log = HERE / f"aur_python_{tested}.log"
    py_out = py_log.open("w", encoding="utf-8")
    python = subprocess.Popen([sys.executable, "-u", "-m", "pytest", "-p", "no:cacheprovider", f"--junit-xml={junit}"],
                              stdout=py_out, stderr=subprocess.STDOUT, cwd=REPO, creationflags=NO_WINDOW)
    say(f"python: started beside the whole Unreal suite, PID {python.pid}")
    suite_log = HERE / f"aur_suite_{tested}.log"
    code = run([sys.executable, "-u", "tools/unreal_build.py", "tests"], suite_log)
    suite_lines = suite_log.read_text(encoding="utf-8", errors="replace").splitlines()
    for l in suite_lines:
        if l.startswith(("Build:", "Tests:", "Declared:", "Test saves:")) or "skipped part" in l:
            say(f"suite: {l[:400]}")
    for wanted in ("Tests:", "Declared:", "Test saves:"):
        if not any(l.startswith(wanted) for l in suite_lines):
            say(f"suite: (no {wanted} line)")
    say(f"suite exit {code}")

    # AGAINST THE REGISTERED TOTAL: performed and succeeded must each equal it, failed must be 0, and the exit code 0.
    # The skipped halves are printed against their registered count and do not stop the window.
    suite_tests = [l for l in suite_lines if l.startswith("Tests:")]

    def suite_number(pattern):
        found = re.search(pattern, suite_tests[-1]) if suite_tests else None
        return int(found.group(1)) if found else None

    performed = suite_number(r"\b(\d+) tests performed")
    succeeded = suite_number(r"\b(\d+) succeeded")
    failed = suite_number(r"\b(\d+) failed")
    skipped = suite_number(r"\b(\d+) skipped part")
    suite_ok = code == 0 and performed == SUITE_TOTAL and succeeded == SUITE_TOTAL and failed == 0
    say(f"suite: read performed={performed} succeeded={succeeded} failed={failed}, registered {SUITE_TOTAL}: "
        f"{'MATCH' if suite_ok else 'MISMATCH'}; skipped halves {skipped}, registered {SKIPPED_HALVES}")
    saves = re.search(r"Test saves: (\d+) written and deleted, (\d+) left behind", "\n".join(suite_lines))
    saves_ok = saves is not None and int(saves.group(1)) == SAVES_WRITTEN and int(saves.group(2)) == 0
    say(f"suite: test saves {saves.group(0) if saves else '(no Test saves: line read)'}; registered {SAVES_WRITTEN} "
        f"written and deleted, 0 left behind: {'MATCH' if saves_ok else 'MISMATCH'}")
    if ENGINE_LOG.exists():
        kept = HERE / f"aur_suite_engine_{tested}.log"
        kept.write_bytes(ENGINE_LOG.read_bytes())
        say(f"engine log kept as {kept.name}, {kept.stat().st_size} bytes")
        memory = [l for l in kept.read_text(encoding="utf-8", errors="replace").splitlines()
                  if "CataclysmTestMemorySummary:" in l]
        say(f"memory: {(memory[-1].strip() if memory else '(no CataclysmTestMemorySummary: line found)')[:400]}")
        # THE SUITE'S TIME AND MEMORY, beside the base run's (window bc-w1), recorded and not judged.
        say(suite_seconds_line(kept.read_text(encoding="utf-8", errors="replace")))
        if memory:
            say(memory_against_base(memory[-1]))

    # THE PYTHON SUITE, started beside the Unreal suite: waited for here, before ruff and the proof.
    code = python.wait()
    py_out.close()
    tail = [l for l in py_log.read_text(encoding="utf-8", errors="replace").splitlines() if l.strip()][-2:]
    for l in tail:
        say(f"python: {l[:300]}")
    xml = junit.read_text(encoding="utf-8", errors="replace") if junit.exists() else ""
    suite = re.search(r"<testsuite [^>]*>", xml)
    say(f"python exit {code} | junit {suite.group(0)[:300] if suite else '(none)'}")
    # THE WINDOW SCRIPTS IN .claude ARE LEFT OUT, as continuous integration's clean checkout leaves them out.
    ruff = quiet_run([sys.executable, "-m", "ruff", "check", ".", "--extend-exclude", ".claude"],
                          capture_output=True, text=True, cwd=REPO, check=False)
    say(f"ruff exit {ruff.returncode}: {(ruff.stdout or ruff.stderr).strip().splitlines()[-1][:200]}")
    if not suite_ok or alone_failed:
        say(f"STOPPED before the proofs: {'the whole suite did not pass its registered total' if not suite_ok else ''}"
            f"{'; ' if not suite_ok and alone_failed else ''}"
            f"{('groups alone failed: ' + ', '.join(alone_failed)) if alone_failed else ''}. No proof is run, because a "
            "fix would move the head")
        return

    # THE PROOFS, one at a time, each keeping both halves' engine logs (see prove_fr.py). An unproved proof is
    # recorded and the next one runs; a changed hash, a moved head or a run that did not report stops the window.
    verdicts = []
    for proof in PROOFS:
        if not still_the_tested_head(tested, f"proof {proof}"):
            break
        log = HERE / f"aur_{proof}_{tested}.log"
        code = run([sys.executable, "-u", str(HERE / "prove_aur.py"), proof], log, env=env, cwd=HERE)
        text = log.read_text(encoding="utf-8", errors="replace")

        def field(tag, proof=proof, text=text):
            found = re.search(rf"^{proof} {tag}: (.*)$", text, re.M)
            return found.group(1) if found else None

        anchors = re.findall(rf"^{proof} anchor count (\d+)", text, re.M)
        reported = (f"{proof} FINAL LINE" in text and field("proved") is not None and field("crashed") is not None
                    and not field("crashed").startswith("True") and bool(anchors)
                    and all(count == "1" for count in anchors))
        same_hash = field("BEFORE") is not None and field("BEFORE") == field("AFTER")
        proved = reported and same_hash and code == 0 and field("proved") == "True"
        verdict = "PROVED" if proved else ("NOT PROVED" if reported and same_hash else "STOP")
        verdicts.append(f"{proof} {verdict}")
        say(f"  {proof}: {verdict} | exit {code} | anchors {anchors} | hash same {same_hash} | reported {reported}")
        say(f"     summary: {(field('summary') or '(none printed)')[:400]}")
        say(f"     crashed: {field('crashed')}")
        say(f"     named failures: {field('named failures')}")
        for half in (1, 2):
            kept = HERE / f"aur_{proof}_run{half}.log"
            say(f"     run{half} log kept: {kept.name if kept.exists() else 'MISSING'}")
        if not same_hash:
            say(f"STOPPED at {proof}: the source hash changed across the proof, or was not printed")
            break
        if not reported:
            say(f"STOPPED at {proof}: the proof's run did not report a verdict")
            break
    say(f"proofs: {', '.join(verdicts)}")
    say(f"tested head: {tested}; HEAD now {head_now()}")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    for l in (HERE / "aur_prechecks.out").read_text(encoding="utf-8").splitlines():
        say(f"precheck: {l}")
    if "PRECHECKS: ALL PASS" not in (HERE / "aur_prechecks.out").read_text(encoding="utf-8"):
        say("STOPPED: the pre-checks did not all pass; the lock was not taken")
        say("AUR FINAL LINE")
        return
    if not recount_the_base():
        say("STOPPED: a registered group count does not equal the base log's; the lock was not taken")
        say("AUR FINAL LINE")
        return
    quiet_run(["git", "-C", REPO, "checkout", "-q", BRANCH], check=True)
    head = head_now()
    dirty = tracked_dirty()
    say(f"{BRANCH} {head} dirty={dirty!r}")
    if head != EXPECTED_HEAD or dirty:
        say("STOPPED: not the registered head, or the worktree is dirty; the lock was not taken")
        say("AUR FINAL LINE")
        return
    # THE WRITTEN-OUT RECORD VERSION CHECK, at the head about to be tested and before the lock: the records at run 11,
    # character 10 and account 4, and no test stating a version other than the committed file it reads. The coordinating
    # session's ruling of 2026-11-19 for this window; issue #2753 for why. A mismatch stops the window here.
    sweep = quiet_run([sys.executable, "-u", str(HERE / "version_sweep_aur.py")], capture_output=True,
                           text=True, cwd=REPO, check=False)
    for l in sweep.stdout.splitlines():
        say(f"version sweep: {l}")
    if sweep.returncode != 0:
        say(f"STOPPED: the written-out version check found a mismatch (exit {sweep.returncode}); the lock was not taken")
        say("AUR FINAL LINE")
        return
    if not lock("acquire"):
        say("STOPPED: the lock could not be taken")
        say("AUR FINAL LINE")
        return
    try:
        window(head)
    finally:
        lock("release")
        say("AUR FINAL LINE")


if __name__ == "__main__":
    main()
