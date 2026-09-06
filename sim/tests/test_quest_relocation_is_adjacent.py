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

**WHAT "MAY MOVE" MEANS IS NOT SETTLED BY THIS FILE.** What is built moves the
dungeon whenever an adjacent exposed city exists, so the "may" comes from the map
rather than from a die roll. The project owner ruled otherwise on 2026-09-06,
verbatim: *"A chance each time"*. No number was given, `CLAUDE.md` forbids
inventing one, and `sim/analyse_quest_move_chance.py` is the dose-response curve
that exists to get one. Until it is answered
`test_some_stayed_because_they_had_nowhere_adjacent_to_go` below still holds, and
its last assertion says in as many words what to do when a chance arrives.

THE GAME'S HALF is `UCataclysmSurgeScheduler::PickRelocation` and
`UCataclysmEmpireRun::RelocateQuestDungeon`, checked against these by
`tools/tests/test_surge_port.py` and by
`Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity`.
"""

from __future__ import annotations

import dataclasses

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


class TestWhatActuallyHappensInACampaign:
    """Driving real runs, because a helper nothing calls is worth nothing.

    THE SAMPLE IS STATED SO IT CAN BE RECHECKED. Twenty campaigns under the
    `triage` policy produce a few hundred quest timers between them, which is
    enough for both outcomes -- moved, and had nowhere to go -- to appear many
    times over.
    """

    @pytest.fixture(scope="class")
    @classmethod
    def moves(cls):
        """Every relocation that happened, as (from, to, neighbours-of-from)."""
        seen: list[tuple[int, int, list[int]]] = []
        stayed: list[int] = []
        original = Simulation._resolve

        def watched(self, d, _o=original):
            if d.dtype is not DungeonType.QUEST:
                return _o(self, d)

            was = d.city_id
            around = self.empire.neighbours(self.empire.cities[was])
            had = [c.cid for c in
                   self.empire.adjacent_exposed_cities(self.empire.cities[was])]

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

    def test_some_stayed_because_they_had_nowhere_adjacent_to_go(self, moves):
        """The design says a Quest dungeon "MAY move", and this is the whole of
        where the "may" comes from -- no die roll, just a dungeon hemmed in.

        **THIS IS ALSO THE CONTROL ON THE TEST ABOVE.** If every quest timer
        moved the dungeon, the adjacency assertion could be satisfied by a rule
        that simply never ran out of targets, which is what the old
        move-anywhere rule was: it had one on every single day of every single
        campaign.
        """
        _, stayed = moves

        assert stayed, (
            "no quest dungeon ever stayed put. Under the adjacency rule some "
            "must -- a dungeon whose neighbours are all sealed has nowhere to "
            "go -- so either the rule is not being applied or the sample is "
            "not representative")

        assert all(count == 0 for count in stayed), (
            "a quest dungeon stayed where it was while it had somewhere "
            "adjacent to go. Nothing implements a chance of staying; if one "
            "has been added, this test needs replacing rather than relaxing")


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
