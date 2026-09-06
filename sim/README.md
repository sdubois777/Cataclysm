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
at tier 1 as at tier 8. **STALE, AND UNDER-POWERED, AND STILL NOT RE-MEASURED.**
That comparison was made when both tiers ran against one fixed Cataclysm, so it
compared two power scales rather than two tiers; and section 7 runs 150
campaigns per cell, where `experiments.win_rate_noise(150)` is 5.77 points, so
it cannot resolve a small ordering change either way. Re-running it is 1.8
minutes of the report's 20; nobody has done it since #1338. See the next
section.

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

**What is still stale in this file**: the claim that the preset ordering in
section 7 differs between tier 1 and tier 8. The Last Stand figures in the map
section below **have** been re-measured on `e8b33c2` and carry their conditions
and the history they replace. Issue
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

  **MEASURED 2026-09-06 OVER 6,000 CAMPAIGNS** in six disjoint blocks of
  1,000 seeds, at tier 1, `No tree`, `triage`, STATIC surges every 120 days
  x5 (`surge_dungeon_count` = 5, not the `TuningConfig` default of 4),
  resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12 days +4%,
  with the Siege damage live and the policies able to see one. This is the
  re-measurement issue #1358 asked for, on `e8b33c2`:

  | | Value | Block sd | Count |
  |---|---:|---:|---|
  | Last Stand reached | **56.4%** | 0.78 | 3,384 of 6,000 |
  | Cleared, per Last Stand reached | **1 in 34.5** | 0.89 pts | 98 of 3,384 |
  | Earned Cataclysm dungeon opens | **50.0%** | 1.55 | 3,001 of 6,000 |
  | Earned dungeon won | **38.3%** | 1.59 | 1,150 of 3,001 |
  | Campaign won at all | **20.8%** | 1.16 | 1,248 of 6,000 |
  | Cities lost, of 25 | **16.34** | 0.24 | |
  | Sieges created per campaign | **10.68** | 0.13 | counted at creation |

  **THE CLEAR RATE IS THE ONE TO DISTRUST.** Its block standard deviation is
  a third of the figure itself: the six blocks run 1 in 23 to 1 in 62 on 9
  to 25 wins apiece, and the pooled 95% interval is 1 in 28 to 1 in 42. Read
  it as "about one in thirty-five" and nothing narrower. Everything else in
  that table is firm.

  **THE GAP BETWEEN TWO BLOCKS IS NOT THE NOISE FLOOR.** Two blocks give one
  realised difference, not an estimate of a spread. Measured on these six:
  the A-to-B gap runs from **0.67 times** the six-block standard deviation
  (the clear rate) to **1.81 times** it (Sieges per campaign), and under the
  sub-type table #1369 replaced it came in at 0.38. It is one draw and it
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
  | **today, six blocks of 1,000** | **56.4%** | **1 in 34.5** | **50.0%** | **38.3%** |

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
