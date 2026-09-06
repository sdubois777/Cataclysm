# Example save files, one per historical schema version

These are the test fixtures for the save system, issue
[#529](https://github.com/sdubois777/Cataclysm/issues/529). They are committed on
purpose and they are read by the automation tests in
`game/Source/Cataclysm/Tests/CataclysmSaveMigrationTests.cpp` and
`CataclysmSaveRecordTests.cpp`.

## Every number in a fixture must survive a 32-bit float exactly

`Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` loads a
fixture, writes it back out, and compares the two **exactly**. There is no
tolerance. So a value like `0.05`, which cannot be represented exactly as a
32-bit float, comes back as a slightly different decimal and the test fails.

**The failure message reads as nonsense when this happens**, because it prints
both sides to six decimal places:

> `Run_v1.json` and the record it loads into disagree at
> the record.Cities[0].Upgrades[0].Value is 0.050000 on one side and 0.050000 on
> the other.

Use values that are exact in binary — `0.25`, `0.5`, `0.125`, whole numbers — or
check a new value round-trips before committing it. This cost a build cycle on
2026-09-05.

## Why the fixtures are the test

`docs/Save_System_Design.md` section 5 says why the obvious version of this test
is worthless:

> **Commit example save files, one per historical schema version, as test
> fixtures.** A test loads each and asserts the result matches what the current
> schema should produce. [...] a test that writes a save with the current code
> and reads it back proves only that the code agrees with itself.

So these files were written by hand rather than produced by the code that reads
them, and once a file is here it is never edited again. **When a schema version is
added, add a new file for the version being left behind and leave the older ones
alone.**

### The one exception, and when it stops applying

**Nothing has ever loaded a save.** `ACataclysmGameMode` begins a fresh run
every session and nothing reads one back, so no file these fixtures describe
has ever existed on a player's disk. Until that changes, a fixture may be
edited to match a field added to its record, because there is no history to
preserve -- the fixture is describing a shape rather than a file somebody has.

`Run_v1.json` was edited twice under that exception, both on 2026-08-20 and
both to add a field the record had gained:

- `Material` and `MaterialQuantity` on a ground item, because a drop on the
  floor can be crafting materials and the record could not say so.
- `MaxHealth` and `MaxEnergyShield` on a creature, because a creature's
  maximum health is set by the encounter rather than by its class, and
  without it a restored creature was clamped down to its class default.

`Run_v1.json` was edited a third time on 2026-09-05, for issue #1299:

- `PartialDay`, the time spent that has not yet added up to a whole day. It
  would have been worth nothing before a dungeon's walk time was separated from
  its floor count, because until then a floor always cost exactly one day and
  the carry was zero at every floor boundary. **It holds 0.25 rather than 0 on
  purpose**: zero is the field's default, so a fixture holding zero would pass
  whether the field was read or not.

  That a file MISSING the field still loads is covered by
  `Cataclysm.SaveRecords.AFileWithoutThePartOfADayStillLoads`, which takes this
  file and removes the line, because no committed fixture can be missing a field
  its record writes -- `EveryFixtureHoldsEveryFieldItsRecordWrites` forbids it.

`Run_v1.json` was edited a fourth time on 2026-09-05, for issue #1307:

- `Cities` and `CityUpgradeSlots`, because the empire's 25 cities were not
  written to a save at all.

  **It holds two cities rather than 25, on purpose.** The guard below checks that
  every FIELD survives a round trip, not every element, so two entries exercise
  all nine saved city fields and all seven upgrade fields. Twenty-three more
  identical entries would add two hundred lines nobody reads and no coverage.
  One city is intact with a repeating upgrade, the other is fallen and doomed
  with a one-time upgrade, so every flag and both kinds of upgrade appear.

  **Only the mutable half of a city is here.** A city's name, tier, position and
  its three adjacency lists are not saved, because
  `UCataclysmEmpireMap::Build` recomputes all of them identically. See
  `FCataclysmCity` for the split.

`Run_v1.json` was edited a fifth time on 2026-09-05, also for issue #1307:

- `Dungeons`, `DungeonTimers` and `CurrentDungeonId`, because the dungeons
  standing on the map and their resolve timers were not written to a save.

  **One dungeon and its one timer**, for the same reason the cities hold two
  rather than 25: the guard checks every FIELD survives a round trip, not every
  element.

  **The two lists must agree**, and a save refuses to write them if they do not
  -- see `UCataclysmEmpireRun::DungeonsAgreeWithTimers`. So this file's single
  timer names this file's single dungeon, and changing one without the other
  would describe a state the game will not produce.

`Run_v1.json` was edited a sixth time on 2026-09-05, also for issue #1307:

- `SurgeMode`, `SurgeLethalityRung`, `SurgeIndex`, `SurgesFired`,
  `NextSurgeDay` and `RandomStreamSeed`, because when the next wave of dungeons
  is due -- and where the run's one source of chance had got to -- were not
  written to a save.

  **Every one of the six holds something other than its default**, and the two
  counts differ from each other: 7 surges escalated against 9 waves landed. Two
  integer fields holding the same number would round-trip perfectly with their
  values swapped, and
  `Cataclysm.SaveRecords.TheCommittedRunFileKeepsTheSurgeSchedule` is what reads
  them back one at a time.

  **`RandomStreamSeed` is a position, not a seed.** `FRandomStream` marks its
  starting seed as savable and the position it has reached as not, so the record
  writes the position out explicitly. See `UCataclysmRunSave::RandomStreamSeed`.

`Run_v1.json` was edited a seventh time on 2026-09-06, for issue #1324:

- `Bosses`, because a dungeon record now says how many bosses it holds. Most
  dungeons hold one, on the final floor, which is the design's universal rule. A
  Fallen City is the stated exception and holds one per dungeon that was standing
  on the city when it fell.

  **It holds 4 rather than the default of 1**, deliberately. A fixture carrying a
  field's default cannot show that the value survives a round trip rather than
  being re-defaulted on load, and
  `Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` would pass
  either way. Four is also distinct from every other whole number in that
  dungeon -- `DungeonId` 3, `CityId` 7, `Floors` 12 and `SpawnedDay` 96 -- so two
  fields written into one another could not go unnoticed.

  **The dungeon it sits on is `Basic`, which in play would hold one.** The
  fixture is test data rather than a reachable game state, and the value is
  chosen to be readable back rather than to be realistic.

`Character_v2.json` was edited twice under it, both on 2026-08-25 and both for
issue #50:

- `StartingWeaponType` and `StartingDamageType`, because a character now
  chooses a starting weapon type and a damage type when it is created.
- `PassiveAllocation` and `DefeatedCataclysmBosses`, because a character can now
  earn and spend passive points. The fixture holds one point in a basic node and
  one in a capstone with its second option taken, so both halves of a spent node
  are described rather than only the count.

`Character_v1.json` was deliberately left alone both times: it describes version
1, which predates even the attribute allocation, and the completeness test does
not walk it for that reason.

**Once the game can load a save, this exception is gone.** From then on a
file here is a file a player might have, and changing one means changing what
an old build wrote -- which is a lie about history and defeats the whole point
of committing them. Add a schema version and a migration step instead.

## What is here

| File | What it is |
| :-- | :-- |
| `Account_v1.json` | An account record at schema version 1. An online Hardcore partition with two characters in it |
| `Character_v1.json` | A character record at version 1. Online Hardcore, not Solo Self-Found, carrying one piece of gear and one stack of material |
| `Run_v1.json` | A run record at version 1, on floor 6 of a dungeon, with a boss at part health and a common creature beside it |
| `Example_v1.json` | The example record at version 1 |
| `Example_v2.json` | The same record at version 2, after `Title` was renamed to `Label` |
| `Example_v3.json` | The same record at version 3, after `TotalCopper` was split into `Gold` and `Copper` |

The three `Example_` files describe **the same record three times**, so the test
loads all three and checks they produce identical results. That is what proves
the chain runs the right steps in the right order rather than merely running
something.

`UCataclysmSaveExampleRecord` exists only for these, because all three real
records are at version 1 and version 1 is the first, so none of them has a
migration to run yet. Its header says more.

## Two things to keep in mind when editing one

**Every number here is exactly representable in binary** — 13.5, 4.25, 0.75,
96 — so a value written out and read back is bit for bit the same and a test can
compare it without a tolerance. Adding 0.1 to a fixture would make a comparison
fail for a reason that has nothing to do with the save system.

**A field added to a record must be added to that record's fixture.**
`Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` loads each
fixture, writes it back out and compares the two, so a field in the C++ that the
fixture does not carry fails the test rather than being quietly defaulted.
