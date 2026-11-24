"""A death is marked once, even when a blow dealt from inside it reaches back to the dying character.

WHY THIS EXISTS. Issue #2871. On 2026-11-24 the owner saw at least 13 identical "Masterful Fetish of Venom" on the
floor, each a real item. `UCataclysmSkillEffects::MarkDead` refused a second death by asking for the dead tag at its TOP
and put the tag on at its BOTTOM, after five hooks that can strike. Harrower's axe, leaping from the dying creature,
struck a creature carrying Sacrificial Bond; the bond paid a share of the leap to its allies by `ReduceHealthDirectly`,
the dying creature among them; that health write ran its `HandleDeath` again before the tag was on; and so on, once per
leap. Every one of those deaths paid the kill, from the same seed in the same frame. The owner's log holds 31 "ranged
attack" lines on the bonded Brute in one frame.

THE FIX IS TWO GUARDS ON ONE RECORD. `MarkDead` puts the actor in a set of deaths being marked before its hooks run and
refuses an actor already in it; it takes the actor out once the tag is on. And
`UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero` asks `IsBeingMarkedDead`, as it asks `IsDead`, before it starts
a death at all.

AND ISSUE #2874: an enemy modifier that builds stacks on whatever its creature hits -- Infernal Brand, Withering Blows,
Touch of Nothing -- is called after the blow's health write, so for the blow that kills, after the death. The owner's
log shows a brand reaching five stacks and bursting for 367 two milliseconds after the player's death line. Each asks
`IsDead` of its target before it grants anything.

WHY IT IS CHECKED FROM PYTHON AS WELL. Continuous integration compiles no C++, so the automation tests that prove these
(`Cataclysm.BuriedWeaponKills.AHostWhoseLeapStrikesABondedAllyDiesOnceAndDropsEachItemOnce` and
`Cataclysm.EnemyModifiers.TheBlowThatKillsThePlayerNeitherBrandsTheBodyNorBurstsOnIt`) never run on a pull request.
Reading the source does.

WHAT IS NOT CHECKED HERE. Whether the guards work: that is what those tests are for. This file checks that each guard
is present, in its function, in the order that makes it a guard, and not in a comment.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
SKILL_EFFECTS = SOURCE / "AbilitySystem" / "CataclysmSkillEffects.cpp"
VITAL_SET = SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"
ENEMY_MODIFIERS = SOURCE / "Character" / "CataclysmEnemyModifiers.cpp"


def body_of(path: pathlib.Path, signature: str) -> str:
    """The body of the function whose definition is `signature(`, with every line comment removed.

    COMMENTS ARE STRIPPED because the paragraphs above each guard name the very calls checked here, so a plain search
    would pass with the call itself deleted.
    """
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    opening = text.find(f"{signature}(")
    assert opening != -1, f"{path.name} no longer defines {signature}; if it was renamed, rename it here"
    brace = text.find("{", opening)
    depth = 0
    end = brace
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index
                break
    return "\n".join(re.sub(r"//.*$", "", line) for line in text[brace:end].splitlines())


def position(body: str, needle: str, where: str) -> int:
    """Where `needle` first appears in `body`, failing with `where` named when it does not."""
    found = body.find(needle)
    assert found != -1, f"{where} no longer holds `{needle}` outside a comment"
    return found


def test_mark_dead_refuses_a_death_already_being_marked_before_any_hook_runs() -> None:
    body = body_of(SKILL_EFFECTS, "bool UCataclysmSkillEffects::MarkDead")
    where = "UCataclysmSkillEffects::MarkDead"

    refused = position(body, "BeingMarked.Contains(Actor)", where)
    added = position(body, "BeingMarked.Add(Actor)", where)
    first_hook = position(body, "UCataclysmCurseSpread::SpreadFromDying(Actor)", where)
    leap = position(body, "UCataclysmBuriedWeapon::LeapFromDying(Actor)", where)
    tagged = position(body, "System->AddLooseGameplayTag(Dead)", where)
    removed = position(body, "BeingMarked.Remove(Actor)", where)
    announced = position(body, "UCataclysmCombatEvents::NoteDeath(Actor)", where)

    assert refused < added, "MarkDead must refuse an actor already being marked before it adds it"
    assert added < first_hook < leap, (
        "MarkDead must record the death as being marked BEFORE its hooks run: a hook's blow (Harrower's leap into a "
        "bonded ally, issue #2871) can reach back to the dying actor, and without the record its HandleDeath runs again")
    assert tagged < removed < announced, (
        "MarkDead must keep the record until the dead tag is on, and take it out before the death is announced")


def test_a_health_write_at_nought_starts_no_second_death_while_one_is_being_marked() -> None:
    body = body_of(VITAL_SET, "void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero")
    where = "UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero"

    asked = position(body, "UCataclysmSkillEffects::IsBeingMarkedDead(Character)", where)
    handled = position(body, "Character->HandleDeath()", where)
    assert asked < handled, (
        "NotifyIfHealthReachedZero must ask IsBeingMarkedDead before it starts a death, as it asks IsDead: a blow dealt "
        "from inside MarkDead's hooks writes the dying character's health at nought again (issue #2871)")


@pytest.mark.parametrize("function,grant", [
    ("float UCataclysmEnemyModifiers::BrandOnHit", "UCataclysmStacks::NoteInfernalBrand("),
    ("bool UCataclysmEnemyModifiers::WitherOnHit", "UCataclysmStacks::NoteWithering("),
    ("bool UCataclysmEnemyModifiers::TouchOfNothingOnHit", "UCataclysmStacks::NoteTouchOfNothing("),
])
def test_a_stack_built_on_hit_is_refused_to_a_dead_target(function: str, grant: str) -> None:
    body = body_of(ENEMY_MODIFIERS, function)
    asked = position(body, "UCataclysmSkillEffects::IsDead(Target)", function)
    tag = position(body, "UCataclysmSkillEffects::ApplyTagForDuration(", function)
    granted = position(body, grant, function)
    assert asked < tag and asked < granted, (
        f"{function} must ask whether its target is dead before it puts on its status or grants a stack: the blow "
        "that kills reaches it after the death, and a brand burst on the player's body (issue #2874)")
