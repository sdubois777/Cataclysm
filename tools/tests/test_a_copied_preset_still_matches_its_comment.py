"""A preset built with `replace` still is what its own comment says. Issue #1421.

**THERE ARE NONE IN THE PROJECT TODAY, AND THIS FILE SAYS SO OUT LOUD RATHER
THAN REPORTING GREEN ON AN EMPTY SET.** Read that first, because it changes what
every check below is worth: the checks that examine a real preset examine
nothing, and `test_there_are_no_copied_presets_in_the_simulation_today` is the
one that states it. What is still live is the search that finds a copied preset,
the machinery that judges one, and a proof of each against a synthetic case.

WHAT WENT WRONG, ON 2026-09-07, WHICH IS WHY ANY OF THIS EXISTS.
`TREE_EXPLORER_DAY_NODES_ONLY` was `replace(TREE_EXPLORER_AS_DESIGNED,
floor_delta=0.0)` in `sim/analyse_surge_cadence.py`, and its comment called it
"the four unconditional day-removal nodes and none of its five depth nodes. 56
of the branch's 316 points."

Issue #1397 added two per-active-Cataclysm-type fields to the preset it copies.
**`dataclasses.replace` carries across every field it is not told to change**, so
the sub-build inherited both: a fifth day-removal node worth ten more points, and
+20 floors at difficulty tier 1 rising to +160 at tier 8, when having none is the
entire point of it.

**Nothing failed.** It kept running and kept producing figures under a comment
that had become false, and it was found by a person reading the diff. That is
what this file was for.

WHAT HAPPENED TO IT, ON 2026-09-07 AS WELL. Issue #1416 repaired the copy and
issue #1420 removed it: `TREE_EXPLORER_DAY_NODES_ONLY` is now a shipped preset in
`sim/cataclysm_sim/config.py` with all twelve of its fields written out, so no
neighbouring preset can reach it. The checks that were about that preset rather
than about copying moved with it, to
`tools/tests/test_the_cheap_explorer_preset_matches_its_own_description.py` --
the 56-of-316 point totals, the depth nodes it excludes, and its being the same
player at every tier. Nothing was dropped.

**WHY THIS FILE WAS NOT DELETED WITH IT.** The defect was in the idiom, not in
that one preset, and the idiom is one line long and free to reintroduce. What
survives here is:

  1. **The search**, which finds any module-level `NAME = replace(OTHER, ...)` in
     `sim/` by parsing the source. It is proved against a synthetic file below,
     because a search with nothing to find is otherwise indistinguishable from a
     search that is broken -- and a broken one reports a clean tree.
  2. **The judgement**, `inherited()`, which asks not "do these fields hold these
     values" -- that is the definition restated, and it passed throughout the
     incident -- but **which fields the copy took from its base WITHOUT BEING
     ASKED**, which is the thing `replace` changes silently. It is proved below
     against a miniature of the 2026-09-07 incident.
  3. **The standard for judging a copy that changes shape**, in the long comment
     above `CLAIMS`, with the one worked example this project has.

So a copied preset added tomorrow is found, and
`test_every_derived_preset_has_a_claim_recorded` fails until somebody writes down
what its comment promises.
"""

from __future__ import annotations

import ast
import pathlib
import re
from dataclasses import dataclass, fields, replace

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SIM = REPO_ROOT / "sim"


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


def find_derived_presets(root: pathlib.Path = SIM) -> list[Derived]:
    """Every module-level `NAME = replace(OTHER, ...)` under `root`.

    MODULE LEVEL ONLY, AND THAT IS DELIBERATE. A `replace` inside a function
    builds one config for one measurement and is read at the point of use; a
    module-level one is a named thing with a comment that other code and other
    people rely on. This issue is about the second.

    **`root` IS A PARAMETER SO THE SEARCH CAN BE PROVED.** With no real copied
    preset left in `sim/`, a search that returned nothing because it was broken
    would look exactly like the desired state. The tests below point it at a
    synthetic file and require it to find what is there.
    """
    out = []
    for path in sorted(root.rglob("*.py")):
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
                path=path.relative_to(root).as_posix(),
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

    module_name = derived.path.removesuffix(".py").replace("/", ".")
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
#: **IT IS EMPTY BECAUSE THERE ARE NO COPIED PRESETS LEFT**, not because nobody
#: has filled it in. `TREE_EXPLORER_DAY_NODES_ONLY` was the only entry and issue
#: #1420 made it a shipped preset written out field by field, so there is nothing
#: for it to describe. The two tests that walk it therefore walk an empty list
#: today; `test_there_are_no_copied_presets_in_the_simulation_today` is what says
#: so, and it fails the moment that stops being true.
#:
#: **ADDING A PRESET HERE IS THE POINT OF THE EXERCISE**, not a chore around it:
#: writing down what a copy promises is what makes the promise checkable.
CLAIMS: dict[str, dict] = {}

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
#: standard to judge against rather than a rule with no example. **IT IS KEPT
#: THOUGH THE PRESET IT HAPPENED TO IS GONE**, because it is the only worked
#: example this project has of the judgement, and the next copied preset will
#: need it.
#:
#: `b4799a2` ruled the Explorer branch's four unconditional walk-time nodes a
#: PERCENTAGE of run time rather than a flat number of days -- the project owner
#: on 2026-09-06, verbatim "Change to a percentage", issue #1383. So
#: `TREE_EXPLORER_AS_DESIGNED.run_days_flat` went to 0.0 and
#: `run_days_mult` became `0.975 ** 55 * 0.88`, which `config.py` documents as
#: Temporal Mastery 25 points, Overclock 20 and Pacing 10 at -2.5% each, plus
#: the Fleet Footed keystone at -12%. **Fifty-six points: exactly the four nodes
#: that sub-build is defined as, and nothing else.**
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
#:
#: That worked example now also lives beside the preset it is about, in
#: `tools/tests/test_the_cheap_explorer_preset_matches_its_own_description.py`,
#: where the multiplier is checked against the four nodes' own descriptions.


@pytest.fixture(scope="module")
def derived_presets() -> list[Derived]:
    """Every copied preset in `sim/`. **EMPTY TODAY**; see the module docstring.

    It carries no "and there must be at least one" assertion, because there is
    not one and that is deliberate. What stops an empty list from meaning a
    broken search is `test_the_search_still_finds_a_copied_preset_when_there_is_one`.
    """
    return find_derived_presets()


# ---------------------------------------------------------------------------
# The state of the project, stated rather than implied
# ---------------------------------------------------------------------------

def test_there_are_no_copied_presets_in_the_simulation_today(derived_presets):
    """**THE TEST THAT KEEPS THIS FILE HONEST ABOUT BEING NEARLY VACUOUS.**

    Two of the tests below walk `CLAIMS`, which is empty, so they check nothing
    at all. A file in that state that simply reported green would be worse than
    no file: it would look like a guard that had passed. This is the assertion
    that makes the emptiness a stated fact rather than a silent one.

    **IT IS NOT A BAN ON `dataclasses.replace`.** A `replace` inside a function,
    building one config for one measurement, is read at the point of use and is
    fine; `sim/analyse_surge_cadence.py` has two. What this refuses is a
    module-level NAMED preset built by copying another, which is the shape that
    went wrong: it acquires a comment, other code relies on it, and it changes
    when its base does without anybody touching it.
    """
    assert derived_presets == [], (
        "these module-level presets are built by copying another one:\n  "
        + "\n  ".join(repr(d) for d in derived_presets)
        + "\n\n`dataclasses.replace` carries across every field it is not told "
          "to change, so a copy silently gains whatever is later added to its "
          "base. That is what happened to TREE_EXPLORER_DAY_NODES_ONLY on "
          "2026-09-07: it gained a fifth day-removal node and +20 floors at "
          "difficulty tier 1, nothing failed, and it went on producing campaign "
          "figures under a comment that had become false.\n\n"
          "Two ways forward, and the first is what issue #1420 chose:\n"
          "  * write the preset out field by field in "
          "`sim/cataclysm_sim/config.py`, so nothing can reach it; or\n"
          "  * keep the copy, record what its comment promises in CLAIMS above, "
          "and change this test to expect it -- deliberately, with the reason "
          "written down.")


def test_every_derived_preset_has_a_claim_recorded(derived_presets):
    """**THE TRIPWIRE FOR THE DAY A SECOND ONE APPEARS.**

    A guard over a hand-written list stops covering the code the moment somebody
    adds a case it does not know about. This finds them by parsing the source and
    refuses to pass on one nobody has described.

    Today both sides are empty and it proves nothing on its own; the test above
    is what says so.
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
        "-- issue #1420 did exactly that to the one entry this file was written "
        "for -- take the entry out.")


# ---------------------------------------------------------------------------
# What each claim asserts. Both walk an empty CLAIMS today.
# ---------------------------------------------------------------------------

def test_it_inherits_only_the_fields_its_claim_allows(derived_presets):
    """**THE ASSERTION THE 2026-09-07 INCIDENT WOULD HAVE FAILED.**

    Not "its fields have these values", which is the definition restated and
    which passed throughout the incident. The fields it OVERRIDES are exactly as
    written by definition; what moved was a field nobody mentioned in either
    place, added to the base and inherited in silence.

    **THIS LOOPS OVER AN EMPTY CLAIMS TODAY AND CHECKS NOTHING.** What keeps
    `inherited()` from rotting in the meantime is
    `test_the_inherited_set_is_the_fields_a_copy_takes_without_asking`, which
    runs it against a miniature of the incident.
    """
    for preset_name, claim in sorted(CLAIMS.items()):
        derived = next(d for d in derived_presets if d.name == preset_name)
        _preset, base = load(derived)

        actual = inherited(base, derived.overridden)
        allowed = claim["inherits"]

        leaked = sorted(actual - allowed)
        assert not leaked, (
            f"{preset_name} now inherits {leaked} from {derived.base} without "
            f"asking for it. `replace` copies what it is not told to change, so "
            f"a field added to {derived.base} lands here silently and this "
            "preset stops being what its comment says. Either override it at "
            f"{derived.path}:{derived.lineno}, or -- if it genuinely belongs -- "
            "add it to CLAIMS here and say so in the comment.")

        gone = sorted(allowed - actual)
        assert not gone, (
            f"{preset_name} no longer inherits {gone}, which its claim says it "
            "does. If the base stopped setting that field, this preset may have "
            "quietly become something else in the other direction.")


def test_its_comment_still_says_what_the_claim_quotes(derived_presets):
    """Each phrase `CLAIMS` relies on is still in the comment it came from.

    Without this, a comment could be rewritten to promise something else while
    every check above went on testing the old promise. **Empty today**; the
    reader that finds the comment is proved separately below.
    """
    for preset_name, claim in sorted(CLAIMS.items()):
        derived = next(d for d in derived_presets if d.name == preset_name)

        # THE POSITIVE CONTROL. An empty comment would satisfy nothing below and
        # would mean the extraction is broken rather than the comment wrong.
        assert len(derived.comment) > 100, (
            f"no comment was found above {preset_name} at {derived.path}:"
            f"{derived.lineno}; the extraction is reading the wrong lines")

        for phrase in claim["phrases"]:
            assert phrase in derived.comment, (
                f"the comment above {preset_name} no longer says {phrase!r}, "
                "and the checks in this file rest on it. If the preset changed, "
                "change the checks; if the wording changed, follow it here.")


# ---------------------------------------------------------------------------
# The machinery, proved against synthetic cases
#
# **THIS IS THE HALF OF THE FILE THAT IS NOT VACUOUS**, and it is why the file
# was kept rather than deleted when its one real subject went away. Everything
# above rests on the search finding a copied preset and on `inherited()` judging
# it. With nothing real to find, a search that was broken would report exactly
# the state the project is actually in.
# ---------------------------------------------------------------------------

def write_module(directory: pathlib.Path, name: str, body: str) -> pathlib.Path:
    path = directory / name
    path.write_text(body, encoding="utf-8")
    return path


def test_the_search_still_finds_a_copied_preset_when_there_is_one(tmp_path):
    """**THE CONTROL ON THE SEARCH ITSELF.** A null result proves nothing until
    the same search has found something."""
    write_module(tmp_path, "sample.py", (
        "from dataclasses import replace\n"
        "\n"
        "BASE = Thing(a=1, b=2)\n"
        "\n"
        "#: The cheap one: b unbought.\n"
        "#: Two lines, so the reader has to flatten them.\n"
        "COPY = replace(BASE, name='cheap', b=0)\n"))

    found = find_derived_presets(tmp_path)

    assert [d.name for d in found] == ["COPY"], (
        f"the search found {[d.name for d in found]} in a file holding exactly "
        "one module-level copied preset")
    assert found[0].base == "BASE"
    assert found[0].overridden == frozenset({"name", "b"})
    assert found[0].path == "sample.py"
    assert "The cheap one: b unbought. Two lines" in found[0].comment, (
        f"the comment reader gave {found[0].comment!r}; it should flatten the "
        "hard-wrapped lines above the statement into one string")


def test_the_search_ignores_a_replace_inside_a_function(tmp_path):
    """Module level only, and that restriction has to be real rather than
    accidental. `sim/analyse_surge_cadence.py` builds configs with `replace`
    inside functions; those are read at the point of use and are not what this
    file is about. A search that flagged them would be turned off."""
    write_module(tmp_path, "sample.py", (
        "from dataclasses import replace\n"
        "\n"
        "def build(cfg):\n"
        "    return replace(cfg, tier=4)\n"
        "\n"
        "def other(cfg):\n"
        "    LOCAL = replace(cfg, tier=8)\n"
        "    return LOCAL\n"))

    assert find_derived_presets(tmp_path) == []


def test_the_search_skips_tests_and_bytecode(tmp_path):
    """Test files construct presets deliberately, including by copying, and are
    not what this covers."""
    (tmp_path / "tests").mkdir()
    write_module(tmp_path / "tests", "test_x.py",
                 "from dataclasses import replace\nCOPY = replace(BASE, b=0)\n")
    assert find_derived_presets(tmp_path) == []


@dataclass
class Sample:
    """A stand-in for `EmpireTree`, at the size the incident needs."""
    name: str = "none"
    speed: float = 1.0
    floors: float = 0.0
    floors_per_type: float = 0.0


def test_the_inherited_set_is_the_fields_a_copy_takes_without_asking():
    """**THE 2026-09-07 INCIDENT, IN MINIATURE, KEPT AS A RUNNING TEST.**

    The real preset it happened to is gone, so this reproduces it: a base with
    one field, a copy that names one field, and then a field ADDED to the base
    that the copy never mentioned. `inherited()` has to report the new one.

    Written out step by step rather than asserted once, because the thing being
    demonstrated is that the copy changes when its base does while its own
    source line does not move.
    """
    base = Sample(name="whole branch", speed=0.5, floors=20.0)
    copy = replace(base, name="cheap", floors=0.0)
    overridden = frozenset({"name", "floors"})

    # Before: the copy takes `speed` and nothing else, which is what its comment
    # would say -- it is the same speed with the floors unbought.
    assert inherited(base, overridden) == frozenset({"speed"})
    assert copy.floors == 0.0

    # Now the base gains a per-type field, exactly as issue #1397 did. The
    # copy's own line has not changed.
    grown = replace(base, floors_per_type=20.0)
    grown_copy = replace(grown, name="cheap", floors=0.0)

    assert grown_copy.floors == 0.0, "the field it overrode is still overridden"
    assert grown_copy.floors_per_type == 20.0, (
        "the copy did NOT gain the new field, so this test is no longer a "
        "model of the incident and proves nothing about `inherited()`")

    assert inherited(grown, overridden) == frozenset({"speed", "floors_per_type"}), (
        "`inherited()` did not report the field the copy gained without asking. "
        "That set is the whole judgement this file makes; everything else it "
        "checks would have passed throughout the 2026-09-07 incident.")


def test_a_field_left_at_its_default_does_not_count_as_inherited():
    """**THE CONTROL ON `inherited()`.** A set that grew every time the dataclass
    did would fail constantly and be edited to match, which is the reflex the
    comment above `CLAIMS` warns against. Only a field the base actually SETS
    counts: the copy would have had the default anyway.
    """
    base = Sample(name="whole branch", speed=0.5)
    assert inherited(base, frozenset({"name"})) == frozenset({"speed"}), (
        "`floors` and `floors_per_type` are at their dataclass defaults on the "
        "base, so the copy takes nothing from it by holding them")


def test_the_comment_reader_stops_at_the_first_non_comment_line():
    """`comment_above` walks up from a statement. If it did not stop, it would
    hand a claim the whole file's leading comments and any phrase check would
    pass on text belonging to something else."""
    lines = ["# unrelated block", "", "SOMETHING = 1", "# the claim", "COPY = 2"]
    assert comment_above(lines, 5) == "the claim"
    assert comment_above(lines, 3) == ""
