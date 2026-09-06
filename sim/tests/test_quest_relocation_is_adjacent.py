"""A Quest dungeon whose timer runs out moves to an ADJACENT city.

**THE RULING.** `docs/Cataclysm_GDD_v2.md` section VIII says a Quest dungeon
"does not resolve -- refreshes and may move to adjacent city". The model moved it
to a uniformly random exposed city anywhere on the map, through
`Empire.exposed_cities()`, which filters for exposure and nothing else. Asked
which was intended the project owner answered on 2026-09-06, verbatim:
*"Adjacent, and fix the simulation"*. So the document was right and the model was
the defect. Issue
[#1324](https://github.com/sdubois777/Cataclysm/issues/1324), slice 4.

**WHAT "ADJACENT" WAS TAKEN TO MEAN.** Every city the map links this one to:
`outward`, `inward`, and -- on the rim only -- `perimeter`, the curved edges of
the diamond. The design never defines adjacency, so that is a reading and
`docs/DECISIONS.md` records it as one, along with what it is worth: a Quest
dungeon has somewhere to go 79.5% of the time with the perimeter links counted
and 69.1% without.

**WHY A CAMPAIGN-DRIVEN TEST AND NOT ONLY A UNIT ONE.** The thing that can go
wrong is `Simulation._resolve` reaching for the wrong list, and a unit test of
`Empire.adjacent_exposed_cities` would keep passing while `_resolve` called
something else. `TestWhatActuallyHappensInACampaign` below drives real runs and
checks every move that happened, which is the only way to catch that.

**WHAT "MAY MOVE" MEANS, AND IT IS TWO THINGS.** The project owner ruled on
2026-09-06, verbatim: *"A chance each time"*, and then chose the number, verbatim
*"0.5"*. So a Quest dungeon stays for either of two reasons -- it had nowhere
adjacent to go, or it had somewhere and the coin said no --
and `TestWhyAQuestDungeonStays` below asserts that both happen, because a sample
showing only one of them would leave half the rule untested. This replaces
`test_some_stayed_because_they_had_nowhere_adjacent_to_go`, whose last assertion
said in as many words that a chance arriving is what should replace it.

**AND A DUNGEON CARRYING A SIEGE MAY NOT LAND ON A BESIEGED CITY.** "Max 1 per
city" was enforced at spawn and by nothing on this path until the owner ruled on
2026-09-06, verbatim *"Check the limit on arrival too"*. Issue
[#1371](https://github.com/sdubois777/Cataclysm/issues/1371).
`TestASiegeMayNotWalkOntoABesiegedCity` builds the situation by hand rather than
looking for it, because looking for it does not find it: measured over 40
campaigns of this model there was **one** quest timer on a Siege-carrying dungeon
and **zero** landings that would have broken the cap, so a campaign-driven test
would pass identically against a game with no check at all.

THE GAME'S HALF is `UCataclysmSurgeScheduler::PickRelocation` and
`UCataclysmEmpireRun::RelocateQuestDungeon`, checked against these by
`tools/tests/test_surge_port.py` and by
`Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity`.
"""

from __future__ import annotations

import dataclasses
import math

import pytest

from cataclysm_sim.config import CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Dungeon, Simulation
from cataclysm_sim.policies import ALL as POLICIES
from cataclysm_sim.world import RADIUS, build_empire


def safe(**overrides) -> TuningConfig:
    """A config where clearing a dungeon cannot kill the player, so campaigns
    run long enough for quest timers to fire. The shape `test_engine.py` uses.
    """
    return dataclasses.replace(TuningConfig(), per_floor_risk=0.0,
                               boss_risk_multiplier=0.0, **overrides)


class TestWhatTheMapCallsAdjacent:
    def test_a_city_is_never_its_own_neighbour(self):
        """The one answer that would make "it moved" and "it stayed"
        indistinguishable."""
        empire = build_empire(TuningConfig())

        for city in empire.cities.values():
            assert city.cid not in empire.neighbours(city), (
                f"{city.name} lists itself as its own neighbour")

    def test_adjacency_runs_both_ways(self):
        """If A is adjacent to B then B is adjacent to A. A one-way link would
        let a quest dungeon drift somewhere it could never drift back from, and
        nothing else in the model would notice."""
        empire = build_empire(TuningConfig())

        for city in empire.cities.values():
            for other in empire.neighbours(city):
                assert city.cid in empire.neighbours(empire.cities[other]), (
                    f"{city.name} lists {empire.cities[other].name} as a "
                    "neighbour but not the other way round")

    def test_every_orthogonal_step_is_a_neighbour(self):
        """Read off the lattice rather than off the fields, so a `build_empire`
        that stopped linking one direction fails here."""
        empire = build_empire(TuningConfig())

        for city in empire.cities.values():
            for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                other = empire.by_coord.get((city.r + dr, city.c + dc))
                if other is None:
                    continue
                assert other in empire.neighbours(city), (
                    f"{city.name} is not adjacent to the city one orthogonal "
                    "step away")

    def test_the_rims_perimeter_links_count_as_adjacent(self):
        """**THE DECISION, and the one a tidy-up would undo.**
        `UCataclysmEmpireMap`'s class comment used to say adjacency was
        strictly orthogonal, which excludes these. They are counted, and
        removing them costs about ten percentage points of relocation
        frequency and nothing else in either codebase would notice.
        """
        empire = build_empire(TuningConfig())

        rim = [c for c in empire.cities.values() if c.ring == RADIUS]
        assert rim, "the map has no rim, so this test proves nothing"

        linked = [c for c in rim if c.perimeter]
        assert linked, (
            "no rim Outpost has a perimeter link, so this test cannot tell "
            "whether they are counted as adjacent")

        for city in linked:
            for other in city.perimeter:
                assert other in empire.neighbours(city), (
                    f"{city.name}'s perimeter link is not counted as "
                    "adjacency. See docs/DECISIONS.md, 2026-09-06")

    def test_a_perimeter_link_is_not_an_orthogonal_one(self):
        """The control for the test above. If the perimeter list happened to
        hold the same cities `outward` and `inward` already do, counting it
        would prove nothing and the measured difference would be zero."""
        empire = build_empire(TuningConfig())

        extra = [c for c in empire.cities.values()
                 if set(c.perimeter) - set(c.outward) - set(c.inward)]

        assert extra, (
            "no city's perimeter link names a city its orthogonal links do "
            "not. The perimeter would then be decoration and the adjacency "
            "decision would be empty")


class TestWhereAQuestDungeonMayMoveTo:
    def test_a_target_is_always_a_neighbour(self):
        empire = build_empire(TuningConfig())

        for city in empire.cities.values():
            targets = empire.adjacent_exposed_cities(city)
            for target in targets:
                assert target.cid in empire.neighbours(city), (
                    f"{city.name} may relocate to {target.name}, which is not "
                    "adjacent to it")

    def test_a_sealed_city_is_never_a_target(self):
        """A dungeon may only stand where a surge could have put one."""
        empire = build_empire(TuningConfig())

        for city in empire.cities.values():
            for target in empire.adjacent_exposed_cities(city):
                assert empire.is_exposed(target), (
                    f"{target.name} is sealed and is still offered as a "
                    "relocation target")

    def test_a_fallen_city_is_never_a_target(self):
        """`is_exposed` answers false for a fallen city, so this follows -- but
        it is the case a reader will ask about, because a fallen city carries a
        Fallen City dungeon and a Quest dungeon landing on it would put two
        dungeons of different kinds on one ruin.
        """
        empire = build_empire(TuningConfig())

        rim = [c for c in empire.cities.values() if c.ring == RADIUS]
        victim = rim[0]
        victim.fallen = True

        for city in empire.cities.values():
            names = [t.cid for t in empire.adjacent_exposed_cities(city)]
            assert victim.cid not in names, (
                f"{victim.name} has fallen and is still a relocation target")

        # AND THE FALL DID OPEN SOMETHING, or the assertion above is vacuous:
        # a map where nothing changed would satisfy it trivially.
        opened = [c for c in empire.cities.values()
                  if c.ring < RADIUS and empire.is_exposed(c)]
        assert opened, (
            "no city became exposed when a rim Outpost fell, so this test is "
            "not exercising the case it names")

    def test_the_pillar_is_never_a_target(self):
        """The same exclusion `exposed_cities` makes. A dungeon reaching the
        Pillar is the Last Stand, issue #43, not a quest dungeon wandering."""
        empire = build_empire(TuningConfig())

        # OPEN THE WHOLE MAP, so the Pillar is genuinely exposed and the
        # exclusion has to do the work rather than the lane rule doing it.
        for city in empire.cities.values():
            if city.ring in (RADIUS, RADIUS - 1, 1):
                city.fallen = True

        assert empire.pillar_exposed(), (
            "the Pillar is not exposed even with every ring around it fallen, "
            "so this test cannot tell whether it is excluded")

        for city in empire.cities.values():
            names = [t.cid for t in empire.adjacent_exposed_cities(city)]
            assert empire.pillar_id not in names, (
                f"{city.name} may relocate a quest dungeon onto the Pillar")


@pytest.fixture(scope="module")
def moves():
    """Every relocation that happened, as (from, to, neighbours-of-from).

    `stayed` counts, for each quest timer that did not move the dungeon, how
    many destinations it could legally have taken. Zero means hemmed in and more
    than zero means the coin said no; both are outcomes the design describes and
    `TestWhyAQuestDungeonStays` asserts both occur.

    **IT IS A MODULE FIXTURE AND WAS A CLASS ONE.** Two classes read it now --
    the adjacency rule and the reasons a dungeon stays -- and twenty campaigns
    are too slow to run twice.
    """
    seen: list[tuple[int, int, list[int]]] = []
    stayed: list[int] = []
    original = Simulation._resolve

    def watched(self, d, _o=original):
        if d.dtype is not DungeonType.QUEST:
            return _o(self, d)

        was = d.city_id
        around = self.empire.neighbours(self.empire.cities[was])
        had = [c.cid for c in
               self.empire.adjacent_exposed_cities(self.empire.cities[was])
               if d.subtype != "Siege"
               or self.sieges_on(c.cid) < self.cfg.siege_max_per_city]

        result = _o(self, d)

        if d.city_id != was:
            seen.append((was, d.city_id, around))
        else:
            stayed.append(len(had))

        return result

    Simulation._resolve = watched
    try:
        for seed in range(20):
            Simulation(safe(), seed=seed).run(POLICIES["triage"])
    finally:
        Simulation._resolve = original

    return seen, stayed


class TestWhatActuallyHappensInACampaign:
    """Driving real runs, because a helper nothing calls is worth nothing.

    THE SAMPLE IS STATED SO IT CAN BE RECHECKED. Twenty campaigns under the
    `triage` policy produce a few hundred quest timers between them, which is
    enough for every outcome -- moved, declined, and had nowhere to go -- to
    appear many times over.
    """

    def test_quest_dungeons_actually_relocated(self, moves):
        """The evidence has to exist before anything can be concluded from it.
        A run that never moved one would pass every assertion below by never
        reaching one."""
        seen, _ = moves

        assert len(seen) > 20, (
            f"only {len(seen)} quest relocations happened across 20 campaigns, "
            "which is too few to say anything about the rule")

    def test_every_move_landed_on_an_adjacent_city(self, moves):
        """THE RULE. One counter-example is a failure; this is not a rate."""
        seen, _ = moves

        for was, now, around in seen:
            assert now in around, (
                f"a quest dungeon moved from city {was} to city {now}, which "
                "is not adjacent to it. The owner ruled on 2026-09-06, "
                "verbatim \"Adjacent, and fix the simulation\"")


class TestWhyAQuestDungeonStays:
    """**THE DESIGN'S "MAY", AND IT IS TWO THINGS RATHER THAN ONE.**

    A Quest dungeon stays because it had nowhere adjacent to go, or because it
    had somewhere and the coin said no. The project owner ruled the second one
    on 2026-09-06, verbatim *"A chance each time"*, and chose the number,
    verbatim *"0.5"*.

    **THIS REPLACES `test_some_stayed_because_they_had_nowhere_adjacent_to_go`,
    WHICH ASSERTED THE OPPOSITE ON PURPOSE.** Its last assertion read "Nothing
    implements a chance of staying; if one has been added, this test needs
    replacing rather than relaxing", and one has been.

    **THE STAYING CASE IS STILL THE CONTROL ON THE ADJACENCY TEST.** If every
    quest timer moved the dungeon, the adjacency assertion would be satisfied by
    a rule that never ran out of targets, which is what the old move-anywhere
    rule was: it had one on every single day of every single campaign.
    """

    def test_some_stayed_because_they_had_nowhere_adjacent_to_go(self, moves):
        """The half that predates the chance, and it must not be swallowed by
        it: a dungeon hemmed in by sealed neighbours never reaches the coin."""
        _, stayed = moves

        assert any(count == 0 for count in stayed), (
            "no quest dungeon ever stayed for want of anywhere to go. Under "
            "the adjacency rule some must -- a dungeon whose neighbours are "
            "all sealed is hemmed in -- so either the rule is not being "
            "applied or the sample is not representative")

    def test_some_stayed_although_they_had_somewhere_to_go(self, moves):
        """**THE RULING.** Before 2026-09-06 this could not happen at all, and
        the test it replaces asserted that it did not."""
        _, stayed = moves

        assert any(count > 0 for count in stayed), (
            "every quest dungeon that stayed was hemmed in, so nothing ever "
            "declined a move it could have taken. The owner ruled on "
            "2026-09-06, verbatim \"A chance each time\", and chose 0.5; see "
            "config.quest_move_chance. Issue #1324")

    def test_the_take_up_rate_is_the_configured_chance(self, moves):
        """The number, measured over every quest timer that had a choice.

        **THE BAND IS COMPUTED, NOT CHOSEN.** Take-up is a binomial proportion
        over `n` independent draws, so its standard deviation is
        `sqrt(p(1-p)/n)`; four of those either side is the band. Writing it this
        way rather than as a hard-coded interval is what stops the tolerance
        being widened until whatever the code does passes -- and it still fails
        loudly at the two settings that matter, because a chance of 0 gives a
        take-up of 0% and a chance of 1 gives 100%, both tens of standard
        deviations out.
        """
        _, stayed = moves
        seen, _ = moves

        declined = sum(1 for count in stayed if count > 0)
        chose = len(seen) + declined

        assert chose > 100, (
            f"only {chose} quest timers had a choice to make across 20 "
            "campaigns, which is too few to say anything about the rate")

        wanted = TuningConfig().quest_move_chance
        spread = 4.0 * math.sqrt(wanted * (1.0 - wanted) / chose)
        rate = len(seen) / chose

        assert abs(rate - wanted) <= spread, (
            f"a Quest dungeon with somewhere to go took it {rate:.1%} of the "
            f"time over {chose} timers, against the configured "
            f"{wanted:.0%} and a four-sigma band of "
            f"{wanted - spread:.1%} to {wanted + spread:.1%}. The owner ruled "
            "the chance on 2026-09-06, verbatim \"A chance each time\", and "
            "chose 0.5; see config.quest_move_chance")


class TestASiegeMayNotWalkOntoABesiegedCity:
    """**THE OWNER'S RULING OF 2026-09-06, VERBATIM: "Check the limit on arrival
    too".** Issue
    [#1371](https://github.com/sdubois777/Cataclysm/issues/1371).

    The design's Siege row says "Max 1 per city". `_roll_subtype` enforced that
    when a dungeon was created and nothing enforced it when one moved, so a Quest
    dungeon that had rolled Siege could walk onto a city that already had one and
    the city would take the daily bite twice.

    **THE SITUATION IS BUILT BY HAND AND THAT IS THE POINT.** Watched over 40
    campaigns of this model, exactly **one** quest timer fired on a Siege-carrying
    dungeon and **none** of them would have broken the cap. A test that ran
    campaigns and found no violation would therefore pass, unchanged, against a
    build with no check in it at all -- which is what issue #1371 says in as many
    words. Everything below places the dungeons itself.

    **AND IT REFUSES THE DESTINATION RATHER THAN THE MOVE**, which is the shape
    `_roll_subtype` already uses on the spawn half of the same rule: a refused
    Siege there is spread across the other sub-types rather than dropped, and a
    refused city here leaves the other neighbours available.
    """

    @staticmethod
    def _rim_pair(sim):
        """A rim Outpost and one of its perimeter neighbours.

        On an intact map a rim Outpost's exposed neighbours are exactly its
        perimeter links, so a scenario built here has a known, small target set
        and nothing else can creep into it.
        """
        for city in sim.empire.cities.values():
            if city.ring == RADIUS and len(city.perimeter) >= 2:
                return city, [sim.empire.cities[k] for k in city.perimeter]
        raise AssertionError("no rim Outpost has two perimeter links")

    @staticmethod
    def _quest_siege_on(sim, city):
        d = sim._make_dungeon(DungeonType.QUEST, city)
        d.subtype = "Siege"
        return d

    def test_the_situation_is_reachable_at_all(self):
        """**THE CONTROL ON EVERY TEST BELOW.** If a Quest dungeon could never
        carry the Siege sub-type, the whole class would be asserting something
        about a state that cannot exist and would pass whatever the code did.
        """
        sim = Simulation(safe(), seed=1)

        assert "Siege" in sim.subtypes_allowed_on(DungeonType.QUEST), (
            "a Quest dungeon may no longer roll Siege, so issue #1371's "
            "situation cannot arise and this class guards nothing. Barring the "
            "pair was the alternative the owner REJECTED on 2026-09-06")

    def test_it_moves_to_the_free_neighbour_instead(self):
        """The redirect. One neighbour is besieged, the other is not, so the
        only legal answer is the free one -- and it is reached often enough that
        the coin cannot hide the result."""
        sim = Simulation(safe(), seed=1)
        home, (blocked, free) = self._rim_pair(sim)

        # THE CITY THAT ALREADY HAS ONE, and it is a Basic dungeon so that the
        # thing being refused is the destination rather than the mover.
        sitting = sim._make_dungeon(DungeonType.BASIC, blocked)
        sitting.subtype = "Siege"

        landed = set()
        for _ in range(200):
            wanderer = self._quest_siege_on(sim, home)
            sim._resolve(wanderer)
            landed.add(wanderer.city_id)
            sim.dungeons.pop(wanderer.did, None)

        assert blocked.cid not in landed, (
            f"a Quest dungeon carrying a Siege moved onto {blocked.name}, "
            f"which already holds one. The owner ruled on 2026-09-06, verbatim "
            "\"Check the limit on arrival too\"; issue #1371")

        assert free.cid in landed, (
            f"the dungeon never reached {free.name}, which carries no Siege "
            "and is adjacent to it. Refusing the besieged city must redirect "
            "the move, not cancel it")

    def test_it_stays_when_every_neighbour_is_besieged(self):
        """The other outcome the owner named: "or stay where it is if there is
        none". A refused destination is not a licence to teleport."""
        sim = Simulation(safe(), seed=2)
        home, neighbours = self._rim_pair(sim)

        for city in neighbours:
            occupied = sim._make_dungeon(DungeonType.BASIC, city)
            occupied.subtype = "Siege"

        for _ in range(50):
            wanderer = self._quest_siege_on(sim, home)
            sim._resolve(wanderer)
            assert wanderer.city_id == home.cid, (
                "a Quest dungeon carrying a Siege moved although every city "
                f"adjacent to {home.name} already had one. It must stay where "
                "it is; issue #1371")
            sim.dungeons.pop(wanderer.did, None)

    def test_a_dungeon_carrying_no_siege_is_not_refused(self):
        """**THE CONTROL THAT SAYS THE RULE IS NARROW.** The cap is about
        Sieges. A check written against the destination rather than against what
        the mover carries would stop every dungeon from entering a besieged
        city, which is a different and much larger rule than the one ruled.
        """
        sim = Simulation(safe(), seed=3)
        home, (blocked, _free) = self._rim_pair(sim)

        sitting = sim._make_dungeon(DungeonType.BASIC, blocked)
        sitting.subtype = "Siege"

        landed = set()
        for _ in range(200):
            plain = sim._make_dungeon(DungeonType.QUEST, home)
            plain.subtype = "Horde"
            sim._resolve(plain)
            landed.add(plain.city_id)
            sim.dungeons.pop(plain.did, None)

        assert blocked.cid in landed, (
            f"a Quest dungeon carrying no Siege was kept out of {blocked.name} "
            "because something else there has one. The cap is 'max 1 Siege per "
            "city' and not 'one dungeon per besieged city'")

    def test_the_besieged_city_is_still_adjacent_and_exposed(self):
        """**THE CONTROL THAT MAKES THE REFUSAL MEAN SOMETHING.** If the
        besieged city were sealed, or not a neighbour, the adjacency filter
        would already have excluded it and the tests above would pass with no
        Siege rule present at all.
        """
        sim = Simulation(safe(), seed=1)
        home, (blocked, _free) = self._rim_pair(sim)

        offered = [c.cid for c in sim.empire.adjacent_exposed_cities(home)]

        assert blocked.cid in offered, (
            f"{blocked.name} is not an adjacent exposed city of {home.name} "
            "even before any Siege is placed, so refusing it proves nothing")


class TestWhatARelocatedDungeonKeeps:
    """**THE OWNER'S RULING OF 2026-09-06, VERBATIM: "Keeps everything, fix the
    size".**

    A relocated Quest dungeon keeps its floor count, its resolve timer and its
    sub-type -- which both implementations already did -- and it also keeps
    `city_tier`, which neither of them did. `docs/Cataclysm_GDD_v2.md` section
    VIII now states all four.

    **WHAT `city_tier` IS.** The tier the dungeon's DEPTH WAS ROLLED FROM, set
    once at creation. `Simulation._resolve` reads it to find the specification
    row that says what a typical dungeon of this kind on that tier is, and
    divides `floors` by the midpoint of that row to scale the bite. Both halves
    of the division have to name the same row.

    **THE DEFECT.** `_resolve` used to assign `new_city.tier` here while leaving
    `floors` alone, so after a move the two halves came from different rows: a
    dungeon that drifted inward onto a bigger city read as shallower than it is
    and one that drifted outward read as deeper.

    **AND NOTHING WOULD HAVE FAILED WHEN IT MATTERED.** A Quest dungeon returns
    from `_resolve` before the scale is computed, and `cfg.spec` gives it zero
    city damage, so the wrong row was never read. It becomes a live wrong number
    the moment any non-Basic kind is given city damage -- silently, with no test
    failing. That is why this class exists for a value nothing consumes yet.
    `Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity` is the game's half,
    and it asserted the opposite of this until 2026-09-06.
    """

    @pytest.fixture(scope="class")
    @classmethod
    def relocations(cls):
        """Every relocation, as (before, after, tier of the city it left,
        tier of the city it arrived at).

        `before` and `after` are shallow copies of the dungeon taken either
        side of `_resolve`, so a field added to `Dungeon` later can be compared
        without touching this fixture.
        """
        seen: list[tuple[Dungeon, Dungeon, CityTier, CityTier]] = []
        original = Simulation._resolve

        def watched(self, d, _o=original):
            if d.dtype is not DungeonType.QUEST:
                return _o(self, d)

            before = dataclasses.replace(d)
            was = self.empire.cities[d.city_id]

            result = _o(self, d)

            if d.city_id != before.city_id:
                now = self.empire.cities[d.city_id]
                seen.append((before, dataclasses.replace(d), was.tier,
                             now.tier))

            return result

        Simulation._resolve = watched
        try:
            for seed in range(20):
                Simulation(safe(), seed=seed).run(POLICIES["triage"])
        finally:
            Simulation._resolve = original

        return seen

    def test_some_move_crossed_a_tier_boundary(self, relocations):
        """**THE CONTROL ON EVERY ASSERTION BELOW.** A move between two cities
        of the same tier satisfies `city_tier` staying put and `city_tier`
        moving with the dungeon equally well, so a sample whose moves never
        crossed a tier boundary would pass the whole class against the defect it
        exists to catch.
        """
        crossed = [r for r in relocations if r[2] is not r[3]]

        assert len(crossed) > 5, (
            f"only {len(crossed)} of {len(relocations)} relocations crossed a "
            "tier boundary. Every assertion in this class is vacuous on a move "
            "between two cities of one tier")

    def test_it_keeps_the_tier_its_depth_was_rolled_from(self, relocations):
        """THE FIX. `city_tier` is not the host's tier and does not follow the
        dungeon; read the host's tier off `empire.cities[d.city_id]`."""
        for before, after, _was, now in relocations:
            assert after.city_tier is before.city_tier, (
                f"quest dungeon {before.did} moved and its city_tier changed "
                f"from {before.city_tier} to {after.city_tier}. It is the tier "
                "its DEPTH was rolled from, and floors did not move. The owner "
                "ruled on 2026-09-06, verbatim \"Keeps everything, fix the "
                "size\"")

            # AND THE SIGN OF THE DEFECT, stated so a failure says what broke:
            # the tier it kept is the one it was built on, not the one it is
            # standing on now.
            if before.city_tier is not now:
                assert after.city_tier is not now, (
                    f"quest dungeon {before.did} took the tier of the city it "
                    "arrived at. That is the defect, not the rule")

    def test_it_keeps_its_floors_its_timer_and_its_sub_type(self, relocations):
        """The three the owner named first, and which both implementations
        already got right. Guarded anyway: nothing else would notice a move
        that quietly rerolled the depth, and the design now promises it."""
        for before, after, _was, _now in relocations:
            assert after.floors == before.floors, (
                f"quest dungeon {before.did} changed depth by moving")
            assert after.resolve_max == before.resolve_max, (
                f"quest dungeon {before.did} changed its resolve timer by "
                "moving")
            assert after.subtype == before.subtype, (
                f"quest dungeon {before.did} changed sub-type by moving")

    def test_its_bite_scale_survives_the_move(self, relocations):
        """**THE CONSEQUENCE, AND THE ONLY ONE THAT WILL EVER BE VISIBLE.**
        `_resolve` scales a dungeon's damage by `floors / typical`, where
        `typical` is the midpoint of `cfg.spec(dtype, city_tier).floors`. This
        is what went wrong, and it is asserted on its own rather than left to
        follow from the fields, because a future third input to the scale would
        break this and not them.

        It is computed here the way `_resolve` computes it rather than called,
        because `_resolve` returns before reaching the scale for the one kind of
        dungeon that can move -- which is exactly why the defect was free.
        """
        cfg = safe()

        def scale(d):
            lo, hi = cfg.spec(d.dtype, d.city_tier).floors
            return d.floors / ((lo + hi) / 2.0)

        for before, after, was, now in relocations:
            assert scale(after) == pytest.approx(scale(before)), (
                f"quest dungeon {before.did} moved from a {was.value} to a "
                f"{now.value} and its bite scale went from {scale(before):.3f} "
                f"to {scale(after):.3f}. Its floor count did not change, so "
                "nothing about how deep it is did")
