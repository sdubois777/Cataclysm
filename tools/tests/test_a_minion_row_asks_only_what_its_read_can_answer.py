"""A row on a minion stat may carry only a condition its read can answer.

WHY THIS EXISTS. Issue #1804. A summoner's minion stats reach a minion through
reads that hand the pipeline no blow and no skill, and all but one hand it no
target either. A row whose condition asks about the blow, the skill or the
target is then judged against an empty state, refuses every time, and grants
nothing -- while the generator accepts it, the row imports and the passive tree
applies it. Nothing anywhere reports it.

WHAT IS ALLOWED, AND WHERE EACH LIST COMES FROM. Both lists are read out of the
C++ rather than copied here, so a new condition is classified once, in the code:

- The conditions that depend on the blow or the skill are the case labels that
  `UCataclysmStatPipeline::WhatConditionDependsOn` answers with
  `EOn::TheBlowOrSkill`. No minion read can answer one of those, except below.
- `minion_damage` is read against the enemy the minion is striking, since issue
  #1515's Set Upon (merged as #2061), through `SummonerMultiplierAgainst` and
  `MultiplierForStatAgainst`, which fill the state with
  `UCataclysmAbilitySystemComponent::WithTargetState`. The conditions it
  answers are the case labels in that function's switch. It still hands over
  no blow and no skill, so any other blow-or-skill condition refuses there too.

Every other minion stat (`minion_health`, `minion_duration`,
`minion_attack_speed`, `minion_explosion_damage` and the rest) is read with no
target, so it may carry only a condition that does not depend on the blow or
the skill: one about the character, its surroundings or time.

WHAT IT DOES NOT CHECK. Whether a condition that IS answerable means what the
row's sentence says. Only whether the read can answer it at all.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from test_condition_lists_agree_with_the_code import (  # noqa: E402,F401  (fixtures)
    PIPELINE_CPP, body_of, name_of_kind, read,
)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ABILITY_SYSTEM_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                      / "CataclysmAbilitySystemComponent.cpp")
MINION_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmMinion.cpp")
DATA = REPO_ROOT / "game" / "Data"

#: The sheets that grant stats under a condition. `Affixes.csv` has no
#: condition column.
SHEETS = ("PassiveEffects.csv", "EnchantmentEffects.csv")

#: The one minion stat read against the enemy being struck. Issue #1515 / #2061.
TARGETED_MINION_STAT = "minion_damage"

#: Floors, not pins: each says the parser saw something, as the floors in
#: `test_condition_lists_agree_with_the_code.py` do.
FEWEST_BLOW_OR_SKILL = 10
FEWEST_TARGET_ANSWERED = 5
FEWEST_MINION_ROWS = 10


def blow_or_skill_kinds() -> set[str]:
    """The kinds `WhatConditionDependsOn` answers with `EOn::TheBlowOrSkill`."""
    block = body_of(read(PIPELINE_CPP),
                    "ECataclysmConditionDependsOn "
                    "UCataclysmStatPipeline::WhatConditionDependsOn(",
                    "the classification of conditions")
    kinds: set[str] = set()
    for labels, answer in re.findall(
            r"((?:\s*case (?:C|ECataclysmStatCondition)::\w+:)+)\s*return EOn::(\w+);",
            block):
        if answer == "TheBlowOrSkill":
            kinds.update(re.findall(r"::(\w+):", labels))
    return kinds


def target_answered_kinds() -> set[str]:
    """The kinds `WithTargetState` fills the state for."""
    block = body_of(read(ABILITY_SYSTEM_CPP),
                    "FCataclysmStatConditions UCataclysmAbilitySystemComponent::WithTargetState(",
                    "the target state a minion's damage is read with")
    return set(re.findall(r"case ECataclysmStatCondition::(\w+):", block))


def minion_rows() -> list[tuple[str, str, str, str]]:
    """(sheet, row name, stat, condition) for every row on a minion stat."""
    rows = []
    for sheet in SHEETS:
        path = DATA / sheet
        if not path.is_file():
            pytest.skip(f"{sheet} is not present")
        with path.open(encoding="utf-8", newline="") as handle:
            for row in csv.DictReader(handle):
                if row["Stat"].startswith("minion"):
                    rows.append((sheet, row["Name"], row["Stat"], row["Condition"]))
    return rows


def test_the_parsers_found_what_they_read() -> None:
    """The positive control: every list is non-trivial before any verdict."""
    assert len(blow_or_skill_kinds()) >= FEWEST_BLOW_OR_SKILL
    assert len(target_answered_kinds()) >= FEWEST_TARGET_ANSWERED
    assert len(minion_rows()) >= FEWEST_MINION_ROWS
    assert target_answered_kinds() <= blow_or_skill_kinds(), (
        "WithTargetState fills a condition WhatConditionDependsOn does not call "
        "blow-or-skill; one of the two lists has changed its meaning")


def test_minion_damage_is_still_the_one_stat_read_against_a_target() -> None:
    """The allowance below is only true while the read it rests on exists."""
    text = read(MINION_CPP)
    assert re.search(
        r'SummonerMultiplierAgainst\([^;]*TEXT\("' + TARGETED_MINION_STAT + r'"\)',
        text, re.S), (
        f"{TARGETED_MINION_STAT} is no longer read through SummonerMultiplierAgainst; "
        "the allowance in this file no longer describes the code")


def test_no_minion_row_asks_what_its_read_cannot_answer(request) -> None:
    # Through `request`, as `test_every_condition_says_what_it_depends_on.py`
    # does, so the imported fixture is not redefined as a parameter.
    names = request.getfixturevalue("name_of_kind")
    kind_of_name = {name: kind for kind, name in names.items()}
    blow = blow_or_skill_kinds()
    targeted = target_answered_kinds()

    wrong = []
    for sheet, name, stat, condition in minion_rows():
        kind = kind_of_name.get(condition)
        if not condition or kind is None or kind not in blow:
            continue
        if stat == TARGETED_MINION_STAT and kind in targeted:
            continue
        wrong.append(f"{sheet}: {name} ({stat}, condition {condition})")

    assert not wrong, (
        "these rows on a minion stat carry a condition the minion's read cannot "
        "answer, so they would grant nothing, silently (issue #1804):\n  "
        + "\n  ".join(wrong)
        + f"\nOnly {TARGETED_MINION_STAT} is read against the enemy struck, and "
        "only for the conditions WithTargetState fills.")
