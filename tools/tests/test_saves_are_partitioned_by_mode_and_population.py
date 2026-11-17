"""A character shares its empire tree and stash with exactly who the design says.

WHY THIS EXISTS. Issue #529. Partitioning is not a storage convenience: it is a
rule `docs/Cataclysm_GDD_v2.md` already made, and the save layout is the only
place it can be enforced. Each lethality mode has its own empire upgrade tree,
the offline and online populations are separate, and a Solo Self-Found character
shares with nobody at all.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in
`game/Source/Cataclysm/Tests/CataclysmSavePartitionTests.cpp` never run on a pull
request. Reading the source as text does.

WHAT MAKES THIS WORTH GUARDING RATHER THAN TRUSTING. **Getting a partition wrong
does not fail loudly.** It quietly pours a Hardcore character's empire progress
into a Standard character's tree, or opens one Solo Self-Found character's stash
to another. There is no crash and no log line, and the only thing that would ever
notice is a player.

THE TWO THINGS CHECKED ACROSS FILES, which is where this earns its place:

- **The slot names in the C++ are the ones the design document lists.** They are
  in a code block in `docs/Save_System_Design.md` section 4, and they are read
  from there rather than restated here.
- **The lethality modes in the C++ are the ones the design document's table
  names.** A fourth mode added to one and not the other would give it either no
  storage or a silent share with Standard.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
PARTITION_H = SOURCE / "Save" / "CataclysmSavePartition.h"
PARTITION_CPP = SOURCE / "Save" / "CataclysmSavePartition.cpp"
LETHALITY_H = SOURCE / "Character" / "CataclysmLethality.h"
SAVE_DESIGN = REPO_ROOT / "docs" / "Save_System_Design.md"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: `Standard = 0	UMETA(DisplayName = "Standard"),`
ENUMERATOR = re.compile(r"^\s*(\w+)\s*=\s*(\d+)\s*UMETA", re.MULTILINE)

#: The enum whose values become part of a slot name.
LETHALITY_ENUM = "enum class ECataclysmLethality : uint8"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. How "
            f"saves are partitioned is issue #529; if a file was renamed, "
            f"rename it here too.")
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The code with its comments removed.

    NEEDED FOR EVERY "MUST NOT CONTAIN" CHECK IN THIS PROJECT, because every
    rule here is written down beside the code in a comment that names the very
    thing being searched for. `test_the_names_do_not_come_from_the_enum` below
    failed on the comment explaining why the enum is not used, which is the
    fifth time this project has recorded that trap.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def lethality_modes() -> dict[str, str]:
    """The lethality enum's own values, and not the population enum's.

    BOTH ENUMS LIVE IN ONE HEADER, so a search of the whole file finds Offline
    and Online as well and reports them as unnamed lethality modes. Found by
    the checks below failing on a header that is correct.
    """
    header = read(LETHALITY_H)

    start = header.find(LETHALITY_ENUM)
    assert start != -1, (
        f"CataclysmLethality.h no longer declares {LETHALITY_ENUM!r}. It is "
        f"what says which lethality modes exist in the engine at all.")

    end = header.find("};", start)
    assert end != -1, "the lethality enum is not closed"

    return dict(ENUMERATOR.findall(header[start:end]))


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out.

    THE COMMENTS HAVE TO GO OR THE CHECKS BELOW READ THEM. Every rule in this
    project is written down beside the code that implements it, in a comment
    naming the very thing being searched for.
    """
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


@pytest.fixture(scope="module")
def designed_slot_names() -> list[str]:
    """The account slot names the design document lists, read from it.

    NOT RESTATED HERE. A copy in this file would agree with itself while the
    document said something else, which is the whole failure this check is for.
    """
    if not SAVE_DESIGN.is_file():
        pytest.skip("Save_System_Design.md is not present")

    found = re.findall(r"^Account_\w+$", SAVE_DESIGN.read_text(encoding="utf-8"),
                       re.MULTILINE)
    assert found, (
        "docs/Save_System_Design.md lists no Account_ slot names. Section 4 "
        "carries them in a code block; if that block was reformatted, update "
        "this test to read the new shape.")
    return found


@pytest.fixture(scope="module")
def designed_modes() -> list[str]:
    """The lethality modes the design document's own table names."""
    if not GDD.is_file():
        pytest.skip("Cataclysm_GDD_v2.md is not present")

    text = GDD.read_text(encoding="utf-8")
    start = text.find("**Lethality mode. Choose one.**")
    assert start != -1, (
        "docs/Cataclysm_GDD_v2.md no longer has the lethality mode table. It "
        "is what says which modes exist at all.")

    end = text.find("**Solo Self-Found", start)
    rows = re.findall(r"^\|\s*(\w+)\s*\|", text[start:end], re.MULTILINE)

    # THE HEADER AND THE ALIGNMENT ROW ARE NOT MODES.
    modes = [row for row in rows if row not in ("Mode",)]
    assert modes, "the lethality mode table has no rows"
    return modes


# ---------------------------------------------------------------------------
# The rule itself
# ---------------------------------------------------------------------------

def test_solo_self_found_shares_with_nobody(designed_modes) -> None:
    """The strictest rule, and the one a reader is most likely to soften.

    "Its own empire upgrade tree shared with no other character at all -- not
    with the others in its lethality mode, and not with another Solo Self-Found
    character." It would be an easy and invisible mistake to treat Solo
    Self-Found as a fourth partition that its members share.
    """
    body = function_body(
        read(PARTITION_CPP),
        "bool UCataclysmSavePartition::ShareAnAccountRecord")

    assert "UsesAnAccountRecord" in body, (
        "ShareAnAccountRecord does not ask whether either character uses an "
        "account record at all, so a Solo Self-Found character would share one "
        "with anybody matching its mode and population. Issue #529.")

    assert "Lethality" in body and "Population" in body, (
        "ShareAnAccountRecord no longer compares both the lethality mode and "
        "the population. Ignoring either one merges two partitions the design "
        "keeps apart.")


def test_a_solo_self_found_character_is_given_no_account_slot() -> None:
    """An empty slot name is the answer, and a name would be the bug.

    If this returned a name, every Solo Self-Found character in the game would
    share one account record -- which is the exact opposite of what the mode
    means, and it would look like the feature working.
    """
    body = function_body(
        read(PARTITION_CPP), "FString UCataclysmSavePartition::AccountSlotName")

    assert "UsesAnAccountRecord" in body, (
        "AccountSlotName does not check whether the character uses an account "
        "record, so a Solo Self-Found character is given one. Every Solo "
        "Self-Found character would then share it.")


def test_a_record_is_never_named_after_an_unset_identifier() -> None:
    """Otherwise every character created before one exists overwrites the last.

    That failure looks exactly like saving not working, rather than like a
    missing identifier, which is why it is refused rather than allowed through.
    """
    for signature in ("FString UCataclysmSavePartition::CharacterSlotName",
                      "FString UCataclysmSavePartition::RunSlotName"):
        body = function_body(read(PARTITION_CPP), signature)
        assert "IsValid()" in body, (
            f"{signature.split('::')[1]} does not refuse an identifier that "
            f"was never set. Every record created before one exists would be "
            f"written to the same slot and overwrite the last. Issue #529.")


# ---------------------------------------------------------------------------
# Where the names come from
# ---------------------------------------------------------------------------

def test_the_slot_names_are_the_ones_the_design_lists(designed_slot_names) -> None:
    """A slot name is a filename that has to outlive the code that made it."""
    source = read(PARTITION_CPP)

    # THE NAMES ARE BUILT FROM PIECES, so each piece is looked for rather than
    # the whole name. Checking for the assembled string would fail on any
    # implementation that composes it, which is every sensible one.
    for name in designed_slot_names:
        prefix, population, mode = name.split("_")
        for piece in (prefix, population, mode):
            assert f'TEXT("{piece}")' in source, (
                f"docs/Save_System_Design.md lists the slot {name!r}, and "
                f"CataclysmSavePartition.cpp writes no TEXT(\"{piece}\"). The "
                f"C++ and the design disagree about what a save file is "
                f"called.")

    assert len(designed_slot_names) == 6, (
        f"docs/Save_System_Design.md lists {len(designed_slot_names)} account "
        f"slots. There are three lethality modes and two populations, so there "
        f"are six; if a mode or a population was added, the C++ needs the same "
        f"addition.")


def test_the_names_do_not_come_from_the_enum(designed_modes) -> None:
    """Renaming a C++ identifier must not rename every save file ever written.

    `UEnum::GetNameStringByValue` answers with whatever the enumerator happens
    to be called, so using it would make a refactor in C++ orphan every record
    already on disk, silently, with no error anywhere.
    """
    source = without_comments(read(PARTITION_CPP))

    assert "GetNameStringByValue" not in source, (
        "CataclysmSavePartition.cpp takes a slot name from the enum's own "
        "identifier. Renaming that identifier would then rename every save "
        "slot and orphan every record already written. Write the names out.")


def test_the_lethality_modes_match_the_design_table(designed_modes) -> None:
    """A mode in one place and not the other gets no storage, or a silent share.

    A fourth mode added to the design and not to the enum has nowhere to keep
    its empire tree. Added to the enum and not to the slot names, it falls
    through to a slot called Unknown.
    """
    declared = lethality_modes()

    assert set(declared) == set(designed_modes), (
        f"the lethality modes in CataclysmLethality.h are {sorted(declared)} "
        f"and the table in docs/Cataclysm_GDD_v2.md names "
        f"{sorted(designed_modes)}. A mode in one and not the other either has "
        f"nowhere to keep its empire tree or is stored under a name nobody "
        f"chose.")

    # AND THE ORDER IS THE DOCUMENT'S ORDER, least lethal first, so the numbers
    # a save file stores are not arbitrary.
    for position, mode in enumerate(designed_modes):
        assert declared[mode] == str(position), (
            f"{mode} is numbered {declared[mode]} and the design document "
            f"lists it {position} places in. The numbers are persisted, so "
            f"renumbering them changes what every existing save says its mode "
            f"is.")


def test_every_mode_is_given_a_name_in_a_slot() -> None:
    """A mode with no name written out falls through to a slot called Unknown.

    That is a deliberate choice -- it is visible in a directory listing, where a
    silent fall-through to Standard would not be -- but it is a fault to be
    noticed rather than a state to ship.
    """
    source = read(PARTITION_CPP)

    for mode in lethality_modes():
        assert f"ECataclysmLethality::{mode}" in source, (
            f"CataclysmSavePartition.cpp gives {mode} no name to use in a slot, "
            f"so every character in that mode would be stored under "
            f"'Account_<population>_Unknown' and would share a record with "
            f"every other unnamed mode.")
