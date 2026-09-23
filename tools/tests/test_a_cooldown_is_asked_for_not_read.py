"""A skill's cooldown reduction is ASKED FOR through the stat pipeline, not read off an attribute.

WHY THIS EXISTS. Issue #1981. `UCataclysmGameplayAbility::CooldownAfterReduction`
read the `CooldownReduction` gameplay attribute, and
`UCataclysmPlayerClassStats::ApplyTo` writes every attribute with an EMPTY tag
container and the default conditions. So a `cooldown_reduction` row carrying
RequiredTags, a Condition or a Scale was dropped before it reached the attribute
and changed no cooldown in play. Six enchantment sentences need exactly that,
"Summon skills have 30%-60% reduced cooldown" among them.

THE REDUCTION IS ASKED FOR, AND THEN IT DIVIDES. Issue #2000. The reduction is
an ordinary stat asked through `StatForSkill`, whose gear rows are FLAT, and
`FinalCooldown` divides the skill's base cooldown by one plus it. Multiplying
instead would make cooldown reduction lengthen a cooldown, so this file pins
both steps and not merely the fact that something is asked. Issue #1981 first
did this through `UCataclysmStatPipeline::EvaluateRate`, which read the
INCREASES bucket; issue #2004 deleted it, and `RATE_STATS` in
`tools/generate_datatables.py` with it.

WHY IN PYTHON RATHER THAN IN THE AUTOMATION SUITE. Continuous integration runs
`python -m pytest` and nothing else, so nothing under `game/Source/Cataclysm/
Tests/` runs on a pull request and this does. The automation suite has its own
test that a scoped row shortens only the skill it names; this is the cheaper
check that the wiring cannot quietly go back to reading the attribute.

IT ASSERTS THE CALL AND NOT A MENTION. Every comment in this codebase names the
functions it is about, so "the body contains the word" is satisfied by a comment
alone. The source is stripped of comments and string literals before anything is
looked for, and `test_the_comment_stripper_actually_strips` proves the stripping
happened -- without it, every assertion here could pass against a file whose code
had been deleted and whose comments remained.
"""

from __future__ import annotations

import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ABILITY_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
               / "CataclysmGameplayAbility.cpp")
COMPONENT_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmAbilitySystemComponent.cpp")

#: A comment this file relies on being removed, used to prove the stripper ran.
A_COMMENT_IN_THE_ABILITY_FILE = "A PERCENTAGE BECOMES A FRACTION HERE."


def code_only(text: str) -> str:
    """`text` with C++ comments and string literals blanked out.

    NOT BY LINE PREFIX. A wrapped expression can continue on a line that opens
    with a star, so "the line starts with `*`" is not the same question as "this
    is inside a comment". This walks the text instead.

    STRING LITERALS GO TOO, so that a name appearing inside `TEXT("...")` is not
    read as a call.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        pair = text[i:i + 2]
        if pair == "//":
            newline = text.find("\n", i)
            i = n if newline < 0 else newline
        elif pair == "/*":
            close = text.find("*/", i + 2)
            i = n if close < 0 else close + 2
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            i = min(j + 1, n)
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def body_of(code: str, signature: str, where: str) -> str:
    """The braced body of the one function whose definition starts `signature`.

    `code` must already have been through `code_only`, or a brace inside a
    comment would end the body early.
    """
    found = code.count(signature)
    assert found == 1, (
        f"{where}: expected exactly one definition starting {signature!r}, "
        f"found {found}")
    opened = code.index("{", code.index(signature))
    depth = 0
    for at in range(opened, len(code)):
        if code[at] == "{":
            depth += 1
        elif code[at] == "}":
            depth -= 1
            if depth == 0:
                return code[opened:at + 1]
    raise AssertionError(f"{where}: {signature!r} has no closing brace")


@pytest.fixture(scope="module")
def ability_code() -> str:
    return code_only(ABILITY_CPP.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def component_code() -> str:
    return code_only(COMPONENT_CPP.read_text(encoding="utf-8"))


def test_the_comment_stripper_actually_strips() -> None:
    """Without this, every other assertion here could pass on comments alone."""
    raw = ABILITY_CPP.read_text(encoding="utf-8")
    assert A_COMMENT_IN_THE_ABILITY_FILE in raw, (
        "the comment this check is anchored to has been reworded; re-anchor it "
        "rather than deleting this test, which is the only thing keeping the "
        "rest of this file from passing against comments")
    assert A_COMMENT_IN_THE_ABILITY_FILE not in code_only(raw), (
        "code_only left a comment in place, so every check below is vacuous")


def test_the_cooldown_lookup_asks_for_the_reduction(ability_code: str) -> None:
    """It asks, rather than reading the attribute. Issues #1981 and #2000."""
    body = body_of(ability_code,
                   "float UCataclysmGameplayAbility::CooldownAfterReduction(",
                   "CataclysmGameplayAbility.cpp")
    assert "StatForSkill(" in body, (
        "CooldownAfterReduction no longer asks for the reduction, so a "
        "cooldown_reduction row carrying RequiredTags, a Condition or a Scale "
        "is dropped again and four enchantment sentences stop working. "
        "Issue #1981")


def test_the_cooldown_lookup_still_reads_the_attribute_for_a_character_with_no_rows(
        ability_code: str) -> None:
    """The fall through is not optional: every enemy takes it."""
    body = body_of(ability_code,
                   "float UCataclysmGameplayAbility::CooldownAfterReduction(",
                   "CataclysmGameplayAbility.cpp")
    assert "GetNumericAttribute(" in body, (
        "the attribute route is gone, so an ability system with no recorded "
        "stat line -- every enemy, and a player before its first refresh -- "
        "loses its cooldown reduction entirely")


def test_the_one_caller_hands_over_the_skills_tags(ability_code: str) -> None:
    body = body_of(ability_code,
                   "void UCataclysmGameplayAbility::ApplyCooldown(",
                   "CataclysmGameplayAbility.cpp")
    assert "SkillTagsForStats()" in body, (
        "ApplyCooldown stopped passing the skill's tags, so every cooldown row "
        "is asked with an empty container and no scoped row reaches anything")


def test_the_asked_for_figure_is_divided_by_and_not_multiplied(
        ability_code: str) -> None:
    """A cooldown DIVIDES by its reduction. Issue #2000.

    WHAT THIS REPLACED, AND WHY. It used to assert that a rate lookup on the
    ability system called `EvaluateRate`. That lookup is gone: its divisor was
    built from the INCREASES bucket, and the game's data puts cooldown
    reduction in the FLAT bucket, so the `Haste` affix read as nothing.
    Dividing now happens in `FinalCooldown`, which is where it happened before
    any of this, and `CooldownDivisor` floors a negative there so a reduction
    below nought lengthens nothing.
    """
    body = body_of(ability_code,
                   "float UCataclysmGameplayAbility::CooldownAfterReduction(",
                   "CataclysmGameplayAbility.cpp")
    assert "FinalCooldown(" in body, (
        "CooldownAfterReduction no longer divides through FinalCooldown, so "
        "nothing guarantees that a cooldown divides by its reduction rather "
        "than multiplying, and nothing floors a negative reduction")
