"""Two sessions must not both drive the Unreal editor.

WHY THIS EXISTS. Several Claude sessions now work on this repository at once, in
separate git worktrees. Opening the editor, running the automation tests, and
regenerating the DataTable assets all drive one editor on one machine, so exactly
one session may do any of them at a time. tools/unreal_lock.py is how a session
claims that right; these tests are what say it actually holds.

WHAT THE CENTRAL TEST IS. The one that matters is that a second acquire fails
while the lock is held. Everything else here supports it. That test is not
decoration: tools/unreal_lock.py takes the lock with O_CREAT | O_EXCL precisely
so that two sessions asking at the same moment cannot both win, and the obvious
alternative -- read the file, then write it if free -- passes every other test in
this file while failing that one.

WHAT IS NOT CHECKED HERE. That sessions actually call it. Nothing in Python can
enforce that; CLAUDE.md states the rule and the fleet's briefs repeat it.
"""

from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone

import pytest
import unreal_lock


@pytest.fixture
def lock(tmp_path):
    """A lock file of this test's own, so tests never touch the real one."""
    return tmp_path / "unreal-editor.lock"


def test_an_untouched_lock_is_free(lock):
    assert unreal_lock.read_holder(lock) is None
    assert unreal_lock.status(lock) == "The Unreal editor is free."


def test_acquiring_records_the_holder(lock):
    taken, message = unreal_lock.acquire("session-a", lock)
    assert taken is True
    assert "session-a" in message
    assert unreal_lock.read_holder(lock)["holder"] == "session-a"
    assert "session-a" in unreal_lock.status(lock)


def test_a_second_session_cannot_acquire_a_held_lock(lock):
    """The guard. Without O_EXCL in acquire(), this is the test that goes red."""
    assert unreal_lock.acquire("session-a", lock)[0] is True

    taken, message = unreal_lock.acquire("session-b", lock)

    assert taken is False
    assert "session-a" in message
    assert unreal_lock.read_holder(lock)["holder"] == "session-a"


def test_a_session_that_does_not_hold_the_lock_cannot_release_it(lock):
    unreal_lock.acquire("session-a", lock)

    released, message = unreal_lock.release("session-b", lock)

    assert released is False
    assert "session-a" in message
    assert lock.is_file(), "a failed release must leave the lock in place"


def test_the_holder_can_release_and_the_next_session_can_take_it(lock):
    unreal_lock.acquire("session-a", lock)

    released, _ = unreal_lock.release("session-a", lock)

    assert released is True
    assert unreal_lock.read_holder(lock) is None
    assert unreal_lock.acquire("session-b", lock)[0] is True


def test_releasing_a_free_lock_says_so_rather_than_claiming_success(lock):
    released, message = unreal_lock.release("session-a", lock)
    assert released is False
    assert "already free" in message


def test_stealing_fails_when_the_named_holder_is_not_the_real_one(lock):
    """Naming the holder is the safeguard, so a wrong name must not go through."""
    unreal_lock.acquire("session-a", lock)

    stolen, message = unreal_lock.steal("session-c", "session-b", lock)

    assert stolen is False
    assert "session-a" in message
    assert unreal_lock.read_holder(lock)["holder"] == "session-a"


def test_stealing_succeeds_when_the_named_holder_is_right(lock):
    unreal_lock.acquire("session-a", lock)

    stolen, _ = unreal_lock.steal("session-c", "session-a", lock)

    assert stolen is True
    assert unreal_lock.read_holder(lock)["holder"] == "session-c"


def test_stealing_a_free_lock_is_just_acquiring_it(lock):
    stolen, _ = unreal_lock.steal("session-c", "session-a", lock)
    assert stolen is True
    assert unreal_lock.read_holder(lock)["holder"] == "session-c"


def test_an_unreadable_lock_file_counts_as_held_not_free(lock):
    """Treating a corrupt lock as free is how two editors get opened."""
    lock.write_text("this is not json", encoding="utf-8")

    assert unreal_lock.read_holder(lock) is not None
    assert unreal_lock.acquire("session-b", lock)[0] is False
    assert "free" not in unreal_lock.status(lock)


def test_status_calls_out_a_lock_older_than_any_real_build(lock):
    """A session that died leaves the lock held forever; status has to show it."""
    long_ago = datetime.now(timezone.utc) - timedelta(
        hours=unreal_lock.STALE_AFTER_HOURS + 1)
    lock.write_text(
        json.dumps({"holder": "session-a", "taken": long_ago.isoformat()}),
        encoding="utf-8")

    reported = unreal_lock.status(lock)

    assert "session-a" in reported
    assert "may be gone" in reported


def test_status_does_not_call_out_a_lock_taken_moments_ago(lock):
    """The control for the test above: a fresh lock must not read as suspicious."""
    unreal_lock.acquire("session-a", lock)

    reported = unreal_lock.status(lock)

    assert "session-a" in reported
    assert "may be gone" not in reported


def test_the_lock_path_is_shared_by_every_worktree(lock, tmp_path):
    """An override exists for these tests; without it the path comes from git.

    git rev-parse --git-common-dir gives the same directory from every worktree,
    which is the whole point: a lock inside one worktree cannot stop a session in
    another.
    """
    chosen = unreal_lock.lock_path({unreal_lock.LOCK_PATH_VARIABLE: str(lock)})
    assert chosen == lock

    from_git = unreal_lock.lock_path({})
    assert from_git.name == "cataclysm-unreal-editor.lock"
    assert from_git.parent.name == ".git"
