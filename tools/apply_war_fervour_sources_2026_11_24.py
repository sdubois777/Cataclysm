"""State the War starting nodes' Fervour rates in their sentences, by node name.

    python tools/apply_war_fervour_sources_2026_11_24.py docs
    python tools/apply_war_fervour_sources_2026_11_24.py --check docs

THE BULWARK'S SENTENCE IS NO LONGER WRITTEN HERE: the final bundle's slice of 2026-11-26 took it over to state its
Fervour for a kill (`tools/apply_war_bulwark_kill_fervour_sentence_2026_11_26.py`). What follows is slice C's account.

Slice C of `plan_death_and_war.md` (2026-11-24) gives the three War classes a way
to fill Fervour. Two of their starting nodes named the sources and stated no
number, and every passive row's value must appear in its node's own words
(`tools/tests/test_passive_effects_match_the_node_text.py`), so the two sentences
now state the rates their rows grant:

    Bulwark   "Taking hits, blocking and killing enemies generate Fervour. While
              you have this, your Fervour decays slowly out of combat." states 1
              for each hit taken and 1 for each block, and the decay the owner
              sized in Q21 on 2026-11-24: 5 a second after 3 seconds out of
              combat. Killing enemies stays in the sentence and is NOT BUILT by
              slice C (see `docs/DECISIONS.md`).
    Saboteur  "Placing a trap or a gadget generates Fervour, and so does one of
              them dealing damage or triggering." states 1 for each of the four.

The Berserker's starting node already states its numbers ("Gain 1 Fervour per
critical strike ... decays at 10 per second after 3 seconds out of combat") and
is not changed. Every rate of 1 is a judgement, the rate the Ravager's and the
Masochist's starting nodes grant per event.

THE MACHINERY IS `tools/apply_passive_order_fixes_2026_11_24.py`'s, imported: a
node is found by name in the tree its file names, a sentence reading anything
other than the one approved against or the new one is refused, and the file is
written in the serialisation that reproduces it byte for byte. Idempotent: a
file already reading the new sentences is not rewritten. `--check` writes nothing
and exits 1 when a file would change; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402

FIXES: dict[str, tuple[order.Reword, ...]] = {
    "Saboteur": (
        order.Reword(
            node="Fervour",
            before=("Placing a trap or a gadget generates Fervour, and so does one of them dealing "
                    "damage or triggering. It adds no rule about emptying, so your Fervour does "
                    "not decay on its own."),
            after=("Placing a trap or a gadget generates Fervour, and so does one of them dealing "
                   "damage or triggering: 1 for each trap placed, 1 for each gadget placed, 1 each "
                   "time a trap triggers and 1 for each blow one of them lands. It adds no rule "
                   "about emptying, so your Fervour does not decay on its own."),
        ),
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
            print(f"{outcome.path}: the {outcome.tree} tree already states its rates; left alone")
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
