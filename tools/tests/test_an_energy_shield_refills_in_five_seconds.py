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

THE RULE THIS HOLDS. Every source of maximum energy shield grants a fifth of what
it gave as energy shield regeneration, so a shield refills in five seconds
however it was built and whatever class is wearing it. Reading both numbers out
of the data and dividing is what stops one moving without the other.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "sim"))
DATA = REPO_ROOT / "game" / "Data"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: Seconds a full shield takes to come back. Path of Exile recharges 20% of
#: maximum energy shield per second after its own delay, which is the same.
REFILL_SECONDS = 5.0

SHIELD = "max_energy_shield"
REGEN = "energy_shield_regen"


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
        seconds = found[SHIELD] / found[REGEN]
        assert seconds == pytest.approx(REFILL_SECONDS), (
            f"{base['Name']} grants {found[SHIELD]:g} maximum energy shield and "
            f"{found[REGEN]:g} a second, so its shield refills in "
            f"{seconds:.1f} seconds rather than {REFILL_SECONDS:.0f}.")


def test_no_base_grants_a_rate_without_a_shield(bases) -> None:
    """A rate on a base that grants no shield refills nothing the base gave."""
    for base in bases:
        found = implicits(base)
        if REGEN in found:
            assert SHIELD in found, (
                f"{base['Name']} grants energy shield regeneration and no "
                "energy shield.")


def test_the_flat_affix_pair_refills_in_the_same_five_seconds(affixes) -> None:
    """The affix granting a shield and the affix granting its rate.

    They are two separate affixes and a character need not take both, so this is
    not a rule about what any one item does. It keeps them in the same proportion
    as every other source, so a character who takes both gets the same five
    seconds however the two rolled.

    THROUGH THE CURVE AND NOT OFF THE COLUMNS, at both ends of the ladder. A
    floor stated on one and not the other would hold the proportion at the top
    and break it at the bottom, which is what happened when the regeneration
    affix was first added: five shield and 2.5 a second refills in two seconds.
    """
    from cataclysm_sim import affixes as model

    shield = next(a for a in affixes
                  if a["Stat"] == SHIELD and a["ValueKind"] == "flat")
    regen = next(a for a in affixes
                 if a["Stat"] == REGEN and a["ValueKind"] == "flat")

    def value(row: dict, tier: int, roll: float, gear: int) -> float:
        return model.affix_value(float(row["TopValue"]), tier, roll, gear,
                                 float(row["Floor"]))

    corners = (
        (model.AFFIX_TIERS[0], 0.0, 0, "the worst roll on an un-upgraded piece"),
        (model.AFFIX_TIERS[-1], 1.0, model.MAX_GEAR_LEVEL,
         "a perfect top-tier roll on a fully upgraded piece"),
        (4, 0.5, 5, "a middling roll in the middle of both ladders"),
    )
    for tier, roll, gear, label in corners:
        given = value(shield, tier, roll, gear)
        rate = value(regen, tier, roll, gear)
        seconds = given / rate
        assert seconds == pytest.approx(REFILL_SECONDS), (
            f"at {label} the flat maximum energy shield affix gives "
            f"{given:.2f} and the flat energy shield regeneration affix gives "
            f"{rate:.2f} a second, which refills in {seconds:.1f} seconds "
            f"rather than {REFILL_SECONDS:.0f}.")


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
    assert "refills at a fifth of its own maximum per second" in text, (
        "the Energy Shield section of docs/Cataclysm_GDD_v2.md no longer states "
        "how fast a shield refills. It stated the three second delay and no "
        "rate for a long time, and the rate is what was missing from the game.")
