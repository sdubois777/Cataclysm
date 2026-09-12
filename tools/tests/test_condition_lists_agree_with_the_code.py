"""The several hand-written lists describing conditions agree with the code.

WHY THIS EXISTS. Issue #1601. A condition a data sheet may name is declared in
one place and described in several others, and nothing held those others
together. Two faults follow from that, and they fail in opposite ways:

    a condition absent from `ConditionHolds`   -> SILENT. The switch falls
        through to `return false`, the modifier is refused, and every row
        carrying that condition grants nothing with no error anywhere.

    a condition absent from `ConditionTakesAValue` -> SILENT. That function
        returns true by default, so the condition is assumed to compare a
        number it has no meaning for.

**Why these are text checks rather than C++ tests.** Continuous integration
builds no C++ at all, so a check needing a build would not run on a pull request
and a fault would reach `development` unnoticed until somebody ran the Unreal
suite by hand. `test_stat_condition_names_match_the_engine.py` beside this file
is a text check for the same reason, and issue #1581 records the drift that
happens without one.

**Read the data structure, not the text that declares it, wherever that is
possible.** `CONDITION_WORDS` is imported rather than pattern-matched: its value
form is the SECOND ELEMENT OF A TUPLE, so a search for `": None"` finds nothing
and reports the form absent. That misreading happened twice in twenty minutes
while this issue was being investigated. The C++ lists have to be parsed, and
every parse below therefore carries a positive control.

WHAT IS ASSERTED HERE.

    every condition kind is judged: it appears as a case label in `ConditionHolds`
    the three lists of "this condition compares nothing" agree with each other
    the word table agrees with them on the conditions it holds, BY INTERSECTION

WHAT IS NOT ASSERTED. That a case label's BODY is right. A case reading the wrong
field, or returning a constant, still has a label. That is a correctness question
and belongs to the C++ tests, which cover it for the conditions that have them.
This closes a gap where nothing existed; it does not replace testing answers.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PIPELINE_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmStatPipeline.h")
PIPELINE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                / "CataclysmStatPipeline.cpp")
PASSIVE_TESTS = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Tests"
                 / "CataclysmPassiveTreeTests.cpp")

sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

import generate_datatables as gen  # noqa: E402
import test_passive_effects_match_the_node_text as node_words  # noqa: E402

#: THE FEWEST OF EACH THAT MUST BE FOUND, AND WHY THIS IS A MINIMUM RATHER THAN
#: AN EXACT COUNT.
#:
#: A parser that matches nothing makes every set empty, and comparing two empty
#: sets reports perfect agreement. That is the same shape of failure this whole
#: file exists to catch, one level up: a check that has gone blind reads exactly
#: like a check that is passing. So each parse below asserts it found at least
#: this many before any comparison is made.
#:
#: THEY ARE FLOORS, NOT PINS, AND THEY ARE DELIBERATELY WELL BELOW TODAY'S
#: COUNTS. `development` held 13 condition kinds, 6 value-less names and 8 word
#: entries when this landed. The floors sit under those on purpose:
#:
#:   a floor asks ONE question -- did the parser see anything sensible at all
#:   a floor at today's count asks a SECOND one -- has coverage shrunk
#:
#: One assertion answering two questions fires for two unrelated reasons, and
#: whoever meets it relaxes it rather than reading it. Proving this file's own
#: guards showed that directly: with the value-less floor at 6, removing one name
#: failed the floor as well as the agreement test, so a legitimate removal would
#: have tripped a check that is meant to be about parsing.
#:
#: The agreement tests below are what notice a removal that matters.
FEWEST_CONDITION_KINDS = 8
FEWEST_VALUE_LESS = 3
FEWEST_WORD_ENTRIES = 4


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def body_of(text: str, opening: str, what: str) -> str:
    """From a declaration to the closing brace that ends it, at column zero.

    SCOPED DELIBERATELY. `CataclysmStatPipeline.h` declares four enumerations and
    `CataclysmStatPipeline.cpp` holds two switches over conditions, so a pattern
    run over a whole file collects names from the wrong one and the comparison
    means nothing. Every reader below takes a body rather than a file.

    IT RETURNS EMPTY RATHER THAN ASSERTING, AND THAT IS THE POINT. An assertion
    here fires inside a fixture, and pytest reports that as a collection ERROR
    for every test using it rather than as a failure. A guard proof that reads
    failure names would then find none and report that nothing noticed -- a check
    that had gone completely blind would look like a check with nothing to say.
    Returning empty lets the positive control below fail BY NAME, which is what
    makes the blindness provable. Found by proving this file's own guards: the
    first attempt could not demonstrate the control firing at all.
    """
    start = text.find(opening)
    if start < 0:
        return ""
    end = text.find("\n};", start)
    if end < 0:
        end = text.find("\n}", start)
    if end <= start:
        return ""
    return text[start:end]


@pytest.fixture(scope="module")
def condition_kinds() -> set[str]:
    """Every enumerator of `ECataclysmStatCondition`."""
    block = body_of(read(PIPELINE_H), "enum class ECataclysmStatCondition",
                    "the list of condition kinds")
    # An enumerator is a name followed by its display name, on the same line or
    # the next one, so the whitespace between them may include a newline.
    return set(re.findall(r"(\w+)\s+UMETA\(DisplayName", block))


@pytest.fixture(scope="module")
def judged_kinds() -> set[str]:
    """Every condition kind `ConditionHolds` has a case label for."""
    block = body_of(read(PIPELINE_CPP),
                    "bool UCataclysmStatPipeline::ConditionHolds",
                    "the predicate that answers whether a condition holds")
    return set(re.findall(r"case ECataclysmStatCondition::(\w+):", block))


@pytest.fixture(scope="module")
def name_of_kind() -> dict[str, str]:
    """Enumerator to the text name a data sheet writes."""
    block = body_of(read(PIPELINE_CPP),
                    "const FNamedStatCondition NamedStatConditions[]",
                    "the engine's condition name table")
    pairs = re.findall(
        r'TEXT\("([a-z_]+)"\)\s*,\s*ECataclysmStatCondition::(\w+)', block)
    return {kind: name for name, kind in pairs}


@pytest.fixture(scope="module")
def value_less_in_engine(name_of_kind) -> set[str]:
    """The names `ConditionTakesAValue` says compare nothing.

    Only the group before its first `return false;`. `Always` is a second group
    after it and is not a condition any sheet may write.
    """
    block = body_of(read(PIPELINE_CPP),
                    "bool UCataclysmStatPipeline::ConditionTakesAValue",
                    "the helper saying whether a condition compares a number")
    group = block[:block.find("return false;")]
    kinds = re.findall(r"case ECataclysmStatCondition::(\w+):", group)
    return {name_of_kind[kind] for kind in kinds if kind in name_of_kind}


@pytest.fixture(scope="module")
def value_less_in_passive_tests() -> set[str]:
    """The names `ComparesNothing` lists, the deliberate second copy."""
    block = body_of(read(PASSIVE_TESTS), "bool ComparesNothing(const FString& Name)",
                    "the second copy of the value-less list")
    return set(re.findall(r'TEXT\("([a-z_]+)"\)', block))


@pytest.fixture(scope="module")
def value_less_in_generator() -> set[str]:
    """The names the generator maps to nothing rather than to a range.

    THE AUTHORITATIVE ONE. This is what refuses a value written beside such a
    condition in the design workbook, so a disagreement elsewhere is the other
    list being wrong.
    """
    return {name for name, form in gen.CONDITIONS.items() if form is None}


def test_the_parsers_found_what_they_were_looking_for(
        condition_kinds, judged_kinds, name_of_kind, value_less_in_engine,
        value_less_in_passive_tests, value_less_in_generator):
    """The positive control, and the reason every count below is believable.

    Without this, a pattern that matched nothing would make both sides of every
    comparison empty and each test would pass having read no code at all.
    """
    assert len(condition_kinds) >= FEWEST_CONDITION_KINDS, (
        f"found {len(condition_kinds)} condition kinds in {PIPELINE_H.name}, "
        f"and there were at least {FEWEST_CONDITION_KINDS}. The parser is "
        f"probably reading the wrong block or the wrong shape.")
    assert len(judged_kinds) >= FEWEST_CONDITION_KINDS, (
        f"found {len(judged_kinds)} case labels in ConditionHolds.")
    assert len(name_of_kind) >= FEWEST_CONDITION_KINDS, (
        f"found {len(name_of_kind)} entries in the engine's name table.")
    for found, where in ((value_less_in_engine, "ConditionTakesAValue"),
                         (value_less_in_passive_tests, "ComparesNothing"),
                         (value_less_in_generator, "the generator")):
        assert len(found) >= FEWEST_VALUE_LESS, (
            f"found {len(found)} value-less conditions in {where}.")
    assert len(node_words.CONDITION_WORDS) >= FEWEST_WORD_ENTRIES, (
        f"CONDITION_WORDS imported {len(node_words.CONDITION_WORDS)} entries.")


def test_every_condition_kind_is_judged(condition_kinds, judged_kinds):
    """A condition the predicate has no case for is refused, silently.

    THIS IS THE WORST OF THE FAILURES THIS FILE COVERS. A name can be declared,
    registered in the engine's table, accepted by the generator and given the
    right value rule, and still do nothing: `ConditionHolds` falls out of its
    switch to `return false`, the modifier is refused, and no log line or test
    says so. The row simply never applies.
    """
    unjudged = sorted(condition_kinds - judged_kinds)
    assert not unjudged, (
        f"{unjudged} appear in ECataclysmStatCondition and have no case label "
        f"in UCataclysmStatPipeline::ConditionHolds. A modifier carrying one is "
        f"refused rather than applied, silently. Add a case that answers it.")

    unknown = sorted(judged_kinds - condition_kinds)
    assert not unknown, (
        f"ConditionHolds has case labels for {unknown}, which are not "
        f"enumerators of ECataclysmStatCondition. The parser is reading the "
        f"wrong block, or the enumeration lost a name a case still handles.")


def test_the_three_value_less_lists_agree(
        value_less_in_generator, value_less_in_engine,
        value_less_in_passive_tests):
    """Three hand-written copies of one fact, held together here.

    The generator's is authoritative: it is what refuses a value written beside
    such a condition in the design workbook. The other two are copies, and the
    copy in the passive tree tests is deliberate -- asking the engine what to
    expect would make that test agree with the code by construction.
    """
    assert value_less_in_engine == value_less_in_generator, (
        f"only ConditionTakesAValue says these compare nothing: "
        f"{sorted(value_less_in_engine - value_less_in_generator)}; only the "
        f"generator says these do: "
        f"{sorted(value_less_in_generator - value_less_in_engine)}. "
        f"ConditionTakesAValue returns true by DEFAULT, so a condition missing "
        f"from it is assumed to compare a number it has no meaning for.")

    assert value_less_in_passive_tests == value_less_in_generator, (
        f"ComparesNothing in CataclysmPassiveTreeTests.cpp disagrees with the "
        f"generator: only the test says "
        f"{sorted(value_less_in_passive_tests - value_less_in_generator)}, only "
        f"the generator says "
        f"{sorted(value_less_in_generator - value_less_in_passive_tests)}.")


def test_the_word_table_agrees_where_it_overlaps(value_less_in_generator):
    """`CONDITION_WORDS` carries the same fact in a third form, for some names.

    BY INTERSECTION AND NOT BY EQUALITY, and that is not a convenience. That
    table holds only the conditions a PASSIVE NODE names -- eight of twelve when
    this landed -- so it is a proper subset by design. An equality check would
    fail today on four conditions that are perfectly correct, and whoever hit it
    would weaken the test rather than read it.
    """
    disagree = []
    for name, (_words, value_form) in node_words.CONDITION_WORDS.items():
        says_value_less = value_form is None
        generator_says = name in value_less_in_generator
        if says_value_less != generator_says:
            disagree.append(
                f"{name}: CONDITION_WORDS says compares nothing="
                f"{says_value_less}, the generator says {generator_says}")

    assert not disagree, (
        "CONDITION_WORDS disagrees with the generator about which conditions "
        "compare nothing:\n  " + "\n  ".join(disagree))


def test_every_word_table_entry_names_a_condition_that_exists(
        value_less_in_generator):
    """A stale name there would check the wording of a condition nobody has."""
    unknown = sorted(set(node_words.CONDITION_WORDS) - set(gen.CONDITIONS))
    assert not unknown, (
        f"CONDITION_WORDS names {unknown}, which the generator does not know. "
        f"Either the condition was removed and this entry was left behind, or "
        f"it is misspelled and the node rows using it are unchecked.")
