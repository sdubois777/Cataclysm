"""Window aur-w1 end to end: the prechecks until they pass (retried, as a build of another session's may be ending),
the three compiles (compile_aur.py, its own lock), the prechecks again, then the run (run_aur.py, the window's lock).
Every child starts with no console window. Prints "AUR CHAIN FINAL LINE" at the end, whatever happened.

python start_detached.py chain_aur.py aur_chain.out aur_chain.err aur_chain.pid
"""
import datetime
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
HEAD = "4b19e69d8"
NO_WINDOW = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
TRIES = 40
WAIT_SECONDS = 60


def say(text):
    print(f"{datetime.datetime.now().isoformat(timespec='seconds')} {text}", flush=True)


def step(script, out_name, *args):
    with (HERE / out_name).open("w", encoding="utf-8") as out:
        code = subprocess.run([sys.executable, "-u", str(HERE / script), *args], stdout=out, stderr=subprocess.STDOUT,
                              cwd=str(HERE), creationflags=NO_WINDOW, check=False).returncode
    lines = (HERE / out_name).read_text(encoding="utf-8", errors="replace").splitlines()
    return code, lines


def prechecks_until_pass(label):
    for attempt in range(1, TRIES + 1):
        code, lines = step("prechecks_aur.py", "aur_prechecks_run.out")
        last = lines[-1] if lines else "(no output)"
        say(f"prechecks {label}, try {attempt}: {last}")
        if code == 0 and last == "PRECHECKS: ALL PASS":
            return True
        time.sleep(WAIT_SECONDS)
    return False


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    try:
        if not prechecks_until_pass("before the compiles"):
            say("STOPPED: the prechecks did not pass")
            return
        code, lines = step("compile_aur.py", "aur_compile.out", HEAD)
        for line in lines:
            if line.startswith(("build ", "head ", "STOPPED", "ALL THREE", "lock ")) and "ubt" not in line:
                say(f"compile: {line[:300]}")
        if "ALL THREE BUILDS SUCCEEDED" not in lines:
            say("STOPPED: the three compiles did not all succeed; see aur_compile.out")
            return
        if not prechecks_until_pass("before the run"):
            say("STOPPED: the prechecks did not pass before the run")
            return
        code, lines = step("run_aur.py", "aur_run.out")
        say(f"run: exit {code}; last line {lines[-1] if lines else '(none)'}; see aur_run.out")
    finally:
        say("AUR CHAIN FINAL LINE")


if __name__ == "__main__":
    main()
