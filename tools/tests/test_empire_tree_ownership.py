"""Who owns the empire upgrade tree: the account, or the character.

WHY THIS EXISTS. Issue #273. `docs/Cataclysm_GDD_v2.md`, the main design
document, said twice that empire upgrade points "persist through all runs —
including failed ones" and never mentioned characters. So it was unstated
whether making a new character starts the primary meta-progression system over,
and two other decisions were priced against the unknown answer.

WHAT WAS DECIDED, 2026-08-05, by the project owner: "account wide, unless solo
self found." Every character on the account shares one empire upgrade tree. A
Solo Self-Found character has its own, shared with nothing.

WHY THE EXCEPTION FOLLOWS. A Solo Self-Found character already has no auction
house and no shared stash. Inheriting a mature account's empire tree would be a
larger handout than either, and the one shared resource the flag failed to close
off.

WHY IT MATTERS ELSEWHERE. Issue #255 locked the lethality mode and the Solo
Self-Found flag at character creation. The argument for locking was that
rerolling is affordable, which is only true because the empire tree survives a
new character. The two rules hold each other up, so this file checks that the
document still states both together.

WHAT THE SHARING IS SCOPED TO, answered 2026-08-05 on issue #277: the LETHALITY
MODE. The rest of the owner's original answer said sharing "should only apply to
the same difficulty tier", and the game has two difficulty axes — the T1 to T8
content tier and the Standard/Hardcore/Heretic lethality mode. The owner
confirmed the second. So Standard characters share one tree, Hardcore characters
share a second, Heretic characters share a third, and each Solo Self-Found
character has its own on top of that: three shared trees per account plus one per
Solo Self-Found character.

THE POINTS ARE SCOPED, NOT ONLY THE TREE, and that is the sentence most likely to
be lost. The natural way to build three trees is one account balance of empire
upgrade points with three allocations of it, and that permits exactly what the
scope exists to stop: farm on Standard, spend into Heretic.

THREE TESTS HERE USED TO ASSERT THE OPPOSITE OF WHAT THEY ASSERT NOW, because
they recorded that the question was open and that no rule had been written. Each
of the three says so in its own docstring, under the heading WHAT THIS USED TO
ASSERT. A test that records an absence has to be rewritten when the absence is
filled, or it fails for the right reason and gets deleted for the wrong one.

TWO ASSERTIONS HERE WERE WORTHLESS UNTIL AN ADVERSARIAL REVIEW BROKE THEM. Both
matched substrings that survived a paragraph stating the opposite rule, and a
reviewer demonstrated it by writing that paragraph and watching every test pass.
Both now carry enough of their sentence to fix its meaning, and each says so
where it sits. The decision-log checks had the same shape of flaw for a different
reason and now read one entry rather than the whole file; see the
`decision_entry` fixture.

WHAT IS ASSERTED HERE.

    the tree belongs to the account and a new character inherits it
    Solo Self-Found is stated as the one exception, in both places, and is
      private from its own lethality mode and from other Solo Self-Found
      characters as well
    the sharing is scoped to the lethality mode, all three modes are named, and
      the account tree count is stated
    the points are scoped and not only the tree
    the partition is three allocations of one node graph, not three node graphs
    the boundary cannot be crossed, because the mode is locked at creation
    the document admits the boundary is claimed for the tree and not for the
      shared stash or the auction house
    the claim that a harder lethality mode is cheap, which the scope rule made
      false, is gone
    both sections that describe the tree carry both the ownership rule and the
      scope, because a reader arriving at either one must not be told half of it
    the decision log records the reasoning, the genre evidence, the case AGAINST
      the decision, and the five questions left open
    a tree is never destroyed, in any mode, and a lost Solo Self-Found
      character's private tree passes to the next Solo Self-Found character
    the Worn Residue consumption bullet no longer says "empire progress is kept"
      without saying who keeps it when the consumed character owned the tree
    the decision log quotes the owner rather than paraphrasing, and names the
      half of that answer that was NOT applied
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The section that states the rule in full.
OWNERSHIP_SECTION = "## **Empire-Wide Upgrades**"

#: The section that restates it and points back. A reader may arrive at either.
SUMMARY_SECTION = "## **Roguelike Meta Progression**"


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str) -> str:
    """One `## ` section, from its heading to the next heading of any level."""
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in ("\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


@pytest.fixture(scope="module")
def ownership(document: str) -> str:
    return unwrapped(section_of(document, OWNERSHIP_SECTION))


@pytest.fixture(scope="module")
def summary(document: str) -> str:
    return unwrapped(section_of(document, SUMMARY_SECTION))


#: The heading of the decision log entry these tests are about.
DECISION_HEADING = ("## 2026-08-05 — The empire upgrade tree belongs to the "
                    "account, except under Solo Self-Found")


@pytest.fixture(scope="module")
def decision_entry() -> str:
    """Just this decision's entry, not the whole of docs/DECISIONS.md.

    WHY THE SLICE MATTERS. An adversarial review found that four of the five
    genre citations asserted below already passed against the version of
    docs/DECISIONS.md from BEFORE this change, because "Path of Exile",
    "Diablo IV", "Last Epoch" and "Grim Dawn" all appear in unrelated entries
    elsewhere in a 5,000-line file. Only "Diablo III" was load-bearing. A check
    that passes without the thing it checks for is worthless, so every
    decision-log assertion in this file now runs against the entry alone.
    """
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {DECISION_HEADING!r}. If it was "
        f"renamed, update DECISION_HEADING here; if it was removed, the empire "
        f"tree ownership and scoping decisions have lost their reasoning.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


# --------------------------------------------------------------------------
# Who owns it
# --------------------------------------------------------------------------

def test_the_tree_belongs_to_the_account_not_the_character(ownership):
    """The whole of issue #273. Before this the document said only that points
    persist "through all runs", which says nothing about characters. Answered
    2026-08-05: the account, and since #277, the account scoped by lethality
    mode."""
    assert "belongs to the account, not to the character" in ownership, (
        "the Empire-Wide Upgrades section no longer says who owns the empire "
        "upgrade tree. Answered 2026-08-05: the account, scoped to the "
        "lethality mode. Issues #273 and #277.")


def test_it_says_what_a_new_character_inherits(ownership):
    """Stating the owner is not enough on its own; a reader wants the
    consequence, which is what rerolling costs.

    THE FIRST ASSERTION USED TO STOP AT "shares one tree", which was the
    pre-#277 claim verbatim. An adversarial review restored the whole pre-#277
    paragraph, leaving the section stating unscoped sharing in one place and
    scoped sharing three paragraphs later, and every test passed. It now runs to
    the end of the clause that scopes it."""
    assert ("Every character on the account shares one tree with every other "
            "character in the same lethality mode") in ownership, (
        "the Empire-Wide Upgrades section says characters share one tree "
        "without saying they share it only with characters in the same "
        "lethality mode. That is the pre-#277 wording and it is no longer "
        "true.")
    assert "levels and gear and nothing else" in ownership, (
        "the section no longer says what making a new character costs. It costs "
        "that character's levels and gear, not the empire progression. "
        "Issue #273.")


def test_solo_self_found_gets_its_own_tree(ownership):
    """The one exception, and the only part of the answer that was not the
    recommendation the issue offered."""
    assert "own empire tree" in ownership and "shared with nothing" in ownership, (
        "the section no longer states the Solo Self-Found exception. A Solo "
        "Self-Found character has its own empire tree and inherits nothing. "
        "Issue #273.")


def test_a_solo_self_found_tree_is_private_from_its_own_mode_too(ownership):
    """"Shared with nothing" is doing a lot of work and a reader can misread it
    two ways: as still joining its lethality mode's pool, or as one shared Solo
    Self-Found pool. Neither is right, and the account arithmetic depends on it:
    the count is three plus one per Solo Self-Found character precisely because
    two Solo Self-Found characters do not share with each other."""
    assert "not with other Solo Self-Found characters" in ownership, (
        "the section no longer rules out two Solo Self-Found characters sharing "
        "a tree with each other. They do not. Path of Exile does the opposite "
        "and pools its Solo Self-Found characters, so the reading is a natural "
        "one to fall into. Issues #273 and #277.")


def test_the_exception_says_why_rather_than_only_what(ownership):
    """A rule with no reason gets deleted by the next person who reads it and
    thinks it is an oversight."""
    assert "no auction house and no shared stash" in ownership, (
        "the Solo Self-Found exception no longer says why it exists. It exists "
        "because the flag already closes off the auction house and the shared "
        "stash, and the empire tree is the larger of the three. Issue #273.")


# --------------------------------------------------------------------------
# Both sections carry it
# --------------------------------------------------------------------------

def test_the_meta_progression_summary_also_states_the_ownership_rule(summary):
    """Two sections describe this system and a reader may arrive at either. The
    summary section is the one that makes the "no run is wasted" claim, so a
    reader who lands there and not on the other must still learn that the tree
    survives a new character."""
    assert "persists across characters as well as across runs" in summary, (
        f"the {SUMMARY_SECTION} section no longer says the empire tree survives "
        "a new character. It is the section that promises no run is wasted, so "
        "it is the one that must say across what. Issue #273.")


def test_the_summary_section_also_names_the_exception(summary):
    """Half a rule is worse than none: a reader who sees only "it belongs to the
    account" would build a Solo Self-Found character expecting to inherit."""
    assert "Solo Self-Found" in summary, (
        f"the {SUMMARY_SECTION} section states the ownership rule without its "
        "one exception. Issue #273.")


def test_the_summary_section_also_carries_the_lethality_mode_scope(summary):
    """Same argument one step further. This section is the one that makes the
    "no run is wasted" promise, so it is the section where a reader is most
    likely to over-read what the tree gives them. It has to carry the scope, not
    only the ownership rule."""
    assert "within one" in summary and "lethality mode" in summary, (
        f"the {SUMMARY_SECTION} section states that the tree persists across "
        f"characters without saying that it does so only within one lethality "
        f"mode. That is the section promising no run is wasted, so it is the "
        f"one that must say across what. Issue #277.")


def test_the_summary_section_qualifies_the_no_run_is_wasted_promise(summary):
    """The claim is still true and is now narrower, and the narrowing is exactly
    what a player would feel as a broken promise if nobody wrote it down. A run
    played on Standard genuinely does nothing for a Heretic character."""
    assert "no run within a mode is wasted" in summary, (
        f"the {SUMMARY_SECTION} section makes the 'no run is wasted' promise "
        f"without saying it holds within a lethality mode and not across one. "
        f"Diablo IV shipped an unqualified 'account-wide' claim over a "
        f"partitioned system and players reported it as a defect. Issue #277.")


# --------------------------------------------------------------------------
# What it holds up, and what it does not settle
# --------------------------------------------------------------------------

def test_it_connects_to_the_character_creation_lock(ownership):
    """Issue #255 locked the lethality mode and the Solo Self-Found flag at
    character creation, and the argument for locking was that rerolling is
    affordable. That is only true because of this rule. If either rule is
    changed alone the pair stops making sense, so the document says so."""
    assert "locked at character creation" in ownership, (
        "the Empire-Wide Upgrades section no longer connects account-wide "
        "progression to the character-creation lock. Making a new character is "
        "the only way to change either flag, which is why the cost of a new "
        "character matters. Issues #255 and #273.")


def test_the_sharing_is_scoped_to_the_lethality_mode(ownership):
    """WHAT THIS USED TO ASSERT. Until 2026-08-05 this test was called
    test_the_open_scoping_question_is_named_with_its_issue and it asserted that
    the section said the scope was "still open" and named issue #277. That was
    correct while the question was unanswered: the project owner's sentence
    scoping the sharing used words from both of the game's two difficulty axes,
    and writing a scope rule on the wrong axis is harder to notice and undo than
    leaving it absent.

    The owner answered on 2026-08-05: the lethality mode. So the property that is
    true now is the rule itself, and this test asserts that instead."""
    assert "sharing is scoped to the lethality mode" in ownership, (
        "the Empire-Wide Upgrades section no longer says what the sharing is "
        "scoped to. Answered 2026-08-05 on issue #277: the lethality mode, not "
        "the difficulty tier.")
    for mode in ("Standard", "Hardcore", "Heretic"):
        assert mode in ownership, (
            f"the scoping rule no longer names {mode}. All three lethality "
            f"modes have to appear, because the rule is that each has its own "
            f"tree and a reader cannot infer the list. Issue #277.")


def test_the_scoping_question_is_no_longer_recorded_as_open(ownership):
    """WHAT THIS USED TO ASSERT. Until 2026-08-05 this was
    test_no_scoping_rule_was_written_while_the_question_is_open, and it failed if
    the document stated a scope rule while still saying the question was open.
    That tripwire worked: it is what forced the open-question paragraph to be
    replaced rather than left beside the answer.

    Now the answer is written, so the property to hold is the reverse. The
    section must not still be telling a reader the question is undecided."""
    for stale in ("still open", "Until that is answered"):
        assert stale not in ownership, (
            f"the Empire-Wide Upgrades section says {stale!r} while also "
            "stating the scoping rule. Issue #277 was answered on 2026-08-05; "
            "the open-question paragraph should have gone when the answer went "
            "in.")


def test_the_points_are_scoped_and_not_only_the_tree(ownership):
    """THE MOST IMPORTANT SENTENCE IN THE RULE, and the easiest to lose.

    The natural way to build "three trees" is one account-wide balance of empire
    upgrade points with three allocations of it. That permits exactly the thing
    the scope exists to prevent: farm points on Standard, spend them into
    Heretic. The rule only does its job if the currency is partitioned too.

    THE FIRST VERSION OF THIS TEST WAS WORTHLESS AND AN ADVERSARIAL REVIEW
    PROVED IT. It asserted the substrings "can only be spent there" and "three
    balances". A reviewer replaced the paragraph with one stating the OPPOSITE
    rule — "An empire upgrade point earned by a character goes into one
    account-wide balance, and can only be spent there. There is a single account
    balance of points that all three trees draw from; there are not three
    balances." — and every test still passed. "can only be spent there" is an
    anaphor whose antecedent was never checked, and "three balances" matches
    inside "not three balances". Both assertions now carry their own subject."""
    assert ("earned into that character's lethality mode, and can only be spent "
            "there") in ownership, (
        "the section no longer says an empire upgrade point can only be spent "
        "in the lethality mode it was earned in. Without that, three trees "
        "drawing on one balance of points lets a player farm on Standard and "
        "spend into Heretic, which is the head start issue #277 closed.")
    assert "there are three balances" in ownership


def test_it_states_how_many_trees_an_account_holds(ownership):
    """The rule is expressed as a sharing relation and a reader has to derive
    the count. The count is what makes the size of the change visible: three
    shared trees plus one per Solo Self-Found character, at 1,248 allocatable
    points each."""
    assert "three shared trees plus one for every Solo Self-Found" in ownership, (
        "the section no longer states how many empire trees an account holds. "
        "It is three shared, one per lethality mode, plus one per Solo "
        "Self-Found character. Issue #277.")


def test_the_partition_is_storage_rather_than_content(ownership):
    """Forecloses the reading that Heretic gets its own tuned tree, which would
    be a large new content commitment nobody has agreed to. There is one node
    graph and three allocations of it."""
    assert "partition is storage, not content" in ownership, (
        "the section no longer says the three trees are three allocations of "
        "one node graph. Without it a reader may take 'Heretic has its own "
        "tree' to mean Heretic has its own node values. Issue #277.")
    assert "Empire_Development_Tree_Final.json" in ownership


def test_it_says_why_the_boundary_cannot_be_crossed(ownership):
    """The strongest argument the rule has, and it costs one sentence. Because
    the lethality mode is locked at character creation (issue #255), a character
    stays in one partition for its whole life and the boundary needs no
    enforcement beyond that."""
    assert "Nothing crosses the boundary" in ownership, (
        "the section no longer says why the partition holds. It holds because "
        "the lethality mode is locked at character creation, so no character "
        "ever changes partition. Issues #255 and #277.")


def test_the_boundary_covers_the_stash_and_the_auction_house_too(ownership):
    """WHAT THIS USED TO ASSERT. Until 2026-08-05 this was
    test_it_admits_the_boundary_is_claimed_for_the_tree_only, and it checked that
    the section admitted the shared stash and the auction house were NOT
    partitioned, naming issue #285 as the open question. The project owner
    answered #285 on 2026-08-05: partition both. So the property to hold is the
    opposite one, and the admission would now be false.

    A reader who takes 'the empire tree is partitioned' to mean gear can still
    cross would be wrong, and that is the reading the section has to close."""
    assert "shared stash and the auction house are partitioned" in ownership, (
        "the Empire-Wide Upgrades section no longer states that the shared "
        "stash and the auction house are partitioned by lethality mode. That "
        "was decided on issue #285 on 2026-08-05. Without it a reader may take "
        "the tree partition as the only one and assume gear still crosses.")
    assert "not partitioned" not in ownership, (
        "the section still says something is not partitioned. Issue #285 "
        "settled that everything the account shares is. If a new unpartitioned "
        "thing has been found, it needs its own issue rather than a sentence "
        "here that contradicts the rule two paragraphs above it.")


def test_it_states_the_partition_rule_generally_rather_than_as_a_list(ownership):
    """A list of partitioned things has no answer for the next thing added. The
    empire tree, the stash and the auction house were each decided separately
    (#277 and #285) and each time the answer was the same. Stating the general
    rule means a future shared feature — a shared crafting bench, an account
    achievement track — arrives with its answer already written."""
    assert "anything the account shares between characters is held once per " \
           "lethality mode" in ownership.lower(), (
        "the section lists what is partitioned instead of stating the rule "
        "that decides it. Issues #277 and #285 both landed on the same rule: "
        "anything the account shares is held once per lethality mode. Without "
        "the general form, the next shared thing has no answer.")


def test_it_says_what_needs_no_partition_rule_and_why(ownership):
    """The rule is only half an answer without its complement. A character's own
    possessions need no partition, and the reason is the same one that makes the
    whole boundary hold: the lethality mode is locked at character creation
    (#255), so a character never changes partition."""
    assert "never changes mode" in ownership, (
        "the section states what is partitioned but not what needs no "
        "partition. Anything a character holds by itself is already safe, "
        "because the lethality mode is locked at character creation. Leaving "
        "that out invites a partition rule for every per-character thing.")


def test_the_two_questions_the_partition_left_open_are_answered(ownership):
    """WHAT THIS USED TO ASSERT. That the section named issues #305 and #306 as
    open: the stash had been partitioned before anything described it, and
    nobody had said whether gold was held by the character or the account.

    Both were answered on 2026-08-05 and the sentence naming them was removed,
    so this test failed. That is the right failure for the wrong-looking reason,
    and rewriting it is the fix -- it now asserts the answers are here.

    The gold answer belongs in THIS section specifically. This is where the
    general rule lives ("anything the account shares between characters is held
    once per lethality mode"), and that rule was deliberately written to hold
    whichever way #306 went. Now that it is answered, the section has to say
    which of its two branches gold is in, or the rule reads as complete while
    leaving its most-used case unstated.
    """
    assert "Gold is one of the things the account shares" in ownership, (
        "the Empire-Wide Upgrades section states the general partition rule "
        "without saying gold falls under it. Issue #306 settled that gold is "
        "held by the account, once per lethality mode.")
    assert "The Storage section states it in full" in ownership, (
        "the section states the gold rule without pointing at where the stash "
        "and gold are described together. Issues #305 and #306.")
    assert "#305" not in ownership and "#306" not in ownership, (
        "the section still cites #305 or #306 as questions it leaves open. "
        "Both were answered on 2026-08-05: the Storage section describes the "
        "stash, and gold is held by the account once per lethality mode.")


def test_the_old_claim_that_a_harder_mode_is_cheap_is_gone(ownership):
    """A sentence that became FALSE when the scope rule landed, and which no
    test would otherwise have caught.

    Before 2026-08-05 the section said account-wide progression "is what makes
    trying a different class, or a harder lethality mode, cheap enough to be
    worth doing". Under the scope rule a harder lethality mode is not cheap: it
    means starting that mode's tree from nothing. A different class still is."""
    assert "or a harder lethality mode, cheap" not in ownership, (
        "the section claims again that account-wide progression makes trying a "
        "harder lethality mode cheap. It does not, since #277 scoped the "
        "sharing to the lethality mode. Trying a different CLASS is cheap; a "
        "harder MODE costs the whole tree.")
    assert "starts that mode's tree from nothing" in ownership


def test_the_decision_log_records_the_reasoning(decision_entry):
    """The design document states rules; the reasoning lives in the decision
    log, which is where this project keeps it."""
    assert "belongs to the account, except under Solo Self-Found" in decision_entry
    assert "Issue #273" in decision_entry, (
        "the docs/DECISIONS.md entry no longer names issue #273, which settled "
        "who owns the empire upgrade tree.")


def test_the_decision_log_records_the_scope_and_its_evidence(decision_entry):
    """WHAT THIS USED TO ASSERT. Until 2026-08-05 this was
    test_the_decision_log_records_what_was_left_unwritten, and it checked that
    the log entry admitted the scoping half of the answer had not been written.
    That half is written now, so the property to hold is that the log carries
    the reasoning for it.

    CLAUDE.md requires a design decision to cite how shipped games in the genre
    do it. Six were surveyed and none partitions meta-progression by a numeric
    difficulty step, which is the whole argument for the lethality mode over the
    difficulty tier. This runs against the entry alone, because an adversarial
    review showed four of these five names already appeared elsewhere in
    docs/DECISIONS.md and so could not fail."""
    # The "## " matters. The entry's own Affects line names this heading in a
    # cross-reference, so matching the bare words would pass on an entry whose
    # section had been renamed or deleted.
    assert "## What the sharing is scoped to" in decision_entry, (
        "docs/DECISIONS.md no longer records what the empire tree sharing is "
        "scoped to. Answered 2026-08-05 on issue #277: the lethality mode.")
    for game in ("Path of Exile", "Diablo III", "Diablo IV", "Last Epoch",
                 "Grim Dawn"):
        assert game in decision_entry, (
            f"the decision log entry no longer cites {game}. The argument for "
            f"the lethality mode over the difficulty tier is that no shipped "
            f"game partitions meta-progression by a numeric difficulty step, "
            f"and that argument is only as good as the games behind it.")


def test_the_decision_log_cites_sources_for_its_genre_claims(decision_entry):
    """CLAUDE.md says to look up how the genre does it and cite sources rather
    than inventing a rule. The entry quotes a game director, a bug report title
    and a player thread, and names a patch and a season. An adversarial review
    found none of them was cited, while the sibling entry on the same design
    document section ends with four source links."""
    assert "Sources:" in decision_entry, (
        "the docs/DECISIONS.md entry makes claims about six shipped games and "
        "cites nothing. Every other entry that argues from the genre ends with "
        "a Sources list.")
    assert decision_entry.count("https://") >= 5, (
        "the entry cites fewer than five sources for a six-game survey. The "
        "load-bearing ones are the Path of Exile Atlas partition, the Diablo "
        "III Paragon tallies, the Diablo IV Altars behaviour and its later "
        "removal, and the Grim Dawn difficulty page.")


def test_the_decision_log_marks_which_claims_are_less_certain(decision_entry):
    """CLAUDE.md: if the evidence for a result was compromised, say so first
    rather than after. Not every claim in the survey came from a primary source,
    and the decision leans partly on an ABSENCE — that no game shares a
    spendable meta-progression tree across hardcore and softcore — which is
    weaker evidence than a positive finding."""
    assert "Confidence is not uniform" in decision_entry, (
        "the docs/DECISIONS.md entry presents every genre claim at the same "
        "confidence. Two were not confirmed against a primary source and the "
        "central argument rests on an absence. Saying so is the project rule.")


def test_the_decision_log_records_what_argues_against_the_decision(decision_entry):
    """A decision log that only carries the case FOR a decision is a record of
    an argument, not of a decision. Two shipped games retreated from exactly
    this partition: Diablo IV removed the permanent power rather than keep
    making players re-earn it, and Last Epoch answered the same complaint with
    catch-up mechanics. If the re-grind cost turns out to be the problem here,
    whoever reads this next should find that already written down."""
    assert "What argues against it" in decision_entry, (
        "the docs/DECISIONS.md entry no longer records the case against "
        "partitioning by lethality mode. It is a real case and it comes from "
        "two studios that shipped this partition and then retreated from it. "
        "Issue #277.")
    assert "catch-up" in decision_entry


def test_the_decision_log_lists_what_was_deliberately_left_open(decision_entry):
    """Five questions surfaced while writing the rule and each became its own
    issue rather than being answered in passing. The entry names them so that a
    reader does not mistake the rule for a complete account of the partition."""
    for issue in ("#285", "#286", "#287", "#288", "#289"):
        assert issue in decision_entry, (
            f"the docs/DECISIONS.md entry no longer names {issue}, one of the "
            f"five questions the scoping rule deliberately did not settle. "
            f"Either it has been answered and belongs in the entry, or it "
            f"should not have been dropped.")


# --------------------------------------------------------------------------
# What happens to a Solo Self-Found tree when its only owner is lost
#
# Issue #286. `docs/Cataclysm_GDD_v2.md` says a character consumed by Worn
# Residue is gone and that "Empire progress is kept". That sentence predates the
# ownership rule above. For an ordinary character it is obvious who keeps the
# progress -- the tree never belonged to the character. A Solo Self-Found
# character is the ONLY owner of its tree, so consumption destroys the owner and
# the sentence had no referent.
#
# DECIDED by the project owner, 2026-08-05, on issue #286: "Regardless of what
# mode you're playing on, the empire tree persists." So the tree is never
# destroyed. A lost Solo Self-Found character's private tree is held and the next
# Solo Self-Found character created in that lethality mode inherits it.
#
# THE SAME ANSWER ALSO SAID "you restart the tier you were on while keeping your
# gear/levels/empire tree", which would mean a run ending never costs the
# character at all. That was NOT decided here; it was issue #315.
#
# ISSUE #315 HAS SINCE BEEN ANSWERED, on 2026-08-05, and the answer is that a run
# ending does not cost the character. Being consumed by Worn Residue puts a
# snapshot of the character into the corrupted-character pool and ends the run;
# the character itself is untouched. So NOTHING in the design destroys a
# character, and the inheritance rule the tests below check is a safeguard with
# no trigger rather than a live case. The section says so, and issue #325 asks
# whether a player can delete a character, which would give it one.
# `tools/tests/test_what_a_run_ending_costs.py` holds the checks for that
# answer; the tests below are unchanged because the rule they check is.
# --------------------------------------------------------------------------

#: The section describing what being consumed by Worn Residue costs.
CONSUMPTION_SECTION = "## **Worn Residue and Consumption**"

#: The decision log entry for the rule below.
SURVIVAL_DECISION_HEADING = ("## 2026-08-05 — A Solo Self-Found empire tree "
                             "survives the character that earned it")


@pytest.fixture(scope="module")
def consumption(document: str) -> str:
    return unwrapped(section_of(document, CONSUMPTION_SECTION))


@pytest.fixture(scope="module")
def survival_entry() -> str:
    """Just this decision's entry, for the reason the fixture above gives: the
    names in it appear in other entries of a 5,000-line file."""
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(SURVIVAL_DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {SURVIVAL_DECISION_HEADING!r}. "
        f"That entry carries the owner's answer on issue #286 and the reasoning "
        f"for it, and it is the only place either is written down.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


def test_the_tree_is_never_destroyed_in_any_mode(ownership):
    """The rule itself, stated generally rather than only for the Solo
    Self-Found case, because the general form is what the owner answered and it
    covers whatever else can destroy a character later."""
    assert "never destroyed, in any mode, including Solo Self-Found" in ownership, (
        "the Empire-Wide Upgrades section no longer states that an empire tree "
        "survives losing the character that earned it. For a Solo Self-Found "
        "character that character is the tree's only owner, so without this "
        "rule 'Empire progress is kept' has no referent. Issue #286.")


def test_it_says_who_inherits_a_lost_solo_self_found_tree(ownership):
    """Saying the tree is not destroyed is half an answer. A tree nobody can
    ever spend is reading B from the issue, which is destruction with extra
    bookkeeping. The section has to name the character that receives it."""
    assert ("the next Solo Self-Found character created in the same lethality "
            "mode inherits it rather than starting from nothing") in ownership, (
        "the section says a lost Solo Self-Found tree is kept without saying "
        "who inherits it. A tree no character can spend is the same as a "
        "destroyed one. Issue #286.")


def test_it_separates_the_successor_from_a_second_concurrent_character(ownership):
    """This is the sentence that stops the new rule reading as a contradiction
    of the old one. The section still says a second Solo Self-Found character
    starts from nothing, and that stays true: it is about a character played
    ALONGSIDE the first, not the replacement for one that was lost."""
    assert "still alive still starts from nothing" in ownership, (
        "the section grants a lost Solo Self-Found character's tree to the next "
        "one without saying that a second character created while the first is "
        "alive still starts from nothing. Those are different cases and the "
        "section states both rules, so it has to say which is which. "
        "Issue #286.")


def test_the_survival_rule_says_why_rather_than_only_what(ownership):
    """Same argument as the exception above. A rule with no reason gets deleted
    by the next reader who thinks it is an oversight -- and this one looks like
    a softening of the game's harshest mechanic, so it will attract that."""
    assert "Why the tree survives its owner" in ownership, (
        "the section states that a tree outlives its owner without saying why. "
        "The reason is that nothing else in the design destroys empire upgrade "
        "points, and that destroying them would fall only on Solo Self-Found, "
        "which is already the harshest flag. Issue #286.")
    assert "run is never wasted" in ownership


def test_the_consumption_bullet_no_longer_says_kept_without_saying_by_whom(
        consumption):
    """The sentence this issue was about. "Empire progress is kept" was true for
    an ordinary character and undefined for a Solo Self-Found one."""
    assert ("Empire progress is kept, in every mode, including Solo Self-Found"
            in consumption), (
        "the Worn Residue consumption bullet says empire progress is kept "
        "without saying it holds for Solo Self-Found, where the consumed "
        "character was the tree's only owner. Issue #286.")


def test_the_consumption_bullet_points_at_the_rule(consumption):
    """A reader arriving at the consumption bullet must be able to find out what
    happens to the private tree without already knowing the ownership rule."""
    assert "Empire-Wide Upgrades section" in consumption, (
        "the consumption bullet no longer points at the section that says what "
        "happens to a Solo Self-Found character's tree. Issue #286.")
    assert "only owner of its tree" in consumption, (
        "the consumption bullet does not say why Solo Self-Found needs its own "
        "answer here. It needs one because the consumed character owned the "
        "tree outright. Issue #286.")


def test_the_summary_section_no_longer_says_it_inherits_nothing_at_all(summary):
    """WHAT THIS USED TO BE. The summary section said a Solo Self-Found
    character "has its own tree and inherits nothing at all". That is now false
    in one case: the successor to a lost Solo Self-Found character inherits its
    tree. A reader who lands on this section and not the other must not be told
    the old rule."""
    assert "inherits nothing at all" not in summary, (
        "the Roguelike Meta Progression section still says a Solo Self-Found "
        "character inherits nothing at all. Since issue #286 it inherits the "
        "tree of a lost Solo Self-Found character in the same lethality mode.")
    assert "inherits nothing from the account" in summary
    assert "takes over its tree" in summary, (
        "the Roguelike Meta Progression section no longer carries the "
        "inheritance rule. It is the section that promises no run is wasted, so "
        "a reader who lands there must learn that losing a character does not "
        "lose the tree. Issue #286.")


def test_the_no_run_is_wasted_promise_covers_losing_the_character(summary):
    """The promise is the reason this decision went the way it did, so the
    section that makes the promise should say it survives losing a character."""
    assert "losing the character does not destroy the tree it built" in summary, (
        "the 'no run is wasted' paragraph does not say that losing the "
        "character keeps the tree. That is the strongest case for the rule and "
        "the section making the promise is where it belongs. Issue #286.")


def test_the_decision_log_quotes_the_owner_rather_than_paraphrasing(
        survival_entry):
    """The answer was three sentences and only the first was applied. A
    paraphrase would hide that, and the second sentence is issue #315."""
    assert ("Regardless of what mode you're playing on, the empire tree persists"
            in survival_entry), (
        "the docs/DECISIONS.md entry no longer quotes the owner's answer on "
        "issue #286. It is the whole basis of the rule and part of it was "
        "deliberately not applied, so the exact words matter.")


def test_the_decision_log_records_what_argues_against_the_survival_rule(
        survival_entry):
    """This rule softens the mechanic the document calls the Forge's only
    permanent cost. Whoever reads this next should find that already written
    down rather than discover it as an objection."""
    assert "The case against" in survival_entry, (
        "the docs/DECISIONS.md entry for issue #286 records no case against. "
        "There is one: Worn Residue is described as the only way the Forge can "
        "cost a player anything permanent, and for Solo Self-Found this "
        "removes the largest part of that cost.")
    assert ("only way the Forge can cost a player anything permanent"
            in survival_entry)


def test_the_decision_log_says_which_half_of_the_answer_was_not_applied(
        survival_entry):
    """CLAUDE.md: say what did not work, or was skipped, plainly. The owner's
    second sentence would remove the Worn Residue mechanic's cost entirely and
    was not acted on. Without this, a later reader comparing the answer with the
    document would think the answer had been applied wrongly."""
    assert "#315" in survival_entry, (
        "the docs/DECISIONS.md entry does not name the issue holding the part "
        "of the owner's answer that was NOT decided -- that a run ending keeps "
        "the character's gear and levels. Issue #315.")
    assert "was not decided here" in survival_entry
