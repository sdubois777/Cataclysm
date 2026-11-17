"""An account holds 24 characters, as one pool. Issue #577.

WHY THIS EXISTS. Issue #325 gave the player a way to delete a character on
2026-08-14, and deleting only matters if something is scarce. The design document
had never said how many characters an account could hold, so deletion read as
housekeeping rather than as the way to free a slot.

WHY ONE POOL IS THE PART WORTH GUARDING. This design partitions almost everything
by population and lethality mode: the stash, the auction house, gold and the
empire upgrade tree are each held once per combination, and issue #528 doubled
that to six on 2026-08-14. A reader who has absorbed that rule will assume the
slot count follows it. It does not, and the reason is stated rather than left to
be inferred: everything partitioned is something characters SHARE, and a slot
count is a count of characters rather than a thing they share.

THE COUNT IS A TUNING VALUE AND THE RULE IS NOT. The tests below read the number
out of the document and check the surrounding claims against it, rather than
pinning 24 in an assertion. Changing the number should not break a test; changing
it in one place and not another should, and so should losing the one-pool rule or
the nothing-raises-it rule.

WHAT IS NOT ASSERTED HERE. Anything about how slots are presented, or what the
character select screen does when the pool is full. Neither is designed.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SAVE_SYSTEM = REPO_ROOT / "docs" / "Save_System_Design.md"

#: The sentence the count is read from. Everything else is checked against
#: whatever number this yields, so the number itself lives in one place.
COUNT = re.compile(r"An account holds (\d+) characters, as one pool")


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def save_system() -> str:
    if not SAVE_SYSTEM.is_file():
        pytest.skip("Save_System_Design.md is not present")
    return SAVE_SYSTEM.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def slots(document) -> int:
    found = COUNT.search(document)
    if not found:
        pytest.skip("the count is missing; test_the_count_is_stated_at_all says so")
    return int(found.group(1))


def test_the_count_is_stated_at_all(document):
    """Read directly rather than through the `slots` fixture, deliberately.

    A fixture that asserts turns every test in this file into a pytest ERROR
    rather than a FAILURE, and the one thing a reader needs from a run like that
    is a single named test saying what is missing. The other tests skip instead.
    """
    found = COUNT.search(document)
    assert found, (
        "the design document does not say how many characters an account holds. "
        "Issue #325 gave the player a way to delete one on 2026-08-14, and "
        "deletion only means something if slots are scarce. Issue #577.")
    assert int(found.group(1)) > 0


def test_it_is_one_pool_rather_than_one_per_partition(document):
    """The part a reader will get wrong if it is not said.

    Everything else this design partitions -- stash, auction house, gold, empire
    tree -- is held once per population per lethality mode, six times over since
    issue #528. A slot count is not.
    """
    assert "There is not one allowance per partition" in document, (
        "the design document states a character count without saying whether it "
        "is one pool or one per partition. Everything else in this design is "
        "partitioned, so a reader will assume this is too. Issue #577.")

    assert ("whatever its population, whatever its lethality mode and whether or\n"
            "not it is Solo Self-Found") in document, (
        "the design document does not spell out which characters count against "
        "the pool. All of them do. Issue #577.")


def test_it_says_why_the_partition_does_not_apply(document):
    """A rule that contradicts the surrounding pattern needs its reason attached,
    or the next person to touch it will 'fix' it to match."""
    assert "it is a count *of* them" in document, (
        "the design document says the slot count is not partitioned without "
        "saying why, when every neighbouring rule is partitioned. The reason is "
        "that partitioning applies to what characters share, and a slot count "
        "is not shared. Issue #577.")


def test_nothing_raises_the_count(document):
    """Including money, which is the route the genre normally takes.

    Path of Exile sells character slots. This design does not, and the
    monetisation section has to say so in the same breath as the stash, or the
    promise there is incomplete.
    """
    assert "Nothing raises it" in document, (
        "the design document does not say whether anything grants extra "
        "character slots. Nothing does. Issue #577.")

    assert "no character\nslots for sale" in document, (
        "the monetisation section lists what is not sold and does not mention "
        "character slots, which is what Path of Exile sells. Issue #577.")


def test_deletion_is_named_as_the_only_way_to_free_a_slot(document):
    """This is what ties the count to issue #325 rather than leaving two
    unrelated paragraphs."""
    assert "the only way to free one" in document, (
        "the design document states a character limit without saying how a slot "
        "is freed. Deleting a character is the only way, and that is the second "
        "reason deletion exists. Issues #325 and #577.")


def test_the_empire_tree_ceiling_agrees_with_the_count(document, slots):
    """The tree count had no ceiling before this, and it has one now.

    Six shared trees plus one per Solo Self-Found character was unbounded until
    the character count existed. The two numbers have to move together.
    """
    ceiling = 6 + slots
    assert f"at most {ceiling} trees on an account" in document, (
        f"the empire tree section does not bound the number of trees, or bounds "
        f"it at a number other than {ceiling}: six shared plus at most {slots} "
        f"private, one per Solo Self-Found character. Issue #577.")


def test_the_save_format_carries_the_same_bound(save_system, slots):
    """`Save_System_Design.md` is where somebody sizing the format will look."""
    assert f"holds {slots} characters as one pool" in save_system, (
        f"Save_System_Design.md describes up to six account records plus one per "
        f"Solo Self-Found character without bounding the second half. An account "
        f"holds {slots} characters. Issue #577.")

    assert "largest object in it" in save_system, (
        "Save_System_Design.md bounds the record count without saying which "
        "record is the big one. A Solo Self-Found character record carries a "
        "600-slot stash and a private empire tree. Issues #576 and #577.")
