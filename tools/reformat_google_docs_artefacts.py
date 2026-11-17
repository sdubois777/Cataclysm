"""Remove the Google Docs conversion artefacts from the design documents.

    python tools/reformat_google_docs_artefacts.py           # rewrite in place
    python tools/reformat_google_docs_artefacts.py --check   # report, change nothing

WHAT THE ARTEFACTS ARE. Issue #238. `docs/Cataclysm_GDD_v2.md` and the other
Markdown files in `docs/` were converted from Google Docs. The converter left
two things behind.

1. EVERY TABLE GOT AN EMPTY HEADER ROW, and the real headings became escaped
   bold text in the first body row:

       |  |  |  |
       | :-- | :-: | :-: |
       | \\*\\*Base\\*\\* | \\*\\*Hands\\*\\* | \\*\\*Sub-Type\\*\\* |
       | Sword | 1 | Slashing |

   GitHub renders that with a blank heading band and a first row reading the
   literal characters `\\*\\*Base\\*\\*`. It should be:

       | Base | Hands | Sub-Type |
       | :-- | :-: | :-: |
       | Sword | 1 | Slashing |

2. SOME PUNCTUATION IS ESCAPED THAT DOES NOT NEED TO BE, and one form is
   escaped twice. `\\\\+2% move speed` is a double backslash, which Markdown
   renders as a visible backslash followed by the text, so the document reads
   "\\+2% move speed" on GitHub.

WHY THIS IS A SCRIPT AND NOT A HAND EDIT. The document has 86 tables. A hand
pass would miss some and corrupt others, and the result could not be checked.

WHY IT STAYS IN THE REPOSITORY AFTER THE ONE CONVERSION. Because `--check` makes
it a guard. `docs/README.md` says these files are edited directly, and the
obvious way to add a table is to paste one out of Google Docs, which brings the
artefacts straight back. `tools/tests/test_design_documents_have_no_conversion_artefacts.py`
runs this in `--check` mode over every file listed below.

THE ONE CASE THAT IS NOT A HEADING. Two tables in the design document are
single-row callout boxes rather than tables with headings:

    |  |  |
    | :-: | :-: |
    | \\*\\*KEY PILLARS\\*\\* | Time is the primary resource. ... |

Promoting that row would leave a table with a heading and no body, and would
render the prose in the right-hand cell bold, which it is not today. So a table
whose only body row is the bold one keeps its empty heading and has the escaping
removed and nothing else. `HEADING_NEEDS_A_SECOND_ROW` is the rule.

LINE ENDINGS. `.gitattributes` sets `* text=auto`, so these files are stored
with line feed endings and checked out with carriage return and line feed on
Windows. Reading and writing through `pathlib` with `encoding="utf-8"` uses
Python's text mode, which translates both ways, so the rewrite does not touch
them. Reading in binary and splitting on a line feed would rewrite every line
and bury the real edit.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The Markdown design documents that came out of the Google Docs converter.
#: The JSON and spreadsheet files in `docs/` were exported in their own formats
#: and never went through it.
DOCUMENTS = (
    "docs/Cataclysm_GDD_v2.md",
    "docs/Empire_Skill_Tree_Keystones.md",
)

#: A table's alignment row: pipes, colons, hyphens and spaces, with at least one
#: hyphen. `| :-- | :-: |` and `| ----- | ----- |` both match.
SEPARATOR = re.compile(r"^\|[\s:|-]+\|\s*$")

#: One cell that is entirely escaped bold. The inner group may be empty: one
#: heading row in the design document opens with `\*\*\*\*`, an empty label over
#: a column of row names.
ESCAPED_BOLD_CELL = re.compile(r"^\\\*\\\*(.*?)\\\*\\\*$")

#: The same thing anywhere in a line rather than as a whole cell. Used for what
#: is left after the headings have been promoted.
ESCAPED_BOLD_SPAN = re.compile(r"\\\*\\\*(.*?)\\\*\\\*")

#: A heading row is only promoted when the table still has a body row under it.
#: See the callout-box paragraph in the module docstring.
HEADING_NEEDS_A_SECOND_ROW = True

#: Escapes the converter emitted that Markdown does not need. The double
#: backslash is the one that shows: Markdown renders `\\` as a visible
#: backslash, so `\\+3` reads as "\+3". The other two are invisible in the
#: rendered page and are removed so that the source says what it means.
UNNECESSARY_ESCAPES = (
    ("\\\\+", "+"),
    ("\\~", "~"),
    ("\\=", "="),
)


def split_cells(row: str) -> list[str]:
    """The cells of one Markdown table row, without the outer pipes."""
    inner = row.strip()
    if inner.startswith("|"):
        inner = inner[1:]
    if inner.endswith("|"):
        inner = inner[:-1]
    return [cell.strip() for cell in inner.split("|")]


def is_empty_heading(row: str) -> bool:
    """`|  |  |  |` -- a heading row the converter left with no text in it."""
    return row.startswith("|") and set(row) <= set("| ")


def unescaped_heading(row: str) -> list[str] | None:
    """The heading text of a row whose every cell is escaped bold, else None."""
    if not row.startswith("|"):
        return None
    cells = split_cells(row)
    unescaped = [ESCAPED_BOLD_CELL.match(cell) for cell in cells]
    if not cells or not all(unescaped):
        return None
    return [match.group(1) for match in unescaped]


def reformat(text: str) -> tuple[str, dict[str, int]]:
    """The whole conversion. Returns the new text and a count of what changed."""
    lines = text.split("\n")
    out: list[str] = []
    counts = {"headings promoted": 0}

    index = 0
    while index < len(lines):
        line = lines[index]
        heading_row = out[-1] if out else ""
        after = lines[index + 1] if index + 1 < len(lines) else ""
        following = lines[index + 2] if index + 2 < len(lines) else ""

        if (SEPARATOR.match(line) and "-" in line
                and is_empty_heading(heading_row)):
            heading = unescaped_heading(after)
            has_body = following.startswith("|")
            if heading is not None and (has_body
                                        or not HEADING_NEEDS_A_SECOND_ROW):
                # The row under the alignment row is the real heading. Put it
                # above the alignment row, and drop it from the body.
                out[-1] = "| " + " | ".join(heading) + " |"
                out.append(line)
                counts["headings promoted"] += 1
                index += 2
                continue

        out.append(line)
        index += 1

    result = "\n".join(out)

    # Anything still carrying escaped bold is not a heading: either a callout
    # box, whose empty heading is deliberate, or bold used inside a cell. Both
    # want the escaping removed and nothing else.
    counts["escaped bold left in place"] = len(ESCAPED_BOLD_SPAN.findall(result))
    result = ESCAPED_BOLD_SPAN.sub(r"**\1**", result)

    for escaped, plain in UNNECESSARY_ESCAPES:
        found = result.count(escaped)
        if found:
            counts[f"{escaped} became {plain}"] = found
            result = result.replace(escaped, plain)
    return result, counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true",
        help="report what would change and exit 1 if anything would, "
             "without writing")
    args = parser.parse_args(argv)

    dirty = False
    for name in DOCUMENTS:
        path = REPO_ROOT / name
        original = path.read_text(encoding="utf-8")
        converted, counts = reformat(original)
        changed = {what: n for what, n in counts.items() if n}
        if converted == original:
            print(f"{name}: no conversion artefacts")
            continue
        dirty = True
        summary = ", ".join(f"{n} {what}" for what, n in changed.items())
        if args.check:
            print(f"{name}: STILL HAS ARTEFACTS -- {summary}")
        else:
            path.write_text(converted, encoding="utf-8")
            print(f"{name}: rewritten -- {summary}")

    if args.check and dirty:
        print("\nRun tools/reformat_google_docs_artefacts.py without --check "
              "to fix them.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
