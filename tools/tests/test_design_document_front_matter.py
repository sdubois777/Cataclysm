"""The design document carries no version number and no links back to Drive.

WHY THIS EXISTS. Issue #35. `docs/Cataclysm_GDD_v2.md` used to say "Version 0.3"
in its body while its filename said `_v2` and the Google Drive document it was
exported from was titled `Cataclysm_GDD_v2(1)`. Three identifiers for one
document, none of which was ever advanced when the design changed, so none of
them said anything true.

WHAT WAS DECIDED. The version of a document in `docs/` is its git history. Every
change arrives through a pull request and `docs/DECISIONS.md` records the
reasoning. A hand-maintained number would be a fourth thing to keep in step with
the other three, and `CLAUDE.md` names hand-maintained duplicates of one fact as
a failure this project has already had twice.

The filename keeps `_v2` because roughly twenty test files and several C++
sources name the file by path. It is part of the name, not a counter.

THE OTHER HALF: THE TABLE OF CONTENTS IS GONE. The exported one was 106 links
back into the Google Drive document, every one pointing at the same empty
anchor, so they all landed at the top of a document that has been historical
since 2026-08-02. GitHub builds an outline from the headings instead.

WHAT THIS FILE STOPS. A later export, or a well-meaning tidy-up, putting either
of them back. Both would look like improvements while undoing a decision.

WHAT IT DELIBERATELY DOES NOT CHECK. The Google Docs conversion artefacts in the
table formatting -- empty header rows with the real headers as escaped bold in
the first body row, and escaped asterisks such as `\\*\\*Base\\*\\*`. Those are
real and worth fixing, and roughly twenty test files parse the tables in their
current shape, so changing them is its own piece of work. Filed separately.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO_ROOT / "docs"

#: Every Markdown design document in the folder. Found rather than listed, so a
#: document added later is covered without anyone remembering to add it.
DOCUMENTS = sorted(DOCS.glob("*.md"))

#: `DECISIONS.md` is the log rather than a design document, and it quotes past
#: wording, including the "Version 0.3" line this file exists to keep out of the
#: documents themselves. `README.md` explains the rule and has to name it.
NOT_DESIGN_DOCUMENTS = {"DECISIONS.md", "README.md"}

#: A version number stated as a document's own version. Deliberately narrow: it
#: matches "Version 0.3" and "v1.2" standing as a label, not the word "version"
#: used in a sentence about an earlier version of a design decision, which the
#: document does several times and should be free to keep doing.
STATES_A_VERSION = re.compile(
    r"^\s*(?:\*\*)?[Vv](?:ersion|\.)?\s*\d+(?:\.\d+)*(?:\*\*)?\s*$",
    re.MULTILINE)

#: Any link back to the Google Drive original.
LINKS_TO_DRIVE = re.compile(r"docs\.google\.com|drive\.google\.com")


def design_documents() -> list[pathlib.Path]:
    found = [p for p in DOCUMENTS if p.name not in NOT_DESIGN_DOCUMENTS]
    if not found:
        pytest.skip("no design documents present")
    return found


@pytest.mark.parametrize("path", design_documents(), ids=lambda p: p.name)
def test_a_design_document_states_no_version_number(path):
    text = path.read_text(encoding="utf-8")
    stated = STATES_A_VERSION.findall(text)
    assert not stated, (
        f"{path.name} states a version of its own: {stated}. The version of a "
        "document in docs/ is its git history; see the 'Why these documents "
        "carry no version number' section of docs/README.md. Issue #35.")


@pytest.mark.parametrize("path", design_documents(), ids=lambda p: p.name)
def test_a_design_document_links_nowhere_into_google_drive(path):
    text = path.read_text(encoding="utf-8")
    found = LINKS_TO_DRIVE.findall(text)
    assert not found, (
        f"{path.name} has {len(found)} link(s) into Google Drive. The Drive "
        "originals have been historical since 2026-08-02 and a link to one "
        "sends a reader to a document that no longer matches this repository. "
        "Issue #35.")


@pytest.fixture(scope="module")
def opening() -> str:
    """Everything above the document's first heading."""
    path = DOCS / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the main design document is not present")
    text = path.read_text(encoding="utf-8")
    first_heading = text.find("\n# ")
    assert first_heading != -1, "the document has no headings"
    return text[:first_heading]


class TestTheMainDesignDocumentSaysWhySpecifically:
    """The two decisions are written into the document itself, so a reader who
    goes looking for a version number finds the reason instead of nothing."""

    def test_it_says_there_is_no_version_number_and_why(self, opening):
        assert "no version number" in opening
        assert "git history" in opening

    def test_it_says_the_filename_is_not_a_counter(self, opening):
        assert "_v2" in opening, (
            "the opening should explain the _v2 in the filename, because that "
            "is the first thing a reader will read as a version")

    def test_it_says_there_is_no_table_of_contents_and_why(self, opening):
        assert "table of contents" in opening.lower()
