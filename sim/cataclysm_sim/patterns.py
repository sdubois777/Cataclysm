"""Cataclysm attack patterns.

Each Cataclysm assaults the empire differently. That is what makes stacking
them harder than simply multiplying the dungeon count -- three Cataclysms are
three different problems at once, and a defence tuned against one is wrong
against another.

Patterns are derived from each Cataclysm's quest mechanic in GDD XI.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from .config import CityTier
from .world import City, Empire


@dataclass(frozen=True)
class Pattern:
    name: str
    blurb: str
    # Relative number of dungeons this Cataclysm contributes to a wave.
    count_mult: float = 1.0
    # Relative depth of the dungeons it spawns.
    floors_mult: float = 1.0
    # Rifts open behind the front line -- the frontier rule does not apply.
    ignores_frontier: bool = False
    # Cities lost to this Cataclysm can never be reclaimed.
    erases_cities: bool = False
    # Weighting function over candidate cities.
    weight: Callable[[Empire, City], float] = lambda e, c: 1.0


def _tier_weight(c: City) -> float:
    return {CityTier.OUTPOST: 5.0, CityTier.BULWARK: 3.0,
            CityTier.SANCTUARY: 1.5, CityTier.PILLAR: 0.0}[c.tier]


# --- Death ------------------------------------------------------------------
# "Cursed Abominations spawn as cities fall, growing stronger with each loss."
# A swarm: many shallow dungeons smeared across the whole rim, which then pours
# through any hole the moment one opens.
def _death(e: Empire, c: City) -> float:
    w = 6.0 if c.ring == 3 else 1.0
    # Funnel: anything adjacent to a breach gets swarmed.
    if any(e.cities[k].fallen for k in c.outward):
        w *= 8.0
    return w


# --- Demonic ----------------------------------------------------------------
# "Infernal Rifts tear open across the map, spawning dungeons without needing a
# direct path." Lanes do not protect you.
def _demonic(e: Empire, c: City) -> float:
    return _tier_weight(c)


# --- War --------------------------------------------------------------------
# "Player cities become aggressive and begin attacking each other." Goes for
# the strongest thing standing rather than the softest.
def _war(e: Empire, c: City) -> float:
    return _tier_weight(c) * (0.5 + c.defense_frac) * (1.0 + 0.5 * len(c.inward))


# --- Pestilence -------------------------------------------------------------
# "A spreading plague infects cities via expanding Plague Zones. Fallen cities
# become plague zones and spread infection." Metastasises from what it has
# already touched.
def _pestilence(e: Empire, c: City) -> float:
    w = _tier_weight(c)
    neighbours = c.outward + c.inward + c.perimeter
    infected = sum(1 for k in neighbours
                   if e.cities[k].fallen or e.cities[k].defense_frac < 0.6)
    return w * (1.0 + 3.0 * infected)


# --- Famine -----------------------------------------------------------------
# "Essential resources become scarce." Strangles the economy: goes for the
# population centres and starves the loot supply.
def _famine(e: Empire, c: City) -> float:
    return _tier_weight(c) * (0.3 + 2.0 * (c.population / max(1.0, c.max_population)))


# --- Celestial --------------------------------------------------------------
# "Celestial gates open and angelic armies pour through." Few strikes, each one
# a judgement -- deep and aimed at what matters.
def _celestial(e: Empire, c: City) -> float:
    return {CityTier.OUTPOST: 1.0, CityTier.BULWARK: 3.0,
            CityTier.SANCTUARY: 5.0, CityTier.PILLAR: 0.0}[c.tier]


# --- Chaos ------------------------------------------------------------------
# "Chaotic Resolutions mean dungeons can have completely unpredictable
# outcomes." No pattern at all, which is its own pattern.
def _chaos(e: Empire, c: City) -> float:
    return 1.0


# --- Void -------------------------------------------------------------------
# "When stacks reach a threshold, the city is permanently erased and cannot be
# reclaimed." Fixates: keeps hammering whatever it has already damaged, and
# what it takes never comes back.
def _void(e: Empire, c: City) -> float:
    return _tier_weight(c) * (1.0 + 4.0 * (1.0 - c.defense_frac))


PATTERNS: dict[str, Pattern] = {
    "Death": Pattern(
        "Death", "swarms the rim with shallow dungeons, then funnels into breaches",
        count_mult=1.8, floors_mult=0.55, weight=_death),
    "Demonic": Pattern(
        "Demonic", "Rifts open behind the front line -- ignores lanes entirely",
        count_mult=0.9, floors_mult=1.0, ignores_frontier=True, weight=_demonic),
    "War": Pattern(
        "War", "attacks the strongest standing city rather than the weakest",
        count_mult=1.0, floors_mult=1.15, weight=_war),
    "Pestilence": Pattern(
        "Pestilence", "spreads from whatever it has already damaged",
        count_mult=1.3, floors_mult=0.8, weight=_pestilence),
    "Famine": Pattern(
        "Famine", "targets population centres and starves the loot supply",
        count_mult=0.9, floors_mult=1.1, weight=_famine),
    "Celestial": Pattern(
        "Celestial", "few strikes, aimed deep at the inner rings",
        count_mult=0.6, floors_mult=1.6, weight=_celestial),
    "Chaos": Pattern(
        "Chaos", "no pattern whatsoever",
        count_mult=1.0, floors_mult=1.0, weight=_chaos),
    "Void": Pattern(
        "Void", "fixates on what it has already hurt; what it takes is erased",
        count_mult=0.8, floors_mult=1.25, erases_cities=True, weight=_void),
}

DEFAULT = Pattern("Generic", "uniform pressure on the frontier", weight=lambda e, c: _tier_weight(c))
