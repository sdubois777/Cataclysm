"""The value kinds a data row may carry are the ones the game reads.

WHY THIS EXISTS. Issue #1791. `tools/generate_datatables.py` refuses a Passive
Effects or Enchantment Effects row whose value kind is outside `VALUE_KINDS`. The
game reads the kind in two places of its own, and each turns the name into a
bucket of the stat pipeline:

    enchantments   `EnchantmentModifierFor` in `CataclysmItem.cpp`
    passive nodes  `UCataclysmPassiveTree::AccumulateInto`

**Both send a name they have no case for to the increased bucket, in silence.**
So a kind the generator writes and a reader does not name is not refused
anywhere: it is applied as an increase of the row's value. For `removed`, whose
rows state 1, that is "You have no armor" granting one per cent more armour. A
name a reader handles and the generator refuses is a row nobody can write.
Continuous integration builds no C++, so the three lists are compared here as
text, as `test_pool_action_names_match_the_engine.py` compares the pool names.

WHAT IS ASSERTED, FOR EACH READER: every kind it names goes to the bucket of the
same name, its one fallback is the increased bucket, and the names plus that
fallback are exactly `VALUE_KINDS`.

WHAT IS NOT ASSERTED. That a bucket does the right arithmetic. That is
`CataclysmStatPipelineTests.cpp`, and for a removal
`Cataclysm.StatPipeline.ARemovedStatResolvesToZeroWhateverElseReachesIt`.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: Where each reader is, and the text its definition begins with.
READERS = {
    "enchantments": (SOURCE / "Items" / "CataclysmItem.cpp",
                     "bool EnchantmentModifierFor("),
    "passive nodes": (SOURCE / "Character" / "CataclysmPassiveTree.cpp",
                      "int32 UCataclysmPassiveTree::AccumulateInto("),
}

#: Comments are removed before anything is matched, so prose naming a kind or a
#: bucket cannot stand in for the code that reads one.
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)

#: `Effect.ValueKind.Equals(TEXT("more"), ...)` and the `Bucket = ...;` inside
#: the branch it opens, with `Effect->` accepted as well.
NAMED = re.compile(
    r'ValueKind\.Equals\(\s*TEXT\("([a-z_]+)"\)[^{]*\{\s*'
    r'\w+\.Bucket\s*=\s*ECataclysmStatBucket::(\w+)\s*;')

#: `else { X.Bucket = ECataclysmStatBucket::Increased; }`, the branch every name
#: without a case of its own reaches.
FALLBACK = re.compile(
    r'\belse\s*\{\s*\w+\.Bucket\s*=\s*ECataclysmStatBucket::(\w+)\s*;\s*\}')

#: Every comparison of the kind column, whatever it compares against.
ANY_COMPARISON = re.compile(r"ValueKind\.Equals\(")

PARSE_ADVICE = ("Read the C++ and change the pattern at the top of this file "
                "rather than the lists it compares.")


def body_of(code: str, opening: str) -> str:
    """The definition that begins with `opening`, braces matched, or empty."""
    start = code.find(opening)
    if start < 0:
        return ""
    brace = code.find("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[start:index + 1]
    return ""


@pytest.fixture(scope="module")
def readings() -> dict[str, tuple[str, list[tuple[str, str]], list[str], int]]:
    """For each reader: its body, the named kinds, the fallbacks, the count of
    comparisons."""
    out = {}
    for label, (path, opening) in READERS.items():
        if not path.is_file():
            pytest.skip(f"{path.name} is not present")
        code = COMMENT.sub("", path.read_text(encoding="utf-8"))
        body = body_of(code, opening)
        out[label] = (body, NAMED.findall(body), FALLBACK.findall(body),
                      len(ANY_COMPARISON.findall(body)))
    return out


def test_the_parser_read_every_comparison_in_both_readers(readings):
    """Without this, a reader reshaped so the pattern stopped matching would
    make the comparisons below fail with the wrong explanation, or pass having
    read one branch of four."""
    for label, (body, named, fallbacks, comparisons) in readings.items():
        path, opening = READERS[label]
        assert body, f"no definition beginning {opening!r} in {path.name}. {PARSE_ADVICE}"
        assert named and len(named) == comparisons, (
            f"the {label} reader compares the kind column {comparisons} times "
            f"and {len(named)} of them were read as a name and a bucket. "
            f"{PARSE_ADVICE}")
        assert len(fallbacks) == 1, (
            f"the {label} reader has {len(fallbacks)} branches reached by a "
            f"kind with no case of its own, and this reads exactly one. "
            f"{PARSE_ADVICE}")


def test_each_kind_a_reader_names_goes_to_the_bucket_of_that_name(readings):
    """"removed" to `Removed`, "more" to `More`. A name sent to another bucket is
    the silent failure this file exists for, arrived at by an edit rather than
    by an omission."""
    wrong = [f"the {label} reader sends {name!r} to {bucket}"
             for label, (_, named, _, _) in readings.items()
             for name, bucket in named
             if bucket.lower() != name]
    assert not wrong, "; ".join(wrong)


def test_every_kind_the_generator_writes_is_one_both_readers_name(readings):
    generator = set(gen.VALUE_KINDS)
    for label, (_, named, fallbacks, _) in readings.items():
        engine = {name for name, _ in named} | {fallbacks[0].lower()}
        assert engine == generator, (
            f"only the generator writes {sorted(generator - engine)}; only the "
            f"{label} reader names {sorted(engine - generator)}. A kind the "
            f"reader has no case for is applied as an increase of the row's "
            f"value. Add it to VALUE_KINDS in tools/generate_datatables.py and "
            f"to both readers together.")
