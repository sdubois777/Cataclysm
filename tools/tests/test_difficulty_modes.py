"""Difficulty modes: two independent axes, and every effect carries a number.

WHY THIS EXISTS. Issue #32. The Difficulty Options table in
`docs/Cataclysm_GDD_v2.md`, the main design document, listed four entries as if
they were one mutually exclusive choice: Standard, SSF (Solo Self-Found),
Hardcore and Heretic. SSF does not belong on that list. It changes loot rules and
disables the auction house without changing how easily a player dies, so it is a
flag that combines with a lethality mode, not one of the alternatives to it.

Three of the four entries also said "Increased loot drops" with no number, and
Hardcore said "each piece of equipment has a chance to drop on death" with no
probability. A rule with no number in it is not a rule; it is a note saying
somebody should decide later, and nothing was tracking that it had not happened.

WHAT THE GENRE SETTLED, with sources in `docs/DECISIONS.md`:

    Solo Self-Found is a flag, not a mode.   Last Epoch lets one character carry
                                             Hardcore and Solo Challenge at once.
                                             Path of Exile ships Hardcore SSF.
    No mode grants extra loot.               Path of Exile's SSF has drop rates
                                             identical to trade. Diablo IV
                                             attaches drops to the World Tier,
                                             which is the difficulty, not to the
                                             Hardcore flag.
    Per-piece drop chance on death.          Tibia drops each equipped item with
                                             a 10% chance when unblessed. That is
                                             the mechanic the document already
                                             described, so only the number was
                                             missing.

WHEN THE CHOICES ARE MADE. Issue #255. The section defined the two axes but never
said when a player picks them or whether the pick can move. The project owner
answered on 2026-08-05: both are locked at character creation and never change,
including on death. `docs/DECISIONS.md` records why, and why Last Epoch and Path
of Exile converting a dead Hardcore character does not transfer here.

WHAT IS ASSERTED HERE.

    the document has a lethality table and a separate Solo Self-Found table
    it says plainly that the two combine
    Casual is still gone, and no mode promises unquantified extra loot
    every lethality mode states a day cost and an equipment loss rule
    Heretic loses more equipment than Hardcore, which is the whole ladder
    the risk table's list of modes matches the lethality table
    both choices are locked at character creation, death does not unset the
      lethality mode, and Solo Self-Found never comes off
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

SECTION = "### **Difficulty Options**"
SECTION_ENDS = "## **Controls and Key Bindings**"

#: The three mutually exclusive lethality modes, in increasing harshness.
LETHALITY_MODES = ("Standard", "Hardcore", "Heretic")

#: How many pieces of equipment a character wears. Read from the model so this
#: file and `sim/cataclysm_sim/affixes.py` cannot disagree about it.
def gear_pieces() -> int:
    from cataclysm_sim import affixes
    return affixes.GEAR_PIECES


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def section(document: str) -> str:
    start = document.find(SECTION)
    assert start != -1, f"the design document has no {SECTION} section"
    end = document.find(SECTION_ENDS, start)
    assert end != -1, f"the {SECTION} section has no end marker"
    return document[start:end]


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


def lethality_table(section: str) -> str:
    """Just the table of mutually exclusive modes, without the prose around it."""
    start = section.index("**Lethality mode. Choose one.**")
    return section[start:section.index("**Solo Self-Found. Optional", start)]


def row_for(section: str, mode: str) -> str:
    """The table row whose first cell names `mode`."""
    for line in section.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if cells and cells[0] == mode:
            return line
    raise AssertionError(f"the Difficulty Options section has no row for {mode}")


# --------------------------------------------------------------------------
# The two axes
# --------------------------------------------------------------------------

def test_it_says_the_two_choices_are_independent(section):
    assert "two independent choices" in section, (
        "the Difficulty Options section no longer says the lethality mode and "
        "the Solo Self-Found flag are separate choices. Issue #32.")


def test_solo_self_found_is_not_listed_among_the_lethality_modes(section):
    """It was listed as if choosing it meant not choosing Hardcore. Last Epoch
    ships one character carrying both."""
    table = lethality_table(section)
    assert "SSF" not in table and "Self-Found" not in table, (
        "Solo Self-Found is back in the lethality mode table. It changes loot "
        "rules and the auction house, not how easily a player dies, so it "
        "combines with a lethality mode rather than replacing one. Issue #32.")


def test_it_names_a_combination_so_the_rule_is_unmissable(section):
    """A reader skims tables. The combined names have to appear in prose."""
    text = unwrapped(section)
    assert "Hardcore Solo Self-Found" in text
    assert "Heretic Solo Self-Found" in text


def test_solo_self_found_has_its_own_table_and_keeps_its_two_rules(section):
    row = row_for(section, "Solo Self-Found (SSF)")
    assert "No auction house" in row
    assert "no shared stash" in row


# --------------------------------------------------------------------------
# When the choices are made, and that they never move afterwards
# --------------------------------------------------------------------------

def test_the_difficulty_section_sits_inside_character_creation(document):
    """Where the section lives is half of the answer: the choices are made when
    the character is made. If someone moves this section out to a settings or
    options chapter, the placement stops saying that and the prose below is the
    only thing left holding the rule up."""
    creation = document.find("## **Character Creation and Customization**")
    assert creation != -1, "the document has no Character Creation chapter"
    difficulty = document.find(SECTION, creation)
    assert difficulty != -1, (
        f"{SECTION} no longer appears after the Character Creation chapter. "
        "The choices are made at character creation, so the section belongs "
        "there. Issue #255.")
    following = document.find("\n## ", creation + 1)
    assert following == -1 or difficulty < following, (
        f"{SECTION} has moved out of the Character Creation chapter. "
        "Issue #255.")


def test_it_says_both_choices_are_locked_at_character_creation(section):
    """The rule the project owner chose on 2026-08-05, out of three options."""
    text = unwrapped(section)
    assert "locked at character creation and cannot be changed" in text, (
        "the Difficulty Options section no longer says when the lethality mode "
        "and the Solo Self-Found flag are chosen or whether they can move. "
        "That was the whole of issue #255.")


def test_dying_does_not_take_the_lethality_mode_off(section):
    """The one place this could have gone the other way. Last Epoch and Path of
    Exile both convert a dead Hardcore character, but they do it because the
    character is dead and unusable. Here the run continues, so converting would
    change a live run's rules on one unlucky moment."""
    text = unwrapped(section)
    assert "dying does not change it" in text, (
        "the section no longer says what a Hardcore or Heretic death does to "
        "the lethality mode. It does nothing to it: the character stays in the "
        "mode and pays the day cost. Issue #255.")
    assert "stays Hardcore or Heretic" in text


def test_the_solo_self_found_flag_never_comes_off(section):
    """Without this the flag is worth nothing: a player runs self-found until it
    is inconvenient and then switches the auction house on."""
    text = unwrapped(section)
    assert "Solo Self-Found flag never comes off" in text, (
        "the section no longer says the Solo Self-Found flag is permanent. "
        "Issue #255.")
    assert "can never use the auction house" in text


def test_it_says_what_a_player_does_who_wants_a_different_combination(section):
    """A rule that forbids something has to say what to do instead, or the
    section reads as an oversight rather than a decision."""
    text = unwrapped(section)
    assert "makes a new character" in text, (
        "the section forbids changing the difficulty choices without saying "
        "what a player who wants a different combination does instead. "
        "Issue #255.")


def test_the_section_names_the_empire_tree_cost_of_choosing_a_mode(section):
    """Issue #277 scoped the empire upgrade tree to the lethality mode, so
    choosing Hardcore or Heretic for the first time means starting that mode's
    tree from nothing. That is the largest cost of the choice and it is
    permanent, and this is the section where the player makes the choice.

    WHY IT HAS TO BE HERE AND NOT ONLY IN THE EMPIRE SECTION. The empire rule
    lives about 2,400 lines further down the document. A player reading this
    table is told what dying costs, what equipment it takes and what the display
    shows, and was told nothing about the meta-progression restart. Diablo IV
    shipped a partitioned system while describing it as account-wide and players
    reported the partition as a defect; the fix for that is saying so where the
    choice is made.

    THE FIRST VERSION OF THIS TEST WAS WORTHLESS AND AN ADVERSARIAL REVIEW
    PROVED IT. It asserted the substrings "own empire upgrade tree" and
    "starting that mode's tree from nothing". A reviewer replaced the paragraph
    with one stating the OPPOSITE rule — "No lethality mode has its own empire
    upgrade tree ... so choosing Hardcore or Heretic never means starting that
    mode's tree from nothing." — and every test still passed, because both
    substrings survive inside the negations. Each assertion now carries enough
    of its sentence to fix its meaning."""
    text = unwrapped(section)
    assert "Each lethality mode has its own empire upgrade tree" in text, (
        "the Difficulty Options section no longer says that each lethality mode "
        "has its own empire upgrade tree. It is the largest and most permanent "
        "cost of choosing a harder mode, and this is where the player chooses. "
        "Issue #277.")
    assert "means starting that mode's tree from nothing" in text


def test_the_section_also_names_the_solo_self_found_tree(section):
    """Half a rule at the point of choice is worse than none, and this is the
    harsher half. The Solo Self-Found table row lists what the flag takes away —
    the auction house and the shared stash — and said nothing about the empire
    tree, which the decision log calls "the largest handout of the three" and
    "the strictest version anyone has shipped".

    It is also stricter than the mode rule this section already carries: a Solo
    Self-Found tree is private to one character, so a second Solo Self-Found
    character starts from nothing again."""
    text = unwrapped(section)
    assert "Solo Self-Found is stricter than that" in text, (
        "the Difficulty Options section states the lethality mode partition "
        "without the Solo Self-Found one, which is harsher. A player ticking "
        "that box is told what it takes away and not that it also starts the "
        "empire tree from nothing. Issues #273 and #277.")
    assert "not with another Solo Self-Found character" in text


def test_the_section_names_the_stash_and_auction_house_partition(section):
    """Issue #285, answered by the project owner on 2026-08-05: the shared stash
    and the auction house are partitioned by lethality mode, as the empire tree
    already was.

    WHY IT HAS TO BE HERE. Same argument as the empire tree test above. This is
    the screen where the choice is made, and a player choosing Heretic needs to
    know that the gear and the market do not come with them. It is arguably a
    larger surprise than the tree, because a stash is a thing the player can see
    the contents of and reasonably expect to still be there.

    The assertion carries enough of the sentence to fix its meaning, because the
    test above records an adversarial review that defeated a shorter one by
    negating the paragraph around a surviving substring."""
    text = unwrapped(section)
    assert "The shared stash and the auction house are partitioned the same way" \
        in text, (
        "the Difficulty Options section no longer says the shared stash and the "
        "auction house are partitioned by lethality mode. That was decided on "
        "issue #285 on 2026-08-05, and this is the section where the player "
        "picks the mode it partitions on.")
    assert "Nothing moves between them" in text


def test_the_section_says_why_sealing_the_tree_alone_was_not_enough(section):
    """A rule with no reason reads as arbitrary and gets removed by the next
    person who finds it inconvenient. The reason here is a comparison of sizes:
    a geared handoff from a mature Standard character is worth more to a first
    Heretic character than any number of empire upgrade points, so partitioning
    the tree alone would have sealed the smaller route and left the larger open.

    This is the argument the project owner accepted on #285, and it is the one
    that answers the obvious objection — that three auction houses split an
    already thin market three ways."""
    text = unwrapped(section)
    assert "larger head start" in text, (
        "the Difficulty Options section states the stash and auction house "
        "partition without the reason for it. The reason is that a geared "
        "handoff is a larger head start than the empire points the tree "
        "partition already blocks. Issue #285.")


def test_the_section_says_solo_self_found_is_unaffected_by_that_partition(section):
    """Solo Self-Found already has no auction house and no shared stash — the
    Solo Self-Found table row says exactly that. So the partition divides
    nothing for it, and a reader who has just been told about three stashes
    needs to be told which of them a Solo Self-Found character gets. The answer
    is none, and it is already true rather than a new rule."""
    text = unwrapped(section)
    assert "A Solo Self-Found character is unaffected, because it has neither" \
        in text, (
        "the Difficulty Options section partitions the stash and the auction "
        "house three ways without saying where that leaves Solo Self-Found, "
        "which has neither. Issue #285.")


def test_the_decision_log_records_the_stash_partition_and_its_cost(document):
    """CLAUDE.md requires a design decision to cite how shipped games in the
    genre do it, and to record what argues against it. Five games were surveyed
    and all five partition the stash on the same axis as their meta-progression.

    The case against is auction house liquidity: three markets in a game whose
    market was never going to be deep, and the thinnest of the three will be
    Heretic, the mode with the fewest players. That belongs written down."""
    decisions = REPO_ROOT / "docs" / "DECISIONS.md"
    assert decisions.is_file(), "docs/DECISIONS.md is missing"
    log = decisions.read_text(encoding="utf-8")
    heading = ("## 2026-08-05 — The shared stash and the auction house are "
               "partitioned by lethality mode")
    assert heading in log, (
        "docs/DECISIONS.md has no entry for the stash and auction house "
        "partition decided on issue #285. The design document states the rule; "
        "this log is where this project keeps the reasoning.")
    entry = log[log.index(heading):]
    entry = entry[:entry.index("\n---", 1)] if "\n---" in entry[1:] else entry
    # The table ROW, not the name anywhere in the entry. Proving these guards
    # caught the looser version: deleting Grim Dawn from the survey table left
    # the name in a later sentence about its two save files, and the test still
    # passed. A survey is its rows.
    rows = {line.strip().strip("|").split("|")[0].strip()
            for line in entry.splitlines() if line.strip().startswith("|")}
    for game in ("Path of Exile", "Diablo III", "Diablo IV", "Last Epoch",
                 "Grim Dawn"):
        assert game in rows, (
            f"the genre survey table in the docs/DECISIONS.md entry for issue "
            f"#285 has no row for {game}. The argument is that no shipped game "
            f"partitions its meta-progression on one axis and its stash on "
            f"another, and that argument is only as good as the games behind "
            f"it. Rows found: {sorted(rows)}")
    assert "What argues against it" in entry, (
        "the entry records only the case for partitioning. Splitting the "
        "auction house three ways is a real cost in a game with a thin market "
        "and it belongs in the log.")
    assert entry.count("https://") >= 5, (
        "the entry makes claims about five shipped games and cites fewer than "
        "five sources.")


def test_the_decision_log_admits_its_sources_are_search_summaries(document):
    """CLAUDE.md: if the evidence for a result was compromised, say so plainly
    and first. None of the five sources for the genre survey was fetched — the
    hosts return HTTP 402 or an access-denied page — so every claim rests on a
    search result summary of the page rather than the page. A later reader
    deciding how much weight to give this needs to know that."""
    decisions = REPO_ROOT / "docs" / "DECISIONS.md"
    log = decisions.read_text(encoding="utf-8")
    heading = ("## 2026-08-05 — The shared stash and the auction house are "
               "partitioned by lethality mode")
    entry = log[log.index(heading):]
    entry = entry[:entry.index("\n---", 1)] if "\n---" in entry[1:] else entry
    assert "search result summaries" in entry, (
        "the docs/DECISIONS.md entry for issue #285 presents five web sources "
        "without saying none of them was actually fetched. Stating that is the "
        "project rule when evidence is second-hand.")


def test_the_decision_log_records_why_no_migration_rule_was_needed(document):
    """The obvious question about partitioning a stash is what happens to items
    already in it. The answer here is that there are none, because no save
    format exists yet. That is a fact about timing, not about design, and it
    will stop being true — so it is recorded now, while it is still cheap."""
    decisions = REPO_ROOT / "docs" / "DECISIONS.md"
    log = decisions.read_text(encoding="utf-8")
    heading = ("## 2026-08-05 — The shared stash and the auction house are "
               "partitioned by lethality mode")
    entry = log[log.index(heading):]
    entry = entry[:entry.index("\n---", 1)] if "\n---" in entry[1:] else entry
    assert "nothing to migrate" in entry, (
        "the docs/DECISIONS.md entry for issue #285 does not say what happens "
        "to items already in a shared stash. The answer is that there are none "
        "because no save format exists, and that answer expires.")


def test_the_capital_services_table_scopes_the_auction_house(document):
    """The Capital Services table is where a player looks up what the auction
    house does. Before #285 its row read 'Buy and sell items (disabled in SSF)',
    which is now incomplete in a way that matters: it is one market per
    lethality mode, so a Heretic character cannot buy what a Standard character
    listed."""
    row = next((line for line in document.splitlines()
                if line.strip().startswith("| Auction House |")), None)
    assert row is not None, (
        "the Capital Services table in docs/Cataclysm_GDD_v2.md has no Auction "
        "House row.")
    assert "One market per lethality mode" in row, (
        "the Capital Services row for the Auction House does not say it is "
        "partitioned by lethality mode. Issue #285 settled that on 2026-08-05, "
        "and this table is where a player looks the service up.")
    assert "disabled in SSF" in row, (
        "the Auction House row lost the rule that Solo Self-Found cannot use "
        "it, which predates #285 and is still true.")


def test_the_decision_log_records_why_the_choices_are_locked(document):
    """The design document states the rule; the reasoning lives in the decision
    log, which is where this project keeps it."""
    decisions = REPO_ROOT / "docs" / "DECISIONS.md"
    assert decisions.is_file(), "docs/DECISIONS.md is missing"
    text = unwrapped(decisions.read_text(encoding="utf-8"))
    assert "locked at character creation" in text, (
        "docs/DECISIONS.md has no entry explaining why the lethality mode and "
        "the Solo Self-Found flag cannot change. Issue #255.")
    assert "Issue #255" in text


# --------------------------------------------------------------------------
# Every effect carries a number
# --------------------------------------------------------------------------

def test_no_mode_promises_increased_loot_without_saying_how_much(section):
    """Three of the four entries said "Increased loot drops" and none said how
    much. The answer taken was that no mode grants any, which is what Path of
    Exile and Diablo IV both do. If that is reversed, it needs a figure."""
    assert not re.search(r"[Ii]ncreased loot drops", section), (
        "a difficulty mode promises increased loot drops with no number again. "
        "Either give it a figure or take it out. Issue #32.")


def test_it_says_where_drop_rate_actually_lives(section):
    assert "No mode grants increased loot." in section
    assert "difficulty tier" in section


def test_every_lethality_mode_states_a_day_cost(section):
    for mode in LETHALITY_MODES:
        row = row_for(section, mode)
        assert re.search(r"\|\s*\d+ days\s*\|", row), (
            f"{mode} does not state what dying costs in days. Issue #32.")


def test_every_lethality_mode_states_what_equipment_it_takes(section):
    """Standard takes none, and saying so is part of specifying it."""
    assert "none" in row_for(section, "Standard")
    for mode in ("Hardcore", "Heretic"):
        row = row_for(section, mode)
        assert re.search(r"\d+% chance", row), (
            f"{mode} says equipment drops on death without a probability. "
            f"That is the gap issue #32 was filed for.")


def test_the_document_counts_the_same_equipment_pieces_as_the_model(section):
    """Hardcore's rule is a per-piece chance, so the piece count is what turns
    it into an amount. It has to be the model's count, not a different one."""
    assert f"{gear_pieces()} equipped pieces" in section


def test_heretic_takes_more_equipment_than_hardcore(section):
    """The ladder. At one shared rate a floor of two pieces sits barely above
    Hardcore's own average, so the two modes would have felt the same on death.
    Read from the stated averages rather than recomputed, because the averages
    are what a reader acts on."""
    averages = {}
    for mode in ("Hardcore", "Heretic"):
        found = re.search(r"so ([\d.]+) on average", row_for(section, mode))
        assert found, f"{mode} does not state an average number of pieces lost"
        averages[mode] = float(found.group(1))
    assert averages["Heretic"] > averages["Hardcore"], averages


def test_the_stated_averages_match_the_stated_rates(section):
    """Each average is what the rate and the floor actually produce over the
    model's equipment count. A typed average that drifts from its own rule is
    the defect this whole file is about, one level down."""
    from math import comb

    pieces = gear_pieces()
    for mode in ("Hardcore", "Heretic"):
        row = row_for(section, mode)
        rate = int(re.search(r"(\d+)% chance", row).group(1)) / 100.0
        stated = float(re.search(r"so ([\d.]+) on average", row).group(1))
        # The floor is part of the rule, so read it rather than assume it.
        least = re.search(r"at least (\d+) always drop", row)
        floor = int(least.group(1)) if least else 0
        expected = sum(max(k, floor) * comb(pieces, k) * rate ** k
                       * (1 - rate) ** (pieces - k) for k in range(pieces + 1))
        assert round(expected, 1) == stated, (
            f"{mode} says {rate:.0%} per piece with a floor of {floor} loses "
            f"{stated} pieces on average, but over {pieces} pieces that is "
            f"{expected:.2f}")


def test_heretic_keeps_the_two_effects_that_were_already_specified(section):
    row = row_for(section, "Heretic")
    assert "25% more dungeons" in row
    assert "2 upgrade slots instead of 3" in row


# --------------------------------------------------------------------------
# What the rest of the document says about them
# --------------------------------------------------------------------------

def test_casual_mode_is_still_gone(document):
    """It was referenced in the risk table and defined nowhere. Removed rather
    than defined. Kept out because it read like an ordinary mode name."""
    assert not re.search(r"\bCasual/", document), (
        "the risk table names a Casual difficulty mode again. It is not defined "
        "anywhere. Issue #32.")


def test_the_risk_table_names_the_lethality_modes_and_nothing_else(document):
    """It said "(Casual/Standard/HC/Heretic)", which named one mode that does
    not exist and one that is not a lethality mode."""
    row = next(line for line in document.splitlines()
               if "Time pressure mechanics" in line)
    for mode in LETHALITY_MODES:
        assert mode in row, f"the risk table no longer names {mode}"
    assert "SSF" not in row and "Self-Found" not in row, (
        "the risk table lists Solo Self-Found as a way to tune urgency. It does "
        "not change urgency; it changes where items come from. Issue #32.")


def test_hiding_the_display_is_still_a_lethality_mode_effect(section):
    """Settled on this issue earlier: it is a difficulty choice a player opts
    into, not an accessibility failure. Both harsher modes hide it."""
    assert "map overlay only" in row_for(section, "Hardcore")
    assert "hidden" in row_for(section, "Heretic")
    assert "shown" in row_for(section, "Standard")
