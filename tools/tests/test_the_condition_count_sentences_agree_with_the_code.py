"""The three sentences that COUNT the stat conditions say what the code holds.

WHY THIS EXISTS. Issue #1640. Above `ConditionTakesAValue` in
`CataclysmStatPipeline.h` a sentence states how many conditions compare no
number, out of how many there are; above `AllConditionNames` a neighbour says
what adding "a twenty-second" would do; and the `.cpp`'s own switch says "each
of the fourteen". They rotted three times on 2026-09-12 alone, each correction
superseded within hours, because every change adds enumerators BELOW them and
merges cleanly, so no conflict marker ever lands on the sentence. When this
check landed the header said fourteen of twenty-eight and the code held
sixteen of thirty-eight.

REUSES THE PARSERS, DOES NOT REWRITE THEM. The counts come from the same
readers `tools/tests/test_condition_lists_agree_with_the_code.py` uses to hold
the lists to each other; that file's positive control already fails by name if
they find nothing. Two quick patterns written for the issue both returned zero,
because the enumerators carry a `UMETA` line and the case labels group
differently than a naive pattern expects.

WHAT EACH NUMBER IS. The denominator is the conditions a data sheet may name,
which is the engine's name table (`Always` is an enumerator no sheet may
write and is not counted); the numerator is the names `ConditionTakesAValue`
lists before its first `return false;`. The ordinal is one more than the
denominator.

THE WORDS ARE WORDS. The sentences write numbers out ("SIXTEEN OF THE
THIRTY-EIGHT", "a thirty-ninth"), so the check reads them through a small
table, in the shape `test_affix_tier_gate_is_stated_everywhere.py` uses.
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from test_condition_lists_agree_with_the_code import (  # noqa: E402,F401  (fixtures)
    PIPELINE_CPP, PIPELINE_H, name_of_kind, read, value_less_in_engine,
)

UNITS = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
         "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
         "sixteen", "seventeen", "eighteen", "nineteen"]
TENS = {"twenty": 20, "thirty": 30, "forty": 40, "fifty": 50, "sixty": 60}
ORDINALS = {"first": 1, "second": 2, "third": 3, "fourth": 4, "fifth": 5,
            "sixth": 6, "seventh": 7, "eighth": 8, "ninth": 9}


def number_word(word: str) -> int:
    """'sixteen' -> 16, 'thirty-eight' -> 38, case-insensitive."""
    word = word.lower()
    if word in UNITS:
        return UNITS.index(word)
    tens, _, units = word.partition("-")
    assert tens in TENS and (units == "" or units in UNITS), f"cannot read {word!r} as a number"
    return TENS[tens] + (UNITS.index(units) if units else 0)


def ordinal_word(word: str) -> int:
    """'thirty-ninth' -> 39, 'twentieth' -> 20, 'ninth' -> 9."""
    word = word.lower()
    if word in ORDINALS:
        return ORDINALS[word]
    if word.endswith("ieth"):
        return TENS[word[:-4] + "y"]
    tens, _, units = word.partition("-")
    assert tens in TENS and units in ORDINALS, f"cannot read {word!r} as an ordinal"
    return TENS[tens] + ORDINALS[units]


HEADER_SENTENCE = re.compile(r"([A-Z]+(?:-[A-Z]+)?) OF THE ([A-Z]+(?:-[A-Z]+)?) COMPARE NOTHING")
ORDINAL_SENTENCE = re.compile(r"adds a ([a-z]+(?:-[a-z]+)?),")
SWITCH_SENTENCE = re.compile(r"Each of the ([a-z]+(?:-[a-z]+)?) says")


def test_the_header_count_sentence_states_the_two_counts_the_code_holds(request) -> None:
    # THE FIXTURES ARE FETCHED BY NAME rather than taken as parameters, because
    # a parameter of the same name as the imported fixture is a redefinition to
    # the linter (F811); the import above is what registers them here.
    names = request.getfixturevalue("name_of_kind")
    value_less = request.getfixturevalue("value_less_in_engine")
    match = HEADER_SENTENCE.search(read(PIPELINE_H))
    assert match, "the 'N OF THE M COMPARE NOTHING' sentence is gone from CataclysmStatPipeline.h"
    stated_value_less, stated_all = number_word(match.group(1)), number_word(match.group(2))
    assert (stated_value_less, stated_all) == (len(value_less), len(names)), (
        f"CataclysmStatPipeline.h says '{match.group(0)}'; the code holds "
        f"{len(value_less)} value-less conditions of {len(names)} "
        f"named. Correct the sentence; the numbers are read from ConditionTakesAValue "
        f"and NamedStatConditions. Issue #1640.")


def test_the_ordinal_above_all_condition_names_is_one_past_the_count(request) -> None:
    names = request.getfixturevalue("name_of_kind")
    match = ORDINAL_SENTENCE.search(read(PIPELINE_H))
    assert match, "the 'adds a <ordinal>,' sentence is gone from CataclysmStatPipeline.h"
    assert ordinal_word(match.group(1)) == len(names) + 1, (
        f"CataclysmStatPipeline.h says adding '{match.group(1)}'; there are "
        f"{len(names)} named conditions, so the next is the "
        f"{len(names) + 1}th. Issue #1640.")


def test_the_switch_comment_counts_the_value_less_cases(request) -> None:
    value_less = request.getfixturevalue("value_less_in_engine")
    match = SWITCH_SENTENCE.search(read(PIPELINE_CPP))
    assert match, "the 'Each of the <count> says' sentence is gone from CataclysmStatPipeline.cpp"
    assert number_word(match.group(1)) == len(value_less), (
        f"CataclysmStatPipeline.cpp says 'Each of the {match.group(1)}'; the switch "
        f"holds {len(value_less)} value-less names. Issue #1640.")


def test_the_word_readers_read_what_the_sentences_use() -> None:
    """The control on the two small tables, so a sentence written in a word
    they cannot read fails here by name rather than as a wrong count."""
    assert number_word("fourteen") == 14 and number_word("TWENTY-EIGHT") == 28
    assert number_word("forty") == 40
    assert ordinal_word("twenty-second") == 22 and ordinal_word("thirty-ninth") == 39
    assert ordinal_word("fortieth") == 40 and ordinal_word("ninth") == 9
