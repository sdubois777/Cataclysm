"""Reword the War nodes slice D writes rows for, where a ruling changed their words, by node name.

    python tools/apply_war_data_rows_2026_11_24.py docs
    python tools/apply_war_data_rows_2026_11_24.py --check docs

SLICE D OF `plan_death_and_war.md` (2026-11-24) writes rows for the War nodes
that need no code. Eleven of their sentences change first:

  - nine accepted by the project owner in the War trees' rulings pass (Q20) on
    2026-11-24, "I'll go with your recommendations on the war rulings", each
    word for word as ruled: Crushing Presence (R1), Weight of the World (R2),
    Crimson Pact (R15, a Third Oath option), Saturation (R21), Meat Shield
    (R22), Dense Muscle (R23), Unstoppable Force (R24), Walking Fortress (R25)
    and Ironblood (R26);
  - Barbed Skin, the coordinating session's ruling of 2026-11-24 under the
    owner's delegation: a flat 2 a point, "+2% Retaliation per point.", as the
    Masochist's Spine node words the same row;
  - Bloodthirst, by the design document's rule "A threshold or a step, never a
    fraction" (the owner's choice of 2026-11-24): "+0.3% life leech per point"
    becomes a whole 1% for every 3 points, 2% at its 8 points where 0.3 a point
    gave 2.4. The 3 is a judgement: the whole step nearest 1 / 0.3.

THE MACHINERY IS `tools/apply_passive_order_fixes_2026_11_24.py`'s, imported,
with one addition for a capstone option's words, which that script never
reworded: the option is found by its name inside the capstone found by its
name. A sentence reading anything other than the one the change was approved
against or the new one is refused, so a sentence somebody changed since is
never overwritten; a file already reworded is not rewritten; the file keeps its
byte-for-byte serialisation. `--check` writes nothing and exits 1 when a file
would change; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402


@dataclasses.dataclass(frozen=True)
class OptionReword:
    """Replace one capstone option's sentence, the option found by name inside the capstone found by name."""
    capstone: str
    option: str
    before: str
    after: str


W = order.Reword

FIXES: dict[str, tuple] = {
    "Bulwark": (
        W("Barbed Skin", "+2% damage reflected to attackers per point (multiplicative).",
          "+2% Retaliation per point."),
        W("Ironblood", "+50 flat HP Regen per second per point.",
          "+2 flat HP Regen per second per point."),
        W("Crushing Presence", "+100 flat damage per point. Requires 5,000 Maximum HP to activate.",
          "+1 flat damage per point for every 1,000 Maximum HP."),
        W("Weight of the World",
          "+50 flat damage per point for every 5,000 Maximum HP you have. Requires 8,000 Maximum HP to activate.",
          "+1% damage per point for every 5,000 Maximum HP (multiplicative)."),
        W("Meat Shield",
          "+4% Maximum HP per point. Each 1,000 Maximum HP above 5,000 increases your Fervour cap by 5.",
          "+4% Maximum HP per point. Each 1,000 Maximum HP increases your maximum Fervour by 3."),
        W("Dense Muscle",
          "+2% damage reduction per point for every 5,000 Maximum HP above 10,000 (multiplicative).",
          "+1% damage reduction per point for every 5,000 Maximum HP (multiplicative)."),
        W("Unstoppable Force", "+1% damage per point for every 1,000 Maximum HP above 10,000.",
          "+1% damage per point for every 1,000 Maximum HP."),
        W("Walking Fortress",
          "+500 flat Maximum HP per point. At 6 points: For every 10,000 Maximum HP you have, gain 1% of your "
          "Maximum HP as flat damage reduction (multiplicative).",
          "+40 flat Maximum HP per point. At 6 points: +1% damage reduction for every 5,000 Maximum HP "
          "(multiplicative)."),
    ),
    "Berserker": (
        W("Bloodthirst", "+0.3% life leech per point on all melee attacks.",
          "+1% life leech on all melee attacks for every 3 points."),
        OptionReword(
            capstone="Third Oath", option="Crimson Pact",
            before=("Your critical strike multiplier bonus is added to your life leech percentage. Your life leech "
                    "percentage is added to your critical strike multiplier bonus."),
            after="Your critical strike multiplier is increased by 10% for every 1% of life leech you have."),
    ),
    "Saboteur": (
        W("Saturation",
          "Your evasion chance cap is increased by 5% for each active trap on the battlefield. Bonus evasion from "
          "this effect is not subject to the normal 60% cap.",
          "+5% evasion chance for each of your active traps, up to 5 traps."),
    ),
}


def apply_option_reword(data: dict, fix: OptionReword, tree: str) -> list[str]:
    """Reword one capstone option; refuse an option missing, doubled, or reading neither sentence."""
    node = order.node_named(data, fix.capstone, tree)
    options = node.get("data", {}).get("options", [])
    found = [option for option in options if option.get("name") == fix.option]
    if len(found) != 1:
        raise order.Refused(f"the {tree} tree's {fix.capstone!r} has {len(found)} options named {fix.option!r}")
    current = found[0].get("description")
    if current == fix.after:
        return []
    if current != fix.before:
        raise order.Refused(f"the {tree} tree's {fix.capstone!r} option {fix.option!r} reads {current!r}, which is "
                            f"neither the sentence the change was approved against nor the new one")
    found[0]["description"] = fix.after
    return [f"reworded option {fix.option!r} of {fix.capstone!r}"]


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
        if isinstance(fix, OptionReword):
            changes += apply_option_reword(data, fix, tree)
        else:
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
            print(f"{outcome.path}: the {outcome.tree} tree is already reworded; left alone")
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
