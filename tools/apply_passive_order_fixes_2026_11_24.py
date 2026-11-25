"""Apply the passive tree order fixes the project owner approved on 2026-11-24.

    python tools/apply_passive_order_fixes_2026_11_24.py <tree file or directory> ...
    python tools/apply_passive_order_fixes_2026_11_24.py --check <tree file or directory> ...

An audit of the class passive trees found nodes whose order or wording works
against what they do, and the project owner approved every fix it proposed
("Apply all (Recommended)", 2026-11-24):

    Bulwark         Serrated Edge is reworded from "+10% bleed duration per point
                    applied by your retaliation damage" to "+10% duration of
                    bleeds you apply, per point". Its maximum points stay.
    Masochist       Blood Price and Deeper Cuts swap, so the health cost comes
                    before the nodes that raise the Fervour gained from health
                    spent; and Grand Tithe's threshold drops from 10% of maximum
                    health to 2%, a figure the limb reaches (see GRAND_TITHE).
    Soul Collector  Spent Breath and Soul Fuel swap, so the first node that spends
                    Fervour comes before the node that pays out for spending it.
    Necromancer     Gathered Remains keeps only its "remains last longer" half.
                    The Abomination half moves to a node of its own, Many Made
                    One, that cannot be reached before 40 points are spent (see
                    MANY_MADE_ONE). Gravefinder moves to directly after Bone Fuse.
    Shadow          Death's Patience and Envenom swap, so the node that applies
                    Poison and Bleed comes before the node that pays out for kills
                    on enemies carrying them.

AND ONE ADDITION THE COORDINATING SESSION ASKED FOR THE SAME DAY, found by the
Saboteur vows drafter:

    Saboteur        Six nodes still state count caps that the owner's ruling of
                    2026-11-20 removed ("We shouldn't be putting arbitrary limits
                    on any of those things that reserve the class resource"):
                    Bag of Tricks, Minefield, Overwhelming Presence, Precision
                    Placement, Calculated Chaos and Mechanical Familiar. Each is
                    reworded to keep its intent without a maximum count (see
                    SABOTEUR_CAPS). Precision Placement keeps its one-trap rule,
                    which is a conversion and not a cap. And the four oaths gain
                    the `isDecision`, `threshold` and `selectedOption` fields the
                    other trees' capstones carry, with their options left empty
                    for the drafted ones to fill.

EVERY FIX FINDS ITS NODES BY NAME, NEVER BY IDENTIFIER OR POSITION, so it applies
to any copy of a tree: the files in `docs/`, the drafts of the three Death trees,
and a "sigil" layout that keeps a tree's identifiers, contents and edges but
moves every node. Which fixes apply is decided by the tree's own name, read from
`metadata.name` ("Masochist Class Tree") and checked against the file name, so a
node name shared between trees -- Blood Price is in three of them -- is only ever
looked for in the tree the fix is about.

A SWAP EXCHANGES CONTENT AND LEAVES THE GRAPH ALONE. The two nodes trade their
`data` -- name, description, maximum points and every other field -- while each
keeps its identifier, position and edges, so a layout made by node identifier
stays valid. ONE FIELD STAYS WITH THE POSITION: `color`. It is how the authoring
tool paints a limb on its canvas (the whole Masochist Blood Tithe limb is one
colour and the spine another), the generator leaves it out of the game for that
reason, and moving it would paint a spine node in a limb's colour. That is a
judgement, stated in the report this script was written for.

IT REFUSES RATHER THAN GUESSES, and writes nothing when it refuses:
  - a named node is missing, or two nodes share the name;
  - a sentence it rewrites is neither the one the fix was approved against nor
    the fixed one, so somebody has changed it since;
  - two nodes to be ordered are on no common path, so "before" means nothing;
  - the node directly after Bone Fuse on the way to Gravefinder is not the one
    the fix was approved against;
  - the new Necromancer node's place on the canvas is taken (pass
    `--new-node-position X,Y` to choose one);
  - a Saboteur oath already carries a decision field with another value, or a
    threshold that is not its tier's;
  - after the fixes, an edge asks for more points than its source holds, or a
    keystone's edge no longer asks for its parent in full.
With several files, every file is checked before any is written, so a refusal
in one leaves all of them as they were.

IT IS IDEMPOTENT. Each fix first asks whether the tree is already in the fixed
state -- the first node already upstream of the second, the sentence already the
fixed one, the new node already there -- and does nothing if so. A file with
nothing to change is not rewritten at all.

THE FILE KEEPS ITS FORMAT. The trees are written by the authoring tool at
C:\\Projects\\PassiveTreeCreator as two-space-indented JSON; the files in `docs/`
end their lines with CR LF and the sigil layouts with LF. Before writing, the
script finds the serialisation that reproduces the file byte for byte and uses
it, so the change is only the strings it touched. A file no serialisation
reproduces is refused unless `--allow-reformat` is passed.

`--check` writes nothing and exits 1 when any file would change, 0 when every
file is already fixed. Exit 2 is a refusal. `--only <Tree>`, repeatable, limits
the run to the named trees' fixes, so one part can land without the others.
"""

from __future__ import annotations

import argparse
import collections
import copy
import dataclasses
import json
import math
import pathlib
import sys

#: Data fields a swap leaves with the position rather than moving with the
#: content. See the module docstring.
STAYS_WITH_THE_POSITION = ("color",)


class Refused(Exception):
    """A fix cannot be applied to this tree as it stands."""


# --------------------------------------------------------------------------
# The fixes, as data
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Reword:
    """Replace one node's sentence.

    `before` is the sentence the fix was approved against. A node reading
    anything other than `before` or `after` is refused, so a sentence somebody
    has changed since is never overwritten silently.
    """
    node: str
    before: str
    after: str


@dataclasses.dataclass(frozen=True)
class Order:
    """Make `first` come before `then` on their path, by swapping their content.

    Nothing to do when `first` is already upstream of `then`. Refused when
    neither is upstream of the other.
    """
    first: str
    then: str


@dataclasses.dataclass(frozen=True)
class DirectlyAfter:
    """Make `node` a direct child of `after`, by swapping it with `partner`.

    `partner` is the node currently directly after `after` on the way to
    `node`, named so that a layout where something else sits there is refused
    rather than having an unexpected node swapped.
    """
    node: str
    after: str
    partner: str


@dataclasses.dataclass(frozen=True)
class NewNode:
    """Add a basic node, joined by one edge to an existing parent.

    `offset` is where it goes relative to its parent on the canvas, used when no
    `--new-node-position` is given. `least_points` is the fewest points that
    must be spent in the tree before it can take its first point; the fix is
    refused if the edge would make it cheaper than that.
    """
    node_id: str
    edge_id: str
    name: str
    description: str
    max_points: int
    parent: str
    required_points: int
    offset: tuple[float, float]
    least_points: int


@dataclasses.dataclass(frozen=True)
class CapstoneFields:
    """Give each named capstone the decision fields the other trees' carry.

    `isDecision: true`, `threshold` and `selectedOption: null`, the three the
    Berserker, Bulwark, Masochist, Ravager and Ritualist capstones have and the
    Saboteur's lack. The threshold is the tier's, checked against the
    `pointThreshold` the node already carries; a disagreement is refused, as is
    a field already present with another value. Options are left as they are.
    """
    tiers: tuple[tuple[str, int], ...]


#: Grand Tithe, the Masochist's Blood Tithe limb, `basic_bt_b2` in docs/.
#:
#: THE THRESHOLD IS A JUDGEMENT, made under the owner's approval to lower it "to
#: a figure the limb reaches". The limb's own health cost is Deeper Cuts, 0.25%
#: of maximum health per point, at most 2.5% at ten points. Above 2%, which is
#: what nine or ten points pay, so the node rewards nearly full investment in the
#: limb's own cost rather than switching on at the four points every Grand Tithe
#: holder already has on the way (the edge out of Deeper Cuts asks for four).
#: Blood Pyre, the Fist's special and the one skill with a cost of its own (8% of
#: current health), clears it on its own above 25% health, and Exsanguinate, the
#: keystone behind Grand Tithe (15% of current health), above about 13%. The
#: comparison is strictly above, so eight points of Deeper Cuts and nothing else
#: (exactly 2.0%) sit on the boundary, where floating point decides it.
GRAND_TITHE = Reword(
    node="Grand Tithe",
    before=("A skill whose health cost is above 10% of your maximum health deals 4% "
            "increased damage per point."),
    after=("A skill whose health cost is above 2% of your maximum health deals 4% "
           "increased damage per point."),
)

#: The Necromancer's Abomination half, moved off Gathered Remains.
#:
#: A NODE OF ITS OWN RATHER THAN A RIDER ON A LATE NODE, a judgement. The owner
#: allowed either. No ordinary node in the draft is 40 points deep (the deepest,
#: Called to Heel, opens at 38), so the only late nodes are four keystones, and
#: putting the Abomination on one would give that keystone two unrelated halves,
#: which is the fault being fixed on Gathered Remains.
#:
#: ON THE LEGION'S "ONE BIG MINION" BRANCH, after First Among the Dead, whose
#: own sentence already makes the Abomination its target when you have one. The
#: edge asks for First Among the Dead in full, ten points, so the node opens at
#: 40 points spent. Its words are Gathered Remains' first sentence, unchanged.
#: Its place is one column out and one row up from its parent, the step the
#: draft's limbs use from a stem to its first branch.
MANY_MADE_ONE = NewNode(
    node_id="basic_lg_d0",
    edge_id="e_lg_a1_lg_d0",
    name="Many Made One",
    description=("Your Abomination rises sooner: each point takes 2 off the percentage "
                 "of a floor's starting creatures you must kill, from 50% down to 34% "
                 "at 8 points."),
    max_points=8,
    parent="First Among the Dead",
    required_points=10,
    offset=(-150.0, -132.0),
    least_points=40,
)

#: The Saboteur's six count caps, reworded. Every figure is a judgement.
#:
#: THE RULE THEY FOLLOW is the owner's second ruling of 2026-11-20, on the nodes
#: and enchantments that added "+N to max minions" or "+N to max traps": "Lower
#: the reserve instead". Each becomes "<that kind> reserves less", sized by the
#: formula docs/DECISIONS.md records for that entry, reduction = N / (C + N), so
#: a player holds about N more, with C what a typical pool of 100 holds: 20 traps
#: at 5 each, about 6.7 gadgets at 15 each. The enchantments rewritten that day
#: are the precedent: "+1-6 max trap count" became "Your traps reserve 5%-25%
#: reduced Fervour" and "+1-3 max deployable count" "Your deployables reserve
#: 15%-30% reduced Fervour".
#:
#:   Bag of Tricks          +1 trap and +1 gadget a point, 3 points. Sized on the
#:                          full node: traps 3/23 = 13%, gadgets 3/9.7 = 31%, so
#:                          4% and 10% a point (12%: about 2.7 more traps; 30%:
#:                          about 2.9 more gadgets).
#:   Minefield              +1 trap at 6 points: 1/21 = 4.8%, so 5%, the trap
#:                          enchantment's figure for +1.
#:   Mechanical Familiar    +1 gadget: 1/7.7 = 13%, so 15%, the deployable
#:                          enchantment's figure for +1.
#:   Overwhelming Presence  "at maximum active traps" becomes "your Fervour has
#:                          no room to reserve another trap": with no count, that
#:                          is what being unable to place another means.
#:   Precision Placement    keeps "only one active trap at a time", a conversion.
#:                          What it converted was the trap limit, so it now counts
#:                          the traps the maximum Fervour could hold, +10% damage
#:                          and +25% area for every 4: on a pool of 100 at 5 a
#:                          trap that is 20 traps and 5 steps, +50% and +125%,
#:                          what the old wording gave at the old limit of five.
#:   Calculated Chaos       "no maximum active limit" is the rule for everybody
#:                          now, so its overreach becomes placing traps the pool
#:                          has no room to reserve, each draining 5 Fervour a
#:                          second instead, all triggered when Fervour reaches 0.
#:
#: ONE GAP THE WORDS CANNOT CLOSE. A gadget's reserve reaches nothing yet: the
#: War gadgets reserve nothing until issue #1934 ("Only the War gadgets keep
#: their counts, under the no-War rule" in the design document), and a skill
#: gadget is one per converted slot by design. So the gadget halves of Bag of
#: Tricks and Mechanical Familiar say what the rule will be, not what the game
#: does today.
SABOTEUR_CAPS = (
    Reword(
        node="Bag of Tricks",
        before="+1 to maximum active traps and gadgets per point.",
        after=("Your traps reserve 4% reduced Fervour and your gadgets reserve 10% "
               "reduced Fervour, per point."),
    ),
    Reword(
        node="Minefield",
        before=("+10% trap placement speed per point. At 6 points: Your maximum active "
                "trap limit is increased by 1."),
        after=("+10% trap placement speed per point. At 6 points: Your traps reserve 5% "
               "reduced Fervour."),
    ),
    Reword(
        node="Overwhelming Presence",
        before=("While at maximum active traps, you deal 50% more trap damage and trap "
                "trigger radius is doubled."),
        after=("While your Fervour has no room to reserve another trap, you deal 50% more "
               "trap damage and trap trigger radius is doubled."),
    ),
    Reword(
        node="Precision Placement",
        before=("You may only have one active trap at a time. That trap gains +10% damage "
                "and +25% area of effect for each point of your maximum trap limit. Your "
                "maximum trap limit still benefits from all other sources."),
        after=("You may only have one active trap at a time. That trap gains +10% damage "
               "and +25% area of effect for every 4 traps your maximum Fervour could "
               "hold. Anything that lowers what your traps reserve, or raises your maximum "
               "Fervour, still counts toward it."),
    ),
    Reword(
        node="Calculated Chaos",
        before=("Your traps no longer have a maximum active limit. Instead each active "
                "trap beyond your normal maximum drains 5 Fervour per second. When you "
                "reach 0 Fervour all excess traps are immediately triggered "
                "simultaneously."),
        after=("Your traps can be placed even when your Fervour has no room to reserve "
               "them. Each trap placed that way reserves nothing and drains 5 Fervour per "
               "second instead. When you reach 0 Fervour, all of those traps are "
               "immediately triggered simultaneously."),
    ),
    Reword(
        node="Mechanical Familiar",
        before=("Your maximum active gadget limit is increased by 1. Your gadgets gain +1% "
                "bonus damage and +5% bonus HP per 10 Fervour."),
        after=("Your gadgets reserve 15% reduced Fervour. Your gadgets gain +1% bonus "
               "damage and +5% bonus HP per 10 Fervour."),
    ),
)

FIXES: dict[str, tuple] = {
    "Bulwark": (
        # The owner's sentence, with the full stop every node in the tree ends on.
        Reword(
            node="Serrated Edge",
            before="+10% bleed duration per point applied by your retaliation damage.",
            after="+10% duration of bleeds you apply, per point.",
        ),
    ),
    "Masochist": (
        Order(first="Deeper Cuts", then="Blood Price"),
        GRAND_TITHE,
    ),
    "Soul Collector": (
        Order(first="Soul Fuel", then="Spent Breath"),
    ),
    "Necromancer": (
        Reword(
            node="Gathered Remains",
            before=("Your Abomination rises sooner: each point takes 2 off the percentage "
                    "of a floor's starting creatures you must kill, from 50% down to 34% "
                    "at 8 points. Remains within 10 metres of you last 10% longer per "
                    "point."),
            after="Remains within 10 metres of you last 10% longer per point.",
        ),
        MANY_MADE_ONE,
        DirectlyAfter(node="Gravefinder", after="Bone Fuse", partner="Mantle of Bone"),
    ),
    "Shadow": (
        Order(first="Envenom", then="Death's Patience"),
    ),
    "Saboteur": SABOTEUR_CAPS + (
        CapstoneFields(tiers=(("First Oath", 25), ("Second Oath", 50),
                              ("Third Oath", 100), ("Final Oath", 200))),
    ),
}


# --------------------------------------------------------------------------
# Reading a tree
# --------------------------------------------------------------------------

def tree_name(data: dict, path: pathlib.Path | None = None) -> str:
    """The class a tree file is for: "Soul Collector" for either spelling.

    READ FROM THE FILE'S OWN METADATA FIRST, because a sigil layout's file name
    ends `_Class_Tree_Sigil.json` rather than `_Final.json` and a copy can be
    called anything. The file name is checked against it when it names a class,
    and a disagreement is refused: it is the one way a fix could be applied to
    the wrong tree.
    """
    from_metadata = ""
    name = (data.get("metadata") or {}).get("name", "")
    if name.endswith(" Class Tree"):
        from_metadata = name[: -len(" Class Tree")]

    from_file = ""
    if path is not None and "_Class_Tree" in path.name:
        from_file = path.name.split("_Class_Tree")[0].replace("_", " ")

    if from_metadata and from_file and from_metadata != from_file:
        raise Refused(f"{path.name if path else 'the tree'} says it is the "
                      f"{from_metadata!r} tree in its metadata and the {from_file!r} "
                      f"tree in its file name")
    found = from_metadata or from_file
    if not found:
        raise Refused(f"{path.name if path else 'the tree'} does not say which class "
                      f"it is for: no metadata name ending ' Class Tree' and no "
                      f"'<Class>_Class_Tree' file name")
    return found


def node_named(data: dict, name: str, tree: str) -> dict:
    found = [n for n in data["nodes"] if n.get("data", {}).get("name") == name]
    if not found:
        raise Refused(f"{tree}: no node is named {name!r}")
    if len(found) > 1:
        raise Refused(f"{tree}: {len(found)} nodes are named {name!r}, "
                      f"so which one the fix means is ambiguous")
    return found[0]


def children(data: dict) -> dict[str, list[str]]:
    out: dict[str, list[str]] = collections.defaultdict(list)
    for edge in data["edges"]:
        out[edge["source"]].append(edge["target"])
    return out


def is_upstream(data: dict, source_id: str, target_id: str) -> bool:
    """Whether a path of edges leads from `source_id` to `target_id`."""
    below = children(data)
    seen = {source_id}
    stack = [source_id]
    while stack:
        for child in below[stack.pop()]:
            if child == target_id:
                return True
            if child not in seen:
                seen.add(child)
                stack.append(child)
    return False


def least_points_to_open(data: dict) -> dict[str, int]:
    """The fewest points spent in the tree before each node can take a point.

    The sum of the edge requirements along the cheapest path from a node with
    no incoming edge, which is the measure the Death tree summaries use for
    "points to enter" a limb. A node with several parents opens through any of
    them, as `UCataclysmPassiveTree::EdgesAllow` has it.
    """
    incoming = {e["target"] for e in data["edges"]}
    cost = {n["id"]: 0 for n in data["nodes"]
            if n.get("type") != "capstone" and n["id"] not in incoming}
    changed = True
    while changed:
        changed = False
        for edge in data["edges"]:
            if edge["source"] not in cost:
                continue
            through = cost[edge["source"]] + int(edge["data"]["requiredPoints"])
            if through < cost.get(edge["target"], math.inf):
                cost[edge["target"]] = through
                changed = True
    return cost


# --------------------------------------------------------------------------
# Applying a fix
# --------------------------------------------------------------------------

def swap_content(first: dict, second: dict, tree: str) -> str:
    """Exchange two nodes' content in place, and say what moved."""
    if first.get("type") != second.get("type"):
        raise Refused(f"{tree}: {first['data']['name']!r} is a {first.get('type')} "
                      f"and {second['data']['name']!r} a {second.get('type')}; a swap "
                      f"would put a node of one kind where the other was drawn")
    kept_first = {k: first["data"][k] for k in STAYS_WITH_THE_POSITION if k in first["data"]}
    kept_second = {k: second["data"][k] for k in STAYS_WITH_THE_POSITION
                   if k in second["data"]}
    first["data"], second["data"] = second["data"], first["data"]
    # ASSIGNED IN PLACE RATHER THAN REMOVED AND ADDED, so the field keeps its
    # place in the dictionary and the written file differs only in the values.
    for node, kept in ((first, kept_first), (second, kept_second)):
        for k in STAYS_WITH_THE_POSITION:
            if k in kept:
                node["data"][k] = kept[k]
            else:
                node["data"].pop(k, None)
    return (f"{first['id']} now holds {first['data']['name']!r} "
            f"({first['data']['maxPoints']} points) and {second['id']} holds "
            f"{second['data']['name']!r} ({second['data']['maxPoints']} points)")


def apply_reword(data: dict, fix: Reword, tree: str) -> list[str]:
    node = node_named(data, fix.node, tree)
    now = node["data"].get("description", "")
    if now == fix.after:
        return []
    if now != fix.before:
        raise Refused(f"{tree}: {fix.node!r} reads {now!r}, which is neither the "
                      f"sentence the fix was approved against nor the fixed one")
    node["data"]["description"] = fix.after
    return [f"reworded {fix.node!r} ({node['id']})"]


def apply_order(data: dict, fix: Order, tree: str) -> list[str]:
    first = node_named(data, fix.first, tree)
    then = node_named(data, fix.then, tree)
    if is_upstream(data, first["id"], then["id"]):
        return []
    if not is_upstream(data, then["id"], first["id"]):
        raise Refused(f"{tree}: {fix.first!r} and {fix.then!r} are on no common path, "
                      f"so neither can come before the other")
    moved = swap_content(then, first, tree)
    return [f"swapped {fix.first!r} and {fix.then!r}: {moved}"]


def apply_directly_after(data: dict, fix: DirectlyAfter, tree: str) -> list[str]:
    node = node_named(data, fix.node, tree)
    after = node_named(data, fix.after, tree)
    below = children(data)
    if node["id"] in below[after["id"]]:
        return []
    on_the_way = [c for c in below[after["id"]]
                  if c == node["id"] or is_upstream(data, c, node["id"])]
    if not on_the_way:
        raise Refused(f"{tree}: {fix.node!r} is not below {fix.after!r}, so it cannot be "
                      f"moved to directly after it by a swap")
    by_id = {n["id"]: n for n in data["nodes"]}
    names = sorted(by_id[c]["data"]["name"] for c in on_the_way)
    if names != [fix.partner]:
        raise Refused(f"{tree}: the node directly after {fix.after!r} on the way to "
                      f"{fix.node!r} is {names}, and the fix was approved for swapping "
                      f"with {fix.partner!r}")
    partner = by_id[on_the_way[0]]
    moved = swap_content(partner, node, tree)
    return [f"moved {fix.node!r} to directly after {fix.after!r}: {moved}"]


def apply_capstone_fields(data: dict, fix: CapstoneFields, tree: str) -> list[str]:
    """Add `isDecision`, `threshold` and `selectedOption` where they are missing.

    APPENDED AFTER THE FIELDS ALREADY THERE, which is the order the Berserker's
    capstones have them in: `pointThreshold`, then `isDecision`, `threshold` and
    `selectedOption`.
    """
    changes = []
    wanted_tiers = sorted(tier for _, tier in fix.tiers)
    for name, tier in fix.tiers:
        node = node_named(data, name, tree)
        body = node["data"]
        if node.get("type") != "capstone":
            raise Refused(f"{tree}: {name!r} is a {node.get('type')}, not a capstone")
        stated = body.get("pointThreshold", body.get("threshold"))
        if stated is not None and int(stated) != tier:
            raise Refused(f"{tree}: {name!r} carries a threshold of {stated}, and the fix "
                          f"was written for the {tier} point tier")
        wanted = {"isDecision": True, "threshold": tier, "selectedOption": None}
        added = []
        for field, value in wanted.items():
            if field in body:
                if body[field] != value:
                    raise Refused(f"{tree}: {name!r} already has {field} = "
                                  f"{body[field]!r}, not {value!r}")
                continue
            body[field] = value
            added.append(field)
        if added:
            changes.append(f"gave {name!r} ({node['id']}) {', '.join(added)}; its "
                           f"{len(body.get('options') or [])} options left as they are")
    found = sorted(int(n["data"].get("threshold", 0)) for n in data["nodes"]
                   if n.get("type") == "capstone")
    if found != wanted_tiers:
        raise Refused(f"{tree}: the capstones' thresholds are {found}, not {wanted_tiers}")
    return changes


def _too_close(data: dict, x: float, y: float, ignore: str = "") -> list[str]:
    """Nodes drawn within half the tightest spacing of the draft trees.

    The Demonic and Death trees put neighbours 120 to 150 units apart on the
    canvas, so anything within 60 is on top of the new node.
    """
    return [n["data"]["name"] for n in data["nodes"]
            if n["id"] != ignore
            and math.hypot(n["position"]["x"] - x, n["position"]["y"] - y) < 60.0]


def apply_new_node(data: dict, fix: NewNode, tree: str,
                   position: tuple[float, float] | None) -> list[str]:
    parent = node_named(data, fix.parent, tree)
    if parent["data"].get("maxPoints") is None or (
            int(parent["data"]["maxPoints"]) < fix.required_points):
        raise Refused(f"{tree}: {fix.parent!r} holds {parent['data'].get('maxPoints')} "
                      f"points and the new node's edge asks for {fix.required_points}")

    existing = [n for n in data["nodes"] if n.get("data", {}).get("name") == fix.name]
    if existing:
        node = existing[0]
        joined = [e for e in data["edges"]
                  if e["source"] == parent["id"] and e["target"] == node["id"]
                  and int(e["data"]["requiredPoints"]) == fix.required_points]
        if (len(existing) == 1 and node["data"].get("description") == fix.description
                and int(node["data"].get("maxPoints", 0)) == fix.max_points and joined):
            return []
        raise Refused(f"{tree}: a node named {fix.name!r} already exists and is not the "
                      f"one this fix adds")
    if any(n["id"] == fix.node_id for n in data["nodes"]):
        raise Refused(f"{tree}: node identifier {fix.node_id!r} is already used")
    if any(e["id"] == fix.edge_id for e in data["edges"]):
        raise Refused(f"{tree}: edge identifier {fix.edge_id!r} is already used")

    if position is None:
        position = (parent["position"]["x"] + fix.offset[0],
                    parent["position"]["y"] + fix.offset[1])
    crowded = _too_close(data, *position)
    if crowded:
        raise Refused(f"{tree}: the new node {fix.name!r} would be drawn at {position}, "
                      f"on top of {crowded}. Choose a place with --new-node-position X,Y")

    # COPIED FROM THE PARENT, so every key the file's other nodes carry -- the
    # editor's `measured` size, its selection flags -- is present in the same
    # shape, and only what this node is about is set.
    node = copy.deepcopy(parent)
    node["id"] = fix.node_id
    node["position"] = {"x": position[0], "y": position[1]}
    node["type"] = "basic"
    node["data"] = {"kind": "basic", "name": fix.name, "description": fix.description,
                    "currentPoints": 0, "color": parent["data"].get("color", "#94a3b8"),
                    "maxPoints": fix.max_points}
    for flag in ("selected", "dragging"):
        if flag in node:
            node[flag] = False
    _match_number_kind(node["position"], parent["position"])

    template = next((e for e in data["edges"] if e["source"] == parent["id"]), None)
    if template is None:
        template = data["edges"][0]
    edge = copy.deepcopy(template)
    edge["id"] = fix.edge_id
    edge["source"] = parent["id"]
    edge["target"] = fix.node_id
    edge["data"] = {**edge.get("data", {}), "requiredPoints": fix.required_points}
    if "selected" in edge:
        edge["selected"] = False

    # Beside the parent in both lists, so a reader of the file finds it there.
    data["nodes"].insert(data["nodes"].index(parent) + 1, node)
    out_of_parent = [i for i, e in enumerate(data["edges"]) if e["source"] == parent["id"]]
    after = out_of_parent[-1] if out_of_parent else len(data["edges"]) - 1
    data["edges"].insert(after + 1, edge)

    opens_at = least_points_to_open(data).get(fix.node_id)
    if opens_at is None or opens_at < fix.least_points:
        raise Refused(f"{tree}: the new node {fix.name!r} would open at {opens_at} points "
                      f"spent, and it was placed to need at least {fix.least_points}")
    return [f"added {fix.name!r} ({fix.node_id}, {fix.max_points} points) after "
            f"{fix.parent!r} ({parent['id']}), asking for {fix.required_points} points in "
            f"it, so it opens at {opens_at} points spent; drawn at "
            f"({node['position']['x']}, {node['position']['y']})"]


def _match_number_kind(position: dict, like: dict) -> None:
    """Write whole coordinates as integers when the parent's are integers.

    The draft trees write `-600`, not `-600.0`; matching it keeps the new node's
    lines looking like every other node's.
    """
    for axis in ("x", "y"):
        value = position[axis]
        if isinstance(like.get(axis), int) and float(value).is_integer():
            position[axis] = int(value)


def check_structure(data: dict, tree: str) -> None:
    """The two edge rules a swap can break, checked after the fixes.

    A swap moves maximum points between positions while the edges stay, so an
    edge out of a node that shrank could ask for more than it now holds, and a
    keystone whose parent changed size would no longer ask for its parent in
    full. `tools/tests/test_class_passive_trees.py` holds both rules for the
    trees in docs/; this refuses before writing, so a broken tree is never
    written in the first place.
    """
    by_id = {n["id"]: n for n in data["nodes"]}
    problems = []
    for edge in data["edges"]:
        source = by_id.get(edge["source"])
        target = by_id.get(edge["target"])
        if source is None or target is None:
            problems.append(f"edge {edge['id']} joins a node that does not exist")
            continue
        required = int(edge["data"]["requiredPoints"])
        held = int(source["data"]["maxPoints"])
        if required > held:
            problems.append(f"edge {edge['id']} asks for {required} points in "
                            f"{source['data']['name']!r}, which holds {held}")
        if target.get("type") == "keystone" and required != held:
            problems.append(f"keystone {target['data']['name']!r} asks for {required} "
                            f"points in {source['data']['name']!r}, which holds {held}: "
                            f"not full investment")
    ids = collections.Counter(n["id"] for n in data["nodes"])
    names = collections.Counter(n["data"]["name"] for n in data["nodes"])
    problems += [f"node identifier {i!r} is used {c} times" for i, c in ids.items() if c > 1]
    problems += [f"node name {n!r} is used {c} times" for n, c in names.items() if c > 1]
    if problems:
        raise Refused(f"{tree}: after the fixes, " + "; ".join(problems))


def keystone_parents(data: dict) -> set[str]:
    by_id = {n["id"]: n for n in data["nodes"]}
    return {e["source"] for e in data["edges"]
            if by_id.get(e["target"], {}).get("type") == "keystone"}


def _swap_note(data: dict, held_before: dict[str, int]) -> str:
    """What a swap did to the two positions' sizes, and to any keystone.

    A swap moves maximum points while the edges stay where they are. That is
    harmless unless one of the two positions is a keystone's parent: the
    keystone's edge asks for the parent's old maximum, so a parent that changed
    size no longer asks for "full investment". `check_structure` refuses that
    case; this line says which it was, so the report never has to be inferred.
    """
    by_id = {n["id"]: n for n in data["nodes"]}
    sizes = ", ".join(f"{i} {held_before[i]} -> {by_id[i]['data']['maxPoints']} points"
                      for i in sorted(held_before))
    parents = sorted(set(held_before) & keystone_parents(data))
    if not parents:
        return f"  sizes: {sizes}; neither position is a keystone's parent"
    resized = [i for i in parents if held_before[i] != by_id[i]["data"]["maxPoints"]]
    if not resized:
        return (f"  sizes: {sizes}; {parents} parent a keystone and kept their size, so "
                f"the keystone still asks for its parent in full")
    return (f"  sizes: {sizes}; KEYSTONE PARENT CHANGED SIZE at {resized}, so its "
            f"keystone no longer asks for full investment")


def apply_fixes(data: dict, tree: str,
                new_node_position: tuple[float, float] | None = None,
                only: frozenset[str] | None = None) -> list[str]:
    """Apply every fix for `tree` to `data` in place; return what changed.

    An empty list means the tree was already fixed, has no fixes, or is not
    among `only` when that is given.
    """
    if only is not None and tree not in only:
        return []
    changes: list[str] = []
    for fix in FIXES.get(tree, ()):
        if isinstance(fix, Reword):
            changes += apply_reword(data, fix, tree)
        elif isinstance(fix, (Order, DirectlyAfter)):
            pair = ((fix.first, fix.then) if isinstance(fix, Order)
                    else (fix.node, fix.partner))
            held_before = {node["id"]: int(node["data"]["maxPoints"])
                           for node in (node_named(data, name, tree) for name in pair)}
            done = (apply_order(data, fix, tree) if isinstance(fix, Order)
                    else apply_directly_after(data, fix, tree))
            changes += done
            if done:
                changes.append(_swap_note(data, held_before))
        elif isinstance(fix, NewNode):
            changes += apply_new_node(data, fix, tree, new_node_position)
        elif isinstance(fix, CapstoneFields):
            changes += apply_capstone_fields(data, fix, tree)
        else:  # pragma: no cover - a new kind of fix without a branch here
            raise TypeError(f"no way to apply {fix!r}")
    if changes:
        check_structure(data, tree)
    return changes


# --------------------------------------------------------------------------
# Files
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Format:
    newline: str
    trailing: str
    ensure_ascii: bool

    def write(self, data: dict) -> bytes:
        body = json.dumps(data, indent=2, ensure_ascii=self.ensure_ascii)
        return (body.replace("\n", self.newline) + self.trailing).encode("utf-8")


def format_of(raw: bytes, data: dict) -> Format | None:
    """The serialisation that reproduces `raw` exactly, or None."""
    for newline in ("\r\n", "\n"):
        for trailing in (newline, ""):
            for ensure_ascii in (True, False):
                candidate = Format(newline, trailing, ensure_ascii)
                if candidate.write(data) == raw:
                    return candidate
    return None


@dataclasses.dataclass
class Outcome:
    path: pathlib.Path
    tree: str
    changes: list[str]
    new_bytes: bytes | None  # None when nothing changes


def plan_file(path: pathlib.Path, new_node_position: tuple[float, float] | None = None,
              allow_reformat: bool = False, only: frozenset[str] | None = None) -> Outcome:
    """Work out what fixing one file would write, without writing it."""
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise Refused(f"{path.name} is not a JSON tree file: {error}") from error
    if not isinstance(data, dict) or "nodes" not in data or "edges" not in data:
        raise Refused(f"{path.name} has no nodes and edges, so it is not a tree file")

    tree = tree_name(data, path)
    form = format_of(raw, data)
    if form is None and not allow_reformat:
        raise Refused(f"{path.name}: no two-space JSON serialisation reproduces the file "
                      f"byte for byte, so writing it would reformat every line. Pass "
                      f"--allow-reformat to write it anyway")
    form = form or Format("\r\n" if b"\r\n" in raw else "\n",
                          "\r\n" if raw.endswith(b"\r\n") else
                          "\n" if raw.endswith(b"\n") else "", True)

    changes = apply_fixes(data, tree, new_node_position, only)
    new_bytes = form.write(data) if changes else None
    if new_bytes == raw:
        new_bytes = None
    return Outcome(path, tree, changes, new_bytes)


def tree_files(arguments: list[str]) -> list[pathlib.Path]:
    out: list[pathlib.Path] = []
    for argument in arguments:
        path = pathlib.Path(argument)
        if path.is_dir():
            found = sorted(path.glob("*_Class_Tree_*.json"))
            if not found:
                raise Refused(f"{path} holds no '*_Class_Tree_*.json' file")
            out += found
        elif path.is_file():
            out.append(path)
        else:
            raise Refused(f"{path} does not exist")
    return out


def parse_position(text: str) -> tuple[float, float]:
    try:
        x, y = (float(part) for part in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"{text!r} is not a position; write it as X,Y") from error
    return x, y


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Apply the passive tree order fixes of 2026-11-24 by node name.")
    parser.add_argument("paths", nargs="+",
                        help="tree JSON files, or directories holding *_Class_Tree_*.json")
    parser.add_argument("--check", action="store_true",
                        help="write nothing; exit 1 if any file would change")
    parser.add_argument("--new-node-position", type=parse_position, default=None,
                        metavar="X,Y",
                        help="where to draw the Necromancer's new node, when the place "
                             "next to its parent is taken")
    parser.add_argument("--allow-reformat", action="store_true",
                        help="write a file even when its format cannot be reproduced")
    parser.add_argument("--only", action="append", choices=sorted(FIXES), metavar="TREE",
                        help="apply only this tree's fixes; repeat for several. "
                             f"One of: {', '.join(sorted(FIXES))}")
    args = parser.parse_args(argv)
    only = frozenset(args.only) if args.only else None

    try:
        paths = tree_files(args.paths)
        outcomes = [plan_file(p, args.new_node_position, args.allow_reformat, only)
                    for p in paths]
    except Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2

    pending = 0
    for outcome in outcomes:
        if outcome.tree not in FIXES:
            print(f"{outcome.path}: the {outcome.tree} tree has no fixes; left alone")
            continue
        if only is not None and outcome.tree not in only:
            print(f"{outcome.path}: the {outcome.tree} tree is not among --only; left alone")
            continue
        if outcome.new_bytes is None:
            print(f"{outcome.path}: the {outcome.tree} tree is already fixed; left alone")
            continue
        pending += 1
        verb = "would change" if args.check else "changed"
        print(f"{outcome.path}: the {outcome.tree} tree {verb}:")
        for line in outcome.changes:
            print(f"  {line}")

    if args.check:
        print(f"{pending} of {len(outcomes)} file(s) would change.")
        return 1 if pending else 0

    for outcome in outcomes:
        if outcome.new_bytes is not None:
            outcome.path.write_bytes(outcome.new_bytes)
    print(f"{pending} of {len(outcomes)} file(s) changed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
