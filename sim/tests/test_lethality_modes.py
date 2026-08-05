"""The three difficulty modes, and which of their rules this rig can represent.

WHY THIS EXISTS. Issue #289. The empire upgrade tree is partitioned by lethality
mode (issue #277), so a first Heretic character starts with an empty Heretic tree
however much Standard progress the account has. The issue asks whether Heretic's
25% extra surge dungeons over-compensate for that cold start, which would make
the hardest mode the fastest place to grow an empire tree.

It could not be asked of this simulation, because nothing here represented the
modes at all. A search on 2026-08-05 found "lethality", "SSF" and "Self-Found"
only in the design document and its two test files.

WHAT IS MODELLED, and it is not all four columns of the design document's table:

    death day cost              5 / 10 / 15         modelled
    surge dungeon multiplier    1.0 / 1.0 / 1.25    modelled
    city upgrade slots          3 / 3 / 2           NOT modelled -- issue #318
    equipment lost on death     0 / 10% / 20%       no per-item model exists
    heads-up display            shown / map / none  no perception model exists

THE THREE UNMODELLED ROWS ALL COST THE HARDER MODES. That is what makes the
measurement in `sim/analyse_lethality_modes.py` an upper bound on how much
Heretic is over-compensated rather than an estimate of it, and it is why the
answer is usable despite being incomplete.

WHAT IS ASSERTED HERE.

    the three modes carry the numbers the design document gives them
    `with_lethality` sets every number a mode owns, together, so a config cannot
      be labelled Heretic while running Standard numbers
    the defaults in TuningConfig are exactly the Standard rules
    Heretic's multiplier reaches the number of dungeons a surge spawns, and does
      so after the cap rather than before it
    nothing reads `city_upgrade_slots`, which is what issue #318 says
"""

from __future__ import annotations

import pathlib
from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (
    LETHALITY_RULES, TREE_NONE, LethalityMode, SurgeMode, TuningConfig,
)
from cataclysm_sim.engine import Simulation

#: What `docs/Cataclysm_GDD_v2.md` gives each mode, transcribed. Written out
#: rather than read from LETHALITY_RULES, so this checks the table against the
#: design document rather than against itself.
GDD = {
    LethalityMode.STANDARD: (5, 1.0, 3),
    LethalityMode.HARDCORE: (10, 1.0, 3),
    LethalityMode.HERETIC: (15, 1.25, 2),
}


class TestTheModesCarryTheDesignDocumentsNumbers:
    @pytest.mark.parametrize("mode", list(LethalityMode))
    def test_every_mode_has_rules(self, mode):
        assert mode in LETHALITY_RULES, (
            f"{mode} has no entry in LETHALITY_RULES, so with_lethality would "
            f"raise on it. Issue #289.")

    @pytest.mark.parametrize("mode", list(LethalityMode))
    def test_the_numbers_are_the_ones_the_design_document_gives(self, mode):
        days, surge, slots = GDD[mode]
        rules = LETHALITY_RULES[mode]
        assert rules.death_day_cost == days
        assert rules.surge_dungeon_multiplier == pytest.approx(surge)
        assert rules.city_upgrade_slots == slots

    def test_heretic_is_the_only_mode_that_changes_the_dungeon_count(self):
        """The design gives Hardcore a higher death cost and nothing else that
        touches the strategy layer. A multiplier that crept onto Hardcore would
        change what issue #289 measured without anything saying so."""
        changed = [m for m, r in LETHALITY_RULES.items()
                   if r.surge_dungeon_multiplier != 1.0]
        assert changed == [LethalityMode.HERETIC], (
            f"{changed} change the surge dungeon count. Only Heretic does: "
            f"'Surges spawn 25% more dungeons'. Issue #289.")

    def test_the_death_cost_rises_with_the_mode(self):
        """Standard 5, Hardcore 10, Heretic 15. The ordering is the point of
        the modes, so it is checked as an ordering and not only as values."""
        costs = [LETHALITY_RULES[m].death_day_cost for m in
                 (LethalityMode.STANDARD, LethalityMode.HARDCORE,
                  LethalityMode.HERETIC)]
        assert costs == sorted(costs) and len(set(costs)) == 3


class TestSettingTheModeSetsEverythingItOwns:
    def test_with_lethality_sets_the_mode_and_its_numbers_together(self):
        cfg = TuningConfig().with_lethality(LethalityMode.HERETIC)
        rules = LETHALITY_RULES[LethalityMode.HERETIC]
        assert cfg.lethality_mode is LethalityMode.HERETIC
        assert cfg.death_day_cost == rules.death_day_cost
        assert cfg.surge_dungeon_multiplier == rules.surge_dungeon_multiplier

    def test_it_changes_nothing_else(self):
        """A mode is three numbers, not a re-tune. If it ever starts touching
        anything else, the comparison in analyse_lethality_modes.py stops being
        a controlled one."""
        base = TuningConfig()
        heretic = base.with_lethality(LethalityMode.HERETIC)
        differing = {name for name in vars(base)
                     if getattr(base, name) != getattr(heretic, name)}
        assert differing == {"lethality_mode", "death_day_cost",
                             "surge_dungeon_multiplier"}, (
            f"with_lethality now also changes {differing}. Issue #289.")

    def test_the_defaults_are_the_standard_rules(self):
        """THE GUARD AGAINST TWO SOURCES OF TRUTH. `death_day_cost` and
        `surge_dungeon_multiplier` have defaults of their own AND live in
        LETHALITY_RULES. If the two ever disagree, a config that never called
        with_lethality would be labelled Standard and run on something else."""
        base = TuningConfig()
        rules = LETHALITY_RULES[LethalityMode.STANDARD]
        assert base.lethality_mode is LethalityMode.STANDARD
        assert base.death_day_cost == rules.death_day_cost, (
            "TuningConfig's default death_day_cost no longer matches "
            "LETHALITY_RULES[STANDARD]. One of the two moved. Issue #289.")
        assert base.surge_dungeon_multiplier == rules.surge_dungeon_multiplier

    def test_standard_is_the_identity(self):
        """Applying Standard to a fresh config must produce a fresh config, or
        every existing experiment silently changes meaning."""
        assert (TuningConfig().with_lethality(LethalityMode.STANDARD)
                == TuningConfig())


class TestTheMultiplierReachesTheSurge:
    def surge_count(self, **overrides) -> int:
        cfg = replace(TuningConfig(), **overrides)
        return Simulation(cfg, seed=0).surge_count()

    def test_heretic_spawns_more_dungeons_than_standard(self):
        standard = self.surge_count(surge_dungeon_multiplier=1.0)
        heretic = self.surge_count(surge_dungeon_multiplier=1.25)
        assert heretic > standard, (
            "Heretic's surge dungeon multiplier does not reach the number of "
            "dungeons a surge spawns, so the whole of issue #289 measures "
            "nothing. Issue #289.")

    def test_the_default_four_becomes_five(self):
        """4 x 1.25 = 5 exactly, so this case has no rounding in it and states
        the rule plainly."""
        assert self.surge_count(surge_dungeon_count=4,
                                surge_dungeon_multiplier=1.25) == 5

    def test_it_multiplies_after_the_cap_not_before(self):
        """The choice recorded in `Simulation.surge_count`. `surge_count_max`
        bounds how far the Cataclysm's own escalation runs; Heretic's rule is
        stated without qualification. Multiplying first would make Heretic
        identical to Standard at every surge that reached the cap, which is
        where the extra dungeons would hurt most."""
        capped = dict(surge_mode=SurgeMode.SWELLING, surge_dungeon_count=40,
                      surge_count_max=8)
        assert self.surge_count(surge_dungeon_multiplier=1.0, **capped) == 8
        assert self.surge_count(surge_dungeon_multiplier=1.25, **capped) == 10, (
            "the lethality multiplier is applied before surge_count_max, so a "
            "capped surge spawns the same number of dungeons on Heretic as on "
            "Standard. Issue #289.")

    def test_a_surge_never_spawns_nothing(self):
        """The max(1, ...) floor. A multiplier below 1 is not used today but
        the enum is data and someone will add one."""
        assert self.surge_count(surge_dungeon_count=1,
                                surge_dungeon_multiplier=0.1) == 1

    def test_heretic_actually_faces_more_dungeons_over_a_campaign(self):
        """The unit test above checks one call. This checks the effect survives
        a whole campaign, which is what the analysis script measures."""
        base = replace(TuningConfig(), tier=1).with_tree(TREE_NONE)
        seeds = range(12)
        standard = sum(Simulation(base, seed=i).run(policies.triage).dungeons_resolved
                       + Simulation(base, seed=i).run(policies.triage).dungeons_cleared
                       for i in seeds)
        heretic_cfg = replace(base, surge_dungeon_multiplier=1.25)
        heretic = sum(
            Simulation(heretic_cfg, seed=i).run(policies.triage).dungeons_resolved
            + Simulation(heretic_cfg, seed=i).run(policies.triage).dungeons_cleared
            for i in seeds)
        assert heretic > standard, (
            f"over {len(seeds)} campaigns Heretic met {heretic} dungeons and "
            f"Standard {standard}. The 25% extra dungeons are not reaching the "
            f"campaign. Issue #289.")


class TestTheUnmodelledHalfIsStillUnmodelled:
    """Issue #318. `LethalityRules.city_upgrade_slots` carries 3, 3, 2 because
    the design gives it and a reader will look for it, and NOTHING READS IT,
    because this simulation has no city upgrade system.

    That is stated in three docstrings and on two issues. If someone builds the
    system, all of those become wrong at once, so this fails and points at them
    rather than letting the claim quietly go stale.
    """

    PACKAGE = pathlib.Path(__file__).resolve().parents[1] / "cataclysm_sim"

    def files(self) -> list[pathlib.Path]:
        return sorted(self.PACKAGE.glob("*.py"))

    def test_only_config_mentions_the_slot_count(self):
        users = [path.name for path in self.files()
                 if "city_upgrade_slots" in path.read_text(encoding="utf-8")
                 and path.name != "config.py"]
        assert users == [], (
            f"{users} now use LethalityRules.city_upgrade_slots. Three "
            f"docstrings say nothing reads it -- in config.LethalityRules, in "
            f"sim/analyse_lethality_modes.py and in this file -- and issue "
            f"#289's measurement is described as one-sided because of it. "
            f"Update all of them and close issue #318.")

    def test_the_city_upgrade_field_is_still_inert(self):
        """The other half of the same claim. `world.City.upgrades` is declared
        and never touched. If it starts being used, the empire layer has grown
        a system that issue #289's measurement did not account for."""
        readers = [path.name for path in self.files()
                   if path.name not in ("world.py", "config.py")
                   and ".upgrades" in path.read_text(encoding="utf-8")]
        assert readers == [], (
            f"{readers} now read City.upgrades. The simulation has grown a city "
            f"upgrade system, so the one-sided measurement in "
            f"sim/analyse_lethality_modes.py can be completed. Issue #318.")
