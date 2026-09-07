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

**THE CONDITIONS.** 150 campaigns per cell, the `triage` policy, static surges,
resolve timer 1.6 days per floor, 120 days between surges, dungeon power +0.22
per 100 days, craft 12 days for +4% of a tier width, day cap 2,500, and
`surge_on_empty_board` on. **Active Cataclysms follow the tier**: 1 at tier 1 and
8 at tier 8. A figure from section 7 without those is not a figure.

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
- **A city falling triggers a surge**, and optionally advances the escalation.
- **An empty board triggers a surge too, however it emptied.** The project owner
  ruled on 2026-09-07, verbatim, "Anytime there are no longer dungeons on the
  board, a surge happens." A dungeon the player cleared and a dungeon that
  detonated undefeated both count; a narrower "only a clear fires" version was
  proposed and overruled. `TuningConfig.surge_on_empty_board` carries it and
  issue #1406 has the reasoning.

  **AT THESE DEFAULTS THE BROAD AND NARROW VERSIONS BEHAVE IDENTICALLY**, and
  that is a fact about `dungeon_persists_after_resolve` rather than about either
  of them. It is `True`, so a Basic dungeon that detonates undefeated *stays* on
  the board with a refreshed timer and cannot empty it. Measured over 20
  campaigns on the `triage` policy: 126 empty-board surges fired and all 126
  followed the player clearing the last dungeon. The broad rule is still what is
  built — it is what the owner ruled, and it stays correct if that setting is
  ever turned off — but nothing on record may claim the narrow version allows a
  stalling trade that today it does not.

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
  was. It is a fight, and it is very nearly always lost. Issue #5 measured
  the original; whether a near-unwinnable fight is the intended shape is
  issue #1286, and the owner ruled that it is.

  **MEASURED 2026-09-07 OVER 6,000 CAMPAIGNS** in six disjoint blocks of
  1,000 seeds, at tier 1, `No tree`, `triage`, STATIC surges every 120 days
  x5 (`surge_dungeon_count` = 5, not the `TuningConfig` default of 4),
  resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12 days +4%,
  with the Siege damage live and the policies able to see one. This is the
  re-measurement issue #1358 asked for, on `a71ee1f`:

  **EVERY ROW BELOW IS A PLAYER WITH NO EMPIRE TREE.** The owner has ruled
  that the game is balanced around a player fully invested in the Explorer
  tree; **nothing on record measures that player at these settings**, so do
  not read these as describing them.

  **AND EVERY ROW BELOW PREDATES `surge_on_empty_board`, WHICH DEFAULTS ON.**
  A surge now fires whenever the board has no dungeons on it, on top of the
  120-day clock, so these seven figures were measured against a different
  rule rather than merely at different settings. `docs/DECISIONS.md` names
  this table specifically in its 2026-09-07 entry and says the same. Issue
  [#1406](https://github.com/sdubois777/Cataclysm/issues/1406). They are the
  best figures on record and they have not been re-measured; quote them with
  this sentence attached.

  | | Value | Block sd | Count |
  |---|---:|---:|---|
  | Last Stand reached | **45.9%** | 1.22 | 2,754 of 6,000 |
  | Cleared, per Last Stand reached | **1 in 24.6** | 0.34 pts | 112 of 2,754 |
  | Earned Cataclysm dungeon opens | **54.9%** | 1.27 | 3,294 of 6,000 |
  | Earned dungeon won | **40.4%** | 2.19 | 1,332 of 3,294 |
  | Campaign won at all | **24.1%** | 1.47 | 1,444 of 6,000 |
  | Cities lost, of 25 | **13.61** | 0.16 | |
  | Sieges created per campaign | **8.99** | 0.14 | counted at creation |

  **THE CLEAR RATE IS THE ONE TO DISTRUST**, though it is firmer than it
  was. Its block standard deviation is 0.34 points against a figure of 4.07,
  which is 8% of it: the six blocks run 1 in 22 to 1 in 28 on 17 to 21 wins
  apiece, and the pooled 95% interval is 1 in 21 to 1 in 30. Read it as
  "about one in twenty-five" and nothing narrower. Everything else in that
  table is firm.

  **THE GAP BETWEEN TWO BLOCKS IS NOT THE NOISE FLOOR.** Two blocks give one
  realised difference, not an estimate of a spread. Measured on these six:
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
  | **today, on `a71ee1f`, six blocks of 1,000** | **45.9%** | **1 in 24.6** | **54.9%** | **40.4%** |

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
