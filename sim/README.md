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
at tier 1 as at tier 8.

The tier is `SWEEP_TIER` in `experiments.py`, and the preset section's tiers are
`PRESET_TIERS`. Sweeping all eight tiers would take about two and a half hours,
which is why only section 7 pays for a second one. This was issue
[#281](https://github.com/sdubois777/Cataclysm/issues/281); before it, the tier
came from an unstated default and nothing in the output mentioned it.

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
  was. It is a fight, and at tier 1 it is a fight the player wins about
  **1 time in 84 per Last Stand reached** -- 1 in 70 if you count only the
  ones actually entered, which is a quarter fewer -- against about **40%**
  for an earned one. Measured 2026-09-06 over 10,000 campaigns in two
  disjoint seed blocks. Issue #5 measured the original; whether a
  near-unwinnable fight is the intended shape is issue #1286, and the
  owner ruled that it is.

  **Those figures replace 2% and 57%, and the fight is now about twice as
  common** -- 27.7% of campaigns against 13.5%. Two thirds of that rise is
  the dungeon sub-type distribution and the boss growing with dungeons
  cleared; the rest is not attributed, which is issue #1343.

  **THOSE ARE MEASURED WITH THE SIEGE'S CITY DAMAGE ZEROED**, because that
  is the only way to compare with the older figures. With it on, which is
  what the model does today after issue #1345, the Last Stand is reached in
  **99.1%** of campaigns and the earned Cataclysm dungeon opens in **2.4%**.
  The route the design treats as the ordinary win condition has almost
  stopped happening. Whether a Siege should be that decisive has not been
  put to the project owner.

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
