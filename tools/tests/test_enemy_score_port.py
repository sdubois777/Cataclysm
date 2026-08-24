"""The Unreal Enemy Score constants must match the simulation's.

WHY THIS EXISTS. `UCataclysmEnemyScore` in
`game/Source/Cataclysm/Dungeon/CataclysmEnemyScore.cpp` is a port of
`enemy_scores` in `sim/cataclysm_sim/scoring.py`. That file is itself a port of
`src/utils/calculateScores.tsx` in the separate, private
`sdubois777/DungeonSimulator` repository, and `CLAUDE.md` records that it has
silently drifted from its own source twice.

THE CHAIN OF AUTHORITY, and where this file sits in it:

    src/utils/calculateScores.tsx        authoritative, in another repository
      ^ checked by sim/verify_scoring_port.py
    sim/cataclysm_sim/scoring.py         a verified port, never hand-edited
      ^ checked by tools/tests/test_enemy_score_formula.py   (the document)
      ^ checked by THIS FILE                                 (the C++)
    docs/Cataclysm_GDD_v2.md section X   what a person reads
    CataclysmEnemyScore.cpp              what the game runs

Two copies of a number are two numbers. The Python and the C++ now both hold the
eight tier anchors, four type weights, eight sub-type weights, six rarity
weights, four floor scaling bases and four bare constants, and nothing else would
notice if the two lists parted company.

WHY IT PARSES SOURCE RATHER THAN READING A GENERATED TABLE. The same reason
`test_power_score_port.py` does: these numbers have an authoritative home
elsewhere, and putting them in the design workbook would make a third copy of
numbers that already have two. Parsing source is cruder, and the alternative is
no guard at all.

WHAT IT DOES NOT CHECK. That the C++ computes anything. The arithmetic is checked
by Unreal automation tests under the `Cataclysm.EnemyScore` prefix, in
`game/Source/Cataclysm/Tests/CataclysmEnemyScoreTests.cpp`, which include eight
scores computed by the Python model and typed in. A test written against a
constant cannot notice that the code ignores it, and a test that reads a constant
out of source cannot notice that the constant is wrong.

Issue #926.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
          / "CataclysmEnemyScore.h")
SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
          / "CataclysmEnemyScore.cpp")
RARITIES_CSV = REPO_ROOT / "game" / "Data" / "EnemyRarities.csv"

#: The C++ enum spellings against the model's dictionary keys. The two differ
#: only where the model's key has a space in it, which an identifier cannot.
TYPE_NAMES = {
    "Basic": "Basic",
    "Quest": "Quest",
    "FallenCity": "Fallen City",
    "Cataclysm": "Cataclysm",
}

SUBTYPE_NAMES = {
    "None": "None",
    "Timed": "Timed",
    "Horde": "Horde",
    "Siege": "Siege",
    "CowLevel": "Cow Level",
    "Elite": "Elite",
    "Volatile": "Volatile",
    "Sacrificial": "Sacrificial",
}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The C++ with both kinds of comment removed.

    NEEDED, NOT TIDINESS. The port's comments quote the very names this file
    searches for: the block comment above `JsRound` explains at length that
    `FMath::RoundToInt` is deliberately NOT used, so a test asserting that name
    is absent fails on the explanation for the thing it is checking.

    BOTH KINDS, AND THE BLOCK ONE IS THE ONE THAT CAUGHT ME OUT. Stripping only
    `//` lines left every `/** ... */` header comment in place, which is where
    this project keeps most of its reasoning.

    Line breaks inside a block comment are kept, so line numbers and the
    line-by-line `//` pass below still line up with the real file.
    """
    kept: list[str] = []
    rest = text
    while True:
        start = rest.find("/*")
        if start == -1:
            kept.append(rest)
            break
        kept.append(rest[:start])
        end = rest.find("*/", start + 2)
        if end == -1:
            break
        kept.append("\n" * rest.count("\n", start, end + 2))
        rest = rest[end + 2:]

    joined = "".join(kept)
    return "\n".join(line.split("//", 1)[0] for line in joined.splitlines())


def function_body(code: str, signature: str) -> str:
    """One function's body, found by matching braces.

    `TypeWeightFor` and `FloorScalingBaseFor` both switch on
    `ECataclysmDungeonType`, so a search across the whole file finds eight cases
    where it wants four and silently compares the wrong half. That is not
    hypothetical: cutting at the first `\\n}` instead ran past the end of the
    function, because these are inside an anonymous namespace and so are
    indented, and it read the floor scaling bases as the type weights.
    """
    at = code.find(signature)
    if at == -1:
        pytest.fail(f"{signature} is not in {SOURCE.name}; has it been renamed?")

    opened = code.find("{", at)
    depth = 0
    for index in range(opened, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[opened:index + 1]
    pytest.fail(f"{signature} in {SOURCE.name} has unbalanced braces")
    return ""


def switch_values(body: str, enum: str) -> dict[str, float]:
    """Every `case Enum::Name: return <number>;` in one function body."""
    found = re.findall(rf"case\s+{enum}::(\w+):\s*return\s+([-\d.]+);", body)
    return {name: float(value) for name, value in found}


def braced_list(code: str, name: str) -> list[float]:
    """The numbers in a `const TArray<double> Name = { ... };` initialiser."""
    match = re.search(rf"{name}\s*=\s*\{{([^}}]*)\}}", code)
    if not match:
        pytest.fail(f"could not find {name} in {SOURCE.name}; has it been renamed?")
    return [float(piece) for piece in match.group(1).replace("\n", " ").split(",")
            if piece.strip()]


def constant(text: str, name: str) -> float:
    match = re.search(
        rf"static\s+constexpr\s+[\w:]+\s+{name}\s*=\s*([-\d.]+)\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {HEADER.name}; has it been renamed?")
    return float(match.group(1))


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import scoring
    return scoring


@pytest.fixture(scope="module")
def header() -> str:
    return read(HEADER)


@pytest.fixture(scope="module")
def code() -> str:
    return without_comments(read(SOURCE))


def test_the_tier_anchors_match(model, code):
    """The eight numbers every other figure in the model is a fraction of."""
    ported = braced_list(code, "GTierAnchors")
    expected = [float(model.PLAYER_MAX_SCORES[tier]) for tier in range(0, 9)]
    assert ported == expected, (
        f"the Unreal tier anchors are {ported} and the model's are {expected}")


def test_the_type_weights_match(model, code):
    ported = switch_values(function_body(code, "double TypeWeightFor"),
                           "ECataclysmDungeonType")
    assert set(ported) == set(TYPE_NAMES), (
        f"the Unreal type weight switch covers {sorted(ported)}, not the four "
        f"dungeon types")
    for cpp_name, model_key in TYPE_NAMES.items():
        assert ported[cpp_name] == pytest.approx(model.TYPE_WEIGHTS[model_key]), (
            f"{cpp_name} weighs {ported[cpp_name]} in Unreal and "
            f"{model.TYPE_WEIGHTS[model_key]} in the model")


def test_the_floor_scaling_bases_match(model, code):
    ported = switch_values(function_body(code, "double FloorScalingBaseFor"),
                           "ECataclysmDungeonType")
    assert set(ported) == set(TYPE_NAMES)
    for cpp_name, model_key in TYPE_NAMES.items():
        assert ported[cpp_name] == pytest.approx(
            model.FLOOR_SCALING_BASES[model_key])


def test_the_subtype_weights_match(model, code):
    ported = switch_values(function_body(code, "double SubTypeWeightFor"),
                           "ECataclysmDungeonSubType")
    assert set(ported) == set(SUBTYPE_NAMES), (
        f"the Unreal sub-type weight switch covers {sorted(ported)}, and the "
        f"model has {sorted(model.SUBTYPE_WEIGHTS)}")
    for cpp_name, model_key in SUBTYPE_NAMES.items():
        assert ported[cpp_name] == pytest.approx(
            model.SUBTYPE_WEIGHTS[model_key]), (
            f"{cpp_name} weighs {ported[cpp_name]} in Unreal and "
            f"{model.SUBTYPE_WEIGHTS[model_key]} in the model")


def test_the_subtype_list_is_the_whole_list(model):
    """The mapping above is written out, so a sub-type could be added to the
    model and never compared. This is what notices."""
    assert set(SUBTYPE_NAMES.values()) == set(model.SUBTYPE_WEIGHTS), (
        "the sub-types this file knows about are not the model's. Add the new "
        "one to SUBTYPE_NAMES here, to the ECataclysmDungeonSubType enum, and "
        "to the switch in CataclysmEnemyScore.cpp.")
    assert set(TYPE_NAMES.values()) == set(model.TYPE_WEIGHTS)


def test_the_rarity_weights_match_and_are_in_step_order(model, code):
    """THE ONE TABLE WHOSE ORDER CARRIES MEANING. The C++ holds it as an array
    indexed by the Step column of `game/Data/EnemyRarities.csv`, and the model
    holds it as a dictionary keyed by name. Lining them up wrongly would give
    every enemy another rarity's score without any test noticing, so the order
    is read out of the CSV rather than assumed."""
    import csv

    with RARITIES_CSV.open(newline="", encoding="utf-8") as handle:
        rows = sorted(csv.DictReader(handle), key=lambda row: int(row["Step"]))
    by_step = [row["RarityName"] for row in rows]

    ported = braced_list(code, "GRarityWeights")
    assert len(ported) == len(by_step), (
        f"Unreal has {len(ported)} rarity weights and "
        f"game/Data/EnemyRarities.csv has {len(by_step)} rarities")

    for step, name in enumerate(by_step):
        assert ported[step] == pytest.approx(model.RARITY_WEIGHTS[name]), (
            f"step {step} is {name}, which weighs {model.RARITY_WEIGHTS[name]} "
            f"in the model and {ported[step]} in Unreal")


def test_the_four_bare_constants_match(model, header):
    """The parts of the formula that are numbers in the reference rather than
    entries in one of its tables. `check_against_reference` in the model cannot
    see them, so this is the only comparison they get."""
    for cpp_name, model_name in (
        ("BaselineWeight", "BASELINE_WEIGHT"),
        ("ProceduralDivisor", "PROCEDURAL_DIVISOR"),
        ("ProceduralPerFloor", "PROCEDURAL_PER_FLOOR"),
        ("DepthTensionPerTier", "DEPTH_TENSION_PER_TIER"),
    ):
        assert constant(header, cpp_name) == pytest.approx(
            getattr(model, model_name)), (
            f"{cpp_name} is {constant(header, cpp_name)} in Unreal and "
            f"{model_name} is {getattr(model, model_name)} in the model")


def test_the_port_rounds_the_way_the_model_does(code):
    """floor(x + 0.5), and NOT `FMath::RoundToInt`.

    The model's `_js_round` says using Python's own `round` made the port
    disagree with the reference on about 2% of inputs, always by exactly one.
    The same trap exists here, and it is worse than in the experience curve
    because these scores can be negative near a dungeon entrance, where rounding
    half away from zero and rounding half up are genuinely different.
    """
    assert "FloorToDouble(Value + 0.5)" in code, (
        "the port no longer rounds a score with floor(x + 0.5). Whatever it "
        "does instead has to agree with _js_round in "
        "sim/cataclysm_sim/scoring.py, including on negative scores.")
    assert "FMath::RoundToInt" not in code, (
        "the port now uses the engine's rounding helper, which does not promise "
        "floor(x + 0.5) and rounds a negative half the other way")


def test_the_middle_floor_rounds_up(code):
    """`math.ceil`, not a round or a truncation. A 51-floor dungeon's middle is
    floor 26, so the depth tension term changes sign one floor later than a
    reader might expect. Truncating would move it a floor the other way on every
    odd-length dungeon."""
    assert "CeilToInt" in code, (
        "the port no longer takes the ceiling of half the floor count. The "
        "model uses math.ceil, so anything else moves the middle floor of every "
        "odd-length dungeon.")


def test_the_port_says_what_still_has_no_source(header):
    """The modifier score is a real term of the model and nothing fills it. A
    header that did not say so would read as though the port were complete."""
    assert "ModifierScore" in header
    assert "#41" in header, (
        "the header no longer names the issue that brings dungeon modifiers, so "
        "a reader has no way to find out why the modifier score is always zero")
