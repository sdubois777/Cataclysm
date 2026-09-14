"""A reword of an enchantment's sentence must not rename its row.

WHY THIS EXISTS. An enchantment's DataTable row name is built from the FIRST 48
CHARACTERS of its sentence -- `row_name(kind, text[:48])` in
`tools/generate_datatables.py` -- and `FCataclysmRolledEnchantment` stores that
row name on a dropped item, with the `SaveGame` specifier. So a renamed row is
orphaned on every saved item carrying it: the item survives, the enchantment on
it finds nothing, and no error is raised anywhere.

`docs/DECISIONS.md` has stated that consequence since 2026-09-11, in the entry
about the ranged close-range reword:

    "a renamed row would orphan every saved item carrying it. A reword that
     changed the first 48 characters would have that cost and this one does not."

IT HAS ALREADY HAPPENED SIXTEEN TIMES, which is why a written rule was not
enough. Walking every revision of the two enchantment tables and comparing the
Name sets finds fifteen rows renamed in one wording pass -- "critical hits"
became "critical strikes" -- and one more when a number inside the first 48
characters changed. Nothing failed, because the name lives in a generated file
that the generator rewrites wholesale and nothing compared it to anything.

SO THE PIN IS A SEPARATE FILE THAT THE GENERATOR DOES NOT WRITE.
`enchantment_row_names.txt` beside this one holds every row name. Editing it is a
human act, and that is the entire mechanism: a rename cannot land without
somebody opening that file and seeing the pair.

WHAT THIS DOES NOT DO. It does not stop a rename, and it should not -- the
project owner may well decide the saved games of a pre-release build are not
worth keeping. Issue #1799 carries that question. This only makes the rename
visible at the moment it is made, rather than discovered by reading rows by hand
weeks later.
"""
import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
TABLES = (DATA / "EnchantmentsNegative.csv", DATA / "EnchantmentsPositive.csv")
PINNED = pathlib.Path(__file__).with_name("enchantment_row_names.txt")

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: How many characters of the sentence the row name is built from. Read off the
#: generator rather than restated, so the two cannot drift: `enchantments` builds
#: its rows with `row_name(kind, text[:48])`.
NAME_CHARACTERS = 48


def name_for(kind: str, sentence: str) -> str:
    """The row name an enchantment with these words would get.

    THE SAME CALL THE GENERATOR MAKES, not a reimplementation of it. A copy here
    would answer correctly until the generator's naming changed, and then answer
    confidently and wrongly -- which is the failure this whole file is about.
    """
    return gen.row_name(kind, sentence[:NAME_CHARACTERS])


def renames(before: str, after: str, kind: str = "Positive") -> bool:
    """Whether rewording an enchantment from these words to those renames it."""
    return name_for(kind, before) != name_for(kind, after)


@pytest.fixture(scope="module")
def current() -> list[str]:
    names: list[str] = []
    for path in TABLES:
        if not path.is_file():
            pytest.skip(f"{path.name} has not been generated")
        with path.open(encoding="utf-8-sig", newline="") as handle:
            names += [row["Name"] for row in csv.DictReader(handle)]
    assert names, "the enchantment tables are empty, so every check below would "\
                  "pass having read nothing"
    return names


@pytest.fixture(scope="module")
def pinned() -> list[str]:
    lines = PINNED.read_text(encoding="utf-8").splitlines()
    return [line for line in lines if line and not line.startswith("#")]


def paired(removed: list[str], added: list[str]) -> list[tuple[str, str]]:
    """Removed and added names paired up where one is plainly the other reworded.

    A RENAME LOOKS LIKE A REMOVAL AND AN ADDITION, and reading it as two
    unrelated events is what makes it easy to wave through. Two names are paired
    when they share a long opening or a long ending: "Critical_hits_drain_3_6"
    and "Critical_strikes_drain_3_6" share the tail, and a number changing inside
    the words leaves the opening shared.
    """
    pairs = []
    for gone in removed:
        for new in added:
            if gone[:20] == new[:20] or gone[-25:] == new[-25:]:
                pairs.append((gone, new))
    return pairs


def test_no_enchantment_row_has_been_renamed(current, pinned):
    removed = sorted(set(pinned) - set(current))
    added = sorted(set(current) - set(pinned))
    if not removed and not added:
        return

    pairs = paired(removed, added)
    story = ""
    if pairs:
        story = "\n\nthese look like renames rather than one row leaving and "\
                "another arriving:\n" + "\n".join(
                    f"  {gone}\n  -> {new}" for gone, new in pairs)

    assert not removed, (
        f"{len(removed)} enchantment row name(s) are gone and {len(added)} are "
        f"new.\ngone:\n" + "\n".join(f"  {n}" for n in removed)
        + "\nnew:\n" + "\n".join(f"  {n}" for n in added) + story
        + f"\n\nA row name is built from the first {NAME_CHARACTERS} characters "
        "of the enchantment's sentence, and a dropped item stores that name, so "
        "a renamed row is orphaned on every saved item carrying it. If a rename "
        "is what you meant, say so out loud and update "
        f"{PINNED.name}; issue #1799 carries the question of whether existing "
        "saves matter. If it is not what you meant, reword later in the "
        "sentence: a change past the first "
        f"{NAME_CHARACTERS} characters leaves the name alone.")

    assert not added, (
        f"{len(added)} enchantment(s) are new and none was removed, so nothing "
        f"was renamed:\n" + "\n".join(f"  {n}" for n in added)
        + f"\n\nAdd them to {PINNED.name}.")


def test_the_pin_holds_every_row_exactly_once(current, pinned):
    """WITHOUT THIS THE CHECK ABOVE COMPARES TWO SETS AND MISSES A DUPLICATE.
    Set difference is empty when a name appears twice on one side and once on
    the other, so the counts are asserted as well as the membership."""
    assert len(pinned) == len(set(pinned)), "a name is pinned twice"
    assert len(current) == len(set(current)), "the tables hold a name twice"
    assert len(current) == len(pinned), (
        f"{len(current)} enchantment rows and {len(pinned)} pinned names")


def test_a_reword_inside_the_first_characters_renames_the_row():
    """The rule itself, on MADE-UP sentences.

    Checked on invented pairs rather than on the real tables, so that no change
    to the real data can stop this being exercised -- the arrangement
    `test_longer_excuses_a_negative_value_on_one_stat_only` uses in
    `test_enchantment_effects_match_the_row_text.py`, and for the same reason.
    """
    # THE CASE THAT PROMPTED THIS, from the ruling on issue #1642. Eighty
    # characters long, so the sentence is not short -- and the reword still
    # renames it, because it falls at the start. A rule about sentence LENGTH
    # would have called this one safe.
    before = ("Each void splinter stack on an enemy increases your damage "
              "against them by 3%-5%")
    after = "Enemies carrying a void splinter take 9%-15% increased damage from you"
    assert len(before) > NAME_CHARACTERS
    assert renames(before, after)

    # AND THE CASE docs/DECISIONS.md RECORDS AS SAFE, from 2026-09-11: a clause
    # appended past the first 48 characters, leaving the name alone.
    kept = "Ranged skills deal 15%-30% less damage at close range"
    widened = ("Ranged skills deal 15%-30% less damage at close range "
               "(within 5 meters)")
    assert not renames(kept, widened)
    assert name_for("Negative", kept) == name_for("Negative", widened)

    # A SENTENCE SHORTER THAN THE CUT CANNOT BE REWORDED AT ALL WITHOUT A
    # RENAME, which is what made the minion row of issue #1792 unwritable.
    short = "Your minions have 20%-50% less hp"
    assert len(short) < NAME_CHARACTERS
    assert renames(short, "Your minions have 20%-50% reduced HP")

    # AND THE KIND IS PART OF THE NAME, so the same words on the two tables are
    # two different rows rather than a collision.
    assert name_for("Positive", kept) != name_for("Negative", kept)


def test_the_rename_pairing_reads_a_rename_as_one_event():
    """The helper that turns a removal and an addition into a readable pair,
    on made-up names, including a case it must NOT pair."""
    gone = "Positive_Critical_hits_drain_3_6_of_your_current_HP"
    new = "Positive_Critical_strikes_drain_3_6_of_your_current_HP"
    assert paired([gone], [new]) == [(gone, new)]

    # AND A GENUINE ADDITION BESIDE A GENUINE REMOVAL IS NOT A RENAME. Without
    # this the helper could pair anything with anything and the message would
    # invent a rename that did not happen.
    assert paired(["Positive_You_have_no_armor"],
                  ["Negative_Kills_no_longer_generate_any_experience"]) == []
