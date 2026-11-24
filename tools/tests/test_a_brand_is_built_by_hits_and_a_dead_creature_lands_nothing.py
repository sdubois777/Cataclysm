"""Creature combat guards from issues #2874, #2877 and the coordinating session's addition of 2026-11-24.

WHY THIS EXISTS. Issue #2874: on 2026-11-24 a tier 1 Boss Brute killed the owner with a blow the log called "attack ...
critical" and took 396 of 430 health. It was not the Brute's swing: every creature blow the log names "attack", with no
melee, ranged or spell tag, is followed in the same frame by "<that creature>'s Infernal Brand reached five stacks ...
dealt X". The explosion sent 367, five times the Brute's 73.4, struck critically, and was cut to the health left. Its
five stacks were one thrown rock and four ticks of the same Brute's Hellfire Aura burn, because the attribute set
called `BrandOnHit` for every blow that took health, ticks included. The coordinating session ruled on 2026-11-24: the
brand is built by hits alone (its row says "on hit", and a tick is not a hit), and the explosion cannot critically
strike, as the overkill explosion and the death blast were ruled on 2026-10-07.

WHY IT IS CHECKED FROM PYTHON AS WELL. Continuous integration compiles no C++, so the automation tests that prove these
never run on a pull request. Reading the source does. Whether the guards WORK is what the Unreal tests are for; this
file checks that each guard is present, where it acts, and not in a comment.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
VITAL_SET = SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp"


def without_comments(text: str) -> str:
    """`text` with every line comment removed, so a call a comment names does not pass for the call."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in text.splitlines())


def body_of(path: pathlib.Path, signature: str) -> str:
    """The body of the function whose definition is `signature(`, with every line comment removed."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    opening = text.find(f"{signature}(")
    assert opening != -1, f"{path.name} no longer defines {signature}; if it was renamed, rename it here"
    return without_comments(block_from(text, text.find("{", opening)))


def block_from(text: str, brace: int) -> str:
    """The text from the brace at `brace` to the brace that closes it, both included."""
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError("an unbalanced brace: the source could not be read as blocks")


def position(body: str, needle: str, where: str) -> int:
    """Where `needle` first appears in `body`, failing with `where` named when it does not."""
    found = body.find(needle)
    assert found != -1, f"{where} no longer holds `{needle}` outside a comment"
    return found


def test_the_brand_is_called_only_for_a_blow_that_is_not_damage_over_time() -> None:
    """`BrandOnHit` is called once, inside the `!Hit.bIsDamageOverTime` block that builds Withering, and nowhere else.

    THE BLOCK IS FOUND BY WHAT IT HOLDS: the `if (!Hit.bIsDamageOverTime)` whose block calls `WitherOnHit`, the rule the
    owner gave Withering Blows on issue #1722 ("one stack per hit that takes health").
    """
    if not VITAL_SET.is_file():
        pytest.skip(f"{VITAL_SET.name} is not present")
    text = without_comments(VITAL_SET.read_text(encoding="utf-8"))
    calls = [match.start() for match in re.finditer(r"UCataclysmEnemyModifiers::BrandOnHit\(", text)]
    assert len(calls) == 1, (
        f"{VITAL_SET.name} calls BrandOnHit {len(calls)} times; it should call it once, for a hit and not a tick "
        "(issue #2874)")

    wither = position(text, "UCataclysmEnemyModifiers::WitherOnHit(", VITAL_SET.name)
    gate = text.rfind("if (!Hit.bIsDamageOverTime)", 0, wither)
    assert gate != -1, f"{VITAL_SET.name}: WitherOnHit is no longer behind `if (!Hit.bIsDamageOverTime)`"
    brace = text.find("{", gate)
    block = block_from(text, brace)
    assert brace < wither < brace + len(block), "the `!Hit.bIsDamageOverTime` gate found is not the one around Wither"
    assert brace < calls[0] < brace + len(block), (
        "BrandOnHit must be called inside the same `if (!Hit.bIsDamageOverTime)` block as WitherOnHit: the brand's row "
        "says 'on hit', a tick is not a hit, and four Hellfire Aura ticks branded the owner to an explosion that killed "
        "on 2026-11-24 (issue #2874)")


ENEMY_MODIFIERS = SOURCE / "Character" / "CataclysmEnemyModifiers.cpp"


def test_the_brands_explosion_is_sent_with_a_delivery_that_cannot_critically_strike() -> None:
    """The explosion's delivery sets `bCannotCriticallyStrike` before the blow is sent, and the blow is sent with it.

    Ruled by the coordinating session on 2026-11-24 (issue #2874), as the overkill explosion and a minion's death blast
    were on 2026-10-07: five hits' worth, never ten.
    """
    function = "float UCataclysmEnemyModifiers::BrandOnHit"
    body = body_of(ENEMY_MODIFIERS, function)
    declared = re.search(r"FCataclysmHitDelivery\s+(\w+)\s*;", body)
    assert declared, f"{function} no longer declares a delivery of its own for the explosion"
    name = declared.group(1)
    flagged = position(body, f"{name}.bCannotCriticallyStrike = true;", function)
    sent = position(body, f"UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Target, Damage, {name})", function)
    assert declared.start() < flagged < sent, (
        f"{function} must mark its explosion's delivery as unable to critically strike before it sends the blow: a "
        "critical explosion was ten of the creature's hits and killed the owner on 2026-11-24 (issue #2874)")


STRENGTH_TESTS = SOURCE / "Tests" / "CataclysmEnemyStrengthTests.cpp"


def test_the_melee_critical_measurements_design_hit_is_the_simulations() -> None:
    """`OutpostBossBruteDesignHit` in the Unreal measurement is the simulation's hit for the owner's Boss Brute.

    `Cataclysm.EnemyStrength.ATierOneOutpostBossBrutesMeleeCriticalOnALevelSixCharacterIsTheDesigns` asserts the
    floor-built Brute against this figure, so a figure written by hand that drifted from the model would make it measure
    against nothing. Issue #2874.
    """
    from cataclysm_sim import enemy_stats

    if not STRENGTH_TESTS.is_file():
        pytest.skip(f"{STRENGTH_TESTS.name} is not present")
    found = re.search(r"constexpr double OutpostBossBruteDesignHit = ([\d.]+);",
                      without_comments(STRENGTH_TESTS.read_text(encoding="utf-8")))
    assert found, f"{STRENGTH_TESTS.name} no longer states OutpostBossBruteDesignHit"
    model = enemy_stats.strength_on_floor("Boss", 1, "Basic", "Timed", 10, 6, kind="Brute", ring="Outpost")
    assert abs(float(found.group(1)) - model.damage_per_hit) < 1e-3, (
        f"the Unreal measurement says {found.group(1)} and the simulation {model.damage_per_hit:.4f} for a tier 1 "
        "Outpost Boss Brute's hit on floor 6 of a 10-floor Timed dungeon")
    assert model.crit_multiplier == 200.0, "the measurement's critical is the Brute's 200%; the model's moved"


SKILL_TEMPLATE = SOURCE / "AbilitySystem" / "CataclysmSkillTemplate.cpp"


def test_a_creatures_waiting_swing_asks_whether_it_died_before_its_blow_lands() -> None:
    """The timer `WhenTheSwingConnects` arms asks whether a CREATURE avatar is dead before it runs the blow.

    Issue #2877: the Corrupted Stalker's weapon-kit blows wait on `UCataclysmSkillTemplate::SwingTimer`, which nothing
    cleared at a creature's death. A creature only: a player's waiting blow is left as the owner answered #1549.
    """
    function = "void UCataclysmSkillTemplate::WhenTheSwingConnects"
    body = body_of(SKILL_TEMPLATE, function)
    armed = position(body, "SetTimer(", function)
    creature = position(body, "Cast<ACataclysmEnemyCharacter>(Avatar())", function)
    asked = position(body, "UCataclysmSkillEffects::IsDead(Creature)", function)
    deferred = body.find("Blow();", armed)
    assert deferred != -1, f"{function} no longer runs the blow from the timer it arms"
    assert armed < creature < asked < deferred, (
        f"{function}'s timer must ask whether its creature died during the wait before it runs the blow, and only a "
        "creature (Cast<ACataclysmEnemyCharacter>), so a player's waiting blow keeps the owner's ruling on #1549")
