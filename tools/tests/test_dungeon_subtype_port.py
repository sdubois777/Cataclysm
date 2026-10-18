"""The Unreal dungeon sub-type weights must match the simulation's.

WHY THIS EXISTS AT ALL. `UCataclysmSurgeScheduler::RollSubType` in the
`CataclysmEmpire` module rolls a sub-type for every dungeon a surge puts on the
map, and the weights it rolls from are a copy of `config.SUBTYPE_SPAWN_WEIGHTS`
in `sim/cataclysm_sim/config.py`. Two copies of a number are two numbers.
`test_day_clock_port.py`, `test_surge_port.py` and `test_empire_map_port.py`
beside this guard the same arrangement for the other constants in that module,
and `CLAUDE.md` carries a rule about it because the power model in
`sim/cataclysm_sim/scoring.py` drifted from its own source twice before anyone
noticed.

EVERY DUNGEON A SURGE MAKES HAS A SUB-TYPE, so `None` is not in the distribution
on either side. The owner ruled that on 2026-09-05, in
https://github.com/sdubois777/Cataclysm/issues/1293. Before it, no sub-type at
all was the commonest outcome at 34 in 100, and the remaining weights were
rescaled to take up that share.

`ECataclysmDungeonSubType::None` STILL EXISTS AND IS STILL REACHED, which is why
this file checks that it is in the enum and NOT in the distribution rather than
checking it is gone. A dungeon entered outside the empire has no sub-type --
`UCataclysmGameMode::RunDungeonSubType` returns that value -- and it is what
`UCataclysmEnemyScore::SubTypeWeight` gives no difficulty.

NOTHING A SURGE MAKES CARRIES IT. A Siege refused by the one-per-city cap used
to, until the owner ruled on 2026-09-06 that such a dungeon is spread across the
other six sub-types instead.

WHAT IT DOES NOT CHECK. That the roll behaves -- that the spread over many rolls
matches the weights, that it takes exactly one draw from the stream, that a Cow
Level's walk really is doubled and really cannot be shortened. Those are Unreal
automation tests under the `Cataclysm.Surge` prefix. A test written against a
constant cannot notice that the constant is wrong, and a test that reads the
constant out of the source cannot notice that the code ignores it, so both halves
are needed.

THE TWO SIDES ROLL IN DIFFERENT ORDERS AND THAT IS FINE. The model lists its
weights commonest first; `ECataclysmDungeonSubType` lists them in the order the
design document does, and its numbering is persisted so it cannot be reordered.
The same random fraction therefore picks different sub-types on the two sides.
The distributions are identical, which is what is compared here -- weights by
name, never a sequence of rolls.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

SURGE_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
                / "CataclysmSurge.h")

KIND_HEADER = (REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
               / "CataclysmDungeonKind.h")

GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The heading over the design's spawn table, matched by its opening rather than
#: in full: the rest of the line explains which quantity it is and is prose.
#:
#: THERE ARE TWO SUBTYPE TABLES IN THAT DOCUMENT AND THEY ARE NOT THE SAME
#: QUANTITY. "Subtype Difficulty Weights" says how much harder each one makes a
#: dungeon and is guarded by `test_enemy_score_formula.py`. This one says how
#: often each is rolled. Reading the wrong one was most of issue #1293.
SPAWN_HEADING = "### **Subtype Spawn Weights"

#: The name the model gives each sub-type, against the name the C++ constant and
#: the enumerator use. The model's names carry a space where C++ cannot.
NAMES = {
    "Timed": "Timed",
    "Horde": "Horde",
    "Siege": "Siege",
    "Cow Level": "CowLevel",
    "Elite": "Elite",
    "Volatile": "Volatile",
    "Sacrificial": "Sacrificial",
}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def constant(text: str, name: str, pattern: str, where: str) -> str:
    """One `static constexpr` value out of a header, by name."""
    match = re.search(
        rf"static\s+constexpr\s+\w+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {where}; has it been renamed?")
    return match.group(1)


def weight(text: str, cpp_name: str) -> float:
    return float(constant(text, f"SpawnWeight{cpp_name}", r"[0-9.]+f?",
                          SURGE_HEADER.name).rstrip("f"))


@pytest.fixture(scope="module")
def surge_source() -> str:
    return read(SURGE_HEADER)


@pytest.fixture(scope="module")
def kind_source() -> str:
    return read(KIND_HEADER)


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


class TestTheSpawnWeights:
    def test_every_sub_type_carries_the_same_weight_in_both(
            self, surge_source, model):
        for model_name, cpp_name in NAMES.items():
            assert model_name in model.SUBTYPE_SPAWN_WEIGHTS, (
                f"the model no longer has a sub-type called {model_name!r}; "
                "if it was renamed, rename it in NAMES and in "
                "ECataclysmDungeonSubType too")

            unreal = weight(surge_source, cpp_name)
            wanted = model.SUBTYPE_SPAWN_WEIGHTS[model_name]

            assert unreal == pytest.approx(wanted), (
                f"{model_name}: Unreal has {unreal}, the model has {wanted}")

    def test_neither_side_knows_a_sub_type_the_other_does_not(self, model):
        assert set(model.SUBTYPE_SPAWN_WEIGHTS) == set(NAMES), (
            "the model's sub-types and the ported ones have parted company: "
            f"model has {sorted(model.SUBTYPE_SPAWN_WEIGHTS)}, "
            f"the port covers {sorted(NAMES)}")

    def test_the_weights_add_up_to_a_hundred_on_both_sides(
            self, surge_source, model):
        """Not required by either roll, which divides by its own total, but it
        is what makes each weight readable as a percentage. A change that
        forgets to rebalance the rest shows up here, which is exactly what
        removing the no-sub-type outcome had to avoid."""
        unreal = sum(weight(surge_source, cpp) for cpp in NAMES.values())

        assert unreal == pytest.approx(100.0), (
            f"the Unreal weights add up to {unreal}, not 100")
        assert sum(model.SUBTYPE_SPAWN_WEIGHTS.values()) == pytest.approx(
            100.0)

    def test_no_sub_type_at_all_is_not_in_the_distribution_on_either_side(
            self, surge_source, model):
        """Every dungeon a surge makes has a sub-type.

        THE C++ CONSTANT IS GONE RATHER THAN SET TO ZERO. A weight of zero sits
        in the list beside the real ones and reads as an outcome that can still
        come up; the absence of the constant is what says it cannot. So this
        asserts the declaration is absent, not that it holds zero.
        """
        assert "None" not in model.SUBTYPE_SPAWN_WEIGHTS, (
            "the model's spawn distribution has a no-sub-type entry again; "
            "the owner ruled on 2026-09-05 that every dungeon has a sub-type")

        assert "SpawnWeightNone" not in surge_source, (
            f"{SURGE_HEADER.name} declares a SpawnWeightNone again. Removing "
            "the outcome means removing the constant, not setting it to zero")

    def test_cow_level_is_the_rarest_and_the_two_commonest_are_level(
            self, surge_source, model):
        """The shape of the distribution, not its individual numbers.

        Every weight being wrong in the same direction would pass the per-value
        check above only if both copies moved together, which is the one thing
        that check cannot see. This states the design intent instead: a Cow
        Level is the rarest thing a surge produces, because the design document
        gives it "ridiculous amounts of loot".

        **THE SIEGE WAS AN EXCEPTION FOR ONE DAY AND IS NOT ONE NOW.** Halving
        the Siege weight to 7.5 on issue #1349 handed its slack to all six of
        the others in proportion, which carried the Cow Level to 7.617647 --
        above the Siege, so the Siege briefly became the rarest sub-type in the
        game. That fell out of the arithmetic rather than being chosen, and this
        assertion was re-scoped to the six the ruling did not touch while the
        question was open. On issue #1369 the owner delegated the answer with
        one constraint, verbatim: "Your call, but the cow level should be pretty
        rare". The Cow Level went back to the 7 that #1293 decided, the Siege's
        7.5 was left exactly where the owner set it, and so THE ASSERTION IS
        BACK AT FULL STRENGTH: the Cow Level against every other sub-type.
        """
        unreal = {cpp: weight(surge_source, cpp) for cpp in NAMES.values()}

        assert unreal["CowLevel"] == min(unreal.values()), (
            f"Cow Level is no longer the rarest: {unreal}")

        assert unreal["Timed"] == unreal["Horde"] == max(unreal.values()), (
            "Timed and Horde are no longer the joint commonest sub-types")

        assert (model.SUBTYPE_SPAWN_WEIGHTS["Cow Level"]
                == min(model.SUBTYPE_SPAWN_WEIGHTS.values()))

        # AND STRICTLY RARER, WITH NOTHING LEVEL WITH IT. `min` is satisfied by
        # a tie, so on its own it would pass for a table where the Siege had
        # been brought down to meet the Cow Level -- which is the shape #1369
        # said was "probably not what anyone intended", a tenth of a point being
        # no designed margin. This is the assertion that was re-scoped rather
        # than deleted while that was open; it is stated the strong way now.
        at_or_below = [name for name, value
                       in model.SUBTYPE_SPAWN_WEIGHTS.items()
                       if name != "Cow Level"
                       and value <= model.SUBTYPE_SPAWN_WEIGHTS["Cow Level"]]
        assert at_or_below == [], (
            f"{at_or_below} are as rare as a Cow Level or rarer. A Cow Level is "
            "the rarest thing a surge produces and the margin is a design "
            "decision, not a rounding artefact; see issue #1369")


class TestTheEnumTheRollWalks:
    def test_the_enum_names_every_sub_type_the_port_covers_and_no_sub_type(
            self, kind_source):
        """`RollSubType` walks `ECataclysmDungeonSubType` from `Timed` to
        `Sacrificial` and asks `SpawnWeightFor` for each. A sub-type added to
        the enum without a weight would never be rolled; one removed would make
        the port silently narrower.

        `None` IS IN THE ENUM AND NOT IN THE DISTRIBUTION, and that is the whole
        arrangement this checks. It is what a dungeon entered outside the empire
        carries, and what a Siege refused by the one-per-city cap becomes, so
        deleting the value would break both.
        """
        block = re.search(
            r"enum\s+class\s+ECataclysmDungeonSubType\s*:\s*uint8\s*\{(.*?)\}",
            kind_source, re.DOTALL)

        assert block, ("could not find ECataclysmDungeonSubType in "
                       f"{KIND_HEADER.name}; has it been renamed?")

        found = dict(re.findall(r"(\w+)\s*=\s*(\d+)", block.group(1)))

        assert set(found) == set(NAMES.values()) | {"None"}, (
            f"the enum names {sorted(found)}, the port covers "
            f"{sorted(NAMES.values())} and expects a None beside them")

    def test_the_roll_starts_past_no_sub_type(self, surge_source):
        """`RollSubType` must not begin its walk at zero.

        Beginning at zero and relying on `SpawnWeightFor` returning zero for
        `None` would give the same distribution and would read as though no
        sub-type were still one of the options. The source says which value it
        starts from, and this is what keeps that true.
        """
        source = (SURGE_HEADER.parent / "CataclysmSurge.cpp").read_text(
            encoding="utf-8")

        assert "ECataclysmDungeonSubType::Timed);" in source, (
            "UCataclysmSurgeScheduler::RollSubType no longer names Timed as "
            "the first sub-type it walks")

        assert "for (uint8 Value = 0;" not in source, (
            "UCataclysmSurgeScheduler::RollSubType walks from zero again, "
            "which puts no-sub-type back among the options it considers")

    def test_sacrificial_is_still_the_last_one(self, kind_source):
        """`RollSubType` stops at `Sacrificial` by name. A sub-type added after
        it would be walked; one added before it and numbered higher would not,
        and would silently never spawn."""
        block = re.search(
            r"enum\s+class\s+ECataclysmDungeonSubType\s*:\s*uint8\s*\{(.*?)\}",
            kind_source, re.DOTALL)

        found = {name: int(value)
                 for name, value in re.findall(r"(\w+)\s*=\s*(\d+)",
                                               block.group(1))}

        assert found["Sacrificial"] == max(found.values()), (
            "Sacrificial is no longer the highest-numbered sub-type, so "
            "UCataclysmSurgeScheduler::RollSubType stops before the end of the "
            "enum and whatever is above it can never be rolled")


class TestWhatACowLevelCosts:
    def test_the_walk_is_doubled_on_both_sides(self, surge_source):
        """The design document: "Time to complete is doubled and cannot be
        reduced." The model writes the doubling out as a literal in
        `Simulation._make_dungeon`; Unreal has it as a named constant."""
        unreal = float(constant(surge_source, "CowLevelWalkMultiplier",
                                r"[0-9.]+f?", SURGE_HEADER.name).rstrip("f"))

        assert unreal == pytest.approx(2.0), (
            f"Unreal doubles a Cow Level's walk by {unreal}, not 2")

        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py").read_text(
            encoding="utf-8")

        assert "cfg.days_per_floor)) * 2" in source, (
            "sim/cataclysm_sim/engine.py no longer doubles a Cow Level's run "
            "days; UCataclysmSurgeScheduler::CowLevelWalkMultiplier still does")

    def test_the_model_ignores_the_reduction_for_a_cow_level_too(self):
        """Not the multiplier but the other half of the rule. The model's
        `run_days_for` is where the empire tree's reduction is applied, and the
        Cow Level branch does not call it -- which is what "cannot be reduced"
        means. Unreal skips the city upgrade in the same place and for the same
        reason.

        IT READS `Simulation._walk_days`, WHICH USED TO BE WRITTEN OUT INLINE.
        Issue #1315 moved it into a named helper because the earned Cataclysm
        dungeon changes its own depth after it is built and has to work the days
        out again; the rule did not change, only where it lives.
        """
        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py").read_text(
            encoding="utf-8")

        helper = re.search(
            r"def _walk_days\(self, floors: int, subtype: str\) -> int:"
            r"(.*?)(?=\n    def )", source, re.DOTALL)

        assert helper, ("sim/cataclysm_sim/engine.py no longer has "
                        "Simulation._walk_days, which is where a Cow Level's "
                        "walk is chosen separately from run_days_for; the port "
                        "in UCataclysmSurgeScheduler::MakeDungeon still makes "
                        "that choice")

        body = helper.group(1)
        branch = re.search(r"if subtype == [\"']Cow Level[\"']:(.*?)\n        "
                           r"return", body, re.DOTALL)

        assert branch, ("Simulation._walk_days no longer treats a Cow Level "
                        "differently from anything else")

        assert "run_days_for" not in branch.group(1), (
            "the model now runs a Cow Level's walk through run_days_for, which "
            "applies the tree's reduction; Unreal still skips the city upgrade")

        assert "self._walk_days(floors, subtype)" in source, (
            "Simulation._make_dungeon no longer routes through _walk_days, so "
            "an ordinary Cow Level may not be getting the rule at all")


class TestWhichSubTypesAKindOfDungeonMayNotRoll:
    """**ONE PAIR OF THE 28 IS ILLEGAL AND BOTH SIDES HAVE TO AGREE WHICH.**

    The project owner ruled on 2026-09-06, verbatim: *"Last stand is a cataclysm
    dungeon and should not be allowed to roll as a cow level sub type."* Asked
    how far to take it they answered *"Only the one you ruled"*. Issue #1333.

    THE MODEL IS THE ORIGINAL AND THE GAME IS THE COPY, the same direction as
    the weights above. `config.SUBTYPES_FORBIDDEN_ON` holds the rule;
    `UCataclysmSurgeScheduler::CataclysmForbiddenSubType` and `BarredSubTypeOn`
    hold it in the game.

    WHAT THIS CANNOT SEE, and why the Unreal test exists beside it. A constant
    both sides agree on says nothing about whether either side reads it.
    `Cataclysm.Surge.ACataclysmDungeonNeverRollsACowLevel` rolls the C++
    implementation many times and checks what comes out;
    `sim/tests/test_cataclysm_cannot_be_a_cow_level.py` does the same for the
    model. All three are needed.
    """

    def forbidden_in_the_game(self, surge_source) -> dict[str, tuple[str, ...]]:
        """The game's rule, as dungeon-type name against sub-type names."""
        barred = constant(surge_source, "CataclysmForbiddenSubType",
                          r"ECataclysmDungeonSubType::\w+", SURGE_HEADER.name)
        return {"Cataclysm": (barred.split("::")[1],)}

    def test_the_game_bars_the_same_pair_the_model_does(self, surge_source,
                                                        model):
        game = self.forbidden_in_the_game(surge_source)

        wanted = {dtype.name.title().replace("_", ""):
                  tuple(NAMES[n] for n in names)
                  for dtype, names in model.SUBTYPES_FORBIDDEN_ON.items()}

        assert game == wanted, (
            f"the game bars {game} and the model bars {wanted}; "
            "config.SUBTYPES_FORBIDDEN_ON is the original and "
            "UCataclysmSurgeScheduler::CataclysmForbiddenSubType is the copy")

    def test_the_model_bars_exactly_one_pair(self, model):
        """"Only the one you ruled". Twenty-seven pairs stay legal, and a
        session adding a second row is making a design decision the owner has
        not made."""
        pairs = [(dtype, name)
                 for dtype, names in model.SUBTYPES_FORBIDDEN_ON.items()
                 for name in names]

        assert len(pairs) == 1, (
            f"the model now bars {pairs}. The ruling of 2026-09-06 named one "
            "pair and explicitly declined to name any others")

        dtype, name = pairs[0]
        assert dtype.name == "CATACLYSM"
        assert name == "Cow Level"

    def test_the_game_constrains_only_the_cataclysm(self):
        """`BarredSubTypeOn` answers `None` for the other three kinds.

        READ OUT OF THE SWITCH ITSELF, because the constant above says which
        sub-type is barred and not which dungeon it is barred on. A second
        `case` here would be the same unmade design decision as a second row in
        the model.
        """
        source = (SURGE_HEADER.parent / "CataclysmSurge.cpp").read_text(
            encoding="utf-8")

        body = re.search(
            r"ECataclysmDungeonSubType UCataclysmSurgeScheduler::"
            r"BarredSubTypeOn\((.*?)\n\}\n", source, re.DOTALL)

        assert body, ("UCataclysmSurgeScheduler::BarredSubTypeOn is gone from "
                      "CataclysmSurge.cpp, so nothing in the game applies the "
                      "ruling of 2026-09-06")

        cases = re.findall(r"case\s+ECataclysmDungeonType::(\w+)\s*:",
                           body.group(1))

        assert cases == ["Cataclysm"], (
            f"BarredSubTypeOn now names {cases}. The owner barred one pair and "
            "said nothing about any other dungeon type")

        assert "default:" in body.group(1), (
            "BarredSubTypeOn no longer answers for the kinds it does not name, "
            "so a dungeon type added to the enum would fall through")

    def test_the_roll_actually_applies_the_bar(self):
        """The wiring, which the constant cannot show.

        `RollSubType` has to hand the barred sub-type to BOTH `TotalSpawnWeight`
        and `SubTypeAtPoint`. Passing it to only the first would shorten the
        line the draw is scaled against while still walking the full one, which
        skews every share instead of removing one option -- and would not
        change what the constant says.
        """
        source = (SURGE_HEADER.parent / "CataclysmSurge.cpp").read_text(
            encoding="utf-8")

        roll = re.search(
            r"ECataclysmDungeonSubType UCataclysmSurgeScheduler::RollSubType"
            r"\((.*?)\n\}\n", source, re.DOTALL)

        assert roll, "UCataclysmSurgeScheduler::RollSubType is gone"

        body = roll.group(1)

        assert "BarredSubTypeOn(Type)" in body, (
            "RollSubType no longer asks what this kind of dungeon may not roll")
        assert "TotalSpawnWeight(Barred)" in body, (
            "RollSubType scales its draw against the full line again, so the "
            "barred sub-type's weight is dropped rather than redistributed")
        assert "SubTypeAtPoint(Point, Barred)" in body, (
            "RollSubType walks the full line again, so the barred sub-type can "
            "still be chosen")

    def test_the_model_lets_the_last_stand_recompute_with_the_sub_type(self):
        """The defect the ruling removed, held closed on the model's side.

        Issue #1333: `_open_last_stand` recomputed the walk after adding floor
        bonuses using a bare `run_days_for`, which knows nothing about
        sub-types, so a Cow Level Last Stand lost its doubling. The ruling makes
        that unreachable; routing the call through `_walk_days` makes it wrong
        even if it ever became reachable again.
        """
        source = (REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py").read_text(
            encoding="utf-8")

        stand = re.search(r"def _open_last_stand\(self\) -> None:"
                          r"(.*?)(?=\n    def )", source, re.DOTALL)

        assert stand, "Simulation._open_last_stand is gone"

        assert "d.run_days = self._walk_days(d.floors, d.subtype)" in \
            stand.group(1), (
                "Simulation._open_last_stand works its walk out without the "
                "sub-type again, which is the shape issue #1333 was about")


def spawn_table_in(document: str) -> dict[str, float]:
    """The design's spawn table, as names against numbers.

    Same shape of reader as `test_enemy_score_formula.table_after`, kept here
    rather than imported because the two files guard different documents' tables
    and one changing its parser should not silently change the other's.
    """
    start = document.find(SPAWN_HEADING)
    assert start != -1, (
        f"{GDD.name} has no heading beginning {SPAWN_HEADING!r}. The spawn "
        "distribution has to be stated in the design: that is issue #1293")

    after = document[start + len(SPAWN_HEADING):]
    stop = re.search(r"^#{1,6} ", after, re.MULTILINE)
    body = after[:stop.start()] if stop else after

    out: dict[str, float] = {}
    for line in body.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 2:
            continue
        name, value = cells[0], cells[1]
        if not name or not re.fullmatch(r"-?\d+(?:\.\d+)?", value):
            continue
        out[name] = float(value)
    return out


@pytest.fixture(scope="module")
def design() -> str:
    return read(GDD)


class TestTheDesignStatesTheSameDistribution:
    """The numbers have a design source, and it is the same one the code uses.

    ISSUE #1293 WAS THAT THEY HAD NO SOURCE AT ALL. The distribution lived only
    in `sim/cataclysm_sim/config.py` and its port, so nobody who owns the design
    could review it and nothing could be checked against it. Writing it into the
    document fixes that only until the two drift, which is what these check.
    """

    def test_the_design_states_every_sub_type_the_model_rolls(
            self, design, model):
        stated = spawn_table_in(design)

        assert set(stated) == set(model.SUBTYPE_SPAWN_WEIGHTS), (
            f"{GDD.name} states {sorted(stated)}, the model rolls "
            f"{sorted(model.SUBTYPE_SPAWN_WEIGHTS)}")

    def test_the_design_states_the_same_numbers_as_the_model(
            self, design, model):
        stated = spawn_table_in(design)

        for name, wanted in model.SUBTYPE_SPAWN_WEIGHTS.items():
            assert stated.get(name) == pytest.approx(wanted), (
                f"{name}: {GDD.name} says {stated.get(name)}, the model says "
                f"{wanted}")

    def test_the_design_does_not_state_a_no_sub_type_spawn_weight(self, design):
        """The design's OTHER subtype table still has a None row, and should.

        That one is the difficulty weight, and a dungeon entered outside the
        empire genuinely has no subtype and genuinely adds no difficulty. A None
        row appearing in the spawn table would mean the two had been merged or
        the wrong one edited.
        """
        stated = spawn_table_in(design)

        assert "None" not in stated, (
            f"{GDD.name}'s spawn table has a None row. Every dungeon a surge "
            "creates has a sub-type; the None row belongs to the difficulty "
            "table above it, which is a different quantity")
