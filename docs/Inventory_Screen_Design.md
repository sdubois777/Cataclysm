# Inventory and Gear Screen

How the carried inventory and the worn gear are laid out and operated. Written
for issue #854, after the project owner played on 2026-08-23 and reported that
the whole screen was bad and should follow what Path of Exile and Last Epoch do.

**This describes the screen being built, not the screen that exists.** The game
today draws 48 identical cells beside a panel of nineteen gear slots, every item
occupies exactly one cell, and the cells are up to 112 pixels across. Everything
below is the target. The section at the end says which parts have landed.

## What was already decided, and by whom

**Items occupy different numbers of cells.** Decided by the project owner on
2026-08-23 in issue #855: "yes, build item footprints ... a two-handed weapon is
2 wide by 6 high, and the cells themselves get smaller." The same decision states
that this game follows Path of Exile here rather than Last Epoch, which
deliberately removed footprints, and that the choice was made knowingly.

Everything below builds on that. The footprint question is not reopened here.

## What the genre does

| | Path of Exile | Last Epoch | Diablo IV |
| :-- | :-- | :-- | :-- |
| Carried grid | 12 wide by 5 high, 60 cells | A flat grid; an 8 by 12 storage space opens from the same window | 11 wide by 3 high, 33 cells |
| Item footprints | Yes. A ring is 1 by 1, a dagger or wand 1 by 3, larger pieces up to 8 cells | No | No. Every item takes one cell |
| Crafting materials | Share the bag and take grid space | A dedicated crafting material tab, separate from the bag | Separate tabs for consumables, quests and aspects |
| Gear slots | Arranged around a character figure | Arranged around a character figure | A character figure with the slots around it, 10 slots |
| Getting back to town | A portal scroll, usable almost anywhere | A portal, usable almost anywhere | A town portal on a short cooldown |

**The last row is why this game cannot copy any of their bag sizes.** Path of
Exile can afford a 60-cell bag because a player is a few seconds from a stash at
any moment. This game has no town portal at all: the Storage section of
`Cataclysm_GDD_v2.md` states that a player who fills up part way down a dungeon
cannot leave, and a dungeon runs 100 to 150 floors at one day each. A 60-cell bag
here would not be Path of Exile's pressure, it would be a much harsher one that
nobody chose.

**The argument for footprints is friction, and this game already has that
friction from somewhere else.** The general case for variable footprints is that
"the added friction of having to spend time sorting out your inventory to make
space for a few more items helps deter players from just picking up everything",
and that different sizes let a player recognise an item type without hovering.
The case against is that it "can become frustrating faster than the regular,
one-item-per-slot grid, when you spend hours upon hours playing and every 10
minutes you need to stop and clean it up." Both are real. This design takes the
first and manages the second by keeping the bag large enough that packing is
occasional rather than constant.

## The carried grid

**20 cells wide by 12 cells high, which is 240 cells.**

**Why 240 and not Path of Exile's 60.** The Storage section fixed the bag at 48
items and gave the reason: it "is not under pressure over ten floors, and it is
under real pressure over the 100 to 150 a Cataclysm dungeon spans." That is a
tuned figure. Footprints do not change how much a player should be able to carry,
they change how it is measured, so the cell count is chosen to hold the same 48
items rather than to match another game's screen.

The average footprint over all 55 item bases is 4.24 cells, so 48 items need 203
cells packed perfectly. Rectangles of mixed sizes do not pack perfectly; at a
realistic 85 per cent, 48 items need about 239. **240 is that number.** Choosing
72 cells instead, which is the smallest grid that holds a two-handed weapon,
would hold about 14 items and would cut the carrying capacity the design tuned by
seventy per cent without anyone deciding to.

**Why 12 rows.** A two-handed weapon is 6 cells high, so 6 is the floor. Twelve
means a weapon never has to land in an exact-fit column, and it lets the gear
panel beside it be the same height so the two align.

## Cell size

**48 pixels at most, shrinking to fit the viewport, and never below 20.**

The screen today allows up to 112 pixels per cell, which is what the play test
called far too large. Path of Exile and Diablo IV both sit near 48 pixels at a
1920 by 1080 viewport, and a cell has to hold an icon rather than an image.

The whole screen fits: 20 bag cells plus 8 gear cells is 28 across, which at 48
pixels with 4-pixel gaps is 1452 pixels, inside the 1651 the panel is given on a
1920-wide viewport. Twelve rows is 620 pixels plus the header, inside the 778 a
1080-high viewport gives. On a smaller viewport the cell shrinks and nothing
reflows.

## What each item occupies

Width by height, in cells.

| Slot | Footprint | Cells |
| :-- | :-: | :-: |
| Ring | 1 by 1 | 1 |
| Necklace | 1 by 1 | 1 |
| Belt | 2 by 1 | 2 |
| Relic | 1 by 2 | 2 |
| Head | 2 by 2 | 4 |
| Shoulders | 2 by 2 | 4 |
| Gloves | 2 by 2 | 4 |
| Boots | 2 by 2 | 4 |
| Pants | 2 by 2 | 4 |
| Chest | 2 by 3 | 6 |
| One-handed weapon | 1 by 3 | 3 |
| Shield | 2 by 3 | 6 |
| Two-handed weapon | 2 by 6 | 12 |
| Crafting material | 1 by 1 | 1 |

**A shield is 2 by 3 and not 1 by 3**, even though it is a one-handed weapon
everywhere else in this design, because it is the one one-handed piece that is
broad rather than long.

**A crafting material is one cell whatever the quantity.** Materials stack, and
the Storage section already says a stack of any size is one place in the bag.

**These are per slot, not per base.** All four Head bases are 2 by 2. A base
needs its own footprint only if it should differ from its slot, and none does
today. Issue #855 adds the columns to the Item Bases sheet of
`All_Things_Cataclysm.xlsx` that carry this.

## Where the gear slots sit

**A character figure with the eleven body slots around it, and the eight rings in
their own block below.**

The eleven keep the three-column arrangement the gear panel already uses, read
around the figure rather than as a list:

| Left | Centre | Right |
| :-- | :-- | :-- |
| Shoulders | Head | Necklace |
| Gloves | Chest | Relic |
| Main hand | Belt | Off hand |
| | Pants | |
| | Boots | |

**The eight rings are a 4 by 2 block beneath the figure**, and not on the body.
Path of Exile, Last Epoch and Diablo IV all have two rings and can put one on
each hand. Eight is a list, and drawing a list around a figure would push
everything else out to make room for six slots that mean nothing positionally.

**A gear slot is drawn at the footprint of the item it accepts**, in the same
cell unit as the bag, which is what Path of Exile does. The two weapon slots are
therefore 2 wide by 6 high. The panel comes to 8 cells wide by 12 tall, the same
height as the bag.

## Crafting materials share the bag

**They do, and this does not change.** The Storage section already decided it and
gave the reason: the choice about what to leave behind "only exists if everything
competes for the same" bag.

Last Epoch and Diablo IV both hold materials somewhere else, and that is the
stronger convenience. It is not adopted, because those games let a player return
to a stash whenever they like, so nothing is being protected from competition
there. Here the competition is the mechanic.

**A material at 1 by 1 competes very cheaply** — one cell out of 240, where a
two-handed weapon costs twelve. That is a real softening of the rule compared
with today, where a material and a greatsword each cost one slot of 48, and it
is worth watching in play rather than pre-empting.

## What the tooltip says

**What the item is, and what the character's totals would become if it were
worn.**

The first half exists. The second is issue #832 and is not settled here beyond
the requirement. That issue notes that a stat-by-stat comparison only works when
two items grant the same stats, which two items of one base usually do not, and
that showing the resulting totals answers the player's real question without
needing them to line up. This design takes that reading.

**A weapon's tooltip also says its sub type, its weapon type and how fast it
swings**, which is issue #856. A player choosing a second weapon cannot otherwise
see what the choice costs, because a hit's sub type is the one every weapon swung
agrees on and a mixed pair carries none.

## How it is operated

| Button | On a carried item | On a worn item |
| :-- | :-- | :-- |
| Left | Pick it up and hold it on the cursor; a second left click puts it down under the cursor | Take it off onto the cursor the same way |
| Right | Wear it | Take it off into the bag |

This is issue #853, and it is what Path of Exile, Last Epoch and Diablo all do.
Today left click is the only button the screen understands and it means "wear or
take off". A held item is a third place an item can be, besides the bag and the
body, so the rule that no item is ever destroyed has to cover it.

## What has to change elsewhere, and what is holding it

**The Storage section of `Cataclysm_GDD_v2.md` still says "48 slots, four rows of
twelve" and "one item takes one slot whatever it is", which is the opposite of
this document.** That is deliberate and the sentence is left alone here, because
`tools/tests/test_carried_inventory_is_forty_eight_slots.py` reads that exact
sentence and asserts the engine's `SlotCount`, `Rows` and `Columns` match it. The
sentence and the C++ change together when issue #855 lands, and that test is what
keeps them together. Changing the sentence now would only break the guard.

`Save_System_Design.md` states the same 48 and is checked by the same test.

**The bag is a flat array indexed by slot number**, and that is the substantial
work rather than the drawing. Issue #855 lists it: `AddItem` answers which index
an item went to, `UCataclysmWearing` passes carried slot indices throughout, the
`Cataclysm.Equip` console command names a slot 0 to 47, and the save format
records what is carried. "Is there room" becomes "is there a free rectangle".

## What this does not settle

- **A footprint for each item base**, and the workbook columns that carry them. Issue #855. Note issue #331: a change that adds a workbook column cannot be completed from a git worktree.
- **What happens to a save written before the change.** Issue #855.
- **The comparison tooltip's contents.** Issue #832.
- **Sorting and filtering.** Last Epoch has both. Nothing here needs them yet and no issue asks for them.
- **The stash screen.** A different container with its own 600 slots, and it needs issue #529 before it persists at all.
