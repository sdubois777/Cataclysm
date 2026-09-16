"""Root pytest configuration. One replacement inside pytest, for issue #1635.

A FAILING `assert phrase not in text` OVER A DOCUMENT DID NOT FINISH. Guards
over `docs/` are one of the main kinds of test this project writes, and "this
superseded sentence is gone" is their most natural shape. Written the natural
way, such a guard passed in milliseconds every day and then, the first time it
caught the thing it exists for, stalled the run: not finished at 300 seconds
over the flattened decisions log (issue #1635, measured 2026-09-12), 77 to 104
seconds per failure over the 470,000-character design document (the same issue,
second measurement).

THE CAUSE, established on 2026-09-16 against pytest 9.1.1 (the version
sim/requirements-dev.txt pins): pytest explains a failing `not in` between two
strings by removing the phrase from the text and diffing the two texts line by
line (`_pytest.assertion.util._notin_text`). A document flattened with
`" ".join(text.split())` is one line, so the line diff falls through to a
character-by-character comparison of two lines that differ only by the phrase,
and that grows faster than the text: consuming the explanation took 0.03s at
10,000 single-line characters, 0.19s at 40,000, 0.55s at 80,000, and a failing
test over the 2,586,463-character flattened docs/DECISIONS.md was killed at 120
seconds. A failing `in` has no such explanation in pytest, which is why the same
text in a failing `in` reported in 0.13s; that is the case the issue said any
explanation had to survive.

WHY THIS IS A REPLACEMENT AND NOT A HOOK. The public hook for this,
`pytest_assertrepr_compare`, looks like the place: a conftest implementation
that returns lines is printed instead of pytest's own. It was tried first and
did nothing, because the hook is not first-result: pytest calls every
implementation and its own builds the whole diff into a list before anything
chooses between them, so the stall happens before the choice. The only place
the diff can be prevented is the function that builds it, so this file replaces
that function on pytest's own module, for texts at or above the threshold only,
and leaves it alone otherwise.

THE NAME IS PRIVATE TO PYTEST, AND THIS FILE DOES NOTHING WHEN IT IS NOT WHAT
WAS MEASURED. The replacement was written and measured against pytest 9.1.1
only. If a later pytest has no `_notin_text`, or has one whose parameters are
not `(term, text, verbose)`, nothing here is installed and pytest's own
behaviour stands: every test still collects and a failing `not in` still fails,
only slowly again over a long text. `tools/tests/test_a_failing_not_in_against_a_document_reports_at_once.py`
proves both halves: under the pinned pytest the replacement is in place by
name, and with the name removed or re-signed a failing `not in` still reports.

THE FIX IS HERE AND NOT IN EACH TEST. Every `assert phrase not in text` in the
suite, present and future, written the natural way, reports where the phrase is
and the text around it and never builds the diff. The two tests that carried the
workaround (the answer computed into a variable before the assert) keep it; it
is harmless.
"""

from __future__ import annotations

import inspect

import _pytest.assertion.util as assertion_util

#: Below this many characters pytest's own diff is cheap and more useful (it
#: shows the whole text with the phrase marked), so it is left alone. At this
#: size consuming the diff cost 0.03s on 2026-09-16.
LARGE_TEXT_CHARACTERS = 10_000

#: Characters shown on each side of the phrase.
CONTEXT_CHARACTERS = 60

#: The pytest the replacement was measured against, and the parameters its
#: private function had there. Anything else is left alone; see the docstring.
MEASURED_AGAINST_PYTEST = "9.1.1"
MEASURED_PARAMETERS = ["term", "text", "verbose"]

pytest_plugins = ["pytester"]


def pytests_own_diff_builder():
    """pytest's `_notin_text` when it is the function that was measured, else
    None. Never raises: an absent name or an unreadable signature is None."""
    original = getattr(assertion_util, "_notin_text", None)
    if original is None:
        return None
    try:
        parameters = list(inspect.signature(original).parameters)
    except (TypeError, ValueError):
        return None
    if parameters != MEASURED_PARAMETERS:
        return None
    return original


_pytests_own_notin_text = pytests_own_diff_builder()


def explain_a_phrase_found_in_a_large_text(phrase: str, text: str) -> list[str]:
    """The lines pytest prints under a failed `phrase not in text`."""
    index = text.find(phrase)
    start = max(0, index - CONTEXT_CHARACTERS)
    end = min(len(text), index + len(phrase) + CONTEXT_CHARACTERS)
    return [
        f"{phrase!r} is in the {len(text):,}-character text",
        f"first at character {index:,}, {text.count(phrase)} time(s) in all; around it:",
        f"  ...{text[start:end]!r}...",
        "(pytest's own diff of a text this long against itself minus the phrase does "
        "not finish; see the root conftest.py and issue #1635)",
    ]


def a_failing_not_in_over_a_large_text_explains_itself(term: str, text: str, verbose: int = 0):
    """Stands in for `_pytest.assertion.util._notin_text`, same signature. A
    generator like the original, because pytest iterates the result."""
    if isinstance(term, str) and isinstance(text, str) and len(text) >= LARGE_TEXT_CHARACTERS \
            and text.find(term) >= 0:
        yield from explain_a_phrase_found_in_a_large_text(term, text)
        return
    yield from _pytests_own_notin_text(term, text, verbose)


if _pytests_own_notin_text is not None:
    assertion_util._notin_text = a_failing_not_in_over_a_large_text_explains_itself
