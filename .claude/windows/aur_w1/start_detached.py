"""Start a window script in the background with ONE HIDDEN CONSOLE, so no console window appears for anyone to close,
and record its process ID. Copied from sharp-meitner-246682's start_detached.py (the skills session's), unchanged in
what it does.

As the coordinating session ruled on 2026-11-19: the rebalance session's runner, started with DETACHED_PROCESS, gave each
console program it started a visible window of its own, and closing one ended that run twice ("Engine exit requested
(reason: ConsoleCtrl RequestExit)"). CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP gives the runner and everything it
starts one hidden console, and the runner goes on after this session ends.

python start_detached.py <script.py> <stdout file> <stderr file> <pid file> [script arguments...]
"""
import datetime
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent


def main() -> None:
    script, out, err, pid_file, *rest = sys.argv[1:]
    flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    with open(HERE / out, "wb") as stdout, open(HERE / err, "wb") as stderr:
        started = subprocess.Popen([sys.executable, "-u", str(HERE / script), *rest], cwd=str(HERE), stdout=stdout,
                                   stderr=stderr, stdin=subprocess.DEVNULL, creationflags=flags, close_fds=True)
    line = f"PID {started.pid} started {datetime.datetime.now().isoformat(timespec='seconds')}: {script} {' '.join(rest)}"
    (HERE / pid_file).write_text(line + "\n", encoding="utf-8")
    print(line, flush=True)


if __name__ == "__main__":
    main()
