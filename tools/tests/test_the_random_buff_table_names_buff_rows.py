"""The buffs a row's random buff may draw are Buff rows of the status table,
and every Buff row is either in the table or on the list of those left out.

Ruled 2026-10-12 under the owner's delegation, for "Shard of Anarchy (2-Piece
Bonus): Every 30 seconds, a random buff or debuff is applied to you". A drawn
buff is one entry of `UCataclysmAbilitySystemComponent::RandomBuffTable`, a
table in code. THE OWNER HAS BEEN ASKED WHICH BUFFS ARE IN THE POOL, and until
the answer the table holds the buffs a character can hold whose own
description the existing stat lines and conditions can express.

WHAT THIS FILE IS FOR. The table names each buff by the name of its row in
`game/Data/StatusEffects.csv`, and nothing in the engine compares the two: a
name spelled differently in the table would still be drawn, held and shown,
under a name no row has. And a Buff row added to or taken out of the status
table changes what "a random buff" could mean without anybody deciding whether
it joins the draw. Both are refused here.

WHEN THE OWNER ANSWERS, the table in the engine changes, and `IN_THE_TABLE`
and `LEFT_OUT` below change with it. That is the point of the second list: it
is the record of what was left out and why.
"""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
          / "CataclysmAbilitySystemComponent.cpp")
STATUS_TABLE = ROOT / "game" / "Data" / "StatusEffects.csv"

#: The buffs in the table, in the table's order, which is the order
#: `Cataclysm.RandomBuffPick` counts in.
IN_THE_TABLE = (
    "Mana Surge",
    "Tactical Advantage",
    "Famished",
    "Commander",
    "Divine Aegis",
    "Spellslinger",
)

#: Why ten of the Buff rows are not in the table, as ruled.
ANOTHERS = "it describes another set's own mechanic or an enemy's"

#: Every other Buff row, and why it is not in the table.
LEFT_OUT = {
    # ITS DESCRIPTION CANNOT BE EXPRESSED WITH WHAT EXISTS. A judgement of the
    # session that built the table; the coordinating session rules on each.
    "Dervish": "its attack speed is for melee attacks, and nothing reads attack speed with a melee tag",
    "Demonic Rage": "ten seconds from the moment health falls below a quarter is an event, not a condition",
    # EACH DESCRIBES ANOTHER SET'S OWN MECHANIC OR AN ENEMY'S. Ruled 2026-10-12,
    # pending the owner.
    "Warhound": ANOTHERS,
    "Plague": ANOTHERS,
    "Sanctified Power": ANOTHERS,
    "Temporal Flux": ANOTHERS,
    "Void": ANOTHERS,
    "Death": ANOTHERS,
    "Warzone Control Points": ANOTHERS,
    "Chaotic Barrier": ANOTHERS,
    "The Ascended": ANOTHERS,
    "Feasting": ANOTHERS,
}


def table_names() -> tuple[str, ...]:
    """The name of each entry of `RandomBuffTable`, in order."""
    text = SOURCE.read_text(encoding="utf-8")
    body = re.search(
        r"UCataclysmAbilitySystemComponent::RandomBuffTable\(\)\s*\{(.*?)\n\}",
        text, re.DOTALL)
    assert body, f"RandomBuffTable was not found in {SOURCE.name}"
    return tuple(re.findall(r'Buff\.Name = TEXT\("([^"]+)"\);', body.group(1)))


def buff_rows() -> dict[str, str]:
    """Every Buff row of the status table: its name and its description."""
    with STATUS_TABLE.open(encoding="utf-8", newline="") as handle:
        return {row["EffectName"]: row["Description"]
                for row in csv.DictReader(handle) if row["EffectKind"] == "Buff"}


def test_the_reader_finds_the_table() -> None:
    """A reader that finds nothing would pass the comparisons below."""
    assert len(table_names()) == len(IN_THE_TABLE) == 6


def test_the_table_holds_exactly_the_buffs_listed_here_in_this_order() -> None:
    assert table_names() == IN_THE_TABLE


def test_every_buff_in_the_table_is_a_buff_row_of_the_status_table() -> None:
    rows = buff_rows()
    assert rows, f"no Buff row was read from {STATUS_TABLE.name}"
    assert [name for name in table_names() if name not in rows] == []


def test_every_buff_row_is_in_the_table_or_on_the_list_of_those_left_out() -> None:
    """Eighteen Buff rows on 2026-10-12: six in the table and twelve left out.
    A row added to the status table fails here until somebody decides which
    list it joins."""
    rows = set(buff_rows())
    assert not set(IN_THE_TABLE) & set(LEFT_OUT)
    assert rows == set(IN_THE_TABLE) | set(LEFT_OUT)
    assert len(rows) == 18


def test_the_pin_names_the_buffs_in_the_tables_order() -> None:
    """`Cataclysm.RandomBuffPick` pins a buff by its place in the table, and
    its help text says which place is which."""
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index('TEXT("Cataclysm.RandomBuffPick")')
    declaration = text[start:text.index("ECVF_Default", start)]
    words = "".join(re.findall(r'"([^"]*)"', declaration))
    stated = ", ".join(f"{place} {name}" for place, name in enumerate(IN_THE_TABLE))
    assert stated in words, f"the help text does not say {stated!r}: {words!r}"
