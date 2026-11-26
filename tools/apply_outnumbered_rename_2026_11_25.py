"""Rename the Bulwark's basic node Wrath Incarnate to Outnumbered (the War rulings' R29).

    python tools/apply_outnumbered_rename_2026_11_25.py docs
    python tools/apply_outnumbered_rename_2026_11_25.py --check docs

TWO BULWARK ENTRIES WERE NAMED "WRATH INCARNATE": the basic node
`basic_tw_stem_004` ("+2% retaliation damage per point for each unique enemy that
has hit you in the last 10 seconds ...") and the Final Oath's third option. The
War rulings pass (Q20) proposed, as R29, "rename the basic node to
"Outnumbered", which says what it counts, and keep the oath's name". The project
owner accepted every War ruling on 2026-11-24: "I'll go with your
recommendations on the war rulings."

THE NODE IS FOUND BY ITS ID, not its name, because the name is the one that was
doubled; and the change is refused unless the node still carries the old name
or the new one and the sentence the ruling was made against. The sentence is not
changed. A file already renamed is not rewritten, and the file keeps its
byte-for-byte serialisation (`tools/apply_passive_order_fixes_2026_11_24.py`'s
machinery, imported). `--check` writes nothing and exits 1 when a file would
change; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402

TREE = "Bulwark"
NODE_ID = "basic_tw_stem_004"
BEFORE = "Wrath Incarnate"
AFTER = "Outnumbered"
SENTENCE = ("+2% retaliation damage per point for each unique enemy that has hit you in the last 10 seconds. While at "
            "maximum Fervour, your retaliation damage hits all nearby enemies rather than just your attacker.")
#: The Final Oath's option keeps the name; the rename is refused if it has gone.
OATH = "Final Oath"


def apply_rename(data: dict) -> list[str]:
    """Rename the node; refuse a node missing, doubled, or no longer the one the ruling named."""
    found = [node for node in data["nodes"] if node.get("id") == NODE_ID]
    if len(found) != 1:
        raise order.Refused(f"the {TREE} tree has {len(found)} nodes with the id {NODE_ID!r}")
    node = found[0]["data"]
    if node.get("description") != SENTENCE:
        raise order.Refused(f"{NODE_ID} reads {node.get('description')!r}, which is not the sentence R29 was ruled "
                            f"against")
    oath = order.node_named(data, OATH, TREE)
    if BEFORE not in [option.get("name") for option in oath["data"].get("options", [])]:
        raise order.Refused(f"the {OATH}'s options no longer include {BEFORE!r}, so the collision R29 resolves is gone")
    if node.get("name") == AFTER:
        return []
    if node.get("name") != BEFORE:
        raise order.Refused(f"{NODE_ID} is named {node.get('name')!r}, neither {BEFORE!r} nor {AFTER!r}")
    node["name"] = AFTER
    return [f"renamed {NODE_ID} from {BEFORE!r} to {AFTER!r}"]


def plan_file(path: pathlib.Path) -> order.Outcome:
    """What renaming would write to one tree file, without writing it."""
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
    changes = apply_rename(data)
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
            print(f"{outcome.path}: already renamed; left alone")
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
