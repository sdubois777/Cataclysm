"""The game's rule for a kill's experience and loot by the Power Score gap is the simulation's, and a kill pays it.

WHY THIS EXISTS. Issue #2824, slice c: the coordinating session's rule D and the owner's 25% loot floor, both of
2026-11-22; and issue #2853, the owner's per-tier start of the cut, 2026-11-24. `FCataclysmPowerGapRule` (`game/Source/Cataclysm/Dungeon/CataclysmPowerGapRewards.h`) is a port of
`sim/analyse_power_gap_rewards.py`'s `RULED.multiplier`. Continuous integration compiles no C++, so the automation
tests `Cataclysm.PowerGapRewards.` never run on a pull request; they hold the simulation's figures written out, and
this check holds those figures, and the port's constants, to the simulation. Two copies of a number are two numbers.

WHAT IS CHECKED:

  1. each of the port's six constants is the ruled rule's field of the same meaning, and its table of where the cut
     begins at each tier is the ruled rule's `band_down_by_tier`;
  2. each row of `TheRuleIsTheSimulationsAtElevenGaps`'s table is `RULED.multiplier` at that gap and tier, experience
     and loot, and the rows reach both floors, the cap, both ramps and the band; and each row of
     `TheCutBeginsAtOnePointFourWidthsAtTierOneAndNoughtPointEightAtTierEight`'s table, likewise;
  3. each row of `TwoScoresGiveBothFactorsAndTheLineAndNoScoreChangesNothing`'s table is the line the simulation's
     factors make for those two scores, at the simulation's tier width;
  4. the death handler asks for the factor before it pays, multiplies the loot quantity and the gold multiplier by the
     loot factor, and passes both experience grants through the experience factor after the city's;
  5. the floor panel, the empire map's city detail and the city screen show the rewards after the Dungeon Score, which
     no automation test can see on a screen (-nullrhi, #650).
"""

from __future__ import annotations

import contextlib
import io
import math
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADER = SOURCE / "Dungeon" / "CataclysmPowerGapRewards.h"
RULE_CPP = SOURCE / "Dungeon" / "CataclysmPowerGapRewards.cpp"
TEST = SOURCE / "Tests" / "CataclysmPowerGapRewardsTests.cpp"
ENEMY = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"
MODE = SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp"
LAYOUT = SOURCE / "Interface" / "CataclysmEmpireMapLayout.cpp"

if str(REPO_ROOT / "sim") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import scoring  # noqa: E402

# THE SCRIPT PRINTS ITS WHOLE REPORT ON IMPORT, as every analysis script does; it is read for its rule, not its report.
with contextlib.redirect_stdout(io.StringIO()):
    import analyse_power_gap_rewards as gaps  # noqa: E402

RULED = gaps.RULED

#: Each C++ constant and the simulation's field of the same meaning. Where the cut begins is a table by tier since
#: issue #2853, `BandBelow`'s `ByTier`, checked on its own.
CONSTANTS = {"BandAbove": "band", "BonusCap": "cap_up", "BonusRamp": "ramp_up",
             "CutRamp": "ramp_down", "ExperienceFloor": "floor", "LootFloor": "loot_floor"}


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return flat(text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


def table(test_name: str) -> str:
    """The `Cases[]` table of the automation test whose `RunTest` belongs to `test_name`'s class."""
    run = body(code(TEST), f"bool {test_name}::RunTest(")
    start = run.index("const FCase Cases[] = {")
    return run[start:run.index("};", start)]


def half_up_percent(value: float) -> int:
    """`FMath::RoundToInt32(value * 100.0)`: half up, which Python's `round` is not."""
    return math.floor(value * 100.0 + 0.5)


def test_the_ports_constants_are_the_ruled_rules() -> None:
    found = dict(re.findall(r"static constexpr double (\w+) = ([\d.]+);", code(HEADER)))
    assert set(found) == set(CONSTANTS), f"the port's constants are {sorted(found)}, not {sorted(CONSTANTS)}"
    for name, field in CONSTANTS.items():
        assert float(found[name]) == getattr(RULED, field), (
            f"{name} is {found[name]} in the game and {field} is {getattr(RULED, field)} in the ruled rule")


def test_where_the_cut_begins_at_each_tier_is_the_ruled_rules() -> None:
    below = body(code(RULE_CPP), "double FCataclysmPowerGapRule::BandBelow(")
    found = re.search(r"static constexpr double ByTier\[\] = \{([^}]*)\};", below)
    assert found, "BandBelow holds no ByTier table"
    table = tuple(float(x) for x in found.group(1).split(","))
    assert table == RULED.band_down_by_tier == (1.4, 1.3, 1.1, 1.0, 0.9, 0.9, 0.8, 0.8), (
        f"the game's cut begins at {table} and the simulation's at {RULED.band_down_by_tier}")
    assert "FMath::Clamp(DifficultyTier, 1, static_cast<int32>(UE_ARRAY_COUNT(ByTier)))" in below, (
        "BandBelow no longer holds the tier inside the table")
    assert "return ByTier[Tier - 1];" in below
    assert "BandBelow = " not in code(HEADER), "the header still holds one start for every tier"


def test_the_multiplier_is_written_as_the_simulation_writes_it() -> None:
    multiplier = body(code(RULE_CPP), "double FCataclysmPowerGapRule::Multiplier(")
    assert "const double Below = BandBelow(DifficultyTier);" in multiplier, (
        "the multiplier does not read where the cut begins at the dungeon's tier")
    above = multiplier.find("if (Gap > BandAbove)")
    below = multiplier.find("if (Gap < -Below)")
    assert -1 < above < below, "the port no longer asks above the band first and below it second, as the simulation does"
    assert "return 1.0 + BonusCap * FMath::Min(1.0, (Gap - BandAbove) / BonusRamp);" in multiplier
    assert "return 1.0 - (1.0 - Floor) * FMath::Min(1.0, (-Gap - Below) / CutRamp);" in multiplier
    assert "const double Floor = bLoot ? LootFloor : ExperienceFloor;" in multiplier


def tier_rows(test_name: str) -> list[tuple[int, float, float, float]]:
    """The (tier, gap, experience, loot) rows of an automation test's `Cases[]` table."""
    return [(int(tier), float(gap), float(experience), float(loot)) for tier, gap, experience, loot in
            re.findall(r"\{\s*(\d+),\s*(-?[\d.]+),\s*([\d.]+),\s*([\d.]+)\s*\}", table(test_name))]


def assert_rows_are_the_simulations(rows: list[tuple[int, float, float, float]]) -> None:
    for tier, gap, experience, loot in rows:
        assert math.isclose(experience, RULED.multiplier(gap, tier=tier), abs_tol=1e-12), (
            f"at {gap}, tier {tier}: the test says experience {experience}, the simulation "
            f"{RULED.multiplier(gap, tier=tier)}")
        assert math.isclose(loot, RULED.multiplier(gap, loot=True, tier=tier), abs_tol=1e-12), (
            f"at {gap}, tier {tier}: the test says loot {loot}, the simulation "
            f"{RULED.multiplier(gap, loot=True, tier=tier)}")


def test_the_automation_tests_eleven_gaps_are_the_simulations() -> None:
    rows = tier_rows("FCataclysmPowerGapRewardsRuleTest")
    assert len(rows) == 11, f"the table holds {len(rows)} rows, not 11"
    assert_rows_are_the_simulations(rows)
    tiers = {row[0] for row in rows}
    assert len(tiers) == 1, f"the eleven gaps are at tiers {sorted(tiers)}, not one"
    tier = tiers.pop()
    experiences = {row[2] for row in rows}
    assert RULED.floor in experiences and RULED.loot_floor in {row[3] for row in rows}, "the table misses a floor"
    assert 1.0 + RULED.cap_up in experiences, "the table misses the cap"
    assert any(RULED.floor < e < 1.0 for e in experiences), "the table misses the cut's ramp"
    assert any(1.0 < e < 1.0 + RULED.cap_up for e in experiences), "the table misses the bonus's ramp"
    assert {-RULED.below_at(tier), RULED.band} <= {row[1] for row in rows}, "the table misses an edge of the band"


def test_the_automation_tests_per_tier_starts_are_the_simulations() -> None:
    """`TheCutBeginsAtOnePointFourWidthsAtTierOneAndNoughtPointEightAtTierEight`: its seven rows, and its table of the
    eight starts, against the ruled rule. Each of tier 1's and tier 8's starts is in the table, paid in full, with a gap
    a tenth beyond it that is cut."""
    rows = tier_rows("FCataclysmPowerGapRewardsStartTest")
    assert len(rows) == 7, f"the table holds {len(rows)} rows, not 7"
    assert_rows_are_the_simulations(rows)
    for tier in (1, 8):
        start = -RULED.below_at(tier)
        assert (tier, start, 1.0, 1.0) in rows, f"the table does not pay tier {tier}'s start, {start}, in full"
        assert any(t == tier and math.isclose(gap, start - 0.1) and e < 1.0 for t, gap, e, _ in rows), (
            f"the table does not cut a tenth beyond tier {tier}'s start")
    run = body(code(TEST), "bool FCataclysmPowerGapRewardsStartTest::RunTest(")
    found = re.search(r"const double Starts\[\] = \{([^}]*)\};", run)
    assert found and tuple(float(x) for x in found.group(1).split(",")) == RULED.band_down_by_tier, (
        "the test's eight starts are not the ruled rule's")


def test_the_automation_tests_lines_are_the_simulations() -> None:
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*TEXT\(\"([^\"]*)\"\)\s*\}",
                      table("FCataclysmPowerGapRewardsScoresTest"))
    assert len(rows) == 4, f"the table holds {len(rows)} rows, not 4"
    for dungeon, player, tier, text in rows:
        gap = (int(dungeon) - int(player)) / scoring.tier_width(int(tier))
        experience = half_up_percent(RULED.multiplier(gap, tier=int(tier)))
        loot = half_up_percent(RULED.multiplier(gap, loot=True, tier=int(tier)))
        expected = (f"Rewards {experience}%." if experience == loot
                    else f"Rewards: experience {experience}%, loot {loot}%.")
        assert text == expected, f"{dungeon} against {player} at tier {tier}: the test says {text!r}, the simulation {expected!r}"


def test_a_kill_asks_for_the_factor_before_it_pays_and_applies_it_to_each_reward() -> None:
    handler = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()")
    asked = handler.find("const FCataclysmPowerGapRewards GapRewards = ACataclysmDungeonGameMode::PowerGapRewardsIn(World);")
    city_loot = handler.find("LootQuantity *= CityRewards.LootMore;")
    scaled = handler.find("const FCataclysmPowerGapKillLoot KillLoot = FCataclysmPowerGapRule::KillLootFor( LootQuantity, "
                          "TreeDrops.GoldMultiplier * CityRewards.GoldMore, GapRewards);")
    taken = handler.find("LootQuantity = KillLoot.LootQuantity;")
    paying = handler.find("if (bPays)")
    assert -1 < city_loot < asked < scaled < taken < paying, (
        "the kill does not scale its loot quantity and gold multiplier through KillLootFor, after the city's and the "
        "tree's, before it pays")

    gold = re.search(r"UCataclysmDropSpawner::SpawnGoldFor\(([^;]*)\);", handler)
    last = gold.group(1).rsplit(",", 1)[-1] if gold else ""
    assert last.strip() == "KillLoot.GoldMultiplier", (
        f"the kill's gold multiplier is not KillLootFor's: {last.strip()!r}")
    kill_loot = body(code(RULE_CPP), "FCataclysmPowerGapKillLoot FCataclysmPowerGapRule::KillLootFor(")
    for field, given in (("LootQuantity", "LootQuantity"), ("GoldMultiplier", "GoldMultiplier")):
        assert f"Out.{field} = {given} * static_cast<float>(Rewards.Loot);" in kill_loot, (
            f"KillLootFor does not multiply the {field} by the loot factor")
    for grant in ("State->GrantExperience(", "State->GrantSkillExperience("):
        at = handler.find(grant)
        assert at != -1, f"the kill no longer calls {grant}"
        call = handler[at:handler.index(";", at)]
        assert "State->ExperienceAfterGain(GapRewards.ExperienceFrom(CityRewards.ExperienceFrom(" in call, (
            f"{grant} does not take the gap's factor after the city's figure and before the player's own gain")


def test_the_factor_and_the_panel_read_the_same_two_scores() -> None:
    mode = code(MODE)
    here = body(mode, "FCataclysmPowerGapRewards ACataclysmDungeonGameMode::PowerGapRewardsHere(")
    assert "GapScoresHere(" in here and "FCataclysmPowerGapRule::RewardsFor(" in here
    line = body(mode, "FString ACataclysmDungeonGameMode::FloorScoreLineHere(")
    assert "GapScoresHere(" in line
    assert "FCataclysmPowerGapRule::RewardsText(FCataclysmPowerGapRule::RewardsFor(" in line
    within = body(mode, "FCataclysmPowerGapRewards ACataclysmDungeonGameMode::PowerGapRewardsIn(")
    assert "Mode->PowerGapRewardsHere()" in within
    rewards = body(code(RULE_CPP), "FCataclysmPowerGapRewards FCataclysmPowerGapRule::RewardsFor(")
    for loot in ("false", "true"):
        assert f"Multiplier(Gap, {loot}, DifficultyTier);" in rewards, (
            "RewardsFor does not ask the multiplier at the dungeon's tier")


def test_the_map_and_the_city_screen_show_the_rewards_after_the_dungeon_score() -> None:
    line = body(code(LAYOUT), "FString UCataclysmEmpireMapLayout::DungeonScoreAndRewardsLine(")
    assert line.find("DungeonScoreLine(Run, DungeonId, PlayerScore)") != -1, "the line does not begin as DungeonScoreLine"
    assert "FCataclysmPowerGapRule::RewardsText(FCataclysmPowerGapRule::RewardsFor(" in line
    assert "Run->DifficultyTier" in line, "the rewards are not worked out at the run's difficulty tier"
    for widget in ("CataclysmEmpireMapWidget.cpp", "CataclysmCityScreenWidget.cpp"):
        text = flat(code(SOURCE / "Interface" / widget))
        assert "UCataclysmEmpireMapLayout::DungeonScoreAndRewardsLine(Run, " in text, widget


def test_the_kill_tests_creatures_are_made_whatever_the_floor_holds() -> None:
    """Window forge-pg-w1: inside an entered empire dungeon the default collision handling refused both creatures of
    `AKillInAnEmpireDungeonGrantsTheFactorsExperienceAndThePanelShowsIt` ("SpawnActor failed because of collision at the
    spawn location"), so no kill happened and the test read -1. The helper that makes them must always spawn, and the
    test must say so as a set-up failure when it cannot."""
    test = code(TEST)
    helper = body(test, "int64 KillAndMeasure(")
    assert "SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;" in helper, (
        "KillAndMeasure no longer spawns its creature past whatever the generated floor holds")
    assert "SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator, Params)" in helper, (
        "KillAndMeasure does not pass its spawn parameters")
    run = body(test, "bool FCataclysmPowerGapRewardsKillTest::RunTest(")
    for made in ("Gained != INDEX_NONE", "GainedOutside != INDEX_NONE"):
        assert made in run, f"the kill test does not report a creature it could not make: {made}"
