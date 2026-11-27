"""Write the aura reservation of 2026-11-27 into the design workbook and three class tree files.

    python tools/apply_aura_reservation_2026_11_27.py
    python tools/apply_aura_reservation_2026_11_27.py --check

AURAS RESERVE MAXIMUM MANA INSTEAD OF DRAINING IT, ruled by the coordinating session on 2026-11-27 under the owner's
choice "Reserve, don't drain". Three stats carry it, in the shape of the Fervour reservation stats
(`minion_reserve_percent_less`, `minion_reserve`, `minion_reserve_reduction`): `mana_reserve_percent_less`, a summed
percentage scoped by Required Tags whose negative rows raise it; `mana_reserve`, a `more` stat; and
`mana_reserve_reduction`, a flat amount off each aura's reservation. This writes:

  1. THE SKILL SLOTS SHEET: a `Mana Reserved` column (appended after the last header, as `Fervour Cost` was), 50 on
     the Aura row and 0 on every other; the Aura's `Mana Cost` from 20 to 0, because switching an aura on is free; and
     the Aura's `Note`, which said it drained resource per second.
  2. TWO ENCHANTMENTS REWORDED, their Weight and Tags untouched. Each sentence changes inside its first 48 characters,
     so each row name changes; `FCataclysmEnchantmentRenames::Aliases()` and `tools/tests/enchantment_row_names.txt`
     carry the old names, which this script does not write.
       "Your aura costs 30%-50% less mana per second" -> "Your auras reserve 30%-50% less mana"
           its effect: `mana_cost` more -30..-50 -> `mana_reserve` more -30..-50, Slot.Aura
       "Your auras cost 100%-200% increased mana per second" -> "Your auras reserve 50%-100% increased mana"
           its effect: `mana_cost` increased 100..200 -> `mana_reserve_percent_less` flat -50..-100, Slot.Aura
  3. THREE NEW BENEFITS in the shape of "Your minions reserve 15%-30% reduced Fervour" (Generic, Weight 2, not rolled
     down), appended to the end of the benefits list, each with one effect row appended to the Enchantment Effects
     sheet.
  4. FOUR PASSIVE NODES each gain one row on the Passive Effects sheet (appended) and one clause on the end of their
     sentence in `docs/<Class>_Class_Tree_Final.json`, which generates `game/Data/PassiveNodes.csv`. What each
     sentence already said is kept as it is.

WRITTEN TO SURVIVE A WORKBOOK ANOTHER BRANCH HAS CHANGED SINCE. Every row is found by its key -- a slot's name, an
enchantment's sentence or row name, a node's id and stat -- and never by its row number; a new row goes after the last
row of its list, wherever that is by then.

IDEMPOTENT AND REFUSING: a cell or sentence already holding the new value is left alone; one holding the old value is
written; one holding anything else is refused and nothing is saved. A new row already present is checked cell by cell
the same way. EVERY PART IS CHECKED BEFORE ANY IS WRITTEN. `--check` writes nothing and exits 1 when anything would be
written.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DOCS = REPO_ROOT / "docs"

sys.path.insert(0, str(REPO_ROOT / "tools"))

from generate_datatables import row_name  # noqa: E402

#: How many characters of a sentence its row name is built from, as `enchantments` in the generator builds it.
NAME_CHARACTERS = 48


def enchantment_key(kind: str, sentence: str) -> str:
    """The row name the generator gives an enchantment with these words."""
    return row_name(kind, sentence[:NAME_CHARACTERS])


class RulingError(Exception):
    """A cell or sentence holds neither the old value nor the new."""


# --------------------------------------------------------------------------
# What is written
# --------------------------------------------------------------------------

AURA_RESERVED = 50
AURA_MANA_COST_BEFORE = 20
OLD_AURA_NOTE = ("Persistent and toggled, draining resource per second. This is per second rather than per use, and it "
                 "is what an aura stating damage is typically worth: an aura whose row states none deals none.")
#: THE COORDINATING SESSION'S SENTENCE, with the old note's clause on the slot's damage kept after it, because that
#: clause explains the Damage Percent column, which the ruling did not change.
NEW_AURA_NOTE = ("Persistent and toggled; while on it reserves this much maximum mana at skill level 1, lowered by "
                 "the skill's level. Switching it on costs nothing. Its damage is per second rather than per use, and "
                 "it is what an aura stating damage is typically worth: an aura whose row states none deals none.")

OLD_POSITIVE_TEXT = "Your aura costs 30%-50% less mana per second"
NEW_POSITIVE_TEXT = "Your auras reserve 30%-50% less mana"
OLD_NEGATIVE_TEXT = "Your auras cost 100%-200% increased mana per second"
NEW_NEGATIVE_TEXT = "Your auras reserve 50%-100% increased mana"

OLD_POSITIVE = enchantment_key("Positive", OLD_POSITIVE_TEXT)
NEW_POSITIVE = enchantment_key("Positive", NEW_POSITIVE_TEXT)
OLD_NEGATIVE = enchantment_key("Negative", OLD_NEGATIVE_TEXT)
NEW_NEGATIVE = enchantment_key("Negative", NEW_NEGATIVE_TEXT)

AURA_TAGS = "Slot.Aura, Stat.Resource.Mana"


@dataclass(frozen=True)
class NewBenefit:
    """A new benefit on the Enchantments sheet and its one Enchantment Effects row."""

    sentence: str
    tags: str
    stat: str
    low: float
    high: float
    required_tags: str

    @property
    def key(self) -> str:
        return enchantment_key("Positive", self.sentence)


NEW_BENEFITS = (
    NewBenefit("Your auras reserve 15%-30% reduced mana", AURA_TAGS, "mana_reserve_percent_less", 15, 30,
               "Slot.Aura"),
    NewBenefit("Each aura you hold reserves 5-15 less mana", AURA_TAGS, "mana_reserve_reduction", 5, 15, "Slot.Aura"),
    NewBenefit("Your gadgets reserve 15%-30% reduced Fervour", "Type.Gadget", "minion_reserve_percent_less", 15, 30,
               "Type.Gadget"),
)

#: THE SHAPE THEY COPY: "Your minions reserve 15%-30% reduced Fervour" is Generic, Weight 2, rolls up, in no set.
BENEFIT_TYPE = "Generic"
BENEFIT_WEIGHT = 2


@dataclass(frozen=True)
class NodeRow:
    """A passive node's new row and the clause its sentence gains."""

    tree: str
    node: str
    name: str
    stat: str
    per_point: float
    required_tags: str | None
    old_sentence: str
    clause: str

    @property
    def key(self) -> str:
        return f"{self.tree}_{self.node}"

    @property
    def new_sentence(self) -> str:
        return self.old_sentence + self.clause


NODE_ROWS = (
    NodeRow("Ritualist", "basic_d_c0", "Wider Vessel", "mana_reserve_percent_less", 3, "Slot.Aura",
            "+2% increased Maximum Mana per point.", " Your auras reserve 3% reduced mana per point."),
    # NO TAGS, as Cheap Bones (`Necromancer_basic_ch_c1`) has none: every kind that reserves Fervour.
    NodeRow("Ritualist", "basic_c_c1", "Reserve", "minion_reserve_percent_less", 2, None,
            "+2% increased maximum Fervour per point.", " Your minions reserve 2% reduced Fervour per point."),
    NodeRow("Masochist", "basic_fl_b2", "Beacon of Despair", "mana_reserve_percent_less", 2, "Slot.Aura",
            "You radiate an aura that applies a random debuff to enemies within 6 metres every 3 seconds, lasting 4% "
            "longer per point. You deal 1% increased damage per point to enemies for each debuff they carry.",
            " Your auras reserve 2% reduced mana per point."),
    NodeRow("Bulwark", "basic_tw_t3_001", "Aura of Pain", "mana_reserve_percent_less", 2, "Slot.Aura",
            "+5% aura retaliation damage per point. Enemies within 3 meters take retaliation damage equal to a "
            "percentage of damage dealt to you.",
            " Your auras reserve 2% reduced mana per point."),
)


# --------------------------------------------------------------------------
# The workbook
# --------------------------------------------------------------------------

class _Plan:
    """The writes a run would make, collected before any is made, and the report."""

    def __init__(self) -> None:
        self.writes: list[tuple[object, int, int, object]] = []
        self.report: list[str] = []

    def cell(self, sheet, row: int, column: int, before: object, after: object, where: str) -> None:
        current = sheet.cell(row, column).value
        if current == after:
            self.report.append(f"{where}: already {after!r}; left alone")
        elif current == before:
            self.writes.append((sheet, row, column, after))
            self.report.append(f"{where}: {before!r} -> {after!r}")
        else:
            raise RulingError(f"{where} is {current!r}, neither {before!r} nor {after!r}. Nothing was written")


def _headers(sheet) -> dict[str, int]:
    """Header text to its 1-based column."""
    return {str(cell.value).strip(): cell.column for cell in sheet[1] if cell.value is not None}


def _last_header_column(sheet) -> int:
    return max((cell.column for cell in sheet[1] if cell.value is not None), default=0)


def _rows_where(sheet, column: int, keys: tuple[object, ...]) -> list[int]:
    return [row for row in range(2, sheet.max_row + 1) if sheet.cell(row, column).value in keys]


def _one_row(sheet, column: int, keys: tuple[object, ...]) -> int:
    found = _rows_where(sheet, column, keys)
    if len(found) != 1:
        raise RulingError(f"{sheet.title}: {len(found)} rows hold {keys[0]!r}, not 1. Nothing was written")
    return found[0]


def _row_after_the_last(sheet, column: int, reserved: set[int], empty_columns: tuple[int, ...]) -> int:
    """The first row after the last one holding something in `column`, not already taken by this run.

    `empty_columns` must all be empty in the row chosen: the Enchantments sheet holds two lists side by side, so a row
    past the last benefit may still hold a drawback, and that is fine as long as the benefit's own cells are clear.
    """
    last = max((row for row in range(2, sheet.max_row + 1) if sheet.cell(row, column).value not in (None, "")),
               default=1)
    row = max([last] + sorted(reserved)) + 1
    for each in empty_columns:
        if sheet.cell(row, each).value not in (None, ""):
            raise RulingError(f"{sheet.title} row {row}, after the last of its list, is not empty in column {each}. "
                              "Nothing was written")
    return row


def _skill_slots(book, plan: _Plan) -> None:
    sheet = book["Skill Slots"]
    headers = _headers(sheet)
    slot_column = headers["Slot"]
    if "Mana Reserved" in headers:
        reserved_column = headers["Mana Reserved"]
        plan.report.append("Skill Slots: the Mana Reserved column is already there; left alone")
    else:
        reserved_column = _last_header_column(sheet) + 1
        plan.cell(sheet, 1, reserved_column, None, "Mana Reserved", "Skill Slots header")
    slots = [row for row in range(2, sheet.max_row + 1) if sheet.cell(row, slot_column).value not in (None, "")]
    if sum(1 for row in slots if sheet.cell(row, slot_column).value == "Aura") != 1:
        raise RulingError("Skill Slots: there is not exactly one Aura row. Nothing was written")
    for row in slots:
        slot = sheet.cell(row, slot_column).value
        plan.cell(sheet, row, reserved_column, None, AURA_RESERVED if slot == "Aura" else 0,
                  f"Skill Slots {slot} Mana Reserved")
    aura = _one_row(sheet, slot_column, ("Aura",))
    plan.cell(sheet, aura, headers["Mana Cost"], AURA_MANA_COST_BEFORE, 0, "Skill Slots Aura Mana Cost")
    plan.cell(sheet, aura, headers["Note"], OLD_AURA_NOTE, NEW_AURA_NOTE, "Skill Slots Aura Note")


def _enchantments(book, plan: _Plan) -> None:
    sheet = book["Enchantments"]
    # BY POSITION, as the generator reads this sheet: benefits in A to E, drawbacks in F to J, the benefit's set in K.
    positives, kind, weight, tags, rolls_down, negatives, positive_set = 1, 2, 3, 4, 5, 6, 11
    for column, old, new, label in ((positives, OLD_POSITIVE_TEXT, NEW_POSITIVE_TEXT, "benefit"),
                                    (negatives, OLD_NEGATIVE_TEXT, NEW_NEGATIVE_TEXT, "drawback")):
        row = _one_row(sheet, column, (old, new))
        plan.cell(sheet, row, column, old, new, f"Enchantments {label} sentence")

    taken: set[int] = set()
    for benefit in NEW_BENEFITS:
        found = _rows_where(sheet, positives, (benefit.sentence,))
        if len(found) > 1:
            raise RulingError(f"Enchantments: {len(found)} rows hold {benefit.sentence!r}. Nothing was written")
        if found:
            row = found[0]
        else:
            row = _row_after_the_last(sheet, positives, taken,
                                      (positives, kind, weight, tags, rolls_down, positive_set))
            taken.add(row)
        where = f"Enchantments new benefit {benefit.sentence!r}"
        plan.cell(sheet, row, positives, None, benefit.sentence, f"{where} sentence")
        plan.cell(sheet, row, kind, None, BENEFIT_TYPE, f"{where} type")
        plan.cell(sheet, row, weight, None, BENEFIT_WEIGHT, f"{where} weight")
        plan.cell(sheet, row, tags, None, benefit.tags, f"{where} tags")
        plan.cell(sheet, row, rolls_down, None, None, f"{where} rolls down")
        plan.cell(sheet, row, positive_set, None, None, f"{where} set")


def _effect_cells(sheet, plan: _Plan, row: int, values: dict[str, tuple[object, object]], where: str) -> None:
    headers = _headers(sheet)
    for column, (before, after) in values.items():
        plan.cell(sheet, row, headers[column], before, after, f"{where} {column}")


def _enchantment_effects(book, plan: _Plan) -> None:
    sheet = book["Enchantment Effects"]
    headers = _headers(sheet)
    key = headers["Enchantment"]
    for old, new, values in (
            (OLD_POSITIVE, NEW_POSITIVE, {
                "Effect": (OLD_POSITIVE_TEXT, NEW_POSITIVE_TEXT),
                "Stat": ("mana_cost", "mana_reserve"),
                "Value Kind": ("more", "more"),
                "Value Low": (-30, -30),
                "Value High": (-50, -50),
                "Required Tags": ("Slot.Aura", "Slot.Aura")}),
            (OLD_NEGATIVE, NEW_NEGATIVE, {
                "Effect": (OLD_NEGATIVE_TEXT, NEW_NEGATIVE_TEXT),
                "Stat": ("mana_cost", "mana_reserve_percent_less"),
                "Value Kind": ("increased", "flat"),
                "Value Low": (100, -50),
                "Value High": (200, -100),
                "Required Tags": ("Slot.Aura", "Slot.Aura")})):
        row = _one_row(sheet, key, (old, new))
        # ITS NAME LAST in the report's order only; every cell is found through the one row number above.
        values = dict(values, Enchantment=(old, new))
        _effect_cells(sheet, plan, row, values, f"Enchantment Effects {old}")

    taken: set[int] = set()
    for benefit in NEW_BENEFITS:
        found = _rows_where(sheet, key, (benefit.key,))
        if len(found) > 1:
            raise RulingError(f"Enchantment Effects: {len(found)} rows hold {benefit.key}, not 1. Nothing was written")
        if found:
            row = found[0]
        else:
            row = _row_after_the_last(sheet, key, taken, tuple(headers.values()))
            taken.add(row)
        _effect_cells(sheet, plan, row, {
            "Enchantment": (None, benefit.key),
            "Effect": (None, benefit.sentence),
            "Stat": (None, benefit.stat),
            "Value Kind": (None, "flat"),
            "Value Low": (None, benefit.low),
            "Value High": (None, benefit.high),
            "Required Tags": (None, benefit.required_tags)}, f"Enchantment Effects {benefit.key}")


def _passive_effects(book, plan: _Plan) -> None:
    sheet = book["Passive Effects"]
    headers = _headers(sheet)
    taken: set[int] = set()
    for each in NODE_ROWS:
        found = [row for row in _rows_where(sheet, headers["Node"], (each.key,))
                 if sheet.cell(row, headers["Stat"]).value == each.stat]
        if len(found) > 1:
            raise RulingError(f"Passive Effects: {len(found)} rows on {each.key} state {each.stat}. "
                              "Nothing was written")
        if found:
            row = found[0]
        else:
            row = _row_after_the_last(sheet, headers["Node"], taken, tuple(headers.values()))
            taken.add(row)
        _effect_cells(sheet, plan, row, {
            "Node": (None, each.key),
            "Stat": (None, each.stat),
            "Value Kind": (None, "flat"),
            "Value Per Point": (None, each.per_point),
            "Required Tags": (None, each.required_tags),
            "Node Name": (None, each.name)}, f"Passive Effects {each.key} {each.stat}")


def _workbook(path: pathlib.Path, check_only: bool) -> list[str]:
    book = openpyxl.load_workbook(path)
    plan = _Plan()
    _skill_slots(book, plan)
    _enchantments(book, plan)
    _enchantment_effects(book, plan)
    _passive_effects(book, plan)
    if plan.writes and not check_only:
        for sheet, row, column, value in plan.writes:
            sheet.cell(row, column).value = value
        book.save(path)
    plan.report.append(f"{len(plan.writes)} cell(s) {'to write' if check_only else 'written'}")
    return plan.report


# --------------------------------------------------------------------------
# The tree files
# --------------------------------------------------------------------------

def _swap_sentence(text: str, node: str, old_sentence: str, new_sentence: str, where: str, name: str) -> str:
    """The tree file's text with one node's sentence replaced, every other byte kept."""
    old, new = (json.dumps(s, ensure_ascii=False)[1:-1] for s in (old_sentence, new_sentence))
    # THE SAME WORDS MAY BE ANOTHER NODE'S TOO ("+2% increased Maximum Mana per point." is three Ritualist
    # nodes'), so the sentence replaced is the first one after this node's id and before the next node's.
    ids = list(re.finditer(r'"id":\s*"([^"]*)"', text))
    mine = [i for i, m in enumerate(ids) if m.group(1) == node]
    if len(mine) != 1:
        raise RulingError(f"{name}: the id {node} appears {len(mine)} times. Nothing was written")
    start = ids[mine[0]].end()
    end = ids[mine[0] + 1].start() if mine[0] + 1 < len(ids) else len(text)
    quoted = f'"description": "{old}"'
    at = text.find(quoted, start, end)
    if at < 0:
        raise RulingError(f"{where} was not found in {name} in its expected shape. Nothing was written")
    return text[:at] + f'"description": "{new}"' + text[at + len(quoted):]


def _trees(folder: pathlib.Path, check_only: bool) -> list[str]:
    """Each node's sentence gains its clause. Every file is checked before any is written."""
    report: list[str] = []
    pending: dict[pathlib.Path, str] = {}
    for each in NODE_ROWS:
        path = folder / f"{each.tree}_Class_Tree_Final.json"
        text = pending.get(path) or path.read_bytes().decode("utf-8")
        nodes = [n for n in json.loads(text)["nodes"] if n["id"] == each.node]
        if len(nodes) != 1:
            raise RulingError(f"{path.name}: {len(nodes)} nodes are {each.node}, not 1. Nothing was written")
        current = nodes[0]["data"]["description"]
        where = f"{each.key} ({each.name})'s sentence"
        if current == each.new_sentence:
            report.append(f"{where}: already written; left alone")
            continue
        if current != each.old_sentence:
            raise RulingError(f"{where} is {current!r}, neither the old sentence nor the new. Nothing was written")
        pending[path] = _swap_sentence(text, each.node, each.old_sentence, each.new_sentence, where, path.name)
        report.append(f"{where}: {'to write' if check_only else 'written'}")
    if not check_only:
        for path, text in pending.items():
            path.write_bytes(text.encode("utf-8"))
    report.append(f"{len(pending)} tree file(s) {'to write' if check_only else 'written'}")
    return report


def apply(path: pathlib.Path = WORKBOOK, folder: pathlib.Path = DOCS, check_only: bool = False) -> list[str]:
    """Everything the ruling writes; return the report. The last line says whether anything was or would be."""
    if not check_only:
        apply(path, folder, check_only=True)
    report = _workbook(path, check_only)
    cells_pending = not report[-1].startswith("0 cell")
    trees = _trees(folder, check_only)
    trees_pending = not trees[-1].startswith("0 tree")
    report += trees
    if not (cells_pending or trees_pending):
        report.append("everything is already written; nothing written")
    elif check_only:
        report.append("something would be written; nothing written (--check)")
    else:
        report.append("written")
    return report


# --------------------------------------------------------------------------
# Taking it out again
# --------------------------------------------------------------------------

def take_out(path: pathlib.Path, folder: pathlib.Path | None = None) -> None:
    """Put a workbook, and the tree files in `folder` when given, back as they stood before the ruling.

    THE RULING READ BACKWARDS, FOR TESTS. The scripts written before this one are each held to reproduce the
    repository's files from an earlier state (`tools/tests/test_reapply_demonic_tree_rework_workbook.py` and its
    siblings), and the rows this ruling adds to their nodes are not theirs. Those tests compare against a copy with this
    ruling taken out, made by `tools/tests/without_the_aura_reservation.py`. Every value must hold the ruling's own
    new value, so a copy that does not hold the ruling is refused rather than half undone. New rows are DELETED, so
    rows another script appends later land where they would have without this one.
    """
    book = openpyxl.load_workbook(path)

    slots = book["Skill Slots"]
    h = _headers(slots)
    aura = _one_row(slots, h["Slot"], ("Aura",))
    for column, now, then in (("Mana Cost", 0, AURA_MANA_COST_BEFORE), ("Note", NEW_AURA_NOTE, OLD_AURA_NOTE)):
        if slots.cell(aura, h[column]).value != now:
            raise RulingError(f"Skill Slots Aura {column} does not hold the ruling's value; cannot take it out")
        slots.cell(aura, h[column]).value = then
    if "Mana Reserved" not in h:
        raise RulingError("Skill Slots has no Mana Reserved column; cannot take the ruling out")
    slots.delete_cols(h["Mana Reserved"])

    sheet = book["Enchantments"]
    for column, now, then in ((1, NEW_POSITIVE_TEXT, OLD_POSITIVE_TEXT), (6, NEW_NEGATIVE_TEXT, OLD_NEGATIVE_TEXT)):
        sheet.cell(_one_row(sheet, column, (now,)), column).value = then
    for benefit in NEW_BENEFITS:
        row = _one_row(sheet, 1, (benefit.sentence,))
        for column in (1, 2, 3, 4, 5, 11):
            sheet.cell(row, column).value = None

    effects = book["Enchantment Effects"]
    h = _headers(effects)
    for new, cells in ((NEW_POSITIVE, (("Enchantment", OLD_POSITIVE), ("Effect", OLD_POSITIVE_TEXT),
                                       ("Stat", "mana_cost"))),
                       (NEW_NEGATIVE, (("Enchantment", OLD_NEGATIVE), ("Effect", OLD_NEGATIVE_TEXT),
                                       ("Stat", "mana_cost"), ("Value Kind", "increased"), ("Value Low", 100),
                                       ("Value High", 200)))):
        row = _one_row(effects, h["Enchantment"], (new,))
        for column, value in cells:
            effects.cell(row, h[column]).value = value
    gone = [_one_row(effects, h["Enchantment"], (b.key,)) for b in NEW_BENEFITS]
    for row in sorted(gone, reverse=True):
        effects.delete_rows(row)

    passives = book["Passive Effects"]
    h = _headers(passives)
    gone = []
    for each in NODE_ROWS:
        found = [row for row in _rows_where(passives, h["Node"], (each.key,))
                 if passives.cell(row, h["Stat"]).value == each.stat]
        if len(found) != 1:
            raise RulingError(f"Passive Effects: {len(found)} rows on {each.key} state {each.stat}; cannot take out")
        gone.append(found[0])
    for row in sorted(gone, reverse=True):
        passives.delete_rows(row)
    book.save(path)

    if folder is None:
        return
    for each in NODE_ROWS:
        tree = folder / f"{each.tree}_Class_Tree_Final.json"
        text = tree.read_bytes().decode("utf-8")
        tree.write_bytes(_swap_sentence(text, each.node, each.new_sentence, each.old_sentence,
                                        f"{each.key}'s sentence", tree.name).encode("utf-8"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if anything would be written")
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--docs", type=pathlib.Path, default=DOCS)
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, args.docs, check_only=args.check)
    except RulingError as error:
        print(error)
        return 2
    print("\n".join(report))
    return 1 if report[-1].endswith("(--check)") else 0


if __name__ == "__main__":
    sys.exit(main())
