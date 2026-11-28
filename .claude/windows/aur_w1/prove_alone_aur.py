"""Window aur-w1's C++ guard proof, run on its own after the window, as the coordinating session ruled on 2026-11-27: the
window's run stopped before the proof only because its script misread a single group's result (it now reads the
`Tests:` line when one group runs alone). ONE combined call of prove_aur.py G, on the tested head 87e2124f9, under the
lock aur-w1, released in a `finally`.

python start_detached.py prove_alone_aur.py aur_proof.out aur_proof.err aur_proof.pid

Prints "AUR PROOF FINAL LINE" at the end, whatever happened.
"""
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = str(HERE.parents[2])
TESTED = "87e2124f"
LOCK_NAME = "aur-w1"
NO_WINDOW = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def say(text):
    print(text, flush=True)


def git(*args):
    done = subprocess.run(["git", "-C", REPO, *args], capture_output=True, text=True, check=False,
                          creationflags=NO_WINDOW)
    return (done.stdout + done.stderr).strip()


def lock(action):
    done = subprocess.run([sys.executable, "tools/unreal_lock.py", action, LOCK_NAME], capture_output=True, text=True,
                          cwd=REPO, check=False, creationflags=NO_WINDOW)
    say(f"lock {action}: exit {done.returncode} {(done.stdout + done.stderr).strip()[:300]}")
    return done.returncode == 0


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    head = git("rev-parse", "--short=8", "HEAD")
    dirty = git("status", "--porcelain", "--untracked-files=no")
    say(f"head {head}, tested {TESTED}, tracked tree dirty={dirty!r}")
    if head != TESTED or dirty:
        say("STOPPED: not the tested head, or the tracked tree is dirty; the lock was not taken")
        say("AUR PROOF FINAL LINE")
        return
    if not lock("acquire"):
        say("STOPPED: the lock could not be taken")
        say("AUR PROOF FINAL LINE")
        return
    try:
        log = HERE / f"aur_G_{TESTED}.log"
        with log.open("w", encoding="utf-8") as out:
            code = subprocess.run([sys.executable, "-u", str(HERE / "prove_aur.py"), "G"], stdout=out,
                                  stderr=subprocess.STDOUT, cwd=str(HERE), env=dict(os.environ, REPO=REPO),
                                  check=False, creationflags=NO_WINDOW).returncode
        text = log.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if re.match(r"^G (anchor count|prefix|BEFORE|AFTER|summary|crashed|proved|named failures|broken performed|"
                        r"restored performed|predicted|predicted match|run \d|EXCEPTION|FINAL LINE)", line):
                say(line[:900])
        before = re.search(r"^G BEFORE: (.*)$", text, re.M)
        after = re.search(r"^G AFTER: (.*)$", text, re.M)
        say(f"proof exit {code}; source hash same before and after: "
            f"{bool(before and after and before.group(1) == after.group(1))}")
        say(f"head after: {git('rev-parse', '--short=8', 'HEAD')}; dirty "
            f"{git('status', '--porcelain', '--untracked-files=no')!r}")
    finally:
        lock("release")
        say("AUR PROOF FINAL LINE")


if __name__ == "__main__":
    main()
