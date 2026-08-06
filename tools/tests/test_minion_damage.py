"""What a minion is, stated in the design and checked against the data.

WHAT THIS USED TO ASSERT. Until issue #209 this file checked two numbers: that
`docs/Cataclysm_GDD_v2.md`, the main design document, stated a minion's attack
dealt 30% of its summoner's weapon damage once per second, and that
`game/Source/Cataclysm/AbilitySystem/CataclysmMinion.h` held the same two figures
in `DamagePercentOfSummoner` and `AttackIntervalSeconds`. It existed because
issue #165 found the code carried 25 with a comment admitting the number was a
judgement rather than a design figure.

**Both of those design figures have been reversed and the test could not survive
unchanged.** The project owner ruled that each minion skill gets its own stats,
that base scaling comes from a primary attribute rather than from the summoner's
weapon damage, and that minion affixes exist on gear.

WHY THE REPLACEMENT IS A STRONGER GUARD THAN WHAT IT REPLACES. The old test could
see two numbers in one paragraph. It could not see that the paragraph was already
false. The design justified its rule "for two skills", and
`game/Data/WeaponSkills.csv` holds SIX skills tagged `Type.Minion`. Two of those
six state their own health and attack rate in their descriptions -- a bolt turret
has 200 health and fires every 1.5 seconds, a ballista has 500 health and fires
every 2 seconds -- which the rule of "once per second, no stats of its own"
contradicted outright. Nothing noticed for a year.

So this file now checks the design against the data rather than against one
constant: the counts it claims, the two minion stat blocks it quotes, and the
enchantment it names as the one exception to the blocking rule.

WHAT IS DELIBERATELY RECORDED RATHER THAN FIXED. The four constants in
`CataclysmMinion.h` are now behind the design. They cannot be changed in the same
work, because `Build.bat` refuses to run while the Unreal editor is open, and a
C++ change that cannot be compiled should not be shipped. Issue #340 tracks it and
`test_the_code_is_recorded_as_behind_the_design` is what stops that being
forgotten.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN_DOC = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"
MINION_HEADER = (
    REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmMinion.h"
)
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
ENCHANTMENTS = REPO_ROOT / "game" / "Data" / "EnchantmentsPositive.csv"

#: The paragraph that opens the minion rules.
SECTION_START = "**Every minion type has its own stats.**"

#: Where the minion rules stop, so a match cannot wander into the next topic.
SECTION_ENDS_AT = "**The Shape column is deliberately separate"


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def section() -> str:
    """The minion rules of the design document, and only those."""
    text = DESIGN_DOC.read_text(encoding="utf-8")
    start = text.find(SECTION_START)
    if start == -1:
        pytest.fail(
            f"{DESIGN_DOC.name} no longer contains a paragraph beginning "
            f"{SECTION_START!r}. The design must state what a minion is; see "
            f"issues #165 and #209."
        )
    end = text.find(SECTION_ENDS_AT, start)
    return text[start : end if end != -1 else len(text)]


@pytest.fixture(scope="module")
def minion_skills() -> list[dict]:
    """Every row of the weapon skill table tagged as producing a minion."""
    with WEAPON_SKILLS.open(encoding="utf-8-sig") as handle:
        return [row for row in csv.DictReader(handle)
                if "Type.Minion" in (row.get("Tags") or "")]


# --------------------------------------------------------------------------
# The rule the design now states
# --------------------------------------------------------------------------

def test_a_minion_has_its_own_stats(section):
    """The reversal itself. Issue #209."""
    collapsed = unwrapped(section)
    assert "Every minion type has its own stats" in collapsed
    assert "A minion is not a percentage of its summoner" in collapsed


def test_the_three_channels_are_named(section):
    """The inheritance split is the whole design. If the document stops naming
    all three, someone implementing it has to guess, which is what produced the
    30% figure this replaced.

    THE COUNT IS CHECKED AS WELL AS THE NAMES, because they can disagree: an
    edit that changes "three channels" to "two" while leaving the table of three
    intact is exactly the kind of half-done change that reads as deliberate.
    """
    collapsed = unwrapped(section)
    assert "exactly three channels" in collapsed, (
        f"{DESIGN_DOC.name} no longer says a minion reaches its summoner "
        f"through exactly three channels. Issue #209.")
    for channel in ("Its side", "the summoner's **level**",
                    "One **primary attribute**"):
        assert channel in collapsed, (
            f"{DESIGN_DOC.name} no longer names {channel!r} as one of the three "
            f"channels a minion reaches its summoner through. Issue #209.")


def test_everything_else_is_blocked(section):
    """A blocklist that stops naming what it blocks is not a rule. Weapon damage
    is the reversal itself and must be in the list.

    THE SENTENCE IS SLICED OUT FIRST. Searching the whole section for "weapon
    damage" passes on the paragraph that explains what was reversed, which
    quotes the old rule, so dropping weapon damage from the blocklist was not
    caught. The blocklist has to be checked against itself.
    """
    collapsed = unwrapped(section)
    opening = "**Everything else is blocked unless a modifier says"
    start = collapsed.find(opening)
    assert start != -1, (
        f"{DESIGN_DOC.name} no longer states that everything not named is "
        f"blocked. Issue #209.")
    blocklist = collapsed[start:collapsed.find(".", collapsed.find("does not", start))]
    for blocked in ("weapon damage", "attack speed", "critical strike",
                    "resistances", "cooldown reduction"):
        assert blocked in blocklist, (
            f"{DESIGN_DOC.name} no longer lists {blocked!r} among the things a "
            f"minion does not take from its summoner. The blocklist read:\n"
            f"{blocklist}\nIssue #209.")


def test_the_double_scaling_reason_is_stated(section):
    """WHY the attribute route was chosen, not only that it was. Without the
    reason, a later edit that puts minion damage back on weapon damage while
    keeping minion affixes would look harmless."""
    collapsed = unwrapped(section)
    assert "scaled them twice from one investment" in collapsed, (
        f"{DESIGN_DOC.name} no longer explains that scaling from an attribute "
        f"is what stops minions being scaled twice by one investment. That is "
        f"the reason the reversal is safe. Issue #209.")


def test_count_is_an_enchantment_and_never_an_affix(section):
    """The single highest-impact minion modifier in the genre. The design has to
    say where it lives and why, or it will be proposed as an affix."""
    collapsed = unwrapped(section)
    assert "It is never an affix" in collapsed
    assert "eight ring slots" in collapsed, (
        f"{DESIGN_DOC.name} no longer gives the reason count cannot be an "
        f"affix. There are eight ring slots, so a '+1 minion' suffix would be "
        f"eight from rings alone. Issue #209.")


# --------------------------------------------------------------------------
# The design is checked against the data, which is what the old test could not do
# --------------------------------------------------------------------------

def test_the_document_no_longer_says_there_are_two_minion_skills(
        section, minion_skills):
    """THE CLAIM THAT MADE THE OLD RULE LOOK REASONABLE. It said a minion stat
    family was not wanted "for two skills". There are six."""
    assert len(minion_skills) == 6, (
        f"{WEAPON_SKILLS.name} now has {len(minion_skills)} skills tagged "
        f"Type.Minion rather than 6. The design document quotes the count; "
        f"update both together.")
    assert "There are six" in unwrapped(section), (
        f"{DESIGN_DOC.name} no longer states how many minion skills there are. "
        f"The count is the evidence that the retired rule was wrong.")


@pytest.mark.parametrize(
    "skill_name,health,interval",
    [("Bolt Turret", 200, 1.5), ("Ballista", 500, 2)],
)
def test_the_two_stat_blocks_the_document_quotes_are_in_the_data(
        section, minion_skills, skill_name, health, interval):
    """The document says these two minions already had stats of their own, which
    is its evidence that the old rule was false. Read out of the skill table
    rather than trusted, because a quoted number is exactly what goes stale."""
    row = next((r for r in minion_skills if r["SkillName"] == skill_name), None)
    assert row is not None, (
        f"{WEAPON_SKILLS.name} no longer has a Type.Minion skill called "
        f"{skill_name!r}, which {DESIGN_DOC.name} quotes.")
    description = row["SkillDescription"]
    assert f"{health} HP" in description, (
        f"{skill_name} no longer states {health} health in "
        f"{WEAPON_SKILLS.name}. {DESIGN_DOC.name} quotes that figure as "
        f"evidence that minions already had stats of their own.")
    assert re.search(rf"every {interval:g} second", description), (
        f"{skill_name} no longer states an interval of {interval:g} seconds in "
        f"{WEAPON_SKILLS.name}.")

    collapsed = unwrapped(section)
    assert f"{health} health" in collapsed
    assert f"every {interval:g} seconds" in collapsed


def test_the_named_inheritance_exception_actually_exists(section):
    """THE TRAP THIS AVOIDS. A blanket zero-inheritance rule would contradict
    the enchantment table on the day it was written, because one enchantment
    already grants partial inheritance. The design names it as the exception, so
    the name has to be real."""
    quoted = "Summoned minions inherit 10%-25% of your armor and resistances"
    assert quoted in unwrapped(section), (
        f"{DESIGN_DOC.name} no longer names the one enchantment that is an "
        f"exception to the blocking rule. Without it the document and "
        f"{ENCHANTMENTS.name} disagree. Issue #209.")
    assert quoted in ENCHANTMENTS.read_text(encoding="utf-8"), (
        f"{ENCHANTMENTS.name} no longer contains {quoted!r}, which "
        f"{DESIGN_DOC.name} names as the single exception to the rule that "
        f"nothing crosses from summoner to minion.")


def test_the_count_enchantments_the_rule_depends_on_exist():
    """The design says count is gear-modifiable through enchantments. If none
    exist, minion count has no lever at all and the rule is empty."""
    text = ENCHANTMENTS.read_text(encoding="utf-8")
    found = [line for line in text.splitlines()
             if "minion" in line.lower()
             and ("additional minions" in line or "maximum minion count" in line)]
    assert found, (
        f"{ENCHANTMENTS.name} has no enchantment that raises the minion count. "
        f"{DESIGN_DOC.name} states count is gear-modifiable only through "
        f"enchantments, so without one the archetype cannot scale at all.")


# --------------------------------------------------------------------------
# The gap that is recorded rather than hidden
# --------------------------------------------------------------------------

def test_the_code_is_recorded_as_behind_the_design():
    """SAY WHAT DID NOT WORK. `CataclysmMinion.h` still holds the two constants
    the design reversed, because Build.bat refuses to run while the Unreal
    editor is open and a C++ change that cannot be compiled should not ship.

    This test asserts the gap is TRACKED, not that it is closed. When issue #340
    lands, rewrite this to compare the header against the minion type table.
    """
    header = MINION_HEADER.read_text(encoding="utf-8")
    stale = [name for name in ("DamagePercentOfSummoner", "AttackIntervalSeconds")
             if name in header]
    if not stale:
        pytest.fail(
            f"{MINION_HEADER.name} no longer declares "
            f"{', '.join(('DamagePercentOfSummoner', 'AttackIntervalSeconds'))}, "
            f"so issue #340 has been done. Rewrite this test to compare the "
            f"header against the minion type table instead of recording a gap "
            f"that has closed.")
    assert "#209" in DECISIONS.read_text(encoding="utf-8"), (
        "docs/DECISIONS.md does not record the minion reversal, so the reason "
        "the code and the design disagree is written nowhere.")


def test_the_retired_rule_is_not_stated_as_current(section):
    """Kept out by the BOLD form it was written in, not by the words.

    The document quotes the retired rule in plain prose when it explains what
    was reversed, and it should: a reader needs to know what changed. What must
    not come back is the rule ASSERTED. It was asserted in bold -- "deals **30%
    of its summoner's weapon damage**, and it attacks **once per second**" --
    and the explanation of the reversal does not use bold, so the markers are
    what tell the two apart.
    """
    collapsed = unwrapped(section)
    for phrase in ("**30% of its summoner's weapon damage**",
                   "**once per second**",
                   "does not have and does not want"):
        assert phrase not in collapsed, (
            f"the reversed rule is stated as current in {DESIGN_DOC.name}: "
            f"{phrase!r}. Issue #209 replaced it. Quoting it in prose while "
            f"explaining the reversal is fine; asserting it is not.")


def test_the_reversal_says_what_it_reversed(section):
    """The other half of the test above. A document that silently drops a rule
    leaves anyone holding the old one with no way to find out."""
    collapsed = unwrapped(section)
    assert "This reverses the rule this document used to state" in collapsed
    assert "30% of its summoner's weapon damage" in collapsed, (
        f"{DESIGN_DOC.name} no longer says what the old rule was, so a reader "
        f"who knows the 30% figure cannot tell it has been replaced.")
