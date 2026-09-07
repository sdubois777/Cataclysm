# Cataclysm — empire layer tuning rig

A headless simulation of the strategy layer. It is **not** the game and it is
not Unreal code. It exists to derive the numbers the design documents leave
open, by playing thousands of campaigns and measuring what happens.

```bash
python experiments.py
```

Pure standard-library Python. No dependencies.

## Which difficulty tier the results are from

**Every section of `experiments.py` runs at difficulty tier 1, except section 7,
which runs at tier 1 and tier 8.** The game has eight tiers. A number from this
report is a tier 1 number unless its own heading says otherwise, and the report
prints that in its header.

This matters because tier width — the gap between one tier's maximum player
Power Score and the tier below — multiplies every weighted term of the Enemy
Score formula. It runs 385, 498, 625, 717, 853, 979, 1063, 1207 across the eight
tiers, so the relation between player power and enemy power is not the same at
both ends. Measured: the empire tree preset ordering in section 7 is not the same
at tier 1 as at tier 8. **STALE — that comparison was made when both tiers ran
against one fixed Cataclysm, so it compared two power scales rather than two
tiers. See the next section.**

The tier is `SWEEP_TIER` in `experiments.py`, and the preset section's tiers are
`PRESET_TIERS`. Sweeping all eight tiers would take about two and a half hours,
which is why only section 7 pays for a second one. This was issue
[#281](https://github.com/sdubois777/Cataclysm/issues/281); before it, the tier
came from an unstated default and nothing in the output mentioned it.

## Which Cataclysms a campaign faces, and what that makes stale

**How many is the difficulty tier: tier N faces N of the eight. Which ones is
drawn per character.** `TuningConfig.active_cataclysm_count` and
`engine.cataclysm_order_for` own those two rules. Issue
[#1338](https://github.com/sdubois777/Cataclysm/issues/1338).

**The seed is the character.** The order is drawn from a generator keyed only on
the campaign seed, so the same seed always meets the same Cataclysms — which is
the design's rule that a failed run replays the same tier against the same ones —
and the same seed one tier higher meets those plus one, which is one character
climbing. Different seeds are different characters, so a sweep cell averages over
the draws a population of players would meet.

**EVERY CAMPAIGN FIGURE ON RECORD BELOW TIER 8 IS STALE, INCLUDING THE ONES IN
THIS FILE.** Until #1338 the count was a flat 1 whatever the tier, and the set
was the first N of a fixed tuple, so every campaign the model ever ran faced
Demonic and nothing else. Demonic is the only one of the eight that ignores the
frontier, so every lane-based and frontier-pressure figure was measured against
the one Cataclysm that does not respect lanes. Measured on #1338 at 250 campaigns
per cell, tier 1, the `triage` policy, at surge size 4 — the raw `TuningConfig`
default, not the calibrated 5 this report uses: which Cataclysm is active swings
the win rate by 13.6 points, against the 4.5 points `win_rate_noise` allows at
that sample size, and it flips the ordering between empire tree branches.

**Tier 8 is unaffected, but say it precisely.** All eight are active there under
either scheme, so the modifier pool is the same 116 entries for every seed and
the mix of attack patterns is identical — measured, not assumed. What the draw
still changes at tier 8 is the ORDER the eight sit in, and `_surge` picks a
Cataclysm by index, so one seeded campaign will not replay identically. The
distribution a tier 8 cell samples from has not moved; the particular sample has.

**Specifically stale in this file**: every Last Stand figure in the map section
below, and the claim that the preset ordering differs between tier 1 and tier 8.
Both are marked where they appear. Re-measuring them is issue
[#1358](https://github.com/sdubois777/Cataclysm/issues/1358), which also lists
what was **not** checked — nobody has swept `docs/DECISIONS.md` for the older
campaign figures.

## Fixed rules (not swept)

- **One floor costs one day, as a starting rate.** `days_per_floor` is 1.0 and
  this model never changes it, which is why it is listed here: no sweep moves it.
  **It is not an invariant of the design.** In the game, city upgrades and the
  empire upgrade tree lower the days a dungeon takes to walk while its floor
  count stays where it is, so an invested player can run a fifty floor dungeon in
  a couple of days. **Depth and reward are the same axis; depth and time are not,
  once a player has invested.** This model has no upgrades, so here the two never
  come apart.
- **Resolve timers scale with depth**: `resolve_days = base + floors * ratio`.
  A flat timer table cannot coexist with the rule above — a 40-floor dungeon
  on a 30-day timer is unsavable no matter how well the player plays.
- **A city falling triggers a surge**, and optionally advances the escalation.
- **A surge never brings more than `surge_count_max` dungeons, and that
  ceiling applies to the BASE count and not only to escalation growth.** It is
  14, matching `MostDungeonsPerSurge` in `CataclysmSurge.h`. The project owner
  ruled on 2026-09-06, verbatim, "Leave it, document it", so this is the
  documentation.

  **SETTING `surge_dungeon_count` ABOVE 14 SILENTLY GIVES 14.**
  `Simulation.surge_count` applies `min(n, surge_count_max)` before anything
  else, and warns about nothing. A sweep whose count axis runs past 14
  measures the same cell over and over and reports it as a trend; that is how
  it was found, on issue #1090, where a grid of 4, 5, 10, 20, 30 and 40 was
  really a grid of 4, 5, 10, 14, 14 and 14. `sim/analyse_surge_cadence.py`
  raises the cap for the length of its own batches so its axis means what its
  label says; nothing that ships may exceed 14 without moving both constants
  together.

## What it models

| Modelled | Deliberately not modelled |
|---|---|
| Days, surges, dungeon spawn | Combat, skills, gear, enchantments |
| Dungeon floors, run time, resolve timers | Loot, crafting, gold |
| City defense/population, city falls | The player's build |
| Fallen cities and retaking | Individual passive nodes |
| Quest objectives and the win condition | Anything visual |

The player is a single actor who is either idle at the Pillar or committed to
a dungeon for a fixed number of days. Combat is assumed won — the only cost of
a dungeon is **time**, which is the design's stated primary resource.

## The map

A layered graph rather than a hex field, so "a clear path to the capital" has
an exact meaning:

```
Pillar (1)
  └── Sanctuary (4)        1 step from the Pillar
        └── Bulwark (8)    2 steps
              └── Outpost (12)  3 steps
```

**Every campaign that ends at all ends inside a Cataclysm dungeon.**
`engine.Simulation` sets `won` at exactly one place and `lost` at exactly
one, both in `_finish_current` and both for a dungeon of type Cataclysm:
clear it and the run is won, die in it and the run is lost. There is no
other exit.

There are two ways to get into one:

- **Earned.** Clear the quest objectives and the enemy capital opens.
- **The Last Stand.** Some Outpost, its parent Bulwark, and that Bulwark's
  parent Sanctuary have all fallen, so the Cataclysm can reach the Pillar
  and comes to the player, absorbing every dungeon still standing as extra
  floors. **This is not itself a loss**, and the older wording here said it
  was. It is a fight, and at tier 1 it is a fight the player wins about
  **1 time in 84 per Last Stand reached** -- 1 in 70 if you count only the
  ones actually entered, which is a quarter fewer -- against about **40%**
  for an earned one. Measured 2026-09-06 over 10,000 campaigns in two
  disjoint seed blocks. Issue #5 measured the original; whether a
  near-unwinnable fight is the intended shape is issue #1286, and the
  owner ruled that it is.

  **STALE: every figure in this bullet and the two below it was measured at
  tier 1 against Demonic as the only active Cataclysm**, which is what the
  model did until issue #1338. They are kept because they are the last
  measured values and nothing has replaced them, not because they still
  describe what the model does. The copies of them in
  `docs/Cataclysm_GDD_v2.md` carry the same warning.

  **Those figures replace 2% and 57%, and the fight is now about twice as
  common** -- 27.7% of campaigns against 13.5%. Two thirds of that rise is
  the dungeon sub-type distribution and the boss growing with dungeons
  cleared; the rest is not attributed, which is issue #1343.

  **THOSE ARE MEASURED WITH THE SIEGE'S CITY DAMAGE ZEROED**, because that
  is the only way to compare with the older figures. With it on, and at the
  Siege settings of the day, the Last Stand was reached in **99.1%** of
  campaigns and the earned Cataclysm dungeon opened in **2.4%**: the route
  the design treats as the ordinary win condition had almost stopped
  happening.

  **THE OWNER RULED AGAINST THAT ON 2026-09-06, ISSUE #1349**, verbatim
  "Halve the rate and cut the growth". The Siege spawn weight went 15 to 7.5
  and `siege_damage_growth_per_day` 10 to 2.5; the two 1% shares of a city's
  maximum were left alone. **Re-measured on the shipped configuration** with
  `sim/analyse_siege_dose.py` at `CATACLYSM_SIEGE_DOSE_TRIALS=1000` -- 2,000
  campaigns in two disjoint blocks of 1,000 seeds, at tier 1, `No tree`,
  `triage`, STATIC surges every 120 days x5, resolve ratio 2.0, escalation
  0.10 per 100 days, craft 12 days +4%, Siege damage live and the policies
  able to see one -- **the earned Cataclysm dungeon opens in 50.4% and 48.3%
  of campaigns, the Last Stand is reached in 55.7% and 57.0%, and the empire
  loses 16.5 and 16.2 cities of 25.** An unattended Siege now empties a city
  in 25 / 39 / 55 / 70 days by size rather than 14 / 23 / 34 / 47. **The
  99.1% and 2.4% above describe the game that ruling moved away from.**

  **THOSE THREE WERE 51.2/48.9, 55.7/56.7 AND 16.4/16.1 UNTIL #1369**, which
  held the Cow Level at 7 instead of letting it drift up to 7.6 with the rest.
  The Siege weight is untouched by that change and the figures did not really
  move: over six disjoint blocks of 1,000 seeds a side rather than two, the
  earned route went 50.22% to 50.02%, cities lost 16.347 to 16.335 and Sieges
  created per campaign 10.633 to 10.677 -- **all under half of the 1.14 /
  0.16 / 0.11 block-to-block standard deviation**. `docs/DECISIONS.md` has the
  table. **THE GAP BETWEEN TWO BLOCKS IS NOT THE NOISE FLOOR**: the recorded
  Siege gap of 0.04 is one realised difference and is a quarter of the spread
  six blocks actually show.

  **RE-MEASURED AFTER #1338 AND #1333, WHICH IS WHAT REPLACES THE STALE
  FIGURES ABOVE.** 2,000 campaigns in two disjoint blocks of 1,000 seeds, at
  tier 1, `No tree`, `triage`, STATIC surges every 120 days ×5
  (`surge_dungeon_count` = 5, not the default of 4), resolve ratio 2.0,
  escalation 0.10 per 100 days, craft 12 days +4%, **with the Siege damage
  live**: the Last Stand is reached in **96.3%** of campaigns and won **1 in
  26.4** of those reached -- 73 wins in 1,926, or 1 in 25.2 of the 1,836
  entered -- at a mean 407 floors, and the earned Cataclysm dungeon opens in
  **8.1%** of campaigns and is won **41.6%** of the time. Those are not
  comparable with the 99.1% above: commit `b196cd9` now draws the active
  Cataclysms from the campaign seed with their count tied to the tier, so a
  seeded campaign does not replay a figure measured before it.

  **Barring a Cataclysm dungeon from rolling Cow Level did not measurably
  move any of that.** The same 2,000 seeds without the rule give 1 in 28.8
  reached, 67 wins against 73, and the same 96.3% and 407 floors -- six wins
  on a base of 67, against a standard deviation of about eight. 115 of those
  Last Stands were Cow Levels before the rule and none after, so the two
  conditions really are different and the nil result is a result. Issue
  #1333, and `docs/DECISIONS.md` carries the ruling.

A campaign that reaches the day cap has done neither, so it has **no
result** rather than a third outcome. The `stale%` column reads like one
and is not one, and nothing in the model ranks it against winning or
losing. Raising the cap resolves those campaigns, but dungeon power is
keyed to elapsed days, so a longer run is also a harder one and the cap is
not an independent variable. Issue #293, and
`experiments.warn_about_unresolved_campaigns` carries the measurement.

## The metrics that matter

- **triage%** — share of the player's free days on which two or more dungeons
  were about to detonate and there was only time for one. This is the health
  of the entire empire layer. Near zero means the strategy game is decoration.
- **policy spread** — win-rate gap between the best and worst way to play. If
  a careless player does as well as a careful one, the choices are fake.
- **idle%** — free days with nothing worth doing.

## Layout

```
cataclysm_sim/
  config.py     every tunable number; the five UNKNOWNs are tagged
  world.py      the empire graph
  engine.py     the day loop
  policies.py   five ways to play, from careless to optimal
experiments.py  the sweeps and the report
```

## Porting to Unreal

The rules in `engine.py` are deliberately plain arithmetic on plain structs so
they transliterate to C++ directly. `config.py` is the shape the eventual
`DataTable` assets should take. Keep this rig alive after the port — it is
much cheaper to re-tune here than in the editor.
