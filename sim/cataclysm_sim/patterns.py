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

from .config import CityTier, TuningConfig
from .world import City, Empire


@dataclass(frozen=True)
class Pattern:
    name: str
    blurb: str
    # Relative number of dungeons this Cataclysm contributes to a wave.
    count_mult: float = 1.0
    # Relative depth of the dungeons it spawns.
    floors_mult: float = 1.0
    #: NO PATTERN LANDS BEHIND THE FRONT LINE, since 2026-11-19 (#2749). Every
    #: Cataclysm's wave draws from `Empire.exposed_cities`, as the game's
    #: `UCataclysmSurgeScheduler` does for every sender. Demonic alone had an
    #: `ignores_frontier` field set until then, and it was deleted rather than
    #: left false, because nothing would set it: see `_demonic`.
    # Cities lost to this Cataclysm can never be reclaimed.
    #: Its ordinary dungeons add void stacks instead of damage, and ten erase a
    #: city (`Simulation._adds_void_stacks`). Until 2026-10-18 this marked every
    #: city the Cataclysm's dungeons landed on to be erased by its fall.
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
# THE FRONT LINE, BY TIER, AS THE GAME'S DOES, since 2026-11-19 (#2749). The
# design document's Demonic quest mechanic -- "Infernal Rifts tear open across
# the map, spawning dungeons without needing a direct path" -- is the Hell on
# Earth mechanic, issue #51, and not how a Demonic wave lands. The ruling on
# issue #1085, 2026-09-14: "the vertical slice's Demonic wave does not ignore
# the empire map's lane rule. The generic frontier pattern the scheduler already
# ports stays; the GDD's Demonic quest mechanic stays a Demonic-only mechanic
# and is not the general surge rule." Until 2026-11-19 this pattern drew from
# every standing city, sealed ones included, so it put dungeons on Bulwarks and
# Sanctuaries no other Cataclysm and no game wave could reach; nothing here
# models the Rifts.
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
#
# ITS HURT IS STACKS, since 2026-10-18: its ordinary dungeons add void stacks
# and take no defence, so it returns to the cities it has stacked. Until that
# day this read lost defence, `1 + 4 x (1 - defence share)`, which other
# Cataclysms, Sieges and Quest hits take. Ruled by the coordinating session,
# 2026-10-18. THE PULL IS AT MOST 2 TIMES, at ten stacks: the owner chose it
# the same day ("Stacks, up to 2x") among 2, 3 and 5 times and lost defence,
# measured; `docs/DECISIONS.md` has the table. The share is of
# `TuningConfig.void_stacks_to_erase` as the class
# declares it, read once here: a campaign that sets another threshold still
# weighs by ten.
_VOID_STACKS_FULL = TuningConfig.__dataclass_fields__["void_stacks_to_erase"].default


def _void(e: Empire, c: City) -> float:
    return _tier_weight(c) * (1.0 + 1.0 * min(1.0, c.void_stacks / _VOID_STACKS_FULL))


PATTERNS: dict[str, Pattern] = {
    "Death": Pattern(
        "Death", "swarms the rim with shallow dungeons, then funnels into breaches",
        count_mult=1.8, floors_mult=0.55, weight=_death),
    "Demonic": Pattern(
        "Demonic", "uniform pressure on the front line, weighted by tier; its Rifts are not modelled",
        count_mult=0.9, floors_mult=1.0, weight=_demonic),
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
