"""Which folders under `game/Content/` hold third-party asset packs.

`.gitignore` is the single place the vendor list is written down. This reads it
rather than keeping a second copy, because a second copy drifts: before this
existed, `.gitignore` and `tools/tests/test_game_readme_is_true.py` each decided
independently that a third-party folder is one whose name starts with "Paragon",
and a pack from any other vendor would have been counted as project content by
one and committed by the other.

WHY THIS IS NOT A TEST FILE. Two tests use it and it states a fact about the
project rather than checking one, so it lives beside the other tools.
"""

from __future__ import annotations

import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
GITIGNORE = REPO_ROOT / ".gitignore"

#: The comment lines in `.gitignore` that fence the vendor patterns.
BEGIN_MARKER = "# THIRD-PARTY-PACKS-BEGIN"
END_MARKER = "# THIRD-PARTY-PACKS-END"

#: Where third-party packs sit, relative to the repository root.
CONTENT_ROOT = "game/Content/"


def ignore_patterns() -> list[str]:
    """The raw `.gitignore` patterns for third-party packs, in file order.

    Raises:
        ValueError: if the markers are missing or out of order, which means
            somebody edited `.gitignore` without knowing this parses it.
    """
    text = GITIGNORE.read_text(encoding="utf-8")
    lines = text.splitlines()

    try:
        start = lines.index(BEGIN_MARKER)
        end = lines.index(END_MARKER)
    except ValueError as exc:
        raise ValueError(
            f"{GITIGNORE.name} is missing {BEGIN_MARKER!r} or {END_MARKER!r}. "
            "Those two comments fence the list of third-party asset pack "
            "folders, and tools/third_party_content.py reads between them. If "
            "the block was moved, move the markers with it; if it was deleted, "
            "the packs are no longer excluded and a single `git add -A` would "
            "commit about 17 GB through Git LFS."
        ) from exc

    if end <= start:
        raise ValueError(
            f"{GITIGNORE.name} has {END_MARKER!r} before {BEGIN_MARKER!r}.")

    return [
        line.strip()
        for line in lines[start + 1:end]
        if line.strip() and not line.strip().startswith("#")
    ]


def folder_prefixes() -> list[str]:
    """The leading folder names third-party packs use, e.g. `["Paragon"]`.

    Derived from the ignore patterns, which look like
    `game/Content/Paragon*/`. Anything that does not name a folder directly
    under `game/Content/` is skipped rather than guessed at.
    """
    prefixes = []
    for pattern in ignore_patterns():
        if not pattern.startswith(CONTENT_ROOT):
            continue
        remainder = pattern[len(CONTENT_ROOT):].rstrip("/")
        # `Paragon*` names every folder starting with `Paragon`; a bare
        # `SomePack` names exactly one. Both reduce to a prefix to match on.
        prefixes.append(remainder.rstrip("*"))
    return prefixes


def is_third_party(relative_path: pathlib.PurePath) -> bool:
    """Whether a path relative to `game/Content/` belongs to a vendor pack."""
    parts = relative_path.parts
    if not parts:
        return False
    return any(parts[0].startswith(prefix) for prefix in folder_prefixes())
