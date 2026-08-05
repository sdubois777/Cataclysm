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
    """Checked against the SOURCE of `sim/experiments.py`, not against
    `cataclysm_power_key`.

    Testing the helper alone is not enough and this test exists because that gap
    was found: replacing the call in `main` with a hard-coded print left every
    test on the helper passing while the report went back to a stale number.
    Running `main` instead is not an option -- it is 25,000 simulated campaigns
    and about eighteen minutes.
    """
    import pathlib

    import experiments

    source = pathlib.Path(experiments.__file__).read_text(encoding="utf-8")
    body = source[source.index("def main("):]
    assert "cataclysm_power_key" in body, (
        "sim/experiments.py's main() no longer prints the computed power key, "
        "so the report is describing the power column with something else")
    assert "320-420" not in source, (
        "the stale hard-coded power range is back in sim/experiments.py. It was "
        "derived against the player power anchors issue #2 replaced. See "
        "issue #8.")


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
