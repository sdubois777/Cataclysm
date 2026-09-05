"""Section 7 must name the settings it did not choose, and name them correctly.

WHY THIS FILE EXISTS. Issue #1287. `exp_presets` compares the empire tree presets
and prints an ordering, but it chooses almost none of the numbers that ordering
depends on. `exp_calibrate` picks the surge mode, the resolve timer ratio, the
surge interval and the surge size by maximising `health`, which scores a NO-TREE
player against a 55% win rate and 45% triage target. `exp_forge` then picks the
difficulty treadmill rate and the craft numbers. Neither is asking a question
about the empire tree. Section 7 used to say only that the settings were
"calibrated at tier 1 in sections 0 and 2" and never say what they were.

THE ORDERING IS NOT STABLE ACROSS THEM. Measured 2026-09-05 at tier 1, the triage
policy, 150 campaigns per cell, moving only the surge size:

    dungeons per surge   4     5     6     7
    No tree win%        44    52    15    11
    Architect win%      45    52    53    48
    verdict vs no tree  tied  tied  BETTER  BETTER

Four surge sizes gave four different orderings. `TuningConfig.surge_dungeon_count`
defaults to 4, which `exp_calibrate` never tries, so anyone calling `exp_presets`
with a raw config measures a different world from the report's and gets a
different answer with nothing to explain why.

AND ISSUE #1290, which is the same defect one step further on: the day-cap
warning printed a hard-coded "22% per 100 days" and a 6.5x power multiplier
beside a table produced at 10% and 3.5x, because 0.22 is `TuningConfig`'s default
and section 7 runs at whatever `exp_forge` chose. Fixed together with #1287
because they contradict each other apart -- once the header prints the real rate,
the warning's fixed one sits a few lines below it saying something different.

WHAT IS CHECKED HERE. That every reported value is read off the config rather
than written out; that the reported set covers every field sections 0 and 2
actually set, which is the guard that caught `surge_mode` being missing; and that
section 7 prints them. None of it runs the sweep.
"""

from __future__ import annotations

import ast
import contextlib
import io
import pathlib
import sys
from dataclasses import replace

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import experiments  # noqa: E402
from cataclysm_sim.config import SurgeMode, TuningConfig  # noqa: E402

SOURCE = pathlib.Path(experiments.__file__).read_text(encoding="utf-8")
TREE = ast.parse(SOURCE)

#: The config section 7 received on 2026-09-05, from exp_calibrate then
#: exp_forge. Not the defaults -- that is the whole point.
CALIBRATED = dict(
    surge_mode=SurgeMode.STATIC, resolve_floor_ratio=2.0,
    surge_interval_days=120.0, surge_dungeon_count=5,
    dungeon_power_escalation_per_100_days=0.10,
    craft_days=12, craft_power_gain_frac=0.04,
)


def function_named(name: str) -> ast.FunctionDef:
    for node in ast.walk(TREE):
        if isinstance(node, ast.FunctionDef) and node.name == name:
            return node
    raise AssertionError(f"{name} is gone from experiments.py")


def fields_replaced_in(name: str) -> set[str]:
    """Every TuningConfig field this function sets through `replace`."""
    found = set()
    for node in ast.walk(function_named(name)):
        if (isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "replace"):
            found.update(kw.arg for kw in node.keywords if kw.arg)
    return found


def presets_output(**kwargs) -> str:
    base = replace(TuningConfig(), tier=experiments.SWEEP_TIER, **CALIBRATED)
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        experiments.exp_presets(base, **kwargs)
    return buffer.getvalue()


class TestTheReportedSettingsAreReadOffTheConfig:

    def test_every_named_field_exists_on_the_config(self):
        for field, _, _ in experiments.INHERITED_SETTINGS:
            assert hasattr(TuningConfig(), field), (
                f"INHERITED_SETTINGS names {field}, which TuningConfig does "
                "not have, so the report would crash or print nothing.")

    def test_one_row_per_named_field(self):
        cfg = replace(TuningConfig(), **CALIBRATED)
        assert (len(experiments.inherited_settings(cfg))
                == len(experiments.INHERITED_SETTINGS))

    def test_the_labels_are_prose_rather_than_attribute_names(self):
        """A reader of the report is not reading config.py."""
        cfg = replace(TuningConfig(), **CALIBRATED)
        for label, _ in experiments.inherited_settings(cfg):
            assert "_" not in label, (
                f"{label!r} is an attribute name rather than a description")

    @pytest.mark.parametrize("field,value,expected", [
        ("surge_dungeon_count", 4, "4"),
        ("surge_dungeon_count", 7, "7"),
        ("dungeon_power_escalation_per_100_days", 0.22, "0.22"),
        ("dungeon_power_escalation_per_100_days", 0.50, "0.50"),
        ("resolve_floor_ratio", 1.2, "1.2"),
        ("craft_days", 20, "20"),
    ])
    def test_the_value_reported_is_the_value_configured(
            self, field, value, expected):
        """The defect this whole file is about: a report that states a setting
        it did not run under. Issues #1287 and #1290."""
        cfg = replace(TuningConfig(), **{**CALIBRATED, field: value})
        reported = [v for _, v in experiments.inherited_settings(cfg)]
        assert expected in reported, (
            f"ran at {field}={value} and the report says {reported}")

    def test_the_surge_mode_prints_its_value_not_its_python_type(self):
        cfg = replace(TuningConfig(), **CALIBRATED)
        reported = [v for _, v in experiments.inherited_settings(cfg)]
        assert "static" in reported
        assert not any("SurgeMode" in v for v in reported)

    def test_a_different_surge_mode_is_reported_differently(self):
        cfg = replace(TuningConfig(),
                      **{**CALIBRATED, "surge_mode": SurgeMode.ACCELERATING})
        assert "accelerating" in [
            v for _, v in experiments.inherited_settings(cfg)]


class TestTheReportedSettingsCoverWhatTheOtherSectionsSet:
    """The guard that caught `surge_mode` being left out."""

    #: Fields the earlier sections set that section 7 does NOT inherit, with the
    #: reason. Anything else they set has to be reported.
    #:
    #: KEEP THIS AS SHORT AS THE TRUTH ALLOWS. Every entry is a hole in the
    #: guard, and an entry for a field the sections do not set is a hole with an
    #: invented justification attached -- the check would still pass and would be
    #: measuring less than it claims.
    NOT_INHERITED = {
        # exp_presets loops over `tiers` and prints the tier in each table's own
        # heading, so it chooses this rather than inheriting it.
        "tier",
    }

    def test_every_exemption_is_a_field_those_sections_actually_set(self):
        """An exemption for a field nobody sets exempts nothing and reads as a
        considered decision. One was written here and removed."""
        chosen = (fields_replaced_in("exp_calibrate")
                  | fields_replaced_in("exp_forge"))
        assert not self.NOT_INHERITED - chosen, (
            f"{sorted(self.NOT_INHERITED - chosen)} is exempted from the "
            "reported settings, and no earlier section sets it, so the "
            "exemption is inert. Delete it.")

    def test_every_field_sections_0_and_2_set_is_reported(self):
        reported = {field for field, _, _ in experiments.INHERITED_SETTINGS}
        chosen = (fields_replaced_in("exp_calibrate")
                  | fields_replaced_in("exp_forge"))
        missing = chosen - reported - self.NOT_INHERITED
        assert not missing, (
            f"sections 0 and 2 set {sorted(missing)}, section 7 runs at "
            "whatever they chose, and the report does not name it. Add it to "
            "INHERITED_SETTINGS or to NOT_INHERITED with the reason. "
            "Issue #1287.")

    def test_nothing_is_reported_that_those_sections_do_not_set(self):
        """A reported setting that no earlier section chooses is not inherited,
        and listing it under that heading is a false claim."""
        reported = {field for field, _, _ in experiments.INHERITED_SETTINGS}
        chosen = (fields_replaced_in("exp_calibrate")
                  | fields_replaced_in("exp_forge"))
        assert not reported - chosen, (
            f"{sorted(reported - chosen)} is reported as inherited from "
            "sections 0 and 2, and neither section sets it.")

    def test_the_surge_size_is_among_them(self):
        """Named on its own because it is the field issue #1287 is about, and
        the one the ordering moves most sharply with."""
        assert "surge_dungeon_count" in {
            f for f, _, _ in experiments.INHERITED_SETTINGS}


class TestSectionSevenPrintsThem:

    def test_the_settings_block_is_printed(self):
        out = presets_output(tiers=(1,), trials=2)
        assert "SETTINGS THIS SECTION DID NOT CHOOSE" in out

    def test_every_setting_appears_with_its_value(self):
        out = presets_output(tiers=(1,), trials=2)
        cfg = replace(TuningConfig(), tier=experiments.SWEEP_TIER, **CALIBRATED)
        for label, value in experiments.inherited_settings(cfg):
            assert label in out, f"{label} is not printed"
            assert value in out, f"{label} is printed without its value {value}"

    def test_it_says_the_ordering_is_conditional_on_them(self):
        """Naming the settings without saying they change the answer leaves the
        reader with more numbers and the same wrong conclusion."""
        out = presets_output(tiers=(1,), trials=2)
        assert "CONDITIONAL" in out
        assert "four different orderings" in out

    def test_it_names_the_surge_size_as_the_sharpest_of_them(self):
        out = presets_output(tiers=(1,), trials=2)
        assert "dungeons per surge" in out

    def test_the_block_is_printed_once_rather_than_per_tier(self):
        """These do not change between tiers -- that is what makes the tables
        comparable -- so printing them per tier would suggest they do."""
        out = presets_output(tiers=(1, 8), trials=2)
        assert out.count("SETTINGS THIS SECTION DID NOT CHOOSE") == 1

    def test_it_comes_before_the_first_table(self):
        out = presets_output(tiers=(1,), trials=2)
        assert out.index("SETTINGS THIS SECTION DID NOT CHOOSE") < out.index(
            "TIER 1 -- player power ceiling")
