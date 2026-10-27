"""The buffs a row's random buff may draw are Buff rows of the status table,
and every Buff row is either in the table or on the list of those left out.

Ruled 2026-10-12 under the owner's delegation, for "Shard of Anarchy (2-Piece
Bonus): Every 30 seconds, a random buff or debuff is applied to you". A drawn
buff is one entry of `UCataclysmAbilitySystemComponent::RandomBuffTable`, a
table in code. THE OWNER ANSWERED ON 2026-10-26, "Add everything": every Buff
row joins the pool once it is built as a standalone buff (issue #2538). Each
batch that builds some appends them to the table and moves them here.

WHAT THIS FILE IS FOR. The table names each buff by the name of its row in
`game/Data/StatusEffects.csv`, and nothing in the engine compares the two: a
name spelled differently in the table would still be drawn, held and shown,
under a name no row has. And a Buff row added to or taken out of the status
table changes what "a random buff" could mean without anybody deciding whether
it joins the draw. Both are refused here.

AS EACH BATCH LANDS, the table in the engine changes, and `IN_THE_TABLE`
and `LEFT_OUT` below change with it. That is the point of the second list: it
is the record of what is still left out and why.
"""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
          / "CataclysmAbilitySystemComponent.cpp")
STATUS_TABLE = ROOT / "game" / "Data" / "StatusEffects.csv"
EFFECT_TABLE = ROOT / "game" / "Data" / "EnchantmentEffects.csv"

#: The buffs in the table, in the table's order, which is the order
#: `Cataclysm.RandomBuffPick` counts in.
IN_THE_TABLE = (
    "Mana Surge",
    "Tactical Advantage",
    "Famished",
    "Commander",
    "Divine Aegis",
    "Spellslinger",
    # Batch 1 of issue #2538, 2026-10-26: each the stat lines of the set row
    # that builds it. Appended, so places 0 to 5 keep their buffs.
    "Dervish",
    "Warhound",
    # Batch 2 of issue #2538, 2026-10-26: each wears the set rows that build
    # it, by name. Appended, so places 0 to 7 keep their buffs.
    "Sanctified Power",
    "Demonic Rage",
    "Temporal Flux",
    "Void",
    # Batch 3 of issue #2538, 2026-10-26: each wears rows of its own, written
    # in code, because no row of the effect table states them. Appended, so
    # places 0 to 11 keep their buffs.
    "Death",
    "Feasting",
    "The Ascended",
)

#: Why eight of the Buff rows were not in the table, as ruled on 2026-10-12.
ANOTHERS = "it describes another set's own mechanic or an enemy's"

#: Every other Buff row, and why it is not in the table.
LEFT_OUT = {
    # EVERY ONE OF THESE JOINS THE TABLE once it is built as a standalone buff:
    # the owner answered "Add everything" on 2026-10-26 (issue #2538). The
    # reasons are why each is not built yet.
    #
    # EACH DESCRIBES ANOTHER SET'S OWN MECHANIC OR AN ENEMY'S. Ruled 2026-10-12.
    "Plague": ANOTHERS,
    "Chaotic Barrier": ANOTHERS,
}


def table_names() -> tuple[str, ...]:
    """The name of each entry of `RandomBuffTable`, in order."""
    text = SOURCE.read_text(encoding="utf-8")
    body = re.search(
        r"UCataclysmAbilitySystemComponent::RandomBuffTable\(\)\s*\{(.*?)\n\}",
        text, re.DOTALL)
    assert body, f"RandomBuffTable was not found in {SOURCE.name}"
    return tuple(re.findall(r'Buff\.Name = TEXT\("([^"]+)"\);', body.group(1)))


def worn_row_names() -> tuple[str, ...]:
    """Every effect row an entry of `RandomBuffTable` names to wear, in order."""
    text = SOURCE.read_text(encoding="utf-8")
    body = re.search(
        r"UCataclysmAbilitySystemComponent::RandomBuffTable\(\)\s*\{(.*?)\n\}",
        text, re.DOTALL)
    assert body, f"RandomBuffTable was not found in {SOURCE.name}"
    return tuple(re.findall(
        r'Buff\.WornRows\.Add\(FName\(TEXT\("([^"]+)"\)\)\);', body.group(1)))


def buff_rows() -> dict[str, str]:
    """Every Buff row of the status table: its name and its description."""
    with STATUS_TABLE.open(encoding="utf-8", newline="") as handle:
        return {row["EffectName"]: row["Description"]
                for row in csv.DictReader(handle) if row["EffectKind"] == "Buff"}


def test_the_reader_finds_the_table() -> None:
    """A reader that finds nothing would pass the comparisons below."""
    assert len(table_names()) == len(IN_THE_TABLE) == 15


def test_the_table_holds_exactly_the_buffs_listed_here_in_this_order() -> None:
    assert table_names() == IN_THE_TABLE


def test_every_buff_in_the_table_is_a_buff_row_of_the_status_table() -> None:
    rows = buff_rows()
    assert rows, f"no Buff row was read from {STATUS_TABLE.name}"
    assert [name for name in table_names() if name not in rows] == []


def test_every_buff_row_is_in_the_table_or_on_the_list_of_those_left_out() -> None:
    """Eighteen Buff rows on 2026-10-12: six in the table and twelve left out.
    Seventeen since 2026-10-26 (issue #2533 retired Warzone Control Points): six and eleven.
    Eight and nine since batch 1 of issue #2538 added Dervish and Warhound.
    Twelve and five since batch 2 added Sanctified Power, Demonic Rage,
    Temporal Flux and Void.
    Fifteen and two since batch 3 added Death, Feasting and The Ascended.
    A row added to the status table fails here until somebody decides which
    list it joins."""
    rows = set(buff_rows())
    assert not set(IN_THE_TABLE) & set(LEFT_OUT)
    assert rows == set(IN_THE_TABLE) | set(LEFT_OUT)
    assert len(rows) == 17


def test_the_pin_names_the_buffs_in_the_tables_order() -> None:
    """`Cataclysm.RandomBuffPick` pins a buff by its place in the table, and
    its help text says which place is which."""
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index('TEXT("Cataclysm.RandomBuffPick")')
    declaration = text[start:text.index("ECVF_Default", start)]
    words = "".join(re.findall(r'"([^"]*)"', declaration))
    stated = ", ".join(f"{place} {name}" for place, name in enumerate(IN_THE_TABLE))
    assert stated in words, f"the help text does not say {stated!r}: {words!r}"


def test_every_row_a_buff_wears_is_a_row_of_the_effect_table() -> None:
    """Batch 2 of issue #2538: an entry may name effect rows to wear, and the
    engine leaves out a named row the loaded table does not hold, with only a
    warning in the log. So a row renamed in the workbook would make a buff do
    nothing, and no automation test reads the names. They are read here.

    THE COUNT IS PINNED, six rows from four entries (Sanctified Power one,
    Demonic Rage three, Temporal Flux one, Void one), so a reader that finds
    nothing cannot pass."""
    worn = worn_row_names()
    assert len(worn) == 6, worn
    with EFFECT_TABLE.open(encoding="utf-8", newline="") as handle:
        rows = {row["Name"] for row in csv.DictReader(handle)}
    assert rows, f"no row was read from {EFFECT_TABLE.name}"
    assert [name for name in worn if name not in rows] == []
