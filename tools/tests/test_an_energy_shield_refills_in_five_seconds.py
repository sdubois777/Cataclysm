"""Every source of maximum energy shield grants a rate to refill it.

WHAT WENT WRONG. `docs/Cataclysm_GDD_v2.md` says an energy shield "refills 3
seconds after the character last took damage", and the delay was built: three
seconds in `UCataclysmRegeneration::ShieldRefillDelaySeconds`, a quarter-second
timer on every character calling it, the clock restarting on any damage that
reached health, the shield or mana.

**The rate was the part nobody supplied.** The shield refills at the character's
`energy_shield_regen` stat, and only the Ritualist had one. Every other class had
a base of zero, the only gear source was an INCREASED affix which multiplies
zero, and no item base granted it. So a Ravager wearing a Vestment held 120
maximum energy shield and a refill rate of nothing: the shield never came back at
all. The Ritualist's own took 38 seconds at level 100. Issue #1237.

The automation test for the delay could not notice, because it writes both the
shield and the rate by hand to have something to refill.

THE RULE THIS HOLDS. Every item base and every class granting maximum energy
shield grants about a fifth of it as energy shield regeneration, so a shield
refills in about five seconds however it was built and whatever class is wearing
it: 4.8 to 5.5 since issue #2883 made each implicit a whole number at +0. The flat
affix pair is not held to it (ruled 2026-11-25); see its test below. Reading both numbers out
of the data and dividing is what stops one moving without the other.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

from cataclysm_sim import affix_ranges

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "sim"))
DATA = REPO_ROOT / "game" / "Data"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: Seconds a full shield takes to come back. Path of Exile recharges 20% of
#: maximum energy shield per second after its own delay, which is the same.
REFILL_SECONDS = 5.0

SHIELD = "max_energy_shield"
REGEN = "energy_shield_regen"

#: ABOUT FIVE SECONDS, since issue #2883 made each implicit a whole number at +0, rounded on its own: the four shield
#: bases refill in 4.8 to 5.5 seconds. Ruled by the coordinating session on 2026-11-25; no data changed.
ABOUT_FIVE = (4.8, 5.5)


def rows(name: str) -> list[dict]:
    with (DATA / name).open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def bases() -> list[dict]:
    return rows("ItemBases.csv")


@pytest.fixture(scope="module")
def affixes() -> list[dict]:
    return rows("Affixes.csv")


@pytest.fixture(scope="module")
def class_stats() -> list[dict]:
    return rows("ClassStats.csv")


def implicits(base: dict) -> dict[str, float]:
    """The stats one item base grants, as {stat: value}."""
    found = {}
    for index in ("1", "2"):
        stat = base.get(f"Implicit{index}Stat") or ""
        if stat:
            found[stat] = float(base[f"Implicit{index}Value"])
    return found


def whole_implicits(base: dict) -> dict[str, int]:
    """The whole +0 value of each implicit one item base grants, as {stat: value}."""
    found = {}
    for index in ("1", "2"):
        stat = base.get(f"Implicit{index}Stat") or ""
        if stat:
            found[stat] = int(base[f"Implicit{index}AtZero"])
    return found


def test_every_base_granting_a_shield_also_grants_a_rate(bases) -> None:
    granting = [b for b in bases if SHIELD in implicits(b)]
    assert granting, "no item base grants maximum energy shield any more"

    for base in granting:
        found = implicits(base)
        assert REGEN in found, (
            f"{base['Name']} grants {found[SHIELD]:g} maximum energy shield and "
            "no rate to refill it. A class with no energy shield regeneration "
            "of its own -- which is every class but the Ritualist -- would wear "
            "it and never see the shield come back. Issue #1237.")
        # THE STATED +10 FIGURES KEEP EXACTLY A FIFTH, and the whole +0 values the game
        # upgrades (issue #2883) keep it within their rounding: ABOUT FIVE SECONDS,
        # 4.8 to 5.5, ruled 2026-11-25. Checked at +0 and +10.
        seconds = found[SHIELD] / found[REGEN]
        assert seconds == pytest.approx(REFILL_SECONDS), (
            f"{base['Name']} states {found[SHIELD]:g} maximum energy shield and "
            f"{found[REGEN]:g} a second, so its shield refills in "
            f"{seconds:.1f} seconds rather than {REFILL_SECONDS:.0f}.")
        whole = whole_implicits(base)
        for level in (0, 10):
            seconds = (affix_ranges.upgraded(whole[SHIELD], level)
                       / affix_ranges.upgraded(whole[REGEN], level))
            assert ABOUT_FIVE[0] <= seconds <= ABOUT_FIVE[1], (
                f"{base['Name']} at +{level} refills in {seconds:.2f} seconds, outside "
                f"{ABOUT_FIVE[0]} to {ABOUT_FIVE[1]}")


def test_no_base_grants_a_rate_without_a_shield(bases) -> None:
    """A rate on a base that grants no shield refills nothing the base gave."""
    for base in bases:
        found = implicits(base)
        if REGEN in found:
            assert SHIELD in found, (
                f"{base['Name']} grants energy shield regeneration and no "
                "energy shield.")


def test_the_flat_affix_pair_is_not_held_to_a_fifth_and_refills_as_it_rolls() -> None:
    """The affix granting a shield and the affix granting its rate, since issue #2883.

    THEY ARE TWO SEPARATE CHOICES AND NOT HELD TO A FIFTH: ruled by the coordinating session on 2026-11-25. Until then
    this test held the pair to five seconds through the old curve (`affixes.affix_value` with Top Value and Floor), and
    it went on passing against that retired curve after the game stopped using it. Under the approved whole-number
    ranges a character taking both refills as the two roll, and one stacking regeneration refills well under five
    seconds. The figures are pinned so that a later change to either range is seen.
    """
    from cataclysm_sim import affix_ranges

    shield = "Flat maximum energy shield"
    regen = "Flat energy shield regeneration"
    pinned = (
        (1, 0.0, 0, 5, 1, 5.0),     # the worst roll on an un-upgraded piece
        (1, 1.0, 0, 6, 2, 3.0),
        (4, 0.5, 5, 20, 13, 20 / 13),
        (7, 1.0, 10, 50, 35, 50 / 35),  # a perfect top-tier roll on a fully upgraded piece
    )
    for tier, roll, gear, given, rate, seconds in pinned:
        assert affix_ranges.value(shield, tier, roll, gear) == given, (tier, roll, gear)
        assert affix_ranges.value(regen, tier, roll, gear) == rate, (tier, roll, gear)
        assert given / rate == pytest.approx(seconds)


def test_a_class_with_a_shield_refills_it_in_five_seconds(class_stats) -> None:
    """Both halves of the class line, the base and the per-level figure.

    Checked at both because a class whose shield grows with level and whose rate
    does not would refill in five seconds at level 1 and far longer at level 100,
    which is the fault this issue is about arriving by another route.
    """
    by_class: dict[str, dict[str, dict[str, float]]] = {}
    for row in class_stats:
        if row["Stat"] in (SHIELD, REGEN):
            by_class.setdefault(row["ClassName"], {})[row["Stat"]] = {
                "base": float(row["Base"]),
                "per_level": float(row["PerLevel"]),
            }

    with_shield = {name: stats for name, stats in by_class.items()
                   if SHIELD in stats}
    assert with_shield, "no class line grants maximum energy shield any more"

    for name, stats in with_shield.items():
        assert REGEN in stats, (
            f"the {name} class line grants maximum energy shield and no rate to "
            "refill it.")
        for half in ("base", "per_level"):
            shield = stats[SHIELD][half]
            regen = stats[REGEN][half]
            if shield == 0.0 and regen == 0.0:
                continue
            seconds = shield / regen
            assert seconds == pytest.approx(REFILL_SECONDS), (
                f"the {name} class line's {half} figures are {shield:g} shield "
                f"and {regen:g} a second, which refills in {seconds:.1f} "
                f"seconds rather than {REFILL_SECONDS:.0f}.")


def test_the_design_document_states_the_rate() -> None:
    """It stated the delay and not the rate, which is how the rate went missing."""
    text = DESIGN.read_text(encoding="utf-8")
    assert "refills at about a fifth of its own maximum per second" in text, (
        "the Energy Shield section of docs/Cataclysm_GDD_v2.md no longer states "
        "how fast a shield refills. It stated the three second delay and no "
        "rate for a long time, and the rate is what was missing from the game.")
