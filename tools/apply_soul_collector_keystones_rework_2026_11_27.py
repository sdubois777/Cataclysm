"""Apply the Soul Collector keystone rework of 2026-11-27 to the Soul Collector tree file.

    python tools/apply_soul_collector_keystones_rework_2026_11_27.py
    python tools/apply_soul_collector_keystones_rework_2026_11_27.py --check
    python tools/apply_soul_collector_keystones_rework_2026_11_27.py <tree file> [--check]

ISSUE #2951. The project owner, 2026-11-27, after play-testing the Soul Collector: "the soul collectors keystones
pretty much all have something to do with retaining/gaining fervour but you don't need any of them which makes the
tree pretty lame. Should probably rework most of those to be something like buffs based on fervour or something more
in line with the class theme." The owner then chose, on the same day, to replace all eight such keystones, to rework
the small nodes about gaining or keeping Fervour in the same change, and that kills by the player's minions, gadgets
and traps count as the player's kills tree-wide. The small nodes' sentences are the coordinating session's ruling
within that approval. `docs/DECISIONS.md` has the whole record.

This script writes the node half: names and sentences in the tree file. The numbers behind each sentence are rows in
the design workbook's Passive Effects sheet, which `tools/reapply_soul_collector_keystones_rework_workbook.py` writes.

IT USES THE DEMONIC REWORK'S MACHINERY (`tools/apply_demonic_tree_rework_2026_11_24.py`): each change finds its node
by name, refuses a sentence that is neither the one approved against nor the new one, refuses a new name another node
already has, and the file keeps its format byte for byte. NOTHING HERE CHANGES THE GRAPH: no identifier, kind,
position, maximum or edge moves, so a saved allocation, which stores node identifiers, loads as it is. It is
idempotent and writes nothing when it refuses.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_demonic_tree_rework_2026_11_24 as rework  # noqa: E402
import apply_passive_order_fixes_2026_11_24 as order_fixes  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
TREE_FILE = REPO_ROOT / "docs" / "Soul_Collector_Class_Tree_Final.json"
TREE = "Soul Collector"

Refused = order_fixes.Refused
Rework = rework.Rework

#: The starting node's sentence before and after: answer 5 adds its last sentence.
STARTING_BEFORE = (
    "Kills generate Fervour, taken from the dead: 1 for each enemy you kill, 3 for an Elite, 5 for a Legendary, 8 for "
    "a Herald and 15 for a Boss. You deal 1% more damage for every 2 Fervour you hold. While you have this, Fervour "
    "decays at 5 per second once 4 seconds have passed since your last kill, until you kill again, so a pause between "
    "kills is what empties it. Killing an Elite or stronger creature holds off the decay for 8 seconds rather than 4.")
STARTING_AFTER = STARTING_BEFORE + " Enemies your minions, gadgets and traps kill count as your kills."

#: Every change, in tree order: the spine, then limbs a, b and c.
REWORKS: tuple[Rework, ...] = (
    Rework("Fervour", STARTING_BEFORE, STARTING_AFTER),
    # THE EIGHT KEYSTONES.
    Rework("Death's Toll",
           "Enemies your minions, gadgets and traps kill give you their Fervour, and count as your kills for "
           "everything this tree asks.",
           "Each enemy that dies within 6 metres of you, whoever kills it, gives you a stack of Gathered Souls. Each "
           "stack gives 2% increased Attack Speed and 2% increased Movement Speed, up to 10 stacks. Gathered Souls "
           "lasts 6 seconds, restarted by each new stack.",
           name="Gathered Souls"),
    Rework("Reclaimed",
           "A kill made after your Fervour has begun to decay gives back the Fervour that decay took from you in the "
           "3 seconds before it.",
           "Every 5 Fervour you lose to decay strikes the strongest enemy within 10 metres for 40% of your weapon "
           "damage.",
           name="Unquiet Souls"),
    Rework("Soulbound",
           "Your Fervour no longer decays with time. Instead you lose 1 Fervour for every 2% of your maximum health "
           "that you lose to damage.",
           "The Fervour you hold no longer adds to your damage. Instead you take 1% less damage for every 4 Fervour "
           "you hold, up to 25%.",
           name="Death Made Flesh"),
    Rework("Hoarder",
           "You lose 50% less Fervour to decay.",
           "You deal 0.5% more damage for each Fervour you hold above 50.",
           name="Heavy Urn"),
    Rework("Long Shadow",
           "Each Fervour threshold you reach stays in effect for 5 seconds after you fall below it.",
           "Each time your Fervour rises to a threshold you have opened, you release a blast dealing 100% of your "
           "weapon damage to every enemy within 5 metres. Each threshold can do this once every 5 seconds.",
           name="Tolling Count"),
    Rework("Hungry for the Strong",
           "Elite and stronger creatures give 100% more Fervour.",
           "Killing an Elite or stronger creature gives you 20% more damage for 10 seconds.",
           name="Trophy of the Strong"),
    Rework("Kindred Fires",
           "Fervour you gain from any source, not only a kill, holds off decay as a kill does: the time before your "
           "Fervour begins to decay starts again.",
           "While your current chain of kills has 5 or more kills, you deal 15% more damage and take 10% less damage.",
           name="Reaper's Rhythm"),
    Rework("Living Harvest",
           "Each enemy in your Siphon gives you 1 Fervour every 2 seconds, up to 4 Fervour a second. This Fervour "
           "does not come from kills, so on its own it does not hold off decay.",
           "Your Siphon deals damage over time to every enemy in it: 0.5% of your weapon damage each second for "
           "every 5 Fervour you hold, as Death damage.",
           name="Hungering Siphon"),
    # THE ELEVEN SMALL NODES.
    Rework("Tithe of the Dead",
           "+2% increased Fervour gained from kills per point.",
           "+1% increased damage per point against enemies within 6 metres of you."),
    Rework("Grave Patience",
           "Your Fervour begins to decay 1 second later after a kill for every 5 points.",
           "Every 5 Fervour you lose to decay restores 0.2% of your maximum health per point."),
    Rework("Claimed",
           "An enemy you hit gives you its Fervour if it dies within 1 second for every 2 points of that hit, "
           "whatever kills it.",
           "+1% increased damage per point for 4 seconds after you kill an enemy."),
    Rework("Miser's Grip",
           "Your Fervour never decays below 3% of your maximum Fervour per point.",
           "+2% increased damage per point while you hold 50 or more Fervour."),
    Rework("Cold Storage",
           "Your Fervour decays 5% slower per point.",
           "You take 1% reduced damage per point while you hold 50 or more Fervour."),
    Rework("Gleaning",
           "Each enemy you kill has a 5% chance per point to give 1 extra Fervour.",
           "Each kill has a 5% chance per point to strike the strongest enemy within 10 metres for 50% of your "
           "weapon damage."),
    Rework("Thinning the Herd",
           "Each kill gives +1% increased Fervour per point for every other enemy within 8 metres of you, up to 10 "
           "enemies.",
           "+1% increased damage per point for every 2 enemies within 8 metres of you, up to 10 enemies."),
    Rework("Tear Loose",
           "Your hits on an Elite or stronger creature have a 2% chance per point to give you 1 Fervour, torn from "
           "it.",
           "Killing an Elite or stronger creature restores 1% of your maximum health per point."),
    Rework("Vigil Over the Strong",
           "While an Elite or stronger creature is within 15 metres of you, your Fervour decays 5% slower per point.",
           "+1% increased Attack Speed per point while an Elite or stronger creature is within 15 metres of you."),
    Rework("Any Fuel",
           "+2% increased Fervour gained from sources other than kills per point, so another class's way of filling "
           "the bar fills it faster.",
           "+0.5% increased Attack Speed per point for each kill in your current chain of kills, up to 10 kills.",
           name="Gathering Pace"),
    Rework("Borrowed Time",
           "Each time you gain Fervour from a source other than a kill, the time before your Fervour begins to decay "
           "is extended by 1 second for every 3 points, never past its full length.",
           "You take 0.5% reduced damage per point for every 2 kills in your current chain, up to 10 kills."),
)


def apply_to_tree(data: dict) -> list[str]:
    """Apply every change to the Soul Collector tree `data` in place; return what changed."""
    tree = order_fixes.tree_name(data)
    if tree != TREE:
        raise Refused(f"this is the {tree} tree; the rework is the {TREE}'s")
    before = rework.graph_of(data)
    changes: list[str] = []
    for fix in REWORKS:
        changes += rework.apply_rework(data, fix, tree)
    if changes:
        if rework.graph_of(data) != before:
            raise Refused(f"{tree}: the rework changed an identifier, a kind, a position, a maximum or an edge, and "
                          f"it was approved as changing none")
        order_fixes.check_structure(data, tree)
    return changes


def plan(path: pathlib.Path) -> tuple[list[str], bytes | None]:
    """What would change in the file at `path`, and its new bytes, or None when nothing would."""
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise Refused(f"{path.name} is not a JSON tree file: {error}") from error
    form = order_fixes.format_of(raw, data)
    if form is None:
        raise Refused(f"{path.name}: no two-space JSON serialisation reproduces the file byte for byte, so writing "
                      f"it would reformat every line")
    changes = apply_to_tree(data)
    new_bytes = form.write(data) if changes else None
    return changes, (None if new_bytes == raw else new_bytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path", nargs="?", type=pathlib.Path, default=TREE_FILE)
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if the file would change")
    args = parser.parse_args(argv)
    try:
        changes, new_bytes = plan(args.path)
    except Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2
    if new_bytes is None:
        print(f"{args.path}: the {TREE} tree is already reworked; left alone")
        return 0
    for line in changes:
        print(f"  {line}")
    if args.check:
        print(f"{args.path}: {len(changes)} change(s) to make; nothing written (--check)")
        return 1
    args.path.write_bytes(new_bytes)
    print(f"{args.path}: {len(changes)} change(s) written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
