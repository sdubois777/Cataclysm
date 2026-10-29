# Cataclysm — empire layer tuning rig

A headless simulation of the strategy layer. It is **not** the game and it is
not Unreal code. It exists to derive the numbers the design documents leave
open, by playing thousands of campaigns and measuring what happens.

```bash
python experiments.py
```

Pure standard-library Python. No dependencies.

## A walk's days run when the player leaves, since 2026-10-24

**Nothing happens in the empire while the player is inside a dungeon.** The owner, 2026-10-24, verbatim: "all of the
resolutions and day/timer updates happen once a player leaves a dungeon." A walk still costs its days; they are owed,
and when the walk ends the clear is credited (or the death resolves) and then they run in order.
`TuningConfig.days_resolve_on_exit` carries it and defaults to on; off is the model as it was. `docs/DECISIONS.md`,
2026-10-24, has the rulings.

**Every other table and figure in this file was measured with it off.** `python measure_days_on_exit.py` measures both
halves on the same seeds: 0 to 499 for the guard cell and the undefended control, 0 to 299 for the triage
table. Measured on 2026-10-24, raw `TuningConfig()` but for the tier, the tree and the day cap;
"change" is the mean over seeds of on minus off in cities lost, with 1.96 standard errors of that paired difference.

The guard cell, Explorer on `triage` at tier 1, 500 campaigns, the default day cap:

| switch | won (95% interval) | cities lost | change in cities lost |
|---|---|---:|---|
| off | 51.6% (47.2 to 56.0) | 1.33 |  |
| on | 54.0% (49.6 to 58.3) | 1.33 | +0.00 +/- 0.19 |

A player on `triage`, 300 campaigns a cell, a day cap of 2,500:

| tree | tier | switch | won (95% interval) | lost | cities lost | change in cities lost | cleared | deaths | surges | from an empty board | days | Last Stand reached |
|---|---:|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|
| Explorer | 1 | off | 51.3% (45.7 to 56.9) | 48.7% | 1.37 |  | 24.62 | 4.52 | 8.46 | 0.09 | 628 | 0.3% |
| Explorer | 1 | on | 55.3% (49.7 to 60.9) | 44.7% | 1.43 | +0.07 +/- 0.28 | 24.53 | 4.56 | 8.61 | 0.32 | 604 | 0.3% |
| Explorer | 2 | off | 13.3% (9.9 to 17.6) | 86.7% | 6.76 |  | 20.60 | 9.84 | 9.21 | 0.00 | 559 | 31.7% |
| Explorer | 2 | on | 15.0% (11.4 to 19.5) | 85.0% | 6.12 | -0.64 +/- 0.52 | 20.05 | 9.59 | 8.76 | 0.00 | 540 | 31.7% |
| Explorer | 3 | off | 1.3% (0.5 to 3.4) | 97.7% | 13.39 |  | 15.51 | 8.30 | 11.62 | 0.00 | 646 | 95.0% |
| Explorer | 3 | on | 0.3% (0.1 to 1.9) | 98.3% | 13.54 | +0.15 +/- 0.66 | 16.11 | 8.23 | 11.86 | 0.00 | 648 | 94.7% |
| Explorer | 4 | off | 0.3% (0.1 to 1.9) | 99.3% | 15.57 |  | 4.23 | 3.06 | 10.30 | 0.00 | 524 | 99.7% |
| Explorer | 4 | on | 0.0% (0.0 to 1.3) | 99.7% | 15.55 | -0.02 +/- 0.34 | 4.29 | 3.11 | 10.32 | 0.00 | 528 | 100.0% |
| none | 1 | off | 40.7% (35.3 to 46.3) | 59.3% | 5.11 |  | 16.35 | 3.04 | 10.75 | 3.46 | 591 | 13.7% |
| none | 1 | on | 47.3% (41.8 to 53.0) | 52.7% | 4.63 | -0.48 +/- 0.37 | 15.30 | 2.86 | 10.93 | 4.56 | 528 | 8.3% |
| none | 2 | off | 23.7% (19.2 to 28.8) | 76.3% | 8.73 |  | 14.23 | 3.72 | 9.59 | 1.09 | 545 | 25.3% |
| none | 2 | on | 25.7% (21.1 to 30.9) | 74.3% | 8.49 | -0.24 +/- 0.58 | 13.46 | 3.74 | 9.59 | 1.68 | 505 | 25.3% |
| none | 3 | off | 4.0% (2.3 to 6.9) | 96.0% | 19.58 |  | 20.62 | 6.32 | 14.27 | 0.59 | 892 | 81.7% |
| none | 3 | on | 5.3% (3.3 to 8.5) | 94.7% | 19.98 | +0.40 +/- 0.69 | 21.73 | 6.99 | 15.05 | 0.96 | 946 | 79.0% |
| none | 4 | off | 1.7% (0.7 to 3.8) | 98.3% | 21.46 |  | 18.14 | 5.92 | 13.16 | 0.17 | 848 | 90.7% |
| none | 4 | on | 3.3% (1.8 to 6.0) | 96.7% | 21.95 | +0.50 +/- 0.54 | 19.47 | 6.42 | 13.88 | 0.45 | 890 | 90.0% |

With nobody entering any dungeon the two halves are identical campaign by campaign, so the undefended table below does
not move. **The triage table below was measured on 2026-10-13**, before the 2026-10-16 retune as well as before this
switch, which is why its rows differ from the switch-off rows above.

## An ordinary dungeon leaves after it hits its city, and nothing has been retuned for it

**Since 2026-10-12 an ordinary dungeon that hits its city undefeated leaves the
map.** The owner ruled it on 2026-10-08, verbatim: "Ordinary dungeons will do
their city/population damage, and then despawn." Until that date it stayed, with
its timer full again, and hit the same city every time the timer came round.
`TuningConfig.dungeon_persists_after_resolve` carries the rule and now defaults
to `False`; `True` is the model as it was. A Quest dungeon and a Fallen City
dungeon do not change. The dungeon whose hit fells its city is absorbed by the
fall, as before. A dungeon carrying a Siege leaves like any other and its daily
damage stops. A dungeon the player dies in hits once and leaves. A dungeon that
leaves is not a clear and adds nothing to the Cataclysm boss's floors.

**THE GAME HAS THIS RULE SINCE ITS LAYER OF 2026-10-13.** It went into the
game together with the retuned surge, 5 dungeons every 90 days.
`tools/tests/test_an_ordinary_dungeon_leaves_in_the_model_and_in_the_game.py`
holds the two halves together.

**NOTHING HAS BEEN RETUNED, AND AN EMPIRE NOBODY DEFENDS NO LONGER ALWAYS
LOSES.** No constant moved with the default. `experiments.py` has not been run
since. Every other table and figure in this file, in `docs/DECISIONS.md`, in
`docs/Cataclysm_GDD_v2.md` and in the comments of this package dated before
2026-10-12 was measured with the dungeon staying; set
`dungeon_persists_after_resolve=True` to reproduce one. Four analysis scripts
hold that setting `True` in their `base_config` for that reason:
`analyse_tier_throughput.py`, `analyse_surge_cadence.py`,
`analyse_board_empty_surge.py` and `analyse_siege_dose.py`. The other campaign
scripts run at the default and their quoted figures are from before the change.

**Nobody enters any dungeon.** 500 campaigns a cell, seeds 0 to 499, raw
`TuningConfig()` defaults but for the tier, no empire tree, 2,000 days. "Lost"
is the first day `Empire.pillar_exposed()` answers true. Cities lost are counted
on that day, or on day 2,000 where it never came. "Hits" are the times a dungeon
took from a city.

| tier | rule | path to the Pillar lost (95% interval) | day lost: median | quartiles | cities lost | surges | from an empty board | hits |
|---:|---|---|---:|---|---:|---:|---:|---:|
| 1 | stays | 100.0% (99.2 to 100.0) | 883 | 746 to 1036 | 17.63 | 14.23 | 0.00 | 189.07 |
| 1 | leaves | 15.0% (12.1 to 18.4) | 1443 | 1046 to 1734 | 5.56 | 18.29 | 1.08 | 58.58 |
| 2 | stays | 100.0% (99.2 to 100.0) | 641 | 542 to 732 | 17.93 | 11.62 | 0.00 | 196.54 |
| 2 | leaves | 37.6% (33.5 to 41.9) | 1516 | 1112 to 1799 | 9.70 | 18.26 | 0.43 | 92.26 |
| 3 | stays | 100.0% (99.2 to 100.0) | 510 | 438 to 582 | 17.92 | 10.24 | 0.00 | 190.99 |
| 3 | leaves | 81.0% (77.3 to 84.2) | 1435 | 1108 to 1707 | 13.03 | 17.05 | 0.27 | 108.82 |
| 4 | stays | 100.0% (99.2 to 100.0) | 447 | 378 to 515 | 18.14 | 9.55 | 0.00 | 192.03 |
| 4 | leaves | 95.8% (93.7 to 97.2) | 1260 | 963 to 1539 | 14.13 | 15.39 | 0.18 | 117.47 |

**A player on the `triage` policy.** 300 campaigns a cell, seeds 0 to 299, raw
`TuningConfig()` defaults but for the tier and the tree, the day cap of 2,500.
**Measured on 2026-10-13, before the 2026-10-16 retune and before the 2026-10-24
switch that runs a walk's days when the player leaves**; the section above has
that switch measured off and on. Explorer is `TREE_EXPLORER_AS_DESIGNED`. These are not the settings
`experiments.py` calibrates, so the rows cannot be set beside the report's
tables. "Lost" includes a death in the earned Cataclysm dungeon.

| tree | tier | rule | won (95% interval) | lost | cities lost | dungeons cleared | hits | deaths | surges | from an empty board | days | idle days (share of free days) | Last Stand reached |
|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| Explorer | 1 | stays | 36.3% (31.1 to 41.9) | 44.0% | 3.05 +/- 0.16 | 55.99 | 34.20 | 8.60 | 17.24 | 2.64 | 1538 | 0.15 (0.2%) | 5.7% |
| Explorer | 1 | leaves | 38.0% (32.7 to 43.6) | 40.3% | 1.55 +/- 0.08 | 44.30 | 20.27 | 6.15 | 17.06 | 8.81 | 1542 | 0.15 (0.2%) | 2.3% |
| Explorer | 2 | stays | 1.7% (0.7 to 3.8) | 96.0% | 15.75 +/- 0.24 | 27.60 | 40.54 | 12.41 | 14.02 | 0.04 | 1000 | 0.88 (0.5%) | 90.7% |
| Explorer | 2 | leaves | 4.3% (2.5 to 7.3) | 94.7% | 14.03 +/- 0.29 | 27.28 | 37.53 | 12.63 | 15.11 | 0.19 | 1220 | 26.76 (5.4%) | 84.0% |
| Explorer | 3 | stays | 0.3% (0.1 to 1.9) | 98.3% | 16.44 +/- 0.25 | 13.19 | 32.26 | 6.98 | 11.97 | 0.01 | 780 | 31.55 (6.5%) | 100.0% |
| Explorer | 3 | leaves | 0.3% (0.1 to 1.9) | 99.0% | 16.69 +/- 0.23 | 11.05 | 33.55 | 6.29 | 12.68 | 0.01 | 869 | 110.77 (31.0%) | 100.0% |
| Explorer | 4 | stays | 0.0% (0.0 to 1.3) | 100.0% | 17.91 +/- 0.22 | 1.77 | 24.89 | 1.84 | 10.82 | 0.00 | 662 | 435.48 (81.8%) | 100.0% |
| Explorer | 4 | leaves | 0.0% (0.0 to 1.3) | 99.7% | 17.70 +/- 0.23 | 1.28 | 24.79 | 1.53 | 10.85 | 0.00 | 685 | 486.39 (90.0%) | 100.0% |
| none | 1 | stays | 27.0% (22.3 to 32.3) | 58.0% | 5.27 +/- 0.24 | 56.29 | 89.34 | 6.39 | 18.20 | 8.63 | 1499 | 0.04 (0.1%) | 7.3% |
| none | 1 | leaves | 40.3% (34.9 to 46.0) | 57.3% | 1.00 +/- 0.07 | 31.59 | 35.74 | 4.11 | 17.60 | 14.02 | 1019 | 0.18 (0.7%) | 1.0% |
| none | 2 | stays | 8.7% (6.0 to 12.4) | 91.3% | 18.27 +/- 0.37 | 46.71 | 185.01 | 9.35 | 17.09 | 0.99 | 1316 | 0.00 (0.0%) | 73.3% |
| none | 2 | leaves | 25.7% (21.1 to 30.9) | 68.0% | 3.00 +/- 0.17 | 29.44 | 62.05 | 7.18 | 15.34 | 8.85 | 1094 | 11.46 (5.0%) | 7.0% |
| none | 3 | stays | 1.0% (0.3 to 2.9) | 99.0% | 23.10 +/- 0.16 | 37.74 | 219.19 | 8.79 | 16.05 | 0.11 | 1138 | 0.00 (0.0%) | 100.0% |
| none | 3 | leaves | 3.3% (1.8 to 6.0) | 56.7% | 12.05 +/- 0.35 | 50.04 | 132.75 | 16.26 | 24.34 | 6.09 | 2018 | 194.77 (49.0%) | 54.7% |
| none | 4 | stays | 0.0% (0.0 to 1.3) | 100.0% | 23.53 +/- 0.11 | 30.82 | 221.40 | 7.52 | 14.51 | 0.03 | 973 | 0.00 (0.0%) | 100.0% |
| none | 4 | leaves | 1.0% (0.3 to 2.9) | 85.7% | 17.58 +/- 0.36 | 41.22 | 147.30 | 14.24 | 22.68 | 3.58 | 1836 | 306.68 (69.4%) | 90.7% |

`docs/DECISIONS.md`, 2026-10-12, has the same for `lane_aware`, and a first look
at which single constant would make the undefended empire lose again.

## A death resolves its dungeon, and every figure below was measured before it did

**Since 2026-10-12 a death inside an ordinary dungeon resolves that dungeon at
once**, which the game has done since 2026-10-10 (issue #41). The dungeon's
consequence lands on its city exactly as when its timer runs out, the dungeon
then leaves the map (it stayed with its timer full again until the rule in the
section above; the measurement below was taken while it stayed), the player
leaves, and `death_day_cost` days are then charged.
A death in a Quest dungeon or a Fallen City dungeon costs the days and takes
nothing from any city. A death in the Cataclysm dungeon ends the run, as before.
`TuningConfig.death_resolves_dungeon` carries it and defaults to on. Before that
date a death here cost days and touched no city.

**EVERY OTHER TABLE AND FIGURE IN THIS FILE WAS MEASURED WITH THAT RULE ABSENT,
AND NONE OF THEM HAS BEEN RE-MEASURED.** `experiments.py` has not been run since
the change. Set `death_resolves_dungeon=False` to reproduce one of them. The same
is true of every campaign figure in `docs/DECISIONS.md`, in
`docs/Cataclysm_GDD_v2.md` and in the comments of this package dated before
2026-10-12.

**WHAT WAS MEASURED, AND IT IS A SMALL RUN.** 500 campaigns a cell, seeds 0 to
499, the same seeds with the setting off and on, at raw `TuningConfig()` defaults
but for the tier and the tree. Those are not the settings `experiments.py`
calibrates (it uses 5 dungeons a surge, among others), so these rows cannot be
set beside the report's tables. "Change" is the mean over seeds of on minus off
in cities lost, with 1.96 standard errors of that paired difference. "Resolved"
with the setting on includes the dungeons a death resolved.

| policy | tree | tier | cities lost, off | on | change | won %, off | on | deaths, off | on | resolved, off | on |
|---|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| `triage` | No tree | 1 | 5.31 | 5.32 | +0.00 +/- 0.34 | 26.8 | 27.6 | 6.47 | 6.52 | 87.0 | 89.1 |
| `triage` | No tree | 2 | 18.15 | 18.29 | +0.14 +/- 0.43 | 7.4 | 8.4 | 9.68 | 9.38 | 183.8 | 183.6 |
| `triage` | No tree | 3 | 23.06 | 23.05 | -0.01 +/- 0.25 | 0.6 | 0.6 | 9.04 | 8.80 | 220.7 | 219.0 |
| `triage` | No tree | 4 | 23.59 | 23.54 | -0.05 +/- 0.12 | 0.0 | 0.2 | 7.68 | 7.64 | 222.2 | 221.9 |
| `triage` | Explorer maxed (as designed) | 1 | 2.36 | 2.97 | **+0.61 +/- 0.22** | 35.4 | 35.0 | 7.98 | 8.51 | 29.9 | 34.4 |
| `triage` | Explorer maxed (as designed) | 2 | 15.78 | 15.77 | -0.01 +/- 0.43 | 3.2 | 2.2 | 14.04 | 12.49 | 39.3 | 40.8 |
| `triage` | Explorer maxed (as designed) | 3 | 16.86 | 16.85 | -0.02 +/- 0.42 | 0.6 | 0.4 | 7.82 | 7.09 | 33.2 | 33.2 |
| `triage` | Explorer maxed (as designed) | 4 | 17.66 | 17.71 | +0.04 +/- 0.19 | 0.2 | 0.0 | 2.09 | 1.86 | 24.5 | 24.6 |
| `lane_aware` | No tree | 1 | 3.54 | 4.11 | **+0.57 +/- 0.27** | 27.0 | 24.8 | 6.62 | 6.76 | 70.2 | 76.4 |
| `lane_aware` | No tree | 2 | 19.32 | 19.35 | +0.03 +/- 0.45 | 6.4 | 5.4 | 11.65 | 11.04 | 194.0 | 194.1 |
| `lane_aware` | No tree | 3 | 23.16 | 23.08 | -0.09 +/- 0.21 | 0.4 | 0.6 | 9.98 | 9.62 | 219.2 | 221.0 |
| `lane_aware` | No tree | 4 | 23.48 | 23.46 | -0.02 +/- 0.15 | 0.6 | 0.2 | 8.58 | 8.24 | 219.2 | 220.5 |
| `lane_aware` | Explorer maxed (as designed) | 1 | 2.34 | 2.89 | **+0.55 +/- 0.23** | 36.8 | 36.6 | 7.90 | 8.21 | 29.3 | 33.6 |
| `lane_aware` | Explorer maxed (as designed) | 2 | 16.04 | 15.91 | -0.13 +/- 0.44 | 2.8 | 2.0 | 14.63 | 13.29 | 41.3 | 42.6 |
| `lane_aware` | Explorer maxed (as designed) | 3 | 16.88 | 16.86 | -0.01 +/- 0.44 | 0.4 | 0.2 | 7.88 | 7.30 | 33.2 | 34.2 |
| `lane_aware` | Explorer maxed (as designed) | 4 | 17.64 | 17.68 | +0.04 +/- 0.19 | 0.2 | 0.0 | 2.09 | 1.86 | 24.5 | 24.5 |

**WHAT THAT SHOWS AND WHAT IT DOES NOT.** In three of the sixteen cells, all at
tier 1, a campaign loses a little over half a city more with the rule on, and
the change is outside its interval. In the other thirteen the change is inside
it. No cell's win rate moved by more than 2.2 points, on 500 campaigns, which is
not a sample that separates a 2 point change. Sixteen cells were looked at, so
one interval missing by chance would not be surprising; three, all in the same
direction and at the same tier, is more than that, and it is still three cells
of one small run. `docs/DECISIONS.md`, 2026-10-12, has the script's printed
output.

**NO POLICY WEIGHS WHAT A DEATH NOW COSTS.** Every policy refuses a dungeon
whose death chance is above `death_risk_tolerance` and treats every dungeon
under it alike. That was written when a death cost days alone, and it was not
changed with this rule.

## Which difficulty tier the results are from

**Every section of `experiments.py` runs at difficulty tier 1, except section 7,
which runs at tier 1 and tier 8.** The game has eight tiers. A number from this
report is a tier 1 number unless its own heading says otherwise, and the report
prints that in its header.

This matters because tier width — the gap between one tier's maximum player
Power Score and the tier below — multiplies every weighted term of the Enemy
Score formula. It runs 385, 498, 625, 717, 853, 979, 1063, 1207 across the eight
tiers, so the relation between player power and enemy power is not the same at
both ends.

**THE CLAIM THAT THE PRESET ORDERING DIFFERS BETWEEN TIER 1 AND TIER 8 IS GONE
FROM THIS FILE, BECAUSE THERE IS NO TIER 8 ORDERING TO COMPARE.** Re-measured on
`998d758`, which is the first run of the report since
[#1386](https://github.com/sdubois777/Cataclysm/issues/1386),
[#1397](https://github.com/sdubois777/Cataclysm/issues/1397),
[#1383](https://github.com/sdubois777/Cataclysm/issues/1383) and
[#1406](https://github.com/sdubois777/Cataclysm/issues/1406) changed what it
measures. Issue
[#1404](https://github.com/sdubois777/Cataclysm/issues/1404) is where this was
tracked.

**MEASURED BEFORE 2026-10-13, WHEN THE MODEL'S DEFAULT SURGE WAS 4 DUNGEONS
EVERY 120 DAYS.** It is 5 every 90 since that date. Neither this table nor any
other figure in this file was re-measured for that, for a Quest dungeon's timer
hitting its city, or for a hit being sized by natural floors, all three of
2026-10-13; `docs/DECISIONS.md` of that date has what was measured.

**THE CONDITIONS.** 150 campaigns per cell, the `triage` policy, static surges,
resolve timer 1.6 days per floor, 120 days between surges, dungeon power +0.22
per 100 days, craft 12 days for +4% of a tier width, day cap 2,500, and
`surge_on_empty_board` on. **Active Cataclysms follow the tier**: 1 at tier 1 and
8 at tier 8. A figure from section 7 without those is not a figure.

**MEASURED BEFORE A DEATH RESOLVED ITS DUNGEON, like the two tables after it.** See the
section at the top of this file.

| dungeons per surge | tier 1 | tier 8 | what the report printed |
|---|---|---|---|
| 5, the size sections 0 and 2 calibrated | 5 presets in 4 groups | 5 presets in **1 group** | NO CONCLUSION. Every preset ties at tier 8 |
| 7, the second size | 6 presets in 6 groups | 6 presets in 2 groups | The ordering DIFFERS between tiers |

**MORE CAMPAIGNS DO NOT PRODUCE ONE, AND THAT IS MEASURED RATHER THAN ARGUED.**
Section 7 was re-run at four times the sample, everything else held:

| campaigns per cell | tier 1 closest pair | tier 8 closest pair |
|---|---|---|
| 150 | gap 5.3, tolerance 9.3, tied | gap 0.0, tolerance 1.5, tied |
| 600 | gap 8.5, tolerance 5.2, **apart** | gap 0.0, tolerance 0.4, tied |

The extra campaigns bought a full tier 1 ordering — the tie between `Explorer
maxed (as designed)` and `No tree` broke — and bought nothing at tier 8, where
the tolerance fell to about a quarter and separated nothing.

**WHY IT SEPARATED NOTHING, FROM THE EXACT COUNTS RATHER THAN THE ROUNDED
COLUMNS.** The report prints win, loss and win-minus-loss to the nearest whole
number and the closest gap to one decimal, so "0% win" and "gap 0.0" cannot be
read as exact. Measured directly at tier 8, surge size 5, the five ranked
presets:

| campaigns per cell | campaigns won, every preset | margins |
|---|---|---|
| 150 | **0 of 150** | all five exactly -100.000000 |
| 600 | **0 of 600** | four exactly -100.000000; `Explorer via floors (-25 floors)` is -99.833333 |

**No preset has won a single tier 8 campaign in 600 attempts.** The one
difference at 600 is a campaign that reached the day cap without winning or
losing, not a win, and the closest pair is still separated by exactly
0.000000 points because the other four remain identical. **A shrinking tolerance
never separates two identical numbers.**

**THAT IS ALSO A STATEMENT ABOUT THE GAME AND NOT ONLY ABOUT THE SAMPLE**, and
it has its own issue rather than living inside this one:
[#1441](https://github.com/sdubois777/Cataclysm/issues/1441). The sixth preset,
`Architect maxed (as designed)`, wins nothing either — it is left out of the
ranking because 82% of its campaigns reach the day cap, which is the absence of a
result and not a better one. And `Explorer maxed (as designed)` clears **zero
floors** at tier 8 while spending 83% of its free days facing two or more
dungeons about to detonate, so that player never enters a dungeon at all.

**WHAT A LARGER SAMPLE WOULD COST.** Section 7 at 600 campaigns per cell took
2,462 seconds for 14,400 campaigns, 171 ms each, on a machine also running the
full report. Cost is linear in the sample, so the same section at 150 per cell is
a quarter of that, about 615 seconds. **`docs/DECISIONS.md` records 225 seconds
for it on 2026-09-05 and that no longer holds** — the difference is some mixture
of machine contention and `surge_on_empty_board` lengthening campaigns, and this
run cannot separate the two. On the measured figure, 1,250 campaigns per cell is
about 85 minutes and 30,000 campaigns for section 7 alone, which is more
campaigns than the whole rest of the report, and it would change none of the
above.

**THE SURGE-SIZE-7 "DIFFERS" IS NOT THE OLD CLAIM.** It differs because the
Architect preset is ranked at surge size 7 and left out at surge size 5: 82% of
its tier 8 campaigns ran out of days at size 5 against 47% at size 7, and the
cut-off is `UNRESOLVED_WARNING_PERCENT`, 50%. What differs is that tier 1 has an
order and tier 8 has one preset ahead of a five-way tie — not two orderings of
the same presets.

**THE 5.77 POINTS THIS FILE USED TO QUOTE WAS THE TOLERANCE FOR A DIFFERENT
TABLE**, and that is worth stating on its own because it was wrong whatever the
ordering did. `experiments.win_rate_noise(150)` is 5.77, and it governs the win
rate ranking section 7 prints **as a second opinion**. The ordering is ranked on
win rate MINUS loss rate, whose tolerance is `margin_noise` — 11.55 points at 150
campaigns as a cap, and a tighter per-pair figure in practice. The distinction is
deliberate: `margin_noise`'s docstring opens "WHY IT IS NOT `win_rate_noise`.
Issue [#294](https://github.com/sdubois777/Cataclysm/issues/294)", and the report
prints `ITS TOLERANCE IS NOT THE WIN RATE'S 5.8 POINTS` directly above the
ordering. This file contradicted a sentence printed in the output it described.

**WHAT WAS WRONG WITH THE OLD CLAIM BESIDES THAT.** It was measured when both
tiers ran against one fixed Cataclysm, Demonic, so it compared two power scales
rather than two tiers — issue
[#1338](https://github.com/sdubois777/Cataclysm/issues/1338), and the next
section. It also used `TREE_EXPLORER_AS_DESIGNED` when that preset held a single
tier 1 number. It no longer does: `floors_added` is 40 at tier 1 and 180 at tier
8, and `days_removed` is 10 and 30, so a preset is now correct at every tier.

**A LARGER SAMPLE DOES CHANGE AN ANSWER ELSEWHERE IN SECTION 7, SO DO NOT READ
THE ABOVE AS "THE SAMPLE NEVER MATTERS".** At tier 1 the same four-times-larger
run turned the Explorer branch's comparison against taking no empire tree from
"+2.0 points, cannot be told apart" into "+5.8 points, BETTER". Those are nested
samples — a cell's seeds run from 0 upward — so the second is a better estimate
of the same quantity, not a second opinion. The game is balanced around a player
fully invested in that branch, and at the report's own sample size the report
cannot tell that player from one who allocated no points. That is issue
[#1437](https://github.com/sdubois777/Cataclysm/issues/1437), and it is the
failure that produced issue
[#5](https://github.com/sdubois777/Cataclysm/issues/5).

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

**EVERY CAMPAIGN FIGURE MEASURED BELOW TIER 8 BEFORE 2026-09-06 IS STALE.**
Until #1338 the count was a flat 1 whatever the tier, and the set was the first
N of a fixed tuple, so every campaign the model ever ran faced Demonic and
nothing else. Demonic is the only one of the eight that ignores the frontier, so
every lane-based and frontier-pressure figure was measured against the one
Cataclysm that does not respect lanes. Measured on #1338 at 250 campaigns per
cell, tier 1, the `triage` policy, at surge size 4 -- the raw `TuningConfig`
default, not the calibrated 5 this report uses: which Cataclysm is active swings
the win rate by 13.6 points and flips the ordering between empire tree branches.

**THAT 13.6 IS NOT COMPARED AGAINST A MEASURED FLOOR, AND IT USED TO SAY IT
WAS.** This file quoted "4.5 points" beside it. That figure is
`experiments.win_rate_noise(250)`, which is `100 x sqrt(2) x sqrt(0.25 /
trials)` -- a worst-case binomial bound at a 50% win rate, a function of the
sample size and of nothing else. It is 4.5 at 250 campaigns whatever the model
does, so it cannot confirm or deny anything about a change. The empirically
measured floors are far tighter, because the real win rates here are 6% to 29%
rather than 50%; `docs/DECISIONS.md` records them at 16 disjoint blocks of 250.
13.6 points clears both, so the finding stands -- it is the reasoning that was
wrong.

**Tier 8 is unaffected, but say it precisely.** All eight are active there under
either scheme, so the modifier pool is the same 116 entries for every seed and
the mix of attack patterns is identical — measured, not assumed. What the draw
still changes at tier 8 is the ORDER the eight sit in, and `_surge` picks a
Cataclysm by index, so one seeded campaign will not replay identically. The
distribution a tier 8 cell samples from has not moved; the particular sample has.

**Nothing in this file is now marked stale for that reason.** The claim that the
preset ordering in section 7 differs between tier 1 and tier 8 was the last one,
and it has been removed rather than re-stated — the section above says what the
report measured on `998d758` and why no tier 8 ordering exists to compare. Issue
[#1404](https://github.com/sdubois777/Cataclysm/issues/1404). The Last Stand
figures in the map section below **have** been re-measured on `e8b33c2` and carry
their conditions and the history they replace; note that they predate
`surge_on_empty_board`, which is a separate matter recorded with them. Issue
[#1358](https://github.com/sdubois777/Cataclysm/issues/1358) is where both were
tracked; it also asked for a sweep of `docs/DECISIONS.md`, which has now been
done -- the older entries there are annotated where their figures are
load-bearing rather than rewritten, because that file records what was decided
on what evidence at the time.

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
- **A death in an ordinary dungeon resolves it at once**, through the same call
  its timer running out makes, and the days a death costs are charged after
  that. It is the game's rule, built there for issue #41.
  `TuningConfig.death_resolves_dungeon` turns it off, to reproduce a figure
  measured before 2026-10-12; no sweep moves it. Whether the resolved dungeon
  then stays is `dungeon_persists_after_resolve`, a separate rule.
- **An ordinary dungeon leaves the map after it hits its city**, by its timer
  or by a death. The owner ruled it on 2026-10-08.
  `TuningConfig.dungeon_persists_after_resolve` is `False`; `True` keeps the
  dungeon with its timer full again, which was the rule here until 2026-10-12
  and in the game until 2026-10-13. No sweep moves it. A Quest dungeon and a
  Fallen City dungeon stay as they were.
- **A city falling triggers a surge**, and optionally advances the escalation.
- **An empty board triggers a surge too, however it emptied.** The project owner
  ruled on 2026-09-07, verbatim, "Anytime there are no longer dungeons on the
  board, a surge happens." A dungeon the player cleared and a dungeon that
  detonated undefeated both count; a narrower "only a clear fires" version was
  proposed and overruled. `TuningConfig.surge_on_empty_board` carries it and
  issue #1406 has the reasoning.

  **SINCE 2026-10-12 THE BROAD AND NARROW VERSIONS ARE DIFFERENT RULES**, and
  that is a fact about `dungeon_persists_after_resolve` rather than about either
  of them. It is `False`, so an ordinary dungeon that hits its city undefeated
  leaves the board, and the last one leaving empties it. Until that date it was
  `True`, a hit could not empty the board, and the two versions behaved
  identically: measured then over 20 campaigns on the `triage` policy, 126
  empty-board surges fired and all 126 followed the player clearing the last
  dungeon. The stalling trade the narrow version would allow — let the last
  dungeon hit, pay one city's damage, buy a quiet stretch — is real now, and the
  broad rule, which is the one built, is what answers it.

  **THE 120-DAY CLOCK STAYS AND WHICHEVER COMES FIRST WINS.** A trigger that
  replaced the timer would give the weakest player *fewer* surges than today,
  because a Quest dungeon and a Fallen City dungeon never leave the board on
  their own — `_resolve` relocates the first and refreshes the second — so a
  player who ignores one keeps the board permanently non-empty.

  **`surge_interval_min` is the only brake on it**, and it is the same constant
  that floors the gap under the escalating surge modes, so moving it moves both.
  `sim/analyse_board_empty_surge.py` sweeps it.
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
a dungeon for a fixed number of days. Combat is not played out: an attempt
either clears the dungeon or, with the chance `Simulation.death_chance` gives,
kills the player. A clear costs **time**, which is the design's stated primary
resource. A death costs `death_day_cost` days and, in an ordinary dungeon,
resolves that dungeon on its city. **This paragraph said "Combat is assumed
won — the only cost of a dungeon is time" until 2026-10-12**, which the death
roll had already made untrue and this rule made more so.

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
  was. It is a fight. Issue #5 measured the original; issue #1286 asked
  whether a near-unwinnable fight is the intended shape, and the owner ruled
  on 2026-09-05 that it is. **That ruling is superseded**: on 2026-10-29 the
  owner answered "Accept today's balance", which is the table below -- a
  Last Stand reached in 1.9% of campaigns and cleared about 1 in 7 of those.

  **MEASURED 2026-10-29 OVER 6,000 CAMPAIGNS**, on `17366a34d`, in six
  disjoint blocks of 1,000 seeds, at tier 1, `No tree`, `triage`, STATIC
  surges every 120 days x5 (the default is 5 every 90 days since
  2026-10-13), resolve floor ratio 2.0, escalation 0.10 per 100 days, craft
  12 days +4%, with the Siege damage live and the policies able to see one,
  and with every other rule as `TuningConfig` shipped it that day -- the
  board-empty surge, `death_resolves_dungeon`, an ordinary dungeon leaving
  after it hits its city, a Quest dungeon's timer hitting its city, and a hit
  sized by all of a dungeon's floors. Measured with
  `analyse_siege_dose.py`'s `measure` imported; that script's own `BASE`
  still holds the last three of those rules at their September values, so
  running the script as it ships does not reproduce this table. Issue
  [#2615](https://github.com/sdubois777/Cataclysm/issues/2615).

  **EVERY ROW BELOW IS A PLAYER WITH NO EMPIRE TREE.** The owner has ruled
  that the game is balanced around a player fully invested in the Explorer
  tree; **nothing on record measures that player at these settings**, so do
  not read these as describing them.

  | | Value | Block sd | Count |
  |---|---:|---:|---|
  | Last Stand reached | **1.9%** | 0.20 | 116 of 6,000 |
  | Cleared, per Last Stand reached | **1 in 6.8** | 8.75 pts | 17 of 116 |
  | Earned Cataclysm dungeon opens | **99.0%** | 0.32 | 5,937 of 6,000 |
  | Earned dungeon won | **49.0%** | 0.62 | 2,907 of 5,937 |
  | Campaign won at all | **48.7%** | 0.60 | 2,924 of 6,000 |
  | Cities lost, of 25 | **1.87** | 0.10 | |
  | Sieges created per campaign | **6.62** | 0.24 | counted at creation |

  **THE CLEAR RATE IS THE ONE TO DISTRUST, FAR MORE THAN BEFORE.** Its block
  standard deviation is 8.75 points against a figure of 14.7, which is 60%
  of it: the six blocks run 1 in 3.7 to 1 in 20 on 1 to 6 clears apiece, and
  the pooled 95% interval is 1 in 4.5 to 1 in 10.7. A Last Stand is reached
  in 116 campaigns of 6,000, so it rests on 17 clears. Read it as "about one
  in seven" and nothing narrower. Everything else in that table is firm.

  **THE FALL FROM 45.9% REACHED IS ONLY PARTLY ATTRIBUTED.** The same six
  blocks with those last three rules at their September values give 15.5%
  reached, so the three account for 13.6 of the 44.0 points; the other 30.4
  are not attributed to any change. The owner accepted this balance on
  2026-10-29, verbatim "Accept today's balance", superseding the ruling of
  2026-09-05 that a collapse should be near-fatal.

  **THE GAP BETWEEN TWO BLOCKS IS NOT THE NOISE FLOOR.** Two blocks give one
  realised difference, not an estimate of a spread. Measured on the six of
  2026-09-07, which this table carried until issue #2615:
  the A-to-B gap runs from **0.25 times** the six-block standard deviation
  (Last Stand reached) to **2.60 times** it (cities lost). On the six blocks
  taken at `e8b33c2` earlier the same day the same range was 0.67 to 1.81,
  and under the sub-type table #1369 replaced it came in at 0.38. **Three
  measurements, three different answers, none of them a floor.** It is one draw and it
  can land anywhere. Anything justified by clearing a two-block gap is
  justified by nothing; that is issue #1379, and
  `sim/analyse_quest_move_chance.py` still computes a threshold that way and
  prints a categorical conclusion from it.

  **WHAT THOSE FIGURES REPLACE, AND WHY EACH ONE MOVED.** Every row is tier
  1 at the settings above; only what the row itself names differs. They are
  kept because each was the evidence a decision was made on.
  `docs/Cataclysm_GDD_v2.md` carries the same table and `docs/DECISIONS.md`
  the entry behind each row.

  | Measured | LS reached | Cleared per LS reached | Earned opens | Earned won |
  |---|---:|---:|---:|---:|
  | 2026-09-05, #1286, 400 campaigns | 13.5% | 1 in 54 | -- | 57% |
  | 2026-09-06, 10,000 campaigns, Siege damage OFF | 27.7% | 1 in 84 | 74.7% | 40.4% |
  | the same, Siege damage ON at weight 15, growth 10 | 99.1% | 1 in 19 | 2.4% | -- |
  | after #1338 and #1333, Siege still 15 and 10 | 96.3% | 1 in 26.4 | 8.1% | 41.6% |
  | after #1349, before #1369 | 55.7 / 56.7% | 1 in 34.8 / 31.5 | 51.2 / 48.9% | -- |
  | 2026-09-06, on `e8b33c2`, six blocks of 1,000 | 56.4% | 1 in 34.5 | 50.0% | 38.3% |
  | 2026-09-07, on `a71ee1f`, six blocks of 1,000 | 45.9% | 1 in 24.6 | 54.9% | 40.4% |
  | **2026-10-29, today, on `17366a34d`, the rules of that date** | **1.9%** | **1 in 6.8** | **99.0%** | **49.0%** |

  **The first three rows were measured against Demonic and nothing else**,
  which is what the model did until #1338; the section above says why that
  matters. **The two largest moves are both the Siege, in opposite
  directions**: modelling what one does to a city (#1345) took the earned
  route from 74.7% of campaigns to 2.4%, and the owner's ruling on #1349 --
  verbatim "Halve the rate and cut the growth", spawn weight 15 to 7.5 and
  `siege_damage_growth_per_day` 10 to 2.5, the two 1% shares of a city's
  maximum left alone -- put it back to about half. An unattended Siege now
  empties a city in 25 / 39 / 55 / 70 days by size rather than 14 / 23 / 34
  / 47.

  **Barring a Cataclysm dungeon from rolling Cow Level did not measurably
  move any of it.** The same 2,000 seeds without the rule gave 1 in 28.8
  reached against 1 in 26.4, 67 wins against 73, and the same 96.3% and 407
  floors -- six wins on a base of 67, against a standard deviation of about
  eight. 115 of those Last Stands were Cow Levels before the rule and none
  after, so the two conditions really are different and the nil result is a
  result. Issue #1333, and `docs/DECISIONS.md` carries the ruling. That
  comparison has not been repeated since #1349 moved everything under it.

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
  policies.py   seven ways to play, from careless to optimal
experiments.py  the sweeps and the report
```

## Porting to Unreal

The rules in `engine.py` are deliberately plain arithmetic on plain structs so
they transliterate to C++ directly. `config.py` is the shape the eventual
`DataTable` assets should take. Keep this rig alive after the port — it is
much cheaper to re-tune here than in the editor.
