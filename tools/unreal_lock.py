"""Hold and release the right to run the Unreal editor, across every worktree.

WHY THIS EXISTS. Several Claude sessions now work on this repository at the same
time, each in its own git worktree. Most of what they do is independent. Three
things are not, because they all drive one Unreal editor on one machine:

    opening the interactive editor
    running the automation tests, which launch a headless editor
    regenerating the DataTable assets, which runs inside the editor

Two sessions doing any of those at once collide. tools/unreal_build.py says so in
its own docstring for the first case: the build refuses to start while the editor
is open, because Live Coding holds the binaries.

WHAT THIS DOES NOT PROTECT, because the engine already does. Compiling is
serialised by Unreal itself. UnrealBuildTool.cs line 446 builds its lock name
from the location of the UnrealBuildTool assembly -- the engine install path, not
the project path -- and BuildMode.cs line 51 asks for that lock. Every worktree
uses the same engine, so every build takes the same lock. tools/unreal_build.py
passes -WaitMutex, which makes a second build WAIT rather than fail. That is
correct behaviour and needs nothing from this module. It is worth knowing about,
though: a build that appears to hang for minutes is usually waiting for another
session's build, not stuck.

WHERE THE LOCK FILE LIVES, and why not in a worktree. The constraint is one
editor per machine, so a lock inside any single worktree's .claude/ would be
invisible to the others. It goes in the shared git directory instead, which
git rev-parse --git-common-dir reports identically from every worktree, and which
is never committed.

WHY THE HOLDER'S PROCESS IS NOT CHECKED FOR LIFE. The obvious way to ask whether
a recorded process is still running is os.kill(pid, 0). On Windows that does not
probe anything: Python's os.kill supports only CTRL_C_EVENT and CTRL_BREAK_EVENT,
and for any other signal it calls TerminateProcess. Written the obvious way, a
liveness check would kill the holder it was asking about. So the lock records
when it was taken and status reports the age, and taking a lock away from
somebody is a separate command that makes you name them.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

#: Overrides where the lock file lives. Tests set this; nothing else should.
LOCK_PATH_VARIABLE = "CATACLYSM_UNREAL_LOCK"

#: How old a held lock must be before status calls it out as suspicious. A full
#: build and automation run is minutes, not hours, so anything past this is much
#: more likely a session that died than a session still working.
STALE_AFTER_HOURS = 3.0


def lock_path(environment: dict[str, str] | None = None) -> Path:
    """Where the lock file is, the same answer from every worktree."""
    env = os.environ if environment is None else environment
    override = env.get(LOCK_PATH_VARIABLE)
    if override:
        return Path(override)
    common = subprocess.run(
        ["git", "rev-parse", "--git-common-dir"],
        capture_output=True, text=True, check=True).stdout.strip()
    return Path(common).resolve() / "cataclysm-unreal-editor.lock"


def read_holder(path: Path) -> dict[str, str] | None:
    """Who holds the lock, or None if it is free.

    A lock file that cannot be parsed counts as held by an unknown session
    rather than as free. Treating unreadable as free is how two editors get
    opened.
    """
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {"holder": "unknown (lock file is unreadable)", "taken": ""}


def age_hours(record: dict[str, str]) -> float | None:
    """How long the lock has been held, or None if the record does not say."""
    taken = record.get("taken")
    if not taken:
        return None
    try:
        when = datetime.fromisoformat(taken)
    except ValueError:
        return None
    return (datetime.now(timezone.utc) - when).total_seconds() / 3600.0


def acquire(holder: str, path: Path) -> tuple[bool, str]:
    """Take the lock, or report who already has it.

    The file is created with O_CREAT | O_EXCL, so two sessions asking at the same
    moment cannot both succeed. Reading first and writing second would let them.
    """
    record = json.dumps({
        "holder": holder,
        "taken": datetime.now(timezone.utc).isoformat(),
        "pid": os.getpid(),
    })
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        current = read_holder(path) or {}
        return False, (
            "The Unreal editor is held by {} since {}. Wait, or ask them to run "
            "python tools/unreal_lock.py release <their name>.".format(
                current.get("holder", "unknown"),
                current.get("taken") or "an unrecorded time"))
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        handle.write(record)
    return True, "{} holds the Unreal editor.".format(holder)


def release(holder: str, path: Path) -> tuple[bool, str]:
    """Give the lock back. Only the session that took it may.

    Letting anybody release it turns the lock into a suggestion: a session that
    wrongly believes itself the holder would free an editor somebody else is
    using.
    """
    current = read_holder(path)
    if current is None:
        return False, "The Unreal editor lock is already free."
    if current.get("holder") != holder:
        return False, (
            "{} does not hold the lock; {} does. To take it anyway: "
            "python tools/unreal_lock.py steal {} --from {}".format(
                holder, current.get("holder"), holder, current.get("holder")))
    path.unlink()
    return True, "{} released the Unreal editor.".format(holder)


def steal(holder: str, expected: str, path: Path) -> tuple[bool, str]:
    """Take the lock from a session that is gone.

    Naming the current holder is the whole safeguard. It fails if they are not
    who you thought, which is the case where you are about to interrupt somebody
    still working.
    """
    current = read_holder(path)
    if current is None:
        return acquire(holder, path)
    if current.get("holder") != expected:
        return False, (
            "Refusing to steal: you named {}, but {} holds it. Check again "
            "before taking it.".format(expected, current.get("holder")))
    path.unlink()
    return acquire(holder, path)


def status(path: Path) -> str:
    """One line saying whether the editor is free, and for how long if not."""
    current = read_holder(path)
    if current is None:
        return "The Unreal editor is free."
    hours = age_hours(current)
    if hours is None:
        return "Held by {} (start time not recorded).".format(current.get("holder"))
    warning = ""
    if hours >= STALE_AFTER_HOURS:
        warning = (
            " That is over {:g} hours, which is longer than any build and test "
            "run takes. The session may be gone; confirm before stealing "
            "it.".format(STALE_AFTER_HOURS))
    return "Held by {} for {:.1f} hours.{}".format(
        current.get("holder"), hours, warning)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("acquire", "release"):
        one = sub.add_parser(name)
        one.add_argument("holder", help="a name for your session")
    stealer = sub.add_parser("steal")
    stealer.add_argument("holder", help="a name for your session")
    stealer.add_argument("--from", dest="expected", required=True,
                         help="who you believe currently holds it")
    sub.add_parser("status")

    args = parser.parse_args(argv)
    path = lock_path()

    if args.command == "status":
        print(status(path))
        return 0
    if args.command == "acquire":
        ok, message = acquire(args.holder, path)
    elif args.command == "release":
        ok, message = release(args.holder, path)
    else:
        ok, message = steal(args.holder, args.expected, path)
    print(message)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
