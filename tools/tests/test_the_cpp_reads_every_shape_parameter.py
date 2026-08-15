"""Every shape parameter the generator allows is one the game actually reads.

WHY THIS EXISTS. Four parameters were written into `game/Data/WeaponSkills.csv`
by `tools/generate_datatables.py` and rejected by the C++ that reads it, and each
one was found separately, months apart, by running a test suite that nobody had
run in a while:

    Minions         issue #622   so no summon skill knew what to summon
    GroundPercent   issue #590   so burning ground used the wrong formula
    HealthPercent   issue #621   masked behind the missing Deployable shape
    StunSeconds     issue #588   found the day the four stunning skills got a shape

THEY ARE ALL THE SAME MISTAKE. `UCataclysmSkillShapes::ParseParams` in
`game/Source/Cataclysm/AbilitySystem/CataclysmSkillShape.cpp` matches parameter
names one by one in an if-else chain, and its final branch rejects anything it
does not recognise. The generator's `SHAPE_PARAMS` and `SHAPE_RIDERS` are the
list of what may legally be written. Nothing compared the two, so adding a
parameter to the sheet and not to the chain produced a skill that generated
cleanly and then refused its own numbers at runtime.

WHY THE FAILURE IS WORSE THAN IT SOUNDS. `ParseParams` marks the WHOLE cell
invalid when one entry is unreadable, so an unknown parameter does not lose one
number, it loses all of them. A summon skill with an unreadable `Minions` also
lost its `Count` and `MaxActive`.

HOW THIS CHECKS IT. By reading the C++ source and extracting the parameter names
that if-else chain compares against. That is a text search rather than a compile,
which is a real limitation and is stated here rather than hidden: it can be
defeated by writing the comparison differently. It cannot be defeated by simply
forgetting to add one, which is the mistake that actually happened four times.

WHAT IT DOES NOT CHECK. Whether the value is then USED for anything. A parameter
can be read into a field that nothing consults, which is a different failure with
a different shape. `StunSeconds` is in that state today: it is read, and no
gameplay code applies it yet.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PARSER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
          / "CataclysmSkillShape.cpp")

sys.path.insert(0, str(REPO_ROOT / "tools"))

#: How the parser names a parameter it recognises: Key.Equals(TEXT("Radius"), ...)
KEY_COMPARISON = re.compile(r'Key\.Equals\(\s*TEXT\("([A-Za-z]+)"\s*\)')

#: Parameters the sheet may write that the C++ deliberately does not read.
#:
#: EMPTY ON PURPOSE. If something belongs here later, say why in a comment beside
#: it rather than adding a bare name, because an entry here is a parameter the
#: designer can write and the game will refuse.
DELIBERATELY_NOT_READ: set[str] = set()


@pytest.fixture(scope="module")
def generator():
    import generate_datatables as gen
    return gen


@pytest.fixture(scope="module")
def parser_source() -> str:
    if not PARSER.is_file():
        pytest.skip(f"{PARSER} is not present")
    return PARSER.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def keys_the_cpp_reads(parser_source: str) -> set[str]:
    found = set(KEY_COMPARISON.findall(parser_source))
    assert found, (
        f"no parameter names could be extracted from {PARSER.name}. Either the "
        f"parser stopped using Key.Equals(TEXT(\"...\")) to match names, in "
        f"which case fix KEY_COMPARISON in this file, or the file moved. Every "
        f"assertion below would otherwise pass having compared nothing.")
    return found


def test_every_parameter_the_generator_allows_is_read_by_the_cpp(
        generator, keys_the_cpp_reads) -> None:
    allowed = set(generator.SHAPE_RIDERS)
    for params in generator.SHAPE_PARAMS.values():
        allowed |= set(params)

    missing = sorted(allowed - keys_the_cpp_reads - DELIBERATELY_NOT_READ)
    assert not missing, (
        f"{missing} can be written in the Shape Params cell of the Weapon Skills "
        f"sheet and {PARSER.name} does not read them. A skill stating one has its "
        f"ENTIRE parameter cell rejected, not just that entry, so it also loses "
        f"every other number it stated. Add a branch to ParseParams and a field "
        f"to FCataclysmSkillShapeParams. This exact mistake was issues #588, "
        f"#590, #621 and #622.")


def test_the_cpp_reads_nothing_the_generator_would_refuse(
        generator, keys_the_cpp_reads) -> None:
    """The other direction, which is untidy rather than broken.

    A parameter the C++ reads and the generator refuses cannot reach a generated
    table, so it is a branch that can never run. Worth knowing about, because it
    usually means a name was changed on one side only.
    """
    allowed = set(generator.SHAPE_RIDERS)
    for params in generator.SHAPE_PARAMS.values():
        allowed |= set(params)

    # Mode's values are movement modes rather than parameters, and the parser
    # compares against those names with the same call shape.
    modes = set(generator.MOVEMENT_MODES)

    unreachable = sorted(keys_the_cpp_reads - allowed - modes)
    assert not unreachable, (
        f"{PARSER.name} reads {unreachable}, which tools/generate_datatables.py "
        f"would refuse, so no generated table can ever contain them. Either the "
        f"name differs between the two, or the parameter was removed from the "
        f"generator and left here.")


def test_the_four_that_were_missing_are_all_present_now(
        keys_the_cpp_reads) -> None:
    """Named one by one, so that a regression says which one came back.

    The test above would catch any of these, and it would report a list. This
    one exists because each of these four cost a separate investigation, and a
    failure naming the specific parameter is worth more than a failure naming a
    set.
    """
    for name, issue in (("Minions", "#622"), ("GroundPercent", "#590"),
                        ("HealthPercent", "#621"), ("StunSeconds", "#588")):
        assert name in keys_the_cpp_reads, (
            f"{PARSER.name} no longer reads {name}, which was the whole of issue "
            f"{issue}. A skill stating it loses every parameter it states.")
