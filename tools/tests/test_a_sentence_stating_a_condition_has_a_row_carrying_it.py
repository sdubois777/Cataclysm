"""A passive node whose sentence states a condition has a row carrying that condition.

WHY THIS EXISTS. `test_passive_effects_match_the_node_text.py` reads a row's condition against its node's words: a row
that carries a condition must find that condition's words in the sentence. It cannot see the reverse. A row whose
condition has been lost passes it, because a row with no condition is never asked. The Death trees' slice C4a
(2026-11-25) proved that: Death's Patience's row with its condition removed passed all 25 of its tests, and would have
paid Fervour on every kill. The coordinating session ruled a check of its own for the reverse direction, reading the same
phrase table, `CONDITION_WORDS`.

WHAT IS ASSERTED. For every sentence that rows answer to (a node's description, or a capstone option's), each condition
whose words the sentence says is carried by at least one of those rows, in `Condition` or `Condition2`. A JUDGEMENT on the
shape: the ruling's words were "a row ... carries a condition"; asking for THE condition the words state, on at least one
row, catches a lost condition as well and leaves a node free to have an unconditioned clause beside a conditioned one, as
Hunting Leap does. Measured on 2026-11-25, asking every row on such a sentence to carry some condition flagged 93 rows.

TWO NAMED LISTS, AND NOTHING ELSE IS EXCUSED. `TOO_GENERAL_TO_READ_BACKWARDS` holds the conditions whose words appear in
many sentences that state no such condition; each says how many it matched on 2026-11-25. `EXPRESSED_ANOTHER_WAY` holds
each remaining sentence that says a condition's words and carries it some other way: a stat whose meaning includes it, a
stack's own life or the event that grants it. Each says how. When this check was written it found no row in any tree
that had lost its condition.

A KNOWN LIMIT: THE CONDITIONS ON `TOO_GENERAL_TO_READ_BACKWARDS` ARE NOT CHECKED AT ALL, so a row that loses one of them
is still not caught. A later refinement, named by the coordinating session and not made now, is to read them by fuller
phrases, such as "below N% health", rather than by single words.
"""

from __future__ import annotations

import collections
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))

from test_passive_effects_match_the_node_text import (  # noqa: E402
    CONDITION_WORDS,
    EFFECTS_CSV,
    NODES_CSV,
    rows_of,
    words_of,
)

#: Conditions whose words are not read from a sentence back to its rows, because they appear in many sentences that state
#: no such condition. Measured 2026-11-25: how many sentences said the words and carried no such condition.
TOO_GENERAL_TO_READ_BACKWARDS = {
    "enemies_in_reach_at_least": "'within' (32): an area's size, 'within 2 seconds', and the scale enemies_in_reach",
    "health_below": "'below' (24): 'at or below', an enemy below a share, 'dropping below', a floor on a pool",
    "fervour_threshold_reached": "'or more fervour' (11): the same words class_resource_points_at_least is written with",
    "class_resource_points_at_least": "'or more fervour' (6): the same words fervour_threshold_reached is written with, "
                                      "and a spend of '10 or more Fervour at once'",
    "attacker_beyond_metres": "'more than' (7): 'no more than once every N seconds'",
    "opponent_within_metres": "'enemies within' (6): an area's size",
}

#: (node, option, condition): how a sentence that says the condition's words carries it without that condition.
EXPRESSED_ANOTHER_WAY = {
    ("SoulCollector_capstone_200", "1", "target_rarity_at_least"):
        "Wearer of the Dead's 'an Elite or stronger creature' is read off the slain creature at the kill "
        "(RarityStep of 1 or more, in UCataclysmFervour::WearTheModifiersOf), as ruled (W4) in the Death trees' "
        "slice C4d3; its rows wait on the worn modifier instead",
    ("Shadow_keystone_b_kC", "0", "target_rarity_at_least"):
        "Death by Inches' 'counts as killing an Elite or stronger enemy' is met by the Boss's own rung, the kill "
        "credited against the Boss itself, as ruled (Q7 b) in the Death trees' slice C4d2",
    ("SoulCollector_basic_b_a0", "0", "seconds_after_kill"):
        "Kill Chain's 'within 2 seconds of each other' is its stacks' 2-second life (own_stacks, StackSeconds 2)",
    ("SoulCollector_basic_spine_004", "0", "seconds_after_kill"):
        "Grave Patience's 'after a kill' is what fervour_kill_decay_grace_seconds is",
    ("SoulCollector_basic_c_b0", "0", "target_in_your_siphon"):
        "the stat siphon_enemies_deal_less_percent acts on the Siphon's members alone",
    ("SoulCollector_basic_c_stem0", "0", "target_in_your_siphon"):
        "the node that makes the Siphon, siphon_radius_metres",
    ("Masochist_capstone_25", "1", "skill_health_cost_above"):
        "'costs health' is what mana_pool_becomes_health does",
    ("Ritualist_capstone_25", "3", "target_carries_your_curse"):
        "curse_death_extra_imps is read on the death of an enemy carrying the curse",
    ("SoulCollector_basic_d_stem0", "0", "seconds_after_fervour_spent"):
        "stacks granted on fervour_spent of 10 or more, each lasting 4 seconds (own_stacks)",
    ("SoulCollector_basic_b_stem0", "0", "target_rarity_is"):
        "Gleaning's 'each enemy you kill' names every kill, not a rarity",
}


def phrases(condition: str) -> tuple[str, ...]:
    expected, _ = CONDITION_WORDS[condition]
    return (expected,) if isinstance(expected, str) else tuple(expected)


def stated_and_not_carried() -> tuple[list[tuple[str, str, str]], int, int]:
    """Each (node, option, condition) a sentence states and no row on it carries; and how many sentences and pairs."""
    nodes = {row["Name"]: row for row in rows_of(NODES_CSV)}
    sentences: dict[tuple[str, str], list[dict]] = collections.defaultdict(list)
    for row in rows_of(EFFECTS_CSV):
        sentences[(row["Node"], row["Option"])].append(row)
    read = [condition for condition in CONDITION_WORDS if condition not in TOO_GENERAL_TO_READ_BACKWARDS]
    missing = []
    pairs = 0
    for (node, option), rows in sorted(sentences.items()):
        words = words_of(rows[0], nodes).lower()
        carried = {row["Condition"].strip() for row in rows} | {row["Condition2"].strip() for row in rows}
        for condition in read:
            if any(phrase.lower() in words for phrase in phrases(condition)):
                pairs += 1
                if condition not in carried:
                    missing.append((node, option, condition))
    return missing, len(sentences), pairs


def test_every_condition_a_sentence_states_is_carried_by_one_of_its_rows():
    missing, sentences, pairs = stated_and_not_carried()
    unexplained = [key for key in missing if key not in EXPRESSED_ANOTHER_WAY]
    assert not unexplained, (
        "these sentences state a condition that none of their rows carries; a row has probably lost it:\n"
        + "\n".join(f"  {node} option {option}: {condition!r}, the words {list(phrases(condition))}"
                    for node, option, condition in unexplained)
        + f"\n\nRead: {sentences} sentences, {pairs} (sentence, condition) pairs, every condition in CONDITION_WORDS "
          f"but the {len(TOO_GENERAL_TO_READ_BACKWARDS)} in TOO_GENERAL_TO_READ_BACKWARDS. If the sentence carries the "
          "condition another way, add it to EXPRESSED_ANOTHER_WAY saying how.")


def test_every_exception_is_still_needed():
    missing, _, _ = stated_and_not_carried()
    stale = sorted(key for key in EXPRESSED_ANOTHER_WAY if key not in missing)
    assert not stale, f"in EXPRESSED_ANOTHER_WAY and no longer flagged, so no longer needed: {stale}"


def test_the_named_lists_name_real_conditions():
    unknown = sorted(set(TOO_GENERAL_TO_READ_BACKWARDS) - set(CONDITION_WORDS)) + sorted(
        {condition for _, _, condition in EXPRESSED_ANOTHER_WAY} - set(CONDITION_WORDS))
    assert not unknown, f"named in a list here and not in CONDITION_WORDS: {unknown}"
