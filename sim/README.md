# Cataclysm — empire layer tuning rig

A headless simulation of the strategy layer. It is **not** the game and it is
not Unreal code. It exists to derive the numbers the design documents leave
open, by playing thousands of campaigns and measuring what happens.

```bash
python experiments.py
```

Pure standard-library Python. No dependencies.

## Fixed rules (not swept)

- **One floor costs one day.** Depth and time are the same axis, so a dungeon
  can never be made cheaper without also being made poorer.
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

A run is **lost** when some Outpost, its parent Bulwark, and that Bulwark's
parent Sanctuary have all fallen. A run is **won** by clearing the quest
objectives and then the Cataclysm dungeon. Hitting the day cap is a
**stalemate**, and is treated as the worst outcome of the three.

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
