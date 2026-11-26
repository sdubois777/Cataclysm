"""State the Bulwark's Fervour for a kill in its starting node's sentence, by node name.

    python tools/apply_war_bulwark_kill_fervour_sentence_2026_11_26.py docs
    python tools/apply_war_bulwark_kill_fervour_sentence_2026_11_26.py --check docs

THE BULWARK'S KILL FERVOUR (2026-11-26, the final bundle). Slice C (2026-11-24) made the Bulwark's starting node state
the rates it built, and left "killing enemies" in the sentence unbuilt. Every passive row's value must appear in its
node's own words (`tools/tests/test_passive_effects_match_the_node_text.py`), so the kill's 1, written by
`tools/reapply_war_bulwark_kill_fervour_workbook.py`, is stated here:

    before  "Taking hits, blocking and killing enemies generate Fervour: 1 for each hit you take and 1 for each block.
            While you have this, your Fervour decays at 5 per second after 3 seconds out of combat."
    after   "Taking hits, blocking and killing enemies generate Fervour: 1 for each hit you take, 1 for each block and 1
            for each enemy you kill. While you have this, your Fervour decays at 5 per second after 3 seconds out of
            combat."

THIS SCRIPT TAKES THE SENTENCE OVER from `tools/apply_war_fervour_sources_2026_11_24.py`, whose Bulwark reword ends at
`before` above and is no longer listed there.

THE MACHINERY IS `tools/apply_passive_order_fixes_2026_11_24.py`'s, imported: a node is found by name in the tree its
file names, a sentence reading anything other than `before` or `after` is refused, and the file is written in the
serialisation that reproduces it byte for byte. Idempotent. `--check` writes nothing and exits 1 when a file would
change; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402

BEFORE = ("Taking hits, blocking and killing enemies generate Fervour: 1 for each hit you take and 1 for each block. "
          "While you have this, your Fervour decays at 5 per second after 3 seconds out of combat.")
AFTER = ("Taking hits, blocking and killing enemies generate Fervour: 1 for each hit you take, 1 for each block and 1 "
         "for each enemy you kill. While you have this, your Fervour decays at 5 per second after 3 seconds out of "
         "combat.")

FIXES: dict[str, tuple[order.Reword, ...]] = {
    "Bulwark": (order.Reword(node="Fervour", before=BEFORE, after=AFTER),),
}


def plan_file(path: pathlib.Path) -> order.Outcome:
    """What rewording one tree file would write, without writing it."""
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise order.Refused(f"{path.name} is not a JSON tree file: {error}") from error
    tree = order.tree_name(data, path)
    form = order.format_of(raw, data)
    if form is None:
        raise order.Refused(f"{path.name}: no two-space JSON serialisation reproduces the file "
                            f"byte for byte, so writing it would reformat every line")
    changes: list[str] = []
    for fix in FIXES.get(tree, ()):
        changes += order.apply_reword(data, fix, tree)
    if changes:
        order.check_structure(data, tree)
    new_bytes = form.write(data) if changes else None
    if new_bytes == raw:
        new_bytes = None
    return order.Outcome(path, tree, changes, new_bytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+",
                        help="tree JSON files, or directories holding *_Class_Tree_*.json")
    parser.add_argument("--check", action="store_true",
                        help="write nothing; exit 1 if any file would change")
    args = parser.parse_args(argv)
    try:
        outcomes = [plan_file(path) for path in order.tree_files(args.paths)]
    except order.Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2

    pending = 0
    for outcome in outcomes:
        if outcome.tree not in FIXES:
            continue
        if outcome.new_bytes is None:
            print(f"{outcome.path}: the {outcome.tree} tree already states its kill Fervour; left alone")
            continue
        pending += 1
        print(f"{outcome.path}: the {outcome.tree} tree {'would change' if args.check else 'changed'}:")
        for line in outcome.changes:
            print(f"  {line}")
    if args.check:
        print(f"{pending} file(s) would change.")
        return 1 if pending else 0
    for outcome in outcomes:
        if outcome.new_bytes is not None:
            outcome.path.write_bytes(outcome.new_bytes)
    print(f"{pending} file(s) changed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
