"""The rule that stops a player being stunned repeatedly with no chance to act.

WHAT THIS IS FOR. Issue #216. The project owner's requirement was that crowd
control must not become tedious the way it is in many games in the genre, where
the smallest hit can stun and a player can be stun-locked until they die.

THE ANSWER, given 2026-08-05, is three rules and not one:

    1. A target with a lot of health is not stunned by small hits at all.
    2. A target that IS stunned cannot be stunned again for at least 5 seconds.
    3. Bosses are immune to stun outright.

BOTH OF THE FIRST TWO ARE NEEDED. A damage threshold alone still allows chain
stunning by large hits. An immunity window alone still allows constant
interruption by small ones.

WHAT IS TESTED HERE AND WHAT IS NOT. Rules 1 and 3 are arithmetic on one hit and
are enforced in `sim/cataclysm_sim/damage.py`, so they are tested. Rule 2 is
about time, and `damage.resolve` has no clock -- it resolves one hit. The
constant `STUN_IMMUNITY_SECONDS` records the figure so the game and the design
document cannot disagree about it, and the tests below check that the constant
says 5 and that the design document says the same. **Nothing here proves the
game enforces the window**, because nothing in this repository implements it yet.
That is stated plainly rather than left to be assumed.

WHY THE THRESHOLD IS 10%. The three surveyed games do not agree -- Last Epoch
uses 5% of maximum health, Path of Exile about 10%, Path of Exile 2 uses 15% --
and 10% is the middle. `damage.py` carries the full reasoning and the sources are
in `docs/DECISIONS.md`.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import damage as dm

#: A defender with no mitigation at all, so the damage that lands is the damage
#: dealt and the threshold is the only thing being measured.
MAX_HEALTH = 10_000.0


def defender(**kwargs) -> dm.Defender:
    kwargs.setdefault("health", MAX_HEALTH)
    return dm.Defender(**kwargs)


def certain_stun(damage: float, **kwargs) -> dm.Attacker:
    """An attacker that stuns on every eligible hit, so a failure to stun is the
    rule refusing rather than a dice roll."""
    return dm.Attacker(damage=damage, subtype="Blunt",
                       bonus_stun_chance=100.0, **kwargs)


def landed(attacker: dm.Attacker, target: dm.Defender) -> dm.Resolution:
    return dm.resolve(attacker, target, force_evade=False, force_block=False)


# --------------------------------------------------------------------------
# Rule 1: a small hit cannot stun a healthy target
# --------------------------------------------------------------------------

def test_the_threshold_is_a_share_of_maximum_health_not_a_flat_number():
    """A flat threshold would be trivial at high health and impossible at low,
    which is the opposite of the requirement."""
    assert dm.STUN_DAMAGE_THRESHOLD == 10.0
    small = defender(health=100.0)
    large = defender(health=100_000.0)
    assert dm.can_be_stunned(10.0, small)
    assert not dm.can_be_stunned(10.0, large)


@pytest.mark.parametrize("share,expected", [
    (0.0, False),
    (0.05, False),
    (0.099, False),
    (0.10, True),
    (0.50, True),
    (1.00, True),
])
def test_a_hit_stuns_only_once_it_takes_enough_of_the_health_bar(share,
                                                                expected):
    assert dm.can_be_stunned(MAX_HEALTH * share, defender()) is expected


def test_a_small_hit_does_not_stun_however_certain_the_attacker_is():
    """The requirement in one test. 100% chance to stun, and it still does not,
    because the hit was a scratch."""
    scratch = certain_stun(MAX_HEALTH * 0.05)
    result = landed(scratch, defender())
    assert result.dealt_to_health == pytest.approx(MAX_HEALTH * 0.05)
    assert not result.stunned
    assert result.stun_seconds == 0.0


def test_a_heavy_hit_from_the_same_attacker_does_stun():
    """The other half, or the test above would pass on an attacker that could
    never stun anything."""
    heavy = certain_stun(MAX_HEALTH * 0.20)
    assert landed(heavy, defender()).stunned


def test_the_threshold_reads_damage_dealt_rather_than_damage_swung():
    """A well defended character stops being interrupted by chip damage, which
    is the point of the rule. Two identical hits, one against a defender whose
    armour takes it below the threshold."""
    swung = certain_stun(MAX_HEALTH * 0.12)
    assert landed(swung, defender()).stunned

    armoured = defender(damage_reduction=50.0)
    result = landed(swung, armoured)
    assert result.dealt_to_health < MAX_HEALTH * dm.STUN_DAMAGE_THRESHOLD / 100
    assert not result.stunned


def test_a_defender_with_no_health_is_not_stunnable_rather_than_always():
    """Zero maximum health would make the threshold zero, so every hit would
    clear it. Refused instead."""
    assert not dm.can_be_stunned(1.0, defender(health=0.0))


# --------------------------------------------------------------------------
# A skill whose stated purpose is to stun ignores the threshold
# --------------------------------------------------------------------------

def test_a_designed_stun_ignores_the_damage_threshold():
    """Four shipped skills in `game/Data/WeaponSkills.csv` stun by design --
    Shield Bash, Shockwave Leap, Lunge and Whip Swing. A threshold that made
    Shield Bash fail against a healthy target would leave the skill doing
    nothing it was written to do."""
    tiny = dm.Attacker(damage=MAX_HEALTH * 0.01, subtype="Blunt",
                       bonus_stun_chance=100.0, stun_is_designed=True)
    assert not dm.can_be_stunned(MAX_HEALTH * 0.01, defender())
    assert landed(tiny, defender()).stunned


def test_a_designed_stun_still_obeys_crowd_control_resistance():
    """It skips the threshold, not every defence. A character at 100 crowd
    control resistance cannot be stunned at all, which the design document has
    said since before this rule existed."""
    tiny = dm.Attacker(damage=MAX_HEALTH * 0.01, subtype="Blunt",
                       bonus_stun_chance=100.0, stun_is_designed=True)
    immune = defender(crowd_control_resistance=100.0)
    assert dm.effective_stun_chance(tiny, immune) == pytest.approx(0.0)
    assert not landed(tiny, immune).stunned


def test_an_ordinary_attack_is_not_a_designed_stun_by_default():
    """The flag has to be opted into, or every Blunt swing would bypass the
    threshold and the rule would do nothing."""
    assert dm.Attacker(damage=1.0).stun_is_designed is False


# --------------------------------------------------------------------------
# Rule 3: a boss cannot be stunned
# --------------------------------------------------------------------------

def test_a_boss_cannot_be_stunned_by_a_heavy_hit():
    huge = certain_stun(MAX_HEALTH * 0.90)
    assert landed(huge, defender()).stunned
    assert not landed(huge, defender(is_boss=True)).stunned


def test_a_boss_cannot_be_stunned_by_a_designed_stun_either():
    """This is the half of the rule that the issue said had to be decided
    together: whatever stops the player being stun-locked is what stops the
    player chain-stunning a boss. A designed stun skips the threshold and does
    not skip this."""
    bash = dm.Attacker(damage=MAX_HEALTH * 0.90, subtype="Blunt",
                       bonus_stun_chance=100.0, stun_is_designed=True)
    assert landed(bash, defender()).stunned
    assert not landed(bash, defender(is_boss=True)).stunned


def test_boss_immunity_is_checked_before_the_threshold():
    assert not dm.can_be_stunned(MAX_HEALTH, defender(is_boss=True))


def test_a_defender_is_not_a_boss_by_default():
    assert dm.Defender(health=1.0).is_boss is False


# --------------------------------------------------------------------------
# Rule 2: the immunity window. Recorded, not enforced here.
# --------------------------------------------------------------------------

def test_the_immunity_window_is_five_seconds():
    """The project owner said "at least a 5 second stun immunity window".

    NOT ENFORCED IN THIS MODULE. `damage.resolve` resolves one hit and has no
    clock. This pins the number so the design document, the model and whatever
    the game eventually implements cannot disagree about it.
    """
    assert dm.STUN_IMMUNITY_SECONDS == 5.0


def test_the_window_is_longer_than_any_stun_the_game_can_apply():
    """Otherwise a target could be stunned again before recovering, which is the
    thing the window exists to prevent. The longest stun in
    `game/Data/WeaponSkills.csv` and the enchantment tables is 3 seconds, from
    the Brute's Heart set bonus."""
    longest_designed_stun = 3.0
    assert dm.STUN_IMMUNITY_SECONDS > longest_designed_stun
    assert dm.STUN_IMMUNITY_SECONDS > dm.BLUNT_STUN_SECONDS


# --------------------------------------------------------------------------
# The design document says the same
# --------------------------------------------------------------------------

def unwrapped(text: str) -> str:
    return " ".join(text.split())


@pytest.fixture(scope="module")
def gdd() -> str:
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    path = root / "docs" / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the design document is not present")
    return unwrapped(path.read_text(encoding="utf-8"))


def test_the_design_document_has_the_section():
    """Standalone and not using the fixture, so deleting the section fails by
    name rather than erroring every test that shares a fixture."""
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    path = root / "docs" / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the design document is not present")
    assert "### **Stun and the Anti-Stun-Lock Rule**" in path.read_text(
        encoding="utf-8"), (
        "the design document no longer has the section stating the rule that "
        "stops a player being stun-locked. Issue #216.")


def test_the_document_states_the_threshold_the_model_uses(gdd):
    import re

    found = re.search(
        r"at least (\d+)% of the target's maximum health", gdd)
    assert found, (
        "the design document no longer states how much of a target's maximum "
        "health a hit has to take before it can stun")
    assert float(found.group(1)) == pytest.approx(dm.STUN_DAMAGE_THRESHOLD)


def test_the_document_states_the_immunity_window_the_model_uses(gdd):
    import re

    found = re.search(r"cannot be stunned again for (\d+) seconds", gdd)
    assert found, (
        "the design document no longer states how long a target is immune to "
        "stun after being stunned")
    assert float(found.group(1)) == pytest.approx(dm.STUN_IMMUNITY_SECONDS)


def test_the_document_states_that_bosses_are_immune(gdd):
    assert "**A boss cannot be stunned at all.**" in gdd


def test_the_document_says_a_designed_stun_skips_the_threshold(gdd):
    """Without this a reader would conclude Shield Bash fails against a healthy
    target, which is the opposite of what the skill is for."""
    assert ("A skill whose stated effect is to stun ignores the damage "
            "threshold") in gdd


def test_the_document_records_that_point_four_is_still_open(gdd):
    """The project owner answered three of the issue's four questions and
    deferred the fourth. A document that reads as complete would lose that."""
    assert "not yet decided" in gdd
