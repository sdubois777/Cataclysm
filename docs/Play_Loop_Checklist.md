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

- **On screen:** a nearly empty level, and the empire map open over it. The map's status line begins "Day 0", says
  "12 dungeons standing", and ends with the gold balance, "0 gold" for a new lethality mode. Each city's box shows its tier and the share of its defence left, "Outpost 100%".
  **M** closes the map and opens it again.
- **The new character is level 1, with no experience.** Close the map with **M** and look at the bottom-left corner:
  the lowest bar, under health, is the level and experience bar. It reads "Level 1: 0.0%" and is empty. Health, mana
  and the shield, when there is one, are full. Issue
  [#2744](https://github.com/sdubois777/Cataclysm/issues/2744): until then a new character arrived at level 20 with 30
  passive points. A level 1 character has 1 passive point and 1 attribute point. **Nobody has played tier 1 at level 1
  yet**, so floor 1 may be too hard; note how it goes.
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

- **On screen:** the next floor. Experience and drops come from the creatures you kill on the way. **Gold** lies on
  the floor as a name such as "12 gold" in a muted gold, and is picked up without a click when you come within 15
  metres of it, as crafting materials are. Gold is not put in the bag.
- **In the log**, one line per floor:

  `PLAY LOOP: dungeon 3: down to floor 2 of 12; the walk owes 2 days`

- **Watch the level and experience bar** in the bottom-left corner as you kill. It fills with each kill that grants
  experience. When it fills, the level number rises, the bar starts again from the remainder, and maximum health and
  mana rise. Health and mana themselves do not rise with the maximum.
- **In the log**, one line for each level gained:

  `PLAY LOOP: level 2 reached, from level 1: 3120 experience into it, 46280 more to level 3`

- **Pick up a piece of gear** by clicking its name on the floor; gear is never picked up without a click. Press **I**
  to open the inventory and hover the piece: the comparison's last two lines are the basic attack's hit, and its hit
  on the health of a creature of the run's Cataclysm, before and after wearing it. A piece of "increased damage
  against" another Cataclysm's enemies moves neither. **Right-click** the piece to wear it.
- **In the log**, one line for the piece picked up and one for the piece worn. The hit on a creature is the plain hit
  with the weapon's Slashing bonus and the damage against that Cataclysm added; a Greataxe +0 with nothing else worn
  hits for 57.6, and 63.4 on a creature's health:

  `PLAY LOOP: picked up Quality Torc of Warding, +0`

  `PLAY LOOP: wore Quality Torc of Warding in Necklace: basic attack hit 57.6 to 57.6; on a Demonic creature's health 63.4 to 97.2`

### 8. Take the last floor's stairs

- **On screen:** back in the capital, with the map open. The map's status line shows the new day. Its second line says
  what the walk did and what the days away did. The dungeon you walked is gone from the map.
- **In the log**, the clear, then the gold the walk picked up, then the capital with the days away, then the dungeons
  standing:

  `PLAY LOOP: cleared dungeon 3 on day 12: +1.0 empire points; level 4: back to the capital`

  `PLAY LOOP: dungeon 3's walk picked up 8,412 gold; 8,412 gold held`

  **Gold:** the map's status line ends with the same balance, "8,412 gold". A first walk's two figures are equal; after
  that the balance is the walks' gold added together.

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

  `PLAY LOOP: dungeon 7's walk picked up 1,930 gold; 10,342 gold held`

  The death keeps the gold picked up before it: the balance is step 8's plus this walk's.

  `PLAY LOOP: in the capital on day 20, tier 1: 10 dungeons standing, 0 cities fallen; level 5, 40 experience into it; while away, days 12 to 20: dungeons resolved: 7 (the Timed dungeon on Bulwark (1,2)); cities hit: Bulwark (1,2) lost 300 defence and 600 people; cities fallen: none; no surge`

### 11. Buy a city upgrade with gold

- **Open a city's screen** from the map. Its slot line ends with the balance: "0 of 3 upgrade slots filled.   10,342
  gold". Each upgrade it can build has a button ending with its price, "--  10,000 gold" at difficulty tier 1. One the
  character cannot afford is greyed and says how much more it needs: "Costs 10,000 gold; 2,500 more needed."
- **Click an upgrade you can afford.** It moves under "What this city has", and the balance falls by its price.
- **In the log:**

  `PLAY LOOP: bought 'Increase max defense by 20%' on Outpost (-3,0) for 10,000 gold; 342 gold held`

  **Gold:** the map's status line ends with the same balance, "342 gold".

### 12. Open the empire upgrade tree, buy a point, and respec

- **Click "Empire upgrade tree"** in the top right corner of the map. The tree's screen opens on the Architect branch,
  one box per node with its name and points: "Structural Integrity  0/10". The line under the title shows the bank:
  "1.0 empire upgrade points banked: 1 to spend". A node waiting on a system the game does not have yet is greyed --
  Coinage, in the Treasury branch, waits on gold's slice 6 -- and the cursor on it shows what it waits on.
- **Click Structural Integrity.** It shows "1/10" and the bank falls by one point.
- **In the log:**

  `PLAY LOOP: bought a point in Structural Integrity (Architect): 1 of 10; 0.0 empire points banked`

- **Click Respec**, under the branch. It reads "Press again to respec: 5 days of the run". Click it again. Structural
  Integrity shows "0/10", the bank is back, and the map's status line shows a day 5 days later (10 on Hardcore, 15 on
  Heretic). Any other click before the second press cancels it.
- **In the log:**

  `PLAY LOOP: respecced the empire upgrade tree: 1 point back, 1.0 points banked; it cost 5 days, day 20 to day 25`

- **A centre capstone's choice is not reached in one session**: the first, Foundations of the Empire, opens at 25
  points spent in the tree. When it has, click it in the fifth list entry, "The centre", then one of the three options
  the list shows in its place:

  `PLAY LOOP: chose The Delver for Foundations of the Empire`

- **During a walk** the screen opens from the map as well, to be read, and every click on a node or on Respec says
  "The empire upgrade tree can be changed in the capital, not during a walk." and changes nothing.
- **Close** the screen with its Close button.

### 13. Buy Foundation and see a city's maximum defence rise

- **Once 3 points are banked** -- each dungeon defeated adds to the bank, as the line under the tree's title shows --
  open the tree in the capital and buy 2 points of **Urban Planning**, which opens Foundation, and then 1 of
  **Foundation**. Each is a "bought a point" line, the last:

  `PLAY LOOP: bought a point in Foundation (Architect): 1 of 10; 0.0 empire points banked`

- **Close the tree and click an Outpost at full defence** on the map -- not the one step 11 upgraded, whose maximum is
  1200 and now reads 1260. Its screen reads "defence 1000 of 1050 (95%)"
  and "5000 of 5500 people": the maxima rose -- 5% for Foundation's point, 10% of an Outpost's people for Urban
  Planning's two -- and what the city holds did not. The map shows the Outpost at 96%. That is the rule for a maximum
  the tree raises mid-run, as for a player's own health: raising the maximum raises nothing. A run begun after this
  builds every city full at the raised maximum, "1050 of 1050".
- **Keep the points** for step 15.

### 14. Spend a passive point and an attribute point, and choose a skill

Each of these writes the character record at once, so stopping the game straight afterwards keeps it. Issue [#2763](https://github.com/sdubois777/Cataclysm/issues/2763):
until then they waited for the next item, death or trip to be written, and stopping Play In Editor first lost them.

- **Press P** for the passive tree, and click a node you can reach: the first node of your class's tree. It shows one
  more point, and the count of points left falls by one.
- **In the log:**

  `PLAY LOOP: spent a passive point in Fervour (Masochist): 1 of 1; 1 of 2 passive points spent`

  A capstone's option, once its tree holds enough points, is a line of its own:

  `PLAY LOOP: took the option Reprisal Wave at the capstone The First Vow; 26 of 33 passive points spent`

- **Press C** for the character sheet (since issue [#2770](https://github.com/sdubois777/Cataclysm/issues/2770); C opened
  the character creator before). Click the button **Ferocity 0**. It reads "Ferocity 1". **Press C again**: the sheet
  closes and the character moves as before.
- **In the log:**

  `PLAY LOOP: spent 1 attribute point on Ferocity: 1 in it; 1 unspent`

- **Press K** for the skill screen, click the **Heavy** slot, then a skill from the pool. Then click **Clear the
  slot**, and choose it again.
- **In the log**, one line for each:

  `PLAY LOOP: put Pyroclasm in the Heavy slot`

  `PLAY LOOP: forgot the Heavy slot's skill choice: it shows its default again`

- **A reset** gives the points back, and has a line of its own. Only the console resets, with
  `Cataclysm.ResetPassivePoints` and `Cataclysm.ResetAttributePoints`; **skip them here**, so step 15 has the points
  to show:

  `PLAY LOOP: reset the passive tree: 3 points back; 3 to spend`

  `PLAY LOOP: reset the attribute points: 2 points back; 2 unspent`

- **Stop the game now**, straight after the last choice, without walking or picking anything up. Step 15 shows what
  was kept.

### 15. Stop, press Play, and Continue

- Stop the game, if step 14 has not, and press Play again.
- **On screen:** the character select screen, with your character's row. Click the row.
- **In the log:**

  `PLAY LOOP: the front end is open: 1 saved character to continue, or New character to make one`

  `PLAY LOOP: continuing 'Ash': opening the capital`

- **Then:** the capital and the map, as at the end of step 13 -- the city still holds the upgrade, the balance is
  what the purchase left, the empire upgrade tree holds what steps 12 and 13 left in it, and the Outpost of step 13
  still reads "defence 1000 of 1050": the tree's bonus is applied once, on loading, and not saved into the city -- and
  the capital's line. It says no days away, because no walk has just ended.
- **And what step 14 chose is still chosen.** P shows the passive point in its node, C shows "Ferocity 1" on the
  character sheet, and K shows the skill on the Heavy slot. If any of them is gone, the save did not hold it: copy step
  14's PLAY LOOP lines and note which.

**A character saved in the middle of a walk** opens the dungeon level instead, and goes on walking:

`PLAY LOOP: continuing 'Ash': opening the dungeon level, to resume the walk it was saved in`

### 16. Raise a piece at the forge, and combine two stones

The walks of steps 5 to 10 leave upgrade stones in the bag: a +1 or +2 stone is a Common material, collected without a
click like any other.

- **Click "Forge"**, under the empire upgrade tree's button in the top right corner of the map. Its screen lists every
  worn piece, then every carried one, and the stones held: "Upgrade stones held: +1 x16, +2 x19, +3 x5".
- **Click a piece**, then **Upgrade to +1**. The line under the buttons asks first: "Press again to upgrade to +1: two
  +1 stones, residue 52 to 77, 1 day of the run." Click it again. The piece's button now ends "+1", two +1 stones are
  gone, and the map's status line shows the next day. Any other click before the second press cancels it.
- **In the log:**

  `PLAY LOOP: upgraded Everyday Torc of Warding from +0 to +1 with two +1 stones, 14 left; residue 52 to 77; it cost 1 day, day 25 to day 26`

- **Click "Combine two +1 stones into one +2".** Two +1 stones become one +2, and no day passes.
- **In the log:**

  `PLAY LOOP: combined two +1 upgrade stones into one +2: 12 +1 and 20 +2 held`

- **A worn piece raised past the Consumption Threshold** asks that question on the second press, and a third press
  raises it and marks the character, as wearing a piece past it does.
- **During a walk** the screen opens from the map as well, to be read, and every Upgrade and Combine says "The forge can
  be used in the capital, not during a walk." and changes nothing.
- **Close** the screen with its Close button.

## If a step does not show what it should

Note the step, and copy its PLAY LOOP lines. A line that is missing means that step did not run. A line whose figures
disagree with the screen means the screen or the line is wrong, and the line says which figures the game held.
