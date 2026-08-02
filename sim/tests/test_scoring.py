"""Tests for the player and enemy power model.

scoring.py is a port of DungeonSimulator's src/utils/calculateScores.tsx. It has
drifted from that file twice: once when the tier anchor table was replaced
upstream and not copied over, and once through a rounding difference between
Python and JavaScript. These tests exist so neither can happen again quietly.

The tests that need the reference file skip when it is absent, so this suite
passes in CI where DungeonSimulator is not checked out.
"""

from __future__ import annotations

import math

import pytest

from cataclysm_sim import scoring

RARITY_ORDER = ["Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss"]

# T1 / Basic / Timed / 50 floors / floor 25 / no modifier. Produced by running
# calculateScores.tsx itself under Node, not by hand.
REFERENCE_CASE = dict(tier=1, dungeon_type="Basic", subtype="Timed",
                      total_floors=50, current_floor=25, modifier_score=0)
REFERENCE_ENEMY_SCORES = {"Common": 188, "Elite": 208, "Legendary": 227,
                          "Herald": 246, "Boss": 304, "Cataclysm Boss": 381}
REFERENCE_DUNGEON_SCORE = 201


class TestReferenceValues:
    """Pin the arithmetic to values the real TypeScript produced."""

    def test_enemy_scores_match_reference(self):
        assert scoring.enemy_scores(**REFERENCE_CASE) == REFERENCE_ENEMY_SCORES

    def test_dungeon_score_matches_reference(self):
        got = scoring.dungeon_score(1, "Basic", "Timed", 50)
        assert got == REFERENCE_DUNGEON_SCORE

    def test_final_boss_score_uses_the_last_floor(self):
        last = scoring.final_boss_score(1, "Basic", "Timed", 50, rarity="Boss")
        expected = scoring.enemy_scores(1, "Basic", "Timed", 50, 50)["Boss"]
        assert last == expected


class TestJavaScriptRounding:
    """The reference rounds with Math.round; Python's round() does not agree.

    Math.round sends exact halves upward. Python's round() is banker's rounding
    and sends them to the nearest even number. This differed on about 2% of all
    inputs before _js_round was introduced.
    """

    @pytest.mark.parametrize("value,expected", [
        (0.5, 1), (1.5, 2), (2.5, 3), (3.5, 4),      # round() gives 0, 2, 2, 4
        (-0.5, 0), (-1.5, -1), (-2.5, -2),           # halves go toward +infinity
        (0.4, 0), (0.6, 1), (-0.4, 0), (-0.6, -1),
        (390.5, 391), (188.0, 188),
    ])
    def test_matches_javascript_math_round(self, value, expected):
        assert scoring._js_round(value) == expected

    def test_differs_from_builtin_round_on_exact_halves(self):
        # Guards against someone "simplifying" _js_round back to round().
        assert scoring._js_round(2.5) != round(2.5)
        assert scoring._js_round(390.5) != round(390.5)


class TestTierBounds:
    def test_tier_one_starts_at_zero(self):
        low, _ = scoring.tier_bounds(1)
        assert low == 0

    @pytest.mark.parametrize("tier", range(1, 9))
    def test_bounds_are_ordered_and_width_is_positive(self, tier):
        low, high = scoring.tier_bounds(tier)
        assert high > low
        assert scoring.tier_width(tier) == high - low

    def test_anchors_increase_with_tier(self):
        anchors = [scoring.PLAYER_MAX_SCORES[t] for t in range(0, 9)]
        assert anchors == sorted(anchors)
        assert len(set(anchors)) == len(anchors)

    def test_tier_bounds_falls_back_for_out_of_range_tiers(self):
        # tier_bounds uses .get with a default rather than raising.
        low, high = scoring.tier_bounds(99)
        assert high == 4584
        assert low == 0


class TestScoreShape:
    """Properties that must hold regardless of the exact constants."""

    @pytest.mark.parametrize("tier", [1, 4, 8])
    def test_rarities_are_ordered_from_common_to_cataclysm_boss(self, tier):
        scores = scoring.enemy_scores(tier, "Basic", "None", 50, 25)
        values = [scores[r] for r in RARITY_ORDER]
        assert values == sorted(values)

    @pytest.mark.parametrize("tier", [1, 4, 8])
    def test_deeper_floors_are_harder_within_one_dungeon(self, tier):
        by_floor = [scoring.enemy_scores(tier, "Basic", "None", 50, f)["Common"]
                    for f in (1, 10, 25, 40, 50)]
        assert by_floor == sorted(by_floor)
        assert by_floor[0] < by_floor[-1]

    def test_higher_tiers_are_harder(self):
        mids = [scoring.dungeon_score(t, "Basic", "None", 50) for t in range(1, 9)]
        assert mids == sorted(mids)

    def test_modifier_score_shifts_every_rarity_equally(self):
        base = scoring.enemy_scores(3, "Quest", "Horde", 40, 20, 0)
        bumped = scoring.enemy_scores(3, "Quest", "Horde", 40, 20, 50)
        assert all(bumped[r] - base[r] == 50 for r in RARITY_ORDER)

    def test_harder_dungeon_types_score_higher(self):
        scores = [scoring.dungeon_score(4, t, "None", 50)
                  for t in ("Basic", "Quest", "Fallen City", "Cataclysm")]
        assert scores == sorted(scores)

    def test_single_floor_dungeon_is_handled(self):
        # totalFloors=1 makes currentFloor, totalFloors and middleFloor all equal.
        scores = scoring.enemy_scores(1, "Basic", "None", 1, 1)
        assert all(isinstance(v, int) for v in scores.values())
        assert scoring.dungeon_score(1, "Basic", "None", 1) > 0

    def test_depth_is_length_not_difficulty(self):
        """A 150-floor dungeon is only slightly harder per floor than a 20-floor one.

        This is the single most important property of the model: depth costs
        time, not survivability. The empire layer's whole cost structure rests
        on it, so a change here should be deliberate.
        """
        shallow = scoring.dungeon_score(1, "Basic", "Timed", 20)
        deep = scoring.dungeon_score(1, "Basic", "Timed", 150)
        assert deep > shallow
        assert deep < shallow * 1.5


class TestReferenceFileAgreement:
    """Compare against calculateScores.tsx when it is available."""

    def test_constants_match_the_reference(self):
        if scoring.reference_path() is None:
            pytest.skip("DungeonSimulator checkout not present")
        # Raises AssertionError with a readable diff on any drift.
        assert scoring.check_against_reference(verbose=False) is True

    def test_reference_path_respects_the_environment_override(self, monkeypatch, tmp_path):
        missing = tmp_path / "does_not_exist.tsx"
        monkeypatch.setenv(scoring.REFERENCE_ENV_VAR, str(missing))
        assert scoring.reference_path() is None
        assert scoring.check_against_reference(verbose=False) is False

    def test_drift_in_the_reference_is_detected(self, monkeypatch, tmp_path):
        """A corrupted reference must fail. A check that cannot fail is worthless."""
        real = scoring.reference_path()
        if real is None:
            pytest.skip("DungeonSimulator checkout not present")
        drifted = tmp_path / "drifted.tsx"
        drifted.write_text(
            real.read_text(encoding="utf-8").replace("1: 385,", "1: 999,"),
            encoding="utf-8")
        monkeypatch.setenv(scoring.REFERENCE_ENV_VAR, str(drifted))
        with pytest.raises(AssertionError, match="playerMaxScores has DRIFTED"):
            scoring.check_against_reference(verbose=False)


class TestSelfTest:
    def test_module_self_test_passes(self, capsys):
        scoring._selftest()
        assert "Formula check passed" in capsys.readouterr().out


def test_middle_floor_uses_ceiling():
    """dungeon_score reads the middle floor, defined as ceil(totalFloors / 2)."""
    for total in (7, 20, 51):
        middle = math.ceil(total / 2)
        expected = scoring.enemy_scores(2, "Basic", "None", total, middle)
        direct = scoring.dungeon_score(2, "Basic", "None", total)
        mixed = sum(expected[r] * w for r, w in scoring.DUNGEON_SCORE_MIX)
        assert direct == scoring._js_round(mixed)
