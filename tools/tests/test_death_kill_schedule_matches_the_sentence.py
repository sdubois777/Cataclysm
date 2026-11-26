"""The Death starting nodes' Fervour a kill pays what their sentences say, rung by rung.

WHY THIS EXISTS. The Death trees' kill generator, 2026-11-24 (slice B). The Soul
Collector's starting node reads "1 for each enemy you kill, 3 for an Elite, 5 for
a Legendary, 8 for a Herald and 15 for a Boss", and its rows are one
`fervour_per_kill` row a rarity rung. `test_passive_effects_match_the_node_text.py`
checks each row's number is in the sentence and each row's rung words are in the
sentence, separately; it cannot see a row paying 3 at the Legendary rung, because
the sentence holds both "3" and "for a legendary". This reads the pairs.

WHAT IS ASSERTED. For each rung 0 Common to 5 Cataclysm Boss, the flat
`fervour_per_kill` rows on the starting node that apply to a creature of that
rung add up to the figure the sentence gives that rung, a Boss's figure standing
for a Cataclysm Boss; and the Shadow's "10 for each enemy you kill, 25 for each
Elite or stronger enemy you kill" the same way.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"

#: The rungs, by the words the trees write for each.
RUNG_WORDS = {"an elite": 1, "a legendary": 2, "a herald": 3, "a boss": 4}


def rows(name: str) -> list[dict]:
    with (DATA / name).open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def paid_by_rows(node: str, rung: int) -> float:
    """What the flat `fervour_per_kill` rows on `node` pay for a creature of `rung`."""
    total = 0.0
    for row in rows("PassiveEffects.csv"):
        if row["Node"] != node or row["Stat"] != "fervour_per_kill" or row["ValueKind"] != "flat":
            continue
        value = float(row["ConditionValue"] or 0)
        condition = row["Condition"]
        if condition == "target_rarity_is" and round(value) != rung:
            continue
        if condition == "target_rarity_at_least" and rung < round(value):
            continue
        if condition not in ("", "target_rarity_is", "target_rarity_at_least"):
            raise AssertionError(f"{row['Name']} carries {condition!r}, which this check does not read")
        total += float(row["ValuePerPoint"])
    return total


def sentence(node: str) -> str:
    for row in rows("PassiveNodes.csv"):
        if row["Name"] == node:
            return row["Description"].lower()
    raise AssertionError(f"{node} is not in PassiveNodes.csv")


def schedule_of(text: str) -> dict[int, float]:
    """The figure a sentence gives each rung, 0 to 5."""
    common = re.search(r"(\d+) for each enemy you kill", text)
    assert common, f"no 'N for each enemy you kill' in {text!r}"
    by_rung = {0: float(common.group(1))}
    stronger = re.search(r"(\d+) for each elite or stronger enemy you kill", text)
    if stronger:
        for rung in range(1, 6):
            by_rung[rung] = float(stronger.group(1))
        return by_rung
    for figure, words in re.findall(r"(\d+) for (an elite|a legendary|a herald|a boss)", text):
        by_rung[RUNG_WORDS[words]] = float(figure)
    by_rung[5] = by_rung[4]
    assert sorted(by_rung) == list(range(6)), f"the sentence does not name every rung: {text!r}"
    return by_rung


def test_the_soul_collectors_rows_pay_each_rung_its_figure():
    node = "SoulCollector_basic_spine_000"
    wanted = schedule_of(sentence(node))
    assert wanted == {0: 1.0, 1: 3.0, 2: 5.0, 3: 8.0, 4: 15.0, 5: 15.0}, wanted
    got = {rung: paid_by_rows(node, rung) for rung in range(6)}
    assert got == wanted, f"the rows pay {got}; the sentence says {wanted}"


def test_the_shadows_rows_pay_each_rung_its_figure():
    node = "Shadow_basic_spine_000"
    wanted = schedule_of(sentence(node))
    assert wanted == {0: 10.0, 1: 25.0, 2: 25.0, 3: 25.0, 4: 25.0, 5: 25.0}, wanted
    got = {rung: paid_by_rows(node, rung) for rung in range(6)}
    assert got == wanted, f"the rows pay {got}; the sentence says {wanted}"
