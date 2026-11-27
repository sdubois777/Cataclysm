"""Every class's starting node holds a row the Fervour bar's check can see.

The owner's play-test of 2026-11-26, a Death Crossbow character holding the Soul Collector's starting node: "I don't
have a fervour bar". The heads-up display draws the bar only when `UCataclysmCombatOverlay::FervourOf` says the
character can move Fervour, which asks `UCataclysmFervour::HasAGenerator` (the rate attributes, and since that day the
stat lines of every stat in `UCataclysmFervour::GainStats`) and `FCataclysmWarFervour::HasARate` (the stat lines of the
stats in `FCataclysmWarFervour::GainStats`). A starting node none of whose rows is on one of those stats draws no bar.

THE STAT LINES ARE WHAT THIS CHECKS, read from the two C++ lists by the constants' names, so a starting node written on
a new gain stat that neither list holds fails here before anyone plays it. The Unreal test
`Cataclysm.StartingNodeDisplay.EveryClasssStartingNodeDrawsTheFervourBar` checks the same in the engine.
"""

from __future__ import annotations

import csv
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
DATA = ROOT / "game" / "Data"


def constants(path: pathlib.Path) -> dict[str, str]:
    """`Class::Name = TEXT("stat")` definitions in a .cpp file, by the constant's bare name."""
    text = path.read_text(encoding="utf-8")
    return {m.group(1): m.group(2)
            for m in re.finditer(r"::(\w+Stat)\s*=\s*TEXT\(\"([a-z_0-9]+)\"\)", text)}


def listed(path: pathlib.Path, function: str) -> list[str]:
    """The constant names inside `function`'s static list, a brace list after `Stats =`."""
    text = path.read_text(encoding="utf-8")
    body = re.search(rf"{function}\(\)\s*\{{(.*?)\n\}}", text, re.S)
    assert body, f"{function} not found in {path.name}"
    braces = re.search(r"Stats\s*=\s*\{(.*?)\};", body.group(1), re.S)
    assert braces, f"no Stats list in {function}"
    return re.findall(r"\b(\w+Stat)\b", braces.group(1))


def gain_stats() -> set[str]:
    fervour = SOURCE / "CataclysmFervour.cpp"
    war = SOURCE / "CataclysmWarFervour.cpp"
    names = constants(fervour) | constants(war)
    found = set()
    for path, function in ((fervour, "UCataclysmFervour::GainStats"), (war, "FCataclysmWarFervour::GainStats")):
        for constant in listed(path, function):
            assert constant in names, f"{constant} in {function} has no TEXT definition this check can read"
            found.add(names[constant])
    return found


def rows(name: str) -> list[dict[str, str]]:
    text = (DATA / name).read_bytes().decode("utf-8-sig")
    return list(csv.DictReader(text.splitlines()))


def starting_nodes() -> dict[str, str]:
    """Each tree's node named "Fervour" of the basic kind: the class's starting node."""
    out = {}
    for row in rows("PassiveNodes.csv"):
        if row["NodeName"] == "Fervour":
            assert row["Tree"] not in out, f"two Fervour nodes in {row['Tree']}"
            out[row["Tree"]] = row["Name"]
    return out


def test_the_check_reads_both_lists():
    stats = gain_stats()
    # THE SCOPE, so a parse that read nothing cannot pass: the Masochist's, the War classes' and the kill generator.
    assert {"fervour_from_damage", "fervour_per_hit_taken", "fervour_per_kill"} <= stats, sorted(stats)


def test_every_class_starting_node_has_a_row_the_bar_check_reads():
    stats = gain_stats()
    nodes = starting_nodes()
    assert nodes, "CONTROL: the starting nodes were found"
    effects = rows("PassiveEffects.csv")
    unseen = {}
    for tree, node in sorted(nodes.items()):
        held = {row["Stat"] for row in effects if row["Node"] == node and float(row["ValuePerPoint"] or 0) > 0}
        if not held & stats:
            unseen[tree] = sorted(held)
    assert not unseen, (
        "these classes' starting nodes hold no row on a stat UCataclysmFervour::GainStats or "
        f"FCataclysmWarFervour::GainStats lists, so holding them draws no Fervour bar: {unseen}")
