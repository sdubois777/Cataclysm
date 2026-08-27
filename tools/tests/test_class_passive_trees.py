"""The four class passive trees in docs/, checked against the design document.

WHY THIS EXISTS. Issue #63 asked for a Demonic class passive tree, which is one
of the five Phase 1 vertical slice deliverables in section XV of
`docs/Cataclysm_GDD_v2.md`. Three class trees already existed --
`docs/Berserker_Class_Tree_Final.json`, `docs/Bulwark_Class_Tree_Final.json` and
`docs/Saboteur_Class_Tree_Final.json` -- and nothing checked any of them. They
were authored in March 2026, before this repository existed, and the structural
rules they follow were only written down in prose.

`docs/Masochist_Class_Tree_Final.json` is the fourth, added for #63. Writing it
meant reading the other three to work out the rules, and that reading is what
this file makes permanent.

WHAT A CLASS TREE IS. A node graph authored in the editor at
C:\\Projects\\PassiveTreeCreator and exported as JSON. Three node kinds: basic
nodes hold several points and give a per-point bonus, keystones hold one point
and change a rule, capstones are the four milestone choices at 25, 50, 100 and
200 points spent. Edges are dependencies: an edge from A to B with
`requiredPoints: 6` means B cannot be taken until 6 points sit in A.

WHAT THE DESIGN DOCUMENT SPECIFIES, in the "Passive Class Trees" section: "Each
class tree has approximately 74 nodes, 15 keystones, 4 capstone tiers (at
25/50/100/200 points), and a total of ~440 spendable points. The per-character
point budget is 230." The keystone rule is separate and exact: keystones
"require full investment in a parent node".

TWO REAL DEFECTS IN THE EXISTING TREES WERE FOUND BY WRITING THIS FILE, and both
were exempted by name below rather than hidden, so that the check still ran over
everything else and a NEW instance of either failed.

    #343  Two different Bulwark keystones were both named "Immovable Object".
          FIXED on 2026-08-14. The crowd control immunity keystone
          (`keystone_jg_t1`) kept the name, because that is what immovable
          plainly means, and the consecutive-block keystone (`keystone_ic_t2`)
          became "Unyielding Guard". `KNOWN_DUPLICATE_NAMES` is now empty and
          every tree is covered.
    #344  Saboteur's "Reinforced Housing" is a basic node using a "more"
          multiplier, which the design document reserves for gems, keystones
          and enchantments. Still exempted.

An exemption is a single named entry. Delete the entry when the issue is fixed;
the test then covers that tree too. The set itself stays, so a future deliberate
duplicate has somewhere to be declared with its issue number.

WHAT IS ASSERTED HERE.

    every class tree parses, is version 1.0, and carries the 230 point budget
    node counts, keystone counts and capstone counts are in range
    total spendable points is in range, and exceeds the budget, because
      specialisation is the point
    keystones and capstones hold exactly one point
    every capstone tier is 25, 50, 100 or 200 and each offers three choices
    no edge requires more points than its source node can hold
    a keystone's edge requires FULL investment in its parent
    the spendable part of the tree is one connected web reachable from a root
    capstones are not wired into the web, because a tier is reached by total
      points spent rather than by a path
    node ids and node names are unique within a tree
    basic nodes do not use "more" or "less" as a magnitude
    the Masochist tree specifically: it exists, and it has the two ways of
      filling Fervour that the project owner decided on

EVERY CLASS SHARES ONE RESOURCE, CALLED FERVOUR, since 2026-08-25. What a tree
owns is a generator -- how it fills Fervour and what that class adds about
emptying it -- rather than a resource of its own. Every tree's starting node
is named "Fervour" for that reason: they are four ways into one bar, not four
resources. `docs/DECISIONS.md` has the reasoning.
"""

from __future__ import annotations

import collections
import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO_ROOT / "docs"
GDD = DOCS / "Cataclysm_GDD_v2.md"

#: Every class tree in docs/. The empire tree is a different shape and has its
#: own checks in test_empire_tree_documents_agree.py.
CLASS_TREES = ("Berserker", "Bulwark", "Saboteur", "Masochist")

#: The tree issue #63 added, and the one the Phase 1 vertical slice needs.
DEMONIC_TREE = "Masochist"

#: The four capstone tiers, from the Passive Class Trees section of the GDD.
CAPSTONE_THRESHOLDS = (25, 50, 100, 200)

#: The per-character budget, from the same sentence.
POINT_BUDGET = 230

#: Empty since issue #343 was fixed on 2026-08-14. Two different Bulwark
#: keystones were both called "Immovable Object"; the crowd control immunity one
#: kept the name and the consecutive-block one became "Unyielding Guard".
#:
#: KEPT AS AN EMPTY SET rather than deleted with the entry. The test below reads
#: it, so removing it means rewriting the test, and the mechanism is the thing
#: worth keeping: a future deliberate duplicate has somewhere to be declared and
#: an issue number to carry. An empty set states that there are none today.
KNOWN_DUPLICATE_NAMES: set[tuple[str, str]] = set()

#: "20% more damage" and "50% less damage" are separate multipliers, where
#: "increased" joins one additive bucket. Since issue #344 the design document
#: permits both on any passive tree node; it still refuses them on a gear affix,
#: which `test_what_affixes_do_not_grant.py` holds.
#:
#: The pattern requires a number and a percent sign so that ordinary English --
#: "5 or more bleed stacks", "no more than once every 10 seconds", "more than 10
#: meters" -- does not match.
#:
#: THE COUNTS HERE WERE WRONG AND ARE NOW MEASURED BY A TEST. This comment said
#: "twenty-five nodes across the five trees contain the word and only ten are
#: magnitudes". Re-measured 2026-08-14 for issue #582: twenty-nine contained the
#: word and twenty were magnitudes. Both figures were wrong, and nothing noticed,
#: so `test_the_measured_wording_counts_are_still_right` now checks them.
MAGNITUDE_WORDING = re.compile(r"\d+\s*%\s+(?:more|less)\b", re.IGNORECASE)

#: How many strings across the five trees contain the bare word "more" or "less",
#: and how many use it as a percentage magnitude. Measured, not estimated.
#:
#: BOTH DROPPED BY ONE WHEN ISSUE #582 LANDED, because Thornwall's "by 5% more"
#: became "by 5%" and so stopped containing the word at all. The Saboteur
#: keystone kept its "more" and still counts in both.
#:
#: BOTH WENT UP BY ONE ON 2026-08-25, when the Masochist's upper-right section
#: was replaced. Two of the strings that went used the word and both were
#: magnitudes; three of the strings that arrived use it and all three are
#: magnitudes. All six are keystones, which is where the wording belongs: the two
#: basic nodes in the new section that reached for it were reworded instead, one
#: of them because a conditional damage bonus joins the increases bracket rather
#: than becoming a separate multiplier.
#:
#: BOTH WENT UP BY ONE AGAIN ON 2026-08-27, when all twelve Masochist capstone
#: options were rewritten. Issue #1031. Two of the old twelve used the word and
#: both were magnitudes: "50% less damage" in The Immortal Champion and "1% more
#: damage" in Apotheosis. FOUR of the new twelve contain it and THREE are
#: magnitudes -- Doctrine Made Flesh, Carnivore and Vessel Unbroken. The fourth
#: is Rock Bottom's "no more than once every 30 seconds", which is ordinary
#: English, and it is the reason these two counts are measured separately rather
#: than one being derived from the other.
STRINGS_CONTAINING_THE_WORD = 30
STRINGS_USING_IT_AS_A_MAGNITUDE = 21

#: A node that uses BOTH magnitude words for one number, as in "increased by 50%
#: more". Issue #582.
#:
#: WHY IT IS A FAULT RATHER THAN A STYLE PREFERENCE. The two words name different
#: places in the damage pipeline, `(base + flat) x (1 + increases) x more1 x
#: more2`. "increased" joins the additive bucket; "more" is its own multiplier. A
#: sentence using both for one number does not say which the number is, and the
#: two readings differ by a large factor on an invested character.
#:
#: THE 80 CHARACTER WINDOW AND THE FULL STOP MATTER. Without them a node that
#: legitimately grants an increase in one sentence and a "more" multiplier in
#: another would match. Measured 2026-08-14: with them, exactly the two nodes
#: issue #582 names matched and nothing else did.
BOTH_MAGNITUDE_WORDS = re.compile(
    r"(?:increas|reduc|decreas)\w*[^.]{0,80}?\d+\s*%\s+(?:more|less)\b",
    re.IGNORECASE)

#: How many non-keystone nodes use that wording. It was eight on 2026-08-14, when
#: issue #344 widened the rule to cover them: four basic nodes and four capstone
#: options across the Bulwark, Masochist, Saboteur and empire trees.
#:
#: SEVEN SINCE ISSUE #582 the same day. The eighth was Bulwark's Thornwall
#: capstone option, which said "increased by 5% more" and now says "increased by
#: 5%". It is the only one that left the Bulwark tree, so the widened rule is now
#: relied on by the Masochist, Saboteur and empire trees only.
#:
#: STILL SEVEN ON 2026-08-27, BUT NOT THE SAME SEVEN. Issue #1031 rewrote all
#: twelve Masochist capstone options, and the count happens to be unchanged
#: because one Masochist capstone left the list and another joined it. The
#: Second Vow left: its "50% less damage" clause was The Immortal Champion,
#: which is gone. The Third Vow joined: Doctrine Made Flesh grants "1% more
#: damage" per debuff. A count that does not move is exactly the case a reader
#: would assume nothing happened in, so the list is spelled out again below.
#:
#: The seven are Economic Zones, Salvage Protocol, The Imperial Vanguard and
#: Thrifty in the empire tree, The Third Vow and The Final Vow in the Masochist
#: tree, and Reinforced Housing in the Saboteur tree.
#:
#: Pinned exactly rather than as a floor, for the reason
#: `test_the_widened_rule_is_actually_relied_on` gives.
NODES_RELYING_ON_THE_WIDENED_RULE = 7


def load(tree_name: str) -> dict:
    path = DOCS / f"{tree_name}_Class_Tree_Final.json"
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return json.loads(path.read_text(encoding="utf-8"))


@pytest.fixture(scope="module", params=CLASS_TREES)
def tree(request) -> tuple[str, dict]:
    return request.param, load(request.param)


def nodes_of(data: dict, kind: str) -> list[dict]:
    return [n for n in data["nodes"] if n["type"] == kind]


# --------------------------------------------------------------------------
# The shape the design document specifies
# --------------------------------------------------------------------------

def test_the_file_is_a_version_1_tree_with_the_stated_point_budget(tree):
    """The editor at C:\\Projects\\PassiveTreeCreator refuses any other version
    (`parseTreeJSON` in src/utils/serialization.ts), so a tree that fails this
    cannot be opened and edited at all."""
    name, data = tree
    assert data["version"] == "1.0", (
        f"{name}: the tree is version {data['version']!r}. The editor only "
        f"loads '1.0' and throws on anything else.")
    assert data["metadata"]["pointBudget"] == POINT_BUDGET, (
        f"{name}: the point budget is {data['metadata']['pointBudget']}, not "
        f"{POINT_BUDGET}. The design document sets one budget for every class "
        f"tree, and multiclassing spends the same pool across several trees, "
        f"so they cannot differ.")


def test_the_node_count_is_about_seventy_four(tree):
    """"Approximately 74 nodes." The three March trees are 71, 74 and 74, so
    the range is what "approximately" has meant in practice."""
    name, data = tree
    assert 70 <= len(data["nodes"]) <= 78, (
        f"{name}: {len(data['nodes'])} nodes. The design document asks for "
        f"approximately 74 and the existing trees are 71 to 74.")


def test_there_are_exactly_fifteen_keystones(tree):
    """"15 keystones" is stated as a number rather than an approximation, and
    all four trees hit it exactly."""
    name, data = tree
    keystones = nodes_of(data, "keystone")
    assert len(keystones) == 15, (
        f"{name}: {len(keystones)} keystones, not 15. Keystones are the "
        f"build-defining single-point nodes and the design document fixes the "
        f"count.")


def test_there_are_exactly_four_capstones(tree):
    """One per tier: 25, 50, 100, 200."""
    name, data = tree
    capstones = nodes_of(data, "capstone")
    assert len(capstones) == 4, (
        f"{name}: {len(capstones)} capstones, not 4. There is one per tier and "
        f"there are four tiers.")


def test_the_spendable_total_is_about_four_hundred_and_forty(tree):
    """"A total of ~440 spendable points." Berserker is 400 and Saboteur 396,
    so the floor is set by what shipped rather than by the sentence."""
    name, data = tree
    total = sum(n["data"]["maxPoints"] for n in data["nodes"])
    assert 390 <= total <= 460, (
        f"{name}: {total} spendable points. The design document asks for "
        f"about 440 and the existing trees are 396 to 440.")


def test_a_tree_holds_far_more_points_than_a_character_can_spend(tree):
    """The reason the budget matters. "The per-character point budget is 230,
    meaning players invest in roughly 53% of any tree -- specialization is
    required." A tree a character could fill would delete that."""
    name, data = tree
    total = sum(n["data"]["maxPoints"] for n in data["nodes"])
    assert total > POINT_BUDGET * 1.5, (
        f"{name}: {total} spendable points against a {POINT_BUDGET} point "
        f"budget. A character can reach {total / POINT_BUDGET:.0%} of the "
        f"tree. Specialisation stops being a choice when the tree is small "
        f"enough to fill.")


def test_keystones_and_capstones_hold_exactly_one_point(tree):
    """Both are declared `maxPoints: 1` in the editor's own types
    (src/types/nodes.ts). A keystone is a switch, not a scaling node."""
    name, data = tree
    for kind in ("keystone", "capstone"):
        for node in nodes_of(data, kind):
            assert node["data"]["maxPoints"] == 1, (
                f"{name}: {kind} {node['data']['name']!r} holds "
                f"{node['data']['maxPoints']} points. A {kind} is a "
                f"single-point node.")


# --------------------------------------------------------------------------
# Edges, and what an edge means
# --------------------------------------------------------------------------

def test_every_edge_is_a_dependency_carrying_a_point_requirement(tree):
    """There is one edge type. An edge with no `requiredPoints` would be read
    as zero by `isEdgeUnlocked` in the editor's pointsLogic.ts and would gate
    nothing."""
    name, data = tree
    for edge in data["edges"]:
        assert edge["type"] == "dependency", (
            f"{name}: edge {edge['id']} has type {edge['type']!r}. "
            f"'dependency' is the only edge type the editor knows.")
        assert isinstance(edge.get("data", {}).get("requiredPoints"), int), (
            f"{name}: edge {edge['id']} carries no integer requiredPoints, so "
            f"it gates nothing.")


def test_no_edge_asks_for_more_points_than_its_source_can_hold(tree):
    """An edge requiring 9 points from an 8 point node can never unlock, so
    everything beyond it is dead tree. Nothing in the editor prevents it."""
    name, data = tree
    by_id = {n["id"]: n for n in data["nodes"]}
    for edge in data["edges"]:
        source = by_id[edge["source"]]
        required = edge["data"]["requiredPoints"]
        assert required <= source["data"]["maxPoints"], (
            f"{name}: edge {edge['id']} needs {required} points in "
            f"{source['data']['name']!r}, which holds at most "
            f"{source['data']['maxPoints']}. Nothing past this edge can ever "
            f"be reached.")


def test_a_keystone_requires_full_investment_in_its_parent(tree):
    """The design document's node type table: keystones "Require full
    investment in a parent node." All four trees already do this, which is why
    it is asserted rather than recorded as a convention. It is the rule that
    makes a keystone expensive: reaching one costs the whole parent node."""
    name, data = tree
    by_id = {n["id"]: n for n in data["nodes"]}
    for edge in data["edges"]:
        target = by_id[edge["target"]]
        if target["type"] != "keystone":
            continue
        source = by_id[edge["source"]]
        assert edge["data"]["requiredPoints"] == source["data"]["maxPoints"], (
            f"{name}: keystone {target['data']['name']!r} unlocks at "
            f"{edge['data']['requiredPoints']} points in "
            f"{source['data']['name']!r}, which holds "
            f"{source['data']['maxPoints']}. The design document says a "
            f"keystone requires FULL investment in its parent.")


def test_the_spendable_tree_is_one_connected_web(tree):
    """A node no path reaches is unreachable however many points a character
    has. The check walks the edges as undirected, which is how the editor draws
    them, and starts from the node with no incoming edge."""
    name, data = tree
    web = [n["id"] for n in data["nodes"] if n["type"] != "capstone"]
    adjacency = collections.defaultdict(set)
    for edge in data["edges"]:
        adjacency[edge["source"]].add(edge["target"])
        adjacency[edge["target"]].add(edge["source"])

    incoming = collections.Counter(e["target"] for e in data["edges"])
    roots = [n for n in web if incoming[n] == 0]
    assert roots, f"{name}: every node has a parent, so the tree has no entry point."

    seen = {roots[0]}
    stack = [roots[0]]
    while stack:
        for neighbour in adjacency[stack.pop()]:
            if neighbour not in seen:
                seen.add(neighbour)
                stack.append(neighbour)

    stranded = sorted(set(web) - seen)
    assert not stranded, (
        f"{name}: {len(stranded)} nodes are not connected to the rest of the "
        f"tree and can never be taken: {stranded[:5]}")


def test_capstones_are_not_wired_into_the_web(tree):
    """A capstone tier is reached by TOTAL points spent anywhere in the tree,
    not by a path through it. Giving one a parent edge would add a second,
    contradictory condition. All four trees leave them free-standing."""
    name, data = tree
    capstone_ids = {n["id"] for n in nodes_of(data, "capstone")}
    wired = sorted(
        capstone_ids & {e["source"] for e in data["edges"]}
        | capstone_ids & {e["target"] for e in data["edges"]})
    assert not wired, (
        f"{name}: capstones {wired} have edges. A capstone unlocks on total "
        f"points spent, so an edge would gate it twice.")


# --------------------------------------------------------------------------
# Naming and wording
# --------------------------------------------------------------------------

def test_node_ids_are_unique(tree):
    """Two nodes sharing an id makes every edge touching it ambiguous."""
    name, data = tree
    counts = collections.Counter(n["id"] for n in data["nodes"])
    repeated = sorted(i for i, c in counts.items() if c > 1)
    assert not repeated, f"{name}: duplicate node ids {repeated}"


def test_node_names_are_unique_within_a_tree(tree):
    """A player, a build guide and any user interface all identify a node by
    its name. Two nodes sharing one inside a single tree cannot be told apart.

    Sharing a name ACROSS trees is normal here and is not checked: Bulwark and
    Berserker deliberately share eight, including "Iron Will" and "Colossus".

    KNOWN_DUPLICATE_NAMES holds the one real violation, issue #343. It is
    exempted by name rather than skipped, so a second duplicate anywhere --
    including a second one in Bulwark -- still fails this test."""
    name, data = tree
    counts = collections.Counter(n["data"]["name"] for n in data["nodes"])
    repeated = sorted(
        n for n, c in counts.items()
        if c > 1 and (name, n) not in KNOWN_DUPLICATE_NAMES)
    assert not repeated, (
        f"{name}: these node names appear more than once in the same tree: "
        f"{repeated}. Two nodes with one name cannot be told apart by a "
        f"player. If this is deliberate, it needs an issue and an entry in "
        f"KNOWN_DUPLICATE_NAMES.")


def nodes_relying_on_the_widened_wording_rule() -> list[str]:
    """Non-keystone nodes that use "more" or "less" as a magnitude.

    Every one of these was an undocumented exception until issue #344 was
    answered on 2026-08-14. Capstone option text is read as well as the node's
    own description, because a capstone's effects live in its options.

    The EMPIRE tree is included, unlike everywhere else in this file. Two of the
    four are there, and nothing else in the repository scans it for this.
    """
    found: list[str] = []
    paths = sorted(DOCS.glob("*_Class_Tree_Final.json"))
    paths.append(DOCS / "Empire_Development_Tree_Final.json")

    for path in paths:
        if not path.is_file():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        for node in data["nodes"]:
            if node["type"] == "keystone":
                continue
            body = node["data"]
            texts = [body.get("description", "")]
            texts += [o.get("description", "")
                      for o in body.get("options", []) or []]
            if any(MAGNITUDE_WORDING.search(t or "") for t in texts):
                found.append(f"{path.stem.split('_')[0]}/{body['name']}")
    return sorted(found)


def test_the_design_document_permits_the_nodes_that_rely_on_it():
    """WHAT THIS USED TO ASSERT. Until 2026-08-14 this was
    test_basic_nodes_do_not_use_more_or_less_as_a_magnitude, and it forbade a
    basic node from saying "20% more damage", because section IV reserved that
    wording for "gems, passive tree KEYSTONES and enchantments". One Saboteur
    node broke it and was exempted by name in KNOWN_MAGNITUDE_WORDING.

    Issue #344 was answered on 2026-08-14 by widening the rule rather than
    rewording the node: every node in a passive tree may use it. So the old
    assertion now forbids something the design allows, and the property worth
    holding is the reverse one.

    THIS IS NOT A RESTATEMENT OF THE DOCUMENT. It finds the nodes first and only
    then requires the permission, so it fails in both directions: narrowing the
    rule back to keystones fails while those nodes exist, and it stops holding
    on its own if every one of them is reworded.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")

    relying = nodes_relying_on_the_widened_wording_rule()
    if not relying:
        pytest.skip(
            "no node outside a keystone uses 'more' or 'less' as a magnitude, "
            "so nothing depends on the widened rule. If that is deliberate, "
            "this test has nothing left to hold.")

    gdd = GDD.read_text(encoding="utf-8")

    # THE ABSENCE CHECK IS THE LOAD-BEARING ONE. The permitted-sources phrase
    # appears TWICE -- once in section IV and once restated in the affix section
    # -- so requiring the wide form only would pass while one of the two had
    # been narrowed back. An adversarial run of exactly that break is what found
    # this: it changed section IV alone and every assertion still passed.
    assert "passive tree keystones" not in gdd, (
        f"{GDD.name} says the 'more' and 'less' wording is reserved for passive "
        f"tree KEYSTONES. It was widened to all passive nodes on 2026-08-14, "
        f"issue #344, and these {len(relying)} non-keystone nodes rely on the "
        f"wider rule: {relying}. Narrowing it makes every one of them an "
        f"undocumented exception.")
    assert gdd.count("gems, passive tree nodes and enchantments") == 2, (
        f"{GDD.name} states the permitted sources for 'more' and 'less' in two "
        f"places -- section IV and the affix section that restates it -- and "
        f"{gdd.count('gems, passive tree nodes and enchantments')} of them now "
        f"say 'gems, passive tree nodes and enchantments'. Both have to, or a "
        f"reader arriving at one of them gets a different rule. Issue #344.")
    assert "Every node in a passive tree may use that wording" in gdd, (
        f"section IV of {GDD.name} names the permitted sources without saying "
        f"plainly that basic nodes and capstones are included. That sentence "
        f"stops the next reader taking 'passive tree nodes' as shorthand for "
        f"keystones, which is what the rule used to say. Issue #344.")


def test_the_widened_rule_is_actually_relied_on():
    """A permission nothing uses is worth deleting, and the test above skips
    when nothing uses it. This says the skip is not the normal case, so a silent
    skip cannot quietly become the way that test always ends.

    PINNED EXACTLY rather than as a floor. A floor is what went wrong in issue
    #550: a number chosen once, never revisited, and eventually so far below the
    real count that the check could not fail. An exact count cannot rot that
    way, and the cost is a deliberate failure whenever a node is added or
    reworded -- which is the moment somebody should look at whether the rule
    still earns its place.

    It takes no tree fixture on purpose: the count is over all five trees at
    once, so running it per tree would assert the same thing four times."""
    relying = nodes_relying_on_the_widened_wording_rule()
    assert len(relying) == NODES_RELYING_ON_THE_WIDENED_RULE, (
        f"{len(relying)} non-keystone node(s) use 'more' or 'less' as a "
        f"magnitude, where {NODES_RELYING_ON_THE_WIDENED_RULE} did when issue "
        f"#344 widened the rule for them on 2026-08-14. Found: {relying}.\n\n"
        f"If a node was added, update the count here and check the new node "
        f"really wants a separate multiplier rather than 'increased'. If one "
        f"was reworded away, update the count and ask whether the widened rule "
        f"still has enough relying on it to be worth keeping.")


def test_every_node_has_a_name_and_a_description(tree):
    """An unnamed or undescribed node is a hole in the design that the file
    format will happily hold."""
    name, data = tree
    for node in data["nodes"]:
        assert node["data"]["name"].strip(), f"{name}: node {node['id']} has no name"
        assert node["data"]["description"].strip(), (
            f"{name}: node {node['id']} ({node['data']['name']!r}) has no "
            f"description, so nothing says what taking it does.")


# --------------------------------------------------------------------------
# The capstone tiers
# --------------------------------------------------------------------------

def test_the_capstone_tiers_are_twenty_five_fifty_one_hundred_and_two_hundred(
        tree):
    """The four thresholds are fixed by the design document. The three March
    trees record them in the capstone's description text; the Masochist tree
    records them in the `threshold` field as well, which is the field the
    editor reads. Either satisfies this, because the three older files cannot
    be re-exported without opening them in the editor."""
    name, data = tree
    found = []
    for node in nodes_of(data, "capstone"):
        threshold = node["data"].get("threshold")
        if threshold is None:
            matched = re.search(r"(\d+)\s+points", node["data"]["description"])
            assert matched, (
                f"{name}: capstone {node['data']['name']!r} states no point "
                f"threshold, in a `threshold` field or in its description.")
            threshold = int(matched.group(1))
        found.append(threshold)
    assert sorted(found) == list(CAPSTONE_THRESHOLDS), (
        f"{name}: capstone tiers are {sorted(found)}, not "
        f"{list(CAPSTONE_THRESHOLDS)}.")


# --------------------------------------------------------------------------
# The Demonic tree, added by issue #63
#
# The vertical slice in section XV of the design document targets the Demonic
# Cataclysm, so it needs a Demonic class tree. The three that existed are
# Berserker, Bulwark and Saboteur, which are all War classes.
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def demonic() -> dict:
    return load(DEMONIC_TREE)


def test_a_demonic_class_tree_exists(demonic):
    """The Phase 1 line this file was written for. Before #63 the three trees
    in docs/ were all War classes and the slice had no tree to ship."""
    assert demonic["metadata"]["name"] == "Masochist Class Tree"


def test_the_masochist_capstones_offer_three_named_choices_each(demonic):
    """The design document: capstones are "One per tier (25/50/100/200 pts).
    Player chooses one of three options per tier."

    The three March trees say "Choose one of three oaths" in the description
    and record the oaths nowhere, so what the choices ARE is lost. This tree
    puts them in the `options` field, which is what the editor's decision node
    support (`isDecision` in src/types/nodes.ts) is for, and what
    Empire_Development_Tree_Final.json already does."""
    capstones = nodes_of(demonic, "capstone")
    assert len(capstones) == 4
    for node in capstones:
        assert node["data"].get("isDecision") is True, (
            f"Masochist capstone {node['data']['name']!r} is not marked as a "
            f"decision, so the editor will not offer its options.")
        options = node["data"].get("options") or []
        assert len(options) == 3, (
            f"Masochist capstone {node['data']['name']!r} offers "
            f"{len(options)} choices, not 3.")
        for option in options:
            assert option["name"].strip() and option["description"].strip(), (
                f"Masochist capstone {node['data']['name']!r} has a choice "
                f"with no name or no description.")


def test_the_masochist_resource_is_anguish_and_the_tree_unlocks_it(demonic):
    """The design document requires every class to have a resource that "the
    passive tree unlocks and develops", and says they "are not optional stat
    bars, they are the engine of the build". Before this tree the Masochist had
    none: its prose said abilities cost health instead of mana, which is a
    substitution rather than a resource."""
    unlock = [n for n in demonic["nodes"] if n["data"]["name"] == "Fervour"]
    assert len(unlock) == 1, (
        "the Masochist tree has no single node named 'Fervour'. That node is "
        "what grants this class its way of filling the shared resource, and "
        "the design document requires the tree to grant it.")
    assert unlock[0]["data"]["maxPoints"] == 1, (
        "the Fervour node holds more than one point. Granting a way to fill "
        "resource happens once; Bulwark's Resolve node is the same shape.")


def test_anguish_has_the_two_generators_the_owner_decided_on(demonic):
    """THE CONSTRAINT THIS TREE WAS BUILT AROUND, and the one most likely to be
    lost, because it was decided outside the tree.

    `docs/DECISIONS.md`, 2026-08-04, "DECISION 4: the Masochist resource has
    two generators, and the passive tree decides which one a build leans on".
    Three shapes were put to the project owner -- built by taking damage, built
    by spending health, or no resource with power scaling off missing health --
    and the answer was BOTH generators. Everything else about the resource was
    left to this tree.

    A later edit that drops one generator would make the resource a straight
    copy of Bulwark's Resolve and would contradict a recorded decision."""
    unlock = next(n for n in demonic["nodes"] if n["data"]["name"] == "Fervour")
    rule = " ".join(unlock["data"]["description"].split())
    assert "lost to damage" in rule, (
        "the Fervour node no longer says the resource is generated by health "
        "lost to damage. That is one of the two generators the project owner "
        "decided on. See DECISION 4 in docs/DECISIONS.md, 2026-08-04.")
    assert "spent as an ability cost" in rule, (
        "the Fervour node no longer says the resource is generated by health "
        "spent as an ability cost. That is the second of the two generators "
        "the project owner decided on, and it is the half that makes the "
        "resource different from Bulwark's Resolve. See DECISION 4 in "
        "docs/DECISIONS.md, 2026-08-04.")


def test_both_generators_can_be_developed_separately_in_the_tree(demonic):
    """The other half of DECISION 4: "the passive tree decides which one a
    build leans on". Two generators nothing can scale separately is one
    generator with extra words, so the tree has to carry nodes for each."""
    descriptions = [" ".join(n["data"]["description"].split())
                    for n in demonic["nodes"]]
    from_damage = [d for d in descriptions
                   if "Fervour gained from health lost to damage" in d]
    from_cost = [d for d in descriptions
                 if "Fervour gained from health spent" in d]
    assert len(from_damage) >= 2, (
        "fewer than two nodes scale the Fervour gained from health lost to "
        "damage. DECISION 4 requires the tree to be what decides which "
        "generator a build leans on.")
    assert len(from_cost) >= 2, (
        "fewer than two nodes scale the Fervour gained from health spent as "
        "an ability cost. DECISION 4 requires the tree to be what decides "
        "which generator a build leans on.")


def test_the_tree_says_what_decays_anguish(demonic):
    """Left open by DECISION 4 and settled here: healing removes it, at the
    same rate that losing health grants it. It is the only generator in the game
    whose emptying the player controls -- the Bulwark's Resolve and the
    Berserker's Fury empty on a timer out of combat, and the Saboteur's
    Preparation adds no emptying rule at all.

    It is also the rule that makes the Masochist's stat line make sense. It has
    by far the largest health regeneration of the three Demonic classes, 37.6
    per second against 2,526 maximum health, which is 1.49% per second, so a
    full pool of 100 empties in about 67 seconds standing still.

    THE POOL SIZE IS NO LONGER STATED ON THE NODE, and that assertion was
    removed rather than reworded. Since 2026-08-25 there is one shared resource,
    Fervour, and one pool -- 100 for every class, 150 for the Ritualist, in
    game/Data/ClassStats.csv. Four starting nodes each stating a base of 100
    would be four statements of one number. The node states the generator and
    what that generator adds about emptying, which is all it now owns.
    """
    unlock = next(n for n in demonic["nodes"] if n["data"]["name"] == "Fervour")
    rule = " ".join(unlock["data"]["description"].split())
    assert "healing removes Fervour at the same rate" in rule, (
        "the Masochist's Fervour node no longer says what empties the resource "
        "for this "
        "class. Healing does, at 1 Fervour per 1% of maximum health restored. "
        "Fervour does not decay on its own, so without this rule a Masochist's "
        "bar would sit at maximum forever.")
    assert "1 per 1% of maximum health restored" in rule, (
        "the Masochist's Fervour node no longer states the rate at which healing "
        "removes "
        "Fervour. It is the same rate losing health grants it, which is what "
        "makes healing and staying powerful the same resource spent twice.")


def test_the_gdd_class_resource_table_names_the_masochist_resource(demonic):
    """The design document's Class Resource Systems table listed only the three
    War classes. A reader of that table would conclude the Masochist has no way
    to fill the resource, which was true until issue #63.

    WHAT IT MATCHES CHANGED ON 2026-08-25 and the reason is worth keeping. The
    table used to give each class a resource of its own, so this asserted the row
    read "Masochist (Demonic) | Anguish". There is one resource now, called
    Fervour, and the table says how each class fills it. Nothing is named
    Anguish, Fury, Resolve or Preparation any more: keeping a name for the
    generator would put a word in front of the player that no bar bears.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    document = " ".join(GDD.read_text(encoding="utf-8").split())
    assert "Masochist (Demonic) | 1 per 1% of maximum health lost to damage" in document, (
        "the Class Resource Systems table in docs/Cataclysm_GDD_v2.md does "
        "not have a row saying how the Masochist fills Fervour. Every class "
        "needs a way to fill it and the table is where a reader looks. "
        "Issue #63.")
    # A TABLE CELL, NOT THE BARE WORD. The document explains that nothing is
    # called Anguish any more, and a check for the bare word fails on the
    # sentence that says so. A cell is where a name would actually be given.
    for gone in ("Anguish", "Fury", "Resolve", "Preparation"):
        assert f"| {gone} |" not in document, (
            f"docs/Cataclysm_GDD_v2.md has a table cell naming {gone!r}. Every "
            f"class shares one resource called Fervour since 2026-08-25, and a "
            f"second name for it is the confusion that change removed.")


# ---------------------------------------------------------------------------
# One magnitude word per number
# ---------------------------------------------------------------------------
#
# Issue #582. Two nodes said "increased by X% more", using both magnitude words
# for a single number: the Saboteur keystone Overwhelming Presence and the
# Bulwark capstone option Thornwall. Answered on 2026-08-14 by choosing one word
# for each rather than one word for both -- Overwhelming Presence keeps "more"
# because it is one conditional keystone multiplier, and Thornwall keeps
# "increased" because it is a ten-stack debuff that belongs in the additive
# bucket. docs/DECISIONS.md carries the reasoning and the genre sources.


def every_tree_string() -> list[tuple[str, str]]:
    """Every node and capstone-option description across the five trees.

    Returned with the file it came from, so a failure names where to look.
    """
    strings: list[tuple[str, str]] = []
    paths = sorted(DOCS.glob("*_Class_Tree_Final.json"))
    paths.append(DOCS / "Empire_Development_Tree_Final.json")

    for path in paths:
        if not path.is_file():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        for node in data["nodes"]:
            body = node["data"]
            strings.append((path.name, body.get("description", "") or ""))
            for option in body.get("options", []) or []:
                strings.append((path.name, option.get("description", "") or ""))
    return strings


def test_no_node_uses_both_magnitude_words_for_one_number():
    """The fault issue #582 reported, stated as a property rather than by name.

    Naming the two nodes would pass the moment somebody wrote a third. This
    looks for the shape, so it catches the next one as well.
    """
    strings = every_tree_string()
    if not strings:
        pytest.skip("no class tree JSON files are present")

    offenders = [f"{name}: {text}"
                 for name, text in strings
                 if BOTH_MAGNITUDE_WORDS.search(text)]

    assert not offenders, (
        "a passive tree node uses both magnitude words for one number, so it "
        "does not say whether the number joins the additive bucket or is its "
        "own multiplier. The two readings differ by a large factor on an "
        "invested character. Pick one word. Issue #582.\n  "
        + "\n  ".join(offenders))


def test_the_measured_wording_counts_are_still_right():
    """The counts in the comment on MAGNITUDE_WORDING were both wrong.

    It claimed twenty-five strings contained the word and ten were magnitudes;
    the real figures were twenty-nine and twenty. A number written into a
    comment and never checked is a number that drifts, and this one was used to
    argue that the pattern was tight enough.
    """
    strings = every_tree_string()
    if not strings:
        pytest.skip("no class tree JSON files are present")

    bare = re.compile(r"\b(?:more|less)\b", re.IGNORECASE)
    containing = sum(1 for _, text in strings if bare.search(text))
    magnitudes = sum(1 for _, text in strings if MAGNITUDE_WORDING.search(text))

    assert containing == STRINGS_CONTAINING_THE_WORD, (
        f"{containing} tree strings contain the word 'more' or 'less', and "
        f"STRINGS_CONTAINING_THE_WORD says {STRINGS_CONTAINING_THE_WORD}. "
        f"Update it, and check the pattern still separates the magnitudes from "
        f"ordinary English.")

    assert magnitudes == STRINGS_USING_IT_AS_A_MAGNITUDE, (
        f"{magnitudes} tree strings use it as a percentage magnitude, and "
        f"STRINGS_USING_IT_AS_A_MAGNITUDE says "
        f"{STRINGS_USING_IT_AS_A_MAGNITUDE}. Update it.")
