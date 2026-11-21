"""Every minion, gadget and trap no skill makes reserves the figure the design document states for its kind.

WHY THIS EXISTS. Issue #2787. The owner ruled on 2026-11-20: "All gadgets/traps/minions should hold some reserve.
This amount should be different based on what is being used, but they all should reserve some." Six kinds are made by
something other than a skill row: the risen imp, the imp a necrosis kill raises, the Warhound, the Tyrant's Minion, the
skill gadget and the skill trap. A skill row's reserve is checked by `test_every_gadget_skill_reserves_fervour.py`;
these six have no row, so their figures are constants in C++ beside each kind's other numbers, and the design document
`docs/Cataclysm_GDD_v2.md` states them in a table. Two copies of a number drift apart unless something compares them.

WHAT IS CHECKED:
- the design document's table names exactly the six kinds, so a seventh kind cannot be added to one and not the other;
- each figure in the table is the figure the game uses: the constant in the header for five, and Summon Imp's
  `FervourReserve` in `game/Data/WeaponSkills.csv` for the risen imp, which reads that row;
- every figure is above zero, because "they all should reserve some", and a zero reads as the rule applied when it
  was skipped.

WHAT IS NOT CHECKED HERE: that the game RECORDS each reserve when the thing is made. That is behaviour, and the Unreal
group `Cataclysm.KindReserves.` tests it for every kind.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SKILLS_CSV = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
ABILITY_SYSTEM = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"

#: The header row of the design document's table, which is how the table is found.
TABLE_HEADER = "| Kind | What makes it | Reserves each |"

#: The kind as the table names it, and where the game holds its figure: a header and the constant's name, or None
#: for the risen imp, whose figure is Summon Imp's row's.
KINDS: dict[str, tuple[str, str] | None] = {
    "Risen imp": None,
    "Imp raised by a necrosis kill": ("CataclysmRisenImps.h", "NecrosisImpFervourReserve"),
    "Warhound": ("CataclysmMinion.h", "WarhoundFervourReserve"),
    "Tyrant's Minion": ("CataclysmMinion.h", "TyrantsMinionFervourReserve"),
    "Skill gadget": ("CataclysmMinion.h", "SkillGadgetFervourReserve"),
    "Skill trap": ("CataclysmMinion.h", "SkillTrapFervourReserve"),
}

#: The skill whose row the risen imp reads, as `UCataclysmRisenImps` names it.
SUMMON_IMP_ROW = "Demonic_Staff_Special"


def design_table() -> dict[str, float]:
    """The table's kinds and the leading number of each row's last cell, in the order written."""
    lines = DESIGN.read_text(encoding="utf-8").splitlines()
    starts = [index for index, line in enumerate(lines) if line.strip() == TABLE_HEADER]
    assert len(starts) == 1, (
        f"{DESIGN.name} should hold the table headed {TABLE_HEADER!r} exactly once and holds it "
        f"{len(starts)} times. It is in the paragraph that opens 'Every minion, gadget and trap the player makes "
        f"reserves Fervour.'")
    table: dict[str, float] = {}
    for line in lines[starts[0] + 2:]:
        if not line.startswith("|"):
            break
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        figure = re.match(r"\d+(?:\.\d+)?", cells[-1])
        assert figure, f"The row {line!r} does not open its last cell with a number."
        table[cells[0]] = float(figure.group(0))
    return table


def constant(header: str, name: str) -> float:
    """A `static constexpr float` the header defines, read from its definition and not from a comment naming it."""
    text = (ABILITY_SYSTEM / header).read_text(encoding="utf-8")
    found = re.findall(rf"^\s*static constexpr float {name} = (\d+(?:\.\d+)?)f;", text, re.M)
    assert len(found) == 1, f"{header} should define {name} once and defines it {len(found)} times."
    return float(found[0])


def summon_imps_reserve() -> float:
    """`FervourReserve` on Summon Imp's row of the generated weapon skill table."""
    if not SKILLS_CSV.is_file():
        pytest.skip(f"{SKILLS_CSV.name} is not present. Run python tools/generate_datatables.py")
    with SKILLS_CSV.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            if row["Name"] != SUMMON_IMP_ROW:
                continue
            for part in row["ShapeParams"].split(";"):
                key, _, value = part.partition("=")
                if key.strip() == "FervourReserve":
                    return float(value)
    raise AssertionError(f"{SUMMON_IMP_ROW} in {SKILLS_CSV.name} states no FervourReserve.")


def game_figure(kind: str) -> float:
    """The figure the game uses for a kind."""
    where = KINDS[kind]
    return summon_imps_reserve() if where is None else constant(*where)


def test_the_design_table_names_every_kind_and_no_other():
    """A kind added to the game and not to the document, or the other way round, is caught here."""
    assert sorted(design_table()) == sorted(KINDS), (
        f"The design document's table names {sorted(design_table())} and this file knows {sorted(KINDS)}. A new kind "
        f"needs a row in both, and a figure in the game.")


@pytest.mark.parametrize("kind", sorted(KINDS))
def test_each_kinds_figure_is_the_one_the_game_uses(kind):
    """The document's number and the game's number are the same number."""
    stated = design_table().get(kind)
    used = game_figure(kind)
    assert stated == used, (
        f"The design document says a {kind} reserves {stated:g} and the game reserves {used:g}. Change whichever is "
        f"wrong; the coordinating session ruled the figures on 2026-11-20, and docs/DECISIONS.md records them.")


@pytest.mark.parametrize("kind", sorted(KINDS))
def test_every_kind_reserves_something(kind):
    """ "They all should reserve some." A figure of zero is the rule skipped while it reads as obeyed."""
    assert game_figure(kind) > 0, f"A {kind} reserves {game_figure(kind):g}, and every kind must reserve some."
