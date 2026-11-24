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

`Run_v1.json` was edited an eighth time on 2026-09-06, for issue #1331:

- `DefenceBite` and `PopulationBite` became `DefenceDamage` and
  `PopulationDamage`, because a dungeon's damage is now a number of POINTS
  rather than a share of the host city's maximum. A share of the maximum divided
  out of how many resolves a city survived, which made every city-health upgrade
  in the game worth nothing.

  **The two values are the old ones converted rather than new numbers.** The
  dungeon sits on city 7, an Outpost with 1,000 maximum defence and 5,000
  maximum population, so 0.125 became 125 defence points and 0.0625 became 312.5
  people. Both are exactly representable in binary, both are non-default, and
  neither collides with any other number in that dungeon.

  **This is a rename, and a rename is normally a schema version bump.** It is
  not one here only because of the exception above: nothing has ever loaded a
  save, so no file with the old field names exists anywhere. Once the game can
  load one, the same change would need `SchemaVersionNow` to move and a
  `Migrate_1_to_2` step that multiplied each fraction by its city's maximum.

`Run_v1.json` was edited a ninth time on 2026-09-06, for issue #1357:

- `Cataclysm`, because a dungeon record now says which Cataclysm sent it. The
  empire layer had no notion of which Cataclysm was running at all until that
  issue, so there was nothing for the field to hold; the Cataclysm dungeon's
  unlock rule reads it, because a run's total of quest objectives cannot say
  whether any one Cataclysm is finished.

  **It holds `Demonic` rather than the default `None`**, for the same reason
  `Bosses` holds 4: a fixture carrying a field's default cannot show that the
  value survives a round trip rather than being re-defaulted on load, and
  `Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` would pass
  either way.

  **`None` is a real value and not an absent one.** A Fallen City dungeon is
  built from what a city was carrying rather than sent by anybody and carries
  `None` in play, so the value has to stay reachable. This fixture describes the
  other case, which is the one that has something to round trip.

`Run_v1.json` was edited a tenth time on 2026-09-07, for issue #41:

- `Modifiers` and `ModifierScore`, because a dungeon record now says which
  dungeon modifiers it carries and how much harder they make everything inside
  it. Nothing in the game had ever read a row of
  `game/Data/DungeonModifiers.csv` until then.

  **`Modifiers` holds two row keys and not one**, so the fixture proves an array
  survives a round trip rather than only its first element. Both are real rows
  of the modifier table and both are Demonic, which is what the dungeon's
  `Cataclysm` field says sent it -- a pairing the game itself would produce.

  **`ModifierScore` holds 25.0, which is those two rows' danger scores added
  up**: Infernal Rain is 10 and Blood Altar is 15. It is non-default, exactly
  representable in binary, and distinct from every other number in that dungeon.
  It is stored rather than recomputed on load because the danger scores live in
  a DataTable the empire module may not read.

`Run_v1.json` was edited an eleventh time on 2026-09-11, for issue #45:

- `PositiveRoll` and `NegativeRoll` on the enchantment its ground item
  carries, because an item now keeps where inside each enchantment's stated
  range it rolled. The project owner ruled that an enchantment "rolls a value
  evenly inside its range and keeps it".

  **They hold 0.25 and 0.625 rather than the default of 1**, for the reason
  `Bosses` holds 4: a fixture carrying a field's default cannot show that the
  value survives a round trip rather than being re-defaulted on load. The two
  differ from each other, so one written into the other would be noticed, and
  both are exact in binary.

`Run_v1.json` was edited a twelfth time on 2026-09-25, for issue #41:

- `bRisenFromTheDead` on a creature, because a creature a floor rule raised from the
  dead pays nothing when it dies again -- the owner's decision of 2026-09-17 -- and a
  saved floor must bring it back still paying nothing. `docs/DECISIONS.md` has the
  entry, "A saved floor leaves out every creature a floor rule made, and remembers
  which creatures a rule raised from the dead".

  **The first creature holds true and the second false**, for the reason `Bosses`
  holds 4: false is the field's default, so a fixture holding only false could not
  show that the value survives a round trip rather than being re-defaulted on load.

`Run_v1.json` was edited a thirteenth time on 2026-10-09, for issues #1820 and #41:

- `bOnTheDarkFloor` and `DarkFloorFellFrom` on the floor, because the floor a fall
  into a chasm leads to carries the number of the floor its stairs lead to, so the
  floor number alone cannot say the player is on it. `docs/DECISIONS.md` has the
  entry, "Those in the Dark, layer 3".

  **They hold true and 5 rather than the defaults of false and 0**, for the reason
  `Bosses` holds 4: a fixture carrying a field's default cannot show that the
  value survives a round trip rather than being re-defaulted on load. Five is
  not the floor's own number, 6, so one written into the other would be noticed,
  and `Cataclysm.SaveRecords.TheCommittedRunFileKeepsTheDarkFloorAndTheFloorFallenFrom`
  reads them back one at a time.

  **The fixture is test data rather than a reachable game state.** In play a
  dark floor numbered 6 after a fall from floor 5 is a dungeon six floors deep.

  That a file MISSING the two fields still loads, as not on the dark floor, is
  covered by `Cataclysm.SaveRecords.AFileWithoutTheDarkFloorStillLoadsAsNotOnIt`,
  which takes this file and removes the two lines.

`Run_v1.json` was edited a fourteenth time on 2026-10-15:

- `UpgradeFloors` on a dungeon, because a dungeon's hit on its city is now sized
  by the floors it was rolled with, and the record keeps how many of its floors a
  city upgrade added. `docs/DECISIONS.md` has the entry, "In the game a
  dungeon's hit is sized by the floors it was rolled with; the two city upgrades
  that move floors no longer change it".

  **It holds 0, the field's default**, unlike the fields above, by the
  coordinating session's ruling of 2026-10-15: 0 is a dungeon no upgrade
  touched, so no other test that loads this file changes meaning. So this file
  does not show that a non-zero value survives a round trip, and **no test
  does**: `Cataclysm.SaveWriter.AWrittenRunRecordCarriesTheStandingDungeons`
  compares the field on a live dungeon no upgrade touched, so it compares 0
  with 0.

`Run_v1.json` was edited a fifteenth time on 2026-10-22:

- `VoidStacks` on both cities, because a city now carries the Void stacks its
  ordinary Void dungeons added, and ten erase it. `docs/DECISIONS.md` has the
  entry, "In the game an ordinary Void dungeon adds 1 to 3 Void stacks to its
  city instead of damage…".

  **City 7 holds 4 and city 0 holds 0.** Four is not the field's default, so
  the fixture shows a value surviving a round trip rather than being
  re-defaulted on load, and it differs from city 7's identifier, so one
  written into the other would be noticed. City 7 is fallen and not erased
  with four stacks, which is a state play can reach: a city felled by damage
  keeps its stacks until it is retaken. **It is also marked doomed, which
  play cannot reach without erasing it**; that mark predates the stacks and
  is left as it was.

  **Nothing loads a run's cities back yet** (issue #753), so the stacks are
  written and never read; that is the gap every city field has.

`Run_v1.json` was edited a sixteenth time on 2026-10-22:

- `HollowFrom` on its one dungeon, because a dungeon now records the natural
  floors a Hollow -- the Void's Quest dungeon -- was made with, which it
  shrinks from. `docs/DECISIONS.md` has the entry, "In the game the Void's
  Quest dungeon is a Hollow…".

  **It holds 16 rather than the default of 0**, for the reason `Bosses` holds
  4: a fixture carrying a field's default cannot show that the value survives
  a round trip rather than being re-defaulted on load. Sixteen is not the
  dungeon's `Floors`, 12, nor any other number on it, so one written into
  another would be noticed.

  **The fixture is test data rather than a reachable game state.** The
  dungeon is an ordinary Siege, and in play only a Hollow carries a
  `HollowFrom` that is not nought. Nothing reads the field on a dungeon that
  is not a Hollow, so no other test that loads this file changes meaning.

  **Nothing loads a run's dungeons back yet** (issue #753), so the field is
  written and never read, as every dungeon field is.

`Run_v1.json` was edited a seventeenth time on 2026-10-22:

- `LeftEdict` and `LeftEdictUntilDay` on both cities, because a city now
  records the Divine Edict an Ascendant -- the Celestial Cataclysm's Quest
  dungeon -- left on it, and the first day it is no longer carried.
  `docs/DECISIONS.md` has the entry, "In the game the Celestial's Quest dungeon
  is an Ascendant…".

  **City 7 holds `Celestial_Edict_of_Silence` until day 140 and city 0 holds
  none**, `"None"` and 0, the fields' defaults. A row key and a day that are not
  the defaults show that the values survive a round trip rather than being
  re-defaulted on load, and 140 is no other number in the file. City 7 is fallen
  and carrying a left Edict, which play can reach: a city keeps its left Edict
  when it falls, as it keeps its stacks.

  **Nothing loads a run's cities back yet** (issue #753), so both fields are
  written and never read; that is the gap every city field has.

`Run_v1.json` was edited an eighteenth time on 2026-10-24:

- `DaysOwed`, the whole days a walk under way has spent and the empire has not
  yet run, because since 2026-10-24 a walk's days run when the player leaves
  the dungeon (`docs/DECISIONS.md`, the owner's decision of that day). **It holds
  13 rather than 0 on purpose**: zero is the field's default, and 13 is no other
  number in the file. `CurrentDungeonId` is 3 here, so the file describes a run
  saved while the player was inside a dungeon, which is when the field is not
  zero.

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

`Character_v3.json` was added on 2026-09-24 for issue #2064, when a character
became limited to one class tree per damage type and `Migrate_2_to_3` came to
remove the others from an older file. **It is `Character_v2.json` with its
version changed and nothing else**, because the v2 file spends only in the
Masochist tree, so the step leaves it as it is. `Character_v2.json` is kept as
the file the step runs against. What the step does to a file with two trees of
one damage type is checked by
`Cataclysm.SaveRecords.MigratingTo3KeepsOneClassTreePerDamageType`, which
writes its cases out rather than committing a fixture for each.

`Character_v3.json` was edited twice under the exception, on 2026-09-24 and 2026-09-25. The first was for the
finding recorded on issue #753 that the character record did not write worn
gear at all:

- `WornGear`, one entry per occupied worn slot, each naming its slot. **Two
  entries, and the second is in `Ring3` on purpose**: a slot other than the
  first is what shows the NAME was kept rather than the position, since an
  entry saved by position would read back into the first slot whatever it
  said. The greatsword carries a damage type, an affix, sockets and residue,
  none at its default, so each field shows it survived rather than being
  re-defaulted. `Cataclysm.SaveRecords.TheCommittedCharacterFileKeepsItsWornGear`
  reads them back one at a time.
- **One enchantment on the worn greatsword and one on the carried Circlet**,
  added 2026-09-25. Until then no item in the file carried an enchantment, so
  `EnchantmentCount` and `Enchantments` sat at their defaults, and a default
  round-trips identically whether it was read or not. Each enchantment's
  `Positive`, `Negative` and both rolls are away from their defaults, and
  `EnchantmentCount` is 1 to match. The rolls are 0.75, 0.25, 0.5 and 0.125,
  exact in binary.
- **It is still version 3.** A field added with an empty list as its default
  is not a version bump, by section 5 of `docs/Save_System_Design.md`: a file
  without it reads as a character wearing nothing.

`Character_v3.json` and `Run_v1.json` were edited under the exception again on
2026-10-25, for issue #857 (ruling Q-L1): every item gained `bMadeAtCreation`,
which marks the weapon character creation gave. **It is true on the worn
greatsword and false on the other four items**: false is the field's default, so
only the true one shows the value survives a round trip. **Still version 3, and
the run record still version 1**: a field with a default is not a version bump,
and a file without it reads as false. Nothing loads a save yet (#753).

`Character_v3.json` was edited under the exception again on 2026-10-25, for issue
#837 layer 3: the record gained `SkillAssignment`, which pool skill the player chose
for which key. **It holds one choice, `War_Greatsword_Heavy` on the `Ultimate` key**:
an empty list is the field's default, so only a non-empty one shows the value survives
a round trip, and a heavy attack on the Ultimate key shows the key is read by its
NAME and not taken from the row.
`Cataclysm.SaveRecords.TheCommittedCharacterFileReadsIntoTheValuesItStates` reads it
back. **Still version 3** (ruled Q6): a file without the field reads as no choice,
which is the default fill. Nothing loads a save yet (#753).

**Once the game can load a save, this exception is gone.** From then on a
file here is a file a player might have, and changing one means changing what
an old build wrote -- which is a lie about history and defeats the whole point
of committing them. Add a schema version and a migration step instead.

**That happened with issue #753.** `78ad0fa81` (2026-10-31) loads a saved
character and its run from their slots at the console, and the select screen's
Continue loads
one when its level begins, so the exception above no longer applies. Two things
followed, ruled 2026-11-01:

- **A slip, recorded rather than undone.** `Character_v3.json` gained
  `LastWrittenUtc` in `59e5dd893` (#753, 2026-10-31), after `78ad0fa81`, with no
  schema version and no note here. Under the rule above it needed a version and a
  migration step. The file keeps the field.
- **From issue #1472 on, a field added to a record takes a version.** The first
  is `DifficultyTier`, the character's difficulty tier: the character record went
  to version 4 on 2026-11-01, `Migrate_3_to_4` writes tier 1 into a version 3
  file, and `Character_v4.json` is `Character_v3.json` at version 4 with
  `"DifficultyTier": 2` added and nothing else changed. **2 and not 1**, because
  1 is the field's default, which round-trips identically whether it was read or
  not. `Character_v3.json` is now a migration input, not edited again;
  `Cataclysm.SaveRecords.ACharacterFileFromBeforeTheTierMigratesToTierOne` runs it
  through the step and checks every other field is kept.
- **The second is `bMarkedByConsumption`, the Consumption mark** (issue #2688):
  the character record went to version 5 on 2026-11-02, `Migrate_4_to_5` writes
  `false` into a version 4 file, and `Character_v5.json` is `Character_v4.json`
  at version 5 with `"bMarkedByConsumption": true` added and nothing else
  changed. **True and not false**, for the tier's reason: false is the default.
  `Character_v4.json` is now a migration input, not edited again;
  `Cataclysm.ConsumptionMark.ACharacterFileFromBeforeTheMarkMigratesUnmarked`
  runs it through the step and checks every other field is kept.
- **The third is `ConsumptionBuild`, the build the corrupted double wears**
  (issue #2688, the owner's decision of 2026-11-02 that the double is the
  character as it was at the crossing): the character record went to version 6
  on 2026-11-02, `Migrate_5_to_6` writes an empty build into a version 5 file,
  and `Character_v6.json` is `Character_v5.json` at version 6 with a
  `"ConsumptionBuild"` object added and nothing else changed. **The build is not
  empty and differs from the record's own fields** -- level 41 and not 42,
  Ferocity 9 and not 10, its chosen skill on the `Heavy` key and not the
  `Ultimate`, one passive node and not two, `Void` and not `War` as its creation
  type, 7 points besides gear -- so a build read from the record's own fields
  instead of its own cannot pass. Its one worn item is the record's greatsword.
  `Character_v5.json` is now a migration input, not edited again;
  `Cataclysm.ConsumptionDouble.ACharacterFileFromBeforeTheBuildMigratesWithNone`
  runs it through the step and checks every other field is kept.
- **The fourth is `CataclysmOrderSeed`, the character's Cataclysm order seed**
  (issue #1324 slice 6a, ruled 2026-11-02 from `docs/Cataclysm_GDD_v2.md`, Ending
  a Run: "The same tier means the same Cataclysms, by name and not only by
  count."): the character record went to version 7 on 2026-11-02,
  `Migrate_6_to_7` writes into a version 6 file the seed its `CharacterId` gives
  (`UCataclysmCharacterSave::CataclysmOrderSeedFor`, 32-bit FNV-1a, 1412640493 for
  this folder's identifier), and `Character_v7.json` is `Character_v6.json` at
  version 7 with `"CataclysmOrderSeed": 975318642` added after
  `ConsumptionBuild` and nothing else changed. **975318642 and not 1412640493**,
  because a file holding the seed its identifier gives would pass whether the seed
  was read or worked out again on load; and not 0, the field's default. No other
  number in the file is 975318642. `Character_v6.json` is now a migration input,
  not edited again;
  `Cataclysm.CataclysmDraw.ACharacterFileFromBeforeTheDrawMigratesWithItsIdentifiersSeed`
  runs it through the step and checks every other field is kept.
- **And the run record took its first version the same day, to 2**, for the
  corrupted double's hunt (issue #2688, ruled 2026-11-02): three fields on
  `Walk` (`bConsumptionDoubleHasHunted`, `ConsumptionDoubleHealthShare`,
  `ConsumptionDoublePotionCharges`) and three on `Floor.RuleState`
  (`bConsumptionDoubleHuntsThisFloor`, `bConsumptionDoubleStanding`,
  `ConsumptionDoubleLocation`). `Migrate_1_to_2` writes their defaults into a
  version 1 file, and `Run_v2.json` is `Run_v1.json` at version 2 with those six
  keys added holding values that are not their defaults -- true, true, a share
  of 0.625, charges of 12.5, 7.5, 2.5 and 0, and a location of (448.5, -96.25,
  88) -- and nothing else changed. `Run_v1.json` is now a migration input, not
  edited again; `Cataclysm.ConsumptionDouble.ARunFileFromBeforeTheHuntMigratesWithNone`
  runs it through the step and checks every other field is kept. Every test
  that read `Run_v1.json` as the committed run file reads `Run_v2.json` now.
- **The run record went to version 3 the same day**, for what the player's last
  death cost (issue #41, ruled by the coordinating session on 2026-11-02): five
  fields beside the last hit's -- `LastDeathDay`, `LastDeathDungeonName`,
  `LastDeathWalkDays`, `LastDeathDaysCost` and `LastDeathFallenCityIds` -- saved
  because the last hit is. `Migrate_2_to_3` writes an empty death into a version 2
  file (day 0, no name, no days, no city), and `Run_v3.json` is `Run_v2.json` at
  version 3 with those five keys added after `LastMoveToCityId` and nothing else
  changed. **None holds its default**: day 103, the name "the Siege dungeon on
  Outpost (-3,0)", 19 walk days, 15 days for the death (Heretic's, which is the
  rung the file's `SurgeLethalityRung` of 2 names), and the cities 17 and 23 --
  two, so the array is shown to survive past its first element. No number among
  them is used anywhere else in the file. `Run_v2.json` is now a migration input,
  not edited again; `Cataclysm.DeathCost.ARunFileFromBeforeTheDeathLineMigratesWithNone`
  runs it through the step and checks every other field is kept. Every test that
  read `Run_v2.json` as the committed run file reads `Run_v3.json` now.
- **The run record went to version 4 on 2026-11-02**, for the Cataclysm boss
  dungeon the run has earned (issue #1324 slice 6b): three fields after
  `LastDeathFallenCityIds` -- `bCataclysmDungeonOpened`,
  `CataclysmDungeonOpenedDay` and `CataclysmDungeonBaseFloors`. `Migrate_3_to_4`
  writes a boss not yet opened into a version 3 file (false, day 0, no floors),
  and `Run_v4.json` is `Run_v3.json` at version 4 with those three keys added
  holding true, 109 and 137 and nothing else changed. **None holds its default**,
  and neither number is used anywhere else in the file; 137 is inside the 100 to
  150 a boss rolls, and 109 lies between the last death's day, 103, and the
  run's day, 118. **The fixture is test data rather than a reachable state**: it
  says the boss opened and its `Dungeons` list holds no Cataclysm dungeon, which
  play reaches only once the boss is beaten and the run is won. The depth is not
  saved -- it is the floors rolled plus `BasicDungeonsCleared`, which is.
  `Run_v3.json` is now a migration input, not edited again;
  `Cataclysm.EarnedBossSave.ARunFileFromBeforeTheBossDungeonMigratesUnopened` runs
  it through the step and checks every other field is kept. Every test that read
  `Run_v3.json` as the committed run file reads `Run_v4.json` now.
- **The run record went to version 5 on 2026-11-03**, for the win of the run
  before it (issue #1324 slice 6e): a won run is replaced at once by the
  character's next run, one tier higher, which carries a line saying the last one
  was won. Four fields after `CataclysmDungeonBaseFloors` -- `LastRunWonDay`,
  `LastRunWonTier`, `LastRunWonCataclysm` and `LastRunWonPassivePoints`.
  `Migrate_4_to_5` writes "not won" into a version 4 file (day 0, tier 0, `None`,
  0 points), and `Run_v5.json` is `Run_v4.json` at version 5 with those four keys
  added holding 381, 3, `War` and 10 and nothing else changed. **None holds its
  default**, and the four differ from one another. 381 is used nowhere else in the
  file; 3 and 10 are, and 10 is the only figure a first win pays. **The fixture
  describes a state play reaches**: the run is at tier 4 facing Demonic, Death,
  War and Pestilence, so the run before it was at tier 3, and the third Cataclysm
  of that character's draw is War, whose boss the win paid for (the coordinating
  session's ruling R4). `Run_v4.json` is now a migration input, not edited again;
  `Cataclysm.WinningTheRun.ARunFileFromBeforeTheWinLineMigratesWithNone` runs it
  through the step and checks every other field is kept. Every test that read
  `Run_v4.json` as the committed run file reads `Run_v5.json` now.
- **The account record took its first version on 2026-11-03, to 2**, for the
  empire upgrade points a defeated dungeon banks (issue #52, the first slice):
  `EmpireUpgradePoints` was a whole number and is a double, because an award is
  a fraction -- an ordinary dungeon is worth 1 point times the share of the
  empire's population living -- and the coordinating session ruled that a
  fraction is banked rather than rounded away. `Migrate_1_to_2` writes a version
  1 file's whole number back as the same number (and 0 where the key is absent),
  and `Account_v2.json` is `Account_v1.json` at version 2 with
  `"EmpireUpgradePoints": 37.625` in place of 37 and nothing else changed.
  **37.625 and not 37**: a whole number would round-trip identically through the
  old integer field, so only a fraction shows the double is read; and it is exact
  in binary. `Account_v1.json` is now a migration input, not edited again;
  `Cataclysm.EmpirePoints.AnAccountFileFromBeforeFractionsMigratesKeepingItsPoints`
  runs it through the step and checks every other field is kept, and
  `Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` reads
  `Account_v2.json` as the committed account file.
- **The run record went to version 6 on 2026-11-17**, because a trip between the
  capital and a dungeon became a save and a load (issue #2733 slice 3): what the
  run record does not hold, a trip loses. Five fields after
  `LastRunWonPassivePoints` -- `LastEmpirePointsAwarded` (ruled Q3),
  `LastClearDay`, `LastClearDungeonName`, `LastClearWalkDays` and
  `LastDaysAway` (ruled Q6), the last an object of seven fields of its own.
  `Migrate_5_to_6` writes "nothing yet" into a version 5 file: no award (0), no
  clear (`LastClearDay` -1, an empty name, 0 days) and no walk ended (both days
  -1, no cities, nothing fallen, no surge). `Run_v6.json` is `Run_v5.json` at
  version 6 with those five keys added and nothing else changed. **The fixture
  describes a state play reaches**: the run on day 118 last came back from the
  death on day 103 that cost 15 days and felled cities 17 and 23, so its walk left
  on day 84 (103 less the 19 days the walk spent) and came back on day 118, the
  summary lists those two cities as fallen, and the surge on day 96 is its one
  surge; the clear on day 80 came before that walk, and its award, 2.25, is still
  the last because a death awards nothing. **Every value but one holds something
  other than its default; the one is `bEndedInAClear`, false**, because that walk
  ended in a death, and `Cataclysm.Travel.TheMapsReturnLineSaysWhatTheWalksEndDid`
  reads a true one. The losses, 1000, 5000, 750.5 and 2400.25, are exact in
  binary. `Run_v5.json` is now a migration input, not edited again;
  `Cataclysm.Travel.ARunFileFromBeforeTripsMigratesWithNoAwardNoClearAndNoWalkEnded`
  runs it through the step and checks every other field is kept. Every test that
  read `Run_v5.json` as the committed run file reads `Run_v6.json` now, and every
  test that runs an older run file through the whole chain adds what the step
  writes through one helper, `CataclysmSaveFixtures::AddTheRunsVersion6Defaults`.
- **The run record went to version 7 on 2026-11-18**, because a kill can drop
  a gold pile (issue #2742) and a pile lying on the floor is a ground item: a
  save made mid-floor would otherwise lose every pile not yet collected. One
  field on each ground item, `Gold`, after `bDroppedByARaisedCreature`.
  `Migrate_6_to_7` writes a `Gold` of 0 on every ground item of a version 6
  file, and `Run_v7.json` is `Run_v6.json` at version 7 with `"Gold": 0` on its
  one ground item (gear) and **a second ground item added, a gold pile of 37**
  lying 50 cm from the first -- an empty item, no material, unmarked -- and
  nothing else changed. **37 and not 0**, because 0 is the default and a
  fixture holding only zeroes would pass whether the field was read or not.
  `Run_v6.json` is now a migration input, not edited again. Every test that read
  `Run_v6.json` as the committed run file reads `Run_v7.json` now, and every test
  that runs an older run file through the whole chain adds what the step writes
  through `CataclysmSaveFixtures::AddTheRunsVersion7Defaults`.
- **The character record went to version 8 on 2026-11-18**, because a Solo
  Self-Found character holds its own gold (issue #2742): the owner ruled that day
  that such a character earns gold, and the design's Storage section puts it on
  the character, "its gold is its own". One field, `Gold`, after
  `CataclysmOrderSeed`. `Migrate_7_to_8` writes a balance of 0 into a version 7
  file, and `Character_v8.json` is `Character_v7.json` at version 8 with
  `"Gold": 4321` added and nothing else changed. **4321 and not 0**, because 0 is
  the default and only another figure shows the field is read; and **the
  character in the file is not Solo Self-Found**, so this is a value play does
  not give it -- every other character keeps 0 there -- kept so that the file
  differs from version 7 in the one field. `Character_v7.json` is now a migration
  input, not edited again; `Cataclysm.Gold.ACharacterFileFromBeforeGoldMigratesHoldingNone`
  runs it through the step and checks every other field is kept, every test that
  read `Character_v7.json` as the committed character file reads
  `Character_v8.json` now, and every test that runs an older character file
  through the whole chain adds `"Gold": 0` to what it expects.
- **The account record went to version 3 on 2026-11-18**, for its gold (issue
  #2742): `Gold`, a whole number, after `EmpireUpgradePoints`. The empire
  upgrade tree (issue #2741) takes version 4, as the coordinating session ruled
  the same day. `Migrate_2_to_3` writes a balance of 0 into a version 2 file, and
  `Account_v3.json` is `Account_v2.json` at version 3 with `"Gold": 1234567`
  added and nothing else changed; 1234567 because 0 is the default.
  `Account_v2.json` is a migration input now:
  `Cataclysm.Gold.AnAccountFromBeforeGoldMigratesHoldingNone` runs it through the
  whole chain and checks every other field is kept, every test that read it as
  the committed account file reads `Account_v3.json`, and the test that runs
  `Account_v1.json` through the whole chain expects the gold of 0 as well.
- **The account record went to version 4 on 2026-11-18**, for what its empire
  upgrade tree has bought (issue #2741 slice 1b): `EmpireTree`, after `Gold`,
  the class trees' allocation struct -- each node's points, and the option each
  centre capstone took. `Migrate_3_to_4` writes an empty tree,
  `{ "Nodes": [] }`, into a version 3 file, keeps an object already there and
  refuses anything else under the key, and `Account_v4.json` is
  `Account_v3.json` at version 4 with `EmpireTree` added and nothing else
  changed. **The tree in it is one play reaches**: Structural Integrity at its
  full 10, Fortified Gates at its full 8 (it opens at 2 in Structural
  Integrity), Masonry Techniques at 7 (it opens at 6 in Fortified Gates), and
  with those 25 points spent the centre capstone Foundations of the Empire
  (`capstone_0000000m`), its second option, The Delver, chosen and its point
  spent: 26 points, and the 37.625 banked beside them is what is left unspent.
  **Option 2 and not 1**, because 0 is the default and 1 the first option, and
  only another figure shows the choice is read.
  `Cataclysm.EmpireTree.TheCommittedAccountFilesTreeIsOnePlayCanReach` replays
  it through the buying rules. `Account_v3.json` is a migration input now:
  `Cataclysm.EmpireTree.AnAccountFromBeforeTheTreeMigratesHoldingAnEmptyTree`
  runs it through the step and checks every other field is kept, every test
  that read it as the committed account file reads `Account_v4.json`, and every
  test that runs an older account file through the whole chain adds the empty
  tree through `CataclysmSaveFixtures::AddTheAccountsVersion4Defaults`.
- **The character record went to version 9 on 2026-11-18**, for a Solo Self-Found
  character's own empire upgrade points and tree (issue #2741 slice 1c): the owner
  reversed the coordinating session's C8 that day, so such a character banks a
  defeat's award and spends it in a tree of its own. `PrivateEmpireUpgradePoints`,
  which nothing had written, was a whole number and is a double, as the account's
  bank became at its version 2; and `PrivateEmpireTree`, after it, holds what the
  character's own tree has bought, in the account record's shape. `Migrate_8_to_9`
  writes the points back as the same number (0 where the key is absent) and an
  empty tree, keeps a tree object already there, and refuses points that are not a
  number or a tree that is not an object. `Character_v9.json` is
  `Character_v8.json` at version 9 with `"PrivateEmpireUpgradePoints": 12.75` in
  place of 0 and a `PrivateEmpireTree` holding Structural Integrity
  (`basic_0000000w`) at 3, and nothing else changed. **12.75 and not 0, and a node
  and not an empty tree**, because 0 and empty are the defaults; 12.75 is exact in
  binary, and a fraction shows the double is read. **As at version 8, the character
  in the file is not Solo Self-Found**, so these are values play does not give it
  -- every other character keeps 0 and an empty tree -- kept so that the file
  differs from version 8 in these two fields only;
  `Cataclysm.SaveRecords.TheCommittedCharacterFileReadsIntoTheValuesItStates`,
  which until then asserted no private points, now reads both. `Character_v8.json`
  is now a migration input, not edited again;
  `Cataclysm.EmpireTree.ACharacterFromBeforeItsOwnTreeMigratesKeepingItsPoints`
  runs it through the step and checks every other field is kept, every test that
  read it as the committed character file reads `Character_v9.json`, and every
  test that runs an older character file through the whole chain adds what the
  step writes through `CataclysmSaveFixtures::AddTheCharactersVersion9Defaults`.
- **The character record went to version 10 on 2026-11-19**, because a skill
  levels as it is used (issue #2764, ruled by the project owner that day): each
  skill's total experience is kept, by skill row, in `SkillExperience`, after
  `SkillAssignment`, and its level is read from it and the character's level.
  `Migrate_9_to_10` writes an empty list into a version 9 file -- no skill had
  earned any -- keeps a list already there, and refuses a key holding anything
  but a list. `Character_v10.json` is `Character_v9.json` at version 10 with two
  entries added and nothing else changed: `War_Greatsword_Heavy` at 37810.5,
  half a point above the 37,810 skill level 3 costs, so the character's level 42
  reads it as level 3; and `Demonic_Greataxe_Ultimate` at 1234.25, a skill whose
  weapon the character does not wear, kept at level 1 as a choice is kept.
  **Two entries and fractions, not an empty list**, because empty is the default
  and the migration's value, and a fraction shows the double is read. As at
  version 9, these are values play does not give this character -- its
  Greatsword's War skills deal no damage yet, so they earn nothing -- kept so
  that the file differs from version 9 in this field only.
  `Character_v9.json` is now a migration input, not edited again;
  `Cataclysm.SkillLevels.ACharacterFromBeforeSkillLevelsMigratesWithNoSkillExperience`
  runs it through the step and checks every other field is kept, every test that
  read it as the committed character file reads `Character_v10.json`, and every
  test that runs an older character file through the whole chain adds what the
  step writes through `CataclysmSaveFixtures::AddTheCharactersVersion10Defaults`.
- **The account record went to version 7, the character record to version 14
  and the run record to version 15 on 2026-11-22**, for the empire tree's ninth
  slice (issue #2741 slice 9). The account and a Solo Self-Found character count
  the tier 1 gold spent on city upgrades across runs, `GoldSpentOnCityUpgrades`,
  which Reinvestment reads; the run holds Economic Bailout's once a run on each
  city, `bBailedOut`, Scout Parties' bonus dungeons, `bBonus` on each dungeon and
  `BonusDungeonsCleared`, and Bribery's once a surge, `bBribedThisSurge`. Each
  step writes 0 or false: a record written before could use none of them.
  `Account_v7.json` is `Account_v6.json` at version 7 with 75000.5 spent,
  `Character_v14.json` is `Character_v13.json` at version 14 with 50000.25 spent
  (the count is a double and unrounded, the coordinating session's V20, and each
  fraction is exact in binary), and
  `Run_v15.json` is `Run_v14.json` at version 15 with its first city bailed out,
  its second not, 2 bonus dungeons cleared and the surge's bribe spent: figures
  and not the defaults, for the reason below. **ITS ONE DUNGEON'S `bBonus` IS
  FALSE, THE DEFAULT**, because that dungeon is a Siege and a bonus dungeon is
  always an ordinary dungeon with no Siege (the coordinating session's V18 of
  2026-11-22): true would describe a run the game cannot reach. The previous three
  files are now migration inputs, not edited again; every test that read them as
  the committed files reads the new ones, and every test that runs an older file
  through the whole chain adds what the steps write through
  `CataclysmSaveFixtures::AddTheAccountsVersion7Defaults`,
  `AddTheCharactersVersion14Defaults` and `AddTheRunsVersion15Defaults`.
- **The run record went to version 16 on 2026-11-22**, for the cleanse city
  upgrade (issue #2838): the run holds whether the cleanse has been bought in it,
  `bCleanseBoughtThisRun`, once a run. The step writes false: a run written before
  could not buy it, because it was refused as not built. `Run_v16.json` is
  `Run_v15.json` at version 16 with the cleanse bought, a figure and not the
  default. `Run_v15.json` is now a migration input, not edited again; every test
  that read it as the committed file reads the new one, and every test that runs
  an older run file through the whole chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheRunsVersion16Defaults`.
- **The run record went to version 17 on 2026-11-22**, for a thrall loaded from
  a save (issue #2840): a thrall's saved creature keeps the skill that took it,
  `TakenBySkillRow`, and that skill's level factor at the take,
  `TakenLevelFactorOnCreatures`. The step writes Subjugate's row,
  `Demonic_Staff_Ultimate`, and 1 on a creature the player commanded, and None
  and 1 on every other creature: Subjugate is the one row that takes a creature,
  and the level at the take was not recorded, so 1, the factor at level 1, is the
  one figure that never overstates (the coordinating session's ruling, Q1).
  `Run_v17.json` is `Run_v16.json` at version 17 with its thrall, the Abyssal
  Warden, holding Subjugate's row and a factor of 1.5, a figure and not the
  default, and the Brute beside it None and 1. `Run_v16.json` is now a migration
  input, not edited again; every test that read it as the committed file reads
  the new one, and every test that runs an older run file through the whole chain
  adds what the step writes through
  `CataclysmSaveFixtures::AddTheRunsVersion17Defaults`.
  **THE NUMBERS FOLLOW THE GEMS WINDOW'S** (issue #46, character 13 and run 14,
  the same day): this slice was written at character 13 and run 14 and renumbered
  when it was rebased onto the gems window (the coordinating session's V1).
- **The account record went to version 6 and the character record to version 12
  on 2026-11-21**, for Imperial Prowess's extra option (issue #2741 slice 8): the
  project owner ruled that day, "Imperial prowess lets you pick an additional
  keystone from a previous tier", and the coordinating session ruled one extra
  pick in total. `FCataclysmPassiveAllocation` gained two fields after `Nodes`,
  `ExtraOptionNode` and `ExtraOption`: the centre milestone holding the extra
  option and which option it is. Every allocation a record holds carries them --
  the account's `EmpireTree`, and the character's `PassiveAllocation`,
  `ConsumptionBuild.PassiveAllocation` and `PrivateEmpireTree` -- though only an
  empire tree uses them. `Migrate_5_to_6` and the character record's newest step
  write "None" and 0 into each allocation the record holds, keep a value of the
  right kind already there and refuse anything else.
  `Account_v6.json` is `Account_v5.json` at version 6 with
  `"ExtraOptionNode": "capstone_0000000m"` and `"ExtraOption": 1` in its tree,
  and `Character_v12.json` is `Character_v11.json` at version 12 with the same
  milestone and 3 in its own empire tree and "None" and 0 in its other two
  allocations. **Figures and not None and 0** in the empire trees, for the reason
  below. Neither tree holds Imperial Prowess, so play could not take these extra
  options; they are kept so that each file differs from the one before in these
  fields only. `Account_v5.json` and `Character_v11.json` are now migration
  inputs, not edited again; `Cataclysm.EmpireTreeProwess.ThePreviousFilesMigrateHoldingNoExtraOption`
  runs each through its step, every test that read them as the committed files
  reads the new ones, and every test that runs an older file through the whole
  chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheAccountsVersion6Defaults` or
  `CataclysmSaveFixtures::AddTheCharactersVersion12Defaults`. **THE CHARACTER
  RECORD'S NUMBER IS THE NEXT FREE ONE**: gems slice G2 also moves the character
  record, and whichever of the two merges second takes the number after the
  other's.
- **The account record went to version 5 and the character record to version 11
  on 2026-11-20**, for a gold debt (issue #2741 slice 6e): the owner ruled that
  day, "no gold debt unless there's an option for it in the empire tree", and
  Global Credit and Sovereign Loans are those options. Two fields, `GoldOwed` and
  `GoldOwedEachDay`, after `Gold` in each: the debt, and the part of it Sovereign
  Loans takes a day. The account holds every character's but a Solo Self-Found
  one's, whose own are in its character record, as its gold is. `Migrate_4_to_5`
  and `Migrate_10_to_11` write 0 and 0 -- nothing could be bought on credit
  before -- keep a number already there and refuse anything else under either
  key. `Account_v5.json` is `Account_v4.json` at version 5 with `"GoldOwed": 2500`
  and `"GoldOwedEachDay": 250` added and nothing else changed, and
  `Character_v11.json` is `Character_v10.json` at version 11 with 1500 and 150.
  **Figures and not 0**, because 0 is the default and the migration's value, and
  only another figure shows each field is read. As before, the character in the
  file is not Solo Self-Found, so these are values play does not give it, kept so
  that the file differs from version 10 in these two fields only.
  `Account_v4.json` and `Character_v10.json` are now migration inputs, not edited
  again; `Cataclysm.EmpireTreeCredit.ThePreviousFilesMigrateOwingNothing` runs
  each through its step and checks every other field is kept, every test that
  read them as the committed files reads `Account_v5.json` and
  `Character_v11.json`, and every test that runs an older file through the whole
  chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheAccountsVersion5Defaults` or
  `CataclysmSaveFixtures::AddTheCharactersVersion11Defaults`.
- **The run record went to version 8 on 2026-11-18**, because each run has its
  own mask on its dungeons' walk seeds (issue #2749, B10): until then every
  run's dungeon N walked the same floors. One field, `WalkSeedMask`, after
  `CorruptedStalkerSeed`. `Migrate_7_to_8` writes a mask of 0 into a version 7
  file -- the mask that leaves every dungeon the seed it had at version 7 -- and
  `Run_v8.json` is `Run_v7.json` at version 8 with `"WalkSeedMask": 12345`
  added and nothing else changed. **12345 and not 0**, because 0 is the default
  and the migration's value, and only another figure shows the field is read.
  `Run_v7.json` is now a migration input, not edited again:
  `Cataclysm.WalkSeed.ARunFileFromVersion7MigratesToMaskZeroAndKeepsItsWalkSeeds`
  runs it through the step and checks every other field is kept, every test
  that read `Run_v7.json` as the committed run file reads `Run_v8.json` now, and
  every test that runs an older run file through the whole chain adds the mask
  of 0 through `CataclysmSaveFixtures::AddTheRunsVersion8Defaults`.
- **The run record went to version 9 on 2026-11-19**, because the empire upgrade
  tree's event rules count what a run does (issue #2741 slice 3b): three fields
  after `WalkSeedMask` -- `ClearsSinceLastSurge`, `CitiesReclaimed` and
  `bSanctuaryLastStandSpent` -- and a `BoughtDay` on every city upgrade.
  `Migrate_8_to_9` writes 0, 0, false and a `BoughtDay` of 0 on every upgrade
  of every city into a version 8 file. `Run_v9.json` is `Run_v8.json` at
  version 9 with `"ClearsSinceLastSurge": 3`, `"CitiesReclaimed": 1`,
  `"bSanctuaryLastStandSpent": true` added and a `BoughtDay` of 101 and 64 on
  its two upgrades, and nothing else changed: none of them the default, so the
  test that reads it shows each is read. `Run_v8.json` is now a migration
  input, not edited again:
  `Cataclysm.EmpireTreeEvents.ARunFileFromVersion8MigratesWithNothingCounted`
  runs it through the step and checks every other field is kept, every test
  that read `Run_v8.json` as the committed run file reads `Run_v9.json` now,
  and every test that runs an older run file through the whole chain adds what
  the step writes through `CataclysmSaveFixtures::AddTheRunsVersion9Defaults`.
- **The run record went to version 10 on 2026-11-19**, because a Vengeful
  Wraith came back from a load as an ordinary creature (issue #2725): a saved
  creature says whether it is one, `bIsVengefulWraith`, after
  `CarrionFeederOwnMaxHealth`. THE RUN RECORD'S 10, reached the same day as the
  character record's own 10 above; each record counts its own versions.
  `Migrate_9_to_10` writes false on every creature of the floor of a version 9
  file -- nothing in it tells a wraith from another risen creature.
  `Run_v10.json` is `Run_v9.json` at version 10 with `"bIsVengefulWraith": true`
  on its first creature, the risen Brute that sees 30 times as far, and `false`
  on its second, and nothing else changed: true is not the default, so the test
  that reads it shows the field is read, and the false beside it shows each
  creature keeps its own. `Run_v9.json` is now a migration input, not edited
  again: `Cataclysm.SaveLoad.ARunFileFromVersion9MigratesWithNoWraith` runs it
  through the step and checks every other field is kept, every test that read
  `Run_v9.json` as the committed run file reads `Run_v10.json` now, and every
  test that runs an older run file through the whole chain adds what the step
  writes through `CataclysmSaveFixtures::AddTheRunsVersion10Defaults`.
- **The run record went to version 11 on 2026-11-20**, because the empire
  upgrade tree's Relentless Pursuit counts the dungeons cleared since the last
  death (issue #2741 slice 5b): one field after `bSanctuaryLastStandSpent`,
  `ClearsSinceLastDeath`. `Migrate_10_to_11` writes into a version 10 file the
  run's own `DungeonsCleared` when it records no death -- an empty or missing
  `LastDeathDungeonName` -- and 0 when it records one, because the clears
  after a death cannot be told from those before it. `Run_v11.json` is
  `Run_v10.json` at version 11 with `"ClearsSinceLastDeath": 5` added and
  nothing else changed: 5 is not the default, and not the 0 the step writes
  for this run, which records a death on day 103, so the test that reads it
  shows the field is read. As at version 9, it is a value this run's play
  does not give -- its last clear, on day 80, came before that death -- kept
  so that the file differs from version 10 in this field only.
  `Run_v10.json` is now a migration input, not edited again:
  `Cataclysm.EmpireTreeDrops.ARunFileFromVersion10MigratesWithTheClearsItCanProve`
  runs it through the step and checks every other field is kept, every test
  that read `Run_v10.json` as the committed run file reads `Run_v11.json`
  now, and every test that runs an older run file through the whole chain
  adds what the step writes through
  `CataclysmSaveFixtures::AddTheRunsVersion11Defaults`.
- **The run record went to version 12 on 2026-11-20**, for three of the empire
  upgrade tree's gold nodes (issue #2741 slice 6b): each city's
  `bTookSurgeDamage` (Price Controls), the run's `bPopulationFellBelowHalf`
  (Stimulus Package), and the walk's `GoldDroppedThisWalk` and
  `GoldPickedUpThisWalk` (Cleanup Crew). `Migrate_11_to_12` writes false on
  every city, false on the run and 0 and 0 on the walk: the record cannot
  prove a city's past damage (a city below its maximum may be one whose
  maximum the tree raised), the run's flag compares against a maximum that
  depends on the tree, which the record does not hold, and a walk resumed
  from it counts its gold from the load. `Run_v12.json` is `Run_v11.json` at
  version 12 with its first city marked as damaged and its second not, the
  run's flag set, and the walk holding 1234 gold dropped and 1000 picked up:
  none of these is the default or what the step writes, so the test that
  reads it shows each field is read. `Run_v11.json` is now a migration input,
  not edited again: `Cataclysm.EmpireTreeClearGold.ARunFileFromVersion11MigratesUnmarkedAndUncounted`
  runs it through the step, every test that read `Run_v11.json` as the
  committed run file reads `Run_v12.json` now, and every test that runs an
  older run file through the whole chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheRunsVersion12Defaults`.
- **The run record went to version 13 on 2026-11-21**, because a city upgrade
  takes days to build (issue #2825): each city's `Building`, the upgrade it is
  building, and each upgrade's `BuildDaysLeft`. `Migrate_12_to_13` writes an
  empty `Building` on every city and a `BuildDaysLeft` of 0 on every upgrade:
  a version 12 run could build nothing, since every purchase was finished at
  once. `Run_v13.json` is `Run_v12.json` at version 13 with a `BuildDaysLeft`
  of 0 on both its finished upgrades, its first city (city 0) building
  `Architect_Increase_max_defense_by_20` with 4 days left, and its second, the
  fallen one, building nothing: the build is neither the default nor what the
  step writes, so the test that reads it shows the field is read.
  `Run_v12.json` is now a migration input, not edited again:
  `Cataclysm.CityUpgradeBuildSave.ARunFileFromVersion12MigratesBuildingNothing`
  runs it through the step, every test that read `Run_v12.json` as the
  committed run file reads `Run_v13.json` now, and every test that runs an
  older run file through the whole chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheRunsVersion13Defaults`.
- **The character record went to version 13 and the run record to version 14
  on 2026-11-22**, for gems (issue #46, slices G2, G3 and G5). An item's
  `Sockets` was a count and is a list of typed sockets, each holding a gem or
  none; a carried or stashed slot may hold a stack of one gem at one rarity
  (`Gem`, `GemRarity`); and a ground item may be a stack of gems (`Gem`,
  `GemRarity`, `GemQuantity`). `Migrate_12_to_13` (character) and
  `Migrate_13_to_14` (run) write each saved count of N as N empty sockets of
  type Any -- the coordinating session's ruling of 2026-11-21, "sockets
  migrated from today's count fit any type" -- and no gem in every slot and on
  every ground item. `Character_v13.json` is `Character_v12.json` at version 13
  with those, except that its carried piece's two sockets are Attack, holding
  `Gem_Of_The_Viper` at Quality, and Defense, and its worn greatsword's three are
  Attack, Defense and Utility: neither the default nor what the step writes, so
  the test that reads it shows the fields are read. `Run_v14.json` is
  `Run_v13.json` at version 14 with those. `Character_v12.json` and
  `Run_v13.json` are now migration inputs, not edited again: the two
  `Cataclysm.GemSave.` tests run them through the steps, every test that read
  either as the committed file reads the new one now, and every test that runs
  an older file through the whole chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheCharactersVersion13Defaults` or
  `AddTheRunsVersion14Defaults`. **The account record does not move**, though
  its `Stash` is a list of the same slot: nothing fills that stash, so no
  account file can hold a socket count.
- **The character record went to version 15 and the run record to version 18
  on 2026-11-24**, for dismantling (issue #2848, slice S2). An item records the
  gear level and the difficulty tier it arrived at, `GearLevelAtDrop` and
  `TierAtDrop`, so a dismantle refunds only the upgrade levels the player paid
  for. `Migrate_14_to_15` (character) and `Migrate_17_to_18` (run) write -1 and
  0, the fields' defaults, which the forge reads as "not known": nothing recorded
  where an item arrived before. Every item is found by walking the whole record
  for an object holding both `GearLevel` and `bMadeAtCreation`
  (`CataclysmSaveArrival`). `Character_v15.json` is `Character_v14.json` at
  version 15 with the two fields on each of its five items: the creation
  greatsword 0 and 2 (held build and worn), the carried Circlet 4 and 2, the
  empty carried slot -1 and 0, and the worn ring 3 and 1. `Run_v18.json` is
  `Run_v17.json` at version 18 with its ground Circlet 0 and 4 and its gold pile
  -1 and 0. The Circlets' figures are not the defaults, so the tests that read
  the files show the fields are read. `Character_v14.json` and `Run_v17.json`
  are now migration inputs, not edited again: `Cataclysm.Dismantle.`'s two
  migration tests run them through the steps, every test that read either as
  the committed file reads the new one, and every test that runs an older file
  through the whole chain adds what the step writes through
  `CataclysmSaveFixtures::AddTheCharactersVersion15Defaults` or
  `AddTheRunsVersion18Defaults`. **The account record does not move**, for the
  gems step's reason: nothing fills its stash, so no account file holds an item.
  **IT WAS FIRST WRITTEN WITHOUT A VERSION**, on the older sentence in
  `docs/Save_System_Design.md`; window sd-w1 failed six groups on it (every test
  comparing a migrated or committed file field by field), and the coordinating
  session ruled the version on 2026-11-24 by the rule above. That sentence now
  points here.
- **A slip, recorded rather than undone, as `LastWrittenUtc` was.**
  `Run_v1.json` was edited thirteen times after saves could load
  (`78ad0fa81`, 2026-10-30), each time adding fields for #753's dungeon load
  with no schema version and no note here: `da0423c54`, `2082da17b`,
  `904fe4f82`, `6a7cbaef0`, `4aa87ab3e`, `5aa622a56` and `1be88643c` on
  2026-10-30, and `956c2d827`, `c4a41a50d`, `f674abb1a`, `9b225263a`,
  `c968fed35` and `e21133d85` on 2026-10-31. Under the rule above each needed
  a version and a migration step. The file keeps those fields.

## What is here

| File | What it is |
| :-- | :-- |
| `Account_v1.json` | An account record at schema version 1. An online Hardcore partition with two characters in it |
| `Account_v2.json` | The same account at version 2: version 1's contents, with its banked empire upgrade points 37.625, a fraction |
| `Account_v3.json` | The same account at version 3: version 2's contents, plus its gold, 1234567 |
| `Account_v4.json` | The same account at version 4: version 3's contents, plus its empire upgrade tree, 26 points in the Architect branch and the first centre capstone with its second option chosen |
| `Account_v5.json` | The same account at version 5: version 4's contents, plus a gold debt, 2500 owed with 250 taken a day |
| `Account_v6.json` | The same account at version 6: version 5's contents, plus an extra option in its tree, the first of Foundations of the Empire's |
| `Account_v7.json` | The same account at version 7: version 6's contents, plus 75000.5 tier 1 gold spent on city upgrades |
| `Character_v1.json` | A character record at version 1. Online Hardcore, not Solo Self-Found, carrying one piece of gear and one stack of material |
| `Character_v2.json` | The same character at version 2, with its attribute points, its creation choices and two passive points in the Masochist tree |
| `Character_v3.json` | The same character at version 3: version 2's contents, plus a greatsword worn in `Weapon1` and a ring in `Ring3`, and one enchantment each on the greatsword and the carried Circlet, and one chosen skill |
| `Character_v4.json` | The same character at version 4: version 3's contents, plus its difficulty tier, 2 |
| `Character_v5.json` | The same character at version 5: version 4's contents, plus its Consumption mark, true |
| `Character_v6.json` | The same character at version 6: version 5's contents, plus the build its corrupted double wears |
| `Character_v7.json` | The same character at version 7: version 6's contents, plus its Cataclysm order seed, 975318642 |
| `Character_v8.json` | The same character at version 8: version 7's contents, plus its own gold, 4321 |
| `Character_v9.json` | The same character at version 9: version 8's contents, with its own empire upgrade points 12.75, a fraction, and its own tree, Structural Integrity at 3 |
| `Character_v10.json` | The same character at version 10: version 9's contents, plus the experience of two skills, `War_Greatsword_Heavy` at 37810.5 and `Demonic_Greataxe_Ultimate` at 1234.25 |
| `Character_v11.json` | The same character at version 11: version 10's contents, plus its own gold debt, 1500 owed with 150 taken a day |
| `Character_v12.json` | The same character at version 12: version 11's contents, plus an extra option in its own empire tree, the third of Foundations of the Empire's, and none in its other two allocations |
| `Character_v13.json` | The same character at version 13: version 12's contents, with each socket count as a list of sockets -- its carried piece's Attack holding Of The Viper at Quality and Defense, its worn greatsword's Attack, Defense and Utility, its held build's three of type Any -- and no gem in either carried slot |
| `Character_v14.json` | The same character at version 14: version 13's contents, plus 50000.25 tier 1 gold spent on city upgrades |
| `Character_v15.json` | The same character at version 15: version 14's contents, plus where each item arrived -- the creation greatsword +0 at tier 2, the carried Circlet +4 at tier 2, the empty slot not known, the worn ring +3 at tier 1 |
| `Run_v1.json` | A run record at version 1, on floor 6 of a dungeon, with a boss at part health and a common creature beside it |
| `Run_v2.json` | The same run at version 2: version 1's contents, plus the corrupted double's hunt, standing |
| `Run_v3.json` | The same run at version 3: version 2's contents, plus what the player's last death cost |
| `Run_v4.json` | The same run at version 4: version 3's contents, plus its Cataclysm boss dungeon, opened on day 109 at 137 floors |
| `Run_v5.json` | The same run at version 5: version 4's contents, plus the win of the run before it, on day 381 at tier 3 against War, for 10 passive points |
| `Run_v6.json` | The same run at version 6: version 5's contents, plus the last award of 2.25 empire points, the clear on day 80, and the summary of the days its last walk was away, days 84 to 118 |
| `Run_v7.json` | The same run at version 7: version 6's contents, with a `Gold` on each ground item, and a second ground item that is a gold pile of 37 |
| `Run_v8.json` | The same run at version 8: version 7's contents, plus its mask on its dungeons' walk seeds, 12345 |
| `Run_v9.json` | The same run at version 9: version 8's contents, plus the empire upgrade tree's event counts -- 3 clears since the last surge, 1 city reclaimed, the Last Stand spent -- and the day each of its two city upgrades was bought, 101 and 64 |
| `Run_v10.json` | The same run at version 10: version 9's contents, with its first creature saved as a Vengeful Wraith and its second as not one |
| `Run_v11.json` | The same run at version 11: version 10's contents, plus 5 dungeons cleared since the last death |
| `Run_v12.json` | The same run at version 12: version 11's contents, plus its first city marked as damaged by a dungeon, the run's population marked as fallen below half, and the walk's 1234 gold dropped and 1000 picked up |
| `Run_v13.json` | The same run at version 13: version 12's contents, plus a `BuildDaysLeft` of 0 on both its upgrades, its first city building `Architect_Increase_max_defense_by_20` with 4 days left, and its second building nothing |
| `Run_v14.json` | The same run at version 14: version 13's contents, with each ground item's socket count of 0 as an empty list and no gem on either ground item |
| `Run_v15.json` | The same run at version 15: version 14's contents, plus its first city bailed out and its second not, its dungeon not a bonus one, 2 bonus dungeons cleared and the surge's bribe spent |
| `Run_v16.json` | The same run at version 16: version 15's contents, plus the cleanse bought this run |
| `Run_v17.json` | The same run at version 17: version 16's contents, plus its thrall's taking skill, `Demonic_Staff_Ultimate`, at a level factor of 1.5, and the other creature None and 1 |
| `Run_v18.json` | The same run at version 18: version 17's contents, plus where each ground item arrived -- the Circlet +0 at tier 4, the gold pile not known |
| `Example_v1.json` | The example record at version 1 |
| `Example_v2.json` | The same record at version 2, after `Title` was renamed to `Label` |
| `Example_v3.json` | The same record at version 3, after `TotalCopper` was split into `Gold` and `Copper` |

The three `Example_` files describe **the same record three times**, so the test
loads all three and checks they produce identical results. That is what proves
the chain runs the right steps in the right order rather than merely running
something.

`UCataclysmSaveExampleRecord` exists only for these, because all three real
records were at version 1 when it was written and version 1 is the first, so
none of them had a migration to run. The character record has had nine since
(version 10 since 2026-11-19), the run record eleven (version 12 since
2026-11-20), and the account record three (version 4 since 2026-11-18). Its
header says more.

## Two things to keep in mind when editing one

**Every number here is exactly representable in binary** — 13.5, 4.25, 0.75,
96 — so a value written out and read back is bit for bit the same and a test can
compare it without a tolerance. Adding 0.1 to a fixture would make a comparison
fail for a reason that has nothing to do with the save system.

**A field added to a record must be added to that record's fixture.**
`Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` loads each
fixture, writes it back out and compares the two, so a field in the C++ that the
fixture does not carry fails the test rather than being quietly defaulted.
