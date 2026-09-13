"""The design document's Last Stand figures agree with each other and with the model.

WHY THIS EXISTS. Issue #1682. `docs/Cataclysm_GDD_v2.md` stated a Last Stand
clear rate of 1 in 24.6 and, four lines below it, a 95% interval of 1 in 28 to
1 in 42 -- **an interval that does not contain its own headline figure**. The
campaign figures had been re-measured on 2026-09-07 and the paragraph saying how
well they were resolved was left describing the 2026-09-06 run it replaced,
byte for byte.

**Nothing noticed.** 136 test files name that document; none of them read these
figures. The refresh moved one paragraph, left the next, and no check anywhere
compared the two.

A second statement was stale in a way no search for numbers would find: the
document said the Last Stand is "met in **most** campaigns" when the share had
fallen to 45.9%. That is a **word** carrying a claim about a figure, which is
why this file checks the wording as well as the digits.

WHAT IS ASSERTED HERE.

    the clear rate stated in the headline falls inside the 95% interval stated
      for it four lines later -- the exact contradiction that was shipped
    the headline figures are the ones `sim/cataclysm_sim/engine.py` records for
      the run it names, which is where they were measured
    the resolution figures -- five block standard deviations, the six-block
      range and the pooled interval -- are that same record's
    "read the clear rate as about one in N" names the clear rate the document
      states, so the words and the digits cannot drift apart
    a sentence claiming the fight is met in most campaigns is only allowed while
      the share is above half, and an unrecognised rewording of that sentence
      fails rather than passing unchecked
    no statement from the superseded run is live again, while the dated history
      table that legitimately holds those figures is still there
    both files parsed, so none of the above can pass on an empty string

WHAT THIS CANNOT CHECK is whether the measurement itself is right. It compares
the design document against the simulation's own record of what it measured; if
that record is wrong, both agree and both are wrong. What it removes is the
failure that happened here, where the two disagreed and nothing said so.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
ENGINE = REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py"

#: A number, NOT swallowing the full stop that ends the sentence it sits in.
#: `[\d.]+` reads "1 in 30." as "30." and every comparison then fails on a
#: trailing dot rather than on a figure.
N = r"(\d+(?:\.\d+)?)"

#: Sentences the document is allowed to use about how often the fight is met,
#: and the share each one asserts. An unlisted wording fails: the claim is about
#: a number, so a reword has to be re-checked against it rather than assumed.
MET_CLAIMS = {
    "met in most campaigns": (50.0, 100.0),
    "met in a little under half of campaigns": (45.0, 50.0),
    "met in about half of campaigns": (48.0, 52.0),
    "met in a little over half of campaigns": (50.0, 55.0),
}

NUMBER_WORDS = {
    "fifteen": 15, "twenty": 20, "twenty-five": 25, "thirty": 30,
    "thirty-five": 35, "forty": 40, "forty-five": 45, "fifty": 50,
}


def flat(text: str) -> str:
    """Each design paragraph is one long line; the model's record is wrapped."""
    return re.sub(r"\s+", " ", text)


def grab(pattern: str, text: str, what: str) -> re.Match:
    match = re.search(pattern, text)
    assert match, f"could not read {what} -- the wording has changed: {pattern}"
    return match


@pytest.fixture(scope="module")
def document() -> str:
    return flat(GDD.read_text(encoding="utf-8"))


#: The current measurement block in engine.py, bracketed. Everything before the
#: first anchor is an older run; everything from the second is the note
#: recording the superseded figures.
BLOCK_BEGINS = "All of it with NO empire tree"
BLOCK_ENDS = "AN EARLIER SET TAKEN THE SAME DAY"


@pytest.fixture(scope="module")
def record() -> str:
    """The model's record of the CURRENT run, and of no other.

    `engine.py` carries THREE measurement blocks and a note quoting a fourth
    set of figures. Searching the whole file finds the oldest match, which is
    how this fixture was wrong when first written: the pattern for the reached
    share matched a 2026-09-06 block and read 96.3% where the current run says
    45.9%. Scoping is not tidiness here; it is the difference between comparing
    the document against this run and against one from six days earlier.
    """
    text = flat(ENGINE.read_text(encoding="utf-8"))
    for anchor in (BLOCK_BEGINS, BLOCK_ENDS):
        assert text.count(anchor) == 1, (
            f"expected one {anchor!r} in engine.py, found {text.count(anchor)}"
        )
    start, end = text.index(BLOCK_BEGINS), text.index(BLOCK_ENDS)
    assert start < end, "the measurement block's anchors are in the wrong order"
    return text[start:end]


def test_the_record_is_scoped_to_the_current_run(record):
    """A control: the older runs' figures must be outside the slice."""
    assert len(record) < 2_000, (
        f"the slice is {len(record)} characters, which is more than one block"
    )
    # NOT "96.3%": the current block names it legitimately, as the figure the
    # reached share is quoted against. A probe has to be something that exists
    # only in the blocks this slice must exclude.
    for elsewhere, what in (("1 in 34.5", "the superseded clear rate"),
                            ("2.4% of campaigns", "an older opens share"),
                            ("407 floors", "an older block's floor average")):
        assert elsewhere not in record, (
            f"the slice reaches {what} ({elsewhere}), so it covers more than "
            f"the current run"
        )
    assert "Block standard deviations across the six" in record, (
        "the slice does not reach the figures it exists to carry"
    )


def clear_rate_of(document: str) -> float:
    return float(grab(rf"cleared 1 time in {N} of those reached", document,
                      "the headline clear rate").group(1))


def test_both_files_parsed(document, record):
    """Without this every check below could pass on an empty string."""
    assert len(document) > 100_000, f"the design document parsed to {len(document)}"
    # The record is one scoped measurement block, not the whole file -- about
    # 900 characters. A floor written for the whole file would pass on anything.
    assert len(record) > 500, f"the measurement block parsed to {len(record)}"


def test_the_clear_rate_falls_inside_its_own_stated_interval(document):
    """THE FAILURE THIS FILE EXISTS FOR. Shipped 2026-09-07, found 2026-09-12."""
    stated = clear_rate_of(document)
    interval = grab(rf"the pooled 95% interval is 1 in {N} to 1 in {N}",
                    document, "the pooled interval")

    rate = 100 / stated
    ends = (100 / float(interval.group(1)), 100 / float(interval.group(2)))
    low, high = min(ends), max(ends)

    assert low <= rate <= high, (
        f"the document states a clear rate of 1 in {stated} ({rate:.2f}%) and a "
        f"95% interval of 1 in {interval.group(1)} to 1 in {interval.group(2)} "
        f"({low:.2f}% to {high:.2f}%), which does not contain it"
    )


def test_the_headline_figures_match_the_simulation_record(document, record):
    pairs = (
        ("the reached share",
         rf"the Last Stand is reached in {N}% of campaigns",
         rf"Last Stand \*\*reached in {N}%"),
        ("the clear rate",
         rf"cleared 1 time in {N} of those reached",
         rf"Cleared, per Last Stand reached, \*\*1 in {N}\*\*"),
        ("the opens share",
         rf"which itself opens in \*\*{N}%\*\* of campaigns",
         rf"earned Cataclysm dungeon opens in {N}%"),
        ("the earned win rate",
         rf"against a \*\*{N}%\*\* win rate",
         rf"is won \*\*{N}%\*\* of the time"),
    )
    for label, in_document, in_record in pairs:
        mine = grab(in_document, document, f"{label} in the design document").group(1)
        theirs = grab(in_record, record, f"{label} in engine.py").group(1)
        assert mine == theirs, (
            f"the design document states {label} as {mine}; "
            f"sim/cataclysm_sim/engine.py records {theirs}"
        )


def test_the_resolution_figures_match_the_simulation_record(document, record):
    stated = grab(
        rf"the standard deviation is {N} points on the earned share, "
        rf"{N} on its win rate, {N} on the Last Stand share and "
        rf"{N} of a city on cities lost — and \*\*{N} points on "
        rf"the Last Stand clear rate",
        document, "the five standard deviations")
    recorded = grab(
        rf"Block standard deviations across the six: {N} on the earned share, "
        rf"{N} on its win rate, {N} on the Last Stand share, "
        rf"{N} of a city on cities lost, and {N} points on the clear rate",
        record, "the five standard deviations in engine.py")

    names = ("earned share", "its win rate", "Last Stand share", "cities lost",
             "clear rate")
    # strict=True on purpose: if either pattern ever captures a different number
    # of figures, zip would silently compare only the shorter run and the rest
    # would go unchecked.
    for name, mine, theirs in zip(names, stated.groups(), recorded.groups(),
                                  strict=True):
        assert mine == theirs, (
            f"the design document states a standard deviation of {mine} on the "
            f"{name}; sim/cataclysm_sim/engine.py records {theirs}"
        )

    for label, in_document, in_record in (
        ("the six-block range",
         rf"The six blocks run 1 in {N} to 1 in {N}",
         rf"The six blocks run 1 in {N} to 1 in {N}"),
        ("the pooled interval",
         rf"the pooled 95% interval is 1 in {N} to 1 in {N}",
         rf"pooled 95% interval 1 in {N} to 1 in {N}"),
    ):
        mine = grab(in_document, document, f"{label} in the design document").groups()
        theirs = grab(in_record, record, f"{label} in engine.py").groups()
        assert mine == theirs, (
            f"the design document states {label} as {mine}; "
            f"sim/cataclysm_sim/engine.py records {theirs}"
        )


def test_how_to_read_the_clear_rate_matches_the_clear_rate(document):
    """The words and the digits are two statements of one figure."""
    word = grab(r'Read the clear rate as "about one in ([a-z-]+)"', document,
                "how to read the clear rate").group(1)
    assert word in NUMBER_WORDS, (
        f"unrecognised number word {word!r}; add it to NUMBER_WORDS"
    )
    stated = clear_rate_of(document)
    assert abs(NUMBER_WORDS[word] - stated) < 5, (
        f'the document says to read the clear rate as "about one in {word}" '
        f"while stating it as 1 in {stated}"
    )


def test_a_claim_that_the_fight_is_met_in_most_campaigns_needs_a_majority(document):
    """A word can carry a claim about a figure. "most" did, and went stale."""
    reached = float(grab(rf"the Last Stand is reached in {N}% of campaigns",
                         document, "the reached share").group(1))
    found = [claim for claim in MET_CLAIMS if claim in document]
    assert len(found) == 1, (
        f"expected exactly one recognised claim about how often the fight is "
        f"met, found {found}. A reworded claim must be added to MET_CLAIMS with "
        f"the share it asserts, so the wording stays tied to the figure."
    )
    low, high = MET_CLAIMS[found[0]]
    assert low <= reached < high, (
        f'the document says the fight is "{found[0]}" while stating the Last '
        f"Stand is reached in {reached}% of campaigns"
    )


def test_the_superseded_run_is_only_ever_named_as_superseded(document):
    """The history table keeps the old figures on purpose. A live claim may not."""
    assert "What this section carried before" in document, (
        "the history table that legitimately holds the old figures has gone"
    )
    for stale in ('Read the clear rate as "about one in thirty-five"',
                  "the pooled 95% interval is 1 in 28 to 1 in 42",
                  "met in most campaigns"):
        # THE ANSWER IS COMPUTED BEFORE THE ASSERT ON PURPOSE. Issue #1635: a
        # failing `x not in <470,000 characters>` written inside the assert
        # expression takes about 90 seconds to report, because pytest renders
        # the whole string; the same failure with the boolean computed first
        # reports in 0.11s. Measured on this file: three of the seven guard
        # proof cases took 77 to 104 seconds before this change and 0.1s after.
        is_live = stale in document
        assert not is_live, (
            f"a statement from the superseded run is live again: {stale!r}"
        )
