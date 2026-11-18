# The play loop: one press of Play

What to click, in order, to play the main loop once, and what each step should show on screen and in the log. Issue
[#2733](https://github.com/sdubois777/Cataclysm/issues/2733), slice 4.

**The loop:** the front end, a new character, the capital with the empire map, a walk through a dungeon, back to the
capital with the days away shown, a second walk ended by a death, and a Continue.

## Before you start

- **Rebuild the editor after updating the code.** After a `git pull`, close the editor and run
  `python tools/unreal_build.py build` from the repository root before opening the project. The editor loads the game
  libraries in `game/Binaries/Win64` as they are, and it did not warn that they were older than the code on
  2026-11-18: it ran a library built on 2026-09-16, which has no character select screen, so Play put a test dungeon's
  creatures into the front end level and the screen was black. **If Play shows anything other than the character select
  screen, rebuild first.**
- **Open the project in the editor.** It opens on `L_FrontEnd`, the character select level.
- **Open the log:** Window > Output Log, and type `PLAY LOOP` in its search box. Every step below writes one line that
  begins `PLAY LOOP:`, and the search box then shows those lines and nothing else. The same lines are in
  `game/Saved/Logs/Cataclysm.log`.
- **The lines quoted below are examples.** Your numbers, names and dungeons will differ: every new character's run is
  drawn afresh. What should match is the shape of each line, and that the figures in one line agree with the next.
- **There is no shortcut to the last floor or to a death.** `Cataclysm.DungeonFloor` only chooses the floor a dungeon
  level builds when it starts, and it does not move a walk under way; no console command kills the character. So walk
  the floors and fight. **To keep the walk short, choose a dungeon with few floors**: the city screen shows each
  dungeon's floor count.

## What the screens show about a dungeon, and what only the log shows

| What | On screen | In the log |
| :-- | :-- | :-- |
| Its floors | The city screen: "12 floors" in the dungeon's line. Hovering a city on the map: "dungeon of 12 floors, 9 days left" | The `standing:` lines |
| Its timer | The city screen: "9 days until it bites". The map's hover line: "9 days left" | The `standing:` lines |
| Its sub-type (Horde, Timed, Cow Level, and the rest) | The city screen: the first word of the dungeon's line, when it has one | The `standing:` lines, in its name: "the Horde dungeon" |
| **Its kind** (ordinary, Quest, Fallen City, Cataclysm) | **Not shown on any screen** | The `standing:` lines, in its name: "the Quest dungeon"; an ordinary one has no kind word |
| **Its modifiers** | **Not shown on any screen before the walk.** In the dungeon, each floor's modifiers are listed on the floor | The `standing:` lines: "modifiers Brand of the Aggressor, Hard Mode", or "no modifiers" |
| Its city's damage | The city screen's status line: the city's tier, its defence "X of Y" and its people "X of Y". The map's city box: the share of defence left | The capital's line after a walk: "cities hit: ... lost ... defence and ... people" |

## The steps

### 1. Press Play

- **On screen:** the character select screen, titled "Choose a character", with a row for each saved character and the
  buttons "New character" and "Delete a character".
- **In the log:** `PLAY LOOP: the front end is open: no saved character to continue, or New character to make one`

### 2. Click "New character", fill it in, click "Begin"

- **On screen:** "Create a character": a name, a weapon and a damage type. "Begin" opens the capital.
- **In the log:** `PLAY LOOP: new character 'Ash' (Greatsword, War) begun: opening the capital`

### 3. The capital opens, with the empire map

- **On screen:** a nearly empty level, and the empire map open over it. The map's status line begins "Day 0" and says
  "12 dungeons standing". Each city's box shows its tier and the share of its defence left, "Outpost 100%".
  **M** closes the map and opens it again.
- **In the log:** one line for the capital, then one `standing:` line for each dungeon:

  `PLAY LOOP: in the capital on day 0, tier 1: 12 dungeons standing, 0 cities fallen; level 1, 0 experience into it`

  `PLAY LOOP: standing: dungeon 3, the Horde dungeon on Outpost (-3,0): 12 floors, 9 days until it bites, modifiers Brand of the Aggressor, Hard Mode`

### 4. Before the first walk: note a second dungeon

Choose a dungeon on **another** city than the one you will walk. This is how you check what the days away did.

- **Click its city** on the map to open the city screen. Write down its dungeon's line: "Horde, 12 floors, 9 days
  until it bites". Write down the city's status line: its defence and its people.
- **Find the same dungeon's `standing:` line in the log** for what the screen does not show: its kind and its
  modifiers.
- Click the city again to close its screen.

### 5. Walk a dungeon

- **Click the city of the dungeon you will walk**, then the **Walk** button under that dungeon's line.
- **On screen:** the dungeon level, on floor 1. The map and the city screen close.
- **In the log**, one line for the press and one for arriving:

  `PLAY LOOP: Walk pressed: dungeon 3, the Horde dungeon on Outpost (-3,0): 12 floors, 9 days until it bites, modifiers Brand of the Aggressor, Hard Mode; travelling to the dungeon level`

  `PLAY LOOP: entered dungeon 3, floor 1 of 12; the walk owes 1 day`

  A Cow Level owes 2 days a floor: "the walk owes 2 days" after floor 1.

### 6. Leaving early is refused

- Press **M**, click any city, and click **Walk** on any dungeon.
- **On screen:** the city screen says "You are walking dungeon 3. There is no leaving a dungeon early: clear its last
  floor, or die in it." Nothing else happens.
- **In the log:** no PLAY LOOP line.
- Press **M** to close the map.

### 7. Take each floor's stairs

- **On screen:** the next floor. Experience and drops come from the creatures you kill on the way.
- **In the log**, one line per floor:

  `PLAY LOOP: dungeon 3: down to floor 2 of 12; the walk owes 2 days`

### 8. Take the last floor's stairs

- **On screen:** back in the capital, with the map open. The map's status line shows the new day. Its second line says
  what the walk did and what the days away did. The dungeon you walked is gone from the map.
- **In the log**, the clear, then the capital with the days away, then the dungeons standing:

  `PLAY LOOP: cleared dungeon 3 on day 12: +1.0 empire points; level 4: back to the capital`

  `PLAY LOOP: in the capital on day 12, tier 1: 9 dungeons standing, 0 cities fallen; level 4, 310 experience into it; while away, days 0 to 12: dungeons resolved: 5 (the dungeon on Outpost (-2,-1)); cities hit: Outpost (-2,-1) lost 750 defence and 1200 people; cities fallen: none; 1 surge brought 8 dungeons`

### 9. Check the dungeon you noted in step 4

The days away are the two numbers after "while away, days": 12 days in the example above.

- **If it is still standing:** its city screen line, and its `standing:` line, show its timer lower by the days away.
  9 days before a 12-day walk would have resolved it. A 3-day walk would leave "6 days until it bites".
- **If it resolved:** it is gone from its city screen and from the `standing:` lines. Its number is in the capital
  line's "dungeons resolved". Its city's status line shows the defence and people that the capital line's "cities hit"
  says it lost.
- **The map's second line names at most three cities.** The log's capital line names every city hit.

### 10. Walk a second dungeon, and die in it

- **Walk** another dungeon, and fight until the character dies. Three seconds after the death it stands up, and the
  game goes back to the capital.
- **On screen:** the capital with the map. The map's second line begins "Day ...: you died in the dungeon ..." and
  says the days the death cost.
- **In the log:** the death, then the capital, whose days away include the death's own days. An ordinary dungeon
  resolves at once when you die in it, so it is among "dungeons resolved", and its city among "cities hit":

  `PLAY LOOP: died in dungeon 7 on floor 3 of 8: the death cost 5 days: back to the capital`

  `PLAY LOOP: in the capital on day 20, tier 1: 10 dungeons standing, 0 cities fallen; level 5, 40 experience into it; while away, days 12 to 20: dungeons resolved: 7 (the Timed dungeon on Bulwark (1,2)); cities hit: Bulwark (1,2) lost 300 defence and 600 people; cities fallen: none; no surge`

### 11. Stop, press Play, and Continue

- Stop the game, and press Play again.
- **On screen:** the character select screen, with your character's row. Click the row.
- **In the log:**

  `PLAY LOOP: the front end is open: 1 saved character to continue, or New character to make one`

  `PLAY LOOP: continuing 'Ash': opening the capital`

- **Then:** the capital and the map, as at the end of step 10, and the capital's line. It says no days away, because
  no walk has just ended.

**A character saved in the middle of a walk** opens the dungeon level instead, and goes on walking:

`PLAY LOOP: continuing 'Ash': opening the dungeon level, to resume the walk it was saved in`

## If a step does not show what it should

Note the step, and copy its PLAY LOOP lines. A line that is missing means that step did not run. A line whose figures
disagree with the screen means the screen or the line is wrong, and the line says which figures the game held.
