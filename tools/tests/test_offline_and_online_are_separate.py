"""Offline and online characters share nothing, which is a second partition axis.

WHY THIS EXISTS. Issue #528. Decision #505, on 2026-08-10, made the offline and
online populations permanently non-transferable because a local save file can be
edited. It named three things an offline character does not get: the auction
house, the ladder, and the shared table of corrupted characters. **It did not
name the stash or the empire upgrade tree, and neither did the design document.**

THAT GAP WAS A HOLE IN THE RULE #505 MADE, not a neutral omission. A stash both
populations can open is the shortest transfer route between them: edit an item
into a local save, put it in the stash, withdraw it on an online character, sell
it in the auction house. The rule would have been enforced on characters and
bypassed through storage.

WHAT WAS DECIDED, 2026-08-14, by the project owner: separate. An offline
character and an online character share no empire upgrade tree, no stash, no
market and no balance of gold, whatever their lethality mode. So the partition
key is the population together with the lethality mode, and an account holds up
to six of everything it shares rather than three.

WHY THE ASSERTIONS BELOW CARRY WHOLE CLAUSES RATHER THAN KEYWORDS. The sibling
file `test_empire_tree_ownership.py` records that an adversarial review broke two
of its checks by writing a paragraph that stated the OPPOSITE rule while still
containing the matched substring. "offline" and "online" appear over a dozen
times across this document for unrelated reasons -- the network commitment in
section VIII, seasonal leagues in section XIV -- so a bare keyword here would be
worth nothing at all.

WHAT IS ASSERTED HERE.

    section II states the partition, names all four things it covers, and gives
      the count
    section II gives the REASON, which is the editable local file, not a
      preference
    the Empire-Wide Upgrades section carries the axis too, because a reader may
      arrive at either
    the Storage section says the two populations never open the same stash
    the decision log records the reasoning, cites the genre with sources, and
      marks which claims are less certain
    the save system design states it as settled rather than assumed, and no
      longer lists it as an open question
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"
SAVE_DESIGN = REPO_ROOT / "docs" / "Save_System_Design.md"

#: The section a player reads when choosing what to make.
CREATION_SECTION = "### **Difficulty Options**"

#: The section that states the partition rule in full.
OWNERSHIP_SECTION = "## **Empire-Wide Upgrades**"

#: The section that describes the stash itself.
STORAGE_SECTION = "## **Storage"

DECISION_HEADING = ("## 2026-08-14 — Offline and online characters share "
                    "nothing, so the partition key is the population times the "
                    "lethality mode")


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str) -> str:
    """One section, from its heading to the next heading of the same level up."""
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in ("\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


@pytest.fixture(scope="module")
def creation(document: str) -> str:
    return unwrapped(section_of(document, CREATION_SECTION))


@pytest.fixture(scope="module")
def ownership(document: str) -> str:
    return unwrapped(section_of(document, OWNERSHIP_SECTION))


@pytest.fixture(scope="module")
def storage(document: str) -> str:
    return unwrapped(section_of(document, STORAGE_SECTION))


@pytest.fixture(scope="module")
def decision_entry() -> str:
    """This decision's entry alone, not the whole 5,000-line log.

    The sibling file records why: four of five genre citations in an earlier
    entry passed against a version of docs/DECISIONS.md written before the
    change, because the game names appear in unrelated entries elsewhere.
    """
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {DECISION_HEADING!r}. That "
        f"entry is the only place the reasoning for the offline and online "
        f"partition is written down. Issue #528.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


@pytest.fixture(scope="module")
def save_design() -> str:
    if not SAVE_DESIGN.is_file():
        pytest.skip("docs/Save_System_Design.md is not present")
    return unwrapped(SAVE_DESIGN.read_text(encoding="utf-8"))


# --------------------------------------------------------------------------
# The rule, where a player choosing a character reads it
# --------------------------------------------------------------------------

def test_the_creation_section_states_the_partition(creation) -> None:
    """A player picks the population at character creation and can never change
    it, so this is the one moment the cost can be told to them."""
    assert "Offline and online are a second partition, crossing the first" in creation, (
        "the Difficulty Options section no longer states that offline and "
        "online characters are partitioned from each other. Decided "
        "2026-08-14 on issue #528.")


def test_it_names_all_four_things_the_partition_covers(creation) -> None:
    """A list with a gap in it is how this defect arose in the first place.
    #505 named three things offline characters do not get and left out the two
    that mattered, so a reader concluded nothing about the stash."""
    assert ("never share an empire upgrade tree, a stash, a market or a balance "
            "of gold") in creation, (
        "the partition rule no longer names what it covers. It covers the "
        "empire upgrade tree, the stash, the market and gold. Naming three of "
        "four is what left this open after #505. Issue #528.")
    assert "even when both are Standard" in creation, (
        "the rule does not say it holds regardless of lethality mode. Without "
        "that, a reader can take it as a restatement of the mode partition "
        "rather than a second axis crossing it. Issue #528.")


def test_the_creation_section_gives_the_count(creation) -> None:
    """The sharing relation is abstract; the count is what a player feels."""
    assert "up to six of everything the account shares rather than three" in creation, (
        "the Difficulty Options section states the partition without saying "
        "what it costs a player who plays both. Six rather than three is the "
        "whole size of it. Issue #528.")


def test_the_rule_says_why_rather_than_only_what(creation) -> None:
    """A restriction with no stated reason reads as an oversight and gets
    removed by the next person who wants to be generous to players. The reason
    is specific and it is not a preference: an offline save is a local file."""
    assert "forced by the rule that separated the populations, not added to it" in creation, (
        "the partition no longer says it follows from decision #505 rather "
        "than being a new restriction. Issue #528.")
    assert "an item edited into a local save" in creation, (
        "the section no longer describes the transfer route the partition "
        "closes. Without it the rule looks like an arbitrary inconvenience "
        "instead of the thing that keeps the auction house honest. Issue #528.")


# --------------------------------------------------------------------------
# Both other sections carry it
# --------------------------------------------------------------------------

def test_the_ownership_section_carries_the_population_axis(ownership) -> None:
    """This section states the general partition rule -- anything the account
    shares is held once per lethality mode -- and that rule is now incomplete on
    its own. A reader who lands here and not on section II would conclude three
    of everything."""
    assert "And once per population as well" in ownership, (
        "the Empire-Wide Upgrades section states the general partition rule "
        "without the population axis. The full key is the population together "
        "with the lethality mode. Issue #528.")


def test_the_storage_section_says_the_populations_never_share_a_stash(storage) -> None:
    """The stash is the transfer route this decision closes, so the section
    describing the stash is the one that must say it is closed."""
    assert "Offline and online characters never open the same stash" in storage, (
        "the Storage section no longer says an offline and an online character "
        "cannot open the same stash. That container is the shortest route "
        "between the two populations and closing it is the point of the "
        "decision. Issue #528.")
    assert "Gold is partitioned the same way" in storage, (
        "the Storage section partitions the stash without partitioning gold. "
        "Gold is one of the things the account shares, so it takes the same "
        "rule. Issue #528.")


# --------------------------------------------------------------------------
# The reasoning is recorded
# --------------------------------------------------------------------------

def test_the_decision_log_records_that_the_answer_was_forced(decision_entry) -> None:
    """The strongest thing about this decision is that it was not a new
    restriction. If that is lost, a later reader sees only a cost."""
    assert "Why this was not really a new decision" in decision_entry, (
        "the docs/DECISIONS.md entry no longer records that the answer follows "
        "from decision #505 rather than adding to it. Issue #528.")
    assert "#505" in decision_entry


def test_the_decision_log_cites_the_genre_with_sources(decision_entry) -> None:
    """CLAUDE.md requires looking up how shipped games solve a problem and
    naming the sources, rather than inventing a rule. The specific question here
    is narrower than the one #505 answered: not whether the populations are
    separable, but whether shipped games partition STORAGE on that axis."""
    for game in ("Last Epoch", "Diablo II", "Diablo III"):
        assert game in decision_entry, (
            f"the decision log entry no longer cites {game}. The argument that "
            f"storage is partitioned on the same axis as the populations is "
            f"only as good as the games behind it.")
    assert "Sources:" in decision_entry, (
        "the entry makes claims about three shipped games and cites nothing.")
    assert decision_entry.count("https://") >= 4, (
        "the entry cites fewer than four sources. The load-bearing ones are "
        "Last Epoch's separate offline and online stashes with the developers' "
        "stated reason, and Diablo II's open and closed realm storage.")


def test_the_decision_log_marks_which_claims_are_less_certain(decision_entry) -> None:
    """CLAUDE.md: if the evidence for a result was compromised, say so first
    rather than after. One of the two Diablo II details came from a fan wiki
    archive rather than a primary source."""
    assert "Confidence is not uniform" in decision_entry, (
        "the entry presents every genre claim at the same confidence. The "
        "Diablo II transfer-direction detail is from a fan wiki archive, not "
        "from Blizzard. Saying so is the project rule.")


def test_the_decision_log_records_what_it_costs(decision_entry) -> None:
    """A decision log carrying only the case for a decision is a record of an
    argument. This one charges a player who plays both twice over."""
    assert "What it costs" in decision_entry, (
        "the entry does not record what the partition costs a player who plays "
        "both offline and online: two unrelated sets of empire progress and "
        "two stashes, the second starting from nothing. Issue #528.")


# --------------------------------------------------------------------------
# The save system design no longer calls it an assumption
# --------------------------------------------------------------------------

def test_the_save_design_states_it_as_settled(save_design) -> None:
    """docs/Save_System_Design.md was written on this as an assumption and said
    so, naming the issue. The issue is answered, so the assumption language is
    now false and would send a reader looking for an open question."""
    assert "answered by the project owner on 2026-08-14" in save_design, (
        "docs/Save_System_Design.md does not record that the offline and "
        "online partition was decided. It was written assuming the answer and "
        "must now state it as settled. Issue #528.")
    assert "This design assumes they are separate" not in save_design, (
        "docs/Save_System_Design.md still calls the offline and online "
        "partition an assumption. It was answered on 2026-08-14.")


def test_the_save_design_no_longer_lists_it_as_unsettled(save_design) -> None:
    """Section 6 lists what the design deliberately does not settle. Leaving an
    answered question there is worse than never listing it, because that section
    is where someone looks for work that still needs a decision."""
    assert "Whether offline and online share a stash and empire tree" not in save_design, (
        "docs/Save_System_Design.md section 6 still lists the offline and "
        "online partition as unsettled. It was answered on 2026-08-14 and the "
        "rule is now in the design document. Issue #528.")


def test_the_storage_table_covers_both_populations(save_design) -> None:
    """The table mapping a character to its account record had three rows and
    now needs six, because the record is chosen by population as well as mode.
    A three-row table is the storage layout for the rejected answer."""
    for population in ("online", "offline"):
        for mode in ("Standard", "Hardcore", "Heretic"):
            row = f"The {population} {mode} account record"
            assert row in save_design, (
                f"the storage table in docs/Save_System_Design.md has no row "
                f"for {row!r}. There are six account records, not three, "
                f"because the population selects one as much as the lethality "
                f"mode does. Issue #528.")
