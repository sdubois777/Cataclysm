"""A preset built with `replace` still is what its own comment says. Issue #1421.

WHAT WENT WRONG, ON 2026-09-07. `TREE_EXPLORER_DAY_NODES_ONLY` in
`sim/analyse_surge_cadence.py` is `replace(TREE_EXPLORER_AS_DESIGNED,
floor_delta=0.0)`, and its comment calls it "the four unconditional day-removal
nodes and none of its five depth nodes. 56 of the branch's 316 points."

Issue #1397 added two per-active-Cataclysm-type fields to the preset it copies.
**`dataclasses.replace` carries across every field it is not told to change**, so
the sub-build inherited both: a fifth day-removal node worth ten more points, and
+20 floors at difficulty tier 1 rising to +160 at tier 8, when having none is the
entire point of it.

**Nothing failed.** It kept running and kept producing figures under a comment
that had become false, and it was found by a person reading the diff. That is
what this file is for.

HOW IT GUARDS, IN TWO PARTS, BECAUSE ONE WOULD NOT BE ENOUGH:

  1. **It finds the derived presets itself**, by parsing the simulation's source
     rather than being handed a list. A new one added tomorrow is discovered, and
     `test_every_derived_preset_has_a_claim_recorded` fails until somebody writes
     down what its comment promises. A file that only checked the presets
     somebody remembered would go quiet exactly when a second one appeared.
  2. **For each one, it asserts the claim.** The interesting assertion is not
     "these fields have these values" -- that is the definition restated -- but
     **which fields it INHERITED from its base without being asked**, which is
     the thing `replace` changes silently.

WHY THE INHERITED-FIELD SET IS THE ONE THAT MATTERS. Everything else here would
survive the incident: the values a preset overrides stay exactly as written, and
a test comparing them to literals passes. What moved was a field nobody had
mentioned in either place. So the set of fields the copy takes from its base is
written down, and it growing is a failure with the new field named.

**THERE IS EXACTLY ONE DERIVED PRESET IN THE PROJECT TODAY** and this file says
so rather than pretending to a generality it does not have. Issue #1420 proposes
removing that one by making it a shipped preset; if that lands and no other
appears, `test_every_derived_preset_has_a_claim_recorded` is what will say this
file has nothing left to guard.
"""

from __future__ import annotations

import ast
import json
import pathlib
import re
from dataclasses import fields

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SIM = REPO_ROOT / "sim"
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: Every number of active Cataclysm types a campaign can face, which is what
#: `TuningConfig.active_cataclysm_count` clamps the difficulty tier to.
ACTIVE_COUNTS = (1, 2, 3, 4, 5, 6, 7, 8)


# ---------------------------------------------------------------------------
# Finding them
# ---------------------------------------------------------------------------

class Derived:
    """A module-level preset built by copying another one."""

    def __init__(self, path, lineno, name, base, overridden, comment):
        self.path, self.lineno = path, lineno
        self.name, self.base = name, base
        self.overridden = overridden
        self.comment = comment

    def __repr__(self):
        return f"{self.path}:{self.lineno} {self.name} = replace({self.base}, ...)"


def comment_above(lines: list[str], lineno: int) -> str:
    """The run of comment lines immediately above a statement, flattened.

    FLATTENED BECAUSE THE COMMENTS ARE HARD-WRAPPED. A phrase longer than one
    line is not present in the raw text, so a search for one would report a
    clean file that is not clean -- which has cost this project a wrong answer
    more than once.
    """
    out = []
    index = lineno - 2                      # `lineno` is 1-based; go up one
    while index >= 0 and lines[index].lstrip().startswith("#"):
        out.append(lines[index].lstrip().lstrip("#").lstrip(":").strip())
        index -= 1
    return re.sub(r"\s+", " ", " ".join(reversed(out)))


def find_derived_presets() -> list[Derived]:
    """Every module-level `NAME = replace(OTHER, ...)` in the simulation.

    MODULE LEVEL ONLY, AND THAT IS DELIBERATE. A `replace` inside a function
    builds one config for one measurement and is read at the point of use; a
    module-level one is a named thing with a comment that other code and other
    people rely on. This issue is about the second.
    """
    out = []
    for path in sorted(SIM.rglob("*.py")):
        if "tests" in path.parts or "__pycache__" in path.parts:
            continue
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        for node in ast.parse(text).body:
            if not isinstance(node, ast.Assign):
                continue
            call = node.value
            if not (isinstance(call, ast.Call)
                    and isinstance(call.func, ast.Name)
                    and call.func.id == "replace"
                    and call.args):
                continue
            target = node.targets[0]
            if not isinstance(target, ast.Name):
                continue
            out.append(Derived(
                path=path.relative_to(REPO_ROOT).as_posix(),
                lineno=node.lineno,
                name=target.id,
                base=ast.unparse(call.args[0]),
                overridden=frozenset(k.arg for k in call.keywords if k.arg),
                comment=comment_above(lines, node.lineno)))
    return out


def load(derived: Derived):
    """The preset object itself, and the one it was copied from."""
    import importlib
    import sys

    if str(SIM) not in sys.path:
        sys.path.insert(0, str(SIM))

    module_name = (derived.path.removeprefix("sim/").removesuffix(".py")
                   .replace("/", "."))
    module = importlib.import_module(module_name)
    return getattr(module, derived.name), getattr(module, derived.base)


def inherited(base, overridden: frozenset) -> frozenset:
    """Fields the copy took from its base WITHOUT BEING ASKED.

    A field the base leaves at the dataclass default is not inherited in any
    sense that matters -- the copy would have had that value anyway. What counts
    is a field the base sets and the copy does not mention.
    """
    return frozenset(
        f.name for f in fields(type(base))
        if f.name not in overridden and getattr(base, f.name) != f.default)


# ---------------------------------------------------------------------------
# What each one's comment claims
# ---------------------------------------------------------------------------

#: One entry per derived preset, keyed by name. `inherits` is the set of fields
#: it is allowed to take from its base without saying so; `phrases` are
#: sentences its comment must still contain, because the comment is the only
#: description a reader gets and it is hand-typed.
#:
#: **ADDING A PRESET HERE IS THE POINT OF THE EXERCISE**, not a chore around it:
#: writing down what a copy promises is what makes the promise checkable.
CLAIMS = {
    "TREE_EXPLORER_DAY_NODES_ONLY": {
        #: The four nodes it is made of, in whichever field currently carries
        #: them. **THIS MOVED ONCE, LEGITIMATELY, ON 2026-09-07** -- see the
        #: note below.
        "inherits": frozenset({"run_days_mult"}),
        "phrases": (
            "removes no flat days at all and its speed is entirely",
            "the sub-build is the same 56 points it always was",
            "56 of the branch's 316 points",
        ),
    },
}

#: **CHANGING AN `inherits` SET IS A DECISION, NOT A MAINTENANCE CHORE.**
#:
#: When `test_it_inherits_only_the_fields_its_claim_allows` fails, the reflex is
#: to make the recorded set match what the preset now inherits. **That reflex
#: reproduces the exact defect this file exists to catch**: the 2026-09-07
#: incident was a copy that silently gained fields, and a set edited to match
#: would have blessed it.
#:
#: The question to answer first is the one the failure cannot answer for you: is
#: the copy still what its comment says? Only if it is does the set follow.
#:
#: IT HAS MOVED ONCE AND HERE IS THE WORKED CASE, so the next reader has a
#: standard to judge against rather than a rule with no example.
#:
#: `b4799a2` ruled the Explorer branch's four unconditional walk-time nodes a
#: PERCENTAGE of run time rather than a flat number of days -- the project owner
#: on 2026-09-06, verbatim "Change to a percentage", issue #1383. So
#: `TREE_EXPLORER_AS_DESIGNED.run_days_flat` went to 0.0 and
#: `run_days_mult` became `0.975 ** 55 * 0.88`, which `config.py` documents as
#: Temporal Mastery 25 points, Overclock 20 and Pacing 10 at -2.5% each, plus
#: the Fleet Footed keystone at -12%. **Fifty-six points: exactly the four nodes
#: this sub-build is defined as, and nothing else.**
#:
#: So the set moved from `run_days_flat` to `run_days_mult` **because the field
#: carrying those four nodes moved, not because the copy gained or lost any.**
#: `run_days_flat` left it for the honest reason that the base stopped setting
#: it at all.
#:
#: **THE CONTRAST WITH THE DEFECT IS THE WHOLE TEST.** On 2026-09-07 the copy
#: gained nodes it is documented as NOT having. Here it kept the ones it is
#: documented as having, through a rename it did not ask for. A set edited
#: without establishing which of those two happened is worth nothing.


@pytest.fixture(scope="module")
def derived_presets() -> list[Derived]:
    found = find_derived_presets()
    assert found, (
        "no module-level preset built with `replace` was found anywhere in "
        "sim/. Either the search broke or the last one was removed; if it is "
        "the second, issue #1420 did it and this file has nothing left to "
        "guard. Do not delete the search to make this pass.")
    return found


@pytest.fixture(scope="module")
def graph() -> list[dict]:
    with open(TREE_JSON, encoding="utf-8") as handle:
        return json.load(handle)["nodes"]


def points_of(nodes: list[dict], name: str) -> int:
    for node in nodes:
        if node["data"].get("name") == name:
            return node["data"]["maxPoints"]
    raise AssertionError(f"'{name}' is no longer a node in {TREE_JSON.name}")


def explorer_branch_points(nodes: list[dict]) -> int:
    """Every point the Explorer branch holds. The south-east quadrant; the
    graph's own `metadata.description` says which quadrant is which."""
    total = 0
    for node in nodes:
        if node["data"].get("kind") == "capstone":
            continue
        if node["position"]["y"] > 0 and node["position"]["x"] > 0:
            total += node["data"].get("maxPoints") or 0
    return total


# ---------------------------------------------------------------------------
# The tests
# ---------------------------------------------------------------------------

def test_every_derived_preset_has_a_claim_recorded(derived_presets):
    """**THE PART THAT KEEPS THIS FILE HONEST AS THE PROJECT GROWS.**

    A guard over a hand-written list stops covering the code the moment somebody
    adds a second case. This finds them and refuses to pass on one nobody has
    described.
    """
    found = {d.name for d in derived_presets}
    described = set(CLAIMS)

    undescribed = sorted(found - described)
    assert not undescribed, (
        f"these presets are built with `replace` and nothing records what they "
        f"claim: {undescribed}. `dataclasses.replace` carries across every "
        "field it is not told to change, so a copy silently gains whatever is "
        "later added to its base -- which is what happened on 2026-09-07 and "
        "is why this file exists. Read the preset's comment, write down what "
        "it promises in CLAIMS, and the promise becomes checkable.")

    stale = sorted(described - found)
    assert not stale, (
        f"CLAIMS describes {stale}, which no longer exists. If it was removed "
        "-- issue #1420 proposes exactly that -- take the entry out.")


@pytest.mark.parametrize("preset_name", sorted(CLAIMS))
def test_it_inherits_only_the_fields_its_claim_allows(
        derived_presets, preset_name):
    """**THE ASSERTION THE 2026-09-07 INCIDENT WOULD HAVE FAILED.**

    Not "its fields have these values", which is the definition restated and
    which passed throughout the incident. The fields it OVERRIDES are exactly as
    written by definition; what moved was a field nobody mentioned in either
    place, added to the base and inherited in silence.
    """
    derived = next(d for d in derived_presets if d.name == preset_name)
    preset, base = load(derived)

    actual = inherited(base, derived.overridden)
    allowed = CLAIMS[preset_name]["inherits"]

    leaked = sorted(actual - allowed)
    assert not leaked, (
        f"{preset_name} now inherits {leaked} from {derived.base} without "
        f"asking for it. `replace` copies what it is not told to change, so a "
        f"field added to {derived.base} lands here silently and this preset "
        "stops being what its comment says. Either override it at "
        f"{derived.path}:{derived.lineno}, or -- if it genuinely belongs -- add "
        "it to CLAIMS here and say so in the comment.")

    gone = sorted(allowed - actual)
    assert not gone, (
        f"{preset_name} no longer inherits {gone}, which its claim says it "
        "does. If the base stopped setting that field, this preset may have "
        "quietly become something else in the other direction.")


def test_the_sub_build_is_the_same_player_at_every_tier(derived_presets):
    """Its comment says "the four unconditional nodes and nothing else, at every
    tier", and that is what makes the tier 1 and tier 4 worlds in
    `analyse_surge_cadence.py` comparable at all.

    **EVERY EFFECT, NOT ONLY THE TWO THAT WENT WRONG.** A future
    per-active-Cataclysm-type field on any of the three accessors would break
    this without being named anywhere.
    """
    derived = next(d for d in derived_presets
                   if d.name == "TREE_EXPLORER_DAY_NODES_ONLY")
    preset, _base = load(derived)

    for effect, call in (("days removed", preset.days_removed),
                         ("floors added", preset.floors_added),
                         ("damage taken", preset.damage_taken)):
        answers = {call(n) for n in ACTIVE_COUNTS}
        assert len(answers) == 1, (
            f"{derived.name} gives {sorted(answers)} for {effect} across the "
            "eight active Cataclysm counts. It is defined as the four "
            "unconditional nodes, which do not vary with the tier, so a second "
            "answer means it has taken on a per-active-type node. Issue #1421.")

    assert preset.floors_added(1) == 0.0, (
        f"{derived.name} adds {preset.floors_added(1):+g} floors and is "
        "defined as having none of the branch's five depth nodes. This is the "
        "exact defect of 2026-09-07: it had +20 at tier 1 and +160 at tier 8.")


def test_the_point_totals_its_comment_states_are_the_graphs(
        derived_presets, graph):
    """**THE TYPED COPY, CHECKED AGAINST THE DESIGN DOCUMENT.**

    "56 of the branch's 316 points" is hand-typed prose, and prose is what goes
    stale while code keeps working. Both halves come out of
    `docs/Empire_Development_Tree_Final.json`.
    """
    derived = next(d for d in derived_presets
                   if d.name == "TREE_EXPLORER_DAY_NODES_ONLY")

    four = ("Temporal Mastery", "Overclock", "Pacing", "Fleet Footed")
    cheap = sum(points_of(graph, name) for name in four)
    branch = explorer_branch_points(graph)

    assert cheap == 56, (
        f"the four unconditional day-removal nodes cost {cheap} points in the "
        f"design document, and the comment at {derived.path}:{derived.lineno} "
        "says 56.")
    assert branch == 316, (
        f"the Explorer branch holds {branch} points in the design document, "
        "and that comment says 316.")

    assert f"{cheap} of the branch's {branch} points" in derived.comment, (
        f"the comment above {derived.name} no longer states "
        f"'{cheap} of the branch's {branch} points'. The graph gives those two "
        "numbers; follow them in the comment rather than deleting this check.")


@pytest.mark.parametrize("preset_name", sorted(CLAIMS))
def test_its_comment_still_says_what_the_claim_quotes(
        derived_presets, preset_name):
    """Each phrase `CLAIMS` relies on is still in the comment it came from.

    Without this, a comment could be rewritten to promise something else while
    every check above went on testing the old promise.
    """
    derived = next(d for d in derived_presets if d.name == preset_name)

    # THE POSITIVE CONTROL. An empty comment would satisfy nothing below and
    # would mean the extraction is broken rather than the comment wrong.
    assert len(derived.comment) > 100, (
        f"no comment was found above {preset_name} at {derived.path}:"
        f"{derived.lineno}; the extraction is reading the wrong lines")

    for phrase in CLAIMS[preset_name]["phrases"]:
        assert phrase in derived.comment, (
            f"the comment above {preset_name} no longer says {phrase!r}, and "
            "the checks in this file rest on it. If the preset changed, change "
            "the checks; if the wording changed, follow it here.")


def test_the_search_finds_the_one_that_exists(derived_presets):
    """**THE CONTROL ON THE SEARCH ITSELF.**

    Everything above rests on the parse finding the presets. A parse that found
    nothing would make most of this file vacuous, and the fixture's own
    assertion would be the only thing standing between that and a green run.
    """
    names = {d.name for d in derived_presets}
    assert "TREE_EXPLORER_DAY_NODES_ONLY" in names, (
        f"the search found {sorted(names)} and not the one derived preset the "
        "project is known to have. Either it moved out of sim/, or it stopped "
        "being built with `replace`, or the parse is broken.")

    derived = next(d for d in derived_presets
                   if d.name == "TREE_EXPLORER_DAY_NODES_ONLY")
    assert derived.overridden, "the search read no overridden fields"
    assert derived.base.startswith("TREE_"), (
        f"it is built from {derived.base!r}, which is not a preset")
