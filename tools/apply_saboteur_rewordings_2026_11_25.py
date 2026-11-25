"""Reword the Saboteur node sentences the coordinating session ruled on 2026-11-25 (the C1/C5 plan).

    python tools/apply_saboteur_rewordings_2026_11_25.py docs
    python tools/apply_saboteur_rewordings_2026_11_25.py --check docs

THE RULE THE COORDINATING SESSION SET (2026-11-25): build the smallest mechanism, and reword only a sentence the game
cannot have. Each sentence below names a delay or a speed the game does not have: a trap triggers on the step it finds
an enemy, with nothing between; placing a trap and deploying a gadget take no time. Each was reworded to the nearest
thing the game has, as proposed in the C1/C5 plan and accepted as proposed:

    Hair Wire        "At 4 points: Traps trigger 20% faster after detecting an enemy."
                  -> "At 4 points: Your traps arm 20% faster."
    Volatile Casing  "... reduces the trigger delay of all your other traps ..."
                  -> "... reduces the arming delay of all your other traps ..."
    Minefield        "+10% trap placement speed per point."  -> "+10% trap arming speed per point."
    Field Engineer   "+12% gadget deployment speed per point." -> "+12% gadget attack speed per point."

EACH NODE IS FOUND BY NAME IN THE SABOTEUR TREE, and a sentence reading neither the one the ruling was made against nor
the new one is refused (`tools/apply_passive_order_fixes_2026_11_24.py`'s `Reword`, imported). A file already reworded
is not rewritten, and a file keeps its byte-for-byte serialisation. `--check` writes nothing and exits 1 when a file
would change; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402

TREE = "Saboteur"

#: The rewordings, each against the sentence it was ruled on.
REWORDS: tuple[order.Reword, ...] = (
    order.Reword(
        "Hair Wire",
        "+10% trap trigger radius per point. At 4 points: Traps trigger 20% faster after detecting an enemy.",
        "+10% trap trigger radius per point. At 4 points: Your traps arm 20% faster."),
    order.Reword(
        "Volatile Casing",
        "Each active trap on the battlefield reduces the trigger delay of all your other traps by 0.1 seconds per "
        "point, to a minimum of 0.1 seconds.",
        "Each active trap on the battlefield reduces the arming delay of all your other traps by 0.1 seconds per "
        "point, to a minimum of 0.1 seconds."),
    order.Reword(
        "Minefield",
        "+10% trap placement speed per point. At 6 points: Your traps reserve 5% reduced Fervour.",
        "+10% trap arming speed per point. At 6 points: Your traps reserve 5% reduced Fervour."),
    order.Reword(
        "Field Engineer",
        "+12% gadget deployment speed per point. At 4 points: Deploying a gadget does not interrupt any action you "
        "are currently performing.",
        "+12% gadget attack speed per point. At 4 points: Deploying a gadget does not interrupt any action you are "
        "currently performing."),
)


def plan_file(path: pathlib.Path) -> order.Outcome:
    """What rewording would write to one tree file, without writing it."""
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise order.Refused(f"{path.name} is not a JSON tree file: {error}") from error
    tree = order.tree_name(data, path)
    if tree != TREE:
        return order.Outcome(path, tree, [], None)
    form = order.format_of(raw, data)
    if form is None:
        raise order.Refused(f"{path.name}: no two-space JSON serialisation reproduces the file byte for byte")
    changes = [line for fix in REWORDS for line in order.apply_reword(data, fix, tree)]
    new_bytes = form.write(data) if changes else None
    if new_bytes == raw:
        new_bytes = None
    return order.Outcome(path, tree, changes, new_bytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+", help="tree JSON files, or directories holding *_Class_Tree_*.json")
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if the file would change")
    args = parser.parse_args(argv)
    try:
        outcomes = [plan_file(path) for path in order.tree_files(args.paths)]
    except order.Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2
    pending = [outcome for outcome in outcomes if outcome.new_bytes is not None]
    for outcome in outcomes:
        if outcome.tree != TREE:
            continue
        if outcome.new_bytes is None:
            print(f"{outcome.path}: already reworded; left alone")
            continue
        print(f"{outcome.path}: {'would change' if args.check else 'changed'}:")
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
