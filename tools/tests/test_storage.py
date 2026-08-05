"""The shared stash: what it is, not only what is true about it.

WHY THIS EXISTS. Issue #305. `docs/Cataclysm_GDD_v2.md`, the main design
document, referred to a shared stash five times and described it nowhere. Every
mention was a statement ABOUT the stash -- Solo Self-Found does not have one, it
is partitioned by lethality mode, it carries no fees -- and none said how large
it is, what it holds, or whether a character also has one of its own. Issue #285
partitioned a container the document had never defined.

WHAT WAS DECIDED, 2026-08-05. There is no operator answer on this issue; the
section was written under the constraints the document already carried, and
`docs/DECISIONS.md` names what constrained each choice.

    600 slots, six tabs of 100, fixed, all open from the start
    nothing to buy: no gold price, no empire tree node, and the monetisation
      section already rules out money
    it holds gear, gems and crafting materials, and no gold
    a character has no private stash; it has an inventory and the shared one
    the auction house lists from the stash

AND ISSUE #306, ANSWERED THE SAME DAY AND LATER: gold is held by the account,
once per lethality mode, exactly as the stash is. It lives in the same section,
so its checks live in this file. The argument came from inside the document
rather than from the genre survey, which was split: the Empire-Wide Upgrades
section says making another character in a mode already being played costs
"levels and gear and nothing else", and per-character gold would have made that
sentence false.

WHAT IS ASSERTED HERE. Each of the five questions issue #305 listed, the gold
answer and its reasoning, and the boundary this section must not cross: it must
not contradict the monetisation section's promise of no storage fees.

ONE TEST HERE USED TO ASSERT THE OPPOSITE OF WHAT IT ASSERTS NOW, because it
recorded that gold's owner was an open question. It says so in its own docstring,
under the heading WHAT THIS USED TO ASSERT. A test that records an absence has to
be rewritten when the absence is filled, or it fails for the right reason and
gets deleted for the wrong one.

600 IS A TUNING VALUE AND THE TESTS TREAT IT AS ONE. The slot count is read from
the document and checked for internal consistency -- six tabs of 100 make 600 --
rather than pinned to 600 in an assertion. Changing the number should not break
a test; changing it in one place and not the other should.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

STORAGE_SECTION = "## **Storage**"
MONETISATION_SECTION = "## **Base Game**"

DECISION_HEADING = ("## 2026-08-05 — The shared stash is 600 fixed slots, "
                    "free, and holds no gold")


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str) -> str:
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in ("\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


@pytest.fixture(scope="module")
def storage(document: str) -> str:
    return unwrapped(section_of(document, STORAGE_SECTION))


@pytest.fixture(scope="module")
def decision_entry() -> str:
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {DECISION_HEADING!r}. It is the "
        f"only place the reasoning and the genre sources for the stash design "
        f"are written down.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


# --------------------------------------------------------------------------
# It exists at all
# --------------------------------------------------------------------------

def test_the_document_has_a_storage_section(document):
    """Before issue #305 the word "stash" appeared five times and never in a
    heading."""
    assert STORAGE_SECTION in document, (
        "the design document has no Storage section. It refers to a shared "
        "stash in the difficulty options, the empire tree section and the "
        "monetisation section, and something has to say what one is. "
        "Issue #305.")


def test_the_capital_services_table_lists_the_stash(document):
    """Where a reader looking for it will land first. The stash sits at the
    capital beside the other services and was the only one not listed."""
    services = unwrapped(section_of(document, "## **Capital Services**"))
    assert "| Stash |" in services, (
        "the Capital Services table does not list the stash. Issue #305.")


# --------------------------------------------------------------------------
# The five questions issue #305 asked
# --------------------------------------------------------------------------

def slot_figures(storage: str) -> tuple[int, int, int]:
    """(total slots, tabs, slots per tab) as the section states them."""
    total = re.search(r"Size: ([\d,]+) slots", storage)
    assert total, ("the Storage section does not state a slot count in the "
                   "form 'Size: N slots'. Issue #305.")
    split = re.search(r"as (\w+) tabs of (\d+)", storage)
    assert split, ("the Storage section does not say how the slots are divided "
                   "into tabs. Issue #305.")
    words = {"two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7,
             "eight": 8, "nine": 9, "ten": 10}
    return (int(total.group(1).replace(",", "")),
            words[split.group(1)], int(split.group(2)))


def test_it_says_how_large_the_stash_is(storage):
    total, tabs, per_tab = slot_figures(storage)
    assert total > 0 and tabs > 0 and per_tab > 0


def test_the_tabs_and_the_total_agree(storage):
    """THE ONE ARITHMETIC CHECK. The number is a tuning value and is expected to
    move; what must not happen is it moving in one sentence and not the other,
    which is how the same document ended up with five statements about a stash
    and no stash."""
    total, tabs, per_tab = slot_figures(storage)
    assert tabs * per_tab == total, (
        f"the Storage section says {total} slots and also {tabs} tabs of "
        f"{per_tab}, which is {tabs * per_tab}. One of the two was changed "
        f"without the other. Issue #305.")


def test_it_says_the_stash_does_not_grow(storage):
    assert "The stash does not grow and there is nothing to buy" in storage, (
        "the Storage section no longer says whether the stash grows. That was "
        "one of the five questions issue #305 asked, and the answer decides "
        "whether gold needs a price for it.")


def test_it_rules_out_all_three_ways_of_expanding_it(storage):
    """Money, gold and the empire tree. Naming only the one the monetisation
    section already rules out would leave the other two open."""
    for phrase in ("No empire upgrade node grants",
                   "no gold price expands it",
                   "no stash or storage fees of any kind"):
        assert phrase in storage, (
            f"the Storage section does not rule out {phrase!r} as a way to "
            f"expand the stash. All three routes have to be closed or the "
            f"answer is only partial. Issue #305.")


def test_it_says_what_the_stash_holds(storage):
    assert "It holds items: gear, gems and crafting materials" in storage, (
        "the Storage section does not say what the stash can hold. Issue #305.")


def test_it_says_a_character_has_no_private_stash(storage):
    """The fourth question. A reader who assumes a per-character stash exists
    would design a save format with one in it."""
    assert "A character has no private stash" in storage, (
        "the Storage section does not say whether a character has a stash of "
        "its own separate from the shared one. It does not. Issue #305.")


def test_it_says_where_the_auction_house_lists_from(storage):
    assert "The auction house lists from the stash" in storage, (
        "the Storage section does not say whether the auction house draws from "
        "the shared stash or from the carried inventory. Issue #305.")


def test_it_explains_why_solo_self_found_loses_both_together(storage):
    """The rule that makes the auction house answer more than an arbitrary
    choice: a market can only offer what its stash can hold, so removing the
    stash removes the market."""
    assert "Solo Self-Found character loses both together" in storage, (
        "the Storage section states where the auction house lists from without "
        "connecting it to Solo Self-Found losing both. That connection is what "
        "makes the answer follow from something. Issue #305.")


# --------------------------------------------------------------------------
# The boundaries this section must not cross
# --------------------------------------------------------------------------

def test_the_stash_itself_holds_no_gold(storage):
    """Gold is a balance rather than an item, so it is not in the container even
    though it is shared on the same axis as the container."""
    assert "It does not hold gold" in storage


def test_it_says_gold_is_held_by_the_account_once_per_lethality_mode(storage):
    """WHAT THIS USED TO ASSERT. Until issue #306 was answered on 2026-08-05,
    this file asserted the OPPOSITE property: that the Storage section named
    where gold lives as an open question and stated no conclusion about it. That
    test split the section into sentences, found every sentence mentioning gold
    alongside both owners, and required a hedge on each one.

    The question is now answered, so a test that fails when the document states
    the answer would be failing for the right reason and getting deleted for the
    wrong one. It asserts the answer instead.

    THE ANSWER CAME FROM INSIDE THE DOCUMENT, not from the genre survey, which
    was split. The Empire-Wide Upgrades section says making another character in
    a mode already being played costs "levels and gear and nothing else", and
    per-character gold would have made that sentence false.
    """
    assert "Gold is held by the account, once per lethality mode" in storage, (
        "the Storage section no longer says who owns gold. Issue #306 settled "
        "it: the account, once per lethality mode, exactly as the stash is.")


def test_the_gold_rule_names_the_sentence_that_decided_it(storage):
    """A rule taken from a sentence elsewhere in the same document should say
    which sentence, or a later edit to that sentence silently removes the
    argument for this one."""
    assert "levels and gear and nothing else" in storage, (
        "the Storage section states that gold is account-held without quoting "
        "the sentence that decided it. Issue #306.")


def test_solo_self_found_gold_is_private_too(storage):
    """Every other account-level thing has this exception: the empire tree, the
    stash, the auction house. Gold without it would be the one shared resource
    the flag failed to close off, which is the exact argument issue #273 used
    for the tree."""
    assert "its gold is its own and" in storage, (
        "the Storage section makes gold account-held without saying a Solo "
        "Self-Found character's gold is private. Issues #273 and #306.")


def test_the_partition_section_also_names_gold(document):
    """Two sections carry this rule and a reader may arrive at either. The
    Empire-Wide Upgrades section is the one that states the general partition
    rule, so it has to say gold is one of the things the rule covers."""
    ownership = unwrapped(section_of(document, "## **Empire-Wide Upgrades**"))
    assert "Gold is one of the things the account shares" in ownership, (
        "the Empire-Wide Upgrades section states the general partition rule "
        "without saying gold falls under it. That rule was written to hold "
        "whichever way issue #306 went, and now that it is answered the section "
        "should say which way.")
    assert "cannot fund a Heretic one" in ownership, (
        "the section does not state the consequence: three gold balances means "
        "a Standard character cannot fund a Heretic one, which is the same "
        "route the stash partition closed for gear. Issues #285 and #306.")


def test_it_agrees_with_the_monetisation_promise(document, storage):
    """The document promises no storage fees of any kind. A stash design that
    sold anything would contradict a section 1,400 lines away."""
    monetisation = unwrapped(section_of(document, MONETISATION_SECTION))
    assert "no stash or storage fees of any kind" in monetisation
    assert "nothing to buy" in storage


def test_it_carries_the_lethality_mode_partition(storage):
    """Issue #285 partitioned the stash by lethality mode before anything
    described one. The description has to carry the partition, or a reader
    landing here learns the stash is shared and not that it is shared three
    times over."""
    assert "once per lethality mode" in storage, (
        "the Storage section describes the stash without saying it is held once "
        "per lethality mode. Issues #285 and #305.")
    assert "no stash at all" in storage, (
        "the Storage section does not say a Solo Self-Found character has no "
        "stash. Issue #305.")


def test_it_says_the_number_is_a_tuning_value(storage):
    """CLAUDE.md and the project's own habit: separate the rule from the
    number, so that changing the number does not read as changing the design."""
    assert "is a tuning value" in storage, (
        "the Storage section states a slot count without saying it is a tuning "
        "value anchored to another game rather than a measured one. Issue "
        "#305.")


# --------------------------------------------------------------------------
# The decision log
# --------------------------------------------------------------------------

def test_the_decision_log_records_the_genre_survey(decision_entry):
    for game in ("Path of Exile 2", "Diablo IV", "Last Epoch"):
        assert game in decision_entry, (
            f"the docs/DECISIONS.md entry for issue #305 does not cite {game}. "
            f"All three ship a shared stash and the design here is anchored to "
            f"what they do.")


def test_the_decision_log_says_the_sources_are_search_summaries(decision_entry):
    """Recorded because WebFetch cannot reach several of the hosts these sit
    on, so the evidence is a search result rather than the page. A reader has to
    know which they are looking at."""
    assert "search-result summaries rather than the pages themselves" \
        in decision_entry, (
        "the docs/DECISIONS.md entry cites genre sources without saying they "
        "are search summaries rather than fetched pages.")


def test_the_decision_log_records_why_gold_priced_tabs_were_rejected(
        decision_entry):
    """The genre-normal answer was NOT taken, so the entry has to say why or the
    next reader will assume it was overlooked. Two reasons, and the first is a
    dependency on an open issue."""
    assert "Why fixed rather than sold for gold" in decision_entry
    assert "#306" in decision_entry, (
        "the entry does not record that a gold-priced stash depends on whether "
        "gold is held by the character or the account, which is open.")
    assert "no other gold sink written down" in decision_entry


def test_the_decision_log_records_what_argues_against_it(decision_entry):
    assert "The case against a fixed stash" in decision_entry, (
        "the docs/DECISIONS.md entry for issue #305 records no case against a "
        "fixed stash. There is one: it removes something to work towards, and "
        "the number is an anchor rather than a measurement.")
    assert "may be badly wrong in either direction" in decision_entry


def test_the_decision_log_says_what_would_reopen_it(decision_entry):
    """A decision made under a constraint should say what removing the
    constraint would change, or it reads as permanent when it is contingent."""
    assert "What would argue for revisiting it" in decision_entry, (
        "the entry does not say what would make a gold-priced stash the right "
        "answer later. It is issue #306 settling gold as account-held plus a "
        "gold economy with more than one sink in it.")


def test_the_decision_log_connects_it_to_the_cut_inventory_node(decision_entry):
    """Issue #260 established that Weightless Spoils, an empire tree node
    granting inventory slots, was never built. With this decision neither
    container has any scaling source anywhere in the design, and that is worth
    stating in one place rather than being rediscovered."""
    assert "Weightless Spoils" in decision_entry
    assert "#308" in decision_entry, (
        "the entry does not point at the open issue about what grants carried "
        "inventory slots. This entry settles the stash only.")


# --------------------------------------------------------------------------
# The gold decision log entry -- issue #306
# --------------------------------------------------------------------------

GOLD_DECISION_HEADING = ("## 2026-08-05 — Gold is held by the account, once per "
                         "lethality mode")


@pytest.fixture(scope="module")
def gold_entry() -> str:
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(GOLD_DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {GOLD_DECISION_HEADING!r}. It "
        f"is the only place the reasoning for gold's ownership is recorded, "
        f"including that the issue's own original recommendation was reversed.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


def test_the_gold_entry_records_the_argument_from_inside_the_document(
        gold_entry):
    """The genre survey was split, so it did not decide this. The document's own
    sentence did, and that is the part a later reader most needs."""
    assert "levels and gear **and nothing else**" in gold_entry, (
        "the docs/DECISIONS.md entry for issue #306 no longer quotes the "
        "sentence that settled it. The survey was split three to one and the "
        "decision does not rest on it.")


def test_the_gold_entry_records_that_the_first_recommendation_was_wrong(
        gold_entry):
    """Issue #306's body recommended per-character gold on a count that turned
    out to be backwards. An entry that quietly adopted the opposite answer
    without saying so would leave the issue and the document disagreeing with
    no explanation."""
    assert "originally recommended the opposite" in gold_entry, (
        "the entry does not record that issue #306's own recommendation was "
        "reversed by a corrected survey. Anyone reading the issue first will "
        "otherwise think the document ignored it.")
    assert "re-checked" in gold_entry, (
        "the entry does not say the corrected claims were re-checked before "
        "the decision. The correction comment on issue #306 asked for exactly "
        "that, because the count was the whole argument.")


def test_the_gold_entry_surveys_the_four_games(gold_entry):
    for game in ("Diablo IV", "Last Epoch", "Diablo III", "Path of Exile"):
        assert game in gold_entry, (
            f"the docs/DECISIONS.md entry for issue #306 does not cite {game}.")


def test_the_gold_entry_records_what_argues_against_it(gold_entry):
    assert "What argues against it" in gold_entry, (
        "the entry records no case against account-held gold. There is one: it "
        "is one more thing to re-earn per lethality mode, and a first Heretic "
        "character now starts with no tree, no stash and no gold.")
    assert "no tree, no stash and no gold" in gold_entry


def test_the_gold_entry_notes_where_this_design_is_softer_than_last_epoch(
        gold_entry):
    """Last Epoch's hardcore characters share with nothing at all, not even each
    other. This design shares within a mode, and the difference is deliberate
    rather than an incomplete copy."""
    assert "stricter than this design and the difference is deliberate" \
        in gold_entry, (
        "the entry cites Last Epoch without noting that its partition is "
        "harsher than the one chosen here.")


def test_the_stash_entry_no_longer_calls_gold_ownership_open(decision_entry):
    """The entry for issue #305 gave two reasons for a fixed stash and the
    first was that gold's owner was unknown. It was answered hours later, so
    that reason is gone and the entry says so rather than staying wrong."""
    assert "that reason has since gone" in decision_entry, (
        "the docs/DECISIONS.md entry for issue #305 still gives an undecided "
        "gold owner as a reason the stash is fixed. Issue #306 answered it the "
        "same day.")
    assert "only the second reason below still stands" in decision_entry
