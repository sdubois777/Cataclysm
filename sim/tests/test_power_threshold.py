"""What player power the Cataclysm dungeon actually asks for.

WHY THIS EXISTS. Issue #8. The report printed by `sim/experiments.py` told every
reader to compare the power column against "~320-420". That range was worked out
against the player power anchors issue #2 replaced, and was never re-derived, so
the figure a reader measured every result against was unverified.

WHAT THE ANSWER TURNED OUT TO BE. There is no single number. Death chance falls
smoothly with power and never reaches zero at any power a tier allows, because
the thing on the last floor of a Cataclysm dungeon outscores the maximum player
power of its own tier -- 2.0 times at tier 1, falling to 1.2 times at tier 8. So
the question only has an answer once a death chance is named, and the figure is
computed from the model rather than written down.
"""

from __future__ import annotations

import re

import pytest

from cataclysm_sim import combat, scoring
from cataclysm_sim.config import CityTier, DungeonType, TuningConfig

#: The dungeon the report's threshold is about, described the way the engine
#: describes it rather than by numbers repeated here.
DTYPE, SUBTYPE, BOSS = "Cataclysm", "None", "Cataclysm Boss"


@pytest.fixture(scope="module")
def cfg() -> TuningConfig:
    return TuningConfig()


@pytest.fixture(scope="module")
def floors(cfg) -> int:
    spec = cfg.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)]
    return round(sum(spec.floors) / 2)


def risk(power: float, tier: int, cfg: TuningConfig, floors: int) -> float:
    return combat.death_chance(
        power, tier, DTYPE, SUBTYPE, floors, 0.0, cfg.per_floor_risk,
        cfg.boss_risk_multiplier, BOSS, cfg.overwhelm_rate, cfg.overwhelm_cap)


def needed(target: float, tier: int, cfg: TuningConfig, floors: int) -> float:
    return combat.power_for_death_chance(
        target, tier, DTYPE, SUBTYPE, floors, 0.0, cfg.per_floor_risk,
        cfg.boss_risk_multiplier, BOSS, cfg.overwhelm_rate, cfg.overwhelm_cap)


# --------------------------------------------------------------------------
# The solver
# --------------------------------------------------------------------------

def test_the_power_it_returns_produces_the_death_chance_asked_for(cfg, floors):
    """The whole point of the function. Checked at every tier, because the
    dungeon and the tier width both change with it."""
    for tier in range(1, 9):
        power = needed(0.10, tier, cfg, floors)
        assert risk(power, tier, cfg, floors) == pytest.approx(0.10, abs=0.005)


def test_asking_for_a_lower_death_chance_needs_more_power(cfg, floors):
    powers = [needed(t, 4, cfg, floors) for t in (0.30, 0.20, 0.10, 0.05)]
    for looser, tighter in zip(powers, powers[1:], strict=False):
        assert tighter > looser, powers


def test_it_looks_above_the_tier_ceiling_rather_than_clamping_to_it(cfg, floors):
    """The answer is above the tier's own maximum at every tier. A solver that
    searched only inside the tier range would return the ceiling and report a
    death chance the player cannot actually reach."""
    for tier in range(1, 9):
        ceiling = scoring.tier_bounds(tier)[1]
        assert needed(0.10, tier, cfg, floors) > ceiling, tier


def test_a_dungeon_already_safe_enough_needs_no_power_at_all(cfg, floors):
    """A 90% acceptable death chance is met by a player with nothing, so the
    answer is zero rather than a bisection artefact."""
    assert needed(0.90, 1, cfg, floors) == 0.0


def test_an_impossible_target_is_rejected_rather_than_looping(cfg, floors):
    for target in (0.0, 1.0, -0.1, 1.5):
        with pytest.raises(ValueError):
            needed(target, 1, cfg, floors)


# --------------------------------------------------------------------------
# What the model actually says, which is the answer to the issue
# --------------------------------------------------------------------------

def test_more_power_never_makes_a_dungeon_more_dangerous(cfg, floors):
    """The property bisection relies on. If death chance ever rose with power
    the solver would return a wrong answer with no sign of it."""
    for tier in (1, 4, 8):
        ceiling = scoring.tier_bounds(tier)[1]
        chances = [risk(ceiling * f, tier, cfg, floors)
                   for f in (0.0, 0.25, 0.5, 0.75, 1.0, 1.5)]
        for higher_power, lower_power in zip(chances[1:], chances,
                                             strict=False):
            assert higher_power <= lower_power, (tier, chances)


def test_the_last_floor_outscores_the_maximum_player_power_at_every_tier(
        cfg, floors):
    """The finding that makes "what power clears it" unanswerable without a
    death chance. If this ever stops being true the report's wording, and the
    reason `power_for_death_chance` searches above the ceiling, both change."""
    for tier in range(1, 9):
        ceiling = scoring.tier_bounds(tier)[1]
        boss = scoring.enemy_scores(tier, DTYPE, SUBTYPE, floors, floors,
                                    0.0)[BOSS]
        assert boss > ceiling, (
            f"tier {tier}: the Cataclysm Boss scores {boss:,.0f} against a "
            f"player ceiling of {ceiling:,.0f}")


def test_a_player_at_the_tier_ceiling_still_dies_sometimes(cfg, floors):
    """Between one run in six and one in five, at every tier. Stated as a band
    rather than a number because it is a measurement of the current model, not a
    design target. If it moves outside this band something changed on purpose
    and the report's wording should be re-read."""
    for tier in range(1, 9):
        ceiling = scoring.tier_bounds(tier)[1]
        chance = risk(ceiling, tier, cfg, floors)
        assert 0.10 < chance < 0.30, f"tier {tier}: {chance:.1%}"


def test_the_risk_at_the_ceiling_is_about_the_same_at_every_tier(cfg, floors):
    """Overwhelm is rated against tier width precisely so it behaves the same at
    every tier; `combat.py` says so in its opening. This is that claim measured
    rather than assumed."""
    chances = [risk(scoring.tier_bounds(t)[1], t, cfg, floors)
               for t in range(1, 9)]
    assert max(chances) - min(chances) < 0.10, chances


# --------------------------------------------------------------------------
# The report line itself
# --------------------------------------------------------------------------

def test_the_report_key_states_the_computed_figures(cfg):
    """The line a reader compares every power figure against has to come from
    the model. It was a hard-coded range for months after the model moved."""
    import experiments

    lines = experiments.cataclysm_power_key(cfg)
    text = " ".join(lines)
    assert f"tier {cfg.tier}" in text
    assert f"{combat.REPORTED_DEATH_CHANCE:.0%}" in text


def test_the_report_itself_prints_the_computed_key_and_no_typed_range():
    """The header the report prints must hold the COMPUTED key, not a typed one.

    Testing `cataclysm_power_key` alone is not enough, and this test exists
    because that gap was found: replacing the call in the report with a
    hard-coded print left every test on the helper passing while the report went
    back to a stale number.

    THIS USED TO BE A TEXT CHECK on `main()`'s source, because running `main()`
    is 25,000 simulated campaigns and about eighteen minutes. Issue #281 moved
    the header into `experiments.header_lines()`, which is cheap to call, so the
    check is now behavioural: the lines the report prints are compared against
    the lines the helper produces. That is stronger -- a hard-coded print would
    have to reproduce every computed figure exactly to get past it.
    """
    import pathlib
    from dataclasses import replace

    import experiments
    from cataclysm_sim.config import TuningConfig

    header = experiments.header_lines()
    computed = experiments.cataclysm_power_key(
        replace(TuningConfig(), tier=experiments.SWEEP_TIER))
    for line in computed:
        assert line in header, (
            "sim/experiments.py's header no longer prints the computed power "
            "key, so the report is describing the power column with something "
            f"else. Missing: {line!r}")

    source = pathlib.Path(experiments.__file__).read_text(encoding="utf-8")
    assert "cataclysm_power_key" in source
    assert "320-420" not in source, (
        "the stale hard-coded power range is back in sim/experiments.py. It was "
        "derived against the player power anchors issue #2 replaced. See "
        "issue #8.")


def test_the_report_prints_its_header_rather_than_building_one_inline():
    """`main()` must print `header_lines()`, or the test above checks nothing.

    A text check, and it has to be: `main()` is about eighteen minutes, so
    nothing can call it to see what it prints.
    """
    import pathlib

    import experiments

    source = pathlib.Path(experiments.__file__).read_text(encoding="utf-8")
    body = source[source.index("def main("):]
    assert "header_lines()" in body, (
        "sim/experiments.py's main() no longer prints header_lines(), so the "
        "report's header is not the one the tests check.")


def test_the_report_key_says_whether_the_figure_is_reachable(cfg):
    """It is not reachable at any tier today. The sentence must follow from the
    numbers rather than be asserted, or it becomes false silently if they move.
    """
    import experiments

    text = " ".join(experiments.cataclysm_power_key(cfg))
    ceiling = scoring.tier_bounds(cfg.tier)[1]
    spec = cfg.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)]
    want = needed(combat.REPORTED_DEATH_CHANCE, cfg.tier, cfg,
                  round(sum(spec.floors) / 2))
    if want > ceiling:
        assert "NOT reachable" in text
    else:
        assert "is reachable" in text


def test_the_report_key_reads_the_dungeon_depth_from_the_config(cfg):
    """A depth typed into the report would go stale the first time the dungeon
    specification changed, which is the class of fault this issue is about."""
    import experiments
    from dataclasses import replace

    spec = cfg.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)]
    expected = round(sum(spec.floors) / 2)
    assert f"{expected} floors" in " ".join(
        experiments.cataclysm_power_key(cfg))

    deeper = replace(cfg)
    deeper.DUNGEON_SPECS = dict(cfg.DUNGEON_SPECS)
    original = deeper.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)]
    deeper.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)] = replace(
        original, floors=(200, 200))
    assert "200 floors" in " ".join(experiments.cataclysm_power_key(deeper))


# --------------------------------------------------------------------------
# The design document states the intent, and every number in it is checked
# --------------------------------------------------------------------------
#
# Issue #250 asked whether a maxed player losing about one Cataclysm dungeon run
# in five is intended. The project owner answered on 2026-08-05 that it is, and
# chose to write the intent down rather than change any number.
#
# EVERY FIGURE IN THAT SECTION IS RE-DERIVED HERE RATHER THAN RESTATED. The
# section is a measurement of the current model, so the failure to guard against
# is the model moving and the prose staying. That has happened before: issue #6
# was three analysis scripts whose printed conclusions went stale when the player
# power anchors changed, and issue #253 was a second copy of those anchors that
# nothing was checking.


def unwrapped(text: str) -> str:
    """Prose wraps. Collapse whitespace before matching a sentence."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def gdd_section() -> str:
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    path = root / "docs" / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the design document is not present")
    body = path.read_text(encoding="utf-8")

    heading = "### **The Final Fight Is Never Safe, and That Is Deliberate**"
    assert heading in body, (
        "the design document no longer has the section stating that a maxed "
        "player is expected to lose about one Cataclysm dungeon run in five. "
        "Without it there is no way to tell whether the measured figure is the "
        "target, twice it, or half of it. Issue #250.")
    start = body.index(heading)
    end = body.index("### **Maximum Resistance**", start)
    return unwrapped(body[start:end])


def test_the_section_exists_at_all():
    """Standalone, and deliberately not using the fixture above.

    The fixture asserts the heading is present, so deleting the section makes
    every test that depends on it ERROR rather than FAIL. An error is weaker
    evidence than a named failure -- it does not say which rule was broken. This
    is the one test that fails by name when the section goes.
    """
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    path = root / "docs" / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the design document is not present")
    assert ("### **The Final Fight Is Never Safe, and That Is Deliberate**"
            in path.read_text(encoding="utf-8")), (
        "the design document no longer states that a player at their tier's "
        "ceiling is expected to lose about one Cataclysm dungeon run in five, "
        "and that this is deliberate. Issue #250.")


def test_it_says_the_risk_is_intended_rather_than_an_accident(gdd_section):
    """The whole content of the answer. A measured number with no statement of
    intent beside it is what issue #250 was filed about."""
    assert "That is intended, not a tuning accident." in gdd_section


def test_it_says_the_run_ends_rather_than_the_attempt(gdd_section):
    """One in five sounds tolerable for an attempt and severe for a run, and it
    is a run. `Engine._resolve_dungeon` sets `lost` and returns."""
    assert "It is one run in five, not one attempt in five." in gdd_section


def test_the_band_it_states_actually_contains_every_tier(cfg, floors,
                                                         gdd_section):
    """The bounds are parsed out of the document, not typed here, so moving
    either the prose or the model without the other fails."""
    found = re.search(r"Between (\d+)% and (\d+)% at every tier", gdd_section)
    assert found, (
        "the design document no longer states the band of death chances a "
        "player at their tier ceiling faces at the Cataclysm boss dungeon")
    low, high = (int(g) / 100 for g in found.groups())

    measured = [risk(scoring.tier_bounds(t)[1], t, cfg, floors)
                for t in range(1, 9)]
    for tier, chance in enumerate(measured, start=1):
        assert low <= chance <= high, (
            f"tier {tier} measures {chance:.1%}, outside the {low:.0%} to "
            f"{high:.0%} the design document states. Either the model moved and "
            "the document is stale, or the document was tightened past what the "
            "model does.")

    # The band has to be a band, not a claim so loose it could not fail.
    assert high - low <= 0.10, (
        f"the stated band {low:.0%} to {high:.0%} is wider than 10 percentage "
        "points, which is loose enough to survive a real change to the model")


def test_the_multiples_of_the_ceiling_it_states_are_the_measured_ones(
        cfg, floors, gdd_section):
    """"from 2.0 times at tier 1 down to 1.2 times at tier 8" is the reason the
    fight cannot be made safe within a tier, so it is the sentence most worth
    keeping true."""
    found = re.search(
        r"from ([\d.]+) times at tier 1 down to ([\d.]+) times at tier 8",
        gdd_section)
    assert found, (
        "the design document no longer says how far the Cataclysm Boss "
        "out-scores the player ceiling at either end of the tier range")
    stated_first, stated_last = (float(g) for g in found.groups())

    def multiple(tier: int) -> float:
        ceiling = scoring.tier_bounds(tier)[1]
        boss = scoring.enemy_scores(tier, DTYPE, SUBTYPE, floors, floors,
                                    0.0)[BOSS]
        return boss / ceiling

    assert multiple(1) == pytest.approx(stated_first, abs=0.05)
    assert multiple(8) == pytest.approx(stated_last, abs=0.05)
    assert multiple(1) > multiple(8), (
        "the gap between the boss and the player ceiling no longer narrows as "
        "the tier rises, which is what the document says it does")


def test_the_floor_count_it_measured_at_is_the_one_the_config_gives(
        cfg, floors, gdd_section):
    """A depth typed into prose goes stale the first time the dungeon
    specification changes."""
    spec = cfg.DUNGEON_SPECS[(DungeonType.CATACLYSM, CityTier.PILLAR)]
    low, high = spec.floors
    assert f"Measured at {floors} floors" in gdd_section
    assert f"the midpoint of the {low} to {high}" in gdd_section


def test_the_guard_it_points_at_is_the_one_that_exists(gdd_section):
    """The section tells a reader which test fails if the figure moves. If that
    test is renamed or its band changed, the pointer has to move with it."""
    assert "sim/tests/test_power_threshold.py" in gdd_section
    assert "a band of 10% to 30%" in gdd_section, (
        "the design document names a band that "
        "test_a_player_at_the_tier_ceiling_still_dies_sometimes no longer "
        "asserts")
