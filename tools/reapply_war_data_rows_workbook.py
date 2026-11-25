"""Write slice D's War rows into the design workbook's Passive Effects sheet.

    python tools/reapply_war_data_rows_workbook.py
    python tools/reapply_war_data_rows_workbook.py --check

SLICE D OF `plan_death_and_war.md` (2026-11-24): the rows of the War nodes whose
sentences need no code, each sentence's numbers as the tree file states them
(after `tools/apply_war_data_rows_2026_11_24.py`). Forty-eight rows on thirty nodes, in
two groups:

  - the 23 nodes and options the War survey found writable as rows today
    (`classify.py`'s group (a)), Barbed Skin among them since the coordinating
    session ruled its wording on 2026-11-24;
  - the War rulings (Q20) the owner accepted on 2026-11-24 whose ruled sentence
    is a row and nothing more: R1, R2, R15, R21, R22, R23, R24 and R25 (R26,
    Ironblood, is in the first group).

PLAIN "DAMAGE" IS TWO ROWS, attack damage and spell damage, by the owner's
ruling of 2026-08-25 (`docs/DECISIONS.md`, "Increased damage" on a passive
node). Seven nodes here say it, increased or more: Warbringer, Punishing Blow,
Pain is Power, Weight of the World, Unstoppable Force, On Your Toes and Army of
One. Crushing Presence's "+1 flat damage" is flat attack damage alone, as the
"Flat damage" affix grants and as R1 sized it.

`docs/All_Things_Cataclysm.xlsx` is a binary nobody can merge, so the rows are
written by this script and the script is what a reviewer reads. IT IS
IDEMPOTENT: a node (or capstone option) already holding exactly its rows is
left alone. IT REFUSES RATHER THAN GUESSES: one holding any other rows was
written by somebody else since, and nothing is written. Rows for a node with
none are appended at the end of the sheet. It adds the `Per Point From` column
when the sheet lacks it, because Pain is Power's "At 5 points: This bonus also
applies to retaliation damage" pays per point from its threshold. Every other
row and every other sheet is left as it was, which the script checks by reading
the workbook back from disk. Then it regenerates the CSVs, unless
`--no-generate`. `--check` writes nothing and exits 1 when a row would be
written.

THE SAVE-SAFETY HELPERS ARE `tools/reapply_demonic_tree_rework_workbook.py`'s,
imported: the refusal of a workbook holding formulas, charts or images, the
read-back snapshot, and the cell readers.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_demonic_tree_rework_workbook as helpers  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"
SHEET = helpers.SHEET
PER_POINT_FROM = "Per Point From"
#: THE SECOND CONDITION, added to the workbook by the first War slice whose row states two: the Berserker's riders,
#: 2026-11-25 (`tools/reapply_war_riders_workbook.py`), for "Two-handed weapons only ... against enemies below
#: 25% HP" and the like.
CONDITION_2 = "Condition 2"
CONDITION_VALUE_2 = "Condition Value 2"
#: THE EVENT THAT GRANTS A ROW ITS OWN STACK, AND HOW LONG THE STACK LASTS, added to the workbook by the first War slice
#: whose row is "for N seconds after" an event: the Saboteur's device conditions, 2026-11-25
#: (`tools/reapply_war_saboteur_devices_workbook.py`), for Sprung's "+3% movement speed per point for 3 seconds after one
#: of your traps is triggered". A row scaled by `own_stacks`, as the Enchantment Effects sheet writes one since issue
#: #1833; the passive row port of 2026-11-24 made the Passive Effects sheet read the same columns.
ACTION_EVENT = "Action Event"
STACK_SECONDS = "Stack Seconds"
#: The columns this script adds to the sheet when it lacks one an appended row uses, in the order they are added. The two
#: condition columns go together, so a row's second condition never lands without its value's column.
ADDABLE_COLUMNS = (PER_POINT_FROM, CONDITION_2, CONDITION_VALUE_2, ACTION_EVENT, STACK_SECONDS)
ReapplyError = helpers.ReapplyError

@dataclasses.dataclass(frozen=True)
class Row:
    """One Passive Effects row, less its node and label."""
    stat: str
    kind: str
    value: float
    tags: str | None = None
    cond: str | None = None
    cond_value: float | None = None
    scale: str | None = None
    step: float | None = None
    option: int | None = None
    min_points: int | None = None
    cap: int | None = None
    per_point_from: int | None = None
    cond2: str | None = None
    cond_value2: float | None = None
    reach: float | None = None
    action_event: str | None = None
    stack_seconds: float | None = None

    def key(self) -> tuple:
        def num(value):
            return None if value is None else float(value)
        return (self.stat, self.kind, float(self.value), self.tags or None, self.cond or None,
                num(self.cond_value), self.scale or None, num(self.step), num(self.option),
                num(self.min_points), num(self.cap), num(self.per_point_from), self.cond2 or None,
                num(self.cond_value2) or None, num(self.reach) if self.reach and self.reach > 0 else None,
                self.action_event or None, num(self.stack_seconds) or None)


R = Row
MELEE = "Type.Melee"
TRAP = "Type.Trap"
HALF = {"cond": "class_resource_above", "cond_value": 50}

#: (node, Node Name label, rows). A capstone option's rows carry its option number.
NODES: tuple[tuple[str, str, tuple[Row, ...]], ...] = (
    # ---- Bulwark ----
    ("Bulwark_basic_trunk_002", "Iron Constitution", (
        R("max_health", "increased", 2),
        R("max_health", "flat", 1, scale="armor", step=10, min_points=5))),
    ("Bulwark_basic_trunk_003", "Warbringer", (
        R("attack_damage", "increased", 3),
        R("spell_damage", "increased", 3))),
    ("Bulwark_basic_ic_stem_002", "Iron Wall", (R("block_damage_reduction", "more", 4),)),
    ("Bulwark_basic_ic_t1_002", "Punishing Blow", (
        R("attack_damage", "increased", 3, cond="seconds_after_block", cond_value=3),
        R("spell_damage", "increased", 3, cond="seconds_after_block", cond_value=3))),
    ("Bulwark_basic_jg_stem_002", "Ironblood", (R("health_regen", "flat", 2),)),
    ("Bulwark_basic_tw_stem_001", "Barbed Skin", (R("retaliation", "flat", 2),)),
    ("Bulwark_basic_tw_t2_003", "Pain is Power", (
        R("attack_damage", "increased", 2, scale="health_missing", step=10),
        R("spell_damage", "increased", 2, scale="health_missing", step=10),
        R("retaliation", "increased", 2, scale="health_missing", step=10, min_points=5, per_point_from=1))),
    ("Bulwark_basic_tw_t3_001", "Aura of Pain", (
        R("retaliation", "increased", 5),
        R("retaliation_radius_metres", "flat", 3, min_points=1))),
    ("Bulwark_basic_tw_stem_005", "Endless Spite", (
        R("retaliation", "increased", 1, scale="class_resource_held", step=10),)),
    ("Bulwark_capstone_50", "Second Oath", (
        R("block_chance", "more", 100, cond="not_attacked_for_seconds", cond_value=3, option=1),
        R("health_regen", "more", 100, option=2))),
    ("Bulwark_basic_jg_stem_003", "Crushing Presence", (
        R("attack_damage", "flat", 1, scale="max_health", step=1000),)),
    ("Bulwark_basic_jg_t2_002", "Weight of the World", (
        R("attack_damage", "more", 1, scale="max_health", step=5000),
        R("spell_damage", "more", 1, scale="max_health", step=5000))),
    ("Bulwark_basic_jg_stem_001", "Meat Shield", (
        R("max_health", "increased", 4),
        R("class_resource", "flat", 3, scale="max_health", step=1000))),
    ("Bulwark_basic_jg_t2_001", "Dense Muscle", (
        R("damage_reduction", "more", 1, scale="max_health", step=5000),)),
    ("Bulwark_basic_jg_stem_005", "Unstoppable Force", (
        R("attack_damage", "increased", 1, scale="max_health", step=1000),
        R("spell_damage", "increased", 1, scale="max_health", step=1000))),
    ("Bulwark_basic_jg_final_001", "Walking Fortress", (
        R("max_health", "flat", 40),
        R("damage_reduction", "more", 1, scale="max_health", step=5000, min_points=6))),
    # ---- Berserker ----
    ("Berserker_trunk_002", "Bloodthirst", (
        R("life_leech", "flat", 1, tags=MELEE, min_points=3),
        R("life_leech", "flat", 1, tags=MELEE, min_points=6))),
    ("Berserker_trunk_003", "Killing Edge", (R("crit_chance", "flat", 3, tags=MELEE),)),
    ("Berserker_trunk_005", "Savage Blows", (R("crit_multiplier", "flat", 5, tags=MELEE),)),
    ("Berserker_crit_002", "Precision", (
        R("crit_chance", "flat", 2, tags=MELEE),
        R("crit_chance", "flat", 1, **HALF))),
    ("Berserker_capstone_25", "First Oath", (
        R("damage_reduction", "flat", 1, scale="life_leech", step=1, option=3, **HALF),)),
    ("Berserker_capstone_50", "Second Oath", (
        R("crit_multiplier", "flat", 1, scale="health_missing", step=1, option=3),)),
    ("Berserker_capstone_100", "Third Oath", (
        R("crit_multiplier", "flat", 10, scale="life_leech", step=1, option=3),)),
    # ---- Saboteur ----
    ("Saboteur_basic_trunk_005", "Light Footed", (R("evasion", "increased", 5, cond="while_moving"),)),
    ("Saboteur_basic_trunk_009", "On Your Toes", (
        R("attack_damage", "increased", 2, cond="seconds_after_dodge", cond_value=3),
        R("spell_damage", "increased", 2, cond="seconds_after_dodge", cond_value=3))),
    ("Saboteur_basic_trunk_010", "Prepared", (
        R("attack_damage", "increased", 2, **HALF),
        R("spell_damage", "increased", 2, **HALF))),
    ("Saboteur_basic_trap_ql_001", "Hair Trigger Mechanism", (
        R("trap_damage_to_triggering_enemy", "increased", 10, tags=TRAP),)),
    ("Saboteur_keystone_gadget_deep_002", "Army of One", (
        R("attack_damage", "increased", 15, scale="gadgets_active", step=1),
        R("spell_damage", "increased", 15, scale="gadgets_active", step=1),
        R("evasion", "flat", 8, scale="gadgets_active", step=1),
        R("class_resource", "flat", 15, scale="gadgets_active", step=1))),
    ("Saboteur_basic_gadget_mid_001", "Mobile Armory", (
        R("movement_speed", "increased", 2, scale="gadgets_active", step=1),)),
    ("Saboteur_basic_trap_qty_003", "Saturation", (
        R("evasion", "flat", 5, scale="traps_active", step=1, cap=5),)),
)


def row_from_cells(cells: dict[str, object]) -> Row:
    number, text = helpers._number, helpers._text
    return Row(stat=text(cells.get("Stat")), kind=(text(cells.get("Value Kind")) or "").lower(),
               value=number(cells.get("Value Per Point")) or 0, tags=text(cells.get("Required Tags")),
               cond=text(cells.get("Condition")), cond_value=number(cells.get("Condition Value")),
               scale=text(cells.get("Scale")), step=number(cells.get("Scale Step")),
               option=number(cells.get("Option")) or None, min_points=number(cells.get("Min Points")) or None,
               cap=number(cells.get(helpers.CAP_COLUMN)) or None,
               per_point_from=number(cells.get(PER_POINT_FROM)) or None,
               cond2=text(cells.get(CONDITION_2)), cond_value2=number(cells.get(CONDITION_VALUE_2)),
               reach=number(cells.get("Reach Metres")), action_event=text(cells.get(ACTION_EVENT)),
               stack_seconds=number(cells.get(STACK_SECONDS)))


def cells_from_row(node: str, label: str, row: Row) -> dict[str, object]:
    def out(value):
        if isinstance(value, float) and value.is_integer():
            return int(value)
        return value
    return {"Node": node, "Stat": row.stat, "Value Kind": row.kind, "Value Per Point": out(row.value),
            "Required Tags": row.tags, "Condition": row.cond, "Condition Value": out(row.cond_value),
            "Scale": row.scale, "Scale Step": out(row.step), "Option": out(row.option), "Node Name": label,
            "Reach Metres": out(row.reach), "Min Points": out(row.min_points), helpers.CAP_COLUMN: out(row.cap),
            PER_POINT_FROM: out(row.per_point_from), CONDITION_2: row.cond2,
            CONDITION_VALUE_2: out(row.cond_value2), ACTION_EVENT: row.action_event,
            STACK_SECONDS: out(row.stack_seconds)}


def _multiset(rows) -> collections.Counter:
    return collections.Counter(row.key() for row in rows)


def plan(sheet, nodes=None, label: str = "slice D") -> tuple[list[str], list[str], list[dict[str, object]], list[str]]:
    """The header, the columns of `ADDABLE_COLUMNS` that must be added, the rows to append, and the report.

    `nodes` is a list in the shape of `NODES`, which it defaults to; a later War slice's script passes its own
    (`tools/reapply_war_berserker_states_workbook.py`), and `label` names that slice in a refusal."""
    nodes = NODES if nodes is None else nodes
    header = [helpers._text(cell.value) for cell in sheet[1]]
    while header and header[-1] is None:
        header.pop()
    missing = [column for column in helpers.COLUMNS if column not in header]
    if missing:
        raise ReapplyError(f"{SHEET!r} has no column(s) {missing}; its header is {header}")

    held: dict[tuple[str, int], list[Row]] = {}
    for values in sheet.iter_rows(min_row=2, max_col=len(header), values_only=True):
        cells = dict(zip(header, values, strict=False))
        node = helpers._text(cells.get("Node"))
        if node:
            row = row_from_cells(cells)
            held.setdefault((node, int(row.option or 0)), []).append(row)

    append: list[dict[str, object]] = []
    report: list[str] = []
    for node, name_label, wanted in nodes:
        groups: dict[int, list[Row]] = {}
        for row in wanted:
            groups.setdefault(int(row.option or 0), []).append(row)
        for option, rows in groups.items():
            where = node + (f" option {option}" if option else "")
            current = held.get((node, option), [])
            if _multiset(current) == _multiset(rows):
                report.append(f"{where}: already holds its {len(rows)} row(s); left alone")
                continue
            if current:
                raise ReapplyError(f"{where} holds {[row.key() for row in current]}, which are not the rows {label} "
                                   f"writes; somebody has written it since. Nothing was written")
            for row in rows:
                append.append(cells_from_row(node, name_label, row))
                report.append(f"{where}: + {row.stat} {row.kind} {row.value:g}")
    used = {column for column in ADDABLE_COLUMNS if any(cells.get(column) is not None for cells in append)}
    if CONDITION_2 in used:
        used.add(CONDITION_VALUE_2)
    add_columns = [column for column in ADDABLE_COLUMNS if column in used and column not in header]
    return header, add_columns, append, report


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False, nodes=None, label: str = "slice D") -> list[str]:
    """Append the rows the workbook at `path` lacks; return the report. `nodes` and `label` as `plan` takes them."""
    book = openpyxl.load_workbook(path)
    helpers.refuse_what_a_save_would_lose(book)
    sheet = book[SHEET]
    header, add_columns, append, report = plan(sheet, nodes, label)
    if not append:
        return report + ["every row is already written; nothing written"]
    for column in add_columns:
        report.append(f"added the column {column!r} to {SHEET!r}")
    if check_only:
        return report + [f"{len(append)} row(s) to write; nothing written (--check)"]

    others = {s.title: helpers._snapshot(s) for s in book.worksheets if s.title != SHEET}
    before = helpers._snapshot(sheet)
    width_before = len(header)
    for column in add_columns:
        header = header + [column]
        sheet.cell(1, len(header)).value = column
    # THE LAST ROW HOLDING ANYTHING, so a blank row inside the sheet is not mistaken for its end.
    last = max((cell.row for row in sheet.iter_rows(min_row=2, max_col=len(header)) for cell in row
                if cell.value is not None), default=1)
    for offset, cells in enumerate(append, start=1):
        for column, name in enumerate(header, start=1):
            sheet.cell(last + offset, column).value = cells.get(name)
    book.save(path)

    after = openpyxl.load_workbook(path)
    for title, cells in others.items():
        if helpers._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and only {SHEET!r} was written")
    kept = {where: value for where, value in helpers._snapshot(after[SHEET]).items()
            if where[0] <= last and not (where[0] == 1 and where[1] > width_before)}
    if kept != before:
        raise ReapplyError(f"a row above the appended ones changed on saving in {SHEET!r}")
    _, _, still_missing, _ = plan(after[SHEET], nodes, label)
    if still_missing:
        raise ReapplyError(f"{SHEET!r} did not read back with the rows written")
    return report + [f"wrote {len(append)} row(s) after sheet row {last}; no other row or sheet changed"]


def main(argv: list[str] | None = None, nodes=None, label: str = "slice D", description: str | None = None) -> int:
    """The command line. A later War slice's script calls this with its own `nodes`, `label` and description."""
    parser = argparse.ArgumentParser(description=description or __doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true",
                        help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check, nodes=nodes, label=label)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if any(line.endswith("(--check)") for line in report) else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)],
                            cwd=REPO_ROOT)
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
