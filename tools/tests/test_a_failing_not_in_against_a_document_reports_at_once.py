"""A failing `assert phrase not in <document>` reports where the phrase is, at
once, instead of building pytest's diff of the document against itself.

WHY THIS EXISTS. Issue #1635: written the natural way, such a guard did not
finish when it fired (killed at 120 and at 300 seconds over the flattened
decisions log; 77 to 104 seconds per failure over the design document). The
root conftest.py explains the cause and holds the replacement that prevents the
diff. These tests hold it to its job two ways: through the explanation hook of
the pytest this very run is using, and through a real failing test run in a
subprocess under the `pytester` fixture, whose output is read for the
replacement's line and for the absence of pytest's own "is contained here:"
line.

EVERY CASE IS 200,000 SINGLE-LINE CHARACTERS OR FEWER, ON PURPOSE. A first
draft ran the hook over the whole flattened docs/DECISIONS.md, and with the
replacement removed that test hung, which is the very fault the issue reports
and no shape for a guard. Over the repetitive filler text below pytest's own
diff is cheap even at 200,000 characters (0.11s with the replacement removed,
measured in the guard proof on 2026-09-16), so a broken replacement fails these
tests in words rather than by a timeout. The stall is a property of real prose
at real sizes; it was measured on the flattened docs/DECISIONS.md, and the
figures are in the root conftest.py. These tests hold the SHAPE of the
explanation, which does not depend on the text; only the threshold does, and
the threshold has its own control below.
"""

from __future__ import annotations

import pathlib
import time

import _pytest.assertion.util as assertion_util

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFTEST = ROOT / "conftest.py"


def a_single_line_text(characters: int, phrase: str) -> str:
    """`characters` characters on one line with `phrase` a little before the
    middle, so the test assumes nothing about what any real document says."""
    filler = "lorem ipsum dolor sit amet "
    text = (filler * (characters // len(filler) + 1))[:characters]
    at = characters * 2 // 5
    return text[:at] + phrase + text[at + len(phrase):]


def explanation(request, op: str, left, right) -> list[str]:
    """Every line every implementation of the explanation hook returns, through
    the plugin manager this run registered pytest with."""
    results = request.config.hook.pytest_assertrepr_compare(
        config=request.config, op=op, left=left, right=right)
    return [line for lines in results if lines for line in lines]


def test_pytests_diff_builder_is_the_replacement():
    """By name, so the loss of the replacement fails here in words rather than
    as a timing figure elsewhere."""
    assert assertion_util._notin_text.__name__ == "a_failing_not_in_over_a_large_text_explains_itself", (
        "pytest's `_notin_text` is not the root conftest's replacement; a failing "
        "`not in` over a document would build the diff again (issue #1635)")


def test_a_failing_not_in_over_a_long_text_explains_itself_at_once(request):
    text = a_single_line_text(200_000, "the superseded sentence")
    at = text.index("the superseded sentence")

    started = time.perf_counter()
    lines = explanation(request, "not in", "the superseded sentence", text)
    elapsed = time.perf_counter() - started

    assert "'the superseded sentence' is in the 200,000-character text" in lines, lines[:3]
    assert any(line.startswith(f"first at character {at:,}, 1 time(s)") for line in lines), lines
    assert any("the superseded sentence" in line and line.startswith("  ...") for line in lines)
    assert not any("is contained here:" in line for line in lines), "pytest built its own diff"
    assert elapsed < 2.0, f"the explanation took {elapsed:.2f}s over {len(text):,} characters"


def test_a_short_text_keeps_pytests_own_diff(request):
    """THE CONTROL on the replacement's scope: pytest's own diff stays for short
    texts, where it is cheap and shows more, and the threshold is exact."""
    short = explanation(request, "not in", "needle", a_single_line_text(9_999, "needle"))
    long = explanation(request, "not in", "needle", a_single_line_text(10_000, "needle"))
    assert any("is contained here:" in line for line in short), short
    assert not any("is in the 9,999-character text" in line for line in short)
    assert any("is in the 10,000-character text" in line for line in long), long
    assert not any("is contained here:" in line for line in long)


def test_a_real_failing_not_in_prints_the_offset_and_not_the_diff(pytester):
    """A test file with the natural guard shape, run in a subprocess with a copy
    of this repository's root conftest, over 200,000 single-line characters."""
    pytester.makeconftest(CONFTEST.read_text(encoding="utf-8"))
    pytester.makepyfile(
        test_guard="""
        import pathlib
        TEXT = pathlib.Path("document.txt").read_text(encoding="utf-8")

        def test_the_superseded_sentence_is_gone():
            assert "the superseded sentence" not in TEXT, "it is live again"
        """)
    pathlib.Path(pytester.path, "document.txt").write_text(
        a_single_line_text(200_000, "the superseded sentence"), encoding="utf-8")

    started = time.perf_counter()
    result = pytester.runpytest_subprocess("-p", "no:cacheprovider")
    elapsed = time.perf_counter() - started

    assert result.ret == 1, "the guard did not fail; the case is about a failing guard"
    out = result.stdout.str()
    printed_the_offset = "'the superseded sentence' is in the 200,000-character text" in out
    printed_pytests_diff = "is contained here:" in out
    assert printed_the_offset, out[-3000:]
    assert not printed_pytests_diff, "pytest built its own diff as well"
    assert elapsed < 10.0, f"the failing guard took {elapsed:.1f}s to report"
