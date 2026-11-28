"""Window aur-w1's C++ guard proof: ONE proof, G, the three breaks of aur_w1_cpp_breaks.py in one call (the
coordinating session's ruling of 2026-11-27: one call costs four builds, three would cost twelve).

    python -u prove_fr.py --dry-run          # apply the combined break to the text only; every anchor matches once
    python -u prove_fr.py W                  # the proof: four builds, both halves' engine logs kept
    python -u prove_fr.py W --anchors-only   # the anchor counts and the prefix, then stop

ADAPTED FROM THE DEATH WINDOW'S prove_death3.py (in death3_src/), whose output run_fr.py reads: the anchor counts, the
BEFORE and AFTER hashes, the summary, crashed, proved, named failures and the FINAL LINE. THE ENGINE LOG OF EACH HALF IS
KEPT as fr_X_run1.log (broken) and fr_X_run2.log (restored). AN UNPROVED PROOF IS REPORTED, NOT RAISED.

THE BREAKS ARE NOT WRITTEN HERE. They are `combined()` of C:\\ail\\.claude\\windows\\fr_w1_cpp_breaks.py, loaded by
path: {repo-relative path: function(text) -> text}, breaks a to e applied in that order. ANCHORS ARE COUNTED ON THE
TEXT AS prove_cpp_guard READS IT (universal newlines, CRLF to LF), one line per anchor, each of the module's
A_OLD, B_OLD, C_OLD, D_OLD_1, D_OLD_2, D_OLD_3 and E_OLD in the file its break edits; every count must be 1.

THE PREDICTION. `G_PREDICTED` holds the short test names the session predicts the combined break fails, and is printed
beside the named failures with whether the two are the same set. IT DOES NOT CHANGE `proved`, which is the engine
wrapper's own verdict (a named test failed with the breaks in, and nothing failed with them out): a prediction missed
or exceeded is for the session to read, and a break whose prediction a combined run did not catch is rerun alone before
anything is concluded. A test counted as caught may have been failed by another break in the same run.
"""
from __future__ import annotations

import hashlib
import importlib.util
import os
import pathlib
import shutil
import subprocess
import sys
import traceback

HERE = pathlib.Path(__file__).resolve().parent
# THIS FOLDER IS .claude/windows/fr_w1, three levels below the worktree. run_fr.py passes REPO in the environment.
REPO = pathlib.Path(os.environ.get("REPO", str(HERE.parents[2])))
sys.path.insert(0, str(REPO / "tools"))
BREAKS_FILE = REPO / ".claude" / "windows" / "aur_w1_cpp_breaks.py"

# ------------------------------------------------------------------------------- FILLED IN BY THE SESSION ----------
# THE SESSION NARROWS THIS once it has predicted the failures: the groups the predicted tests sit in, each ending in a
# dot and joined with "+" (e.g. "Cataclysm.Ailments.+Cataclysm.EnergyShield."), which `Automation RunTests` accepts
# and the wrapper passes through. "Cataclysm." runs the whole Unreal suite twice, which is correct but slow.
# THE NINE GROUPS THE PREDICTIONS FALL IN, each with its trailing dot, joined with "+" (the coordinating session's
# instruction of 2026-11-26; `run_automation_tests` passes the prefix unchanged to "Automation RunTests", which takes
# several filters joined with "+", as the Death window's proof F did).
G_PREFIX = "Cataclysm.AuraReservation.+Cataclysm.NoCaps.+Cataclysm.Skills.AnAuraReservesHealth"
# EACH HALF'S REGISTERED PERFORMED COUNT, from the registration strings on 7bc095034: 13 Cataclysm.AuraReservation.,
# 6 Cataclysm.NoCaps., and the one Skills test whose name begins Cataclysm.Skills.AnAuraReservesHealth.
G_REGISTERED = 20
G_PREDICTED: tuple[str, ...] = (
    "TheSummedReductionStopsAtSeventyFivePercentAndTheFloorIsOne",
    "NoReductionTakesAReserveBelowOnePoint",
    "ALevelGainedLowersAHeldAurasReservationAtOnce",
    "AReductionTakenOffRaisesAHeldAurasReservationAtOnceAndHoldsTheMana",
    "AReservationThatNoLongerFitsTheWholeMaximumSwitchesTheAuraOff",
    "RoomIsMeasuredAgainstTheMaximumAndNotTheManaInHand",
    "TwoAurasFitAPoolOfAHundredAndAThirdDoesNot",
    "AnAuraReservesHealthWhenTheManaPoolBecameHealth",
)
# ---------------------------------------------------------------------------------------------------------------------

#: The anchors of each break, by the breaks module's constant names, in the order the module applies the breaks.
ANCHORS_BY_BREAK = {"a": ("A_OLD",), "b": ("B_OLD",), "c": ("C_OLD",)}

NO_WINDOW = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def load_breaks():
    spec = importlib.util.spec_from_file_location("aur_w1_cpp_breaks", BREAKS_FILE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read(file: str) -> str:
    """The file's text as prove_cpp_guard hands it to an edit: universal newlines, so "\\n" endings."""
    return (REPO / file).read_text(encoding="utf-8")


def anchors(breaks) -> list[tuple[str, str, str, int]]:
    """(break, constant, file, count) for every anchor, counted on the file's text before any break."""
    found = []
    for name, constants in ANCHORS_BY_BREAK.items():
        files = list(breaks.BREAKS[name])
        assert len(files) == 1, f"break {name} edits {files}; this table expects one file per break"
        for constant in constants:
            found.append((name, constant, files[0], read(files[0]).count(getattr(breaks, constant))))
    assert set(ANCHORS_BY_BREAK) == set(breaks.BREAKS), (sorted(ANCHORS_BY_BREAK), sorted(breaks.BREAKS))
    return found


def report_anchors(breaks) -> bool:
    ok = True
    for name, constant, file, count in anchors(breaks):
        print(f"G anchor count {count}: ({name}) {constant} {file} {getattr(breaks, constant)[:90]!r}", flush=True)
        ok = ok and count == 1
    return ok


def dry_run() -> None:
    breaks = load_breaks()
    if not report_anchors(breaks):
        raise SystemExit("G DRY RUN FAILED: an anchor does not match exactly once; nothing written")
    # THE COMBINED BREAK, IN MEMORY ONLY: each file's chain must apply (every anchor once, on the text as it then
    # stands) and change the text.
    for file, edit in breaks.combined().items():
        before = read(file)
        after = edit(before)
        assert after != before, f"the combined break changed nothing in {file}"
        print(f"G combined {file}: applies cleanly, {len(before)} -> {len(after)} characters", flush=True)
    for line in breaks.dry_run():
        print(f"G module dry run: {line}", flush=True)
    print(f"G prefix: {G_PREFIX}; predicted {len(G_PREDICTED)}: {sorted(G_PREDICTED)}", flush=True)
    print("G DRY RUN OK", flush=True)


def source_hash() -> str:
    dirty = subprocess.run(["git", "-C", str(REPO), "status", "--porcelain", "--", "game"],
                           capture_output=True, text=True, check=True, creationflags=NO_WINDOW).stdout
    tree = subprocess.run(["git", "-C", str(REPO), "rev-parse", "HEAD:game"],
                          capture_output=True, text=True, check=True, creationflags=NO_WINDOW).stdout.strip()
    digest = hashlib.sha256()
    for path in sorted((REPO / "game" / "Source").rglob("*")):
        if path.is_file():
            digest.update(path.read_bytes())
    return f"game tree {tree}, source sha256 {digest.hexdigest()[:16]}, dirty={dirty.strip()!r}"


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    name = sys.argv[1] if len(sys.argv) > 1 else "--dry-run"
    if name == "--dry-run":
        dry_run()
        return
    if name != "G":
        raise SystemExit(f"unknown proof {name!r}: the only proof is G (or --dry-run)")
    breaks = load_breaks()
    if not report_anchors(breaks):
        raise SystemExit("G: an anchor does not match exactly once; nothing run")
    edits = breaks.combined()
    print(f"G prefix: {G_PREFIX}; files {sorted(edits)}; predicted {len(G_PREDICTED)}: {sorted(G_PREDICTED)}",
          flush=True)
    if len(sys.argv) > 2 and sys.argv[2] == "--anchors-only":
        return

    import unreal_build  # noqa: E402 -- only a real proof needs the engine wrapper

    print(f"G BEFORE: {source_hash()}", flush=True)
    runs = []

    def tester(test_prefix):
        outcome = unreal_build.run_automation_tests(test_prefix)
        runs.append(outcome)
        copy = HERE / f"aur_G_run{len(runs)}.log"
        shutil.copyfile(unreal_build.TEST_LOG, copy)
        print(f"G run {len(runs)} ({'broken' if len(runs) == 1 else 'restored'}) log copied to {copy.name}",
              flush=True)
        return outcome

    proved = False
    try:
        result = unreal_build.prove_cpp_guard(edits, test_prefix=G_PREFIX, tester=tester)
        print(f"G summary: {result.summary}", flush=True)
        if not result.build.succeeded:
            kept = HERE / "aur_G_failed_build.log"
            kept.write_text(result.build.stdout, encoding="utf-8")
            print(f"G failed build output kept in {kept.name}", flush=True)
        restored_crashed = result.tests_restored.crashed if result.tests_restored is not None else None
        print(f"G crashed: {result.crashed or bool(restored_crashed)} (broken {result.crashed}, restored "
              f"{restored_crashed})", flush=True)
        named = list(result.named_failures)
        proved = bool(result.proved)
        print(f"G proved: {proved}", flush=True)
        print(f"G named failures: {named}", flush=True)
        for half, outcome in (("broken", result.tests), ("restored", result.tests_restored)):
            performed = outcome.performed if outcome is not None else None
            print(f"G {half} performed: {performed}, registered {G_REGISTERED}: "
                  f"{'MATCH' if performed == G_REGISTERED else 'MISMATCH'}", flush=True)
        # THE PREDICTION, beside the verdict and not part of it.
        print(f"G predicted: {sorted(G_PREDICTED)}", flush=True)
        print(f"G predicted match: {set(G_PREDICTED) == set(named)} (predicted not failed "
              f"{sorted(set(G_PREDICTED) - set(named))}; failed not predicted {sorted(set(named) - set(G_PREDICTED))})",
              flush=True)
    except Exception:  # noqa: BLE001 -- reported, and the runner stops on a run that did not report a verdict
        print("G EXCEPTION:", flush=True)
        print(traceback.format_exc(), flush=True)
    print(f"G AFTER: {source_hash()}", flush=True)
    print("G FINAL LINE", flush=True)
    raise SystemExit(0 if proved else 1)


if __name__ == "__main__":
    main()
