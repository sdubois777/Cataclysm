# Save System and Empire Persistence

This is the design for what the game writes to disk, how it is partitioned, and
how it survives the schema changing. It answers issue #21.

**What is built, as of 2026-08-20.** The storage layer exists: the three
records, the partition rule, JSON reading and writing, the migration chain,
and committed example save files as its test. Issue #529.

| Section | Built? |
| :-- | :-- |
| 1 and 2, the three records | The record classes exist. **Most of the fields listed do not**, because nothing in the game produces a character level, an attribute allocation, a passive tree, 18 equipped slots, a stash, an empire graph or a dungeon timer yet. Each record says which of its fields are absent and why |
| 3, partitioning | Built. `UCataclysmSavePartition` |
| 4, storage format | Built. `FCataclysmSaveStorage` |
| 5, versioning and migration | Built. `FCataclysmSaveMigration`, and `game/Tests/SaveFixtures/` |
| 6, when a save is written | **Not built.** There is no clock, no write on an event, and no death write. Nothing in the game calls the storage layer at all |

---

## 1. Three records, not one save file

The design forces three separate persistence scopes, because three different
things have three different lifetimes and, in co-operative play, three different
owners.

| Record | Lifetime | Owner in co-op | Roughly one per |
| :-- | :-- | :-- | :-- |
| **Account** | Permanent | Each player has their own | Lethality mode |
| **Character** | Permanent, survives a failed run | Each player has their own | Character |
| **Run** | Discarded when the run ends | **Shared by the party** | Active run |

Putting these in one file makes co-operative play impossible to retrofit, which
is the specific risk issue #21 raises. Keeping them apart costs nothing now.

### Why the run record is separate and shared

`docs/Cataclysm_GDD_v2.md`, section "Co-operative Play", states that the death
penalty "is paid once for the party, not once per player" and is "charged once
against the **shared empire clock**". Section XVI's risk table states the intent
directly: "Design empire as shared resource in co-op with individual character
builds."

So the empire, the day count and the dungeon timers belong to the run, and the
run belongs to the party. Character level, passive tree, inventory and equipment
belong to the individual and travel with them out of somebody else's session.

**That split is the co-op split.** It is not an extra feature bolted on for
Phase 2; it is the same boundary the solo game already has, with more than one
character record attached to one run record.

---

## 2. What each record holds

### Account record — permanent

One per lethality mode per player, see the partition rules in section 3.

- Empire upgrade points, banked and unspent
- Empire upgrade tree allocation
- Shared stash contents
- The list of characters belonging to this partition
- Schema version

### Character record — permanent, and it survives a failed run

Issue #315 settled that **nothing in the design destroys a character**. A run
ending — defeating the boss dungeon, losing the capital, dying in the Last Stand,
or being killed by the corrupted double — costs the run and not the character. So
this record is never deleted as a consequence of play.

- Character level and experience
- Attribute allocation
- Passive class tree allocation
- Class resource state where it persists between runs
- Inventory contents
- Equipped items, all 18 slots, with their rolled affixes
- Cataclysmic Residue held
- **Lethality mode**, set at creation, never changes
- **Solo Self-Found flag**, set at creation, never comes off
- **Offline or online flag**, set at creation, never changes, from #505
- For a Solo Self-Found character only: its own private empire upgrade points and
  tree allocation, because it shares a tree with no other character at all
- For a Solo Self-Found character only: its own private stash, 600 slots in six
  tabs, for the same reason. An ordinary character's stash is in an account
  record instead
- Schema version

### Run record — discarded when the run ends

- Current day
- Empire graph: which cities stand, each city's population, defence, and its
  filled upgrade slots — 3 slots, or 2 under Heretic
- Active dungeons, their modifiers, their depth and their resolve timers
- Surge schedule and pending surges
- Cataclysm quest progress, for example rifts sealed of the ten Hell on Earth
  requires
- The identifiers of the character records taking part, one in solo play and up
  to four in co-operative play
- **The floor being fought on, as it stands**: where each character is, the
  health of every creature still alive, each creature's rarity and modifiers,
  and items lying on the floor that were not picked up. This is what lets a
  player who was cut off come back into the same fight rather than to the
  start of it. Section 6 says exactly how much of a fight is restored, and
  what is deliberately not
- Schema version

---

## 3. How saves are partitioned

Partitioning is not a storage convenience here. It is a rule the design already
made, and the save layout has to enforce it because it is the only place it can
be enforced.

`docs/Cataclysm_GDD_v2.md`, section "Difficulty Options":

> **Each lethality mode has its own empire upgrade tree.** A character's empire
> meta-progression is shared with every other character in the same mode and with
> no character in another one.

> **The shared stash and the auction house are partitioned the same way.**
> Standard characters share one stash and one market, Hardcore characters share a
> second pair, and Heretic characters share a third. Nothing moves between them.

> **Solo Self-Found is stricter than that** [...] A Solo Self-Found character has
> its own empire upgrade tree shared with no other character at all — not with the
> others in its lethality mode, and not with another Solo Self-Found character.

### The rule as it applies to storage

| Character is | Reads and writes which account record | Stash |
| :-- | :-- | :-- |
| Online Standard, not Solo Self-Found | The online Standard account record | Shared online Standard stash |
| Online Hardcore, not Solo Self-Found | The online Hardcore account record | Shared online Hardcore stash |
| Online Heretic, not Solo Self-Found | The online Heretic account record | Shared online Heretic stash |
| Offline Standard, not Solo Self-Found | The offline Standard account record | Shared offline Standard stash |
| Offline Hardcore, not Solo Self-Found | The offline Hardcore account record | Shared offline Hardcore stash |
| Offline Heretic, not Solo Self-Found | The offline Heretic account record | Shared offline Heretic stash |
| Any mode, either population, **Solo Self-Found** | **None.** Its empire tree lives in its own character record | **Its own private stash**, 600 slots, shared with nobody. It lives in the same character record |

**A Solo Self-Found character touches no account record at all.** That is the
cleanest expression of the design rule, and it means the strictest mode is also
the simplest to store: one file, self-contained. The private stash does not
change that, because it lives in the character record beside the private empire
tree rather than in an account record.

**It does make that file much larger**, and anybody implementing this should
expect it. An ordinary character record holds 48 inventory slots and 18 equipped
items; a Solo Self-Found one holds those plus 600 stash slots, so the worst case
is about ten times the item data. Every one of those items carries its rolled
affixes.

### The offline and online split

#505, landed 2026-08-10, settled that a character is created offline or online
and never changes in either direction, and that offline characters have no
auction house and no ladder. The reasoning follows Last Epoch and Diablo II's
open and closed realms: local files can be edited, so the two populations are
kept permanently non-transferable.

**#505 did not say whether the two populations also have separate stashes and
separate empire trees, and neither did the design document.** That was issue
#528, and it was answered by the project owner on 2026-08-14: **separate.** An
offline character and an online character share no empire upgrade tree, no
stash, no market and no balance of gold, whatever their lethality mode.
`docs/Cataclysm_GDD_v2.md` states it in section II beside the lethality mode
partition rules, and `docs/DECISIONS.md` carries the reasoning and the sources.

The answer was forced rather than chosen. A shared stash that both an offline and
an online character can reach is a transfer route between the two populations,
which is exactly what the non-transferability rule exists to prevent: an item
edited into a local save would reach the auction house and the ladder through it.

So the partition key is **offline-or-online × lethality mode**, giving up to
six account records per player, plus one self-contained record per Solo
Self-Found character.

**Both halves of that are bounded.** An account holds 24 characters as one pool
across both populations and all three lethality modes, so the worst case is six
account records and 24 character records, of which at most 24 are Solo
Self-Found. That matters to the format because a Solo Self-Found character record
is the largest object in it: it carries a private 600-slot stash and a private
empire upgrade tree as well as the ordinary 48 inventory slots and 18 equipped
items.

---

## 4. Storage format

**Use Unreal's `USaveGame` subclasses, serialised as JSON, with an explicit
schema version as the first field.**

### Why `USaveGame` rather than writing files directly

It is the engine's own mechanism, it goes through `ISaveGameSystem`, and it is
the only route that works unchanged on consoles, where raw file access is
restricted. Section XV lists platform ports, so writing straight to a file path
would have to be undone later.

**Not through `UGameplayStatics::SaveGameToSlot`, though, and this cost a cycle
to find out.** That call takes a `USaveGame` object and serialises it with the
engine's binary archive, so using it would throw away the readable file this
section chose JSON for. `SaveDataToSlot` and `LoadDataFromSlot` take raw bytes
and reach the same `ISaveGameSystem` underneath, so the JSON is built here and
the bytes handed to the engine. The platform abstraction is kept and the file
stays readable. Section 6 records the same finding for the asynchronous write.

Mark persisted fields `UPROPERTY(SaveGame)` and serialise with an archive that has
`ArIsSaveGame = true`, so the set of persisted fields is declared on the field
rather than maintained in a separate list that drifts.

### Why JSON rather than the engine's default binary

The two ends of this trade are both occupied by shipped games in this genre.
**Last Epoch writes JSON**, in `AppData\LocalLow\Eleventh Hour Games\Last Epoch\Saves`,
with a short magic prefix. **Grim Dawn writes a custom binary format**, `.gdc`
files under `Documents\My Games\Grim Dawn\save`.

JSON is right here for three reasons specific to this project:

1. **This game will change constantly through development and Early Access**, which
   issue #21 names as the reason to design migration from the start. A migration
   written against a readable format can be inspected, diffed and tested by hand.
   A migration written against binary cannot, and every bug in one produces a
   corrupt save rather than an error.
2. **The project already follows Last Epoch's architecture.** #505 adopted its
   offline and online split by name. Following its storage choice too is
   consistent, and its offline saves are the closest working example of exactly
   what this document describes.
3. **The cost of JSON is size and load time**, and both are affordable at this
   scale. The largest record is a stash, and the alternative only matters at sizes
   this game does not reach.

`FJsonObjectConverter::UStructToJsonObjectString` and `JsonObjectStringToUStruct`
do the conversion, so the JSON path is engine-supported rather than hand-rolled.

**Binary becomes the right answer if load time is measured and found to be a
problem.** Measure before switching; do not switch on principle.

### Where saves live

`FPlatformMisc::GetProjectSavedDirectory()` — that is `Saved/SaveGames/` under the
project during development, and the platform's own location in a packaged build.
Do not hard-code a path.

Suggested slot names, one record per slot:

```
Account_Online_Standard
Account_Online_Hardcore
Account_Online_Heretic
Account_Offline_Standard
Account_Offline_Hardcore
Account_Offline_Heretic
Character_<guid>
Run_<guid>
```

A character is identified by a generated `FGuid` and not by its name, so renaming
is free and two characters may share a name.

---

## 5. Schema versioning and migration

This is the part issue #21 is most worried about, and it is right to be: a format
that cannot migrate means discarding player progress at every patch, and the
design calls empire meta-progression "the primary meta-progression system".

### The rules

1. **Every record carries an integer `SchemaVersion` as its first field.** First,
   so it can be read without parsing the rest, which is what lets a migration run
   before the record is interpreted.
2. **The version is per record type.** An account record and a character record
   version independently. Bumping one does not touch the other.
3. **Migration is a chain of single-step functions**, `Migrate_3_to_4`,
   `Migrate_4_to_5`, applied in order from the file's version to the current one.
   Never write a migration that jumps versions; a chain of small steps is testable
   and a jump is not.
4. **A migration never reads the game's current data tables.** It transforms one
   schema into the next using only what is in the record and constants frozen into
   the migration itself. A migration that reads `game/Data/` breaks the moment
   that data changes, which is the thing most likely to change.
5. **A record from a newer version than the build understands is refused, not
   guessed at.** Report it plainly to the player. Silently loading a newer save
   loses data.
6. **Migration writes a backup first**, alongside the original, and only replaces
   the original once the migrated record has been read back successfully.

### What a version bump means in practice

Adding a field with a sensible default is not a version bump — `UPROPERTY(SaveGame)`
already handles a missing field by leaving the default. A version bump is for
changes that need transformation: renaming a field, changing its type, splitting
one field into two, or changing the meaning of a value.

### Testing it

Issue #21's third acceptance criterion is a test that loads a save written by a
previous schema version. The way to make that real rather than decorative:

**Commit example save files, one per historical schema version, as test fixtures.**
A test loads each and asserts the result matches what the current schema should
produce. When a version is added, its example file is added and the older ones
stay. This is the only form of the test that cannot quietly stop testing
anything — a test that writes a save with the current code and reads it back
proves only that the code agrees with itself.

Per `CLAUDE.md`, confirm the test fails when it should: change one migration step
to drop a field and check the test that covers that version fails, using
`tools/unreal_build.prove_cpp_guard`.

**The files are in `game/Tests/SaveFixtures/`**, and its `README.md` says what
each one is and what to do when a version is added.

**With every record at version 1 there is no migration to exercise**, so the
chain is proved on a record that exists only to be old:
`UCataclysmSaveExampleRecord`, at version 3, with a step that renames a field
and a step that splits one field into two. Inventing a fake version bump on the
character record instead would mean committing an example file for a version
that never existed.

---

## 6. When a save is written, and what quitting costs

**The game saves itself, often, and there is no manual save.** The project owner
set this on 2026-08-20: a Hardcore character must not be able to leave a losing
boss fight by closing the game, and a player who is cut off must come back where
they were.

### What resuming restores, and what it does not

Decided 2026-08-20: **a neutral restart with the damage kept.**

| Restored | Not restored |
| :-- | :-- |
| Which floor, and where on it the character is standing | Any wind-up in progress. Every creature resumes from a still moment |
| The character's health, mana and energy shield | Projectiles and ground effects already in flight |
| Every creature that was alive, with the health it had. **A boss keeps every point taken off it** | How far through its attack cycle each creature was |
| Each creature's rarity and its modifiers | Remaining durations on buffs and debuffs |
| Items lying on the floor that were not picked up | The ability the character was casting |

**What a player gains by quitting mid-fight is a breather, not a reset.** They
come back at the health they had, standing in front of a boss at the health it
had.

**Why the mid-blow state is deliberately left out.** It is the data most likely to
change shape with every patch — a creature's brain, how far into a wind-up it is,
a projectile's flight — and section 5 requires a migration for any persisted field
whose shape changes. Persisting combat choreography would mean writing a migration
every patch for state nobody wants preserved, which is the cost with none of the
benefit. Keeping the damage and dropping the choreography removes what the escape
is worth while leaving the save format stable.

**It can be tightened later without changing anything else.** If play shows the
breather is worth having, individual pieces of the right-hand column can move to
the left one at a time, each as its own schema bump.

### When a save is written

Two triggers, and both are needed:

1. **On a clock**, so nothing is ever far from being written.
2. **On events that matter, immediately.** A fight starting, a creature dying, the
   character's health falling through a threshold, changing floor, an item
   entering or leaving the inventory.

The second is what makes the gap inside a fight near zero without writing
constantly while a player walks around a city. **The interval and the health
threshold are tuning constants and only playing settles them.**

### Death is written first, and synchronously

The one rule that cannot be relaxed. For a Hardcore character, death is the event
this whole feature exists to make stick, so it is written in the same frame the
health reaches zero, through the synchronous write, before the death is otherwise
processed. Everything else may be written asynchronously.

### Writing must not stall the frame

`UGameplayStatics::AsyncSaveGameToSlot` takes a `USaveGame` object and serialises
it with the engine's binary archive, which would throw away the readable file
section 4 chose JSON for. The route that keeps both, and it is the engine's own:
gather the state and build the JSON on the game thread, then hand the bytes to
`ISaveGameSystem::SaveGameAsync`. That is the same call `AsyncSaveGameToSlot`
makes once it has finished serialising, and its callback returns on the game
thread.

### The three records already pay for this

The thing that changes constantly is the run. The thing that is large is the
account record's 600-slot stash. They are separate records, so they are written on
separate triggers: the run record on the cadence above, the account record only
when it actually changes. That split was made in section 1 for a different reason
and it is what makes a frequent save affordable.

### What this does not prevent, said plainly

**An offline save file can be copied before a boss and put back afterwards.** No
arrangement of writes prevents that. Section 7 already accepted that local files
can be edited, from #505, and this does not change it.

**Full enforcement exists only for an online character**, whose record is held
where the player cannot reach it. An offline Hardcore character is on its honour.
The game should say so plainly rather than implying a guarantee it does not have.

### The escape moves to whatever other way out exists

Path of Exile 2 removed logout macros, and players on its official forum report
that the escape simply moved: a boss fight can be paused, and "Respawn at
Checkpoint" taken instead. **So every other way out of a fight has to answer the
same rule or this work buys nothing.** In this design that means the Last Stand
and any town portal or dungeon exit. It is named here because it is the failure
this feature is most likely to have.

---

## 7. What this design deliberately does not settle

- **Whether an offline character can be converted to an online one**, one way,
  leaving everything behind. Diablo II allows the safe direction and blocks the
  unsafe one. Nobody has asked for it and nothing here depends on it.
- **Anti-tamper for offline saves.** #505 accepted that local files can be edited
  and answered it by making offline and online populations non-transferable, so
  the save format does not need to resist editing. If a checksum is wanted anyway,
  it is additive and does not change anything above.
- **Cloud saves and how conflicts resolve.** Depends on the answer to #501, the
  business model question. Not designed here.
- **Auction house and ladder storage.** Server-side, not a save file, and
  downstream of #501. #57, #58 and #179 cover them.
- **The exact autosave interval, in seconds.** Section 6 settles that there IS
  one, that it is joined by writes on events, and that death is written
  synchronously. The number itself is a tuning constant that needs a measured
  write cost, and there is still nothing to measure.

---

## Sources

The format comparison was taken from how shipped games in the genre solve it:

- Last Epoch's offline saves are JSON, at `AppData\LocalLow\Eleventh Hour Games\Last Epoch\Saves`
  — [save file location](https://steamcommunity.com/app/899770/discussions/0/4338725580143851622/),
  [save editor thread describing the JSON format](https://fearlessrevolution.com/viewtopic.php?t=17089)
- Grim Dawn uses a custom binary `.gdc` format, at `Documents\My Games\Grim Dawn\save`
  — [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Grim_Dawn)

The rules in section 6 about quitting a fight were taken from what shipped
games do about it:

- Diablo IV hardcore holds the character in the world for **10 seconds** after
  a logout is started, and it can die during them: "Leaving the game instantly
  is NOT an easy escape of your inevitable doom". A Scroll of Escape is
  consumed automatically if the player disconnects while monsters are attacking
  them — [Maxroll hardcore guide](https://maxroll.gg/d4/resources/hardcore-guide)
- Path of Exile 1 permits instant logout as a defensive technique, and a
  single-button logout macro is explicitly allowed
- Path of Exile 2 removed logout macros and its players report the escape moved
  rather than closed, to pausing in a boss fight and taking "Respawn at
  Checkpoint" — [What is the future of Hardcore Logouts, pathofexile.com](https://www.pathofexile.com/forum/view-thread/3828741).
  There is no developer reply in that thread; it is player feedback and is
  recorded as such
- Hades autosaves between rooms and does **not** save during combat, so quitting
  mid-fight costs the room rather than saving the player

**Two numbers that are widely quoted for Path of Exile were NOT used**, because
the page carrying them is on Fandom and it refused to serve the page: a 6 second
delay before the server logs out a client whose connection was lost, and a 40
second timer at the start of a new area. Neither is cited above and nothing here
rests on them.

The partition rules are not from research. They are read directly out of
`docs/Cataclysm_GDD_v2.md`, section "Difficulty Options", and `docs/DECISIONS.md`,
the 2026-08-10 entry on offline play.
