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

**And a restore the operating system refuses (2026-11-25).** Another process
holding the file for a moment -- one with it memory-mapped makes `write_bytes`
raise OSError 22 -- used to leave the break in place, raise out of the restore
and say nothing about which file. Each write back is now tried six times over
3.1 seconds and read back (`write_back`); a file still refused after that raises
`FilesLeftBroken`, which names it and says how to put it back.

**The busy-checkout trap (issue #598).** This breaks the REAL repository for the
duration of the command. Anything else working in the same checkout at that
moment — a second test run, an editor, another agent — can read the broken text
or write over it. On 2026-08-14 that produced five failures in tests that had
nothing to do with the break, all of which passed on an immediate rerun, and the
failures pointed at innocent guards in the design document.

It is now detected rather than avoided: the bytes written before the command are
compared against the file afterwards, and a mismatch sets `GuardResult.disturbed`
and makes `failed` answer False whatever the exit code was. **A run whose files
moved underneath it did not test what it was asked to test**, and a failure from
one of those is not evidence that a guard fires. `result.summary` says so in
those words.

**THAT DETECTION ONLY WORKS IN ONE DIRECTION (issue #1429).** It catches another
process *writing* over the broken file. **It cannot catch one *reading* it**,
because a reader changes nothing: the bytes still match, `disturbed` stays empty,
and this reports a clean result. **The damage lands in the other process rather
than in the proof**, which is why it is worth a paragraph rather than a flag.

On 2026-09-07 an 84,000 campaign measurement was running in the same worktree,
fanned out across worker subprocesses that re-execute their script and import
`cataclysm_sim` every few seconds for an hour. A guard proof started in that
window would have had a worker import a deliberately broken model, measure a
different game, and print a number in the same shape as every other cell.
**Nothing anywhere would have said so** -- not `disturbed`, not the worker's exit
code, not the report.

**So run the proof in a copy whenever anything else in this worktree imports the
code you are about to break** -- a second test run, a long measurement, another
session. `REPO_ROOT` above is derived from *this module's own file location*, so
a copy breaks itself and leaves the live worktree alone. **That is why the recipe
works, and it is the half to remember when your layout differs:**

    git archive HEAD | tar -x -C <an empty directory>
    cd <that directory> && python your_proof_script.py

`git archive HEAD` gives the committed state, so commit first -- which a guard
proof wants anyway. Afterwards the live worktree should be untouched: `git
status` empty, and nothing to restore.

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
command did -- or, if it cannot be written back, `FilesLeftBroken` is raised
naming it.
"""

from __future__ import annotations

import dataclasses
import errno
import re
import os
import pathlib
import shutil
import subprocess
import sys
import time
from collections.abc import Callable, Mapping, Sequence

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

#: Where compiled Python is cached. Deleting these is what makes a restored
#: module actually be re-read rather than re-imported from a stale copy.
CACHE_DIRECTORY = "__pycache__"

#: Directories a cache sweep never descends into. Deleting a virtual
#: environment's caches would be slow and pointless.
SKIPPED = {".git", ".venv", "venv", "node_modules", "Binaries", "Intermediate",
           "DerivedDataCache", "Saved"}


#: Text that only a pytest run prints, whether it finished or died collecting.
#:
#: CHOSEN AGAINST REAL OUTPUT, NOT GUESSED. The first attempt looked for
#: "collected" and the session-starts banner, and a collection error under `-q`
#: -- which `pyproject.toml` sets in `addopts` -- prints NEITHER, so the crash
#: fell through to the exit code and read exactly as it did before the fix. The
#: markers below are taken from the captured output of both cases.
TEST_RUN_MARKERS = (
    "short test summary info",
    "error during collection",
    "collected",
    "test session starts",
)

#: pytest's "N failed" count, from its result line. Zero is written out as
#: "0 failed" only when something else failed too, so any match means a test
#: reached its assertions and did not pass.
FAILED_COUNT = re.compile(r"\b([1-9]\d*) failed\b")

#: THE WAITS BETWEEN TRIES WHEN WRITING A BROKEN FILE BACK IS REFUSED: six tries
#: and at most 3.1 seconds of waiting, for each file.
#:
#: MEASURED 2026-11-25 ON THIS PROJECT'S WINDOWS MACHINE, with a scratch file.
#: While another process has the file MEMORY-MAPPED -- a virus scanner or an
#: indexer may, for a moment -- `write_bytes` raises OSError 22, "Invalid
#: argument", the error a whole-suite run met writing
#: `docs/Cataclysm_GDD_v2.md` back after a guard case broke it. While another
#: process holds a byte-range lock it raises PermissionError 13. Both cleared as
#: soon as the holder let go, a 1.5 second hold included, and the next write
#: succeeded with the content intact. Writing the 15 MB `docs/DECISIONS.md` back
#: and reading it again takes about 12 ms, so the waits, not the writes, are
#: the cost.
RESTORE_WAITS_SECONDS: tuple[float, ...] = (0.1, 0.2, 0.4, 0.8, 1.6)


def write_back(path: pathlib.Path, content: bytes, *,
               then: Callable[[], None] | None = None) -> OSError | None:
    """Write `content` to `path` and read it back, trying again when refused.

    Returns None once the file holds exactly `content` and `then`, when given,
    has run. Returns the last error once every try has failed: one, then one
    more after each wait in `RESTORE_WAITS_SECONDS`.

    IT RETURNS THE ERROR RATHER THAN RAISING IT, so a caller restoring several
    files goes on to the next one. A restore loop that raised on its first
    refused write left every later file broken as well, and replaced the
    failure that explained the run.

    READ BACK, BECAUSE A WRITE CAN FAIL PART WAY. A file opened for writing is
    emptied first, so a write refused after that leaves less than it found.

    @param then  run after a verified write, inside the same tries: the C++
                 helper pushes the modification time forward here, and a
                 refusal of that is tried again too.
    """
    last: OSError | None = None
    for wait in (*RESTORE_WAITS_SECONDS, None):
        try:
            path.write_bytes(content)
            if path.read_bytes() != content:
                raise OSError(errno.EIO, "the file read back is not what was written",
                              str(path))
            if then is not None:
                then()
            return None
        except OSError as error:
            last = error
        if wait is not None:
            time.sleep(wait)
    return last


def files_left_broken(paths: Sequence[str], how_to_restore: str | None = None) -> str:
    """The sentence naming every file a break could not be written back to.

    @param how_to_restore  what to run; `git checkout -- <path>` for each file
                           when not given.
    """
    if how_to_restore is None:
        how_to_restore = ", ".join(f"`git checkout -- {path}`" for path in paths)
    tries = len(RESTORE_WAITS_SECONDS) + 1
    return (f"FILES LEFT BROKEN: {', '.join(paths)} could not be written back "
            f"after {tries} tries over {sum(RESTORE_WAITS_SECONDS):.1f} s, so the "
            "break is still in it, and anything that reads it now reads the "
            f"broken text. Restore it before anything else runs: {how_to_restore}. "
            "This run is not a proof.")


class FilesLeftBroken(RuntimeError):
    """A break could not be written back, so the repository still holds it.

    RAISED RATHER THAN RETURNED, ON PURPOSE. A caller that asserts on
    `named_failures`, as `CLAUDE.md` tells one with no second half to do, would
    pass on a returned result whose file was still broken, and the next case
    would fail against the broken file with nothing saying why. That is what a
    whole-suite run met on 2026-11-25: a guard case in
    `tools/tests/test_what_a_run_ending_costs.py` broke the design document, its
    restore was refused, and the break stayed in.

    `paths` names every file still broken, relative to the repository root.
    `result` is what the run measured, with `unrestored` set, so `proved` is
    False and `summary` says the same; None when there was nothing to report.
    """

    def __init__(self, message: str, paths: Sequence[str], result: object | None = None) -> None:
        super().__init__(message)
        self.paths = tuple(paths)
        self.result = result


def files_left_broken_error(refused: Mapping[str, OSError], how_to_restore: str | None = None,
                            result: object | None = None) -> FilesLeftBroken:
    """`FilesLeftBroken` for these files, its message ending with each file's last error."""
    errors = "; ".join(f"{path}: {error}" for path, error in refused.items())
    message = f"{files_left_broken(list(refused), how_to_restore)} The last error: {errors}."
    return FilesLeftBroken(message, list(refused), result)


@dataclasses.dataclass(frozen=True)
class GuardResult:
    """What the command did while the files were broken."""

    returncode: int
    stdout: str
    stderr: str
    #: Paths whose bytes changed underneath the run, so the result cannot be
    #: trusted. Empty on an ordinary run. See `break_and_run`.
    disturbed: tuple[str, ...] = ()

    #: The same command run again with the files put back, which is the other
    #: half of a guard proof. None when there is no second half.
    #:
    #: THIS FIELD MUST ALWAYS BE None ON THE INNER RESULT, and that asymmetry is
    #: deliberate rather than an oversight somebody should tidy. Both halves of a
    #: Python guard proof are a `GuardResult`, so an inner one carrying its own
    #: `restored` would nest without end. The C++ side has no such rule because
    #: `CppGuardResult` carries a `TestOutcome` -- two different types, so it
    #: stops after one level on its own.
    #:
    #: None MEANS "THERE IS NO SECOND HALF", and it must not be given a second
    #: meaning. A restored run that was attempted and failed has to be
    #: distinguishable from one that was never attempted, or this reproduces
    #: issue #1657's defect -- one value standing for two states -- inside the
    #: function written to stop half-proofs.
    restored: "GuardResult | None" = None

    #: Why the caller says a second run is unnecessary, when they opted out.
    #:
    #: A SENTENCE RATHER THAN A FLAG, ON PURPOSE. A boolean can be set without
    #: thought and reviewed without thought. A required sentence puts the reason
    #: at the call site where a reviewer meets a claim they can check, and makes
    #: anybody adding a third opt-out write one -- which is where they find out
    #: they do not have one.
    restored_half_supplied_by: str | None = None

    #: Files the break could not be written back to, relative to the repository
    #: root, so they still hold it. Set only on the result a `FilesLeftBroken`
    #: carries; `break_and_run` raises rather than returning one, so a caller
    #: asserting on `named_failures` cannot pass over it.
    unrestored: tuple[str, ...] = ()

    @property
    def named_failures(self) -> tuple[str, ...]:
        """The tests the run reported as failed, by name.

        ASSERT ON THIS RATHER THAN ON `failed`. It says what a guard proof means
        -- these named tests noticed the break -- where `failed` says only that
        the command exited non-zero, which a crash also does.

        Read from pytest's short summary, the `FAILED <test> - <reason>` lines.
        Empty for a run that named none, including one that never got as far as
        running a test.
        """
        out = []
        for line in (self.stdout + self.stderr).splitlines():
            if line.startswith("FAILED "):
                out.append(line[len("FAILED "):].split(" - ")[0].strip())
        return tuple(out)

    @property
    def looks_like_a_test_run(self) -> bool:
        """Whether the output is pytest's, so the absence of a failure means
        something.

        `break_and_run` takes ANY command. One that is not a test runner has no
        failures to report and its exit code is all there is to go on, so this
        decides which rule `failed` applies. `collected` appears in pytest's
        header and in its collection errors alike, which is exactly the case
        that has to be caught.
        """
        text = self.stdout + self.stderr
        return any(mark in text for mark in TEST_RUN_MARKERS)

    @property
    def reported_a_failing_test(self) -> bool:
        """Whether any test reached its assertions and failed.

        TWO INDEPENDENT SIGNALS, because either alone is too narrow. The named
        `FAILED <test>` lines come from pytest's short summary, which can be
        suppressed; the "N failed" count comes from its result line, which a
        crashed run never prints. Requiring both would call a real failure a
        crash -- issue #1313's mistake in the other direction.
        """
        if self.named_failures:
            return True
        return bool(FAILED_COUNT.search(self.stdout + self.stderr))

    @property
    def crashed(self) -> bool:
        """A test run that exited non-zero without any test failing.

        THIS IS THE HOLE ISSUE #1314 RECORDS. A break that stops a module
        importing gives pytest a collection error and exit code 2, no test is
        ever run, and the old `failed` reported the guard as having fired --
        putting a worthless guard into the record with a proof attached.

        Measured: a real guard break exits 1 and names its failures; adding
        `import nonexistent_module_xyz` to the module under test exits 2, prints
        "1 error" and names none. Both used to read the same.
        """
        return (self.returncode != 0
                and self.looks_like_a_test_run
                and not self.reported_a_failing_test)

    @property
    def failed(self) -> bool:
        """Whether the command reported a failure, which is what proves a guard.

        FALSE WHEN THE RUN WAS DISTURBED, whatever the exit code. A run whose
        files moved underneath it did not test what it was asked to test, and a
        failure from one of those is not evidence a guard fires.

        FALSE WHEN THE RUN CRASHED, for the same reason. A collection error is
        not a test result; nothing was measured. Issue #1314. `summary` says
        which of the two happened rather than leaving a bare False to be read as
        a guard that did not fire -- that is the mistake in the other direction
        and it is issue #1313.
        """
        return (self.returncode != 0 and not self.disturbed
                and not self.crashed)

    @property
    def proved(self) -> bool:
        """Whether BOTH halves held: it failed broken, and passed restored.

        THIS IS THE WHOLE POINT OF ISSUE #1735. A guard proof is two claims --
        the test fails without the fix and passes with it -- and until this
        existed `break_and_run` could only ever produce the first. A test that
        fails for the wrong reason, or fails always, gives a byte-identical
        first half: a name in `named_failures`, a plausible `summary`, and an
        exit code that satisfies the assertion `CLAUDE.md` tells callers to
        write. Only the second half tells those apart.

        FALSE WHEN THERE IS NO SECOND HALF, including when the caller opted out.
        An opt-out says a passing half exists somewhere else; it does not let
        this object claim to have seen one. `summary` says which case this is
        rather than leaving a bare False to be read as a guard that did not
        fire -- that is issue #1313's mistake and it is easy to repeat here.
        """
        if self.restored is None or self.unrestored:
            return False
        return (self.failed
                and bool(self.named_failures)
                and not self.restored.disturbed
                and not self.restored.crashed
                and not self.restored.reported_a_failing_test)

    @property
    def summary(self) -> str:
        """The last line of output, which for pytest is its result line."""
        if self.unrestored:
            lines = [line for line in (self.stdout + self.stderr).splitlines() if line.strip()]
            return (files_left_broken(self.unrestored) + " | with the break in: "
                    + (lines[-1].strip() if lines else "(no output)"))
        if self.disturbed:
            return ("EVIDENCE COMPROMISED: " + ", ".join(self.disturbed)
                    + " changed underneath this run, so its result means nothing. "
                    "Something else is working in this checkout. Issue #598.")
        if self.crashed:
            return (f"NO MEASUREMENT: the run exited {self.returncode} without "
                    "naming a single failing test, so no test reached its "
                    "assertions. A break that stops the module importing does "
                    "this. Make the break surgical and run it again. "
                    "Issue #1314.")
        lines = [line for line in (self.stdout + self.stderr).splitlines() if line.strip()]
        broken_half = lines[-1].strip() if lines else "(no output)"

        # THE VERDICT GOES HERE RATHER THAN ON A PROPERTY OF ITS OWN, because
        # `CLAUDE.md` tells every caller to write `print(result.summary)`. A
        # verdict a caller following the documentation never sees would be issue
        # #1735's own defect one level up.
        #
        # AND IT IS APPENDED ONLY WHEN THERE IS A SECOND HALF TO REPORT. This
        # property has a stated contract -- "the last line of output" -- and two
        # tests assert it by exact equality. Both build a result with no second
        # half, so they are untouched, and a caller who never asked for pairing
        # still gets exactly the line they always got.
        if self.restored_half_supplied_by is not None:
            return (f"{broken_half} | NOT PAIRED BY THIS CALL: the caller says "
                    f"the passing half comes from "
                    f"{self.restored_half_supplied_by}")
        if self.restored is None:
            return broken_half

        line = f"{broken_half} | restored: {self.restored.summary}"
        if self.proved:
            return f"PROVED: {line}"
        if self.restored.disturbed or self.restored.crashed:
            return (f"NOT A PROOF: the restored run gave no usable result, so "
                    f"nothing says the break is what caused the failure. {line}")
        if self.restored.reported_a_failing_test:
            return (f"NOT A PROOF: tests fail with the files PUT BACK, so they "
                    f"were failing before the break went in. {line}")

        # TWO DIFFERENT WAYS TO HAVE NO FIRST HALF, AND SAYING "NOTHING FAILED"
        # FOR BOTH SENDS HALF OF THEM THE WRONG WAY. A run that failed while
        # naming no test has measured something -- the count is there -- and the
        # fix is to find out why the names are missing. A run where nothing
        # failed has measured that the break does not bite, and the fix is a
        # different break. `named_failures` is what a guard proof means, so it
        # is what `proved` requires, but the report has to say which is absent.
        if self.failed and not self.named_failures:
            return (f"NOT A PROOF: the run failed with the break in but named "
                    f"no test, so nothing says a test reached its assertions. "
                    f"A suppressed short summary does this -- check for a "
                    f"second `-q`. {line}")
        return (f"NOT A PROOF: nothing failed with the break in, so no test "
                f"noticed it. {line}")


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
                  env: Mapping[str, str] | None = None,
                  *,
                  restored_half_supplied_by: str | None = None) -> GuardResult:
    """Apply each edit, run the command, restore every file, and run it again.

    BOTH HALVES, BECAUSE ONE IS NOT A PROOF. A guard proof is two claims: the
    test fails without the fix and passes with it. Until issue #1735 this ran
    the command once, with the break in place, and returned only that. A test
    that fails for the wrong reason -- or that was already failing -- produces a
    byte-identical first half, and the assertion `CLAUDE.md` tells callers to
    write is satisfied by it. `result.proved` is the answer to the question the
    caller is actually asking.

    @param edits    path relative to the repository root, and a function taking
                    the file's text and returning the broken text. An edit that
                    changes nothing raises, because a break that did not break
                    anything would produce a passing run and look like a
                    worthless guard.
    @param command  what to run while the files are broken
    @param restored_half_supplied_by
                    pass a sentence naming what already supplies the passing
                    half, and the second run is skipped. For a proof that runs
                    INSIDE the suite it is proving, the enclosing run is that
                    passing half and a second one would be waste.

                    A SENTENCE RATHER THAN A FLAG, ON PURPOSE. A boolean can be
                    set and reviewed without thought; a sentence puts a claim at
                    the call site that a reviewer can check, and makes anybody
                    adding a third opt-out write one -- which is where they find
                    out they do not have one.
    """
    if not edits:
        raise ValueError("break_and_run needs at least one edit to make.")

    originals: dict[pathlib.Path, bytes] = {}
    #: What each broken file held while the command ran. Compared against the
    #: file afterwards. See the note on being disturbed below.
    written: dict[pathlib.Path, bytes] = {}
    #: Every file whose original could not be written back, with its last
    #: error. See `FilesLeftBroken`.
    refused: dict[str, OSError] = {}
    #: An exception already leaving the `try`, so the restore can attach the
    #: files it left broken to it rather than raising over it.
    first_failure: BaseException | None = None
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
            written[path] = path.read_bytes()

        result = run_without_bytecode(command, env)

        # WAS ANYTHING ELSE WRITING TO THESE FILES? Issue #598. This function
        # breaks the REAL repository for the duration of the command, so any
        # other process working in the same checkout -- a second test run, an
        # editor, another agent -- can see the broken text or overwrite it. When
        # that happened, five unrelated tests failed once and passed on a rerun,
        # and the failures pointed at innocent guards in the design document.
        #
        # A file whose bytes are not what this function wrote means the run did
        # not test what it was asked to. Saying so is the whole fix: the result
        # is reported as compromised rather than as a guard firing.
        disturbed = tuple(
            str(path.relative_to(REPO_ROOT)).replace("\\", "/")
            for path, content in written.items()
            if not path.is_file() or path.read_bytes() != content)

        broken = dataclasses.replace(result, disturbed=disturbed)
    except BaseException as error:
        first_failure = error
        raise
    finally:
        # IN A FINALLY, so a raising edit, a crashing command and an interrupt
        # all leave the repository as they found it.
        #
        # EVERY FILE IS TRIED, AND A REFUSED WRITE IS TRIED AGAIN (`write_back`).
        # Until 2026-11-25 this was one `write_bytes` a file: a refusal raised out
        # of the loop, so the files after it were not written back either, and
        # its OSError replaced whatever explained the run.
        for path, content in originals.items():
            error = write_back(path, content)
            if error is not None:
                refused[str(path.relative_to(REPO_ROOT)).replace("\\", "/")] = error
        clear_bytecode_caches()
        if refused and first_failure is not None:
            # THE FIRST FAILURE IS THE ONE THAT EXPLAINS THE RUN, so it is the
            # one that leaves, carrying the broken files as a note. The same rule
            # `unreal_build.prove_cpp_guard` follows, from issue #384.
            first_failure.add_note(str(files_left_broken_error(refused)))

    # NOTHING HERE MAY RUN WHILE A FILE STILL HOLDS THE BREAK, a second half
    # least of all: it would measure the broken file and call it restored.
    if refused:
        raise files_left_broken_error(
            refused, result=dataclasses.replace(broken, unrestored=tuple(refused)))

    # THE SECOND HALF RUNS HERE, AFTER THE finally AND NOT INSIDE IT. That block
    # is what makes a crashing command or an interrupt leave the repository as it
    # found it, and every line added to it is another way for the restore to
    # fail. By this point the files are back and the bytecode caches are cleared,
    # so this run sees the tree as it was before anything was broken.
    #
    # A FAILURE HERE PROPAGATES AND TAKES THE BROKEN HALF WITH IT. Losing
    # evidence already paid for is the cheaper mistake: a broken half returned on
    # its own, with nothing saying the pairing was attempted and lost, is exactly
    # the half-proof this function now exists to refuse.
    if restored_half_supplied_by is not None:
        return dataclasses.replace(
            broken, restored_half_supplied_by=restored_half_supplied_by)

    # NOTHING TO PAIR WITH. `break_and_run` takes ANY command, not only a test
    # runner, and for one that is not, a clean second run says nothing the exit
    # code did not. `looks_like_a_test_run` is the existing property that decides
    # this and it needs no new concept.
    if not broken.looks_like_a_test_run:
        return broken

    return dataclasses.replace(broken, restored=run_without_bytecode(command, env))


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
