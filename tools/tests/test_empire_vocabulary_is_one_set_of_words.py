"""The empire's four city tiers are named the same way everywhere.

WHY THIS EXISTS. Issue #534. Three documents described the empire's shape and no
two agreed:

    docs/Cataclysm_GDD_v2.md              12 villages, 8 cities, 4 metropolises,
                                          the capital
    sim/cataclysm_sim/world.py            Outpost, Bulwark, Sanctuary, Pillar
    docs/Empire_Skill_Tree_Keystones.md   "Cities within 2 hexes of The Pillar"

The design document's own table was inconsistent with itself as well: it counted
villages and metropolises as "cities" while "City" was also one of the four tier
names.

The project owner settled it on 2026-08-12: the simulation's names win. The
mapping was never in doubt, because `sim/cataclysm_sim/world.py` had documented
it in its module docstring the whole time — ring 0 Pillar (Capital), ring 1
Sanctuary (Metropolis), ring 2 Bulwark (City), ring 3 Outpost (Village) — and the
counts already matched, 1/4/8/12, because ring N holds exactly 4N cells.

THE HEX PROBLEM, which was the part that was not merely cosmetic. Two nodes of
the empire passive tree granted a bonus to "cities within 2 hexes". The lattice
is a taxicab ball of radius 3 with orthogonal adjacency, so it has no hexes and
that distance could not be computed. Every orthogonal step changes the ring by
exactly one, so ring is distance from the Pillar, and "within 2 rings" is the
same set the author meant: the 4 Sanctuaries and the 8 Bulwarks.

WHAT IS ASSERTED HERE.

    the four canonical names come from the simulation, not a copy in this file
    no design document uses any of the four superseded tier names
    no design document measures empire distance in hexes
    the design document states the counts the geometry produces
    the empire passive tree node graph uses only canonical names

WHAT IS DELIBERATELY ALLOWED. The word "capital", which now means the hub inside
the Pillar rather than a tier, and the node named "Venture Capital", which is
about gold and not about a city at all.
"""

from __future__ import annotations

import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN_DOCUMENT = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
KEYSTONES = REPO_ROOT / "docs" / "Empire_Skill_Tree_Keystones.md"
EMPIRE_TREE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: Tier names that were replaced. "Capital" is not here: it survives as the name
#: of the hub inside the Pillar, and as the unrelated node "Venture Capital".
SUPERSEDED = ("Village", "Metropolis")

#: The counts fall out of the geometry: ring N holds 4N cells.
EXPECTED_COUNTS = {"Pillar": 1, "Sanctuary": 4, "Bulwark": 8, "Outpost": 12}


@pytest.fixture(scope="module")
def canonical_names() -> set[str]:
    """Read the four names from the simulation rather than restating them.

    A copy here would be one more place to drift, which is the defect this file
    exists to prevent.
    """
    from cataclysm_sim.config import CityTier
    return {tier.value for tier in CityTier}


def test_the_canonical_names_are_the_four_expected(canonical_names):
    """If the simulation renames a tier, every check below is comparing against
    something else and would pass without meaning anything."""
    assert canonical_names == set(EXPECTED_COUNTS), (
        f"sim/cataclysm_sim/config.py defines {sorted(canonical_names)}, and "
        f"this file expects {sorted(EXPECTED_COUNTS)}. If the tiers were "
        "deliberately renamed, update this file and the design documents "
        "together."
    )


@pytest.mark.parametrize("path", [DESIGN_DOCUMENT, KEYSTONES])
def test_no_design_document_uses_a_superseded_tier_name(path):
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    found = []
    for name in SUPERSEDED:
        for match in re.finditer(rf"\b{name}(s|es)?\b", text, re.IGNORECASE):
            line = text.count("\n", 0, match.start()) + 1
            found.append(f"{path.name}:{line} {match.group(0)!r}")
    assert not found, (
        "These are the empire's old tier names. The canonical set is "
        f"{sorted(EXPECTED_COUNTS)}, defined in sim/cataclysm_sim/config.py.\n"
        + "\n".join(found)
    )


@pytest.mark.parametrize("path", [DESIGN_DOCUMENT, KEYSTONES])
def test_no_design_document_measures_empire_distance_in_hexes(path):
    """The lattice has no hexes, so a distance in hexes cannot be computed and
    the bonus that depends on it cannot be implemented."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    found = [
        f"{path.name}:{text.count(chr(10), 0, m.start()) + 1}"
        for m in re.finditer(r"\bhex(es)?\b", text, re.IGNORECASE)
    ]
    assert not found, (
        "Empire distance is counted in rings, not hexes. Adjacency is "
        "orthogonal on a taxicab lattice and every step changes the ring by "
        "one, so a city's ring is its distance from the Pillar.\n"
        + "\n".join(found)
    )


def test_the_empire_tree_node_graph_uses_only_canonical_names():
    if not EMPIRE_TREE.is_file():
        pytest.skip(f"{EMPIRE_TREE.name} is not present")
    graph = json.loads(EMPIRE_TREE.read_text(encoding="utf-8"))
    problems = []
    for node in graph["nodes"]:
        data = node.get("data", node)
        name = str(data.get("name") or "")
        description = str(data.get("description") or "")
        blob = f"{name} {description}"
        for superseded in SUPERSEDED:
            if re.search(rf"\b{superseded}(s|es)?\b", blob, re.IGNORECASE):
                problems.append(f"{name!r}: {description!r}")
        if re.search(r"\bhex(es)?\b", blob, re.IGNORECASE):
            problems.append(f"{name!r} measures distance in hexes: {description!r}")
    assert not problems, (
        "Empire passive tree nodes using superseded vocabulary:\n"
        + "\n".join(problems)
    )


def test_the_design_document_states_the_counts_the_geometry_produces():
    if not DESIGN_DOCUMENT.is_file():
        pytest.skip(f"{DESIGN_DOCUMENT.name} is not present")
    text = DESIGN_DOCUMENT.read_text(encoding="utf-8")
    start = text.index("## **City Tiers**")
    section = text[start:start + 1800]
    for tier, count in EXPECTED_COUNTS.items():
        assert tier in section, f"The City Tiers table does not name {tier}."
        assert re.search(rf"\|\s*{count}\s*\|", section), (
            f"The City Tiers table does not state the count {count}, which is "
            f"how many cells ring {count // 4 if count > 1 else 0} holds."
        )
