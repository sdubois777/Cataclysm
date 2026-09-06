"""The Unreal surge scheduler's constants must match the simulation's.

WHY THIS EXISTS AT ALL. `UCataclysmSurgeScheduler` in the `CataclysmEmpire`
module is a port of `Simulation.surge_count`, `surge_gap` and `trigger_surge` in
`sim/cataclysm_sim/engine.py`, and of the surge constants in `config.py`. Two
copies of a number are two numbers, and the power model in
`sim/cataclysm_sim/scoring.py` silently drifted from its own source twice, which
is why `CLAUDE.md` carries a rule about it.

This is the third of these, after `test_day_clock_port.py` and
`test_empire_map_port.py`, for the third thing ported into that module.

WHY IT MATTERS MORE HERE THAN FOR THE OTHER TWO. How surges escalate is an OPEN
TUNING QUESTION rather than a settled design: `sim/experiments.py` sweeps the four
modes against each other, and the answer it eventually gives is only worth
anything if the game runs the same arithmetic the sweep did.

WHAT IT DOES NOT CHECK. That the scheduler behaves -- that an accelerating run
really shortens its gap and stops at the floor, that Heretic really brings more
dungeons at the cap, that a wave lands only on the frontier. Those are Unreal
automation tests under the `Cataclysm.Surge` prefix. A test written against a
constant cannot notice that the constant is wrong, and a test that reads the
constant out of the source cannot notice that the code ignores it.

IT READS TWO HEADERS. `CataclysmSurge.h` is the scheduler and is most of this
file. `CataclysmEmpireRun.h` holds what a Siege costs its host every day, and
`TestWhatASiegeCostsItsHost` is the only thing here that reads it -- see the
note there for why those three constants run the opposite way round from every
other number in this file. Issue #1353.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

SURGE_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
                / "CataclysmSurge.h")

SURGE_TESTS = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Tests"
               / "CataclysmSurgeTests.cpp")

#: The empire run, which owns what a Siege costs its host every day. A different
#: header from the scheduler's, so it needs its own fixture.
RUN_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
              / "CataclysmEmpireRun.h")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str, *,
             where: str = SURGE_HEADER.name) -> str:
    """One `static constexpr` value out of a header, by name.

    `where` NAMES THE FILE THE TEXT CAME FROM and only the failure message uses
    it. It defaults to the surge header because that is what every caller read
    until this file grew a second one; a caller passing `run_header` must pass
    `where=RUN_HEADER.name` too, or a renamed constant is reported against a
    file it was never in.
    """
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {where}; has it been renamed?")
    return match.group(1)


def number(text: str, name: str, *, where: str = SURGE_HEADER.name) -> float:
    return float(constant(text, name, r"-?[0-9.]+f?", where=where).rstrip("f"))


def flag(text: str, name: str) -> bool:
    return constant(text, name, r"true|false") == "true"


@pytest.fixture(scope="module")
def surge_header() -> str:
    return read(SURGE_HEADER)


@pytest.fixture(scope="module")
def run_header() -> str:
    return read(RUN_HEADER)


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


class TestTheCadence:
    NUMBERS = {
        "IntervalDays": "surge_interval_days",
        "DungeonsPerSurge": "surge_dungeon_count",
        "IntervalDecay": "surge_interval_decay",
        "LeastIntervalDays": "surge_interval_min",
        "CountGrowthPerSurge": "surge_count_growth",
        "MostDungeonsPerSurge": "surge_count_max",
    }

    def test_every_cadence_number_matches(self, surge_header, model):
        for unreal_name, model_name in self.NUMBERS.items():
            unreal = number(surge_header, unreal_name)
            expected = float(getattr(model, model_name))

            assert unreal == pytest.approx(expected), (
                f"{unreal_name}: Unreal has {unreal}, the model has {expected}")

    def test_the_decay_shortens_and_the_growth_lengthens(self, surge_header):
        """Not a copy of anything: a decay above 1 would make an accelerating
        surge slower each time, and a growth below 0 would make a swelling one
        smaller. Either would still pass the comparison above if the simulation
        had the same mistake."""
        assert 0.0 < number(surge_header, "IntervalDecay") < 1.0
        assert number(surge_header, "CountGrowthPerSurge") > 0.0

    def test_the_floor_is_below_the_interval_and_the_ceiling_above_the_count(
            self, surge_header):
        """A floor above the starting gap would make an accelerating run slower
        than a static one from its first surge, and a ceiling below the starting
        count would make a swelling one smaller than a static one."""
        assert (number(surge_header, "LeastIntervalDays")
                < number(surge_header, "IntervalDays"))
        assert (number(surge_header, "MostDungeonsPerSurge")
                > number(surge_header, "DungeonsPerSurge"))


class TestWhatACityFallingDoes:
    def test_a_fall_fires_a_surge_in_both(self, surge_header, model):
        assert flag(surge_header, "bSurgeOnCityFall") == model.surge_on_city_fall

    def test_a_fall_escalates_in_both(self, surge_header, model):
        assert (flag(surge_header, "bCityFallAdvancesEscalation")
                == model.city_fall_advances_escalation)


class TestTheLethalityModes:
    def test_only_heretic_changes_the_wave(self):
        """The Unreal side has one constant, for Heretic, and answers 1.0 for
        anything else. That is only correct while the other two modes really are
        1.0 in the model."""
        from cataclysm_sim.config import LETHALITY_RULES, LethalityMode

        assert LETHALITY_RULES[
            LethalityMode.STANDARD].surge_dungeon_multiplier == 1.0
        assert LETHALITY_RULES[
            LethalityMode.HARDCORE].surge_dungeon_multiplier == 1.0

    def test_heretics_multiplier_matches(self, surge_header):
        from cataclysm_sim.config import LETHALITY_RULES, LethalityMode

        unreal = number(surge_header, "HereticDungeonMultiplier")
        expected = LETHALITY_RULES[
            LethalityMode.HERETIC].surge_dungeon_multiplier

        assert unreal == pytest.approx(expected), (
            f"Unreal has {unreal}, the model has {expected}")

    def test_the_multiplier_is_applied_after_the_cap_in_both(self, model):
        """THE ORDER, NOT THE NUMBER, and it cannot be read off a constant.

        The simulation caps the count and then multiplies. Doing it the other way
        makes Heretic identical to Standard at every surge that reaches the cap,
        which is where the extra dungeons would hurt most. This is what says the
        model still does it that way, so the Unreal side copying the order is
        still copying something true.
        """
        from cataclysm_sim.config import (LethalityMode, SurgeMode,
                                          TuningConfig)
        from cataclysm_sim.engine import Simulation

        heretic = Simulation(
            TuningConfig(surge_mode=SurgeMode.SWELLING)
            .with_lethality(LethalityMode.HERETIC), seed=0)
        standard = Simulation(
            TuningConfig(surge_mode=SurgeMode.SWELLING)
            .with_lethality(LethalityMode.STANDARD), seed=0)

        # Far past the point where a swelling count reaches its ceiling.
        heretic.surge_index = standard.surge_index = 40

        assert standard.surge_count() == model.surge_count_max, (
            "a swelling Standard run no longer reaches the ceiling at surge 40, "
            "so this test is no longer asking about the cap at all")
        assert heretic.surge_count() > standard.surge_count(), (
            "the model now caps AFTER multiplying, so Heretic and Standard send "
            "the same wave at the ceiling. The Unreal side still multiplies last")


class TestWhereAWaveLands:
    WEIGHTS = {
        "OutpostTargetWeight": "Outpost",
        "BulwarkTargetWeight": "Bulwark",
        "SanctuaryTargetWeight": "Sanctuary",
        "PillarTargetWeight": "Pillar",
    }

    def test_every_tiers_weight_matches(self, surge_header, model):
        from cataclysm_sim.config import CityTier

        for unreal_name, tier_name in self.WEIGHTS.items():
            unreal = number(surge_header, unreal_name)
            expected = model.SURGE_TARGET_WEIGHT[CityTier(tier_name)]

            assert unreal == pytest.approx(expected), (
                f"{unreal_name}: Unreal has {unreal}, the model has {expected}")

    def test_the_pillar_is_never_a_target(self, surge_header, model):
        """A rule rather than a preference: the Pillar is only ever attacked in
        the Last Stand, when the Cataclysm comes to the player."""
        from cataclysm_sim.config import CityTier

        assert number(surge_header, "PillarTargetWeight") == 0.0
        assert model.SURGE_TARGET_WEIGHT[CityTier.PILLAR] == 0.0

    def test_a_smaller_city_is_hit_more_often(self, surge_header):
        """The frontier is Outposts before anything else, which is what makes the
        empire crumble from the outside in rather than at random."""
        weights = [number(surge_header, name) for name in self.WEIGHTS]
        assert weights == sorted(weights, reverse=True), weights


class TestWhatADungeonIs:
    def test_the_resolve_jitter_matches(self, surge_header, model):
        unreal = number(surge_header, "ResolveJitter")
        assert unreal == pytest.approx(model.resolve_jitter), (
            f"Unreal has {unreal}, the model has {model.resolve_jitter}")

    def test_the_jitter_cannot_reorder_two_depths(self, surge_header, model):
        """A jitter of 50% would let an 8-floor dungeon outlast a 15-floor one on
        the same city, and the trade the whole strategy layer rests on -- deeper
        is slower to clear AND slower to bite -- would stop being reliable."""
        jitter = number(surge_header, "ResolveJitter")

        shallow = (model.resolve_base_days + 8 * model.resolve_floor_ratio)
        deep = (model.resolve_base_days + 15 * model.resolve_floor_ratio)

        assert shallow * (1 + jitter) < deep * (1 - jitter), (
            f"at a jitter of {jitter} an 8-floor dungeon can outlast a 15-floor "
            "one on the same Outpost")

    # Every (dungeon kind, city tier) pair the model holds, written out by hand
    # rather than read from either side. Two copies of a number are two numbers;
    # this is deliberately a THIRD, so that the model and the C++ drifting
    # together still fails.
    #
    # THE DAMAGE COLUMNS ARE POINTS AND PEOPLE, NOT FRACTIONS OF THE CITY.
    # Issue #1327 changed the model and issue #1331 changed the game. Each value
    # is the fraction it replaced multiplied by that tier's base maximum:
    # 10% of 1,000, 9% of 3,000, 8% of 8,000 and 6% of 20,000 for defence.
    #
    # THE THREE NON-BASIC KINDS ALL TAKE NOTHING and that is the design rather
    # than a gap: a Quest dungeon never resolves, and a Fallen City and a
    # Cataclysm stand on a city whose damage is already done.
    #
    # THE CATACLYSM HAS ONE ROW. `config.DUNGEON_SPECS` holds it only for the
    # Pillar and `TuningConfig.spec` raises when asked for another, so there are
    # thirteen pairs and not sixteen.
    SPECS = {
        ("Basic", "Outpost"):        (8, 15, 100.0, 250.0),
        ("Basic", "Bulwark"):        (15, 25, 270.0, 1000.0),
        ("Basic", "Sanctuary"):      (25, 40, 640.0, 2400.0),
        ("Basic", "Pillar"):         (40, 60, 1200.0, 4500.0),

        ("Quest", "Outpost"):        (20, 30, 0.0, 0.0),
        ("Quest", "Bulwark"):        (30, 45, 0.0, 0.0),
        ("Quest", "Sanctuary"):      (30, 50, 0.0, 0.0),
        ("Quest", "Pillar"):         (50, 70, 0.0, 0.0),

        ("FallenCity", "Outpost"):   (20, 35, 0.0, 0.0),
        ("FallenCity", "Bulwark"):   (40, 60, 0.0, 0.0),
        ("FallenCity", "Sanctuary"): (60, 85, 0.0, 0.0),
        ("FallenCity", "Pillar"):    (80, 120, 0.0, 0.0),

        ("Cataclysm", "Pillar"):     (100, 150, 0.0, 0.0),
    }

    @staticmethod
    def unreal_specs() -> dict:
        """`SpecFor` in `CataclysmSurge.cpp`, read back as a table.

        WHY THIS PARSES RATHER THAN SEARCHES FOR SUBSTRINGS. The earlier version
        of this test asked whether `LeastFloors = 15;` appeared anywhere in the
        file. That was already loose with four rows and is useless with thirteen:
        `LeastFloors = 20;` is correct for a Quest on an Outpost and for a Fallen
        City on an Outpost, so a search cannot tell a right pairing from a wrong
        one, and swapping two rows would pass. This reads which block each number
        is actually in.

        `default:` IS THE OUTPOST. The C++ writes the Outpost as the default arm
        of each tier switch rather than naming it, which is the file's existing
        style. The Cataclysm has no tier switch at all -- one `if` on the Pillar
        -- so it is read separately.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        body = source.split("UCataclysmSurgeScheduler::SpecFor(", 1)[1]
        body = body.split("return Spec;", 1)[0]

        found = {}
        kinds = re.split(r"case ECataclysmDungeonType::(\w+):", body)

        # re.split leaves the text before the first match at index 0, then
        # alternates name, block, name, block.
        for index in range(1, len(kinds), 2):
            kind = kinds[index]
            block = kinds[index + 1]

            tiers = re.split(r"case ECataclysmCityTier::(\w+):|(default):",
                             block)

            if len(tiers) > 1:
                for spot in range(1, len(tiers), 3):
                    named, fallback = tiers[spot], tiers[spot + 1]
                    tier = "Outpost" if fallback else named
                    numbers = TestWhatADungeonIs._numbers_in(tiers[spot + 2])
                    if numbers is not None:
                        found[(kind, tier)] = numbers
                continue

            # NO TIER SWITCH, SO ONE ARM GUARDED BY AN `if`. That is the
            # Cataclysm, which exists on one tier only.
            #
            # THE TIER IS READ FROM THE CODE RATHER THAN ASSUMED TO BE THE
            # PILLAR. An earlier version of this parser recorded the Pillar
            # whenever it saw one mentioned, which made
            # `test_the_game_builds_no_cataclysm_below_the_pillar` incapable of
            # failing: it could never record a lesser tier, so it asserted
            # something the parser had already made impossible.
            named = re.search(r"Tier == ECataclysmCityTier::(\w+)", block)
            numbers = TestWhatADungeonIs._numbers_in(block)
            if named and numbers is not None:
                found[(kind, named.group(1))] = numbers

        return found

    @staticmethod
    def _numbers_in(block: str):
        """The four spec numbers in one arm, or None if it sets no floors.

        DAMAGE THAT IS NOT WRITTEN IS ZERO, because `FCataclysmDungeonSpec`
        declares both numbers `0.0f` and the three new kinds leave them alone.
        Reading an absent line as zero is what the struct actually does; reading
        it as missing would report every new row as broken.
        """
        def one(name: str, fallback):
            hit = re.search(r"Spec\." + name + r"\s*=\s*([0-9.]+)f?;", block)
            return type(fallback)(hit.group(1)) if hit else fallback

        if not re.search(r"Spec\.LeastFloors", block):
            return None

        return (one("LeastFloors", 0), one("MostFloors", 0),
                one("DefenceDamage", 0.0), one("PopulationDamage", 0.0))

    def test_the_model_holds_the_thirteen_specs_this_test_expects(self, model):
        """The model against the hand-written table above.

        BOTH SIDES NOW HOLD POINTS, so this compares them directly. Issue #1327
        changed the model from a fraction of the host city's own maximum to a
        number of points, because the maximum divided out of how long a city
        survived and every city-health upgrade in the design was worth nothing;
        issue #1331 did the same to the game, where `UCataclysmEmpireMap::Bite`
        had the same shape and its whole call chain depended on it.

        WHILE THE TWO DISAGREED IN SHAPE this asserted the PRODUCT -- the
        model's points against the game's fraction times that tier's maximum --
        so the divergence could not widen unnoticed while #1331 waited. That
        scaffolding is gone now that both sides say the same thing.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        for (kind, tier_name), (least, most, defence, pop) in self.SPECS.items():
            tier = CityTier(tier_name)
            spec = model.spec(DungeonType(kind), tier)

            assert spec.floors == (least, most), (
                f"{kind} on {tier_name}: this test expects {(least, most)} "
                f"floors, the model has {spec.floors}")
            assert spec.defense_damage == pytest.approx(defence), (
                f"{kind} on {tier_name}: the model removes "
                f"{spec.defense_damage} defence points and this test expects "
                f"{defence}")
            assert spec.population_damage == pytest.approx(pop), (
                f"{kind} on {tier_name}: the model removes "
                f"{spec.population_damage} people and this test expects {pop}")

    def test_both_sides_hold_points_rather_than_fractions(self, model):
        """A fraction is below one and a point count is not.

        WITHOUT THIS the test above would go on passing if BOTH sides quietly
        became fractions again together, which is the state issues #1327 and
        #1331 exist to prevent. Only the Basic rows are checked because the
        other three kinds deal no damage by design.

        AND IT IS ASKED OF THE C++ TOO. The hand-written table above is a third
        copy that both sides are compared against, so a table edited back to
        fractions would take both implementations with it and every equality
        check in this file would still pass.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        found = self.unreal_specs()

        for (kind, tier_name) in self.SPECS:
            if kind != "Basic":
                continue

            spec = model.spec(DungeonType(kind), CityTier(tier_name))
            assert spec.defense_damage > 1.0, (
                f"{tier_name} takes {spec.defense_damage} defence damage in "
                "the model, which is a fraction rather than a number of points")
            assert spec.population_damage > 1.0

            _, _, defence, population = found[(kind, tier_name)]
            assert defence > 1.0, (
                f"CataclysmSurge.cpp gives a {tier_name} dungeon {defence} "
                "defence damage, which is a fraction rather than points")
            assert population > 1.0

    def test_the_game_no_longer_takes_a_share_of_the_city_when_a_dungeon_resolves(
            self):
        """`ResolveDungeon` calls the points path, not the share path.

        THE HALF OF ISSUE #1331 THE SPEC TABLE CANNOT SHOW. Points in the table
        would still buy the player nothing if the map went on multiplying them
        by the city's own maximum on the way in, and that multiplication lived
        in `UCataclysmEmpireMap::Bite` rather than in the table.

        `Bite` STILL EXISTS AND THAT IS DELIBERATE -- it is the share shape the
        Siege sub-type keeps by the owner's ruling of 2026-09-05, "Keep it as a
        deliberate exception (Recommended)". So the check is that the RESOLVE
        path does not use it, not that it is gone.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmEmpireRun.cpp")

        resolving = source.split("UCataclysmEmpireRun::ResolveDungeon(", 1)[1]
        resolving = resolving.split("\n}", 1)[0]

        assert "Map->Damage(" in resolving, (
            "ResolveDungeon no longer calls UCataclysmEmpireMap::Damage, so a "
            "dungeon resolving may have gone back to taking a share of its "
            "city. Issue #1331")

        assert "Map->Bite(" not in resolving, (
            "ResolveDungeon calls UCataclysmEmpireMap::Bite, which takes a "
            "SHARE of the city's maximum. That share divides out of how many "
            "resolves a city survives and makes every city-health upgrade "
            "worth nothing. Issue #1331")

    def test_the_map_takes_points_and_not_a_share_of_the_maximum(self):
        """The subtraction in `Damage` has no `MaxDefence` in it.

        THE ONE LINE THE WHOLE ISSUE IS ABOUT. It read
        `City->Defence -= City->MaxDefence * DefenceTaken;`.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmEmpireMap.cpp")

        damaging = source.split("UCataclysmEmpireMap::Damage(", 1)[1]
        damaging = damaging.split("\n}", 1)[0]

        assert "City->Defence -= DefenceTaken;" in damaging, (
            "UCataclysmEmpireMap::Damage no longer subtracts the points it was "
            "given. Issue #1331")

        assert "MaxDefence" not in damaging and "MaxPopulation" not in damaging, (
            "UCataclysmEmpireMap::Damage mentions a city maximum again. The "
            "damage a dungeon deals must not scale with the pool it comes out "
            "of, or raising a city's ceiling buys nothing. Issue #1331")

    def test_a_siege_still_takes_a_share_of_the_city_and_that_is_the_ruling(self):
        """The one place a city maximum is still a factor, on purpose.

        THIS TEST GUARDS A DECISION RATHER THAN A DEFECT, and it is the reverse
        of the two above. Issue #1331 turned every dungeon resolve into points;
        the project owner was asked whether the Siege should follow and answered
        on 2026-09-05, verbatim: "Keep it as a deliberate exception
        (Recommended)" -- a siege does not care how thick your walls are.

        SO A LATER SESSION TIDYING THE LAST PERCENTAGE OUT OF FRESHLY-FLATTENED
        CODE FAILS HERE rather than silently removing the one threat that
        city-health investment does not protect against.
        `sim/cataclysm_sim/engine.py::_apply_siege_damage` is the same shape and
        `docs/DECISIONS.md` carries the ruling.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmEmpireRun.cpp")

        besieging = source.split("UCataclysmEmpireRun::ApplySiegeDamage(", 1)[1]
        besieging = besieging.split("\n}", 1)[0]

        assert "City->MaxDefence * SiegeDefenceBitePerDay" in besieging, (
            "ApplySiegeDamage no longer takes a share of the city's maximum "
            "defence. That share is the project owner's deliberate exception "
            "of 2026-09-05 and not an oversight left over from issue #1331")

        assert "City->MaxPopulation * SiegePopulationBitePerDay" in besieging, (
            "ApplySiegeDamage no longer takes a share of the city's maximum "
            "population. See above")

        # AND THE GROWTH IS STILL POINTS, which is the half of a Siege that city
        # health DOES protect against -- 2.5 points is 0.25% of an Outpost's
        # defence and 0.0125% of the Pillar's.
        assert "SiegeDamageGrowthPerDay * Host.Value" in besieging, (
            "ApplySiegeDamage no longer grows by a flat number of points per "
            "day the Siege has stood. Issue #1329")

    def test_the_model_holds_no_other_spec(self, model):
        """Thirteen and not sixteen. A Cataclysm exists only at the Pillar, and
        a fourteenth row appearing in the model without appearing here would
        otherwise go unnoticed by every check in this file."""
        held = {(kind.value, tier.value) for kind, tier in model.DUNGEON_SPECS}

        assert held == set(self.SPECS), (
            f"the model holds {sorted(held - set(self.SPECS))} that this test "
            f"does not, and lacks {sorted(set(self.SPECS) - held)}")

    def test_every_dungeon_spec_matches_in_the_game(self):
        """The C++ switch against the same hand-written table.

        WHAT THIS CATCHES THAT THE MODEL TEST DOES NOT. `SpecFor` is a switch
        written by hand and the model is a dictionary. Nothing but this compares
        them, and until issue #1324 nothing compared any kind but `Basic` -- so
        the game answering only for Basic while the model answered for all four
        went unreported for as long as both existed.
        """
        found = self.unreal_specs()

        assert found == self.SPECS, (
            "CataclysmSurge.cpp and this test disagree.\n"
            f"  only in the C++: {sorted(set(found) - set(self.SPECS))}\n"
            f"  only in this test: {sorted(set(self.SPECS) - set(found))}\n"
            + "\n".join(
                f"  {pair}: the C++ has {found[pair]}, this test expects "
                f"{self.SPECS[pair]}"
                for pair in sorted(set(found) & set(self.SPECS))
                if found[pair] != self.SPECS[pair]))

    def test_the_game_builds_no_cataclysm_below_the_pillar(self):
        """The model raises when asked for one, so the C++ must not answer.

        A plausible-looking floor range for a Cataclysm on an Outpost would be
        an invented number, and this repository has been bitten by those.
        """
        found = self.unreal_specs()

        for tier in ("Outpost", "Bulwark", "Sanctuary"):
            assert ("Cataclysm", tier) not in found, (
                f"CataclysmSurge.cpp answers a Cataclysm spec on a {tier}, "
                "which the model has no row for")

    def test_a_surge_lands_a_basic_or_a_quest_and_nothing_else(self):
        """A wave a surge rolls is `Basic` or `Quest`. Never the other two.

        THIS TEST HAS BEEN RENAMED TWICE AND BOTH RENAMES ARE THE POINT.

        Slice 1 of #1324 called it `test_nothing_creates_a_dungeon_that_is_not_basic_yet`,
        and its docstring said `MakeDungeon` "is the only thing that puts a
        dungeon on the map". Slice 2 made both claims false -- a city that falls
        becomes a Fallen City dungeon -- and the test kept passing, because it
        only ever read `MakeDungeon`. Slice 2 renamed it to
        `test_a_surge_still_lands_only_basic_dungeons`, which said what it
        actually checked.

        Slice 3 has now made THAT name false: a surge rolls a Quest dungeon
        `QuestChance` of the time. So the name changes again rather than the
        assertion being loosened while the name stays, which is the failure mode
        the epic named as worse than a failing test.

        WHAT IT STILL GUARDS, and it is a real thing rather than a formality.
        Slices 4 to 6 remain. A Fallen City must keep coming from
        `MakeFallenCityDungeon` and nowhere else, and NOTHING may build a
        Cataclysm until slice 6 does it deliberately -- that dungeon is the win
        condition and issues #43 and #1315 also reach for it, so a third route to
        it appearing by accident is exactly what this stops.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        rolled = source.split("UCataclysmSurgeScheduler::RollKind(", 1)[1]
        rolled = rolled.split("\n}", 1)[0]

        assert "ECataclysmDungeonType::Quest" in rolled, (
            "RollKind no longer answers Quest, so a surge lands none. Issue "
            "#1324 slice 3 is what made it")

        assert "ECataclysmDungeonType::Basic" in rolled, (
            "RollKind no longer answers Basic, so every dungeon a surge lands "
            "is a Quest")

        made = source.split("UCataclysmSurgeScheduler::MakeDungeon(", 1)[1]
        made = made.split("return Dungeon;", 1)[0]

        for kind in ("FallenCity", "Cataclysm"):
            for where, body in (("RollKind", rolled), ("MakeDungeon", made)):
                assert f"ECataclysmDungeonType::{kind}" not in body, (
                    f"{where} mentions {kind}, so a surge may now land one. A "
                    "Fallen City comes from MakeFallenCityDungeon and nothing "
                    "builds a Cataclysm; see issue #1324")

    def test_only_a_city_falling_creates_a_dungeon_a_surge_did_not(self):
        """One route puts a dungeon on the map that no surge rolled, and one only.

        Slice 2 of #1324 added `MakeFallenCityDungeon`. This is the guard that a
        third route does not appear without being noticed: the two makers on
        `UCataclysmSurgeScheduler` are the whole of how a dungeon comes to exist.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        makers = re.findall(r"UCataclysmSurgeScheduler::(Make\w*Dungeon)\(",
                            source)

        assert sorted(set(makers)) == ["MakeDungeon", "MakeFallenCityDungeon"], (
            f"the makers of a dungeon are now {sorted(set(makers))}. A new one "
            "means a new way for a dungeon to exist; see issue #1324")

        # AND THE FALLEN CITY ONE IS THE ONLY THING THAT NAMES A KIND A SURGE
        # DOES NOT ROLL.
        fallen = source.split("MakeFallenCityDungeon(\n", 1)[1]
        fallen = fallen.split("\n}", 1)[0]

        assert "ECataclysmDungeonType::FallenCity" in fallen, (
            "MakeFallenCityDungeon no longer makes a Fallen City")

        for kind in ("Quest", "Cataclysm"):
            assert f"ECataclysmDungeonType::{kind}" not in fallen, (
                f"MakeFallenCityDungeon mentions {kind}; it should build one "
                "kind only")


class TestWhatASiegeCostsItsHost:
    """What a Siege costs its host, in the game and in the model.

    FOUR NUMBERS: the three a Siege bites with, which live in
    `CataclysmEmpireRun.h`, and the cap on how many may stand on one city, which
    lives with the scheduler in `CataclysmSurge.h`.

    THIS IS THE ONE PORT CHECK IN THIS FILE THAT RUNS THE OTHER WAY ROUND.
    Everywhere else the model came first and the game copied it. The Siege did
    not: `CataclysmEmpireRun.h` was written from the design document while the
    model gave a sub-type no behaviour at all, and `_apply_siege_damage` was
    added to `sim/cataclysm_sim/engine.py` afterwards, on 2026-09-06, under
    issue #1329. The header says so itself -- "The two must now be kept in step
    like every other ported number" -- and until issue #1353 nothing made them.

    SO A READER WHO ASSUMES THE MODEL IS AUTHORITATIVE WOULD CORRECT THE WRONG
    SIDE. The failure messages below name the game's value first for that
    reason, and the design document sentence each number came from is quoted
    beside the table.

    WHAT `sim/tests/test_siege_subtype.py` CANNOT DO, WHICH IS WHY THIS EXISTS.
    Its `TestItMatchesTheGamesOwnStatedFigures` asserts that an unattended Siege
    empties the four city sizes in 25, 39, 55 and 70 days. Those four figures
    are literals copied out of this header's own prose, and that test runs the
    MODEL and nothing else -- so it notices the model drifting away from them
    and cannot notice the C++ constants moving underneath them.

    WHAT THIS DOES NOT CHECK, because
    `test_a_siege_still_takes_a_share_of_the_city_and_that_is_the_ruling` above
    already does: that `ApplySiegeDamage` still USES all three, in the shapes
    the ruling of 2026-09-05 calls for. A constant the code ignores would
    satisfy every assertion here.
    """

    #: The game's name for each constant, the model's name for it, and the value
    #: both must hold.
    #:
    #: THE VALUE IS A THIRD COPY, WRITTEN OUT BY HAND ON PURPOSE, as the spec
    #: table above is. Comparing the two implementations against each other
    #: alone would pass for a pair edited together, which is the easier mistake
    #: to make here than a one-sided edit: a session porting a design change
    #: touches both files.
    #:
    #: All three come from the Siege row of the sub-type table in
    #: `docs/Cataclysm_GDD_v2.md`: "Deals 1% damage to city defenses and
    #: population per day while active. Increases in power by 2.5 points per
    #: day."
    #:
    #: THE GROWTH WAS 10 UNTIL 2026-09-06, when the owner cut it on issue #1349
    #: -- verbatim, "Halve the rate and cut the growth". The two 1% shares were
    #: explicitly left alone by that ruling.
    #:
    #: THE ROW AND NOT A LINE NUMBER, DELIBERATELY. Eight places in this
    #: repository cited that sentence by line number -- seven as "line 3744"
    #: and one as "line 3732" -- and the document had grown past both. Issue
    #: #1355 replaced every one of them with this row citation, and
    #: `tools/tests/test_the_design_document_is_cited_by_name.py` holds them
    #: there. Issue #1366 widened that guard from the Siege row to every
    #: passage the repository cites, which is why it was renamed.
    #:
    #: THE NUMBER WENT STALE TWICE IN ONE DAY, which is the whole argument.
    #: #1355 was filed naming 3801 as the correct line; by the time it was
    #: fixed, later edits had carried the row to 3839.
    BITES = {
        "SiegeDefenceBitePerDay": ("siege_defence_bite_per_day", 0.01),
        "SiegePopulationBitePerDay": ("siege_population_bite_per_day", 0.01),
        "SiegeDamageGrowthPerDay": ("siege_damage_growth_per_day", 2.5),
    }

    def test_every_siege_constant_matches(self, run_header, model):
        for cpp_name, (model_name, expected) in self.BITES.items():
            assert hasattr(model, model_name), (
                f"TuningConfig no longer has {model_name}, which is the "
                f"model's copy of {cpp_name} in {RUN_HEADER.name}")

            game = number(run_header, cpp_name, where=RUN_HEADER.name)
            held = float(getattr(model, model_name))

            assert game == pytest.approx(held), (
                f"{cpp_name} is {game} in {RUN_HEADER.name} and "
                f"TuningConfig.{model_name} is {held} in the model. The game "
                "is the reference for the Siege; see this class's note")
            assert game == pytest.approx(expected), (
                f"{cpp_name} is {game} in the game and TuningConfig."
                f"{model_name} is {held} in the model, so the two agree -- but "
                f"this test's third copy says {expected}. Both sides appear to "
                "have been edited together")

    def test_the_bites_are_shares_and_the_growth_is_points(self, run_header):
        """The SHAPES, which a pair of numbers wrong in the same way would pass.

        THIS IS THE SAME ARGUMENT AS
        `test_both_sides_hold_points_rather_than_fractions` above and the
        opposite conclusion, because the Siege is the opposite case. Issue #1331
        turned every dungeon resolve into points; the project owner was asked
        whether the Siege should follow and answered on 2026-09-05, verbatim,
        "Keep it as a deliberate exception (Recommended)". So its two bites stay
        shares of the city's maximum -- a siege does not care how thick your
        walls are -- while its growth is points, and the growth is the half of a
        Siege that city health does protect against.

        A bite of 1.0 would empty any city on the day the Siege arrived, and a
        growth below 1.0 would be a fraction wearing a points name.
        """
        for name in ("SiegeDefenceBitePerDay", "SiegePopulationBitePerDay"):
            share = number(run_header, name, where=RUN_HEADER.name)
            assert 0.0 < share < 1.0, (
                f"{name} is {share}, which is not a share of the city's "
                "maximum. That share is the owner's deliberate exception of "
                "2026-09-05, not an oversight left over from issue #1331")

        growth = number(run_header, "SiegeDamageGrowthPerDay",
                        where=RUN_HEADER.name)
        assert growth > 1.0, (
            f"SiegeDamageGrowthPerDay is {growth}, which reads as a fraction "
            "rather than the number of points the design states. Issue #1329")

    def test_the_one_per_city_cap_matches(self, surge_header, model):
        """"Max 1 per city", which lives with the scheduler that enforces it.

        THE ONE SIEGE NUMBER THAT IS NOT IN THE OTHER HEADER. `SiegesPerCity` is
        the scheduler's, because refusing a second Siege is a rolling decision
        rather than a daily cost, so this reads `surge_header` like the rest of
        the file.

        THE BEHAVIOUR IS CHECKED ELSEWHERE AND ONLY THE NUMBER IS HERE.
        `UCataclysmSurgeScheduler::RollSubType` spreads a refused Siege across
        the other six sub-types and `Simulation._roll_subtype` does the same;
        `sim/tests/test_siege_subtype.py::TestOnePerCity` and the
        `Cataclysm.Surge` automation tests own that.
        """
        game = number(surge_header, "SiegesPerCity")

        assert game == pytest.approx(float(model.siege_max_per_city)), (
            f"{SURGE_HEADER.name} caps a city at {game} Sieges and the model's "
            f"siege_max_per_city is {model.siege_max_per_city}")
        assert game == pytest.approx(1.0), (
            f"both sides now cap a city at {game} Sieges. The Siege row of the "
            "sub-type table in `docs/Cataclysm_GDD_v2.md` says "
            "\"Max 1 per city\"")


class TestWhatAFallenCityIs:
    """A city that falls becomes a dungeon standing on itself.

    Slice 2 of issue #1324. The retaken city's half-restore is compared in
    `test_empire_map_port.py`, which owns `UCataclysmEmpireMap::RetakenFraction`;
    what is here is the dungeon the fall leaves behind.
    """

    def test_it_never_resolves_in_either(self, surge_header, model):
        """Its timer is set past the end of any run, in both.

        A Fallen City has no consequence left to apply -- the city it stands on
        has already fallen -- so a timer is meaningless for it. The model says so
        with `(999, 999)` on every Fallen City row.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        unreal = number(surge_header, "FallenCityResolveDays")

        for tier in ("Outpost", "Bulwark", "Sanctuary", "Pillar"):
            spec = model.spec(DungeonType.FALLEN_CITY, CityTier(tier))
            assert spec.resolve_days[0] == pytest.approx(unreal), (
                f"{tier}: the model gives a Fallen City "
                f"{spec.resolve_days[0]} days, the game gives {unreal}")

    def test_it_takes_nothing_from_the_city_in_either(self, model):
        """It destroys nothing at any tier. The city has already fallen.

        ZERO IS ZERO IN EITHER UNIT, which is why this test did not have to
        change its meaning when issue #1327 turned the model's damage from a
        fraction of the city's maximum into a number of points. It changed its
        FIELD NAMES only. The game still holds fractions, and
        `test_every_dungeon_spec_matches_in_the_game` above is what converts
        between the two by multiplying the game's fraction by the tier's own
        maximum. A Fallen City needs no conversion, because it destroys nothing
        under either unit.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        for tier in ("Outpost", "Bulwark", "Sanctuary", "Pillar"):
            spec = model.spec(DungeonType.FALLEN_CITY, CityTier(tier))
            assert spec.defense_damage == 0.0, tier
            assert spec.population_damage == 0.0, tier

    def test_the_games_depth_is_the_siege_that_took_the_city(self):
        """The game derives the depth; it does not roll it.

        `docs/Cataclysm_GDD_v2.md` section VIII: "Floor count equals the number
        of dungeons that were in the city when it fell (minimum 20/40/60 for
        Outpost/Bulwark/Sanctuary)". The spec's shallow ends ARE those minimums,
        so the game reads the floor from there rather than writing it twice.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        made = source.split("MakeFallenCityDungeon(", 1)[1].split("\n}", 1)[0]

        assert "FMath::Max(Absorbed, Spec.LeastFloors)" in made, (
            "MakeFallenCityDungeon no longer takes the deeper of the absorbed "
            "count and the tier's minimum, which is the design's rule")

        assert "Stream" not in made, (
            "MakeFallenCityDungeon draws on a random stream. A Fallen City's "
            "depth is determined by what the city was carrying, not rolled")

    def test_the_model_still_rolls_that_depth_and_that_is_issue_1341(self, model):
        """THIS TEST ASSERTS A DIVERGENCE ON PURPOSE, so that the two halves
        disagreeing does not read as the drift this file exists to catch.

        `Simulation._make_dungeon` draws uniformly from the spec range for every
        kind, so a city besieged by six dungeons leaves the same distribution of
        Fallen City as one besieged by one. The design says otherwise and the
        design is authoritative here, so the MODEL is what is wrong. Issue #1341.

        The owner ruled the same way on the same class of conflict on 2026-09-06,
        about a Quest dungeon's adjacency: verbatim "Adjacent, and fix the
        simulation".

        **When #1341 is fixed this test should fail**, and be replaced by one
        comparing the two rules rather than recording that they differ.
        """
        import inspect

        from cataclysm_sim.engine import Simulation

        made = inspect.getsource(Simulation._make_dungeon)

        assert "self.rng.randint(lo, hi)" in made, (
            "Simulation._make_dungeon no longer rolls its floor count. If #1341 "
            "has been fixed, replace this test with one that compares the "
            "game's rule against the model's rather than recording that they "
            "differ")

        fall = inspect.getsource(Simulation._fall)

        assert "absorbed" in fall and "len(absorbed)" not in fall, (
            "Simulation._fall now appears to use how many dungeons it absorbed. "
            "If #1341 has been fixed, this test needs replacing; see above")


class TestWhatAQuestDungeonIs:
    """A surge rolls one instead of a Basic, it refreshes instead of biting,
    and its timer running out moves it to an adjacent city.

    Slices 3 and 4 of issue #1324. Slice 3 shipped with a
    `test_neither_moves_a_quest_dungeon_yet` recording relocation as scope; the
    three tests at the end of this class replace it, and the reason they are
    three rather than one is that "adjacent" is a decision the design does not
    make -- so what the two halves agree on has to be checked in three places:
    that they both move it, what they both count as adjacent, and what they both
    do when there is nowhere to go.
    """

    def test_the_chance_a_surge_rolls_one_matches(self, surge_header, model):
        """`QuestChance` against `config.quest_dungeon_chance`.

        **THIS NUMBER IS KNOWN TO BE SUPERSEDED AND THIS TEST IS STILL THE RIGHT
        ONE.** The project owner ruled on 2026-09-06, verbatim "It should depend
        on the Cataclysm", so the settled design is a rule keyed on which
        Cataclysm sent the wave rather than a single figure. The game cannot
        express that yet -- its empire layer has no Cataclysm identity at all --
        and issue #1357 owns getting there.

        Until it does, both halves hold the same flat number and the job of this
        file is to make sure they hold the SAME one. A drift here would mean the
        game rolling Quest dungeons at a rate no balance figure was measured at.
        """
        unreal = number(surge_header, "QuestChance")

        assert model.quest_dungeon_chance == pytest.approx(unreal), (
            f"the model rolls a Quest dungeon {model.quest_dungeon_chance:.0%} "
            f"of the time and the game {unreal:.0%}")

    def test_its_timer_is_the_models_relocation_clock_at_every_tier(
            self, surge_header, model):
        """One flat figure in the game against four rows in the model.

        The model reads `spec.resolve_days[0]` for every kind but `Basic`
        (`Simulation._make_dungeon`), and all four Quest rows give the same
        number, so the game holds one constant. **All four are checked and not
        just one**: if a tier's row ever diverges, the game holding a single
        constant stops being right and this is what says so.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        unreal = number(surge_header, "QuestResolveDays")

        for tier in ("Outpost", "Bulwark", "Sanctuary", "Pillar"):
            spec = model.spec(DungeonType.QUEST, CityTier(tier))
            assert spec.resolve_days[0] == pytest.approx(unreal), (
                f"{tier}: the model gives a Quest dungeon "
                f"{spec.resolve_days[0]} days, the game gives {unreal}")

    def test_it_takes_nothing_from_the_city_in_either(self, model):
        """It destroys nothing at any tier, in either half.

        ZERO IS ZERO IN EITHER UNIT, so this survived issue #1327 turning the
        model's damage from a fraction of the city's maximum into a number of
        points, exactly as the Fallen City version of it did.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        for tier in ("Outpost", "Bulwark", "Sanctuary", "Pillar"):
            spec = model.spec(DungeonType.QUEST, CityTier(tier))
            assert spec.defense_damage == 0.0, tier
            assert spec.population_damage == 0.0, tier

    def test_neither_lets_one_bite_its_host_when_the_timer_runs_out(self):
        """Both halves refuse on the KIND, not on the damage being zero.

        WHY THAT DISTINCTION IS WORTH A TEST. All four Quest rows carry zero
        damage today, so a check of the numbers would give the same answer for
        the wrong reason. Issue #1327 made city damage flat points and asked, as
        issue #1324's own ninth design question, whether the three non-Basic
        kinds should deal any at all. If that is ever answered yes, a
        zero-valued check silently starts letting a Quest dungeon detonate. The
        design speaks about the kind, so both halves read the kind.
        """
        import inspect

        from cataclysm_sim.engine import Dungeon, Simulation

        resolves = inspect.getsource(Dungeon.resolves.fget)

        assert "DungeonType.BASIC" in resolves, (
            "Simulation's Dungeon.resolves no longer answers on the kind. If "
            "the model now decides by reading the damage, the game's "
            "FCataclysmDungeon::Resolves has to change with it")

        resolve = inspect.getsource(Simulation._resolve)

        assert "d.dtype is DungeonType.QUEST" in resolve, (
            "Simulation._resolve no longer singles a Quest dungeon out before "
            "it detonates")

        run = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
                   / "CataclysmEmpireRun.cpp")

        body = run.split("UCataclysmEmpireRun::ResolveDungeon(", 1)[1]
        body = body.split("\n}", 1)[0]

        assert "if (!Dungeon->Resolves())" in body, (
            "UCataclysmEmpireRun::ResolveDungeon no longer asks whether the "
            "dungeon is a kind that detonates, so a Quest dungeon now bites "
            "the city it is standing on. Issue #1324 slice 3")

        header = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.h")

        assert ("bool Resolves() const { return Type == "
                "ECataclysmDungeonType::Basic; }") in header, (
            "FCataclysmDungeon::Resolves is no longer 'Basic and nothing "
            "else', which is what Dungeon.resolves in the model says")

    def test_the_game_does_not_roll_its_timer_from_its_depth(self):
        """A Quest dungeon's timer is flat; a Basic one's comes from its floors.

        THE DEPTH RULE IS NOT BEING BROKEN HERE AND THE DISTINCTION MATTERS.
        `CLAUDE.md` requires a resolve timer to scale with depth, because a
        deeper dungeon is worth more and should be slower to bite. A Quest
        dungeon has no bite for its depth to be traded against, and its timer is
        how long the player has before the objective moves -- which the design
        relates to nothing.

        **THE JITTER DRAW STILL HAPPENS EITHER WAY.** That is not decoration: a
        Quest dungeon that took fewer numbers off the stream would change every
        later roll in the run depending on which kinds came out earlier.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        made = source.split("UCataclysmSurgeScheduler::MakeDungeon(", 1)[1]
        made = made.split("return Dungeon;", 1)[0]

        assert "Dungeon.ResolveDays = (Kind == ECataclysmDungeonType::Quest)" \
            in made, (
                "MakeDungeon no longer gives a Quest dungeon a different timer "
                "from a Basic one's")

        assert "? QuestResolveDays" in made, (
            "MakeDungeon no longer uses QuestResolveDays for a Quest dungeon's "
            "timer")

        # THE DRAW IS OUTSIDE THE BRANCH, so both kinds cost the stream the same.
        jitter = made.split("const float Jitter", 1)[1]
        jitter = jitter.split("Dungeon.ResolveDays", 1)[0]

        assert "Kind ==" not in jitter and "Type ==" not in jitter, (
            "the resolve jitter is now drawn conditionally. A Quest dungeon "
            "must take the same number of draws off the stream as a Basic one, "
            "or which kinds came out earlier changes every later roll")

    def test_both_move_it_to_an_adjacent_city_and_neither_moves_it_anywhere(
            self, model):
        """Slice 4 of issue #1324, and the rule both halves must now share.

        THIS REPLACES `test_neither_moves_a_quest_dungeon_yet`, which asserted
        the absence of the move on purpose while it was still scope. It said so
        in its own docstring -- "when slice 4 lands it should fail" -- and it
        did.

        **THE MODEL WAS THE DEFECT AND THE GAME WAS NEVER BROUGHT INTO STEP WITH
        IT.** `Simulation._resolve` moved a Quest dungeon to a uniformly random
        exposed city anywhere on the map, through `Empire.exposed_cities()`,
        which filters for exposure and nothing else. The design says "may move
        to ADJACENT city" and the project owner ruled on 2026-09-06, verbatim
        "Adjacent, and fix the simulation". So this checks BOTH halves ask for
        neighbours, and that the model's old call is gone rather than sitting
        beside the new one.
        """
        import inspect

        from cataclysm_sim.engine import Simulation

        assert model.quest_relocates is True, (
            "the model has stopped relocating quest dungeons. That is not the "
            "fix issue #1324 asked for -- the owner ruled the move should be to "
            "an ADJACENT city, not that it should stop")

        resolve = inspect.getsource(Simulation._resolve)

        assert "self.empire.adjacent_exposed_cities(city)" in resolve, (
            "Simulation._resolve no longer moves a quest dungeon to an adjacent "
            "city. The owner ruled on 2026-09-06, verbatim \"Adjacent, and fix "
            "the simulation\"; issue #1324 slice 4")

        # THE OLD CALL HAS TO BE GONE, not merely joined by the new one. A
        # `_resolve` that fell back to `exposed_cities()` when no neighbour was
        # exposed would pass the assertion above and still teleport.
        assert "self.empire.exposed_cities()" not in resolve, (
            "Simulation._resolve still reaches for every exposed city on the "
            "map. Whatever it does with the result, the move-anywhere rule the "
            "owner called a defect is back in the function")

        run = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                   / "Empire" / "CataclysmEmpireRun.cpp")

        moved = run.split("UCataclysmEmpireRun::RelocateQuestDungeon(", 1)
        assert len(moved) == 2, (
            "UCataclysmEmpireRun::RelocateQuestDungeon is gone. It is the "
            "game's half of the relocation rule and the only thing in that "
            "class that moves a dungeon; issue #1324 slice 4")

        body = moved[1].split("\n}\n", 1)[0]

        assert "UCataclysmSurgeScheduler::PickRelocation" in body, (
            "RelocateQuestDungeon no longer asks the scheduler where to move "
            "to. `PickRelocation` is where the adjacency rule lives, and a "
            "second copy of it here is how the two would drift")

        # THE BITING PATH IS STILL UNABLE TO MOVE ANYTHING. The const pointer in
        # `ResolveDungeon` was the structural fact the replaced test rested on,
        # and it still holds: the mutable lookup is in `RelocateQuestDungeon`
        # alone. A first attempt at the replaced test asserted that "CityId ="
        # did not appear anywhere in `ResolveDungeon`, which was wrong in the
        # loose direction -- it already reads `const int32 CityId =
        # Dungeon->CityId;`.
        resolving = run.split("UCataclysmEmpireRun::ResolveDungeon(", 1)[1]
        resolving = resolving.split("\n}\n", 1)[0]

        assert "const FCataclysmDungeon* Dungeon = FindDungeon(DungeonId);" \
            in resolving, (
                "UCataclysmEmpireRun::ResolveDungeon no longer holds the "
                "dungeon by const pointer. Moving a dungeon belongs in "
                "RelocateQuestDungeon, and keeping the biting path unable to "
                "move one is what says so structurally rather than by comment")

    def test_the_two_adjacency_rules_name_the_same_three_kinds_of_link(self):
        """What "adjacent" means, in both halves, read out of the source.

        THE DESIGN NEVER DEFINES IT, so this is a decision rather than a port,
        and it is recorded in `docs/DECISIONS.md`. Both halves take every link
        the map has: the neighbours one ring out, the neighbours one ring in,
        and the rim's perimeter links. `test_quest_objective_counts_are_stated.py`
        guards the decision entry itself.

        WHY THE PERIMETER IS THE ONE WORTH CHECKING. Dropping it is the change
        somebody would make while tidying, because `UCataclysmEmpireMap`'s own
        class comment used to say adjacency was strictly orthogonal. It costs
        about ten percentage points of relocation frequency -- 79.5% of quest
        timers having somewhere to go against 69.1% -- and nothing else in
        either codebase would notice.

        **THE COMMENTS ARE STRIPPED BEFORE ANYTHING IS SEARCHED FOR, AND THE
        FIRST VERSION OF THIS TEST DID NOT DO THAT.** It looked for the bare
        string `City.Perimeter` anywhere in the function. Proving it fired meant
        commenting the append out -- the obvious surgical break -- and the
        string was still there, in the comment, so the test passed against a
        game that had stopped counting the perimeter. It was a guard that could
        not fail. Both halves now match the whole statement with its comments
        removed.
        """
        import inspect

        from cataclysm_sim.world import Empire

        def code_only(text: str, marker: str) -> str:
            """The lines of `text` with everything after `marker` removed."""
            return "\n".join(line.split(marker, 1)[0] for line in
                             text.splitlines())

        neighbours = code_only(inspect.getsource(Empire.neighbours), "#")

        for link in ("list(c.outward)", "list(c.inward)", "list(c.perimeter)"):
            assert link in neighbours, (
                f"Empire.neighbours no longer counts {link}. All three links "
                "are adjacency in this project; see docs/DECISIONS.md, "
                "2026-09-06")

        surge = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                     / "Empire" / "CataclysmSurge.cpp")

        adjacent = surge.split(
            "UCataclysmSurgeScheduler::AdjacentCities(", 1)[1]
        adjacent = code_only(adjacent.split("\n}\n", 1)[0], "//")

        for link in ("City.Outward", "City.Inward", "City.Perimeter"):
            statement = f"Neighbours.Append({link});"
            assert statement in adjacent, (
                f"UCataclysmSurgeScheduler::AdjacentCities no longer counts "
                f"{link}, so the game and the model disagree about which "
                "cities are adjacent")

    def test_both_leave_it_where_it_stands_when_no_neighbour_is_exposed(self):
        """The design says a Quest dungeon "MAY move", and this is where the
        "may" comes from: no die roll, just a dungeon with nowhere to go.

        AND NEITHER HALF MAY TAKE A DRAW ON THAT PATH. The model's
        `self.rng.choice` sits behind `if targets`, and the game's
        `PickRelocation` returns before `Stream.RandRange`. A draw on the empty
        case would put the two streams permanently out of step, which is the
        same rule `MakeDungeon` follows for the resolve jitter.
        """
        import inspect

        from cataclysm_sim.engine import Simulation

        resolve = inspect.getsource(Simulation._resolve)

        choice = resolve.split("adjacent_exposed_cities(city)", 1)[1]
        choice = choice.split("return", 1)[0]

        assert "if targets:" in choice, (
            "Simulation._resolve no longer guards its relocation draw on there "
            "being a target. An unguarded draw both crashes on the empty list "
            "and, if made safe some other way, takes a number off the stream on "
            "a day the game would not")

        surge = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                     / "Empire" / "CataclysmSurge.cpp")

        picked = surge.split(
            "UCataclysmSurgeScheduler::PickRelocation(", 1)[1]
        picked = picked.split("\n}\n", 1)[0]

        empty, drawn = picked.split("return INDEX_NONE;", 1)

        assert "Targets.Num() == 0" in empty, (
            "UCataclysmSurgeScheduler::PickRelocation no longer answers "
            "INDEX_NONE for a dungeon with no exposed neighbour. That is the "
            "design's \"may move\", and without it the function has to invent "
            "somewhere to send it")

        assert "Stream." not in empty and "Stream." in drawn, (
            "PickRelocation now touches the stream before it knows whether it "
            "has anywhere to send the dungeon. The model takes no draw on that "
            "path, so the two runs would diverge from the first quest timer "
            "that fires with nowhere to go")

    def test_neither_half_carries_the_new_hosts_tier_with_the_dungeon(self):
        """**A RELOCATED DUNGEON KEEPS THE TIER ITS DEPTH WAS ROLLED FROM.**

        The project owner ruled on 2026-09-06, verbatim "Keeps everything, fix
        the size". `Dungeon.city_tier` and `FCataclysmDungeon::CityTier` are the
        tier the DEPTH was rolled from, not the host's tier, and each is read to
        find the specification row whose floor midpoint the depth is divided by.
        Both implementations used to assign the destination's tier while leaving
        the floor count alone, so the two halves of that division named
        different rows.

        **WHY A SOURCE-LEVEL CHECK ON TOP OF TWO CAMPAIGN TESTS.** The campaign
        tests are the ones that prove the behaviour, and the C++ one costs four
        builds to prove it fires. This one costs nothing, fails in the fast
        suite, and is the guard that catches somebody putting either line back
        while looking at only one side of the port. Issue #1324.

        THE COMMENTS ARE STRIPPED FIRST, because both files explain the removed
        line in a comment that names it, and a bare substring search would match
        the explanation. That failure has already happened once on this issue,
        on `City.Perimeter`.
        """
        import inspect

        from cataclysm_sim.engine import Simulation

        def code_only(text: str, marker: str) -> str:
            return "\n".join(line.split(marker, 1)[0] for line in
                              text.splitlines())

        resolve = code_only(inspect.getsource(Simulation._resolve), "#")

        assert "city_tier =" not in resolve, (
            "Simulation._resolve assigns city_tier again. It is the tier the "
            "dungeon's DEPTH was rolled from and the floor count does not move "
            "either; the owner ruled on 2026-09-06, verbatim \"Keeps "
            "everything, fix the size\". See TestWhatARelocatedDungeonKeeps in "
            "sim/tests/test_quest_relocation_is_adjacent.py")

        run = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                   / "Empire" / "CataclysmEmpireRun.cpp")

        moved = run.split("UCataclysmEmpireRun::RelocateQuestDungeon(", 1)[1]
        moved = code_only(moved.split("\n}\n", 1)[0], "//")

        assert "CityTier =" not in moved, (
            "UCataclysmEmpireRun::RelocateQuestDungeon assigns CityTier again, "
            "so the game and the model disagree about what a moving dungeon "
            "carries. `BiteScale` divides Floors by the midpoint of "
            "SpecFor(Type, CityTier) and Floors does not move")

        # AND THE MOVE ITSELF IS STILL THERE, or the two assertions above are
        # satisfied by a function that stopped moving anything at all.
        assert "CityId = MovingTo;" in moved, (
            "RelocateQuestDungeon no longer moves the dungeon, so the checks "
            "above prove nothing")


class TestTheEscalationModes:
    def test_the_enum_names_the_same_four_modes(self, surge_header):
        from cataclysm_sim.config import SurgeMode

        block = re.search(
            r"enum\s+class\s+ECataclysmSurgeMode\s*:\s*uint8\s*\{(.*?)\}",
            surge_header, re.DOTALL)
        if not block:
            pytest.fail("could not find ECataclysmSurgeMode in "
                        f"{SURGE_HEADER.name}; has it been renamed?")

        names = {name.lower()
                 for name, _ in re.findall(r"(\w+)\s*=\s*(\d+)",
                                           block.group(1))}

        expected = {mode.value.lower() for mode in SurgeMode}

        assert names == expected, (
            f"Unreal names {sorted(names)}, the model names {sorted(expected)}")


class TestTheHandWorkedFiguresInTheUnrealTest:
    """`CataclysmSurgeTests.cpp` asserts a ladder of counts and gaps that was
    read out of the simulation by running it. This is what notices if the model
    changes afterwards -- the Unreal test would keep passing against figures that
    no longer describe anything.
    """

    def gap_at(self, mode, index: float) -> float:
        from cataclysm_sim.config import SurgeMode, TuningConfig
        from cataclysm_sim.engine import Simulation

        simulation = Simulation(TuningConfig(surge_mode=SurgeMode(mode)), seed=0)
        simulation.surge_index = index
        return simulation.surge_gap()

    def count_at(self, mode, lethality, index: int) -> int:
        from cataclysm_sim.config import LethalityMode, SurgeMode, TuningConfig
        from cataclysm_sim.engine import Simulation

        simulation = Simulation(
            TuningConfig(surge_mode=SurgeMode(mode))
            .with_lethality(LethalityMode(lethality)), seed=0)
        simulation.surge_index = index
        return simulation.surge_count()

    def test_the_accelerating_ladder_still_holds(self):
        assert self.gap_at("accelerating", 1) == pytest.approx(105.6, abs=0.01)
        assert self.gap_at("accelerating", 2) == pytest.approx(92.928, abs=0.01)
        assert self.gap_at("accelerating", 3) == pytest.approx(81.7766, abs=0.01)
        assert self.gap_at("accelerating", 12) == pytest.approx(25.8805, abs=0.01)
        assert self.gap_at("accelerating", 13) == pytest.approx(25.0, abs=0.01)

    def test_the_swelling_ladder_still_holds(self):
        assert self.count_at("swelling", "standard", 0) == 4
        assert self.count_at("swelling", "standard", 2) == 5
        assert self.count_at("swelling", "standard", 4) == 6
        assert self.count_at("swelling", "standard", 19) == 13
        assert self.count_at("swelling", "standard", 20) == 14

    def test_heretic_at_the_ceiling_is_still_seventeen(self):
        """The figure the ordering argument turns on. If it ever equals the
        Standard ceiling again, the cap has moved back in front of the
        multiplier."""
        assert self.count_at("swelling", "heretic", 20) == 17
        assert self.count_at("swelling", "standard", 20) == 14

    def test_the_unreal_test_carries_those_same_figures(self):
        text = read(SURGE_TESTS)

        for figure in ("105.6f", "92.928f", "81.7766f", "25.8805f"):
            assert figure in text, (
                f"CataclysmSurgeTests.cpp no longer asserts {figure}; the "
                "accelerating ladder there and the model's have parted")
