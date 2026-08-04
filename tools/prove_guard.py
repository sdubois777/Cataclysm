"""Break a file, run a command, and put the file back — without a stale cache.

WHY THIS EXISTS. `CLAUDE.md` says a check that cannot fail is worthless, so a
new guard has to be shown failing when the condition it guards against is
present. The way to do that is to break the thing it guards, run the tests,
confirm they fail, and write the original bytes back. Doing that by hand is
where two traps live, and both produce *misleading evidence* rather than an
obvious error.

**The stale bytecode trap (issue #159).** Python decides whether a `.pyc` in
`__pycache__` is current from the source file's modification time and size. A
break-and-restore usually changes neither: writing `0.0` where `5.0` was keeps
the length identical, and the restore lands within the filesystem's modification
time granularity. So the next run imports the compiled copy of the BROKEN
module even though the source on disk is correct.

That is not a stale run that fails visibly. While verifying the guards for issue
#155, five checks were broken in sequence; the fifth broke a row for the
**Ultimate** slot and the test failed naming the **Aura** slot, because the run
loaded the fourth case's compiled module. The failure was real, was attributed
to the wrong break, and was nearly reported as evidence that a guard fires when
a different guard was firing for a different reason. Running the same script
twice gave two different answers, which is the only reason it was noticed.

**The unrestored file trap.** A script that breaks a file and then raises leaves
the repository broken. Every restore here happens in a `finally`, so an
exception, a failing command and a keyboard interrupt all put the files back.

WHAT THIS DOES NOT DO. It does not run the Unreal build. Issue #139 records the
same class of problem for compiled C++ — restoring a source file with a
preserved timestamp leaves the stale binary in place — and the fix there is to
rebuild, which is slow enough that a caller should drive it explicitly rather
than have this module hide it.

## Using it

    from tools.prove_guard import break_and_run

    result = break_and_run(
        {"sim/cataclysm_sim/character.py": lambda text: text.replace("5.0", "0.0")},
        ["python", "-m", "pytest", "sim/tests/test_character.py", "-q"],
    )
    print(result.summary)      # the last line pytest printed
    assert result.failed       # the guard noticed

The file is restored byte for byte before `break_and_run` returns, whatever the
command did.
"""

from __future__ import annotations

import dataclasses
import os
import pathlib
import shutil
import subprocess
import sys
from collections.abc import Callable, Iterable, Mapping, Sequence

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

#: Where compiled Python is cached. Deleting these is what makes a restored
#: module actually be re-read rather than re-imported from a stale copy.
CACHE_DIRECTORY = "__pycache__"

#: Directories a cache sweep never descends into. Deleting a virtual
#: environment's caches would be slow and pointless.
SKIPPED = {".git", ".venv", "venv", "node_modules", "Binaries", "Intermediate",
           "DerivedDataCache", "Saved"}


@dataclasses.dataclass(frozen=True)
class GuardResult:
    """What the command did while the files were broken."""

    returncode: int
    stdout: str
    stderr: str

    @property
    def failed(self) -> bool:
        """Whether the command reported a failure, which is what proves a guard."""
        return self.returncode != 0

    @property
    def summary(self) -> str:
        """The last line of output, which for pytest is its result line."""
        lines = [line for line in (self.stdout + self.stderr).splitlines() if line.strip()]
        return lines[-1].strip() if lines else "(no output)"


def clear_bytecode_caches(root: pathlib.Path | None = None) -> int:
    """Delete every `__pycache__` under `root`. Returns how many were removed."""
    root = root or REPO_ROOT
    removed = 0
    for directory, subdirectories, _ in os.walk(root):
        subdirectories[:] = [
            name for name in subdirectories
            if name not in SKIPPED and name != CACHE_DIRECTORY
        ]
        cache = pathlib.Path(directory) / CACHE_DIRECTORY
        if cache.is_dir():
            shutil.rmtree(cache, ignore_errors=True)
            removed += 1
    return removed


def run_without_bytecode(command: Sequence[str],
                         env: Mapping[str, str] | None = None) -> GuardResult:
    """Run a command with bytecode writing off and caches already cleared.

    Both halves are needed. Clearing the caches removes what is already stale;
    `PYTHONDONTWRITEBYTECODE` stops the run itself writing a new cache that the
    NEXT case in a sequence would then read.
    """
    clear_bytecode_caches()

    environment = {**os.environ, **(env or {}), "PYTHONDONTWRITEBYTECODE": "1"}

    # `-B` as well as the variable, because a caller may pass an interpreter
    # path rather than sys.executable and the two are read at different points.
    argv = list(command)
    if argv and pathlib.Path(argv[0]).stem.startswith("python") and "-B" not in argv:
        argv.insert(1, "-B")

    completed = subprocess.run(argv, capture_output=True, text=True,
                               cwd=REPO_ROOT, env=environment, check=False)
    return GuardResult(completed.returncode, completed.stdout, completed.stderr)


def break_and_run(edits: Mapping[str, Callable[[str], str]],
                  command: Sequence[str],
                  env: Mapping[str, str] | None = None) -> GuardResult:
    """Apply each edit, run the command, and restore every file.

    @param edits    path relative to the repository root, and a function taking
                    the file's text and returning the broken text. An edit that
                    changes nothing raises, because a break that did not break
                    anything would produce a passing run and look like a
                    worthless guard.
    @param command  what to run while the files are broken
    """
    if not edits:
        raise ValueError("break_and_run needs at least one edit to make.")

    originals: dict[pathlib.Path, bytes] = {}
    try:
        for relative, edit in edits.items():
            path = REPO_ROOT / relative
            if not path.is_file():
                raise FileNotFoundError(f"{relative} does not exist.")

            originals[path] = path.read_bytes()
            before = path.read_text(encoding="utf-8")
            after = edit(before)
            if after == before:
                raise ValueError(
                    f"The edit to {relative} changed nothing. A break that does "
                    "not break anything makes a working guard look worthless.")
            path.write_text(after, encoding="utf-8")

        return run_without_bytecode(command, env)
    finally:
        # IN A FINALLY, so a raising edit, a crashing command and an interrupt
        # all leave the repository as they found it.
        for path, content in originals.items():
            path.write_bytes(content)
        clear_bytecode_caches()


def prove_each(cases: Iterable[tuple[str, Mapping[str, Callable[[str], str]]]],
               command: Sequence[str]) -> list[tuple[str, GuardResult]]:
    """Run `break_and_run` once per case and collect what each printed.

    For the common shape: several guards, one command, and a report of which
    case made it fail. Each case is restored before the next one starts, so one
    case cannot be attributed to another.
    """
    return [(label, break_and_run(edits, command)) for label, edits in cases]


def main(argv: Sequence[str] | None = None) -> int:
    """Clear the caches from the command line, for a one-off."""
    argv = list(argv if argv is not None else sys.argv[1:])
    if argv and argv[0] != "clear-caches":
        print(f"usage: python {pathlib.Path(__file__).name} [clear-caches]",
              file=sys.stderr)
        return 2

    removed = clear_bytecode_caches()
    print(f"Removed {removed} {CACHE_DIRECTORY} directories under {REPO_ROOT}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
