"""Reword Vengeful Discharge and Crimson Tide, by node name, before their rows are written.

    python tools/apply_war_riposte_discharge_tide_2026_11_25.py docs
    python tools/apply_war_riposte_discharge_tide_2026_11_25.py --check docs

TWO SENTENCES CHANGE (2026-11-25):

  - Vengeful Discharge (`Bulwark_basic_tw_resolve_001`) takes the War rulings' R7 word for word, which the project owner
    accepted with every War ruling on 2026-11-24 ("I'll go with your recommendations on the war rulings"): a pulse with
    a radius rather than "all nearby enemies", and Bleed rather than "a bleed stack".
  - Crimson Tide (`Berserker_leech_001`) keeps its figures and loses its fractions, by the design document's rule "A
    threshold or a step, never a fraction" (the owner's choice of 2026-11-24): "+0.5% life leech per point" becomes
    "+1% life leech ... for every 2 points" and "0.5% per point of your maximum HP" becomes "1% of your maximum HP for
    every 2 points". The same figures at every even point and at its 8, as Bloodthirst's step was.

Riposte's sentence is unchanged.

THE MACHINERY IS `tools/apply_passive_order_fixes_2026_11_24.py`'s, imported: a sentence reading anything other than the
one the change was approved against or the new one is refused, a file already reworded is not rewritten, and the file
keeps its byte-for-byte serialisation. `--check` writes nothing and exits 1 when a file would change; exit 2 is a
refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402

W = order.Reword

FIXES: dict[str, tuple[order.Reword, ...]] = {
    "Bulwark": (
        W("Vengeful Discharge",
          "Passively drains 10 Fervour per second to increase your retaliation damage by +3% per point. When this drain "
          "reduces you to 0 Fervour, automatically trigger a retaliation pulse hitting all nearby enemies for 100% of "
          "your retaliation damage. At 5 points: This pulse also applies a bleed stack to all enemies hit.",
          "Passively drains 10 Fervour per second to increase your retaliation damage by +3% per point. When this drain "
          "reduces you to 0 Fervour, you retaliate against every enemy within 4 metres for 100% of your retaliation "
          "damage. At 5 points: this also applies Bleed to every enemy it strikes."),
    ),
    "Berserker": (
        W("Crimson Tide",
          "+0.5% life leech per point on melee attacks. At 5 points: Life leech is applied instantly rather than over "
          "time. Every 10 Fervour you spend or drain generates a heal equal to 0.5% per point of your maximum HP.",
          "+1% life leech on melee attacks for every 2 points. At 5 points: Life leech is applied instantly rather than "
          "over time. Every 10 Fervour you spend or drain generates a heal equal to 1% of your maximum HP for every 2 "
          "points."),
    ),
}


def plan_file(path: pathlib.Path) -> order.Outcome:
    """What rewording one tree file would write, without writing it."""
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise order.Refused(f"{path.name} is not a JSON tree file: {error}") from error
    tree = order.tree_name(data, path)
    if tree not in FIXES:
        return order.Outcome(path, tree, [], None)
    form = order.format_of(raw, data)
    if form is None:
        raise order.Refused(f"{path.name}: no two-space JSON serialisation reproduces the file byte for byte")
    changes: list[str] = []
    for fix in FIXES[tree]:
        changes += order.apply_reword(data, fix, tree)
    if changes:
        order.check_structure(data, tree)
    new_bytes = form.write(data) if changes else None
    if new_bytes == raw:
        new_bytes = None
    return order.Outcome(path, tree, changes, new_bytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+", help="tree JSON files, or directories holding *_Class_Tree_*.json")
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if any file would change")
    args = parser.parse_args(argv)
    try:
        outcomes = [plan_file(path) for path in order.tree_files(args.paths)]
    except order.Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2
    pending = [outcome for outcome in outcomes if outcome.new_bytes is not None]
    for outcome in outcomes:
        if outcome.tree not in FIXES:
            continue
        if outcome.new_bytes is None:
            print(f"{outcome.path}: the {outcome.tree} tree is already reworded; left alone")
            continue
        print(f"{outcome.path}: the {outcome.tree} tree {'would change' if args.check else 'changed'}:")
        for line in outcome.changes:
            print(f"  {line}")
    if args.check:
        print(f"{len(pending)} file(s) would change.")
        return 1 if pending else 0
    for outcome in pending:
        outcome.path.write_bytes(outcome.new_bytes)
    print(f"{len(pending)} file(s) changed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
