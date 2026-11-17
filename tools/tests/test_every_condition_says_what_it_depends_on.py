"""Every stat condition says what its answer depends on, exactly once.

WHY THIS EXISTS. Issue #1821. `ACataclysmPlayerCharacter` keeps its movement
speed on the movement component and asks for it again only when something tells
it to. Which conditions can change with nothing telling it -- a timer running
out, an enemy walking closer -- is answered by
`UCataclysmStatPipeline::WhatConditionDependsOn`, a switch with one case label
per condition and no default. A condition added to `ECataclysmStatCondition`
and left out of that switch would log an error and be treated as depending on
time. This check fails first, when the tests run, rather than in play.

REUSES THE ENUMERATOR PARSER in `test_condition_lists_agree_with_the_code.py`,
whose own positive control fails by name if it finds nothing.
"""

from __future__ import annotations

import collections
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from test_condition_lists_agree_with_the_code import (  # noqa: E402,F401  (fixtures)
    FEWEST_CONDITION_KINDS, PIPELINE_CPP, body_of, condition_kinds, read,
)

SIGNATURE = ("ECataclysmConditionDependsOn "
             "UCataclysmStatPipeline::WhatConditionDependsOn(")


def classified_labels(cpp_text: str) -> list[str]:
    """Every case label in `WhatConditionDependsOn`, repeats kept.

    The body is written with `using C = ECataclysmStatCondition;`, so a label
    reads `case C::Name:`. The long spelling is accepted too, so that the check
    does not go blind if somebody writes it out.
    """
    block = body_of(cpp_text, SIGNATURE, "the classification of conditions")
    return re.findall(r"case (?:C|ECataclysmStatCondition)::(\w+):", block)


def test_the_classification_was_found() -> None:
    """The positive control: the parser sees the switch at all."""
    labels = classified_labels(read(PIPELINE_CPP))
    assert len(labels) >= FEWEST_CONDITION_KINDS, (
        f"found {len(labels)} case labels in WhatConditionDependsOn; the "
        f"function was renamed, moved, or its labels spelled another way, and "
        f"every check below would compare nothing")


def test_every_condition_is_classified(request) -> None:
    kinds = request.getfixturevalue("condition_kinds")
    labels = set(classified_labels(read(PIPELINE_CPP)))
    missing = sorted(kinds - labels)
    assert not missing, (
        f"{missing} are conditions WhatConditionDependsOn does not classify. "
        f"A movement speed row under one would be refreshed on the step as "
        f"though it depended on time, and the function logs an error each time "
        f"it is asked. Add each to the case that says what it reads. Issue #1821.")


def test_nothing_is_classified_that_is_not_a_condition(request) -> None:
    kinds = request.getfixturevalue("condition_kinds")
    unknown = sorted(set(classified_labels(read(PIPELINE_CPP))) - kinds)
    assert not unknown, (
        f"{unknown} are case labels in WhatConditionDependsOn that are not "
        f"enumerators of ECataclysmStatCondition")


def test_no_condition_is_classified_twice() -> None:
    counts = collections.Counter(classified_labels(read(PIPELINE_CPP)))
    twice = sorted(name for name, count in counts.items() if count > 1)
    assert not twice, (
        f"{twice} appear more than once in WhatConditionDependsOn; a switch "
        f"refuses to compile a duplicate label, so the parser is reading "
        f"something else")
