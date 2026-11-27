# Example save files, one per record at its current version

These are the test fixtures for the save system, issue
[#529](https://github.com/sdubois777/Cataclysm/issues/529). There are three, one for each record
`docs/Save_System_Design.md` defines, each at the version this build writes:

| File | Record | Version |
| :-- | :-- | --: |
| `Account_v8.json` | `UCataclysmAccountSave` | 8 |
| `Character_v18.json` | `UCataclysmCharacterSave` | 18 |
| `Run_v22.json` | `UCataclysmRunSave` | 22 |

They are read by the automation tests, among them `CataclysmSaveRecordTests.cpp`
(`Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` and the
`TheCommitted...` tests) and the feature tests that read the figures a committed file states.

## No older version is kept, and none is read

**The owner's ruling of 2026-11-27, their chosen option verbatim: "Remove; old saves reset".** A record
older than this build's version is refused, listed as unreadable, and left on disk as it is
(`FCataclysmSaveVersion`); there is no upgrade step. So there is no example file for an older version
either: issue #2928 slice 12 deleted the 45 files of the older versions (account 1 to 7, character 1
to 17, run 1 to 21) and the test-only example record's three, with the 45 upgrade steps and the tests
that read them. Their history is in git and in
`docs/DECISIONS.md`.

**When a record's version is raised, replace its file**: write the new file at the new version, delete
the old one, and update the file name in the tests that read it. Nothing reads the old file any more,
and leaving it would only be a file of a version the game refuses.

## Every number in a fixture must survive a 32-bit float exactly

`Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` loads a
fixture, writes it back out, and compares the two **exactly**. There is no
tolerance. So a value like `0.05`, which cannot be represented exactly as a
32-bit float, comes back as a slightly different decimal and the test fails.

**The failure message reads as nonsense when this happens**, because it prints
both sides to six decimal places: "0.050000 on one side and 0.050000 on the other".

Use values that are exact in binary -- `0.25`, `0.5`, `0.125`, whole numbers -- or
check a new value round-trips before committing it. This cost a build cycle on
2026-09-05.
