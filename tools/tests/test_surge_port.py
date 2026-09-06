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


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str) -> str:
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {SURGE_HEADER.name}; "
                    "has it been renamed?")
    return match.group(1)


def number(text: str, name: str) -> float:
    return float(constant(text, name, r"-?[0-9.]+f?").rstrip("f"))


def flag(text: str, name: str) -> bool:
    return constant(text, name, r"true|false") == "true"


@pytest.fixture(scope="module")
def surge_header() -> str:
    return read(SURGE_HEADER)


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
    # THE THREE NON-BASIC KINDS ALL BITE NOTHING and that is the design rather
    # than a gap: a Quest dungeon never resolves, and a Fallen City and a
    # Cataclysm stand on a city whose damage is already done.
    #
    # THE CATACLYSM HAS ONE ROW. `config.DUNGEON_SPECS` holds it only for the
    # Pillar and `TuningConfig.spec` raises when asked for another, so there are
    # thirteen pairs and not sixteen.
    SPECS = {
        ("Basic", "Outpost"):        (8, 15, 0.10, 0.05),
        ("Basic", "Bulwark"):        (15, 25, 0.09, 0.05),
        ("Basic", "Sanctuary"):      (25, 40, 0.08, 0.04),
        ("Basic", "Pillar"):         (40, 60, 0.06, 0.03),

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

        A BITE THAT IS NOT WRITTEN IS ZERO, because `FCataclysmDungeonSpec`
        declares both bites `0.0f` and the three new kinds leave them alone.
        Reading an absent line as zero is what the struct actually does; reading
        it as missing would report every new row as broken.
        """
        def one(name: str, fallback):
            hit = re.search(r"Spec\." + name + r"\s*=\s*([0-9.]+)f?;", block)
            return type(fallback)(hit.group(1)) if hit else fallback

        if not re.search(r"Spec\.LeastFloors", block):
            return None

        return (one("LeastFloors", 0), one("MostFloors", 0),
                one("DefenceBite", 0.0), one("PopulationBite", 0.0))

    def test_the_model_holds_the_thirteen_specs_this_test_expects(self, model):
        """The model against the hand-written table above.

        THE TWO SIDES NOW EXPRESS DAMAGE DIFFERENTLY AND STILL AGREE ON IT.
        Issue #1327 changed the model from a fraction of the host city's own
        maximum to a number of points, because the maximum divided out of how
        long a city survived and every city-health upgrade in the design was
        worth nothing. The game still holds fractions -- `Bite` in
        `CataclysmEmpireMap.cpp` takes them, so its whole call chain depends on
        the old shape -- and issue #1331 tracks porting it across.

        Until that lands the two are the SAME DAMAGE, because each of the
        model's point values is the fraction beside it multiplied by that
        tier's base maximum. Asserting the product rather than the fraction is
        what keeps this test comparing the two implementations instead of
        quietly comparing one of them with itself.

        `test_empire_map_port.py` ties `TIER_STATS` to the C++
        `OutpostMaxDefence` and its three siblings, so multiplying by the
        model's own maximum here is not a shortcut past the C++ figure.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        for (kind, tier_name), (least, most, defence, pop) in self.SPECS.items():
            tier = CityTier(tier_name)
            spec = model.spec(DungeonType(kind), tier)
            stats = model.TIER_STATS[tier]

            assert spec.floors == (least, most), (
                f"{kind} on {tier_name}: this test expects {(least, most)} "
                f"floors, the model has {spec.floors}")
            assert spec.defense_damage == pytest.approx(
                defence * stats.max_defense), (
                f"{kind} on {tier_name}: the model removes "
                f"{spec.defense_damage} defence points and the game removes "
                f"{defence:.0%} of {stats.max_defense:,.0f}, which is "
                f"{defence * stats.max_defense:,.0f}")
            assert spec.population_damage == pytest.approx(
                pop * stats.max_population), (
                f"{kind} on {tier_name}: the model removes "
                f"{spec.population_damage} people and the game removes "
                f"{pop:.0%} of {stats.max_population:,.0f}")

    def test_the_model_holds_points_rather_than_fractions(self, model):
        """A fraction is below one and a point count is not.

        WITHOUT THIS the test above would go on passing if the model's numbers
        quietly became fractions again and `TIER_STATS` shrank to match, which
        is the state issue #1327 exists to prevent. Only the Basic rows are
        checked because the other three kinds deal no damage by design.
        """
        from cataclysm_sim.config import CityTier, DungeonType

        for (kind, tier_name) in self.SPECS:
            if kind != "Basic":
                continue
            spec = model.spec(DungeonType(kind), CityTier(tier_name))
            assert spec.defense_damage > 1.0, (
                f"{tier_name} takes {spec.defense_damage} defence damage, "
                "which is a fraction rather than a number of points")
            assert spec.population_damage > 1.0

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

    def test_a_surge_still_lands_only_basic_dungeons(self):
        """A wave a surge rolls is `Basic` and nothing else, still.

        THIS TEST WAS RENAMED, AND THE OLD NAME IS WHY. Slice 1 of #1324 called
        it `test_nothing_creates_a_dungeon_that_is_not_basic_yet`, and its
        docstring said `MakeDungeon` "is the only thing that puts a dungeon on
        the map". Slice 2 made both false: a city that falls becomes a Fallen
        City dungeon, built by `MakeFallenCityDungeon` and added by
        `UCataclysmEmpireRun::CityFell`. The test kept passing, because it only
        ever read `MakeDungeon` -- so its NAME claimed something broader than
        what it checked, which is worse than a failure. It now says what it
        checks.

        WHAT IT STILL GUARDS. Slices 3 to 6 are the ones that make a surge roll a
        kind. When one lands, this test should fail and be rewritten
        deliberately, rather than the scope drifting.
        """
        source = read(REPO_ROOT / "game" / "Source" / "CataclysmEmpire"
                      / "Empire" / "CataclysmSurge.cpp")

        made = source.split("UCataclysmSurgeScheduler::MakeDungeon(", 1)[1]
        made = made.split("return Dungeon;", 1)[0]

        assert "Dungeon.Type = ECataclysmDungeonType::Basic;" in made, (
            "MakeDungeon no longer sets Basic. If a surge now rolls a kind, "
            "this test has done its job and should be replaced by one that "
            "checks the roll.")

        for kind in ("Quest", "FallenCity", "Cataclysm"):
            assert f"ECataclysmDungeonType::{kind}" not in made, (
                f"MakeDungeon mentions {kind}, so a surge may now land one. "
                "See issue #1324 for the slice that is meant to.")

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
        """Both bites are zero at every tier. The city has already fallen."""
        from cataclysm_sim.config import CityTier, DungeonType

        for tier in ("Outpost", "Bulwark", "Sanctuary", "Pillar"):
            spec = model.spec(DungeonType.FALLEN_CITY, CityTier(tier))
            assert spec.defense_bite == 0.0, tier
            assert spec.population_bite == 0.0, tier

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
