"""Write the ruled sentence of the Bulwark's keystone Titanfall (the War rulings' R6) into its tree file.

    python tools/apply_titanfall_sentence_2026_11_25.py docs
    python tools/apply_titanfall_sentence_2026_11_25.py --check docs

TITANFALL'S SENTENCE STATED AN ABSOLUTE HEALTH TOTAL ON THE OLD SCALE ("a single hit exceeding 5,000 flat damage ...
Requires 25,000 Maximum HP to activate"). The War rulings pass (Q20) proposed, as R6, "When a single hit takes more
than 20% of your Maximum HP, you cannot be killed for 2 seconds and your next attack deals bonus damage equal to that
hit. Can only happen once every 30 seconds." The project owner accepted every War ruling on 2026-11-24: "I'll go with
your recommendations on the war rulings." The coordinating session ruled on 2026-11-25 (T5) that it is written now,
with the keystone built.

THE NODE IS FOUND BY ITS ID, and the change is refused unless the node carries the old sentence or the new one and is
still called Titanfall. A file already written is not rewritten, and the file keeps its byte-for-byte serialisation
(`tools/apply_passive_order_fixes_2026_11_24.py`'s machinery, imported). `--check` writes nothing and exits 1 when a
file would change; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402

TREE = "Bulwark"
NODE_ID = "keystone_jg_final"
NAME = "Titanfall"
BEFORE = ("When you absorb a single hit exceeding 5,000 flat damage, you become unkillable for 2 seconds and your next "
          "attack deals damage equal to the full amount of that hit. Requires 25,000 Maximum HP to activate. Can only "
          "trigger once every 30 seconds.")
AFTER = ("When a single hit takes more than 20% of your Maximum HP, you cannot be killed for 2 seconds and your next "
         "attack deals bonus damage equal to that hit. Can only happen once every 30 seconds.")


def apply_sentence(data: dict) -> list[str]:
    """Write the sentence; refuse a node missing, doubled, renamed, or carrying a sentence neither ruled one."""
    found = [node for node in data["nodes"] if node.get("id") == NODE_ID]
    if len(found) != 1:
        raise order.Refused(f"the {TREE} tree has {len(found)} nodes with the id {NODE_ID!r}")
    node = found[0]["data"]
    if node.get("name") != NAME:
        raise order.Refused(f"{NODE_ID} is named {node.get('name')!r}, not {NAME!r}")
    if node.get("description") == AFTER:
        return []
    if node.get("description") != BEFORE:
        raise order.Refused(f"{NODE_ID} reads {node.get('description')!r}, neither the sentence R6 was ruled against "
                            f"nor R6's own")
    node["description"] = AFTER
    return [f"wrote R6's sentence into {NODE_ID}"]


def plan_file(path: pathlib.Path) -> order.Outcome:
    """What writing the sentence would write to one tree file, without writing it."""
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
    changes = apply_sentence(data)
    if changes:
        order.check_structure(data, tree)
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
            print(f"{outcome.path}: already written; left alone")
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
