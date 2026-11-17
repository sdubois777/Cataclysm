r"""The design documents must not carry Google Docs conversion artefacts.

WHY THIS FILE EXISTS. Issue #238. `docs/Cataclysm_GDD_v2.md` was converted from
Google Docs, and the converter gave all 86 of its tables an empty heading row
with the real headings one row lower as escaped bold:

    |  |  |  |
    | :-- | :-: | :-: |
    | \*\*Base\*\* | \*\*Hands\*\* | \*\*Sub-Type\*\* |
    | Sword | 1 | Slashing |

GitHub renders that with a blank heading band and a first row reading the
literal characters `\*\*Base\*\*`. `docs/README.md` says the document was
converted to Markdown so that it produces readable diffs, and it says these
files are edited directly, which makes them the source of truth rather than the
Drive originals.

WHY IT IS A TEST AND NOT JUST A ONE-OFF SCRIPT. The obvious way to add a table
to a document that is edited by hand is to paste one out of Google Docs, which
brings the artefacts straight back for that table while every other table stays
clean. Nothing would notice. `tools/reformat_google_docs_artefacts.py --check`
is what notices, and this file is what runs it.

WHAT IS CHECKED HERE, beyond the script agreeing with itself:

1. No document still has an artefact the script would change.
2. Running the script twice does the same as running it once, or `--check` would
   pass on a document the script would keep editing.
3. Every table has a heading row with text in it, and that row has the same
   number of columns as its alignment row. That is what makes GitHub render a
   table at all, and it is the thing the artefact broke.
4. The one shape that is deliberately NOT a headed table -- a single-row callout
   box -- is still allowed, and is still the only exception.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import reformat_google_docs_artefacts as reformat  # noqa: E402

SEPARATOR = reformat.SEPARATOR


def cells(row: str) -> list[str]:
    return reformat.split_cells(row)


def tables(text: str):
    """(line number, heading row, alignment row, body rows) for every table."""
    lines = text.split("\n")
    found = []
    for i, line in enumerate(lines):
        if not (SEPARATOR.match(line) and "-" in line):
            continue
        body = []
        j = i + 1
        while j < len(lines) and lines[j].strip().startswith("|"):
            body.append(lines[j])
            j += 1
        found.append((i + 1, lines[i - 1], line, body))
    return found


@pytest.mark.parametrize("name", reformat.DOCUMENTS)
def test_the_document_has_no_artefact_the_script_would_change(name: str):
    """THE GUARD. Everything else in this file describes what good looks like;
    this is the one that fires when a table is pasted back in from Google
    Docs."""
    path = REPO_ROOT / name
    original = path.read_text(encoding="utf-8")
    converted, counts = reformat.reformat(original)
    assert converted == original, (
        f"{name} has Google Docs conversion artefacts: "
        + ", ".join(f"{n} {what}" for what, n in counts.items() if n)
        + ". Run 'python tools/reformat_google_docs_artefacts.py' to fix them. "
        "Issue #238.")


def test_the_check_mode_exits_zero_on_the_committed_documents():
    """The script is the guard, so its exit code has to mean what the test
    above means. Run as a subprocess because that is how a person runs it."""
    result = subprocess.run(
        [sys.executable, "tools/reformat_google_docs_artefacts.py", "--check"],
        cwd=REPO_ROOT, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    for name in reformat.DOCUMENTS:
        assert f"{name}: no conversion artefacts" in result.stdout


@pytest.mark.parametrize("name", reformat.DOCUMENTS)
def test_the_conversion_is_idempotent(name: str):
    """Running it twice must do what running it once does. Without this,
    --check could pass on a document the script would still edit, and the guard
    above would be checking nothing."""
    text = (REPO_ROOT / name).read_text(encoding="utf-8")
    once, _ = reformat.reformat(text)
    twice, _ = reformat.reformat(once)
    assert once == twice


@pytest.mark.parametrize("name", reformat.DOCUMENTS)
def test_every_table_has_a_heading_row_with_text_in_it(name: str):
    """The artefact left an empty band where the headings should be. The one
    exception is a single-row callout box, which is a bold label beside a
    paragraph rather than a table with columns."""
    text = (REPO_ROOT / name).read_text(encoding="utf-8")
    empty = []
    for line_number, heading, _, body in tables(text):
        if reformat.is_empty_heading(heading) and len(body) > 1:
            empty.append(line_number)
    assert not empty, (
        f"{name} has tables with an empty heading row and more than one body "
        f"row, at lines {empty}. Issue #238.")


@pytest.mark.parametrize("name", reformat.DOCUMENTS)
def test_every_heading_row_has_as_many_columns_as_its_alignment_row(name: str):
    """What makes GitHub render a table at all. A heading row with fewer cells
    than the alignment row renders as plain text, not a table."""
    text = (REPO_ROOT / name).read_text(encoding="utf-8")
    wrong = []
    for line_number, heading, alignment, _ in tables(text):
        if len(cells(heading)) != len(cells(alignment)):
            wrong.append((line_number, len(cells(heading)),
                          len(cells(alignment))))
    assert not wrong, (
        f"{name} has tables whose heading row and alignment row have different "
        f"column counts, as (line, heading columns, alignment columns): "
        f"{wrong}. GitHub renders those as plain text rather than as a table.")


@pytest.mark.parametrize("name", reformat.DOCUMENTS)
def test_no_bold_marker_is_escaped(name: str):
    r"""`\*\*Base\*\*` renders as those characters, not as bold."""
    text = (REPO_ROOT / name).read_text(encoding="utf-8")
    found = reformat.ESCAPED_BOLD_SPAN.findall(text)
    assert not found, (
        f"{name} has {len(found)} escaped bold markers, the first being "
        f"{found[0]!r}. They render as literal backslashes and asterisks.")


@pytest.mark.parametrize("name", reformat.DOCUMENTS)
def test_no_punctuation_is_escaped_twice(name: str):
    r"""`\\+3` is a double backslash. Markdown renders `\\` as one visible
    backslash, so the page reads "\+3" where it should read "+3"."""
    text = (REPO_ROOT / name).read_text(encoding="utf-8")
    for escaped, plain in reformat.UNNECESSARY_ESCAPES:
        assert escaped not in text, (
            f"{name} contains {escaped!r}, which should be {plain!r}.")


def test_the_callout_boxes_are_the_only_tables_without_headings():
    """Named so that the exception cannot quietly grow. Both are a bold label
    beside a paragraph, used as a highlighted note rather than as a table, and
    promoting the row would leave a table with a heading and no body."""
    text = (REPO_ROOT / "docs/Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    without = [(n, cells(body[0])[0])
               for n, heading, _, body in tables(text)
               if reformat.is_empty_heading(heading) and body]
    assert [label for _, label in without] == ["**KEY PILLARS**",
                                               "**UNIQUE PER CHARACTER**"], (
        f"the set of tables with no heading row has changed: {without}. If a "
        f"new callout box was added, add its label here. If a real table lost "
        f"its heading, give it one.")


@pytest.mark.parametrize("endings", ["\n", "\r\n"], ids=["lf", "crlf"])
def test_a_rewrite_never_leaves_mixed_line_endings(tmp_path, endings):
    r"""`.gitattributes` sets `* text=auto`, so these files are stored with line
    feed endings and checked out with carriage return and line feed on Windows.

    WHAT WOULD GO WRONG. A script that read bytes and split on a line feed
    would leave carriage returns stranded on the lines it did not touch and
    strip them from the lines it did, and git would then show every line as
    changed. The real edit would be invisible inside a whole-file diff.

    WHAT IS ACTUALLY GUARANTEED. Not that the endings are preserved byte for
    byte: `pathlib.Path.write_text` uses Python text mode, which writes the
    platform's ending, so on Windows the output is always carriage return and
    line feed whatever went in. What matters is that they are UNIFORM, because
    git's `text=auto` normalises a uniformly-ended file to line feeds on commit
    and the diff then shows only the real edit. A mixture is what cannot be
    normalised away.

    Both input styles are tested against a file that DOES have an artefact, so
    the script rewrites it rather than returning early.
    """
    artefact = (
        "Some prose.\n"
        "\n"
        "|  |  |\n"
        "| :-- | :-: |\n"
        r"| \*\*Name\*\* | \*\*Value\*\* |" "\n"
        "| Sword | 40 |\n"
        "| Axe | 45 |\n"
    ).replace("\n", endings)

    path = tmp_path / "sample.md"
    path.write_bytes(artefact.encode("utf-8"))

    # Exactly the read and write the script uses.
    text = path.read_text(encoding="utf-8")
    converted, counts = reformat.reformat(text)
    assert counts["headings promoted"] == 1, (
        "the sample no longer contains the artefact this test is about")
    path.write_text(converted, encoding="utf-8")

    written = path.read_bytes()
    bare_line_feeds = written.replace(b"\r\n", b"").count(b"\n")
    carriage_returns = written.count(b"\r\n")
    assert not (bare_line_feeds and carriage_returns), (
        f"the rewrite left {carriage_returns} carriage-return endings and "
        f"{bare_line_feeds} bare line-feed endings in the same file. A mixture "
        f"is what makes git show every line as changed.")

    lines = written.decode("utf-8").splitlines()
    assert len(lines) == len(artefact.splitlines()) - 1, (
        "the rewrite should remove exactly one line, the heading row it "
        "promoted")
    assert lines[2] == "| Name | Value |"
    assert lines[3] == "| :-- | :-: |"
    assert lines[4] == "| Sword | 40 |"
