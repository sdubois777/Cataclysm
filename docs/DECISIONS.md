# Design decisions

Decisions made outside the Google Drive documents, newest first.

**The files in this folder are authoritative.** Edit them directly. They began as
exports from Google Drive, but as of 2026-08-02 the repository copies are the
source of truth and are not synced back; treat the Drive originals as historical.

This log records the *reasoning* behind decisions, which the design documents
themselves do not carry. Each entry names which document it affects, so the
change can be applied there.

Entries below dated 2026-08-02 were written before that switch and carry wording
about folding changes into Drive. Ignore that wording; it is obsolete.

**All six of those decisions have now been applied** to
`Cataclysm_GDD_v2.md` and `All_Things_Cataclysm.xlsx`. The "Affects" line on each
entry records where the change landed. Later entries should say whether they are
applied or still pending.

---

## 2026-08-18 — Items drop from enemies, not from floors, and a rarer enemy adds magic find to its own drops

**Affects:** a new Enemy Drops sheet in `docs/All_Things_Cataclysm.xlsx`, a new
What a Kill Drops subsection in section VI of `docs/Cataclysm_GDD_v2.md`, and
`sim/cataclysm_sim/loot.py`. Applied. Part of issue #44.

### What was missing

The design says loot quantity "is a percentage of whatever the dungeon would
otherwise drop, so 100 means unchanged". **There was no number for it to be a
percentage of.** An earlier entry in this log named the gap plainly: "the lever
that really controls how much good gear a player sees is how many items drop, and
that does not exist yet."

### Four decisions, all the project owner's, all on 2026-08-18

**1. The rate is per enemy, not per floor.**

A per-floor budget was proposed first and rejected. It was derived from the
inventory rule — 48 slots, no way out of a dungeon, one Stash at the middle floor
— and the design document's own statement that "48 slots is not under pressure
over ten floors, and it is under real pressure over the 100 to 150 a Cataclysm
dungeon spans". That produced about three items per floor.

**That derivation was wrong, and the owner caught it.** In their words: "in
arpgs, 95% or more of the loot that drops is left on the ground... out of those
15 per floor, the player might pick up one." The inventory rule bounds what a
player KEEPS, not what falls, so it does not constrain the drop rate at all. The
per-floor budget only looked workable because the wrong reading made the numbers
small enough to manage by hand.

Per enemy is also what the genre does, and it has a second advantage: the floor's
total is then whatever its enemies happened to be, so no second number has to be
invented before the dungeon generator decides how many enemies a floor holds.

**2. A rarer enemy drops better gear by adding magic find to its own drops.**

The owner's proposal was "maybe each rarity of enemy has a higher chance of
dropping their equivalent gear rarity". A direct mapping has no form: there are
six enemy rarities and eight gear rarities. Expressing it as magic find needs no
correspondence, adds to the player's own rather than competing with it, and
introduces no new mechanic — `rarity_step_chance` already multiplies every rung
of the cascade by magic find.

Two alternatives were considered and not taken. Raising the difficulty tier cap
for a better enemy would let a boss drop something the tier otherwise cannot,
which cuts across the difficulty tier being the design's own gate, stated three
times over in section VI. Guaranteeing a floor rarity would remove the
disappointment that makes a good drop feel good.

**3. Every base within a slot is equally likely.**

The owner: "it's fine if the bases are equal chance... they do the same thing."
This was already what `sim/cataclysm_sim/loot.py` did, as an unmarked placeholder;
it is now a decision with a reason. The bases in a slot are alternatives rather
than a ladder — a Helm grants armour, a Hood evasion, a Circlet energy shield —
so none of them is the good one to hold out for, and weighting them would say
otherwise. What gates the quality of a drop is its rarity and the difficulty
tier.

**4. Crafting materials drop on a separate roll.**

They have their own five tiers and their own quantity, and do not compete with
gear for the same drop event. The design already had evidence for this: the
Scavenger node on the empire tree "increases drop quantity of t3 and below
crafting materials by 5% per point", which would also reduce gear drops if the
two shared one roll. **That roll is not yet built.**

### The numbers

| Enemy rarity | Gear drops per kill | Magic find it adds |
| :-- | --: | --: |
| Common | 0.16 | 0% |
| Elite | 0.5 | 50% |
| Legendary | 1.0 | 100% |
| Herald | 2.0 | 150% |
| Boss | 5.0 | 300% |
| Cataclysm Boss | 12.0 | 500% |

**0.16 for a Common enemy is Path of Exile's published figure** for a normal
monster, taken because this design had none of its own and that one has survived
contact with players. It is not a measured equivalent: their figure covers
currency and everything else a monster can drop rather than gear alone, so the
real gear-only rate there is lower. It is the right order of magnitude and is
expected to move.

**The drop count is an expected number, not a chance.** The whole part is certain
and the fraction is rolled, so 0.16 means one Common kill in six drops one item.
A chance could not express a Cataclysm Boss dropping twelve things.

**The magic find column follows the enemy power ladder rather than being a second
invented curve.** `scoring.RARITY_WEIGHTS` rises 0, 0.05, 0.1, 0.15, 0.3, 0.5,
jumping at Boss rather than rising evenly, and this column is that shape times
1000. So a harder enemy is more rewarding in proportion to how much harder it is.
`tools/tests/test_enemy_drop_sheet_matches_the_model.py` checks that proportion
still holds, which is a check against something other than the other copy.

The six numbers are authored in the workbook rather than computed from the power
ladder, deliberately: that ladder is a port of an external model, and a change
made there for power reasons should not silently move drop rates.

### There are six enemy rarities, not eight

Worth recording because it was got wrong in the conversation that produced this
entry. The ladder is Common, Elite, Legendary, Herald, Boss, Cataclysm Boss. It
comes from `RARITY_WEIGHTS` in `sim/cataclysm_sim/scoring.py`, a port of the
DungeonSimulator power model that `CLAUDE.md` names as authoritative, so the
count is not this project's to change. A test now asserts the Enemy Drops sheet
names exactly those six, in that order.

### Sources

[Drop rate, PoE Wiki](https://www.poewiki.net/wiki/Drop_rate);
[Drop rate, Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Drop_rate);
[How Increased Item Quantity and Rarity
Works](https://www.leveling-guides.com/how-increased-item-quantity-and-rarity-works/);
[Equipment and Gear Overview, Diablo IV,
Maxroll](https://maxroll.gg/d4/resources/equipment); [Unique and Set Item
Farming, Last Epoch, Maxroll](https://maxroll.gg/last-epoch/resources/unique-and-set-item-farming).

Diablo IV and Last Epoch publish no comparable per-kill figure; their guides
cover loot tables and boss drops rather than rates. Only Path of Exile's number
was usable.

### What this does not settle

Which gear slot a kill drops for is still an argument rather than a roll. How
many enemies a floor holds does not exist anywhere, so the per-floor total cannot
be stated yet; that waits on the dungeon generator, issue #40, and the enemy
rarity spawn weights, issue #508. The crafting material roll is decided in shape
and unbuilt. Nothing in the engine reads any of this.

---

## 2026-08-18 — The Consumption Threshold is derived per difficulty tier, at 85% of the cheapest route to the expected gear

**Affects:** the Worn Residue and Consumption subsection of section VII and the
Cataclysmic Residue subsection of section VI in `docs/Cataclysm_GDD_v2.md`, and a
new `sim/cataclysm_sim/residue.py`. Applied. Closes issue #697.

### What was undecided

`docs/Cataclysm_GDD_v2.md` said, of the number that triggers the corrupted
double:

> **The Consumption Threshold.** A single fixed number, to be tuned.

It had been undecided since the section was written, and issue #697 raised it as
urgent on 2026-08-18 because dropped items had just begun carrying residue: Worn
Residue went from zero-until-you-craft to non-zero on every equipped piece.

### The decision

Two parts, both the project owner's, made on 2026-08-18.

**One threshold per difficulty tier, not one for the whole game.** In their
words: "Otherwise nobody in the lower tiers would ever cross it." A tier 1
loadout carries roughly a tenth of the residue a tier 8 loadout does, so a single
number set high enough to matter at tier 8 is unreachable at tier 1 and the
mechanic does not exist for most of a first run.

**Derived rather than chosen, at 80% to 90% of the cheapest route.** Their words
again: "simulating a player crafting all of their equipment to the maximum
expected player score per tier without using any of the residue mitigating
crafting materials. Then we can just set the threshold at 80-90% of that value,
so that they have to use the mitigating materials in order to avoid the penalty."
85% was taken as the middle of that band, and the result rounded to the nearest
50 so it reads as a designed number.

### The numbers

| Difficulty tier | Consumption Threshold | Cheapest route to the expected gear |
| :-: | --: | --: |
| 1 | 1,950 | 2,280 |
| 2 | 2,400 | 2,845 |
| 3 | 2,900 | 3,415 |
| 4 | 3,400 | 3,980 |
| 5 | 3,950 | 4,632 |
| 6 | 4,500 | 5,292 |
| 7 | 5,050 | 5,932 |
| 8 | 5,600 | 6,592 |

### Why nothing here was invented

Every input already existed and is stated somewhere else:

| Input | Where it already lived |
| :-- | :-- |
| What a drop arrives carrying | the Gear Rarity sheet, mirrored in `sim/cataclysm_sim/loot.py` |
| What each Forge operation adds | the Crafting sheet, mirrored in `sim/cataclysm_sim/residue.py` |
| The gear expected at the end of a tier | `player_power.reference_character`, written for issue #26 |

Because the threshold is computed rather than typed, retuning a drop residue
band or a craft cost moves every threshold with it and nothing has to be kept in
step by hand. `tools/tests/test_consumption_threshold_matches_the_model.py` fails
when the table in the design document stops matching what the model derives.

### Three things that were excluded, and why

**Affix tier upgrades.** The Potency Crystal costs 10 residue times the affix's
current tier, so taking one affix from T1 to T7 costs 210 and four of them cost
840 — more than everything else combined. They are excluded because Power Score
does not read affix tiers at all: `player_power.power_score` reads level, gear
rarity times upgrade level, gems and resistances. So no affix upgrade is needed
to reach the expected build. A player who upgrades affixes anyway crosses the
threshold sooner, which is the correct direction for a safety limit to be wrong
in.

**The two residue-reducing materials**, Purified Essence and Chaos Stabilizer.
The threshold is what they exist to be spent against, so counting one into the
route it is derived from would make the number define itself.

**The Residue Protocols empire node**, for the same reason.

### The cheapest route turns out to use the worst drops

Promoting a piece from Everyday all the way to Cataclysmic costs 35 residue in
total, because every step is one affix or one imprint at 5. A Cataclysmic drop
instead arrives carrying 300 to 500. So the least residue a maxed character can
carry comes from taking the *worst* drops and crafting them all the way up, and
a good drop is a liability to a player who intends to max out.

That was not designed here. It is the rule section VI already states — "a better
item is therefore more expensive to improve", and a Cataclysmic drop "is not
simply better than a Masterful one" — followed to its end. It is recorded because
it is sharp enough to look like a defect to someone meeting it cold, and
`sim/tests/test_residue.py::test_the_cheapest_start_is_always_the_worst_drop`
is where it would change.

### And a full set of good drops is over the threshold from tier 4

Eighteen Masterful drops average 3,600 Worn Residue against a tier 4 threshold of
3,400, and the gap widens at every tier above. So from tier 4 a player who equips
what they found is marked before touching the Forge.

This is the intended shape rather than an oversight: it is what makes both the
cheap route and the residue-reducing materials worth using. It is stated in the
design document rather than left to be discovered.

### Whether the tools that manage residue are enough

The second question issue #697 asked, and it is answered by the same model.

- **Purified Essence** halves accumulated residue. One use brings the expected
  build back under the threshold at every one of the eight tiers.
- **Residue Protocols** ignores 5% of residue per point. Three or four points do
  the same, at every tier.

Neither is a large investment, which is the point: the threshold is meant to make
a player spend something, not to stop them.

### What this does not settle

Nothing in the engine reads residue yet. Worn Residue, the threshold itself, the
corrupted double and both management tools are unbuilt; that is issue #47. The
thresholds live in `sim/cataclysm_sim/residue.py` and in the design document, and
they are derived rather than authored, so when the engine does need them the
right move is to generate a table from the model — the way the two enemy tables
are generated from `sim/cataclysm_sim/enemy_stats.py` — rather than to type them
into the workbook.

---

## 2026-08-18 — The drop weights are set: rarity falls in two segments, affix tiers halve

**Affects:** the Gear Rarity sheet and a new Affix Tiers sheet in
`docs/All_Things_Cataclysm.xlsx`, `sim/cataclysm_sim/loot.py` and
`sim/cataclysm_sim/affixes.py`. Applied. Part of issue #44.

### What was wrong with both

Two separate rolls on a drop were **uniform**, each shipped as a first value with
a note saying so:

- **Gear rarity.** Every weight was 1, so at difficulty tier 8 one drop in eight
  was Cataclysmic.
- **Affix tier.** The draw was uniform across the reachable tiers, so nearly 15%
  of affixes at difficulty tier 8 came out at T7.

The test guarding the affix draw said in its own docstring: "If this is ever tuned
to lean high, this test is the one to change, and the decision belongs in
`docs/DECISIONS.md`." This is that.

### The point both decisions rest on: a drop is raw material

The design says **adding an affix promotes a piece** — an Everyday item with an
affix added becomes Quality — and applying an enchantment promotes it further, all
the way to Cataclysmic. It also says **crafting has no affix tier gate at all**:
"an affix can be raised as high as the player can afford."

So crafting is the intended route to the top of both ladders, and a drop is what
crafting works on. A top-rarity or top-tier **drop** is a windfall rather than the
expected way to own one, which is what makes both of these rare rather than
punishing.

### Gear rarity: two segments, and one in 25,531

**Set by the project owner on 2026-08-18.** One in 255 was put forward first and
rejected: "1 in 255 is too generous as a base ... I think I want cataclysm items to
drop like.. 1 in 5000, maybe even more."

**The ladder falls in two segments, split where the design already splits it.**
The four lower rarities carry only regular affixes; the four upper ones carry
enchantments and are gated on upgrade level. Each half falls at its own rate with a
step between them:

| | |
| :-- | :-- |
| the four ordinary rarities | each 2.5 times rarer than the one below |
| the enchantment boundary | a step of 8 |
| the four enchanted rarities | each 5 times rarer than the one below |

Giving weights 15625, 6250, 2500, 1000 | 125, 25, 5, 1, which sum to 25,531.

| Rarity | Share of drops | |
| :-- | --: | --: |
| Everyday | 61.20% | 1 in 2 |
| Quality | 24.48% | 1 in 4 |
| Superb | 9.79% | 1 in 10 |
| Masterful | 3.92% | 1 in 26 |
| Legendary | 0.49% | 1 in 204 |
| Mythical | 0.098% | 1 in 1,021 |
| Ascendant | 0.020% | 1 in 5,106 |
| Cataclysmic | 0.0039% | 1 in 25,531 |

**Why not one ratio for the whole ladder**, which was simpler and was the first
proposal. A single ratio cannot make the top rare without dragging Masterful down
with it: aimed at one Cataclysmic in 20,000 it puts Masterful at one in 82 against
one in 26 here. **Masterful has to stay common**, because it is the top of the
ordinary ladder, the design fits its affix values against "a full set of Masterful
gear", and crafting promotes a piece upward from there. It is the supply line
rather than the destination.

**Magic find still matters at these weights.** It multiplies each cascade step, so
+400% takes a Cataclysmic drop from one in 25,531 to one in 5,106.

### Affix tiers: each half as likely as the one below

**Set by the project owner on 2026-08-18**, from three candidates at ratios 2, 2.5
and 3. Weights 64, 32, 16, 8, 4, 2, 1, summing to 127.

At difficulty tier 8, where all seven tiers are reachable: T1 50.4%, T2 25.2%,
T3 12.6%, T4 6.3%, T5 3.2%, T6 1.6%, **T7 0.79%, one in 127**. A four-affix item
carries at least one T7 about 3% of the time.

**The genre weights these, and the old argument for uniform read across from a
different question.** Path of Exile was cited in the code for WHICH tiers a drop
can reach, and it expands that range with item level. How LIKELY each one is, is a
separate matter, and there the higher tiers are rarer rather than equal.

**What the gate does is unchanged.** Which tiers are reachable is still the
difficulty tier plus one, capped at T7, and every tier at or below stays in the
pool. Only the weighting inside that range changed.

### How three copies of each table are kept honest

Each table now exists in the workbook and in the simulation, and two tests compare
those to each other — which passes happily if both were changed together by
mistake. So each sheet also has one test that compares it to **the decision rather
than to the other copy**: the gear rarity sheet must halve-and-step at the ratios
above, and the affix tier sheet must halve at every step and sum to 127.

### What is deliberately not built

**Neither table is tuned against play**, because nothing drops in the game yet.
The shapes are decisions; the exact rates are first values, and both are a column
in the workbook away from changing.

---

## 2026-08-18 — An item drops with anywhere from no sockets to all of them

**Affects:** the Sockets and Gems subsection of section VI and the loadout rule in
section V of `docs/Cataclysm_GDD_v2.md`, a new Item Sockets sheet in
`docs/All_Things_Cataclysm.xlsx`, and `sim/cataclysm_sim/loot.py`. Applied. Part
of issue #44.

### What was missing

A rolled item had no sockets at all. The design fixes the maximum for every slot —
Chest 6, Pants 4, Belt 4, Relic 4, a two-handed weapon 6, a one-handed weapon 3,
Head, Shoulders, Gloves and Boots 2 each, a Ring 1, the Necklace 1, and a potion
slot 1 — and nothing anywhere read them.

**The project owner asked for it on 2026-08-18:** "all items should be able to
drop with 0-n sockets where n is their maximum number of sockets."

### The design already implied this, and that is the strongest argument for it

`game/Data/CraftingMaterials.csv` carries an **Add Socket** craft: a Shattered
Core, 15 residue, three days. If every item always dropped with its maximum
sockets, that craft would have nothing to do. Its existence is the design's own
evidence that sockets were always meant to arrive below the maximum.

It also explains something in section IV. The reference character at each tier
carries every socket filled, and is described as "the **ceiling** at the end of
each difficulty tier ... a calibration reference, not a requirement". Reaching all
45 sockets therefore costs crafting, which is why that wording is there.

### The decision: uniform, with no tier gate

Three shapes were put to the project owner and the first was recommended:

| Shape | Outcome |
| :-- | :-- |
| **Chosen: uniform from none up to the base's maximum** | Exactly what was asked for, and it introduces no new rule |
| Capped by the difficulty tier as well | Recommended and declined. It is what Diablo 2 and Path of Exile do — sockets are gated by item level — and it is the shape this design already uses four times over for gear rarity, affix tiers, upgrade stones and weapon damage types |
| Weighted toward fewer sockets | Declined. It would have needed a curve nobody has chosen |

**So the socket count is the one part of a drop that does not rise with depth.** A
tier 1 Chest can drop with all six. That is stated as a test rather than left as a
comment, because every other number on a drop is gated on the tier and a reader
will assume this one is too.

### One sentence in the design document had to change

Section V said "**Every loadout gives the same 6 gem sockets and the same Power
Score.**" With sockets rolled, two loadouts no longer carry the same number — only
the same *maximum*. It now reads "the same 6 maximum gem sockets". The rule it
protects is unchanged and is still checked: two one-handed weapons at three each
must equal a two-hander at six, or one loadout is worth free sockets.

### How the numbers are guarded, given they are a third copy

The twelve maxima now exist in the design document, in the workbook and in the
simulation. Two of the tests compare copies to each other, which passes happily if
two of them were changed together by mistake. A third does not: **it adds the
sheet up and compares the total against the 45 the design document states
separately.** The gear comes to 41 and four potion slots make 45, so a single
mistyped entry is caught by arithmetic rather than by matching.

### What is deliberately not built

**Gems.** A socket is a count on a rolled item and nothing fills one. That is
issue #46.

**Potion slot sockets.** Four of the 45, one per slot. Potions are consumables
rather than gear, the design says they "contribute through their sockets only",
and nothing rolls one as a drop. They are the difference between the 41 the
simulation models and the 45 the design states, and the totals check says so.

---

## 2026-08-18 — An item is called `<rarity> <base> of <word>`, and the word is one of its own affixes

**Affects:** the gear rarity subsection of section VI of
`docs/Cataclysm_GDD_v2.md`, a new Name Word column on the Affixes sheet of
`docs/All_Things_Cataclysm.xlsx`, and `sim/cataclysm_sim/naming.py`. Applied.
Closes issue #695.

### What was missing

A dropped item had no name a player could read. It had a base row name like
`Head_Helm` and nothing else. The design document said nothing about item names at
all.

### The format

**Stated by the project owner on 2026-08-18:** "Quality Item Type of Interesting
word that fits the item. For example, Everyday Short Sword of Malice or Mythic
Robes of The Night."

So `<rarity> <base name> of <word>`. The rarity is computed from the item's
contents as it always is, the base name is the one already in the Item Bases
sheet, and the word is the decision below.

**One correction was made to the owner's example without asking.** They wrote
"Mythic"; the eight rarities in section VI are Everyday, Quality, Superb,
Masterful, Legendary, **Mythical**, Ascendant, Cataclysmic. Mythical is used, so
there is one word for one thing. Issue #660 was about exactly that, for the
critical strike multiplier.

### The decision: where the last word comes from

Three candidates were put to the project owner, and the research is what made them
three rather than two.

| Candidate | Outcome |
| :-- | :-- |
| **Chosen: the item's own strongest suffix affix** | What Diablo 2 and Path of Exile do for magic items -- "Warrior's Sword of Fire" is named for what is on it. The name tells a player something true |
| A flavour list keyed to the item type | Reads well and always works, but two very different pieces of Robes could share a name and the name would say nothing. Path of Exile does this for RARE items, whose names are two invented words |
| Both words from affixes, no rarity word | The strict Diablo 2 shape and the most informative, but not the format asked for |

**And when an item has no suffix affix, the name stops after the base.** Also the
owner's choice. An Everyday piece carries one affix and it may be a prefix, so
`Everyday Short Sword` is a whole name, and a Cataclysmic piece carries four
enchantments and no regular affixes at all so it is never named after one. Diablo
2 does the same: a magic item with only a prefix has no "of" part.

### What "strongest" means, which the format did not settle

**Highest affix tier, then highest roll within that tier, then first on the item.**

The third is the one that matters and it is not decorative. Affixes are stored in
the order they were drawn and two of them can share a tier and a roll, so without
a final tie-break the same item could be called two different things on two runs.

Tier before roll because a tier is worth a seventh of the affix's top value and a
roll is worth at most a quarter of one tier. A perfect T6 can beat a poor T7 in
value; the name follows the tier, which is the number a player reads off the item.

### The one place this departs from the genre, said plainly

**A prefix affix contributes nothing to the name.** Diablo 2 and Path of Exile put
a word from the prefix affix first; here the rarity has that position. So only the
54 suffix affixes carry a word and the 31 prefixes carry none. A word on a prefix
row would be one that could never appear, which is why it is checked for rather
than merely unused.

### What is deliberately not built

**The engine cannot name an item yet.** The words are in the workbook and in the
simulation, and `game/Data/Affixes.csv` does NOT carry them: `generate_datatables.py`
emits only the columns it knows about, so the generated table the game loads is
unchanged. Adding the column there means a matching field on `FCataclysmAffixRow`
and rebuilding the imported DataTable asset, and it belongs with the engine half of
the loot work rather than ahead of it. Nothing in the engine rolls an item to name.

**Whether each word is a good name for its affix is a judgement and is the project
owner's.** The tests check that every suffix has one, that no two share one, and
that the workbook and the simulation agree. They do not check that "of Warding"
suits a resistance affix.

---

## 2026-08-18 — An item drops already carrying residue, in a band set by its rarity

**Affects:** the Cataclysmic Residue subsection of section VI and the Worn Residue
subsection of section VII in `docs/Cataclysm_GDD_v2.md`, the Gear Rarity sheet of
`docs/All_Things_Cataclysm.xlsx`, and `sim/cataclysm_sim/loot.py`. Applied. Part
of issue #44.

### What the design said before

Residue came from crafting and from nothing else. Section VI opened with "Every
modification made to an item adds Cataclysmic Residue (CR)", so a freshly dropped
item carried none, and Worn Residue was zero until the player chose to craft.

### The decision

**Stated by the project owner on 2026-08-18: "Items should drop with some amount
of cataclysmic residue on them already."**

That was put back to them once with the two sentences it contradicted quoted,
because the design said the opposite and this project has twice had an owner
answer change when the conflicting sentence was read back. It did not change. The
design document has been rewritten to match.

### How much, in three passes

The first two proposals were rejected as too safe, and both are recorded because
the third only makes sense against them.

| Proposal | Top rarity | Why it was rejected |
| :-- | :-- | :-- |
| First | 50 | Half the point at which crafting starts costing days. "CR is manageable. You can be more heavy handed than that" |
| Second | 100 | Exactly that point. Still too safe |
| **Third, adopted** | **300 to 500** | The project owner's own figure |

**It is a band and not a figure, on every rarity.** "All of them should be in
ranges." So two pieces of the same rarity differ, and the bands of neighbouring
rarities overlap: a lucky Superb piece arrives cheaper to improve than an unlucky
Masterful one. Residue is a cost, so that is a real trade rather than a second
ladder running beside rarity.

The rest of the ladder is the top band scaled by the rarity's position on the
eight rungs, down to 38 to 62 on an Everyday piece.

### What that actually costs, by the design's own two formulas

The gold multiplier on a craft is `(CR / 50) + 1` and the craft time penalty is
`CR / 100` days rounded down. So a freshly dropped Cataclysmic piece costs **seven
to eleven times the gold** and **three to five real in-game days per craft**,
before it has been modified at all. Every rarity above Quality arrives past the
100 at which the critical time penalty begins.

**A better item is therefore more expensive to improve and brings its wearer
nearer to being hunted by a corrupted copy of itself.** That is the whole trade,
and it follows from a rule the design already states and this does not touch:
"Worn Residue grants nothing. It is not a resource and it does not make the
character stronger."

### One thing this made worse, and it is filed rather than guessed at

**The number that decides whether any of this is dangerous does not exist.** The
Consumption Threshold is described in section VII as "a single fixed number, to be
tuned", and the project owner said the same: "The actual break point of residue is
the point where it triggers the corrupted dopple mechanic. Which we haven't really
decided yet."

That mattered less when a drop carried nothing. Eighteen pieces are equipped at
once and Worn Residue is their sum, so a fully geared character now sits somewhere
between about 700 and 9,000 from the moment they equip, having crafted nothing.
The threshold has to be set against numbers in the thousands. Issue #697.

**`sim/cataclysm_sim/loot.py` therefore has no ceiling check on residue**, and
says so rather than inventing one. It checks that the bands run upward, start
above zero, and never fall as rarity rises. Treating the 100 at which crafting
starts costing days as a ceiling was wrong twice, and the module now says that
too, because the next reader will have the same instinct.

### A naming collision this caught on the way

The sheet was first called **Loot Rarity**, and a test failed: "loot rarity" was
one of the three names the magic find stat carried before issues #244 and #247
gave it one. A sheet by that name would have reintroduced exactly the ambiguity
those issues removed. It is called **Gear Rarity** instead, which is the design's
own term for the eight-rung ladder and is already distinguished from enemy rarity
in section VII.

---

## 2026-08-18 — A drop rolls its rarity first, from a flat weight one above the tier

**Affects:** a new Loot Rarity sheet in `docs/All_Things_Cataclysm.xlsx`, and
`sim/cataclysm_sim/loot.py`. Applied. The first part of issue #44.

### What was missing

Nothing produced an item. `FCataclysmItem` in
`game/Source/Cataclysm/Items/CataclysmItem.h` has defined what an item IS for some
time — its base, its upgrade level, its rolled affixes, the rarity computed from
those contents — and `game/Data/Affixes.csv` and `game/Data/ItemBases.csv` hold 86
affixes and 56 bases. Two character attributes, magic find and loot quantity, exist
and replicate. **No code anywhere rolled any of it**, and no rarity distribution
existed in the workbook, the simulation or the engine.

`docs/Cataclysm_GDD_v2.md` says so itself, in the crafting materials section: "the
loot tables that would answer it do not exist".

### The first decision: which comes first, the rarity or the contents

Asked by the project owner on 2026-08-18: "Gear drops, rolls for rarity, then picks
however many affixes/enchantments based on that? Feels like that's how it should
work but i'm not sure."

**Decision: the rarity is rolled first, and the contents follow from it.**

It was worth asking, because the design says rarity is *not* a property an item
carries — it is a label for what fills the four slots, which is why adding an affix
at the crafting bench promotes a piece. That constrains what an item **stores**, not
the order a generator works in. The generator uses the label as a step and stores
only the contents; `affixes.rarity_of` recovers the same label from them.

**Rolling the contents directly would not work.** Only eight combinations of
enchantment count and affix count are a rarity at all — `affixes.rarity_of` raises
for every other, including nothing at all and one of each — so rolling the two
counts independently produces something illegal most of the time and needs
rejecting and retrying. Rolling the rarity and reading `RARITY_COMPOSITION` cannot.

**It also fixes something the other order would have broken.** The first plan was to
roll the upgrade level first and let it cap the rarity, because Legendary requires
+4, Mythical +6, Ascendant +8 and Cataclysmic +10. That would have made magic find
unable to do its job: a player with a large bonus at difficulty tier 1 would still
never see a Legendary, because the upgrade level was fixed at +3 before the rarity
was rolled. Rarity first makes the gate a floor instead — a lucky Legendary arrives
at +4 rather than being silently downgraded.

**Those four gates had never been implemented anywhere.** They are stated in the
rarity table of section VI and existed in no code until now.

### The second: the shape of the roll

**The research settles the shape and does not settle the weights, and that is said
plainly rather than glossed over.**

What it settles: **the roll is a cascade from the rarest down**, not one weighted
pick. Path of Exile "rolls for rare, then magic, and any remaining items will drop
normal"; Diablo 2 checks hierarchically with a fallback when the quality it rolled
is unavailable. And **magic find multiplies the chance at each step** rather than
changing the outcome — Path of Exile states that +100% increased item rarity gives
"twice as many magic items, twice as many rares and twice as many uniques", which is
exactly what this implements.

What it does not settle: **none of the three publishes base weights**, and all three
have four or five rarities where this game has eight. So the cascade transfers and
the steepness of an eight-rung ladder does not.

**Decision: the cap is one rarity above the difficulty tier, and that is not a new
mechanism.** The design document says the difficulty tier is its own gate three
times over — gear and gem rarity equal it, the best upgrade stone that can drop is
capped by it, and a weapon rolls damage types up to it — and
`affixes.max_affix_tier_on_a_drop` is the fourth, gating affix tiers at tier plus
one. This is the fifth use of the same shape. Tiers 7 and 8 both reach Cataclysmic,
the same way affix tiers 6, 7 and 8 all reach T7.

**Decision: how the rarities inside that cap are weighted is data, and every weight
is 1 today.** The Loot Rarity sheet gives each rarity a drop weight, and a weight is
its share of every reachable rarity's weight. Equal weights give a flat
distribution.

### Flat is generous, it is shipped anyway, and here is why

At difficulty tier 8 with no magic find at all, one drop in eight is Cataclysmic.
That is far above what the genre does with its top rarities, and it is the first
number that should move.

It ships because **the lever that really controls how much good gear a player sees
is how many items drop, and that does not exist yet.** Tuning the split between
rarities before the quantity exists is tuning half a system. Changing it later is a
column in the workbook and no code change at all. This is the project owner's
standing rule that balance numbers wait until the systems around them can be played,
applied rather than argued with.

### What is deliberately not built

**Diminishing returns on magic find.** Diablo 2 has them, per rarity and weaker the
rarer the tier — uniques `(MF×250)/(MF+250)`, sets `(MF×500)/(MF+500)`, rares
`(MF×600)/(MF+600)`, with ordinary magic items not diminished at all. Every one of
those is a chosen constant, and this module has none. Saturating at 1 is the only
ceiling, and whether that is enough is a question for play.

**The upgrade level a drop arrives at, beyond the floor its rarity forces.** Whether
a drop rolls an upgrade level or arrives at its floor and is raised by the player
depends on the upgrade stone system, which is described in the crafting materials
but modelled nowhere. Left for the next part of #44 rather than guessed at.

**Everything else about a drop**: which base, which affixes, how many items a kill
or a floor produces. Those are the later parts of #44.

**Sources.** [Drop rate, PoE Wiki](https://www.poewiki.net/wiki/Drop_rate);
[Rarity, PoE Wiki](https://www.poewiki.net/wiki/Rarity);
[Magic find diminishing returns, Diablo
Wiki](https://diablo2.diablowiki.net/Magic_find_diminishing_returns);
[Magic find, Diablo Wiki](https://diablo.fandom.com/wiki/Magic_find).

---

## 2026-08-18 — A charge runs along the ground, and the ground decides where it can go

**Affects:** the skill shape subsection of section V of
`docs/Cataclysm_GDD_v2.md`, and `ACataclysmEnemyCharacter` in
`game/Source/Cataclysm/Character/`. Applied. Closes issue #497.

### What was wrong

A charge moved the creature with `SetActorLocation` and built each step's
destination from the previous position with only X and Y changed, so a charge was
horizontal for its whole run whatever the ground under it did. Nothing anywhere
in the charge looked for the floor.

It is not visible today. The only playable level,
`game/Content/Maps/L_Sandbox.umap`, is flat, and a test world built with
`UWorld::CreateWorld` holds no geometry at all — so none of the fourteen Abyssal
Warden tests could tell a charge that follows the floor from one that ignores it.

### What the design already settled, so none of it was decided here

Section V of `Cataclysm_GDD_v2.md` says a charge "runs along the ground and meets
whatever is in the way", and the Abyssal Warden's charge subsection says "it is
stopped by the level, not by bodies". Following the floor, and being stopped by
ground it cannot get onto, both follow from those two sentences.

**Issue #497 said `docs/DECISIONS.md` recorded this as a known limitation left
open by issue #491. It did not.** The 2026-08-09 entry on charge speed names only
the `Leap` and `Blink` modes as deliberately not built. The limitation was written
down nowhere before this entry.

### The first thing that had to be decided: what a charge does at a drop

Ground more than one walkable step below — a ledge, a pit, or a ramp too steep to
walk down. Three candidates were put to the project owner on 2026-08-18:

| Candidate | What it does |
| :-- | :-- |
| Leave the height alone | The creature runs forward off the edge and the movement component's gravity pulls it down. Smallest change, decides nothing new, and could not be proved — a test world is never ticked, so the gravity half is unobservable. |
| Run down as steeply as it could walk | The creature descends at the walkable angle until it meets the floor below. |
| Stop at the edge | A drop counts as being stopped by the level, the same as a wall. |

**Decision: run down as steeply as it could walk.** The project owner chose it in
those words. Stopping at the edge was the largest change and had a cost the other
two do not: a 60 cm gap part way along a lane would end a charge short of the
marker it had already drawn, and a telegraphed attack that marks a place must
arrive at that place.

### The second: whether a charge is stopped by a low obstacle

A charge is swept as a **sphere of the capsule's radius centred at the capsule's
centre**, which for the Abyssal Warden leaves 66 cm of clear air beneath it. So a
charge passed through anything shorter than 66 cm, while the creature's capsule
reaches the floor and would really collide with it.

**Decision: a charge stops on anything it could not walk up.** The project owner
chose it. The consequence is a constraint on level dressing rather than on the
creature: a crate in a charge lane ends the charge at the crate. A kerb does not.

### Where the two limits come from, and why they are not new numbers

Both are the character movement component's own figures for walking, read at the
moment they are used:

| Limit | Where it comes from | What it decides |
| :-- | :-- | :-- |
| The steepest slope | `GetWalkableFloorZ()`, the cosine of the walkable floor angle. 44.765 degrees by default, which is very nearly one centimetre up for each one along | The steepest ramp a charge follows, and the fastest it descends a drop |
| The tallest single step | `MaxStepHeight`, 45 cm by default | The tallest lip it mounts, whatever the frame rate |

The slope limit alone would have made the behaviour depend on the frame rate for
a vertical lip, because it is proportional to how far one step travels: a 25 cm
doorway lip would stop a charge at 60 frames a second and not at 30. The step
limit alone would have let a charge climb a 60 degree ramp. The larger of the two
is what a walking character would manage, which is the rule both come from.

**No new tunable number was introduced.** How far down the charge looks for the
floor is derived rather than picked: the creature descends at most the walkable
angle for every centimetre of lane it has left, so a floor lower than that cannot
be reached before the lane ends, and looking further could not change the run.

### What is deliberately not built

**A charge still does not use the character movement component to move.** Driving
it through the movement component would get floor-finding, step-up and gravity
for nothing, and would give up the exact control over the lane that the charge
needs — the run has to end exactly where the marker was drawn, and a movement
component that slid along a wall would not. Issue #497 raised that as the larger
of its two candidate fixes and it stays unbuilt.

**The lane MARKER is still drawn flat, at the creature's own foot height.** This
entry is about where a charge goes, not about where the telegraph is drawn, and
the two now disagree on a slope: the creature follows the ground and the
rectangle on the floor does not. Every marker in the game has the same problem,
because they all flatten their aim point through one function that assumes the
creature's own floor. Issue #690 has it. It cannot be seen or judged today for
the same reason this could not be: there is no sloped level to look at.

---

## 2026-08-17 — A character has its own maximum critical strike chance, which only ever goes down

**Affects:** `docs/Cataclysm_GDD_v2.md`, the caps table in section VII.
`sim/cataclysm_sim/character.py` and
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.h` both gained
the per-character ceiling. Closes issue #680.

### The question

The Enchantments sheet of `docs/All_Things_Cataclysm.xlsx` carries a downside
reading "Your critical strike chance cannot exceed 30%-50%". The cap was a single
constant shared by every character — `HARD_CAPS["crit_chance"]` in the model and
`CritChanceCap` in the engine — so there was nowhere for a personal ceiling to
live and the enchantment could not do anything.

It does not contradict the ruling of the same day that the chance is hard-capped
at 100%. This is the opposite direction: a character with the enchantment has a
*lower* ceiling, not a higher one.

### The answer

**The project owner ruled on 2026-08-17 that the enchantment stays**, so a
character carries its own maximum critical strike chance rather than sharing one.

**It can only ever be lowered.** The ceiling is itself bounded by the shared
100%, so a value above it is ignored. That is deliberately the opposite of
maximum resistance, where one enchantment raises the cap to a ceiling of 90% and
the model records both `RESISTANCE_CAP` and `MAX_RESISTANCE_CEILING`. Critical
strike chance needs only the one figure, because nothing raises it.

**The strictest enchantment wins.** Two saying 50% and 30% leave a character at
30%. A ceiling is a promise about the most you can have, so the lowest is the
only one that can be kept.

### It has to bound two things, not one

The chance reaches a hit by two routes since issue #657, and only one of them
passes through the attribute.

| Route | Where it is bounded |
| :-- | :-- |
| The character's `CritChance` attribute | `PreAttributeChange`, when the attribute is written |
| A skill's own stated chance, which travels with the hit | Where a landing hit is assembled, because it never passes through the attribute at all |

Without the second, a skill stating 100% on a character an enchantment had capped
at 30% would have critically struck every time. That is the leak this decision's
tests were written for and it is proved by breaking it.

### It is not a character sheet stat

The sheet stays at 46 stats. A ceiling does not measure anything a character can
do; it bounds another figure that does. No affix grants it, nothing scales it,
and it has no baseline of its own beyond the shared cap. The model keeps it off
the sheet for the same reason, as a field on `Gear` rather than an entry in
`DEFAULT_STAT_LINE`.

### Nothing lowers it yet

No code reads an enchantment's text into a number — that is issue #45 — so every
character sits at 100 and no behaviour changes. What is built and tested is the
arithmetic that consumes it.

---

## 2026-08-17 — A multiplicative damage reduction is a "more" multiplier, and the 75% cap does not reach it

**Affects:** `docs/Cataclysm_GDD_v2.md`, the Damage Calculation subsection of
section VII, which now describes two damage reduction buckets.
`sim/cataclysm_sim/damage.py` and
`game/Source/Cataclysm/AbilitySystem/CataclysmDamageCalculation.cpp` both gained
the second bucket. Closes issue #665. Filed issue #682, which is about the node
values rather than the mechanism.

### The question

Twelve passive tree nodes — eleven in the Bulwark tree and one in the Saboteur
tree — grant damage reduction and say "(multiplicative)". The stat they feed was
additive everywhere it was implemented, there was no per-source storage anywhere,
and the word was defined nowhere. That decided whether the 75% cap added under
issue #644 bound those nodes at all.

### The answer

**The project owner ruled on 2026-08-17 that multiplicative means "more".** In
their words: there is flat, increased, and more; more are multipliers; the same
thing Last Epoch and Path of Exile do.

So the defensive side takes the shape the offensive side already has. The design
states the offensive pipeline as `(base + flat) x (1 + increases) x more1 x more2`
and this is its defensive half: most sources add into one pool, and a source that
says multiplicative is its own factor on the damage that reaches it.

**The 75% cap binds the additive pool only.** Two sources of 20% in the pool
remove 40% and the pool is then capped; two multiplicative sources remove 36%,
because the second removes a fifth of the 80% the first left.

**The multiplicative bucket needs no cap of its own, because it cannot reach
immunity.** Every factor removes a share of what is left, so the product never
reaches zero. That is the same argument the caps table already makes for cooldown
reduction: "No cap needed. The formula cannot reach zero."

### Two bounds exist anyway, and neither is the 75%

**One source may not remove 100%**, because a single factor of exactly 100 would
be exact immunity, which is the failure the 75% cap was added for under issue
#644. Nothing in the design comes near it: the largest multiplicative node in any
class tree is 3% per point over 8 points.

**The combination is bounded too, and that one was found by a test failing.**
"Multiplicative stacking cannot reach 100%" is true of exact arithmetic and false
of the arithmetic the engine runs in. Unreal computes in single precision, which
carries about seven decimal digits, so forty sources of 50% leave 9.1e-13 of the
damage and `100 x (1 - 9.1e-13)` rounds to exactly 100 — immunity, reached by the
layer the design says cannot reach it. Python's floats are double precision and
came out at 99.99999999999991, so the same expression passed in the model and
failed in the engine. Both are bounded now, so they agree by construction rather
than by both happening to stay away from the edge.

### The eleven node descriptions were not reworded

The word "multiplicative" is now defined, so the nodes say something true. The
house term is "more" and rewording them would be more consistent, but the trees
are authored in the separate tool at `C:\Projects\PassiveTreeCreator` and
re-exported over the files in `docs/`, so a rewording is work outside this
repository for a wording that is no longer ambiguous. The definition went into the
design document instead, where the offensive pipeline already lives.

### What this makes possible, which is issue #682

With the mechanism decided, the Bulwark's twelve nodes combine to a much larger
figure than any one of them suggests. The two unconditional ones give 33.1%. With
three conditional ones met and three enemies nearby it is 77.5%, and with the
additive pool also at its cap a Bulwark takes 5.6% of a hit — 94.4% mitigation,
before armour, resistance, block and the energy shield.

That is a question about the node values rather than about the rule, so it is
issue #682 rather than part of this. Nothing runs these numbers yet: no code loads
a passive tree, so no character has a multiplicative damage reduction today.

---

## 2026-08-17 — Critical strike chance is hard-capped at 100%, and one keystone converts the excess

**Affects:** `docs/Cataclysm_GDD_v2.md`, the caps table in section VII and the
ailment overflow rule that follows it, and
`docs/Berserker_Class_Tree_Final.json`, the keystone named Hair Trigger. Closes
issue #658. Filed issue #680, which is the one part left over.

### The question

Two shipped design documents disagreed. The caps table said:

> | Crit chance | 100% | Hard. Above 100% it means nothing. |

and a Berserker keystone named Hair Trigger said:

> "Your critical strike chance is uncapped. Any critical strike chance above 100%
> is converted to critical strike damage at a 2:1 ratio."

Neither the model nor the engine had any route past the cap —
`HARD_CAPS["crit_chance"]` in `sim/cataclysm_sim/character.py` and `CritChanceCap`
in `CataclysmCombatAttributeSet.h` both clamp unconditionally — so the keystone as
written could not be built.

### The answer

**The project owner ruled on 2026-08-17 that the chance is hard-capped at 100% and
nothing raises it.** The critical strike *multiplier* is a separate stat and is
not what the cap is about.

**The keystone converts rather than uncaps.** Its wording is now "Critical strike
chance past 100% is converted to critical strike damage at a 2:1 ratio." The cap
still stops the chance at 100%; what the keystone changes is that the points which
would have been wasted become multiplier instead. That is a thing the hard cap
permits, and it keeps the node's original intent.

**The 2:1 ratio is carried over from the old wording and is a placeholder.** The
owner's ruling did not name a ratio. Two points of chance for one point of damage
is what the node already said, so it was kept rather than replaced with an
invented figure. It is a constant to tune against real play.

### Why the ailment overflow rule does not reach critical strikes

The design states elsewhere that "Chance to apply caps at 100%. Everything above
it becomes magnitude instead", with the reasoning that "Without this rule a build
would hit the cap and every point past it would be dead". That argument reads as
though it applies word for word to critical strike chance, which is what made the
two documents look contradictory rather than merely different.

It does not apply: that rule is inside the ailments section and is about the
chance to apply an ailment. A sentence saying so has been added there, so the next
reader does not have to work it out again.

### What is not settled here

The Enchantments sheet of `docs/All_Things_Cataclysm.xlsx` carries a negative
enchantment reading "Your critical strike chance cannot exceed 30%-50%". That is a
per-character cap *below* 100 and does not contradict a hard cap at 100, but there
is nowhere for it to live: both the model and the engine hold the cap as a single
constant shared by every character. That is issue #680 and is a separate concern.

---

## 2026-08-17 — A skill states its own critical strike chance in a column, and it travels with the hit

**Affects:** `docs/All_Things_Cataclysm.xlsx`, the Weapon Skills sheet, which
gains a Crit Chance column, and its Tags sheet, which gains
`Data.SkillCritChance`. `docs/README.md`'s sheet table records both. No change to
`Cataclysm_GDD_v2.md`: the design already said this and the code did not do it.
Closes issue #657.

### What was already decided, and what was not

The design has always put critical strike chance on the skill. Its stat source
table names "the skill being used" as the source, and the sentence after it is "A
character has no critical strike chance in the abstract"
(`Cataclysm_GDD_v2.md:858` and `:866`). The 5% default was decided on 2026-08-04
and recorded below, and that entry is explicit that it is "a default and not a
floor: a skill that states 1% gets 1%, which is what lets a skill be designed to
crit less than average".

The override half was never built. There was no column in which a skill could
state a chance, so all 398 rows took the default and a skill designed to
critically strike more or less often than average could not exist. What needed
deciding was not whether a skill may state one, but **where the number lives and
how it reaches the blow**.

### Blank means the default, and -1 carries that through the code

A blank cell means the skill states nothing and takes the 5%. Everything between
the sheet and the running ability carries -1 for that, rather than resolving it to
5 on the way.

**Zero cannot be the sentinel**, which is the whole reason one is needed: a skill
built never to critically strike states 0 and must get 0. Turning -1 into 5 early
would also put the default in two places, and two copies of a number can
disagree without anything reporting it — which is the defect that produced issues
#647, #649 and #639 in the first place.

The generator refuses a chance outside 0 to 100, because the cap is hard and the
engine would otherwise clamp a row stating 150 down to 100 and the row would lie.
If issue #658 lifts that cap, this bound moves with it.

### The number travels with the hit, not on the character

**A character holds six skills at once and the ability system has one
`CritChance` attribute.** Writing the skill's figure onto the character would mean
the last skill granted decided the chance for all six, and a projectile — which
lands after the ability that fired it has ended — would read whatever the
character happened to be holding by then.

So the chance is stamped on the granted ability instance, the same way its name,
its tags and its shape parameters already are, and each blow carries the figure of
the skill that dealt it. The character's attribute keeps holding the default, for
every skill that states nothing and for every enemy, which has no skill row at
all.

**Carried as a set-by-caller magnitude rather than as a tag.** Four properties
already travel from attacker to defender on the damage effect — the damage type,
whether the hit swept a volume, whether it is damage over time, and whether it may
critically strike — and all four are gameplay tags, which can only answer yes or
no. This is a figure. Unreal's set-by-caller magnitudes are a map from tag to
float on an effect spec and are the engine's own mechanism for exactly this, so
the key is named `Data.SkillCritChance`, following the engine's own `Data.`
convention for such keys. It is the first use of the mechanism in this project.

### Only the chance moves. The multiplier stays on the character

The design's stat source table puts critical strike **chance** on the skill and
says nothing about the multiplier, so the multiplier is still read off the
attacker's attribute on every hit. A skill can be designed to critically strike
often without also being designed to hit harder when it does.

### No skill states one yet, and that is deliberate

All 398 rows ship blank. Choosing that a particular skill critically strikes more
or less often than average is a balance decision and it belongs to the project
owner. `test_every_shipped_row_states_nothing` in
`tools/tests/test_generate_datatables.py` records where the data stands and names
the rows if that ever changes; it is a record, not a rule.

---

## 2026-08-17 — An enemy keeps four defensive layers, and does not get the player's other two

**Affects:** `docs/Cataclysm_GDD_v2.md`, the Enemy Stat Blocks section of section
X and the Abyssal Warden's design section. Closes issue #488 as answered no.
Filed issue #674, which is what to do instead.

### The question

An enemy has four defensive layers: evasion, armour, resistance and an energy
shield. A player has six: those four plus block chance and flat damage reduction.
Issue #488 asked for the missing two as fields on an archetype, alongside evasion.

**The answer is no**, and the project owner made that call on 2026-08-17 after
being shown the reasoning below.

### The request behind the issue has already been granted, by other work

The issue exists because the project owner said on 2026-08-09 that "enemies get
layers of defense just like the player". At that moment an enemy had **no working
layers at all**: the simulation applied resistance and nothing else (issue #481),
and in the running game every hit resolved untyped so no resistance did anything
on either side (issue #486). Both landed on 2026-08-12.

The Abyssal Warden now stops **66.3%** of a hit through armour and resistance,
where the model previously said 35% and the game applied nothing. The layers that
were asked for exist and work. What was left of issue #488 is the narrower
question of whether two more are wanted, and that is not automatically yes.

### Flat damage reduction on an enemy is a second copy of its own resistance

A player carries eight typed resistances and chooses which to raise. **An enemy
carries one untyped figure**, applied to every hit whatever its type. So step 4 of
the mitigation order already multiplies every hit an enemy takes by
`(1 - resistance/100)`.

A flat damage reduction at step 5 does the identical arithmetic. The only
difference is that the player's penetration cuts into resistance and cuts into
nothing else, so the second copy would be the same mitigation with the counterplay
removed.

**This project has already deleted a mechanic for exactly that reason.** Per-rarity
enemy penetration came out of `sim/cataclysm_sim/enemy_stats.py` because the
hard-coded figures were a second copy of the same mechanic. Where a creature
should stop more, it is given more resistance or a larger armour share, which are
the same arithmetic with an answer attached.

### Enemy block would be the only enemy layer with no player answer

The design justifies enemy evasion by naming its counter: enemy evasion is
answered by area damage. **Block deliberately applies to area damage too**, so
area damage does not answer it. Nothing else does either — there is no
block-reduction stat in the affix pool or in either enchantment table.

Block is also a shield's stat in this design; the Shield base is "the one weapon
whose base defends" and grants 12 block chance. None of the seven creatures in the
vertical slice carries a shield, so an enemy block chance would be a mechanic with
no fiction behind it.

And block is a variance layer. The model folds it into an expectation anyway, so
there it is identical to a smaller flat number; what it adds in play is per-hit
randomness on the enemy's defence, which makes time to kill noisier without making
a fight more readable. That is the opposite of what a telegraph-driven design
wants.

### There was very little room for it in any case

`ENEMY_MITIGATION_CEILING` in `sim/cataclysm_sim/enemy_stats.py` holds every
creature under 89% of a hit stopped, which is what the reference geared character
stops. Measured at the armour cap, every archetype and every rarity:

| Creature | Stops at most | Room left |
| :-- | --: | --: |
| Abyssal Warden | 83.75% | 5.25 points |
| Gatekeeper | 82.50% | 6.50 |
| Hellhound | 82.00% | 7.00 |
| Corrupted Sentinel | 80.00% | 9.00 |
| Succubus | 79.75% | 9.25 |
| Brute | 78.75% | 10.25 |
| Imp | 25.00% | 64.00 |

The layers multiply, so the Warden crosses the ceiling at about **7.7%** of
combined extra mitigation. The design document's own worked example — 25% block
with 30% flat reduction — reaches 90.05% and fails the check at import.

### What to do instead, and it needs no new field

**Enemy flat damage reduction already exists in this design, at the modifier
layer.** `game/Data/EnemyModifiers.csv`, generated from the Enemy Modifiers sheet
of the workbook, carries five: Shield Wall takes 75% less from the front, Phalanx
gains 10% for every ally within five metres, Divine Aegis reduces all damage by
75% below half health, plus Battle Scarred and Armoured Juggernaut.

Every one is conditional and every one names something a player can do about it —
flank it, pull the pack apart, burst it past the threshold. That is where Path of
Exile puts monster damage reduction, through map and unique-monster modifiers
answered by a family of ignore-physical-damage-reduction stats.

**None of the five is implemented.** That is issue #674, and it is the useful
follow-up rather than a sixth base stat.

### Two things in the research were out of date and are recorded as such

The research comment on issue #488 gave three reasons the timing was wrong. Two
have since been fixed and are no longer arguments: the immunity guard now checks
the combination rather than resistance alone (issue #483), and flat damage
reduction now has a 75% cap (issue #644, decided earlier the same day). The
decision rests on the first reason alone, which is the one about counterplay.

---

## 2026-08-17 — The test world begins play by setting the flag, not by gaining a game mode

**Affects:** nothing in the design documents. It is a change to the automation
test harness. Closes issue #654.

### What was wrong

Twenty test files each carried a copy of a helper called
`MakeWorldThatHasBegunPlay`, and it did not begin play. `UWorld::BeginPlay` is one
call to `GetAuthGameMode()->StartPlay()`, and a world built by
`UWorld::CreateWorld` has no game instance and therefore no game mode. The
world's begun-play flag was never set, so every actor spawned into it skipped
`BeginPlay` silently.

**It looked fine, which is why it lasted.** `InitializeActorsForPlay` does real
work — it is what makes a spawned actor's components initialise — so actors
looked alive. Only `BeginPlay` was missing, and what starts there is a
character's regeneration timer, the player's automatic basic attack, an enemy's
attribute application, two creatures' real art and a ground zone's damage timer.

**Two files stated the opposite in comments**, in the same words: "actors spawned
after this point get their BeginPlay called as they spawn, which is the same
order the real game uses". Two others worked around it by calling
`DispatchBeginPlay` by hand.

### The decision: set the flag rather than build a game mode

The issue offered both. Giving the test world a real game mode is the more
faithful route and it was rejected for a specific reason: **`ACataclysmGameMode`
is what supplies the difficulty tier**, and `ACataclysmGameMode::DifficultyTierIn`
answers tier 1 for a world that has none. Tests depend on that, because the tier
decides what armour is worth — armour removes `armor / (armor + 800 x tier)`. A
game mode would have changed armour arithmetic across the suite as a side effect
of a harness fix.

Setting the world's begun-play flag changes exactly the one thing that was wrong
and nothing else.

### The trap in that route, checked rather than reasoned about

`UWorld::HasBegunPlay` asks two things: that the flag is set, **and** that the
persistent level holds at least one actor. If the level were empty at that
moment, the first actor spawned in every test would skip `BeginPlay` while later
ones did not — an intermittent version of the same bug, which is worse than the
bug. `Cataclysm.TestWorld.AnActorSpawnedIntoABegunWorldReceivesBeginPlay` spawns
one and asks it, instead of trusting the reasoning. It passes.

### One test failed, and it would have failed on one machine only

`Cataclysm.Warden.ItHidesItsPlaceholderOnceDressed` asserts a placeholder cylinder
starts visible, then proves the real art hides it. With `BeginPlay` running, the
creature's own `BeginPlay` loads the art first and there is no transition left to
watch. **It fails where the Paragon art exists and passes in continuous
integration, where that art is gitignored** — the worst shape a failure has. It
now asks for a world that has not begun play, which is what the second helper is
for.

### A shared test header, which this project did not have

All thirty world-creating test files carried private copies, which is how the
helper drifted into three shapes under two names.
`game/Source/Cataclysm/Tests/CataclysmTestWorld.h` is the first shared test
header in the project. The nine files whose helper was honestly named `MakeWorld`
and returned a bare world keep that behaviour, now under
`MakeWorldThatHasNotBegunPlay`, so this change alters only the files that were
claiming something untrue.

---

## 2026-08-17 — A critical strike gets its own colour, decided by playing it

**Affects:** `docs/Cataclysm_GDD_v2.md`, the floating damage number paragraph in
section XIII. Closes issues #668 and #661. **This reverses part of a decision
recorded in this log on 2026-08-16**, and the reversal is the point of the entry.

### What was decided before, and why it was wrong

When floating damage numbers were built under issue #518, colour was reserved to
say one thing: where the damage went. Warm near-white reached health, blue was
absorbed by an energy shield or a mana pool, mid grey got through nothing.
Colouring numbers by damage type was rejected to keep it that way, and this log
records the reasoning.

So when issue #649 made critical strikes real, the question of how to show one
was put to the project owner with three options, and colour was the one I argued
against. A critical strike was drawn 1.35 times larger with an exclamation mark on
its figure.

**Then it was played.** On 2026-08-17 the project owner reported:

> Personally I think the font size is too small in general, and crits should be an
> orange/red color even though it might conflict with some backgrounds. As is, you
> really can't tell the difference between a crit and a normal hit even though
> it's slightly bigger and has an exclamation point.

The earlier decision was made by argument, on a screen neither of us had seen. The
new one was made by looking at it. That is the whole reason the sandbox exists.

### What changed

| Before | After |
| :-- | :-- |
| a critical strike's figure ended in "!" | no punctuation on any figure |
| a critical strike was 1.35x size, in the ordinary colours | 1.35x size, in amber orange `FFA31F` |
| every number drawn at the engine medium font, scale 1.0 | every number and the player's own health and mana figures drawn at 1.6 |

**The base size is applied in one place**, inside `ACataclysmHUD::DrawTextCentred`,
because the complaint was that the font is small "in general" rather than about
the damage numbers alone. The overlay still answers relative sizes — 1.0 for a
blow, 0.7 for a damage over time tick, 1.35 for a critical strike — and the
heads-up display decides how big those are. 1.6 is a step rather than an answer,
and only play settles it.

### What the colour costs, stated rather than discovered later

For a critical strike, colour no longer separates a hit that reached health from
one an energy shield absorbed. The information moved rather than went: the text
still prints `12 (+30)` against `12`. Diablo 4 makes the same trade, white for an
ordinary hit and yellow for a critical one.

The rule that colour is not the only channel still holds, and the test that
enforces it — `Cataclysm.Overlay.ColourIsNotTheOnlyChannelSeparatingTwoOutcomes`
— is unchanged and still passes.

### Orange rather than red, and that is not a taste

`#FF3020` is reserved game-wide for the attack telegraph, and a red number beside
a red telegraph is exactly the confusion the reservation exists to prevent. Amber
orange sits far enough away in the green channel to be told apart.

**The test that guards the reservation could not have caught a near miss.** It
compared three named colour constants against the string "FF3020", so `FF3021`
would have passed it, and two of the class's colours were not in its list at all
— the mana bar, added when the mana display was built, and nothing said so. It
now walks every colour the class declares, asserts the count so a new one cannot
be added unnoticed, and measures distance from the telegraph red rather than
checking two strings differ. That is issue #661, closed here because the new
colour is the reason it mattered.

---

## 2026-08-17 — Flat damage reduction caps at 75%, the same figure as armor

**Affects:** `docs/Cataclysm_GDD_v2.md`, the caps table and the Damage
Calculation subsection of section IV. Closes issue #644.

### What was wrong

**Flat damage reduction was the only layer of the mitigation order with nothing
bounding it** — not in the design document's caps table, not in
`sim/cataclysm_sim/damage.py`, and not in the engine. At 100 it was exact
immunity. The design document says "No combination of these layers reaches
immunity. Each has either a cap or a curve that cannot reach zero damage", and
the second sentence was not true of this layer.

Every other layer can be pointed at. Evasion has a soft cap at 60% and covers
direct attacks only. Block removes half a hit, so even certainty is not immunity.
Armor follows `armor / (armor + K)`, which cannot reach 100%, and is capped at
75% besides. Resistance is capped at 70%. This one is a flat percentage off
everything.

**The engine already said where the bound belonged and there was no bound.**
`CataclysmCombatAttributeSet.cpp`, which declares a character's combat
attributes, read: "Damage reduction and retaliation are likewise left unbounded
here; where they need bounding is in the damage calculation, against the final
number, not against each contributing stat." Nothing in the damage calculation
bounded it. That is the third instance this week of a comment describing an
intention nobody built, after the attack speed of issue #647 and the critical
strike chance of issue #649.

**And the one test aimed at this passed because of the number it picked.** Both
`test_stacking_every_defence_still_lets_damage_through` in `sim/tests/test_damage.py`
and `StackingEveryLayerStillLetsDamageThrough` in
`CataclysmDamageCalculationTests.cpp` set flat damage reduction to **90** and
assert damage still gets through. At exactly 100 both fail. Measured before the
cap existed:

```
with damage_reduction=100 the guard test would see 0.0 -> assertion > 0.0 is False
with damage_reduction=90  it sees 7500.0
```

Both now sweep 90, 100 and 1,000.

### The issue's own figure was wrong, and so was the first correction

Issue #644 states that a Ravager spending "all 36 suffix slots" on the affix
reaches 79.95%. Two things are wrong with that. The affix rolls on seven slot
types only — belt, boots, chest, head, pants, rings and shoulders — which is 14
physical pieces because there are 8 rings, not the character's full 36 suffix
slots. And the affix group rule, stated at `Cataclysm_GDD_v2.md:2518` and
implemented at `affixes.py:2232`, allows one affix from any group per piece, so a
piece carries this once rather than twice.

| Figure | Where it came from |
| :-- | :-- |
| 79.95% | issue #644, counting all 36 suffix slots |
| 63.95% | a first correction in this session, counting 14 pieces x 2 suffix slots and missing the group rule |
| **35.95%** | 14 pieces, one roll each at 2.0, plus the Ravager's 7.95 at level 100 |

### 75%, and why not 90

**The genre never leaves this layer unbounded.** Path of Exile caps additive
physical damage reduction at **90%**, stated on its wiki as a hard limit so that
physical damage "remains a threat even for heavily defensive builds".
<https://pathofexile.fandom.com/wiki/Physical_damage_reduction>
Last Epoch caps **every** layer it has — armour at 85%, resistances at 75%, parry
chance at 75%. <https://maxroll.gg/last-epoch/resources/defenses-explained>
Diablo 4 has **no** cap, and needs none, because its damage reduction sources
stack multiplicatively: each removes a share of what is left, so 100% is
unreachable by construction.
<https://maxroll.gg/d4/getting-started/defenses-for-beginners>

**Path of Exile's shape is this project's shape**, which is what decides between a
cap and a restructure. Here the affixes add into one stat and it is applied once
as `damage x (1 - reduction/100)`. Diablo 4's answer would mean storing each
source separately and multiplying them one at a time, which is a change to how
the stat works rather than a bound on it.

**90 was deliberately not copied.** Path of Exile's 90% covers physical damage
alone, one damage type among several. This project's layer covers all eight types,
unconditionally, with no roll and no curve. The widest layer in the game should
not be held to a looser number than the narrow one it is modelled on.

**75 is the armor cap.** Using the same figure gives the design one number for the
most a single unconditional mitigation layer may remove, and
`tools/tests/test_the_damage_reduction_cap_is_one_number.py` fails if either
moves without the other, so whoever retunes one has to decide about the other.

### What it changes, which is nothing anyone can currently build

Gear and a class base reach 35.95% at the absolute most, so the cap is more than
twice anything reachable and no existing build is touched. **That matters beyond
this decision.** The design publishes 15.9% as a fully geared character's flat
damage reduction and 89.9% as what that character's four layers stop together,
and every enemy damage constant in the game was fitted against the 89.9%. A cap
that moved it would move all of them.

What the cap binds is elsewhere. `docs/Bulwark_Class_Tree_Final.json` has twelve
nodes granting this stat — Thick Hide at 3% per point over 8 points, Iron Wall at
4% per point over 6, and ten more — which pass 75% between them. Two enchantments
in `game/Data/EnchantmentsPositive.csv` grant it as well, and the Starvation
six-piece bonus grows with the number of active leech types and states no bound
at all.

### Applied to the layer, not to the stat

The same shape as armor and resistance: the cap sits on the percentage the layer
removes, inside a function, rather than on the number the character carries. A
character may hold 200 and it does nothing. That is what the caps table's "Hard"
means, and it is what the engine's own comment always said should happen.

The floor is zero rather than -100. That is the one place it differs from
resistance, whose floor is negative on purpose because several enchantments
inflict a negative resistance deliberately. Nothing grants negative flat damage
reduction.

### Two questions this raises and does not answer

**The Bulwark tree says "(multiplicative)" on eight of its twelve damage
reduction nodes**, against a stat the model and the engine both treat as
additive-then-applied-once. What that word means here is undefined, and a cap on
the stat may not bind on a node that genuinely multiplies. Filed separately.

**The design has four damage reduction tags** — `Stat.Defense.Global`, `.Melee`,
`.Spell` and `.Ranged` — and about eight enchantments phrased as "you take X%
less damage" from a particular source. Whether those route through this stat or
their own buckets is stated nowhere, and this cap covers only the one stat. Filed
separately.

---

## 2026-08-17 — Critical strikes are rolled per hit, and the model keeps averaging them

**Affects:** `docs/Cataclysm_GDD_v2.md`, the Damage Calculation subsection of
section IV and the floating damage number paragraph in section XIII. Closes
issue #649. Filed issues #657, #658, #659 and #660 along the way.

### What was wrong

**Nothing in the project ever rolled a critical strike.** `CritChance` and
`CritMultiplier` existed as replicated attributes, were clamped, and were written
onto every enemy from its archetype row, and no code in the game read either one.
No hit was ever multiplied, and neither `FCataclysmIncomingHit` nor
`FCataclysmDamageResult` carried a flag saying a hit landed as one, so a floating
damage number could not have marked a critical strike even if something had
rolled it.

**Every enemy was weaker than the model says**, by whatever its critical strikes
were worth. The Gatekeeper, the boss, carries 15% at a 250% multiplier, so it was
dealing about 78% of its designed damage. The Hellhound, the Succubus and the
Abyssal Warden were all short by less.

**And the player never critically struck at all**, for a second and separate
reason. `UCataclysmCombatAttributeSet` initialises the chance to zero with the
comment "supplied by the skill in use", and nothing supplied it. That is the same
defect as the attack speed fixed the day before under issue #647, one attribute
over: an attribute initialised with a comment describing an intention nobody
built, written in a form that reads as a statement of fact. Everything that scales
the stat — the Ferocity attribute, three affixes, eight item base implicits, two
gems and two whole passive tree branches of the Berserker — multiplied zero.

### Where the multiplier goes was already settled, by the model rather than the design

The design's eight-step Damage Calculation table does not mention critical
strikes, and neither does the three-bucket offensive pipeline. `damage.py`, which
issue #649 said to check, has no critical strike code in it either. Two other
modules in the same package answer it and agree with each other:

| File | What it does |
| :-- | :-- |
| `sim/cataclysm_sim/player_damage.py:368-381` | scales the finished per-hit damage by `(1 - chance + chance x multiplier)` |
| `sim/cataclysm_sim/enemy_stats.py:563-568` | the identical form for the enemy side |
| `sim/cataclysm_sim/reference_build.py:264-266` | hands that scaled figure to `damage.Attacker`, which is what then meets mitigation |

So the multiplier applies to the whole finished hit, after the offensive pipeline
and before any mitigation. Not a "more" multiplier inside the pipeline, and not
applied to what survives. That is a quotation rather than a judgement.

### The one deliberate divergence: the model averages and the game rolls

The model never rolls. It multiplies every hit by the long-run average, because
it has no use for a single blow — `player_damage.py:38-41` says so outright, and
`docs/Cataclysm_GDD_v2.md:2409` uses the same expected-value arithmetic to state
what 100 points of Ferocity are worth.

A game cannot do that. The whole point of issue #649 is that a damage number
should be able to say "that one was a critical strike", and a hit that is 15.8%
larger than usual is not one. So the engine rolls per hit. Over many hits the two
produce the same mean, which is what keeps the model's damage targets true of the
game, and `tools/tests/test_the_critical_strike_numbers_are_one_set.py` pins the
three numbers that make the means agree.

### Damage over time cannot critically strike, and the genre settles that

The design documents say nothing about it. Both games in the genre that ship a
layer of this shape say the same thing:

- **Last Epoch.** A damage over time effect is not a hit, and so "cannot be
  Dodged, are not affected by on hit effects, the damage is not scaled randomly,
  nor do they deal critical strikes."
  <https://support.lastepoch.com/hc/en-us/articles/46363283610523-Understanding-Critical-Damage>
  and <https://lastepoch.fandom.com/wiki/Damage_Over_Time>
- **Path of Exile.** Damage over time cannot critically hit. It adds a rule this
  project has no equivalent of: an ailment inflicted *by* a critical hit is
  scaled up, through a separate multiplier from the one hits use.
  <https://pathofexile.fandom.com/wiki/Ailment>

The Path of Exile refinement was deliberately not copied. This project has no
ailment-from-crit multiplier and inventing one would be adding a stat rather than
building the issue.

The design already agreed in shape: it gives damage over time three scaling levers
of its own and calls critical strike chance and multiplier the direct-hit lever
(`docs/Cataclysm_GDD_v2.md:2409`).

### A minion would have inherited the player's critical strikes

`ACataclysmMinion` deals its damage in its summoner's name — both call sites pass
`Summoner` as the attacker — so the character whose critical strike chance the
engine reads when a minion's blow lands is the player. The design forbids exactly
that: "A minion takes neither the summoner's critical strike chance nor its
multiplier", and minion damage sits at the top of its band *because* it has no
critical strike layer to compound with.

The fix is a flag on `FCataclysmHitDelivery` that the minion's two call sites set,
carried to the defender as a new gameplay tag `Keyword.NoCrit`. That is the route
`Type.AOE` and `Keyword.DoT` already take. The tag was added to the Tags sheet of
`docs/All_Things_Cataclysm.xlsx`, which is where every gameplay tag is defined.

**The same path carries the summoner's penetration and nothing stops it.** That
is the other half of the same design sentence and it is a separate cause, so it
is issue #659 rather than part of this.

### Size and text, not colour

Colour on a floating number was given one job when the numbers were built — it
says where the damage went — and this log records rejecting a damage-type palette
for numbers to keep it saying only that. Diablo 4 marks a critical strike with
colour, white for an ordinary hit and yellow for a critical one. This project
cannot follow that without taking the colour axis back, so a critical strike is
drawn larger and its figure carries a mark instead. The project owner chose that
shape on 2026-08-16.

**Only a figure is marked.** "Evaded", "Blocked" and the bare "0" are left alone.
The roll happens before block, armour and resistance, so a critical strike can be
stopped dead by a well-defended target, and an oversized "Evaded!" would be
excitement about a miss.

### Eleven existing tests would have failed at random, and finding them needed a seam

A critical strike is the first random roll in the project that fires on an
ordinary hit. Evasion and block are rolled too, but a defender's evasion and block
chance are zero unless something sets them, so those rolls never fire by accident.
An attacker's chance is never zero: a player holding any weapon has 5% and every
enemy archetype has 5% to 15%.

Two automation tests failed on the first run after the roll was added. Running the
whole suite with the roll forced on found **eleven**, all in
`CataclysmDamageTypeTests.cpp`, all asserting exact damage figures. Nine of them
would have started failing later for no reason anyone could have connected to this
change.

That is what `Cataclysm.CritRoll` is for: a console variable that pins the roll,
-1 to roll normally, 0 to always critically strike, 100 to never. It is also how
the display is judged at the keyboard without waiting for a critical strike to
happen.

**The pin did not work the first time and said nothing.** A console variable in
Unreal remembers who set it, and a write from code is silently discarded when the
command line has already set one. The scoped helper in the tests called plain
`Set(100.0f)` and all eleven tests still failed — the pin was present in the
source and absent from the run. It sets at `ECVF_SetByConsole` now.

---

## 2026-08-16 — The basic attack swings by itself, at the weapon's rate

**Affects:** nothing in the design documents, which state every rule applied
here. Closes issues #647 and the automatic basic attack part of #36.

### Two things that were built and never connected

**A weapon's attack speed never reached the character (#647).** Every weapon in
`game/Data/ItemBases.csv` states a rate — a Greataxe swings 1.28 times a second
— and `FCataclysmItemBaseRow` carries the column, and the `AttackSpeed`
attribute exists and is replicated. It was initialised to zero with the comment
"supplied by the equipped weapon", describing an intention that was never built
in a form that reads as a statement of fact. Nothing ever wrote it, so every
increased attack speed affix multiplied zero.

**The basic attack was granted and nothing ever fired it (#36).** The design says
plainly that it should fire by itself: "The basic attack is on no key. It fires
automatically... Nothing the player presses triggers it", and "The Basic Attack
is automatic, so the weapon's attack speed sets its rate." The rate it would have
read was zero, which is why the first had to be fixed before the second.

### The design decides almost all of it

| Rule | The design's words |
| :-- | :-- |
| It fires by itself, on no key | "The basic attack is on no key. It fires automatically." |
| Its rate is the weapon's attack speed | "The Basic Attack is automatic, so the weapon's attack speed sets its rate." |
| It swings only when there is a fight | "It is income for being in a fight rather than a filler action." |
| A landed swing returns 6 mana | "The automatic basic attack returns 6 mana each time it lands." |
| Its reach comes from the weapon | melee reach is "0.9 metres plus the weapon's length past the fist" |

**The one thing that needed reading rather than quoting** is whether it swings at
empty air. "Income for being in a fight rather than a filler action" settles it:
a character swinging at nothing between fights is a filler action performed by
the game rather than by the player. So it swings only when something it would
attack is inside the reach.

### Mana on hit is paid per landed swing, not per target

The design states the arithmetic it has to satisfy: 6 mana a swing at "a typical
1.3 attacks per second" is "about 8 mana per second". Six times 1.3 is 7.8, so
the 6 is per swing rather than per target. Paying per target would turn an area
basic attack into a mana engine.

It is paid only when the swing actually dealt damage, which is what "lands"
means. A swing that was evaded, or that armour and resistance stopped
completely, returns nothing.

**It rides the mana pool the same way a cost does.** The design sets the return
against the other slots' costs — 8 mana per second earned against a Heavy attack
costing 10 per second — and if one scaled with the pool and the other did not,
that relationship would hold at exactly one level.

### Two defects the tests caught while being written

**The reach was read off the class default object.** A skill's shape is not on
its class: `UCataclysmWeaponSlotsComponent` parses it out of the weapon's row and
writes it onto the granted instance, because one ability class stands for every
skill of that shape. Reading the class default answered zero for every weapon,
so the basic attack would have found nothing in reach and never swung — the same
shape of defect as the attack speed it was waiting on. Caught by the end-to-end
test rather than by review.

**A test passed for the wrong reason.** The check that a swing hitting nothing
returns no mana was written with only one case, an empty world, and
`UCataclysmSkillTemplate::HitTargets` returns before it decides anything when the
target list is empty. So the test never reached the rule it claimed to guard, and
breaking that rule on purpose did not fail it. It now also covers a swing that
reaches an enemy and deals it nothing, which is the case that does reach the
rule, and breaking the rule now fails it.

---

## 2026-08-16 — Health, mana and energy shield come back over time

**Affects:** nothing in the design documents, which already state every rule
applied here. It wires up three attributes that existed and that no code read.
Closes issue #653. Filed issue #654 along the way.

### What was wrong

**Mana was spent and never returned.** Every ability subtracts its cost on use
and refuses to activate without it, and there were exactly four places in the
whole project that wrote the mana value. Three subtracted: the ability cost, the
aura's per-second drain, and nothing else. The one that added was
`ACataclysmPlayerCharacter::Revive`, which fills all three pools when the player
stands back up after dying.

So a play session ended with every ability permanently refused, and the only way
to recover was to be killed. The project owner reported it on 2026-08-16 as
"sometimes when i'm playing all of my abilities just become disabled."

**Two things were supposed to return mana and neither ran.** `ManaRegen` was
declared, initialised, clamped and replicated, and read by nothing. `ManaOnHit`
in `SkillSlots.csv` gives the basic attack 6 mana per hit and is loaded into a
field that only tests reference — and the basic attack cannot fire at all, which
is issue #36.

How fast it happened at the level the sandbox runs at, with a pool of 50:

| Slot | Cost each use | Uses from a full pool |
| :-- | --: | --: |
| Heavy | 1.16 | 42 |
| Special | 3.11 | 16 |
| Ultimate | 11.65 | **4** |
| Aura | 1.55 per second | **32 seconds** |

### The design already answered the shape, so nothing was invented

Under Stat Calculation, `Cataclysm_GDD_v2.md` says the base regeneration rate
"is a small flat value per second, supplied the same way base health is. This
applies to health, mana and energy shield regeneration alike", and that the
percentages players collect are increases to that base rather than percentages
of the maximum. That is exactly what the three attributes hold.

**The energy shield is the one with a delay, and the design gives its number.**
Its section: the shield "refills 3 seconds after the character last took damage",
taking damage again inside that window restarts the wait, and damage over time
restarts it as well. That last clause is load-bearing — the shield absorbs no
damage over time at all, so without it a bleeding character would refill freely
and the shield would be strongest against the one thing it ignores. Health and
mana have no delay: nothing gives them one, and the enchantment that proves the
shield's delay exists names only the shield.

**The design's own check confirms the figures still work.** It says the Heavy
attack should be affordable from mana regeneration alone — 10 mana per second
against 10.9 regenerated, at level 100. At the level the sandbox runs at the
Heavy attack costs 1.16 on a 1.5 second cooldown, which is 0.78 mana per second
against 1.0 regenerated. The property holds.

### The placeholder rates were checked rather than trusted

`UCataclysmVitalAttributeSet` initialises health and mana regeneration to 1.0 as
a placeholder for a character with no class attached. As a share of the pools it
sits beside, that lands inside the design's own band:

| | Sandbox player | The design's three classes |
| :-- | :-- | :-- |
| Health regeneration | 1.0 against 100 health, 1.00% per second | 0.75% (Ravager) to 1.49% (Masochist) |
| Mana regeneration | 1.0 against 50 mana, 2.00% per second | 1.69% (Masochist) to 2.50% (Ravager) |

So wiring them up imports no invented number. The values themselves are still
placeholders and a class stat line will replace them.

### A creature regenerates nothing, and that is a position rather than an omission

Leaving the placeholder alone would have handed every creature in the game a heal
nobody designed — a Brute recovering while the player backs away, and anything
walked away from returning to full. **The design gives regeneration to classes,
not to creatures:** each of the three Demonic class stat lines states a health and
a mana regeneration figure, `EnemyArchetypes.csv` has no column for either, and
`stats_for` in `sim/cataclysm_sim/enemy_stats.py` computes no such figure.

So `ACataclysmEnemyCharacter::ApplyStartingAttributes` writes zero to all three,
beside where it writes armour, evasion and resistance. Zero in the creature's own
numbers rather than a special case inside the mechanism, so a creature that
should regenerate can be given a rate later without touching the mechanism.

### A repeating timer rather than a periodic gameplay effect

The other shape this could take is an infinite periodic gameplay effect. Its
magnitude would have to be attribute-based, because the amount comes from the
character's own three regeneration attributes, and an effect built at runtime
with three attribute-based modifiers is harder to read and cannot be tested
without an ability system. The arithmetic is instead two static functions over
plain numbers, `GainPerStep` and `ShieldMayRefill`, and a timer on
`ACataclysmCharacterBase` calls them.

The step is a quarter of a second while the rate is stated per second. The damage
over time effect uses a whole second per tick and is right to: a burn tick is an
event a player should see land. A pool coming back is not an event, and a bar
that jumps once a second reads as broken rather than as recovering.

### And a mana bar, because its absence is why this was a mystery

The combat overlay built earlier the same day drew health and energy shield and
not mana, though the design has listed mana as a player resource bar since
section XIII was written. Had the bar been there, this would have been "I have
run out of mana" rather than "all of my abilities just become disabled". The
player's corner now stacks health, then mana, then the energy shield when there
is one.

### One thing found along the way and not fixed

**Actors in the automation tests' world never receive `BeginPlay`**, and the
shared helper is named `MakeWorldThatHasBegunPlay`. `UWorld::BeginPlay` only does
anything when the world has a game mode, and a world built by
`UWorld::CreateWorld` has none. That is issue #654. It was found because the
guard on the regeneration clock failed, and calling `DispatchBeginPlay` by hand
made it pass.

---

## 2026-08-16 — Health bars and damage numbers, drawn on the canvas

**Affects:** `docs/Cataclysm_GDD_v2.md` section XIII, which gained a subsection
"Over the World, Not on the Frame". Closes issue #518. Filed issues #649 and
#650 along the way.

### The problem

The sandbox became fightable earlier the same day — a Brute dies to about seven
uses of Molten Cleave — and nothing on screen reported any of it. There was no
user interface code of any kind in `game/Source/`: no heads-up display class, no
widget, and no `UMG`, `Slate` or `SlateCore` dependency on any of the three
modules. Every combat figure settled in the preceding week — enemy armour, the
eight-step mitigation order, damage types, resistance, resistance penetration —
changes only how fast a health bar that did not exist goes down.

That matters more here than in most projects, because this one settles combat
constants by playing them rather than by arguing them first.

### Three decisions, all the project owner's

**How much interface to build: the smallest subset that makes a fight
readable.** A bar over a creature, a number where a blow lands, and the player's
own health. Not the designed heads-up display, which is the empire status bar,
the skill slots with cooldowns and the minimap, and which is issue #49.

**A health bar appears only after a creature has been hurt.** The design document
does not mention enemy health bars at all — the phrase "health bar" occurs twice
in it, once as a metaphor for energy shield and once saying a Heretic player has
none. So this needed an answer rather than a lookup, and the genre gave one:

| Game | A health bar over every enemy? | Source |
| :-- | :-- | :-- |
| Path of Exile 1 and 2 | Setting "Show Mini Life Bars on Enemies", under Options then UI, default unchecked. Even switched on it draws only after an enemy is damaged or moused over | http://www.vhpg.com/show-mini-life-bars-on-enemies/ and https://www.pathofexile.com/forum/view-thread/3739878 |
| Diablo 4 | "Monster Health Bar Option" has three states: Hover Only, Always On, Always Off. A separate setting covers minions | https://www.vhpg.com/diablo-4-monster-health-bar-option/ |
| Last Epoch | Independent switches for enemies, minions and players, and bars framed by rarity rather than gated by who gets one | https://forum.lastepoch.com/t/new-health-bars-coming-in-our-next-patch/15522 |

**Which of Diablo 4's three states is the default is not recorded here, because
no citable source states it.** Player reports say Hover Only; Blizzard publishes
no default. What the citation does support is that Always On is a state the
player has to choose, which is the part this decision rests on.

The Path of Exile behaviour is the one that settles it, and it has a
staff-tagged answer behind it. Asked to add an always-on option, a Grinding Gear
Games reply on 2025-03-31 said monsters are not highlighted unless moused over
or selected, and that "They are meant to be hard to see."

The reason to follow Path of Exile rather than to draw one over everything is
this game's own art direction, which states that the world is deliberately dark
and low-light and that this fights the combat design. A bar over every creature
undoes that before a fight has started. A bar that appears when something is hurt
means "this fight has started" instead of "there is a creature here".

**Damage numbers ship, on by default, with a switch.** Path of Exile 1 and 2 are
the only games in the genre with none at all — the oldest request for them is
from 2011 and none has ever shipped
(https://www.pathofexile.com/forum/view-thread/4815). Diablo 4 and Last Epoch
both ship them with a switch, and Diablo 4's switched-off state still shows
critical strikes: its "Show All Damage Numbers" setting reads "Determines
whether every character's hit damage is shown or just special hits (e.g.,
Critical Strikes)" (https://mythicdrop.com/guide/diablo-4-gameplay-settings).
**No game in the set colours a damage number by damage type.** In Last Epoch that
is an open feature request
(https://forum.lastepoch.com/t/more-floating-numbers-settings/65578).

### Why the canvas rather than UMG

Issue #518 proposed `UWidgetComponent` for both. Reading the installed Unreal
5.8 source changed the recommendation, and the project owner chose the canvas
with the port recorded as a follow-up.

`AHUD::DrawHUD` needs **no new module dependency and no content asset of any
kind**. `AHUD::DrawText` falls back to `GEngine->GetMediumFont()`, so there is
still no font asset anywhere in the project; `AHUD::Project` does the
world-to-screen arithmetic an overhead bar needs; and `AHUD::bShowHUD` already
matches the Heretic lethality mode, which hides the display entirely.
`Cataclysm.Build.cs` is unchanged by this work.

A widget would have cost `UMG`, `Slate` and `SlateCore` — all three, because
`UMG` lists the latter two as private dependencies, so adding only `UMG` does not
compile — plus either a binary `.uasset` that cannot be reviewed in a diff, which
issue #140 records the editor rewriting on open, or a C++ `UUserWidget` whose
widget tree has to be built in `RebuildWidget`, because `NativeConstruct` runs
after the Slate root has already been captured and a tree built there never
appears at all.

**The cost of that choice, stated plainly.** The canvas has no layout, no text
reflow and no localisation. Three of the design's own commitments need all
three: multiple language support, scalable heads-up display elements, and the
four designed display elements themselves. So this is expected to be replaced,
and issue #650 records the port rather than leaving the choice to become
permanent by silence.

### The colours, and the one rule they had to obey

**`#FF3020` is reserved for the whole game.** Section XIII gives it to the attack
telegraph and states there is one telegraph colour — not one per Cataclysm, not
one per damage type — because the marker has to mean "this ground is about to
hurt" everywhere. A health bar in the same red weakens the only signal that must
survive all eight environments. The health bar is `#C0392B`, a darker and less
saturated red, and an automation test fails if any bar colour is ever set to the
reserved value.

**Every bar carries the telegraph's own near-black `#0A0F12` as a backing**, drawn
two pixels wider on every side. The design's readability guarantee — world
surfaces under 30% brightness, effect primaries over 60% — holds a large fill
against a floor but says nothing about a seven pixel bar seen against Demonic
lava or a Celestial wall. The backing makes contrast a property of the bar.

**The numbers are not coloured by damage type**, though
`UCataclysmImpactEffect::ColoursFor` already maps all eight and reusing it would
have been easy. Section XIII permits the damage-type palette and the rarity
palette to overlap only because "the two palettes never share a surface", and a
floating number is a third surface. Colour says where the damage went instead —
reached health, absorbed by a pool, or stopped — which is what the reader
actually wants to know and what Diablo 4 does with its own three-way scheme.

### What it cannot do, and why

**It cannot mark a critical strike.** `CritChance` and `CritMultiplier` exist as
replicated attributes and are set on enemies from data, and no code in the
project reads either one: the string `Crit` does not appear in
`CataclysmDamageCalculation.cpp` at all, and neither `FCataclysmIncomingHit` nor
`FCataclysmDamageResult` carries a flag saying a hit landed as one. That is issue
#649, filed rather than built, because rolling critical strikes is a change to
the damage pipeline and not to its presentation.

### One thing deliberately built the other way round from its neighbour

`UCataclysmImpactEffect::ShouldDrawFor` refuses a hit that never connected, so
that a particle burst means "that landed" rather than "an attack happened". The
damage number does the opposite and draws for exactly those hits: an evaded blow
says "Evaded" and one that armour and resistance stopped shows a zero. A defence
working completely is precisely the case nobody can see today, and it is what
issues #483 and #644 are about.

The one hit that gets no number is one that arrived at something already dead. A
killing blow is not that case and must still be drawn — it leaves health at zero
but dealt real damage getting there — which is why the rule tests both.

---

## 2026-08-16 — The sandbox's enemies carry the design model's tier 1 figures

**Affects:** nothing in the design documents. It replaces invented numbers in
`game/Source/Cataclysm/Player/CataclysmGameMode.h` with figures the model already
states. Closes issue #525.

### What was wrong

A player in the sandbox could not kill anything. With a bare Greataxe at gear
level 0 — no affixes, no attribute points, no passive tree, because none of those
systems exists — killing the Brute took **116 uses of a 1.5 second cooldown**,
nearly three minutes of uninterrupted attacking. The Abyssal Warden took 240.

**Both sides of that were honest except one.** The player's 40.86 attack damage
is what a bare weapon really supplies. The enemies' health was not a design
figure at all: 11,000 was the training dummy's 5,000 times the Brute's
`health_share` of 2.20, and 17,500 was the same 5,000 times the Warden's 3.50.
That is a ratio the model states applied to a base it does not. The header said
so itself, twice, in the words "SANDBOX SCAFFOLDING, NOT THE DESIGNED FIGURE".

### The answer was to read the model rather than to pick a number

Issue #525 suggested lowering the enemies "to eight or ten uses of Molten
Cleave". That is a target rather than a derivation. The model already answers the
question, at the tier the sandbox already stands at:

```
stats_on_floor(rarity="Common", tier=1, dungeon_type="Cataclysm",
               total_floors=50, floor=50)
```

| Creature | Health | Armour | Resistance |
| :-- | --: | --: | --: |
| the average enemy | 250 | 50 | 0% |
| Brute | 549 | 150 | 15% |
| Abyssal Warden | 873 | 175 | 35% |

Three parts of that choice, each read off something the project already decided:

- **Tier 1**, because `ACataclysmGameMode::DifficultyTier` already defaults to 1
  and the entry of 2026-08-15 says why: "the sandbox is where a new character
  stands, and a new character is at tier 1".
- **Common rarity**, because `ACataclysmEnemyCharacter::RarityStep` already
  defaults to 0, which is Common. Nothing was chosen; the rarity the sandbox
  already spawns was written down.
- **The last floor of a 50-floor Cataclysm dungeon**, because that is the hardest
  encounter tier 1 contains and it is the configuration every other figure in
  this project is quoted against. The first floor of a Basic dungeon gives a
  Brute 8 health.

**What it costs an ungeared character**, counting armour, resistance and the
Greataxe's Slashing bonus of 10% to health:

| Creature | Molten Cleave uses | Seconds |
| :-- | --: | --: |
| Brute | 6.8 | 10.2 |
| Abyssal Warden | 14.6 | 21.9 |

Nothing aimed at issue #525's eight-to-ten target and the Brute landed next to
it anyway, which is the reason to trust the derivation rather than the target.

### The Abyssal Warden is spawned as Common, and the design meets it at Herald

At Herald the same stat block is 6,161 health, which is 148 uses and nearly four
minutes. **The sandbox exists to watch what a creature does**, and a fight nobody
can finish shows less of the creature rather than more. The rarity ladder reaching
the engine is issues #39 and #355.

### Armour reached no creature at all, and now does

`ACataclysmEnemyCharacter::StartingArmour` defaults to zero and **nothing outside
a test had ever called `SetArmour`**. So every enemy in the sandbox had none —
including the Brute, which the design document calls heavily armoured and gives
the second-highest armour share of the seven vertical slice creatures.

**It also made the difficulty tier do nothing.** `ArmorReduction` is
`armor / (armor + 800 x tier)`, and with armour at zero that is zero at every
tier, so the whole of issue #514's work was invisible in play.

The two spawners now call `SetArmour` beside the `SetHealth` they already called,
with the figure from the same stat block.

### What was deliberately left alone

- **The training dummy's 5,000 health.** A training dummy is a practice target
  rather than a creature: its job is to outlast an effect so the effect can be
  watched, and the model's Common baseline of 250 dies to two and a half uses of
  Molten Cleave. It also spawns none by default.
- **Every attack damage figure.** The project owner ruled on 2026-08-14 that how
  hard an enemy hits cannot be judged yet, because there is no gear, no character
  level and no attribute allocation to judge it against. Issue #570 records it.
  So the damage side keeps the scaffolding it had and the tests keep checking the
  ratio it claims.

### Two things issue #525 got wrong, found while checking it

Neither is staleness; both were arithmetic errors in the issue when it was
written.

- **Emberhurl hits twice.** Its shape parameters carry `Returns=1` and the
  projectile clears what it has already hit when it turns round, so one use is
  300% of weapon damage and not 150%. The issue said 211 uses against the Brute;
  the answer was 96 even before this change.
- **Pyroclasm is a three-second channel**, not one blow. Its parameters are
  `Duration=3; Interval=0.5; FinalHitPercent=300`, which is at least six swings
  at 400% plus a 300% finisher. The issue said 79 uses against the Brute; the
  answer was 11.

Slashing's 10% is the one thing that genuinely changed since the issue was
written, and it accounts for about 9% off every single-hit figure.

### One thing found and filed rather than fixed here

**A weapon's attack speed never reaches the character.**
`UCataclysmCombatAttributeSet` initialises `AttackSpeed` to zero with the comment
"supplied by the equipped weapon", `game/Data/ItemBases.csv` gives every weapon
an attack speed, `FCataclysmItemBaseRow` carries the column — and nothing writes
it. `UCataclysmWeaponSlotsComponent::ApplyWeaponDamage` writes `AttackDamage` and
nothing else. So the basic attack, which is meant to fire at the weapon's own
rate, has no rate to fire at, and every increased-attack-speed affix would
multiply zero. It is separate work and is its own issue.

---

## 2026-08-16 — A player at zero health dies, stops, and stands back up

**Affects:** `Cataclysm_GDD_v2.md`, section II. Applied: a new subsection, *What
Dying Does at the Moment It Happens*, under the run and death rules. Closes issue
#570.

### What was wrong, measured

A play session on 2026-08-14 recorded the player taking two hits, going from 100
health to 0 in 1.99 seconds, and then **fifty-six further hits over about seventy
seconds each dealing exactly nothing**.

The project owner ruled on the first half: two hits is not a defect, because
there is no gear, no character levels and no attribute allocation yet, so nothing
about survivability can be judged. That is unchanged by this entry.

The second half was real. `ACataclysmCharacterBase::HandleDeath` was inert on the
base and only the enemy overrode it, so a player at zero health was never marked
dead. It kept its movement, its input, its collision and every ability, and it
stayed a legal target. Every later hit computed `FMath::Min(Damage, Health)`
against a zero pool and dealt nothing.

**The clamp was never the defect.** It reports what a hit actually dealt, and the
simulation's `damage.resolve` does the same thing for the same reason. What was
missing is that nothing marked the player dead, so nothing stopped and nothing
stopped attacking.

### The two decisions this needed

The design already states what dying *costs*: 5 days in Standard, 10 in Hardcore,
15 in Heretic, plus a per-piece equipment drop chance in the two harder modes,
plus a respawn at the capital. It states nothing at all about what dying *is* at
the moment it happens — there is no death screen, no prompt, no input rule, no
timing, and no statement of what a player comes back with.

**Decision 1: a player comes back whole.** No health is carried over from the
death and none is withheld. It follows the reasoning the design already uses for
ordinary death: what a death costs is measured in the world and not on the
character, which is the same thing that makes an ordinary death continue the run.

**Decision 2: the delay before standing back up is 3 seconds, and it is
provisional.** Long enough that the death reads as an event rather than a
flicker, short enough that dying repeatedly while testing is not tedious. Nothing
else in the design depends on it. Labelled provisional in the document because
there is nothing on screen at the moment of death and there will be.

### What is deliberately not charged, and why it cannot be

None of the four things the designed penalty is made of exists in the running
game:

| The penalty needs | The game has |
| :-- | :-- |
| a day clock to take days off | nothing; no day clock class exists in `game/Source/` at all |
| a lethality mode, to know whether it is 5, 10 or 15 days | nothing |
| an equipped inventory, to drop pieces from | nothing |
| a capital to respawn at | nothing; the level's player start is the nearest thing |

So the player stands back up at the level's `APlayerStart`, which is the whole of
the designed rule that this game currently has the machinery for. Issue #41
builds the layer that would carry the other four, and #570's closing comment
records them as owed.

### Nothing attacks a corpse

`UCataclysmTargeting::MatchesAttitude` now refuses a character marked dead, so a
corpse is neither an enemy nor an ally. It is asked there because that is the one
place the other two questions — can this hold damage, and which side is it on —
are already asked together, so all four search functions and both public
predicates pick it up from one line.

**It applies to the enemy side too**, for the one tick a corpse exists before it
is destroyed. It is a rule about target SELECTION and not about damage: a hit
already in flight still resolves, because a projectile outlives whoever fired it.

### One rule this changed

**The `State.Dead` tag can now come off.** It was written for enemies, and its own
comment said it "is added loosely and never removed, because the character it is
on is being taken out of the level". That is still true of an enemy. A player is
never taken out of the level — the design says so in as many words — so a player
comes back and the tag comes off. `UCataclysmSkillEffects::ClearDead` is the one
thing that does it, and it reports whether it changed anything, so reviving
something that was not dead is not a silent free heal.

### One defect found and fixed in the same change

`game/Source/Cataclysm/Tests/CataclysmDeathTests.cpp` had its
`#endif  // WITH_AUTOMATION_TESTS` above its last test rather than at the end of
the file, so that test sat outside the guard. It compiles today because the
editor target defines `WITH_AUTOMATION_TESTS`, and it would have failed to
compile in a configuration that does not. The `#endif` is now at the end of the
file.

---

## 2026-08-16 — No enemy stops more of a hit than a geared player does

**Affects:** `Cataclysm_GDD_v2.md`, the Damage Calculation subsection of section
IV and the enemy resistance subsection of section X. Applied: one paragraph added
to the first and four to the second. Closes issue #483.

### What was wrong

`_check_no_enemy_can_become_immune` in `sim/cataclysm_sim/enemy_stats.py`, the
file that defines each enemy archetype's combat statistics, said in its own
docstring that it existed to enforce a rule about a COMBINATION of defensive
layers, and then asserted `kind.resistance < 70.0` and inspected nothing else.
An enemy already had three other layers — armour, evasion and an energy shield.

**A per-field check cannot enforce that rule however many fields it inspects.**
Armour caps at 75% and resistance at 70%, so those two alone stop **92.5%** of a
hit with neither field over its own limit. The sentence the docstring quotes is
real and is in the design document: "No combination of these layers reaches
immunity. Each has either a cap or a curve that cannot reach zero damage."

### Why it could not have been written earlier

Issue #483 says to fix the enemy mitigation pipeline first because the simulation
applied no enemy armour at all. That was true when it was filed on 2026-08-09 and
stopped being true on 2026-08-12, when issue #481 added
`EnemyStats.defender_for` and `damage_taken_fraction`. There was no combined
figure to assert on until then. Nothing in the history argues for keeping the
check on one field; the combination simply did not exist yet.

### The ceiling, and why this figure

**89% of a hit.** The rule is "no enemy stops more than the player does", which
issue #483 proposed and which is a figure the project already had rather than a
new one: `Cataclysm_GDD_v2.md`, *How Long a Geared Character Survives*, totals
the reference geared character's four layers at 89.9%, and
`reference_build.damage_taken_fraction(8)` measures the same character at 89.87%.

Three choices inside that:

- **The player's tier 8 figure, not tier 1.** Their own total falls as the tier
  rises, because armour is divided by 800 x tier: they stop 94.58% at tier 1 and
  89.87% at tier 8, which is the last tier in the game. Taking their weakest is
  what makes the rule hold at every tier rather than only at the one it was
  measured at.
- **A whole percent below it, not 89.87 exactly.** The design document publishes
  the player's figure rounded to 89.9%, so a ceiling at the measured value would
  leave a gap an enemy could sit in and stop more than the player while still
  matching the published number. It also stops the constant churning every time
  an affix is tuned.
- **Stated in `enemy_stats.py` and pinned by a test, rather than computed.**
  `reference_build` imports `affixes` and `affixes` imports `enemy_stats`, so
  computing it in place would be an import cycle. `sim/tests/test_survivability.py`
  is where both can be imported at once, and it holds the constant below what the
  reference character stops and within two points of it, so the ceiling can
  neither become unreachable nor quietly turn into a stricter rule nobody chose.

### The check is on an upper bound, not on a sample

An enemy's armour is a share of its Power Score and **a score has no maximum** — a
deeper floor, a higher tier and every dungeon modifier all add to it. So there is
no largest real armour figure to check. There is a largest effect: armour is the
only defensive layer that grows with the score, `damage.armor_reduction` rises
with it and stops at 75%, and no later step in the order can undo a larger
reduction at an earlier one. `most_damage_stopped` therefore reads each archetype
with armour saturated at its cap, which bounds every real stat block that
archetype could ever have. An archetype whose armour share is zero never gets
armour at all, so the Imp is read with none and its ceiling is its 25% evasion.

**It goes through `damage_taken_fraction` like every other figure**, rather than
multiplying the layers out locally. A second copy of the mitigation order would
not see a layer added to `defender_for` later, which is the failure issue #481
was.

### Where the seven archetypes sit

| Archetype | Could ever stop | Headroom |
| :-- | --: | --: |
| Imp | 25.00% | 64.00 |
| Baseline | 75.00% | 14.00 |
| Brute | 78.75% | 10.25 |
| Succubus | 79.75% | 9.25 |
| Corrupted Sentinel | 80.00% | 9.00 |
| Hellhound | 82.00% | 7.00 |
| Gatekeeper | 82.50% | 6.50 |
| **Abyssal Warden** | **83.75%** | **5.25** |

What the hardest of them actually stops in play is lower: 66.3% at Herald rarity
on tier 8, and 76.78% at Cataclysm Boss rarity, which is the worst real figure
anywhere in the slice.

### Two consequences, stated rather than discovered later

- **An archetype carrying any armour is effectively capped at 56% resistance**
  rather than 70%, because the rest of the room is spent on armour. One with no
  armour keeps the full 70%. The highest in the slice is the Abyssal Warden's
  35%, so nothing is near it.
- **Issue #488, giving enemies block chance and flat damage reduction, now has a
  number to design against.** An Abyssal Warden with 25% block and 30% flat
  reduction reaches 90.05% and fails the check. That is a real constraint on that
  decision rather than an argument against it, and it is the outcome #488's own
  text asked for: "The immunity guard has to grow with it... it becomes urgent
  the moment a third layer exists."

### The old check is kept, under an accurate name and for a different reason

`_check_no_enemy_resists_more_than_the_cap_allows` still asserts that no
archetype declares more resistance than 70%. Its reason is no longer immunity —
the combined check covers that — but that a figure above the cap does not mean
what it says: `damage.effective_resistance` caps at 70%, so an archetype at 95%
would behave exactly as one at 70% and the extra 25 points would be a number in a
table that changes no outcome anywhere. **Neither check implies the other.** An
archetype with no armour and 69% resistance passes both; one at 60% resistance
with armour passes the first and fails the second.

### What was proved rather than asserted

Four breaks through `tools/prove_guard.py`, each restored afterwards:

| Break | What noticed |
| :-- | :-- |
| Abyssal Warden resistance 35 → 60, every per-field value still legal | the new check: "Abyssal Warden at Common rarity could stop 90.00% of a hit... at or above the 89% ceiling" |
| The ceiling raised from 89 to 99 | `test_the_enemy_mitigation_ceiling_is_what_the_player_actually_stops`, and the check's own firing test stopped firing |
| `most_damage_stopped` stops saturating armour, so it samples instead of bounding | `test_the_bound_is_never_below_what_a_real_creature_stops`, 11 failures |
| Gatekeeper resistance 30 → 95 | the kept per-field check, by its new message |

### One thing found and not fixed here

**Flat damage reduction has no cap anywhere** — not in the design document's caps
table, not in `damage.py`, not in the engine. A Ravager spending all 36 suffix
slots on it reaches 79.95%, and with armour and resistance at their own caps that
stops **98.50%** of a hit; at 100 it is exact immunity and nothing clamps it.
That is the same design sentence failing on the player's side, from a different
cause, so it is its own issue rather than part of this one: **#644**.

---

## 2026-08-16 — No affix scales a stun's duration, because chance already does

**Affects:** `Cataclysm_GDD_v2.md`, the crowd control gear paragraph of the Stun
and Anti-Stun-Lock section. Applied: the "not yet decided" paragraph is replaced.
Closes issue #299.

### The question and the answer

Issue #299 asked whether an affix scales a stun's duration, and laid out three
defensible answers: no affix, a capped one, or one paired with a wider immunity
window.

**The answer is the first: there is no duration affix and there will not be one.**
Confirmed by the project owner on 2026-08-16, immediately after they settled the
sibling question #298.

### Two arguments, and the second survives the first being wrong

**Chance to stun is already the second lever.** Issue #298 settled that chance
past 100% lengthens the stun rather than being wasted, to a cap of 3 seconds. So
one affix raises whichever of the two things a stun has that a build has not yet
filled, in that order, and a duration affix would be a second lever on a number
that already has one. That is the same reasoning that gave Cripple, Weaken, Shred
and Madness one affix each rather than three, recorded under issue #300.

**A duration affix would have a cliff, which is a poor shape for an affix.** A
stunned target is immune for 5 seconds, so duration past that point is worth
exactly nothing — nothing was going to stun it again inside that window anyway. A
player who rolled past the cliff would have wasted the roll and nothing in the
interface would say so. **This argument does not depend on the first**: if chance
ever stopped lengthening a stun, this would still be a reason to have no duration
affix.

### The genre argues the other way, and the difference is the whole point

**Path of Exile sells increased stun duration as a common modifier.** It can,
because its immunity window is much narrower than this one: only unique bosses
there become immune while stunned and for 4 seconds after, and ordinary monsters
have no such window at all. So there is no cliff to run into.

Here the window applies to everything that can be stunned. That difference is the
argument, so it is written into the design document rather than only the
conclusion it supports — the same shape the weakening ailments decision used when
the genre disagreed with it.

Sources: [Stun, Path of Exile Wiki](https://www.poewiki.net/wiki/Stun),
[Stun, Path of Exile Wiki (Fandom)](https://pathofexile.fandom.com/wiki/Stun).

### What would reverse it

If the immunity window ever went, or became narrow enough that a longer stun
still took real time away from a target, the cliff would go with it and a
duration affix would become a reasonable thing to sell.
`tools/tests/test_stun_scales_by_chance_alone.py` checks the cap is still below
the window, so that would be noticed rather than assumed.

---

## 2026-08-16 — An affix grants a chance to stun, and chance past certainty becomes duration

**Affects:** `Cataclysm_GDD_v2.md`, the effect magnitude table and the Weapon
Sub-Types subsection; the Affixes and Debuffs sheets of
`All_Things_Cataclysm.xlsx`. Applied. Closes issue #298 and the last quarter of
issue #639.

### The question, and the answer

Issue #298 asked whether an affix grants a chance to stun and at what value, and
noted that answering no was legitimate: Blunt's stun is the sub-type's identity,
and an affix letting any weapon buy into it weakens that.

**The project owner answered yes on 2026-08-16**, with the reason: "Guaranteed
stuns are overpowered if you don't need to invest." So the chance must be bought
rather than given, and **Blunt's 10% is part of the same pool** rather than a
separate mechanic.

**15%, on Necklace, Relic, Ring and Weapon, as a suffix.** The issue asked why it
would be anything else: the four weakening chance affixes are all 15% on those
four slots, and a chance-to-apply affix with a different shape would need a
reason. There is none.

**Last Epoch settled that the affix should exist at all.** Its "Increased Stun
Chance" suffix runs 15% to 25% at its first tier and 131% to 170% at its seventh,
so a shipped game in the genre sells exactly this stat. Path of Exile argues the
other way and has no such stat: stun there is decided by damage against a
threshold, so there is nothing to buy. This design already chose the Last Epoch
shape when it gave Blunt a flat chance.

**No gem.** Nothing in `game/Data/Gems.csv` applies stun, which makes the affix
the only way to buy chance beyond what a Blunt weapon gives free. It is the first
ailment affix with no gem, and two tests that required every affix to have one
were relaxed to their stated direction with the exception named.

### Chance past certainty becomes duration

The project owner proposed this and marked it a question. **It is already the
rule this project uses**, stated by them for damage over time on 2026-08-03: "DoT
chance caps at 100%, anything beyond 100% applies to the magnitude ... if you
have 800% chance to apply it, it gets a 700% multiplier." A stun has no damage,
so its magnitude is its duration.

    100% chance   0.75 seconds
    200% chance   1.50 seconds
    400% chance   3.00 seconds, which is the cap
    800% chance   3.00 seconds

### The cap, which the proposal needed and did not have

**Without one the anti-stun-lock rule stops working, and that is arithmetic
rather than judgement.** The immunity window is 5 seconds. A stun lasting 5
seconds or more means the window expires while the target is still held, so the
next hit re-stuns and the target never acts again — exactly the chain-stunning
the window exists to stop. Blunt's 0.75 second base reaches 5 seconds at 667%
chance, and the weakening ailments show that much is reachable: affixes alone
reach 165% and gems reach 150% each.

**Three seconds, and it is not a new number.** It is the longest stun the design
already contains: the Brute's Heart ten-piece set bonus, the most expensive thing
in the game to assemble. A character reaching the same hold through 400% chance
has spent comparably. It also leaves a stunned target at least 2 seconds to act.

**Stun is the one effect whose scaling stops dead at its cap.** Every other
effect rolls over into duration once its magnitude caps, so scaling never dies. A
stun's magnitude IS its duration, so there is nothing to roll over into. Past the
cap the extra chance really is wasted, and that is the intended outcome.

### Crowd control resistance reduces the total, not the capped chance

So it bites into the overflow as well. A defender at 50 resistance facing 400%
chance sees 200%, which is still certainty and still doubles the duration — but
from twice the investment. Reducing only the capped 100 would make resistance
worth nothing against a heavy stun build, which is the opposite of what a
defensive stat is for.

### An incidental stun obeys the damage threshold

A stun rolled on an ordinary hit is **not** a designed stun.
`UCataclysmSkillEffects::ApplyStun` takes a flag that skips the 10% damage
threshold, and blunt's roll does not set it. Without that, chip damage from a
fast blunt weapon would interrupt a well defended character constantly, which is
half of what the rule exists to stop.

### A constant was renamed

`BLUNT_STUN_SECONDS` became `INCIDENTAL_STUN_SECONDS`. A stun rolled from the
shared pool lasts that long whatever weapon is held, so a name mentioning only
blunt had stopped being true.

---

## 2026-08-16 — A hit's weapon sub-type is the one every swung weapon agrees on

**Affects:** `Cataclysm_GDD_v2.md`, the Weapon Sub-Types subsection. Applied: one
new paragraph. Closes issue #639, except for blunt.

### What was wrong

`FCataclysmIncomingHit` carries `bIsSlashing` and `bIsMagic`, and
`UCataclysmDamageCalculation::Resolve` applies both correctly: slashing deals 10%
more to health, magic strips 10% more energy shield. **Neither was ever set.**
`FCataclysmItemBaseRow::SubType` holds a sub-type for all fourteen weapons and
`UCataclysmWeaponSlotsComponent` knows which weapon is equipped, and nothing
joined the two at the moment of a hit.

### The decision: every weapon actually swung has to agree

**Asked of the project owner on 2026-08-16, and their first answer was a primary
hand** — declare the right hand primary and let its weapon decide. Checking that
against the documents changed it, which is why the check happened:

- `sim/tests/test_player_damage.py` carries a test named
  `test_which_hand_holds_the_shield_does_not_matter`, whose docstring reads
  "There is no offhand position. A Shield is one of the one-handed weapons, so
  the pair is unordered and both spellings are the same loadout." That was the
  owner's own ruling of **2026-08-15**, made after a day spent modelling a shield
  as a separate offhand category and then removing it.
- The engine could not read a right hand anyway.
  `UCataclysmWeaponSlotsComponent` equips one weapon type and nothing tracks two.

**The rule chosen instead: a hit's sub-type is the one every weapon actually
swung agrees on, and there is none if they disagree.** Two Slashing weapons give
a slashing hit; an Axe with a Wand gives no sub-type at all.

**Why that rather than both applying.** Mixing sub-types is how a player carries
more damage types at once, which the Dual Wielding section calls the primary route
to multiclassing. Giving a mixed pair both bonuses would make mixing strictly
better and matching pointless. Costing it the sub-type makes the two a real trade:
two damage types, or one sub-type bonus.

**Why it fits the model.** This project blends two weapons into ONE swing — base
damage summed, attack speed averaged — so there is no first weapon for a swing to
belong to. A primary hand would have been the odd rule out. Most games in the
genre alternate hands instead, and this one already decided not to.

**A shield does not vote**, and that falls out of `armed_weapons_in` rather than
from a rule about shields. Its own sub-type is Blunt, so counting it would take
the slashing bonus away from a sword-and-board character for holding something
they never swing.

### Piercing adds, it does not replace

A piercing weapon ignores 20% of a target's armour **on top of** whatever the
attacker's armour penetration stat holds, and `Resolve` owns the combination the
way `Attacker.total_armor_ignored` in `sim/cataclysm_sim/damage.py` does. A
caller adding the 20% itself would put the same rule in as many places as there
are callers.

This half needed issue #520 first: until armour penetration was a stat, there was
nothing for the 20% to be added to.

### Blunt is not built, and that is stated rather than hidden

Blunt's effect is a 10% chance to stun for 0.75 seconds. **Nothing in the project
rolls a chance to stun on an ordinary hit** — `UCataclysmSkillEffects::ApplyStun`
applies a stun that is already decided. The anti-stun-lock rules would all apply
and `ApplyStun` already enforces them, so what is missing is the roll and the
place to make it. Issue #639 records what building it takes.

---

## 2026-08-16 — Armor Penetration is a character sheet stat, and the forty-sixth

**Affects:** `Cataclysm_GDD_v2.md`, the Character Sheet subsection of section IV.
Applied: the stat count, the Offence row, and two new paragraphs. Closes issue
#520, which was split; the weapon sub-type half is issue #639.

### What was wrong

`FCataclysmIncomingHit::ArmorPenetration` was applied correctly by the damage
calculation and was never set, because **nothing in the project held an armour
penetration value**. `UCataclysmCombatAttributeSet` had a `Penetration`
attribute, and that is RESISTANCE penetration.

Three enchantments in `game/Data/EnchantmentsPositive.csv` grant it — skills
ignoring 10-25% of enemy armor, critical hits ignoring 20-40%, and a first hit
ignoring all of it — and none of them could do anything.

### The decision: it goes on the character sheet

This was not a wiring fix. **The design's own character sheet had no such stat.**
The stat table in section IV listed one Penetration, the model's
`sim/cataclysm_sim/character.py` had one, and the sheet was 45 stats. So the
question was whether armour penetration is a stat a character HAS, or a property
computed per hit from whatever is granting it at that moment.

**It is a stat, and two of its three sources decide that.** The three
enchantments are character-wide — "Your skills ignore 10%-25% of enemy armor" is
not about one blow — so there has to be somewhere on the character for them to
land. The parallel with resistance penetration is exact: that is also a character
stat that arrives at a hit as a parameter, and `damage.Attacker` in
`sim/cataclysm_sim/damage.py` has taken the two as separate parameters since it
was written.

**The piercing weapon sub-type's flat 20% is NOT a stat**, and that is the same
decision pointed the other way. It depends on what is in the character's hand at
the moment of the hit, so it belongs to the blow. It ADDS to whatever the sheet
holds; `Attacker.total_armor_ignored` combines them.

**It starts at zero, not 100.** An added percentage rather than a percentage OF
something, which is the rule
`tools/tests/test_stat_baselines_match_the_attribute_set.py` holds. Ignoring 100%
of a target's armour is the maximum this stat can mean, so 100 would not be
"unchanged".

### Why the two penetrations stay separate

They cut into different layers and are applied at different steps — armour at
step 3, resistance at step 4 — and the enchantment tables have always listed them
apart. Collapsing them into one stat would mean a point of penetration doing two
things at once, which no source in the design grants.

`Cataclysm.DamageType.TheTwoPenetrationStatsAreNotTheSameStat` is the check: it
gives a target armour and no resistance, sets resistance penetration to its
maximum, and asserts nothing changes; then the other way round.

### Why this matters more than it did

Enemy armour reached no arithmetic at all until issue #481. It is now the largest
single mitigation layer on the most armoured creatures — the Abyssal Warden's
5,954 armour removes 48.19% of a hit at difficulty tier 8 — so an entire
enchantment family existed with nowhere to land.

---

## 2026-08-16 — An enemy's energy shield is a fraction of its health, and the route for it exists before the creatures do

**Affects:** no design document. This records how a designed number reaches the
engine, not what the number is. Applied. Closes issue #485.

### What was wrong

The designed energy shield fraction reached `game/Data/EnemyArchetypes.csv` and
reached the row struct that reads it, and then stopped.
`ACataclysmEnemyCharacter` had no property for it, and the function that writes an
enemy's designed figures onto its gameplay attributes never wrote
`MaxEnergyShield`. Searching that class for "EnergyShield" returned nothing at
all. **Every enemy in the editor had a shield of zero whatever the design said.**

The layer itself works. An energy shield is step 7 of the eight in
`UCataclysmDamageCalculation::Resolve` and absorbs before health. The number was
the only thing missing.

### It is stored as a fraction, not an absolute figure

`stats_for` in `sim/cataclysm_sim/enemy_stats.py` computes the shield as
`health x energy_shield_fraction`, so the engine now does the same arithmetic from
the same two inputs. Storing a second absolute number beside the health would be a
figure that could disagree with it, and the shield would then have two sources.

The consequence is that changing an enemy's health changes its shield, which is
what the model does too.

### This changes no creature in the game today, and that is stated rather than discovered

Only two of the seven designed slice enemies carry a shield: the Succubus at 0.50
and the Corrupted Sentinel at 0.35. **Neither has a C++ class.** Only the Brute
and the Abyssal Warden are built, and both are designed at 0.00.

So what landed is the route, not a balance change. The same situation as the
Hellhound's knockback under issue #625 two entries down: the design names three
creatures, two exist, and the third's number is recorded and waiting.

**The route is made self-enforcing rather than left as a note.**
`tools/tests/test_enemy_energy_shield_reaches_the_engine.py` fails the moment
either creature's class file appears without setting the fraction, and its failure
message says which constant to add and where. That is the only moment the number
can be forgotten, so that is where the check sits.

**A third shielded creature appearing in the design also fails that file**, rather
than quietly having no route, because the list of shielded archetypes is compared
against the model rather than written out.

### The Abyssal Warden states its zero explicitly

It already does this for its designed evasion of 0.0, with the reason given
there: the zero is visibly designed rather than visibly forgotten. Its
survivability is armour and resistance, which are the highest in the slice.

---

## 2026-08-16 — The difficulty tier lives on the game mode until a dungeon exists

**Affects:** no design document. This records where a number lives, not what it
is. Applied. Closes issue #514.

### What was wrong

`UCataclysmVitalAttributeSet::PostGameplayEffectExecute` is the only place in the
running game that resolves an incoming hit, and it passed a hard-coded difficulty
tier of 1. The tier decides what armour is worth and nothing else:
`ArmorReduction` is `armor / (armor + 800 x tier)` capped at 75%.

Against the Abyssal Warden's designed armour of 5,954, that is the 75% cap at
tier 1 against 48.19% at its own tier 8, so **25% of a hit landed where the design
says 51.81% should**. Every armoured thing in the game was 2.07 times harder to
hurt than the simulation says, and the most armoured creatures were the worst
affected.

**The 1 was honest.** Nothing in the project held a tier at all, so there was
nothing to read. This is a value with no source rather than a wrong value read
from the wrong place.

### Where it goes, and why not where the design says

**The design puts the tier on the encounter.** A dungeon has a difficulty tier
and the enemies inside it stand at that tier. That is the right answer and it is
part of issue #41, the dungeon runtime, which does not exist.

**It goes on the game mode instead, world-wide, defaulting to 1.** That is the
smallest thing that lets the sandbox match a chosen tier while there is no
dungeon. It does not block the encounter-level answer and should be replaced by
it.

**One is the start of the game rather than a placeholder**, which is why the
default did not change. The sandbox is where a new character stands, and a new
character is at tier 1. What changed is that the figure can now be something else.

### A console variable, because that is how this project tunes

`Cataclysm.DifficultyTier` overrides the game mode while playing, with 0 meaning
"use the game mode's own". That is the same shape as the Brute's two cooldown
overrides, and it exists because the difference between the tiers is large and
the only way to judge it is to stand in the sandbox and switch.

Every answer is clamped to 1 to 8. A console variable is typed by hand, and a
tier of 40 would make armour worth almost nothing without saying so.

### One thing that could not be tested, and it is named rather than hidden

**Finding the game mode from a world is not covered by any automation test.** A
world built by a test has no authority game mode and cannot be given one:
`UWorld::AuthorityGameMode` is private, and `UWorld::SetGameMode` on a world with
no game instance crashes — measured, not assumed, on 2026-08-16, and it took the
whole test run down with it.

So the rule was split in two. `DifficultyTierFor` takes the game mode and decides;
`DifficultyTierIn` finds the game mode and calls it. The decision is tested in
full and exactly one line — the lookup — is not.

---

## 2026-08-16 — The damage type axis of the skill matrix gets no wildcard, and a row naming a type nobody has now fails generation

**Affects:** no design document. This records a decision **not** to build something,
and the check that makes the absence say so. Applied. Closes issue #579.

### What was asked for

The weapon skill matrix has two axes. The WEAPON axis accepts a wildcard:
`UCataclysmWeaponSkills::WeaponIndependent` is `All`, and the eight Aura rows use
it, because an aura is not a property of the weapon being held. The DAMAGE TYPE
axis does not — `UCataclysmWeaponSkills::SkillsFor` compares it exactly — so a row
whose damage type is `All` matches a character whose damage type is literally the
string "All", which no character has.

Issue #579 asked for the missing wildcard.

### Why it is not being built

**Its reason has gone.** The issue was filed because the basic attack was expected
to live in this matrix as fourteen damage-type-independent rows, one per weapon.
Issue #524 settled that the other way on 2026-08-15: the basic attack comes from
the weapon base, through `UCataclysmWeaponSkills::BasicAttackFor`, and the matrix
has no basic attack row at all. **No row in `game/Data/WeaponSkills.csv` uses a
damage type of `All` today and nothing needs one.**

**Building it would mean choosing a rule with nothing to check the choice
against.** The issue says so itself: with wildcards on both axes there are four
classes of row rather than two, and which beats which for a contested slot has to
be stated rather than fall out of loop order. Deciding that with no data that uses
it means the first row ever written in that shape would be the thing that tests
the rule, which is the wrong order.

**Symmetry between the two axes is not a reason on its own.** The axes answer
different questions. A weapon-independent skill is a real design idea — an aura
does not care what is in your hands. A damage-type-independent skill has no
equivalent argument today, because a character's damage type is what their skills
ARE rather than a piece of equipment they happen to hold.

### What was built instead, and it is wider than the issue asked

**The Damage Type column was checked by nothing at all**, which is a larger hole
than the wildcard. `validate_weapon_skill_damage_types` in
`tools/generate_datatables.py` now holds every value in that column to the eight
`Element.*` tags on the Tags sheet, in both directions.

That closes the trap the issue identified at the point a designer would hit it,
and a bigger one beside it: a MISSPELLED damage type was also silently dead data.
An exact comparison in the engine means such a row generates cleanly, imports
cleanly, fills no slot and reports nothing, so one typo costs one skill and says
so nowhere. It is the same failure `validate_weapon_skill_types` was written for
one column over, where a rename once left five of the fourteen weapons with no
skills at all.

`All` is refused by name rather than lumped in with the misspellings, and the
message says the weapon column has a wildcard and this one does not. Somebody
writing it has guessed at a symmetry that is not there, and the refusal should
answer the guess.

---

## 2026-08-16 — How far each enemy ability shoves, and that a shove is always straight away from what landed it

**Affects:** `Cataclysm_GDD_v2.md`, the Stun and Anti-Stun-Lock section of section
VI and the three enemy ability tables in section X. Applied. Issue #625, and it
closes two of the three abilities that issue names.

### The three distances

The design settled on issue #310 that enemies displace the player, named the three
abilities that do it, and left the distances open. It set the band: the player's
own two numeric knockbacks are Searing Hook's 4 metres and Molten Crush's 3, and
Path of Exile's default knockback distance is 4 units.

**One rule decides all three: an ability that also stuns takes the low end, and
one that does not takes the high end.**

| Enemy | Ability | Shove | Also stuns |
| :-- | :-- | :-: | :-- |
| The Brute | Stomp | 3 metres | Yes, 1.5 seconds |
| The Hellhound | Hellrush | 4 metres | No |
| The Abyssal Warden | Stampede | 4 metres | No |

Being moved and being unable to act at the same time is the harshest thing in the
vertical slice, so the one ability that does both does the smaller of the two.
Molten Crush is also the right comparison for the Stomp rather than Searing Hook:
it is the player's own slam that shoves outward from a point, where Searing Hook
pulls along a line.

### A shove is always straight away from whatever landed it

**There is one direction rule and no special case for a charge**, and the reason
is a measurement rather than a preference. The wording this started from was that
a charge knocks a crowd "aside", which reads as a push perpendicular to the lane.
Measuring the Abyssal Warden's charge in the engine showed away-from-the-creature
already does the job: its lane is 1.5 metres to either side, so it makes contact
about 1.3 metres short of somebody standing 0.75 metres off the centre line, and
the shove from there is diagonal — 3.34 metres along the lane against 2.19 across.
They still finish outside the lane, which is what clearing the ground means.

Being carried forward by something running at you is also the sensible outcome
rather than a defect to correct, so the second direction rule was dropped.

### A shove is not announced separately in the telegraph

All three abilities already draw a ground marker and already commit for a wind-up
the player can walk out of, so the marker already says to leave the area. A second
symbol for the displacement would add something to read without adding anything to
do: the answer to being shoved is the same as the answer to being hit.

### What is not settled, and it is the part that wants playing

**Whether the Brute can close the three metres again has not been shown by
anything.** On paper it can: it walks 2.8 metres per second and the stun it
applies lasts 1.5 seconds, so it arrives before the target recovers. Nothing
tests it, because the enemy behaviour fixture cannot move anybody —
`AdvanceWorldClock` in `game/Source/Cataclysm/Tests/CataclysmEnemyBehaviourTests.cpp`
adds to `World->TimeSeconds` and does not tick the world.

That limitation is not hypothetical. With the shove in place and the fixture
unchanged, the Brute went from 13 and 20 ordinary swings over thirty seconds to
**zero at both ability cooldowns**, because the first stomp pushed its target out
of reach and nothing could ever bring them back together. The tests now hold their
target at the distance they placed it, which is the arrangement they were written
to measure, and the real question is left for play.

---

## 2026-08-16 — Penetration stops at zero resistance, and does not deepen a negative one

**Affects:** `Cataclysm_GDD_v2.md`, section X, the enemy resistance subsection.
Applied: one new paragraph. Closes issue #482.

### What was wrong

The design document has always said that "penetration beyond an enemy's
resistance grants no bonus, so over-stacking it does not become a damage
multiplier against the enemies that need it least". **Both implementations did
the opposite.** `effective_resistance` in `sim/cataclysm_sim/damage.py` and
`UCataclysmDamageCalculation::EffectiveResistance` in the game each subtracted
penetration from resistance and let the result run negative, so against the
Abyssal Warden's 35% a player with 50 penetration landed 115% of a hit and one
with 80 landed 145%.

### The part the document did not settle

A negative resistance is legitimate on its own. Several enchantments push a
target below zero deliberately and that target should take extra damage, which
is why the floor in both files is -100 rather than 0. So "stop at zero" is not
enough on its own: the rule has to distinguish a negative the enchantments
inflicted from one penetration produced.

**The decision: penetration may remove resistance down to zero and no further,
and a resistance that is already negative is left where it is.** A target at
-25% takes 125% of a hit whether the attacker has no penetration or sixty.

### What the genre does, and it is split

- **Path of Exile 1 allows it.** Penetration reduces resistance below 0% and
  there is no floor on the result at all.
- **Path of Exile 2 does not.** Penetration reduces the value the hit is
  calculated against and cannot take it below 0%; negative resistance there comes
  from reduction effects instead. One unique item, Leopold's Applause, carries an
  explicit modifier letting hits penetrate down to -50%, which is the only way
  past the floor.

So this game's stated rule is Path of Exile 2's shape rather than an invented
one, and Path of Exile 1 is the counter-example that shows the other choice is
survivable. **The document's position was already the considered one** and
nothing argued against it, so this changed the code rather than the design.

Path of Exile 2's unique-item exception is also a design option this leaves open:
an affix or enchantment that explicitly says it penetrates past zero would be a
deliberate, named exception rather than the default behaviour every point of
penetration gets. Nothing proposes one today.

Sources: [Resistance, Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Resistance),
[Resistance penetration, Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Resistance_penetration),
[PoE 2 Resistance, Curses, Exposure and Penetration](https://poe2.stratlore.com/en/guides/resistance-curse-exposure-penetration/),
[PoE 2 Penetration Explained, Mobalytics](https://mobalytics.gg/poe-2/guides/penetration).

---

## 2026-08-16 — The radius cap applies to every attack, and a held-on aura is the one exemption

**Affects:** `Cataclysm_GDD_v2.md`, the Attack Telegraphs subsection of section X.
Applied: two new paragraphs. Closes issue #500.

### What was wrong

`fits_its_cycle` in `sim/cataclysm_sim/enemy_abilities.py` returned `True`
immediately for any ability that draws no marker, before the radius cap was
consulted. So an ability whose cycle is too short to telegraph, or whose shape
has no marker in the table, could state any radius at all.

**That exempted the dangerous case.** An ability escapes being telegraphed by
being FAST — the Imp's Rend and the Hellhound's Maul are both under the
threshold — so a fast, huge, unannounced area was legal. An unannounced nine
metre ring is strictly worse than an announced one: unavoidable and unannounced
rather than only unavoidable.

Nothing exploited it. The six untelegraphed designed abilities run 0.90 to 2.00
metres against a 6.50 metre cap, so this was a guard that did not guard rather
than a live balance defect.

### The two rules answer different questions

- **The half-cycle test** asks whether the player can clear the area during the
  wind-up. It means nothing for an ability with no wind-up, so it correctly
  applies to telegraphed abilities only.
- **The cap** asks whether the player can cross the area at all. Nothing about
  that depends on a marker being drawn.

### The exemption, which is the decision

**An ability held on for as long as the creature lives is exempt.** That is the
Aura slot, and the Succubus's Dominion is the only one designed: an 8.00 metre
field granting Commander to allies, over the 6.50 metre cap, which a cap applied
to everything would refuse outright.

**The cap is about a moment.** It asks whether the player can be clear by the
time an attack lands, and a field that is simply on has no moment it lands. The
player may walk out of it at any point, and the design document's own stated
counter to the Succubus is killing it, which ends the aura at once — that is what
the section calls the lesson in target priority. Dominion's radius is also
derived rather than free: the document sets it to the Succubus's own 8 metre
attack range, because a smaller field would buff nothing at the moment it
matters.

**An aura on a cooldown is not exempt.** It fires at a moment like anything else,
so it has a wind-up, draws a marker, and is capped. Nothing designed does this
today, and the distinction is written down so a later one is not exempted by
accident.

### What was considered and rejected

**Exempting by shape rather than by being held on.** `Aura` is one of the four
shapes the telegraph table draws a marker for, so exempting the shape would also
exempt the cooled-down case that genuinely does land at a moment. Being held on
is the property the argument actually rests on.

**Exempting anything that does not damage the player.** It is the more precise
rule and there is no field to express it: an ability carries a shape, a slot and
parameters, and nothing anywhere says whether it deals damage. Inventing that
field to answer one question would be a larger change than the one this issue
asks for, and the held-on test reaches the same answer for every ability that
exists.

---

## 2026-08-16 — The damage target counts the enemy's armour, and the flat damage affix rises from 18 to 22

**Affects:** `Cataclysm_GDD_v2.md`, the Damage Target subsection and the Dual
Wielding subsection of section VII, and the Affixes sheet of
`All_Things_Cataclysm.xlsx`. Applied. Closes issue #511.

### What was wrong

`damage_target` in `sim/cataclysm_sim/affixes.py` divided the average Common
enemy's effective health by how many hits it should take and applied no
mitigation. So it answered "how much health has to be removed" rather than "how
much damage has to be dealt to remove it". That creature carries 673 armour at
tier 8, which stops 9.52% of a hit, so the figure was **10.5% low**: 1,683 where
the answer is 1,860.

It is the same mistake issue #481 fixed one layer down in
`player_damage_to_kill_in`, and it was left behind deliberately, because
`damage_target` anchors every offensive number in the file.

### The decision: which number absorbs the 10.5%

Correcting the target broke the check that the reference build lands on it. The
project owner declared on 2026-08-15 that the target describes a dual wielder,
and `player_damage.py` asserts at import that the reference pair lands within
five per cent. At the corrected target the pair landed at 0.89 times it.

Four numbers could have absorbed the difference, and three of them are not free:

| Number | Why not |
| :-- | :-- |
| `HITS_TO_KILL_A_COMMON_ENEMY` = 2 | The project owner set the range 1 to 3 and this is the middle of it |
| `INCREASED_DAMAGE` = 125% | Set by the project owner |
| The weapon base damage table | Published design. An Axe's 46, a Sword's 40 and a Greatsword's 78 are quoted in the design document and encode the two-handed rule |
| `FLAT_DAMAGE` = 18 | **Derived, and the file says so.** This is the one that moved |

**22 is derived twice over rather than fitted once.**

- **The weapon term has to be a weapon a player can hold.**
  `reference_weapon_base` solves the pipeline backwards and says what the weapon
  and skill must supply. At a flat value of 18 and the corrected target it asks
  for 110.86, and no legal pair of one-handers reaches it — the strongest, an Axe
  with an Axe, supplies 92. At 22 it asks for 86.86, and the reference Axe and
  Sword supply 86. **That is a closer fit than the model had before this change**,
  where the pair's 86 sat 4.5% under a requirement of 90.03.
- **The choice between flat and increased damage has to stay real**, which is
  what the value was originally derived from. With eight increased affixes the
  two kinds are worth the same at 194 points of base, and a build starting from
  the pair's 86 crosses that after three flat affixes. Flat wins early, increased
  wins later, and a character takes some of each. Both neighbouring values still
  fail that test: at 60 flat still wins after six affixes, and at 12 increased
  wins from the first.

### What moved with it

- **The two-handed advantage falls from 1.33x to 1.29x per hit**, and from 1.26x
  to 1.22x per second. The multiplier itself is unchanged at 2.0. Bigger flat
  affixes mean the affixes supply more of the base bracket, so the two-hander's
  doubled implicit is a smaller share of it.
- **The hits-to-kill table in the Damage Target subsection now counts each
  creature's own mitigation** and it did not before, which is the same defect one
  table over. The heavily armoured creatures moved most: a Herald Abyssal Warden
  takes 122 basic attacks where the table said 45.
- The design document's stated affix-only two-handed multiplier was **2.75**,
  which `sim/cataclysm_sim/affixes.py` had already recorded as never computed
  from anything. It is now 3.40, the figure
  `sim/analyse_two_handed_multiplier.py` solves.

### What is still not settled

**Nothing here was played.** These are model figures. A Common enemy taking 2
basic attacks and a Herald taking 122 are both statements about a basic attack,
which is 100% of weapon damage; a Heavy slot skill deals 250% of it, and nobody
fights an Abyssal Warden with basic attacks. The constants are expected to be
tuned against real play.

---

## 2026-08-16 — Repeated displacement halves, and the count lives on the target

**Affects:** no design document. This records **implementation catching up with a
decision already made** on issue #302, plus one choice about where the count
lives. Applied. Closes issue #628.

### What was missing

`docs/Cataclysm_GDD_v2.md` has stated since issue #302 that each displacement
applied to a target already displaced within the last 5 seconds moves it half as
far as the one before, and that the count resets once 5 seconds pass with no
displacement at all. **Nothing implemented it, in either direction.** Every shove
moved the full stated distance however many had just landed, so repeated
displacement could hold a target at the far end of a room — which is the exact
failure the rule was written to prevent.

### Where the count lives, and why

**On `UCataclysmAbilitySystemComponent`, which everything hittable has.** Three
things pointed away from the skill:

- **The count belongs to the target, not the skill.** The second shove is usually
  a different skill and often a different actor, so anything held on the ability
  instance would count each source separately and never reach the second halving.
- **It has to cover both directions.** Issue #310 settled that enemies displace
  the player, so the player is a target too.
- **The existing per-target state does not fit.** The stun immunity window is a
  gameplay tag applied for a duration, which gives a flag. This rule needs a
  count and a timestamp, and a tag gives neither.

**The shared character base was the first choice and was wrong.** It looked right
— the player, every enemy and every minion derive from it — but a thing can be
hit without being one of them, which the automation tests demonstrated
immediately: their targets are bare actors carrying an ability system component
and nothing else. Being hit at all goes through
`UCataclysmSkillEffects::ApplyHit`, which requires that component, so the
component is the real common denominator and the character base was a narrower
guess that happened to cover the shipping cases.

### The 5 seconds is read from the stun rule rather than written again

`ACataclysmCharacterBase::TakeNextDisplacementShare` reads
`UCataclysmSkillEffects::StunImmunityWindowSeconds`. The design says so in as many
words — "It is the stun immunity window, reused rather than a second number to
remember" — and two copies of a number that measure different things which happen
to be equal are exactly the kind that drift without anything noticing.

### What is deliberately not there

**No damage threshold, no immunity flag and no boss exemption**, unlike the stun
rule beside it. A boss is pushed under the same halving as anything else, because
a boss pushed four metres is still fighting while a boss held still is not a
fight. That is the whole difference between the two rules and it is why they are
not one rule with a flag.

**The share never reaches zero.** It halves indefinitely, capped only so the
exponent stays inside what a float represents. A shove that moved nothing would
look like a shove that failed, and the design's own reason for choosing halving
over an immunity flag was that "a halved shove still looks like a shove".

---

## 2026-08-15 — Knockback is a rider, not a property of one shape

**Affects:** the shapes section of `Cataclysm_GDD_v2.md`; the Weapon Skills sheet
of `All_Things_Cataclysm.xlsx`. Applied. Closes issue #626.

### The decision

**`Knockback` moved from the `Strike` shape's parameter list to the riders any
shape may carry.** The project owner chose this over adding it to the `Movement`
shape as well, which was the narrower alternative.

**The reason is that displacement is not specific to one kind of skill.** A
strike, a leap, a charge and an enemy slam can all shove. That is the same
argument that made a burning patch of ground a rider rather than a shape of its
own, and adding the parameter shape by shape would have meant answering the same
question again for every shape that ever needs it.

### What was actually broken

**Shockwave Leap knocked back in its description and could not say so in its
data.** It is a Movement skill, `Knockback` belonged to `Strike`, and the
generator refuses a parameter its shape does not list. It now states
`Knockback=3`, which is the Warhammer's own designed knockback from Molten Crush,
its Heavy. Path of Exile's default knockback distance is 4 units, so 3 metres on a
movement skill sits just inside that band.

**Two of the three enemy abilities that will displace the player were blocked by
the same thing.** The Hellhound's Hellrush and the Abyssal Warden's Stampede are
both charges, which are Movement.

### The code had the same shape as the data, and had to move too

**Applying the shove lived inside `UCataclysmStrikeSkill::SwingOnce`**, so making
the parameter legal everywhere would have produced a number a designer could write
on any shape and only a Strike would honour — which is the exact class of failure
four issues this month were about. It moved to
`UCataclysmSkillTemplate::ApplyKnockbackTo`, called from `HitTargets`, which every
template that hits anything already goes through.

**Displacement is not scaled by the damage dealt**, deliberately, which is the
difference between it and the burn rider beside it. A Support skill deals no
damage by design and can still push.

### One consequence worth stating rather than discovering

**A repeating shape now shoves on every tick.** An Aura pulses through
`HitTargets` once per `Interval`, so an Aura stating a `Knockback` would push on
each pulse. No designed skill states one. What would bound it is the design's own
limit on repeated displacement — each one inside 5 seconds moves half as far as
the one before, decided on issue #302 — and that rule is implemented nowhere, for
either direction. Issue #628 carries it.

---

## 2026-08-15 — Enemies displace the player, and four stunning skills get a shape

**Affects:** the Stun and Anti-Stun-Lock section of `Cataclysm_GDD_v2.md`; the
Weapon Skills sheet of `All_Things_Cataclysm.xlsx`. Applied. Closes issues #310
and #588.

### Enemy displacement is intended, and the immunity clauses stay

Five player skills spend part of their effect on immunity to being knocked back —
Living Pyre, Unstoppable Force, Forge Stance, Bull Rush and Cinder Rush — and
**nothing in the game can knock the player back.** No enemy modifier, archetype
or status effect displaces. Verified: `game/Data/EnemyModifiers.csv` and
`game/Data/EnemyArchetypes.csv` contain no match for "knock" at all.

**The project owner decided that enemy displacement is intended and simply not
built yet.** The five clauses stay exactly as written, because they are already
balanced against a threat that is coming, and adding it later will not need them
re-tuned. The alternative considered was removing the clauses and giving the
freed budget to something that happens; it was rejected because the genre
evidence says the threat is standard.

**Genre research.** Diablo IV's Unstoppable buff exists precisely because monsters
apply control effects to the player: it "grants immunity to all control impairing
effects", which is the same shape these five clauses take. It also ships Knockback
and Knock Down as separate effects, the first pure movement and the second a hard
stop, which is the split this document already draws.
https://www.icy-veins.com/d4/guides/unstoppable-status-effect/ and
https://www.icy-veins.com/d4/guides/knock-down-status-effect/

Path of Exile treats knockback as pure movement with a default distance of 4
units, which is the band the player's own two numeric knockbacks sit in — 4 metres
on Searing Hook and 3 on Molten Crush.
https://pathofexile.fandom.com/wiki/Knockback

**Three enemies do it**, each chosen because a player skill already does the same
thing to enemies: the Brute's Stomp, which is a 360 degree slam that already
stuns, and the two charges — the Hellhound's Hellrush and the Abyssal Warden's
Stampede — because Bull Rush and Cinder Rush charge through a crowd "knocking them
aside" and a charge through the player does the same to them.

**Per-enemy distances were not set**, and neither was whether a shove is
telegraphed. That is implementation and is issue #625.

### The four stunning War skills, and where every number came from

Four War skills stun and had no Shape at all, so their durations lived in prose
that nothing but a person could read. A row with no shape is granted the
placeholder ability, so giving them one makes them playable skills.

**Nothing below was invented.** Each number is either stated in that skill's own
prose or carried over from the SAME WEAPON's designed Demonic skill in the same
slot.

| Skill | Shape and parameters | Where each number came from |
| :-- | :-- | :-- |
| Shield Bash | `Strike`, `Radius=2.5; Angle=60; MaxTargets=1; Effect=Stun; StunSeconds=1.5` | The Shield has no designed Heavy anywhere and no basic attack, so there is nothing of its own to copy. 2.5 and 60 are the shortest designed Heavy Strike in the game, shared by the Dagger and the Fist. A shield is held at the body, so it cannot reach further than a fist. Single target and 1.5 seconds are both stated |
| Shockwave Leap | `Movement`, `Mode=Leap; Range=9; Radius=5; Effect=Stun; StunSeconds=1` | Mode and Range are the Warhammer's own designed Movement, Meteor Drop. Radius 5 and 1 second are stated |
| Lunge | `Movement`, `Mode=Charge; Range=8; Radius=1.5; Effect=Stun; StunSeconds=0.75` | Mode, Range and Radius are the Sword's own designed Movement, Flamedart, carried over unchanged. 0.75 seconds is stated |
| Whip Swing | `Movement`, `Mode=Blink; Range=10; Radius=1.5; Effect=Stun; StunSeconds=0.75` | Range 10 is stated in the prose AND is the Whip's own designed Movement range. Radius is that same skill's. Blink rather than Charge because the prose says it covers the distance instantly |

**All four state `Effect=Stun` as well as `StunSeconds`.** That is not
duplication. The anti-stun-lock rule says a hit must take at least 10% of the
target's maximum health to stun **unless the skill states stunning as its
effect**, so naming the effect is what exempts a designed stun from the damage
threshold. Issue #363 made `Effect=Stun` writable by adding the row to the status
effect table.

### One thing the shape vocabulary could not express

**Shockwave Leap knocks back and the data cannot say so.** Its prose reads "knocks
back all enemies within 5 meters", and `Knockback` is a parameter of the `Strike`
shape only — a `Movement` shape reads `Mode`, `Range` and `Radius`. So that
knockback stays in prose, which is the exact class of problem this work exists to
fix, for one skill. It has its own issue rather than being fixed by widening a
shape's vocabulary in passing, and that issue is #626. It also blocks two of the
three enemy abilities named above, because both are charges.

---

## 2026-08-15 — Three rules the data already stated, now read by the game

**Affects:** no design document. This entry records **implementation catching up
with decisions already made**, plus two small rules that had to be settled to do
it. Closes issues #590, #621 and #622.

### What was wrong, and it was invisible

Three numbers the design documents state and the generated data carries were not
read by the game at all. Each failed silently, and one automation test —
`Cataclysm.SkillShape.EveryShapeInTheDataHasATemplate` — was the only thing that
ever said so. It had been failing for some time, because no pull request in this
project compiles any C++ (issue #20) and recent sessions ran only a small subset
of the tests.

| What the data said | What the game did | Issue |
| :-- | :-- | :-- |
| Three War skills have the shape `Deployable` | The C++ shape list had seven entries and not that one, so all three were granted a placeholder ability that fills a slot and does nothing | #621 |
| Six skills name what they produce, as `Minions=Ballista:2, SpikeTrap:3` | The parser treated every parameter except two as a number, so the whole cell was rejected and every summon spawned the same generic creature | #622 |
| All 22 skills that leave burning ground state what it deals per second | The game derived that figure from the Burn status effect instead, so a longer patch was automatically a bigger one | #590 |

### Two rules that had to be settled to finish it

**`Count` is not multiplied by the count in `Minions`.** A row may state both.
Bolt Turret writes `Count=1` and `Minions=BoltTurret:1`, which is the same number
said twice; multiplying them would deploy one turret per turret. So `Minions` is
the authority on how many of each kind, and `Count` is what a skill with no named
minion uses. All three deployable skills give the same answer either way, so this
decides a case that does not exist yet rather than changing one that does.

**Whether a minion walks is a property of the minion, not of the shape.** A bolt
turret, a ballista and a spike trap all state a move speed of zero in
`game/Data/MinionTypes.csv`; an imp states 4.4 and a mote 5.5. So the difference
between the `Summon` shape and the `Deployable` shape is **where the thing is
placed** — a summon at the caster, a deployable at the point the player aimed at
— and "stays where it is put" falls out of the data. A deployable handed a
walking creature would walk, and that is correct rather than a hole.

### What was deliberately left, and why

**A minion's damage and health still do not come from its type.** Issue #209
reversed the rule that a minion deals 30% of its summoner's weapon damage, and
`game/Data/MinionTypes.csv` states a base and a per-level amount for each type
instead. That model is still not implemented, and two things it needs do not
exist: any way to read the summoner's level, and any code at all that applies
`game/Data/MinionScaling.csv`. **Issue #340 holds that half.**

What a minion now does take from its type is everything that is pure behaviour:
its reach, its notice radius, its attack interval and its move speed. So a
ballista now reaches 15 metres and fires every 2 seconds where an imp reaches 2
metres and hits every second, which is what made "no summon skill knows what to
summon" true rather than merely untidy.

**`ACataclysmGroundZone::Spawn` was kept.** Issue #590 asked whether a spawner
nothing calls should go. It is called — by three automation tests — so removing
it would delete coverage rather than dead code.

---

## 2026-08-15 — The basic attack lives on the weapon, not in the skill matrix

**Affects:** the Combat System damage table and the Skill Acquisition section of
`Cataclysm_GDD_v2.md`; the Item Bases sheet of `All_Things_Cataclysm.xlsx`.
Applied. Closes issue #524.

### What was wrong

`game/Data/WeaponSkills.csv` held 398 rows across six slots and **not one row for
the Basic slot**, so no character had an ordinary attack. The project owner found
it by playing:

> "As far as I can tell, we have two player abilities. One is a teleport, and the
> other is a giant ball that shoots out and comes back. There's no basic attack
> or anything else happening"

### The decision, and why the issue's own plan was rejected

Issue #524 assumed 75 new rows in the weapon-and-damage-type matrix. **The design
document already said that matrix deliberately has none.** It states the matrix
produces "one per non-basic slot" and that it is 398 rows. Adding 75 rows would
make both sentences false.

The project owner chose, from four options, to **key the basic attack on the
weapon type alone**, as two new columns on the Item Bases sheet beside that
weapon's attack speed. Thirteen entries rather than 75 rows, and both sentences
stay true.

The three rejected options were: 75 rows in the matrix; teaching the lookup a
damage-type wildcard so 13 matrix rows would serve; and postponing the work.

**The reason it is not a matrix row is that it does not vary by damage type.**
The basic attack *is* weapon damage, so a Dagger swings the same distance whatever
the blade is made of. Every other slot names a designed skill per weapon **and**
damage type; this one does not.

### Genre research

**Path of Exile stores weapon range as a property of the weapon base type**, and
melee reach is that range plus the character's own hitbox radius. That is the same
shape as the rule below, which is contact distance plus the weapon's length. It is
evidence the structure is right rather than merely convenient.
https://pathofexile.fandom.com/wiki/Range

**Last Epoch's basic attack is one generic skill** unlocked at level 1, costing no
mana, usable with no weapon equipped — one attack rather than one per weapon type.
That supports the premise that a basic attack is not a per-combination designed
skill. https://lastepoch.fandom.com/wiki/Attack

### The numbers, and how they were derived

**Melee reach is `0.9 + the weapon's length past the fist`, on a 0.3 m grid.** The
0.9 is not chosen: it is this project's contact distance, the 0.42 m player capsule
from `CataclysmPlayerCharacter.cpp` plus a 0.48 m baseline enemy body, and it is
already the radius of Maul, Slam and Sunder. **The arc is that weapon's designed
Heavy arc carried over unchanged**, because the arc is the animation and the reach
is the power.

| Weapon | Shape | Parameters |
| :-- | :-- | :-- |
| Dagger | Strike | `Radius=1.5; Angle=60; MaxTargets=1` |
| Fist | Strike | `Radius=1.5; Angle=60; MaxTargets=1` |
| Sword | Strike | `Radius=1.8; Angle=90; MaxTargets=1` |
| Axe | Strike | `Radius=1.8; Angle=100; MaxTargets=1` |
| Warhammer | Strike | `Radius=2.1; Angle=80; MaxTargets=1` |
| Greataxe | Strike | `Radius=2.4; Angle=120; MaxTargets=1` |
| Greatsword | Strike | `Radius=2.7; Angle=140; MaxTargets=1` |
| Whip | Strike | `Radius=3; Angle=45; MaxTargets=1` |
| Spear | Strike | `Radius=3.3; Angle=40; MaxTargets=1` |
| Staff | Projectile | `Range=7.2; Radius=0.9; Pierce=0; Speed=2000` |
| Wand | Projectile | `Range=8.4; Radius=0.6; Pierce=0; Speed=2600` |
| Crossbow | Projectile | `Range=10; Radius=0.4; Pierce=0; Speed=2400` |
| 2H Crossbow | Projectile | `Range=12; Radius=0.9; Pierce=0; Speed=1800` |

**Every one is exactly 0.6 times that weapon's designed Heavy reach**, verified to
three decimals for the ten weapons that have a designed Heavy: eight melee Strikes
plus the Staff at 12 m and the Wand at 14 m. That was not fitted — it fell out of
the contact-distance rule and was noticed afterwards, so it is two independent
derivations agreeing. Since the arc is unchanged, each melee basic attack covers
36% of its Heavy's area.

**Three are judgements with nothing to check against.** The Spear, the Crossbow and
the 2H Crossbow have no designed Heavy of any shape anywhere in the matrix.

**The Shield gets none.** It is a one-handed weapon that grants no attack damage,
decided on issue #619, so there is no hit to compose from it.

### What "no riders" means and why it is stricter than the enemies

A basic attack carries no burn, no patch of ground, no stun and no knockback, and
hits one target. The generator refuses any of them.

**The enemy basic attacks are the nearest precedent and they are not unanimous.**
Of the seven, four state a one-target cap — Rend, Maul, Slam and Sunder — while
Soulfire, Siege Bolt and Dread Cleave state none, and the Hellhound's Maul does set
what it hits alight. The stricter reading was taken because the player's basic
attack has a job no enemy's has: it is the 100% figure every other slot is a
percentage of, so a rider on it would move all six of the others.

### Three consequences in play, all intended

- **A weapon whose damage type covers none of its skills still has a basic
  attack.** A War Wand has no matrix rows and fills one slot instead of none.
- **The Shield fills six slots and no basic attack.**
- **A fourteenth weapon type costs one entry**, not seven rows.

### Two things this surfaced that are not this decision

**The basic attack needed tags of its own.** Every matrix row gets a slot tag and
an element tag written into it by the generator, which is issue #156, and those
tags decide which gear modifiers reach a skill. Having no matrix row, the basic
attack had none, so no scoped modifier reached it — Burning Wrath's increase moved
six of the seven granted skills and not the anchor they are measured against. The
slot tag is now added where the skill is read and the element tag where the weapon
is equipped, because only the equipped weapon knows its rolled damage type.

**Three statements in `Cataclysm_GDD_v2.md` about longest range are in conflict
with the data.** Line 4503 says 14 metres "is the longest range any player attack
reaches ... and no attack states more", while two Demonic Staff rows state
`Range=15` and this log already calls 15 "the joint longest" in the 2026-08-04
entry. Nothing in this decision exceeds 12 metres, so nothing here makes it worse.
It is left as it stands because the Corrupted Sentinel's reach is derived from the
14 figure and changing it is a separate decision.

---

## 2026-08-15 — A Shield is a one-handed weapon that grants no attack damage

**Affects:** the Weapon Types, Power Score and Dual Wielding sections of
`Cataclysm_GDD_v2.md`. Applied. **Supersedes the entry below dated the same day
that made a Shield an offhand.**

### The decision

The project owner, asked whether an offhand should grant skills:

> "shields will have skills so you know what, for our purposes we can probably
> just classify them as a weapon, I don't think there's much of a difference
> between calling them a weapon or an offhand. Except they wont have base damage
> on them, they're a defensive item."

So there is **no offhand category**. A Shield is one of the one-handed weapons.
It grants block chance and armor and no attack damage, and it has its own skills
— Shield Bash, Shield Charge, Shield Wall, all already in the skill matrix.

### Why the simpler classification is better, not just shorter

Every consequence the offhand category needed a rule for now follows from being a
one-handed weapon, with no clause of its own:

| Question | Under the offhand category | As a one-handed weapon |
| :-- | :-- | :-- |
| Gem sockets | A stated rule: three, like a second weapon | Three, because one-handers carry three |
| Power Score pieces | A stated rule: counts like a second weapon | Counts, because it is a weapon |
| Affix slots | A stated rule: four, like a second weapon | Four, because it is a piece |
| Does it grant skills? | Open question, nothing tracked it | Yes, because weapons do |

Three rules and an open question collapse into nothing.

### The one rule that replaces them

**A held weapon that grants no attack damage contributes nothing to the basic
attack** — neither damage nor swing rate. `sim/cataclysm_sim/player_damage.py`
decides this by reading the base's own implicits rather than by checking for the
name "Shield", so a future weapon of the same kind needs no change.

A character holding a weapon and a Shield therefore hits exactly as hard as that
weapon alone, at exactly the same rate, and keeps a dual wielder's nineteenth
equipped piece, four extra affix slots and three extra sockets. That is the whole
trade: slots and defence for a second weapon's damage.

### Three legal loadouts, not four

One two-handed weapon; two one-handed weapons; a single one-handed weapon. A
weapon with a Shield is not a fourth shape — it is the second one.

### What this cost

Half a day modelling an offhand category that was then removed. The decision it
recorded — that a Shield counts for Power Score and sockets just like a second
one-handed weapon, which closed issue #612 — is unchanged and correct. Only the
classification moved.

---

## 2026-08-15 — Deployable is the eighth shape, and a skill names what it produces

**Affects:** the shapes section and the Succubus telegraph note of
`Cataclysm_GDD_v2.md`; the Weapon Skills and Minion Types sheets of
`All_Things_Cataclysm.xlsx`. Applied. Closes issue #338.

### The decision

**An eighth shape, `Deployable`.** A summon spawns things that walk to the enemy;
a deployable places machines that stay where they are put. The name matches the
`Type.Deployable` tag the three skills already carried, rather than inventing a
word — the split was already in the data, it just had no Shape.

**A `Minions` parameter naming what a skill produces**, as `Type:Count` pairs. It
is a list rather than one name because Iron Fortress deploys **two ballistae and
three spike traps**, which no single key and value can say. All six minion skills
now carry it, so a skill can be traced to its stat block and back.

### Three stat blocks that had to exist first

`game/Data/MinionTypes.csv` went from two rows to five: the bolt turret, the
ballista and the spike trap joined the imp and the mote. Without them Iron
Fortress had nothing to name.

| | Bolt Turret | Ballista | Spike Trap |
| :-- | --: | --: | --: |
| Base health, per level | 80, 36 | 200, 90 | 40, 18 |
| Base damage, per level | 45, 45 | 60, 60 | 10, 10 |
| Attack interval | 1.5s | 2.0s | 3.0s |
| Move speed | 0 | 0 | 0 |
| Target mode | Nearest | **Furthest** | Nearest |

**Health keeps the ratio the prose states.** The descriptions say 200 and 500
health, which is 1 to 2.5, and the turret and ballista are 3,680 and 9,200 at
level 100. The flat figures themselves are superseded: the design document cites
them as evidence that minions needed their own stats, not as final values.

**Damage comes from one stated rule** — a deployable squad at full count matches
the Demonic summon squad, which is three imps at 1,010 each per second at level
100. A turret firing every 1.5 seconds hits for 4,545, a ballista every 2 seconds
for 6,060, and a spike trap hits like an imp on a 3 second cycle.

**These three are provisional.** War is not in the vertical slice, so nothing has
played them. They exist because issue #338 could not be finished without them.

### Two latent problems this surfaced

**A test counted every designed skill and called them Demonic.** It was
accidentally correct while all 51 designed skills were Demonic. Giving three War
skills a Shape took the unfiltered count to 54 while the sentence in the design
document — which is about what a Demonic player carries — was still right at 51.
The test was wrong and is now filtered; the document was not touched.

**A fourth shape now has no telegraph marker.** The Succubus note said three
—SelfBuff, Summon and Debuff — and Deployable makes four. Both the note and the
test that checks it were updated, and the test now derives the total from the
shape list rather than hard-coding seven.

### Two things stated in prose that still have no home

**The ballista's one-second pin.** `game/Data/StatusEffects.csv` has Stun and
Bleed and no Pin, so there is no effect for the skill to name. This is the same
gap issue #363 filled for Stun and Knockdown, and it is not filled here.

**How far a deployable can be placed.** The descriptions give the turret's and
ballista's reach — 10 and 15 metres, now in the minion table — but never say how
far from the caster they can be put down. No number was invented for it.

---

## 2026-08-15 — The four minion affixes, and accepting the pool dilution

**Affects:** `All_Things_Cataclysm.xlsx`, the Affixes sheet, which gains four
rows; and the Prefixes and Suffixes section of `Cataclysm_GDD_v2.md`. Applied.
Closes issue #337.

### The decision

Four affixes, each mirroring an existing one exactly so no new power enters the
game. A minion build gives up the weapon damage affix, which is worth nothing to
it, and takes the minion one at the same value on the same slots.

| Affix | Position | Value | Mirrors |
| :-- | :-- | --: | :-- |
| Increased minion damage | prefix | 125.0 | Increased damage |
| Increased minion health | prefix | 12.0 | Increased maximum health |
| Increased minion attack speed | suffix | 15.0 | Increased attack speed |
| Minion damage and minion health | prefix, hybrid | — | The existing hybrid pattern |

**125 and 12 are not an inconsistency.** The increased affixes have never shared
one standard value: damage and spell damage are 125, defensive and attribute
affixes are 12, attack speed is 15. A minion damage affix at 12 would be a tenth
of its weapon counterpart and the archetype would not function.

**Minion health sits on defensive slots deliberately**, so a summoner spends the
slots that would have kept them alive on keeping the army alive. That is the real
cost of the archetype and it should be visible.

**The hybrid's slot list is derived, not chosen.** A hybrid can only appear where
both halves roll, and Ring is the only slot shared by the offensive slots minion
damage uses and the defensive ones minion health uses.

### The three minion stats are off the character sheet

`attack_damage` is already off it, because it belongs to the equipped weapon
rather than to the character. `minion_damage`, `minion_health` and
`minion_attack_speed` join it for the same reason: they belong to the minion. The
character sheet sums every source that names a stat, so a summoner holding no
minions would otherwise carry a visible number meaning nothing.

An affix needs no tag, unlike the attribute scaling in
`game/Data/MinionScaling.csv`. Increased minion damage reaches every minion by
definition; a narrower affix would name a narrower stat rather than carry a tag,
which is how Last Epoch layers them.

### The dilution is accepted, and measured

Issue #337 raised it as the thing most likely to be discovered late: the affix
table has **no weight column**, so every affix is equally likely on any slot it is
allowed on, and four more entries make every other entry on those slots rarer.

Measured rather than estimated:

| Pool | Before | After | Change |
| :-- | --: | --: | --: |
| Weapon prefixes | 3 | 4 | +33% |
| Ring prefixes | 14 | 17 | +21% |
| Gloves prefixes | 5 | 6 | +20% |
| Every other pool | | | +17% or less |

**Accepted rather than fixed, because the absence of weights is not new.** With
no weight column at all, the pool has never expressed rarity for any affix, so
these four are a slightly larger instance of an existing gap. Adding a weight
column is the real fix and is worth doing before the pool grows much further. It
is recorded in the design document rather than filed as an issue, on the
principle that constants and pool shapes get tuned against real play.

### Two things issue #337 proposed and this does not add

**Flat minion damage.** A flat addition is multiplied by the number of active
minions, which is already the most dangerous quantity in the system. It is the
first thing to add if minion builds feel numerically flat after a tuning pass,
and it should be split by attack type when it lands.

**Minion count as an affix. Never.** Every affix has seven tiers and can appear on
several pieces, and count has no meaningful seven-step curve. There are eight ring
slots, so a "+1 maximum minions" suffix would be +8 from rings alone. Count lives
in the enchantment table, where the unique-per-character rule bounds it.

### An asymmetry noted and not acted on

`game/Data/EnchantmentsNegative.csv` carries six minion penalties — less minion
health, minus maximum count, minions costing health to summon, minions reserving
health — against a positive count pool that maxes at +2 to +4. If negatives roll
independently of whether a character summons anything, a minion build is
disproportionately exposed and a weapon build treats most of them as dead text.
Whether that is deliberate is still unknown, and nothing here changes it.

---

## 2026-08-15 — A Shield counts just like a second one-handed weapon

**Affects:** the Weapon Types, Power Score and Dual Wielding sections of
`Cataclysm_GDD_v2.md`. Applied. Closes issue #612.

### The decision

The project owner's words: a Shield "counts towards power score, 3 gem slots,
just like a second 1h weapon. Basically 2 1h weapons or a 1h weapon + shield give
the same amount of power score as a 2h weapon."

|  | Two-handed | Dual wielding | One-handed and Shield |
| :-- | :-: | :-: | :-: |
| Equipped pieces | 18 | 19 | 19 |
| Affix slots | 72 | 76 | 76 |
| Gem sockets in both hands | 6 | 6 | 6 |
| Damage types | 3 | 4 | 2 |
| Weapon affix slots, in one-handed terms | 8 | 8 | 4 |

**What the hands hold is one equipped piece for Power Score, whatever it is.**
All four legal loadouts count once, which is the existing rule extended rather
than a new one: no loadout may be worth free Power Score.

**A one-handed item carries three sockets**, so two of them make six and a
two-handed weapon carries six alone.

### What the Shield trades

It adds **no attack damage at all** — it is the only thing a hand can hold with
no attack damage implicit — and it is left out of the attack rate average,
because a shield is not swung. So a one-handed weapon with a Shield hits exactly
as hard as that weapon on its own, and strictly less hard than a pair. What it
buys instead is block chance and armor, which no weapon base grants.

It still gets the nineteenth piece and its four affix slots, so it is not paying
twice.

### Two constants were renamed

`DUAL_WIELD_GEAR_PIECES` and `DUAL_WIELD_TOTAL_AFFIX_SLOTS` in
`sim/cataclysm_sim/affixes.py` became `GEAR_PIECES_WITH_AN_OFFHAND` and
`TOTAL_AFFIX_SLOTS_WITH_AN_OFFHAND`. A one-handed weapon with a Shield now gets
the same nineteenth piece that two one-handed weapons do, so a name mentioning
only dual wielding described half of what the constant covered.

### What is still not done

`sim/cataclysm_sim/affixes.py` continues to model the Shield as a `WeaponBase`
with one hand. That is a category error now rather than an exception, and moving
it touches `ITEM_BASES`, `WEAPON_BASES`, `attack_speed_of`, `BASES_BY_SLOT` and
the slot table. It changes no number any of these decisions produce, so it was
left alone. Nothing tracks it, deliberately: it is a tidying job, not a defect.

Which affixes a Shield may roll is also untouched. It currently draws from the
weapon pool, and whether an offhand should draw from a defensive pool instead is
a question nobody has asked yet.

---

## 2026-08-15 — Minion scaling reaches a minion through a tag, not a column

**Affects:** the minion section of `Cataclysm_GDD_v2.md`, and
`All_Things_Cataclysm.xlsx` — the "Minion Types" sheet gains a `Tags` column in
place of `Scaling Attribute`, and a new "Minion Scaling" sheet. Applied.
Closes issue #336.

### The decision

A minion carries **tags**. A separate table says what one point of an attribute
grants a minion carrying a tag.

| Attribute | Requires tag | Stat | Percent per point |
| :-- | :-- | :-- | --: |
| spirit | `Minion.Creature` | damage | 1.0 |
| agility | `Minion.Machine` | damage | 1.0 |

Every minion type carries `Type.Minion`, exactly one of `Minion.Creature` or
`Minion.Machine`, and narrower tags for how it fights. The Imp is
`Minion.Creature, Minion.Melee`; the Mote is `Minion.Creature, Minion.Spell`.

### What it replaces, and why

The first version of the minion table, merged earlier the same day, wrote the
attribute onto the minion as a `Scaling Attribute` column. The project owner
asked for something more general: a scaling tag, with values per stat.

**A column can only ever say one thing.** It expressed "Spirit raises this
creature's damage" and nothing else — not two attributes, not a second stat, not
a group narrower than the family. A tag lets a future affix granting increased
minion melee damage reach an imp and not a mote, with no new machinery.

**It also removes a double count that a shared stat would have caused.**
Everything reading `game/Data/Attributes.csv` sums every attribute that names a
stat, so a single shared "increased minion damage" entry would have let a
summoner's Agility raise a summoned creature — which #335 settled it must not.
An earlier proposal to solve this with two separately-named character stats was
rejected in favour of the tag, which is the same fix without inventing names.

### The genre research behind it

Last Epoch: minions scale **only** from stats explicitly tagged for minions, and
the tags are **layered** rather than one flag — a general minion damage reaches
everything, while minion melee damage and minion spell damage reach only minions
doing that. A skeleton mage's spells scale from minion spell damage; an archer's
shots do not. The attribute link comes from the skill's own tag.

- https://maxroll.gg/last-epoch/resources/damage-explained
- https://maxroll.gg/last-epoch/getting-started/damage-for-beginners
- https://www.lastepochtools.com/minions/skeleton_mage
- https://lastepoch.fandom.com/wiki/Summon_Skeleton

**Not copied: Last Epoch's Minion Power**, a separate multiplicative bonus of
0.6% per character level from level 26, reaching 45% more damage and 45% less
damage taken at level 100. This game already raises minion base health and damage
by summoner level inside the minion table, so a second level term would count
level twice.

**Worth revisiting, and deliberately not done here.** Last Epoch's level term is
multiplicative and this game's is a flat per-level addition, which is why three
imps are worth about 2.0x the summoner's basic attack in the middle difficulty
tiers and 1.15x at the level cap: a straight line cannot track a curve that also
rises with gear. Changing the level term to a multiplier would flatten that, and
belongs in its own change.

### This is the tag-scoped increase rule, pointed at minions

The project owner stated on 2026-08-02: "The player holds all of its own
increases, and those increases apply to things with matching tags."
`sim/cataclysm_sim/character.py` already models that as a stat, an amount and a
set of required tags. Minion scaling is the same shape, which is why the four
minion affixes in issue #337 need nothing new.

### What is filled in

**Only damage.** The design document states "Each grants 1.0% increased minion
damage per point", and that is the only figure decided. Health is expressible in
the same table and nobody has chosen a number for it, so no row claims one.

---

## 2026-08-15 — The Imp and Mote stat blocks, and three imps beat the basic attack

**Affects:** `All_Things_Cataclysm.xlsx`, new sheet "Minion Types". Applied.
Partly answers issue #336.

### The decisions

Three, all from the project owner:

1. **A mote is a weaker body in larger numbers** than an imp.
2. **The two should produce different total output.** Left to me; set so a full
   swarm of four motes deals about three quarters of what a full squad of three
   imps deals.
3. **Three permanent imps should out-damage the summoner's own basic attack.**

### What the third one reverses

Issue #336 proposed the opposite floor: "A player who has geared for nothing
minion-related should get less from three imps than from their basic attack."
The project owner decided against it. Three imps now beat the basic attack at
every difficulty tier, from 1.14x at the level cap to about 2.0x in the middle
tiers.

### The numbers, and how they were reached

Summon Imp and Open the Rift are both Staff skills, so the summoner holds a
Staff. Three imps are permanent in normal play: Summon Imp holds 3 for 20 seconds
against a 5 second cooldown.

| | Imp | Mote |
| :-- | --: | --: |
| Base health, per level | 200, 90 | 100, 45 |
| Base damage, per level | 10, 10.0 | 5, 4.5 |
| Attack interval | 1.0s | 0.8s |
| Most active | 3 | 4 |
| Threat | 100% | 40% |
| Reach | 200cm | 350cm |

Damage was set from the top down: at level 100 a Staff character's basic attack
deals 2,652 a second, so three imps were sized at 1.15x that.

### The mid-game bulge is stated rather than hidden

Three imps are worth 1.14x the basic attack at difficulty tier 8 and about 2.0x
at tiers 2 and 3. **This cannot be removed while the storage shape stays as it
is.** A minion's damage rises with the summoner's level alone, while the
summoner's own damage rises with level *and* affix tier *and* gear upgrade level,
so the player's curve climbs about 12 times from level 12 to level 100 where a
linear-in-level minion climbs at most 8. A straight line cannot track it.

That is not only an artefact. It is the shape the design asks for: a minion falls
behind a geared summoner, and closing that gap is what minion affixes (issue
#337) and points in Spirit are for. An uninvested summoner is therefore not the
balance case. The figures are expected to move once the game is playable.

### What is NOT in this change

`docs/Cataclysm_GDD_v2.md` says the rows granting increased minion damage per
attribute point land with issue #336. They are not here. Adding them means adding
`minion_damage` to the character sheet in `sim/cataclysm_sim/character.py`, which
touches the stat groups, the default stat line and the gameplay tags, and is
larger than a stat block table. Issue #336 stays open for it.

The three War deployables — bolt turret, ballista and spike trap — are on Crossbow
and Spear and are outside the vertical slice, which is Demonic only.

---

## 2026-08-15 — A Shield is an offhand, and a single one-handed weapon is a legal loadout

**Affects:** the Weapon Types section and the Dual Wielding section of
`Cataclysm_GDD_v2.md`. Applied. Consequences tracked in issue #612.

### The decision

There are now **four** legal loadouts, not two:

| Loadout | What it holds |
| :-- | :-- |
| Two-handed | One two-handed weapon |
| Dual wield | Two one-handed weapons |
| One-handed | A single one-handed weapon, other hand empty |
| Weapon and shield | A one-handed weapon with a Shield in the offhand |

A **Shield is an offhand**, not a one-handed weapon.

### What this reverses

The document previously said the opposite in three places, and all three were
wrong rather than merely incomplete:

- Weapon Types listed Shield among the one-handed weapons and stated "There are
  no offhand items."
- The Shield paragraph in Dual Wielding said Section V "states there are no
  offhand items, so it is a weapon with nowhere else to be" — the absence of an
  offhand slot was being used as the *justification* for the classification.
- Power Score counts two one-handed weapons as one equipped piece, a rule written
  when every held item was a weapon.

### Why it was quoted back before being applied

The project owner stated the new rule while the old one was being quoted to them
from the document. That is the recorded procedure for an owner answer that
conflicts with the documents, and it was followed: the conflicting sentence was
quoted and the answer confirmed. The document lost.

### What is deliberately still open

Whether a Shield is an equipped piece for Power Score, how many affix slots it
carries, and how many gem sockets the two new loadouts give. Issue #612 carries
all three, because they hang off the same question of what an offhand is in the
item model, and answering one alone risks contradicting the others.

`sim/cataclysm_sim/affixes.py` also still models the Shield as a `WeaponBase`,
with a test excepting it from the rule that every weapon supplies damage. That is
now a category error rather than an exception, and moving it is part of #612.
`sim/cataclysm_sim/player_damage.py` works around it in the meantime by naming the
Shield in a constant and leaving it out of the attack rate average.

---

## 2026-08-15 — The damage target describes a dual wielder

**Affects:** The Damage Target section of `Cataclysm_GDD_v2.md`. Applied.
Closes issue #610.

### The decision

The target of **1,681 damage per non-critical hit at difficulty tier 8**, which
every offensive value in `sim/cataclysm_sim/affixes.py` is fitted against,
describes a character holding **two one-handed weapons**.

### Why, and how it was nearly recorded as a defect

Issue #610 was filed claiming the target was unreachable. Solving the pipeline
backwards, `affixes.reference_weapon_base` reports the weapon term must supply
about 90, and no single weapon supplies 90: the strongest one-hander carries 46
and the smallest two-hander 128. That looked like a hole in the middle.

It was not. Two one-handed weapons **sum** their base damage, which the Dual
Wielding section already stated, so 90 is what a pair supplies. The two strongest
pairs bracket it almost exactly:

| Loadout | Weapon term | Damage per hit | Against the 1,681 target |
| :-- | --: | --: | --: |
| Axe + Sword | 86 | 1,649 | 0.98x |
| Axe + Axe | 92 | 1,700 | 1.01x |
| Greatsword | 156 | 2,244 | 1.33x |
| Axe alone | 46 | 1,309 | 0.78x |

The check that settles it is the two-hander. Reading the target as a dual wielder
puts a Greatsword at **1.33 times** it, and the Dual Wielding section already
states, on independent grounds, that "a two-handed weapon deals about **1.33
times** the damage per hit". Two figures derived separately landing on the same
number is the evidence.

### The residual is stated rather than absorbed

Pair sums are whole numbers and the requirement is 90.03, so nothing lands on it
exactly; the two candidates sit about 4% under and 2% over.
`sim/cataclysm_sim/player_damage.py` checks at import that the reference pair
stays within five per cent of the target, so if a weapon base or an affix value
moves, the declaration fails loudly instead of quietly becoming false.

### What this does not fix

`damage_target()` still applies no enemy mitigation, which is issue #511 and moves
the figure by about 10%. That is a separate error in the same number and is
untouched here.

---

## 2026-08-14 — A minion's scaling attribute is chosen per type: Spirit for creatures, Agility for machines

**Affects:** the minion section of `Cataclysm_GDD_v2.md`. Applied. Issue #335.

### The decision

Asked which primary attribute minions scale from, the project owner answered
**"that will depend on the minion"**. So it is a per-type choice, not one global
one:

| Minion family | Types | Attribute |
| :-- | :-- | :-- |
| Summoned creatures | Lesser imp, mote of living fire | Spirit |
| Deployed machines | Bolt turret, ballista, spike trap | Agility |

Each grants **1.0% increased minion damage per point**.

### The document already expected this answer

`Cataclysm_GDD_v2.md` states what a minion takes from its summoner as three
channels, and the third already read "Increased damage — one **primary
attribute**, declared by the minion type". The mechanism was in place; only the
attributes were missing.

### The 1.0% per point rate is derived, not chosen

The document establishes that 100 points of Efficacy multiply damage over time
output by 2.0, and 100 points of Ferocity multiply a direct hit by about 1.56, so
an attribute at 100 points belongs between those. 1.0% per point gives exactly
2.0, the top of the band, and the top is right because minion damage has no
critical strike layer to compound with: a minion takes neither the summoner's
critical strike chance nor its multiplier.

### Which attributes, and the test that picked them

**The test is whether the attribute's existing stats multiply the new one**, not
which one reads well.

- **Spirit** drives maximum energy shield and energy shield regeneration. Both
  defensive; neither multiplies minion damage.
- **Agility** drives movement speed and evasion. A deployable does not move, so
  movement speed cannot reach it, and evasion is defensive.

**Efficacy was rejected for failing that test**, and it was the obvious candidate.
It already gives a summoner cooldown reduction, raising how often minions are
summoned, and area of effect, enlarging the imp's death explosion. Minion damage
would have been a third compounding gain on one build. **Ferocity fails it too**:
critical strike chance and multiplier would compound the moment minions were ever
allowed to critically strike.

**Which family gets which is a judgement and is labelled one.** Creatures to
Spirit and machines to Agility reads right — a summoner binds a creature with
something soul-shaped, and the deployables are War skills on Crossbow and Spear,
which are mobile ranged weapons. Neither pairing compounds, so if this turns out
to play badly the two can be swapped without any of the reasoning above changing.

### The slot leak the issue warned about, and why nothing is widened

An attribute's affix rolls on the slots matching the stats it drives, so Spirit
and Agility both roll on defensive and mobility slots. A minion build therefore
gets its scaling attribute only on slots that would otherwise have kept the player
alive or moving.

**That is the archetype's cost, not a defect.** It is the same trade minion health
makes by rolling on defensive slots. Widening either slot list would hand every
energy shield build and every evasion build new offensive slots in order to serve
six skills, which is a much larger change than the one being made.

### What this does not do

**No row is added to `game/Data/Attributes.csv` yet.** "Minion damage" is not a
stat anything in the model or the engine knows until the minion type table exists,
so the two attribute rows land with #336. Adding them earlier would put a stat in
the table that nothing reads.

---

## 2026-08-14 — The eight rarity colours, and they may overlap the damage palette

**Affects:** section XIII of `Cataclysm_GDD_v2.md`. Applied. Issue #602.

### The decision

| Rarity | Colour |
| :-- | :-- |
| Everyday | White |
| Quality | Grey |
| Superb | Green |
| Masterful | Blue |
| Legendary | Yellow |
| Mythical | Orange |
| Ascendant | Purple |
| Cataclysmic | Red |

Chosen by the project owner on 2026-08-14.

### A constraint written earlier the same day was wrong and has been replaced

Issue #537, applied a few hours before, added a rule to section XIII saying the
rarity ramp **must not reuse the eight damage-type hues** from the effect
palette, on the grounds that a shared hue would make a drop's colour ambiguous
between what the item is and what it does.

**Seven of the eight colours above break that rule.** Measured against
`game/Data/ElementVisuals.csv`, which holds the eight Cataclysm damage hues:

| Rarity colour | Sits close to |
| :-- | :-- |
| Grey | Chaos (mid grey), and War (slate grey-blue) |
| Green | Pestilence (yellow-green) |
| Blue | Death (cyan) |
| Yellow | Celestial (gold), and Famine (olive) |
| Orange | Demonic (red-orange) |
| Purple | Void (violet) |
| Red | Demonic |
| White | nothing |

Put to the project owner, the answer was that they collide and it is not a
problem. **That is correct and the original rule was too strong.** The reason the
overlap is safe is not that the colours are far enough apart — they are not — but
that **the two palettes never share a surface.** Rarity colours appear on item
names, inventory frames and the marker over a drop on the ground. Damage-type
hues appear on skill and damage effects. Nothing in the game is both an item and
an attack, so nothing ever has to be told apart from itself. Path of Exile and
Diablo both run a rarity ramp and an elemental palette simultaneously for the
same reason.

The rule in section XIII now states the surface separation rather than a hue ban,
and `tools/tests/test_item_rarity_presentation.py` asserts the reason rather than
the old prohibition.

### One observation, recorded and not acted on

**Grey above White is unusual for the genre.** Diablo and Path of Exile both use
white for the ordinary tier and grey, where it appears, for something below it.
Here White is Everyday and Grey is Quality, one rung up. Nothing breaks: the ramp
is legible and the remaining six rungs follow the convention exactly. Recorded so
that it reads as a choice rather than an oversight if anybody asks later.

### What is still open

The second channel. The design requires that colour is not the only way two
rarities differ, so the frame and the drop marker have to differ by shape or
motion as well. That is not designed and belongs with the loot interface.

---

## 2026-08-14 — A damage-taken debuff is the defender's bucket, not the attacker's

**Affects:** the Stat Calculation part of section IV in `Cataclysm_GDD_v2.md`.
Applied. Issue #600.

### The decision

**"Increased damage taken" is additive on the defender.** Ten stacks of Exposed
add up among themselves, on the enemy carrying them, and the result multiplies
against whatever the attacker's own increases already produced. They do not join
the attacker's bucket.

The project owner's instruction was to take the recommendation and adjust later
if it proves wrong.

### Why it needed deciding at all

The damage pipeline is `(base + flat) x (1 + increases) x more1 x more2`, and the
document said "one bucket per stat, one multiplication". **It never said whose
stat**, and for a debuff that increases damage taken the two readings differ by
about a factor of ten:

| Reading | Ten stacks of Exposed are worth |
| :-- | :-- |
| The defender's own bucket, applied after the attacker's increases | close to a full 50% |
| Joining the attacker's additive bucket | a few percent, on a Bulwark whose retaliation increases already run into the hundreds |

That lands directly on **Thornwall**, a Bulwark Second Oath capstone option, and
on **Open Wounds**, a Bulwark basic node. It also decides every future enemy
modifier, status effect or affix worded "takes X% increased damage", which is why
it was answered once in section IV rather than per node.

### Why the defender's side

A debuff that grows weaker the more the attacker has invested is the opposite of
how every other scaling source in this design behaves, and it would be worth
least to the character best placed to use it.

Path of Exile, Path of Exile 2 and Last Epoch all place "increased damage taken"
on the defender. Diablo IV is the exception that tested the other shape:
Vulnerable was a separate multiplier, and patch 1.2.0 froze it at a fixed
baseline and pushed every further source into an additive bucket, because a
separate multiplier there was disproportionately powerful. **That is an argument
against making a damage-taken debuff a "more" multiplier, not against putting it
on the defender** — this decision is additive, and additive on the defender.

Sources:

- https://maxroll.gg/poe2/getting-started/damage-scaling-in-path-of-exile-2
- https://maxroll.gg/d4/getting-started/damage-in-depth

The Path of Exile official wiki could not be fetched — poewiki.net returns an
access-denied page to an automated request, which the #302 entry already records
as a limit. Both sources above are secondary and were read rather than taken from
a search summary.

### It does not implement anything

Nothing in `sim/cataclysm_sim/` or `game/Source/` applies a damage-taken debuff
today, so this changes no code and no number. It settles what the number will
mean when something does.

---

## 2026-08-14 — A full inventory is a choice about what to leave, and a Stash stands at every dungeon's middle floor

**Affects:** the Storage section of `Cataclysm_GDD_v2.md`. Applied. Issue #323.

### The decision

**A player cannot leave a dungeon partway through.** The only ways out are
clearing it or dying. That was the project owner's answer on 2026-08-06.

**So an item that will not fit stays on the floor.** A full inventory is a
decision about what is worth a slot, not a reason to walk out.

**And a Stash stands at the entrance to every dungeon's middle floor**, with no
empire upgrade needed. The Explorer branch of the empire upgrade tree makes them
more frequent from there.

### What was already designed, and what was actually missing

The project owner asked on 2026-08-06 whether to add Explorer nodes for this and
"maybe make it a capstone", leaving the final decision open. **The nodes already
exist**, as three ranked keystones in the Explorer branch of
`Empire_Development_Tree_Final.json`:

| Node | Effect |
| :-- | :-- |
| Field Depot I | A Stash appears every 30 floors in dungeons |
| Field Depot II | A Stash appears every 15 floors |
| Field Depot III | A Stash appears every 5 floors |

So there was nothing to add and no capstone to make. **The only thing genuinely
missing was the default**, and the design document said nothing about any of it.

### Why there is a default at all

Without one, a player who has spent no points in the Explorer branch has no
answer to a full inventory on a dungeon 100 to 150 floors deep, and cannot leave.
That is a dead end rather than a difficulty choice. One Stash at the middle floor
is the smallest thing that removes it.

### Why the ladder still means something with a default under it

The three keystones stack with the middle-floor Stash rather than replacing it,
and the interval ones only overtake it on long dungeons:

| Dungeon depth | Default only | With Field Depot I | With Field Depot III |
| --: | --: | --: | --: |
| 10 floors | 1 | 1 | 2 |
| 125 floors, a Cataclysm dungeon | 1 | 5 | 25 |

That is the right shape. Forty-eight carried slots are not under pressure over
ten floors, and they are under real pressure over a hundred, which is where the
Explorer investment pays.

### One node that looks relevant and is not

**Supply Caches**, a basic Explorer node, gives each floor a chance to contain a
loot chest. That adds items to carry rather than somewhere to put them, so it
makes a full inventory more likely rather than less. Recorded here because the
name reads like storage.

---

## 2026-08-14 — Madness takes the immunity window and boss immunity, but not the damage threshold

**Affects:** the "Stun and the Anti-Stun-Lock Rule" section of
`Cataclysm_GDD_v2.md`. Applied. Issue #303.

**Answered by the project owner on 2026-08-06 with "Option C", and not applied
until 2026-08-14.** The answer sat on the issue for eight days while the document
still marked Madness as open. Worth noting only because an answered issue that
looks open is easy to walk past twice.

### The decision

| Rule | Applies to Madness |
| :-- | :-- |
| A hit must take at least 10% of the target's maximum health | **No** |
| A target affected cannot be affected again for 5 seconds | **Yes**, shared with stun and knockdown |
| A boss cannot be affected at all | **Yes** |

### Why it is the one split case

The criterion the project owner gave on 2026-08-05 is that the rule covers
effects that "completely stop you from operating any part of your character".
Madness reads both ways: a maddened target moves and attacks at full capability,
so nothing is stopped — but nothing it does is for the side that owns it.

**The boss half is decided by the rule's own stated reason.** The document says a
boss cannot be stunned because "a boss that can be held still is not a fight". A
maddened boss is not fighting the player either. The word differs and the outcome
does not.

**The threshold half is decided by its stated reason too, in the other
direction.** The threshold exists to stop small hits interrupting constantly.
Madness is not an interruption; it is a redirection, and how hard the hit landed
has no bearing on whether redirecting a target for 3 seconds is fair.

### Why this was worth answering rather than leaving

**Madness is the longest hold in the game and the only one that is freely
rollable.** It is 3 seconds, where every stun a weapon skill grants runs 0.75 to
1.5 and the two knockdowns run 2 and 3. It comes from a suffix — "Of Madness", a
15% chance on a necklace, relic, ring or weapon in `game/Data/Affixes.csv` — and
from the Staff support skill Subjugate, which lasts 6 seconds against a burning
target.

Leaving it out of the coverage table read as "not covered" by omission, and under
that reading a build stacking chance-to-madden could hold a boss out of its own
fight indefinitely: no threshold, no window, no immunity. That is the exact
outcome the anti-stun-lock rule exists to prevent, reached through a different
word.

### What it costs, stated rather than discovered later

**Subjugate does nothing to a boss.** Its whole effect is Madness, so boss
immunity empties it in exactly the fight where a support skill matters most. That
is accepted: it still works on everything else, which is where the Staff support
slot will spend most of its uses. If the Staff support slot later turns out to
need something against a boss, that is a skill design question and not a reason
to reopen this one.

**The affix is worth less than it looks.** A 15% chance to madden that cannot
fire again for 5 seconds has a real ceiling on it now. Against ordinary enemies
it is unchanged, which is where it will be used most.

---

## 2026-08-14 — An account holds 24 characters, as one pool

**Affects:** section II and the monetisation and empire tree sections of
`Cataclysm_GDD_v2.md`, and the partition key section of `Save_System_Design.md`.
Applied. Issue #577.

### The decision

**24 characters per account, as a single pool.** Every character counts against
the same 24 whatever its population, whatever its lethality mode and whether or
not it is Solo Self-Found. Nothing raises it — no empire node, no city upgrade,
nothing sold. Deleting a character is the only way to free a slot.

### Why one pool rather than one allowance per partition

This is the question the issue actually turns on, because this design partitions
almost everything else. The answer is that **every partitioned thing is something
characters share** — the stash, the auction house, gold, the empire upgrade tree
— and a slot count is not shared between characters. It is a count *of* them.

Every game surveyed that has a limit holds it per account as one pool, and none
partitions it along the axes it partitions storage or progression:

| Game | The limit | What it does not partition it by | Source |
| :-- | :-- | :-- | :-- |
| Path of Exile 2 | 24 free slots | leagues — "By default, players can get 24 maximum free character slots across all leagues in the game's early access" | https://game8.co/games/Path-of-Exile-2/archives/496032 |
| Diablo IV | 12, raised to 14 in Vessel of Hatred | Eternal against Seasonal | https://www.wowhead.com/diablo-4/news/character-cap-increased-in-diablo-4-vessel-of-hatred-347617 |
| Last Epoch | 25 online, unbounded offline | the same online/offline split this design has | https://forum.lastepoch.com/t/character-slots/76622 |
| Grim Dawn | no game limit; a save-storage cap only | nothing | https://www.grimdawn.com/ |

**It also means the number survives issue #287.** That issue asks whether a
seasonal league resets the empire upgrade tree, and is open. A per-partition
count would be six times something today and twelve times it if #287 introduced a
fourth axis, so answering this one per partition would silently pre-commit that
one. A per-account count is unaffected. That is a judgement about this
repository, not a finding from the genre.

### Why 24, which is a judgement

It matches Path of Exile 2's free default exactly — the surveyed game with the
most partitions and the deepest build-crafting — and exceeds Diablo IV's 14,
which is right because this design has more partitions than Diablo IV does. It is
also the number of classes, so one of every class fits at once, which makes the
rule legible without a player having to look it up. The number is a tuning value;
the rule is that it is one pool and nothing buys more.

### Sources checked by hand, because the first pass got some wrong

The research pass for this decision produced a **fabricated quotation** — a
sentence attributed to the Path of Exile 2 page that does not appear on it — and
two misattributed Last Epoch citations, one crediting a forum poster's guess to a
developer and one giving a quote the wrong thread. An adversarial checking pass
found all three, and every source in the table above was then re-fetched and read
before being written here.

Two things are recorded as **not established**: Last Epoch's reason for its 25 is
a community member's guess about database cost and not a developer statement, so
it is not used as an argument here; and Grim Dawn's cap is a save-storage limit
rather than a design decision, so it is evidence about nothing except that a game
can ship without a limit.

---

## 2026-08-14 — An item's rarity changes its colour, never its model

**Affects:** the Item Rarities section, the Set Enchantments section and section
XIII of `Cataclysm_GDD_v2.md`. Applied. Issue #537.

### The decision

**Rarity is colour, frame and drop effect. The item base alone determines the
model.** A Cataclysmic sword and an Everyday sword of the same base are the same
mesh. A named set may carry bespoke geometry, and it is the only itemisation
layer that does.

### Why, and the strongest reason is internal rather than from the genre

**Rarity is computed, not stored, and it is mutable.** The design already says
so: "Rarity is not a property an item carries. It is a label for what fills its
four slots", and "Adding an affix promotes the piece. An Everyday item with an
affix added becomes a Quality item". So a model that followed rarity would change
shape at the crafting bench, in the player's hands, without the item having
become a different object.

**And the tiers are not a power ladder.** The same section says "A higher rarity
is not automatically a better item. It is a different item, weighted further
toward enchantments and away from ordinary stats." There is no ascending power
for ascending visual weight to track.

Either of those would be enough on its own. The genre agrees, and it draws the
line in a more precise place than "colour only":

| Game | Where the line falls | Source |
| :-- | :-- | :-- |
| Path of Exile | Procedural rarity is name colour; uniques are named items with their own art | https://www.poewiki.net/wiki/Item |
| Diablo 2 | Normal, magic, rare and set share base art; uniques and sets are named and get distinct art | https://diablo2.diablowiki.net/Item_Quality |
| Diablo 3 and 4 | Rarity is colour and beam; Legendary and Unique items are named and modelled | https://diablo.fandom.com/wiki/Item_Quality |
| Last Epoch | Rarity is colour; uniques and sets are named and separately authored | https://www.lastepoch.com/ |
| Grim Dawn | Rarity is colour; epics and legendaries are named items | https://www.grimdawn.com/ |

**The rule the genre actually follows is that art tracks an item's identity, not
its roll.** A named set has an identity. A Superb helmet does not. That is why
sets are the exception here and rarity is not.

### The model count this settles, and the figure it corrects

There are **55 item bases**: 14 weapon types and 10 armour and jewellery slots,
counted from `game/Data/ItemBases.csv`. Under this decision the number of gear
models to produce is **55**. Under rarity-per-model it would have been 55 times
the eight tiers.

Issue #17, the 3D asset bake-off, opens with "24 classes of gear, 15 weapon types
across eight rarity tiers". Both figures are wrong: there are 14 weapon types,
not 15, and the eight rarity tiers do not multiply anything. The 15th value
exists only in `game/Data/WeaponSkills.csv` as the pseudo-type "All", the marker
for weapon-agnostic skills, which is not a weapon. #17's body has been corrected.

### What this does not settle

**Which eight colours.** The rule is stated and the ramp is not assigned. Section
XIII now carries two constraints on whoever assigns it: the ramp must not reuse
the eight damage-type hues from the effect palette, because those already mean a
damage type wherever they appear, and colour cannot be the only channel, so the
frame and drop effect must differ by shape or motion as well. Filed separately.

**Where the model-to-base binding lives.** Nothing in `game/Data/ItemBases.csv`,
the Item Bases sheet or `FCataclysmItemBaseRow` has a mesh column, so "the base
decides the model" is a rule with nowhere to write the answer. That is
implementation and belongs with issue #44, which owns loot generation and
rarities. Filed separately.

---

## 2026-08-14 — One magnitude word per number: Overwhelming Presence keeps "more", Thornwall keeps "increased"

**Affects:** `Saboteur_Class_Tree_Final.json`, `Bulwark_Class_Tree_Final.json`
and `tools/tests/test_class_passive_trees.py`. Applied. Issue #582.

### What was wrong

Two passive tree node descriptions used both magnitude words for one number:

| Tree | Node | Said |
| :-- | :-- | :-- |
| Saboteur | Overwhelming Presence, a keystone | "all trap damage is **increased by 50% more**" |
| Bulwark | Thornwall, a Second Oath capstone option | "**increases** the retaliation damage they take from you **by 5% more**" |

The two words name different places in the damage pipeline
`(base + flat) x (1 + increases) x more1 x more2`. "increased" joins the additive
bucket; "more" is its own multiplier. A sentence using both does not say which
the number is, and on an invested character the two readings differ by a large
factor.

Neither is a rule violation. Issue #344, decided the same day, permits any
passive tree node the "more" multiplier. This is purely which of the two words
each sentence keeps.

### The decision, and it is one word each rather than one word for both

**Overwhelming Presence keeps "more"**, and now reads "While at maximum active
traps, you deal 50% more trap damage and trap trigger radius is doubled."

**Thornwall keeps "increased"**, and now reads "Each stack of Exposed increases
the retaliation damage they take from you by 5%, stacking up to 10 times." The
word "more" was simply deleted.

### Why they differ

**Thornwall is a debuff applied by being hit, and the genre is unanimous that
those are additive.** Path of Exile 2's Withered is "5% increased Chaos Damage
taken" per stack up to 10 stacks — the same percentage, the same cap, the same
role. Path of Exile 1's version is 6% over 15. Maxroll's Path of Exile 2 damage
page states the rule plainly: sources of "increased damage taken" are additive
with each other. Last Epoch does the same with Shred. Diablo 4 is the one game
that made a damage-taken debuff a separate multiplier, with Vulnerable, and in
patch 1.2.0 it deliberately froze that at a fixed baseline and pushed every
additional source back into the additive bucket because separate-multiplier
debuffs were disproportionately powerful.

Sources:

- https://maxroll.gg/poe2/getting-started/damage-scaling-in-path-of-exile-2
- https://maxroll.gg/d4/getting-started/damage-in-depth

**Overwhelming Presence is a keystone with one condition and one large
multiplier, which is what a "more" is for.** Path of Exile's keystone Elemental
Overload is the same sentence at a different number: skills that have critically
struck in the past 8 seconds deal 40% more elemental damage. Last Epoch places
its "more" multipliers on the build-defining picks.

**Two arguments from inside this game's own trees point the same way.**

The Saboteur tree has 21 basic nodes granting a percentage to traps, several at
+15% per point over 6 points, so a committed trap build reaches several hundred
percent additive without trying. The design document's own worked example is a
character at +800% increased who adds another +60% and gains 6.7%. At the
additive reading this keystone would be worth roughly 5% on the very build that
satisfies its condition — less than one point in a basic node — while costing a
keystone slot and demanding maximum active traps. It also sits above Reinforced
Housing, a *basic* node on the same tree that already says "20% more damage".

The Bulwark tree's retaliation identity is built entirely from additive nodes,
and two of them are near-twins of Thornwall worded plainly: Thick Hide is "Each
hit you survive increases your retaliation damage by 1% for 5 seconds, stacking
up to 10 times", and Open Wounds is "Enemies with 5 or more bleed stacks take 20%
increased damage from all sources". Making Thornwall the single compounding
source in that tree would make it the only correct Second Oath while reading
identically to Thick Hide.

### What this does not settle, and it changes what ten stacks are worth

**Which side owns the bucket for an "increased damage taken" debuff.** The design
document says "one bucket per stat" and does not say whether Exposed forms its
own bucket on the enemy — in which case ten stacks are worth close to a full 50%
and still multiply against the attacker's own increases — or joins the attacker's
retaliation-increase bucket, where an invested Bulwark would dilute it to a few
percent. In every game surveyed it is the former. This is filed separately rather
than decided here, because it applies to every damage-taken debuff and not only
to this node.

### A count in the issue was wrong, and so was one in this file

Issue #582 said Overwhelming Presence "was the only node of the twenty-five
containing 'more' or 'less' that used both words at once". It was not the only
one — Thornwall did the same — and the figure was wrong: 29 strings across the
five trees contained the word and 20 used it as a magnitude. The #344 entry above
also said nine keystones use the wording, and twelve do.

Two tests now measure rather than assert from memory:
`test_the_measured_wording_counts_are_still_right` pins both counts, and
`test_no_node_uses_both_magnitude_words_for_one_number` looks for the shape of
the fault rather than for the two nodes by name, so it catches a third one.

---

## 2026-08-14 — A Solo Self-Found character gets its own 600-slot stash

**Affects:** the Solo Self-Found table row, the Capital Services table and the
Storage section of `Cataclysm_GDD_v2.md`, and the storage table and character
record list in `Save_System_Design.md`. Applied. Issue #576.

### The decision

Asked while answering #325 about character deletion, the project owner said "that
tree is individual per ssf character, same with their stash", and then confirmed,
when shown the two places the design document said the opposite, that the
document should follow **own private stash, 600 slots**: each Solo Self-Found
character opens a stash of its own, the same size and shape as the shared one,
shared with no other character at all — not with the others in its lethality
mode, and not with another Solo Self-Found character.

### What the document used to say

Three places said it had none, and one implied it:

- the Solo Self-Found table row: "No auction house, no shared stash"
- the Storage section: "A Solo Self-Found character has no stash at all, which is
  what its table row says, so everything it owns is carried"
- the Capital Services table: Stash "(disabled in SSF)"
- `Save_System_Design.md`'s storage table: "**None.** It has no stash"

### The genre, which is not unanimous, and that is the finding

The issue expected the genre to be unanimous that a self-found flag restricts
trading and never storage, and said that if so, this design was the outlier. **It
is not unanimous.** One shipped game does exactly what was decided here, and it is
the closest comparison available.

| Game | What its self-found flag does to storage | Source |
| :-- | :-- | :-- |
| Path of Exile | Restricts trading and partying only. SSF characters on an account still share stash space with each other. Official as of 2.6.0. | https://pathofexile.fandom.com/wiki/Solo_Self-Found |
| Last Epoch, **Solo Account Found** | No grouping, no items from other players — but "you can still share your own items and materials with your other Solo Account Found characters". Storage untouched. | https://dotesports.com/last-epoch/news/what-does-solo-account-found-mean-in-last-epoch |
| Last Epoch, **Solo Character Found** | All of the above **plus** it "prevents you from using your other characters' stash": "anything you find on a character can only be used and seen by that Character", and each character keeps its own stash. | https://thenaturehero.com/last-epoch-ssf/ |

So Last Epoch ships both readings as two separate difficulty tiers, and the
stricter one is the decision taken here. That is worth recording for two reasons.
It means this rule has been played by real players rather than being invented,
and it names what the mode is actually strict about: **who can reach an item, not
how much of it can be put down.**

What the research does not settle: whether Last Epoch's Solo Character Found
stash is the same size as an ordinary one. One guide describes the per-character
stash as six tabs of a 10 by 10 grid, which is the same shape this design already
uses, but a secondary source is not enough to lean on and nothing here depends on
it. Matching the shared stash exactly is a judgement, taken because a private
stash of a different size would be a second number to tune for no stated reason.

**The old design was the outlier, not the new one.** No game found in this survey
removes storage from a self-found character. Removing the container entirely was
this project's invention.

### What this softens, and it is a real loss

The design used "no stash at all" as part of why Solo Self-Found is the harsher
choice. A character that carries everything has nowhere to put an item it cannot
use yet, and with a dungeon floor costing a day, that pressure was real. Six
hundred slots removes it. The mode keeps its two other constraints — no auction
house and no shared storage of any kind — and those are what the promise is
actually made of.

### One argument had to be rewritten rather than only its facts corrected

The Storage section derived the missing auction house from the missing stash: the
market lists from the stash, a Solo Self-Found character has no stash, so it can
have no market. The first half is still true and the second is not.

The market follows from the mode's promise that nothing reaches this character
from another player, which is a statement about trading and not about storage.
**It never needed the storage argument**, which only looked load-bearing because
both facts happened to be true at once. `tools/tests/test_storage.py` now asserts
that the old derivation is gone and that its replacement is present, so neither
can be dropped quietly.

### Where it is stored

The private stash lives in the character's own record, beside its private empire
upgrade tree, so a Solo Self-Found character still touches no account record and
its save file stays self-contained. That makes the file roughly ten times larger
in item data than an ordinary character's: 600 stash slots against 48 inventory
slots and 18 equipped items, every item carrying its rolled affixes.
`Save_System_Design.md` says so, because it is the kind of thing that is only
surprising once it is being implemented.

---

## 2026-08-14 — Standing in burning ground for its whole life costs one hit of the skill that left it

**Affects:** section V's rider list in `Cataclysm_GDD_v2.md`, the Weapon Skills
sheet of `All_Things_Cataclysm.xlsx`, `tools/generate_datatables.py` and
`sim/cataclysm_sim/enemy_abilities.py`. Applied. Issue #361.

### What was wrong

`ACataclysmGroundZone::SpawnRound` and `SpawnLine` take a `DamagePerTick`
parameter and the zone ticks once a second. **Nothing supplied that number from
data.** Twenty-two player skills leave burning ground and every one of them
stated only `GroundRadius` and `GroundDuration`, so a skill that says it "leaves
a pool of lava" left a pool dealing whatever the caller happened to pass, and two
skills with identical radius and duration could silently deal different amounts.

Issue #361 says eight skills. **It is 22**, counted from
`game/Data/WeaponSkills.csv` on 2026-08-14.

### What was decided

A new rider, `GroundPercent`: the percent of the skill's damage the ground deals
per second. Its value is **100 divided by `GroundDuration`**, so standing in the
ground for its whole life costs exactly one hit of the skill that left it.

Chosen by the project owner on 2026-08-14 from three options.

In practice that runs from 10% per second for the four 10-second Ultimate patches
to 33.3% for the two 3-second Movement patches.

### What the genre settles, and what it does not

**Settled: fire damage over time in this genre is priced as a share of the hit
that caused it, per second, rather than as a flat number.** Path of Exile's
ignite deals 50% of the hit's base fire damage per second, over a base 4 second
duration — so 200% of the hit in total. Its *burning ground* from map modifiers
is different again: a flat figure per second by map tier, 800, 1200 or 1600,
independent of anything the player did.

**Not settled, and this is the part that mattered: how a player skill's leftover
ground patch should be priced against that same skill's hit.** A search for that
specific comparison in Path of Exile, Diablo IV and Last Epoch returned general
burn mechanics and no direct answer. So the rule below is a judgement, not a
finding, and it is deliberately more conservative than Path of Exile's ignite.

### Why "one hit over the full duration"

**Duration stops being a damage multiplier.** Under a flat percent per second, a
10-second Ultimate patch would be worth three times a 3-second Movement patch —
and the Ultimates already have the largest radii and the longest durations, so
they would gain from the same lever twice.

**Burning ground stays area denial rather than a second damage source.** `Burn`
is already this game's fire damage-over-time path. A second one overlapping it
would make fire skills scale on two axes at once.

**It is one rule rather than two.** The Hellhound's fire trail already worked
this way and said so in prose: "a quarter of one of its bites per tick, so that
standing in it for the whole 4 seconds costs exactly one bite". That is
`GroundPercent=25` over a 4-second duration, which the general rule produces
exactly. The trail's figure is now data rather than prose.

### What this forecloses, stated plainly

**A burning-ground build cannot exist.** No amount of investment makes ground the
main damage of a build, because the ground is always worth one hit and the hit
scales with everything the ground does. That is a real design space closed off,
and it was the third option on the table: Path of Exile's shape, where a fixed
50% per second makes the full duration worth twice the hit and ignite builds are
built entirely on it.

If a burning-ground archetype is ever wanted, this rule is what would have to
change, and the place to change it is the one sentence in section V rather than
22 rows.

Sources:

- [Ignite — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Ignite)
- [Burning ground — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Burning_ground)

---

## 2026-08-14 — The Corrupted Stalker is a Generic dungeon modifier at weight 20

**Affects:** the Dungeon Modifiers sheet of `All_Things_Cataclysm.xlsx`, and the
Corrupted Stalker section of `Cataclysm_GDD_v2.md`. Applied. Issue #504.

### What was wrong

The Corrupted Stalker is described in full in section VIII of
`Cataclysm_GDD_v2.md` — a corrupted former player character, drawn from a shared
table of characters consumed by Worn Residue, that hunts the player across a
dungeon's floors. **It was the only dungeon modifier described in prose and
absent from the data.** The Dungeon Modifiers sheet held 116 modifiers and none
of them was this one.

That mattered for a specific reason. Every dungeon modifier carries a weight, and
the sum of the weights on a dungeon is the Modifier Score in the Enemy Score
formula. A modifier with no weight cannot be scored, so a dungeon carrying this
one could not have its difficulty computed at all.

### What was decided

Asked on 2026-08-14, the project owner chose **weight 20** and **applies to all
eight Cataclysms**.

**Weight 20 is the top band**, shared with twelve existing modifiers. Those are
the ones that change how a floor is played rather than how hard it hits: Edict of
Silence stops all skill use for 15 seconds at a time, Reality Twister adds a
random modifier to the dungeon every 30 seconds, The Reaper kills instantly on
contact. An enemy carrying a real player's class, level, passive allocation,
equipment and skills belongs with those rather than below them.

### "Applies to all eight" already had a name, and it is Generic

The issue expected this to be a new category the sheet had no column for. It is
not. **`Generic` is already a Cataclysm Type in this project**, used by ten
entries in the enemy modifier table to mean exactly this, and
`tools/generate_datatables.py` already lists it in `CATACLYSM_TYPES`, which is the
same list the dungeon modifier handler validates against.

So no code changed. The row was added with `Generic` and the generator accepted
it. This is the first dungeon modifier to use that value and currently the only
one.

Choosing an existing word over a new one matters here for the reason the Magic
Find decision of 2026-08-05 gives: one thing gets one name, because two names for
one concept in the shipped tables is a defect.

### What this does not settle

**Whether the Corrupted Stalker competes for one of a dungeon's modifier slots or
is granted separately.** A dungeon carries one modifier per difficulty tier, so a
tier 8 dungeon carries eight, drawn from a pool of 12 to 15 per Cataclysm. A
Generic modifier is drawable by every Cataclysm, which makes it the only entry
that can appear alongside any of the others, and nothing states whether it takes
a slot. Nothing depends on the answer yet.

**The uneven modifier counts.** Six Cataclysms have 15, Demonic has 14 and Void
has 12. Whether 15 is the intended target is recorded nowhere and was not decided
here.

---

## 2026-08-14 — One enchantment raises minion count, not two, and gear contributes +2 to +4

**Affects:** the Enchantments sheet of `All_Things_Cataclysm.xlsx`, and the minion
section of `Cataclysm_GDD_v2.md`. Applied. Issue #339.

### What was wrong

Two enchantments both raised the number of minions a player can have active, and
**the rarer one was strictly weaker**:

| Enchantment | Weight | Grants |
|---|--:|---|
| You can have 1-2 additional minions active simultaneously | 2 | +1 to +2 |
| Add 2-4 to your maximum minion count | 4 | +2 to +4 |

For enchantments a lower weight is rarer — the design document says so plainly:
"The maximum resistance enchantment is weight 1, which is the rarest and most
powerful tier." So a player who found the rarer of these two was worse off than
one who found the common one, and nothing said why.

### What was decided

Asked whether to merge them, swap the weights, or keep both and state a reason,
the project owner chose on 2026-08-14: **merge into one at weight 2, granting +2
to +4.**

So the surviving enchantment is "Add 2-4 to your maximum minion count" at weight
2, carrying the tags of both. The other was removed.

### Why merging rather than swapping

The two said the same thing in different words. Swapping the weights would have
fixed the inversion and left two near-duplicate entries in a table of 379, which
is how the inversion became possible in the first place.

**The bound changed and it matters.** Each enchantment can appear only once
across all equipped gear, so two of them bounded gear-granted minion count at +3
to +6. One bounds it at **+2 to +4**. That figure is load-bearing: minion count
multiplies damage, effective health and every rider at once, which is the whole
reason issue #209 put count in the enchantment table rather than letting it be an
affix — there are eight ring slots, so a "+1 minion" suffix would be eight from
rings alone.

### How the workbook was edited, which is worth recording

The Enchantments sheet holds **two independent tables side by side**: positives
in columns A to D and negatives in F to I. They are not paired; a row in one has
no relationship to the row beside it.

**So the removed enchantment was cleared, not deleted.** Deleting spreadsheet row
135 would have taken the negative enchantment sharing that row — "Melee skills
cost 5%-10% of your maximum HP to use" — which has nothing to do with minions.
`tools/generate_datatables.py` skips a row whose text cell is empty, so clearing
A135 to D135 removes the positive and leaves the negative intact. The generated
negative table stayed at 195 rows, which confirms it.

Row keys are derived from an enchantment's own text rather than its position, so
clearing a row renames nothing else. Only `EnchantmentsPositive.csv` changed, and
only one DataTable asset was rebuilt.

---

## 2026-08-14 — A zero in a class stat line is a starting value, not a gate

**Affects:** section IV of `Cataclysm_GDD_v2.md`, under The Three Demonic Class
Stat Lines. Applied. Issue #345.

### What the question was

`docs/Masochist_Class_Tree_Final.json` has one node, `basic_ss_b2` "Rupture
Focus", that reads "When your Energy Shield breaks, enemies within 6 metres are
stunned for 0.1 seconds per point." The Masochist has **0 base Maximum Energy
Shield**, and the design says that is deliberate: "a shield absorbs the damage
the class needs to convert."

So issue #345 asked whether that node can ever do anything, and behind it a much
larger question: **can a class with 0 base in a stat gain that stat from gear at
all?** If the answer were no, a class stat line would be a hard gate and the
consequences would reach well past one node.

### It was already answered, and the answer is yes

Nothing needed deciding. `Cataclysm_GDD_v2.md` already says it four paragraphs
further down, under Stat Calculation:

> **Attributes scale values, they do not create them.** Health, mana and energy
> shield come from three places: the class's base value, its per-level scaling,
> and flat values from gear.

Gear supplies it in two ways that already exist in the data: the **Vestment**
body armour base grants 120 maximum energy shield as an implicit, and the **flat
maximum energy shield** affix reaches 50 at its top tier. `game/Data/ClassStats.csv`
has a `max_energy_shield` row for the Ritualist alone, so the Ravager's and
Masochist's zero is an absence rather than a prohibition.

**This entry exists because the answer was three sections away from the question.**
A reader looking at the stat table saw a 0 and had nothing telling them whether
it was a floor or a wall. The rule is now stated where the table is.

### What the zero does cost, which is the part worth writing down

A class with 0 base gets no help scaling the stat. The per-level scaling is zero,
there is no matching regeneration on the class, and every "increased" source
multiplies only what gear supplied. So the difference between the Ritualist's 832
and the Masochist's 0 is not "one can and one cannot" — it is that one compounds
and one does not.

**For the Masochist there is a second cost and it is specific to the class.**
Anguish is generated by health lost. A shield absorbs damage before health does,
so every point of energy shield is a point of resource the class does not
generate. The 2026-08-06 entry on the Anguish resource made that arithmetic; this
records that it applies to gear as well as to the class base.

### What happens to the node

**Rupture Focus stays as it is.** It is a legitimate build enabler for a
gear-driven variant: the shield breaking is the moment a Masochist starts taking
damage again, so a node that pays out at that moment is coherent. It costs
nothing to a build that never wears a shield, and the rest of the Soul Scourge
branch is mana-based and does not depend on it.

The issue's own recommendation was to keep it, offered rather than decided. The
answer above is what makes it a decision rather than a guess.

Its stun duration was already reduced from 0.5 to 0.1 seconds per point when the
tree was written, separately, because 10 points at 0.5 would have been a 5 second
stun against a 5 second stun immunity window. That is issue #303 and is unchanged
here.

---

## 2026-08-14 — Every passive tree node may use a "more" multiplier, not only keystones

**Affects:** section IV of `Cataclysm_GDD_v2.md`, where the wording rule is
stated, and the restatement in the affix section. Applied. Issue #344.

### What was decided

The wording "more" and "less" — the multipliers that apply separately instead of
joining the additive bucket — was reserved for "gems, passive tree **keystones**
and enchantments". It now reads "gems, passive tree **nodes** and enchantments".
Basic nodes and capstone options are covered as well as keystones.

Asked whether to reword the offending node, record an exception for it, or move
the effect onto a keystone, the project owner answered on 2026-08-14:

> add an exception for passive nodes as well.

### Why the wider rule rather than a node-level exception

**Issue #344 described one node. Eight broke the rule.** Every node in the four
class trees and the empire tree was scanned for a number, a percent sign and then
the word "more" or "less", which is the test
`tools/tests/test_class_passive_trees.py` already applies so that ordinary
English does not match — "3 or more enemies", "more than 10 meters", "5 or more
affixes" and the like are not magnitudes and are correct as written.

The eight that were not allowed under the old rule:

| Tree | Node kind | Node | The magnitude |
|---|---|---|---|
| Saboteur | basic | Reinforced Housing | 20% more damage per trap trigger |
| Empire | basic | Economic Zones | +5% more Gold per point |
| Empire | basic | Salvage Protocol | 25% more crafting materials |
| Empire | basic | Thrifty | 1% less gold per point in the Market |
| Bulwark | capstone option | Second Oath, Thornwall | 5% more retaliation damage per stack |
| Masochist | capstone option | The Second Vow, The Immortal Champion | 50% less damage |
| Masochist | capstone option | The Final Vow, Apotheosis | 1% more damage per 100 Anguish |
| Empire | capstone option | The Imperial Vanguard, The Midas Touch | 50% more gold |

Twelve keystones also use it and were always allowed to: nine in the Masochist
tree, Overwhelming Presence in the Saboteur tree, and Scaffolding and Imperial
Command in the empire tree. **This line said nine when it was written**, counting
only the Masochist tree; corrected on 2026-08-14 while working issue #582. Only
the non-keystone count is pinned by a test, so nothing caught it.

The Thornwall row above also changed the same day. Issue #582 rewrote it from
"5% more retaliation damage per stack" to "5% increased", so it no longer relies
on the widened rule and `NODES_RELYING_ON_THE_WIDENED_RULE` is now seven.

**The first count taken during this work was four, and it was wrong**; it read
each node's own description and not the option text hanging off a capstone, which
is where a capstone's effects actually live. The corrected figure is eight, and
`NODES_RELYING_ON_THE_WIDENED_RULE` in that test file pins it so the next change
to any of them is deliberate.

A node-level exception would have needed eight entries across three files. The
rule was simply drawn in the wrong place.

**The line that carries the weight is gear, not node kind.** A gear affix is
rolled and can arrive by accident; a passive node is chosen and paid for out of a
fixed 230 point budget. That is the distinction worth enforcing, and it is
unchanged: no ordinary affix is a "more" multiplier.

### Where this diverges from the genre, and it does

Path of Exile is the closest comparison and it draws the line differently.
"Increased" is additive and "more" is multiplicative there in exactly the same
way, but **its small passive nodes typically grant "increased"**, and "more"
multipliers come from notables, keystones and other sources. A small passive is
the closest thing there to a basic node here.

**So this decision goes further than the genre does, and that is a judgement
rather than a finding.** The argument for it is the point budget: this game's
basic node holds up to 8 points and a player spends 230 in total across every
tree, so a basic node is a larger commitment relative to the budget than a Path
of Exile small passive is. The argument against it is that the additive bucket
is what keeps damage readable, and widening the set of things that escape it
makes the ceiling harder to reason about.

`CLAUDE.md` says constants are tuned against real play rather than argued to
death first, and this is a wording rule rather than a constant, but the same
applies to how many nodes end up using it. If the ceiling turns out to be the
problem, the narrower rule is one sentence away.

### What this does not settle

**Saboteur's keystone Overwhelming Presence says "increased by 50% more"**, which
is both magnitude words for one number and cannot be read either way. It is a
keystone, so it was always allowed the multiplier; the sentence just has to pick
a word. Filed as #582, because it is a wording defect rather than a rule
question.

Sources:

- [Damage Scaling — Path of Exile 2, Maxroll](https://maxroll.gg/poe2/getting-started/damage-scaling)
- [Passive Skills — Path of Exile 2 Wiki](https://pathofexile2.wiki.fextralife.com/Passive+Skills)

---

## 2026-08-14 — A player can delete a character, and that is the only thing that destroys an empire tree

**Affects:** section II of `Cataclysm_GDD_v2.md`, where a new "Deleting a
Character" subsection sits beside "Ending a Run"; section IX "Empire-Wide
Upgrades"; and the "Roguelike Meta Progression" summary in section XII. Applied.
Issue #325.

### What was decided

Asked on 2026-08-14 whether a player can delete a character, the project owner
answered:

> Yes you can delete a character. Your tree progression persists, but within game
> mode, with the exception of SSF. That tree is individual per ssf character,
> same with their stash.

Asked to separate that from the inheritance rule already in the document, they
added:

> Weird wording. If you actually delete your character, that's when the tree is
> removed. Dying doesn't delete a character in any game mode. The run resets,
> they keep their gear/levels/passive trees/empire tree and try again.

So:

1. **A player can delete a character**, and it is the only thing that removes
   one.
2. **Deleting an ordinary character costs that character and nothing else.** The
   lethality mode's tree, stash, gold and market belong to the account and are
   untouched.
3. **Deleting a Solo Self-Found character destroys its private empire tree with
   it.** That character was the tree's only owner.
4. **Every Solo Self-Found character starts its tree from nothing, always.**
   There is no successor case.
5. **Dying never deletes a character, in any lethality mode**, which the document
   already said and now says beside the rule it is most confused with.

### What this replaced

The document carried a rule from issue #286: "When a Solo Self-Found character is
lost, its private tree is held, and the next Solo Self-Found character created in
the same lethality mode inherits it rather than starting from nothing." Issue
#315 then settled that nothing loses a character, which left that rule labelled a
safeguard with no trigger.

**#325 was raised to find the trigger. The answer removed the rule instead.**
Deletion is what loses a character, and deleting is when the tree is removed
rather than when it is handed on. The document no longer describes inheritance
between Solo Self-Found characters in any form.

The owner's word for the old rule was "weird wording", and it was: "when a Solo
Self-Found character is lost" was written to be neutral about what could lose one,
and read as though something could.

### What the genre settles, and what it does not

**Settled: deleting a character does not touch account-level progression.**
Deleting a Path of Exile character leaves the account's stash tabs and their
contents intact, and leaves atlas progression intact, because both are held per
account and per league rather than per character. A stash on a permanent league
keeps its contents even when no character remains on that league. That is exactly
rule 2 above.

**Not settled, and specific to this game: what deletion does to a
character-owned meta-progression tree.** No shipped game in the genre has one.
Path of Exile's atlas is per account and per league; Diablo III's Paragon is per
account and per mode; Diablo IV's altars and season journey are per account and
per realm. Solo Self-Found in those games restricts trading, not progression
ownership. **So rule 3 is a judgement, not a finding**, and it should be labelled
as one. The judgement is that a tree with exactly one owner ends when that owner
is deliberately removed, because there is nothing left to hold it and holding it
for a stranger is the reading issue #286 already called "destruction with extra
bookkeeping".

### What argues against it

**A player can lose an arbitrary amount of meta-progression with one click**, and
Solo Self-Found is the flag where the tree took longest to build. Nothing else in
this design destroys empire upgrade points.

The answer is that deletion is different in kind from every other loss in the
game: the player chooses it, from outside a run, knowing what is in the tree. It
is not an outcome the game imposes. Whether the interface should make that cost
visible at the moment of deletion is a user-interface question and is not decided
here.

### What this does not settle

**How many character slots an account has.** The document has never said, and
deletion existing makes the question live, because freeing a slot is one of the
reasons to delete. Filed as #577.

**What deleting a Solo Self-Found character does to its private stash**, which
the same answer created. Filed as #576, because the stash change is its own
concern and contradicts two sentences the document currently carries.

Sources:

- [Stash — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Stash)
- [Atlas Progression Guide — Maxroll](https://maxroll.gg/poe/getting-started/atlas-progression-guide)

---

## 2026-08-14 — Offline and online characters share nothing, so the partition key is the population times the lethality mode

**Affects:** section II "Difficulty Options", section IX "Empire-Wide Upgrades"
and the Storage section of `Cataclysm_GDD_v2.md`, and section 3 of
`Save_System_Design.md`. Applied. Issue #528.

### What was decided

**Separate.** An offline character and an online character never share an empire
upgrade tree, a stash, a market or a balance of gold, whatever their lethality
mode. The partition key becomes the population together with the lethality mode,
so a player who plays both holds up to six of everything the account shares
rather than three, plus one self-contained record per Solo Self-Found character.

Answered by the project owner on 2026-08-14, choosing the recommended option.

### Why this was not really a new decision

Decision #505 on 2026-08-10 already made the two populations permanently
non-transferable, and gave the reason: an offline save is a local file and a
local file can be edited. It listed three things an offline character does not
get — the auction house, the ladder, and the shared table of corrupted
characters. **It did not mention the stash or the empire tree, and neither did
the design document.**

That gap was not neutral. A shared stash both populations could open is a
transfer route through the rule #505 made, and it is the shortest one: edit an
item into a local save, put it in the stash, withdraw it on an online character,
sell it in the auction house. Sealing the market and leaving the container open
beside it closes the smaller route and leaves the larger one open. The design
document already uses that exact argument one section earlier, to explain why the
lethality mode partition covers the stash and not only the tree.

So the answer was forced by a rule already made rather than chosen, and the
asymmetry would have been the wrong way round otherwise.

### What the genre does

Both games #505 cited for the non-transferability rule also partition storage on
the same axis, which is the specific question this issue asked and #505 did not.

**Last Epoch.** Offline characters are saved locally; online characters are
stored on the servers. Importing an online character to offline or the reverse is
impossible, and each population has its own stash — items cannot move between
them. The stated reason is the one this decision rests on: local save files can
be edited, and the developers did not want players gaining an advantage by
editing character data.

**Diablo II.** Open Battle.net characters are stored on the player's own
computer; closed realm characters are stored on Blizzard's servers. An open
character may never become a closed one. The Arreat Summit's own description of
open characters is that they were left in for players who cannot resist duping
items or hacking their character.

**Diablo III on console**, already recorded under #505, is the counter-example
that shows the cost of not doing this: local hero saves, and leaderboards that
were permanently polluted as a result.

**Confidence is not uniform.** The Last Epoch finding is well attested across
several independent write-ups and matches the developers' stated reasoning. The
Diablo II detail that a closed character can be exported to open, while an open
one can never become closed, comes from a fan wiki archive rather than a primary
Blizzard source; the direction that matters here — local to server — is blocked
in every account of it, and that is the part this decision leans on.

### What it costs

A player who plays both offline and online keeps two unrelated sets of empire
progress and two stashes, and the second starts from nothing. **That is the same
cost the design already accepts for choosing Hardcore**, and it is defended there
in the same words. It is charged twice for a player who does both, which is the
honest description of it.

The alternative was cheaper for that player and left a hole in a rule made four
days earlier. Closing that hole later would have meant taking a filled stash away
from players, which is the failure mode Diablo IV's Altars of Lilith produced on
a neighbouring question.

### What this does not settle

**Whether an offline character can be converted to an online one, one way, at the
cost of leaving everything behind.** Diablo II allows the safe direction and
blocks the unsafe one. Nobody has asked for it here and nothing depends on it, so
it is not written into the design; it is recorded here so the next reader knows
it was considered rather than missed.

Sources:

- [Last Epoch 1.0 — Can You Transfer Offline Character to Online Mode?](https://www.gamepressure.com/newsroom/last-epoch-10-can-you-transfer-offline-character-to-online-mode/z769a7)
- [Can you play Last Epoch offline? — PCGamesN](https://www.pcgamesn.com/last-epoch/offline)
- [Your Stash. Offline and Online… — Last Epoch Steam discussion](https://steamcommunity.com/app/899770/discussions/0/4338725867372498041/)
- [The Arreat Summit — F.A.Q.: Realms](https://classic.battle.net/diablo2exp/faq/realms.shtml)
- [Battle.net — Diablo Wiki](https://diablo2.diablowiki.net/Battle.net)
- [Archive: Realm FAQ — Diablo Wiki](https://diablo2.diablowiki.net/Archive:_Realm_FAQ)

---

## 2026-08-13 — The eight damage types have an effect palette, and one rule generates it

**Affects:** section XIII of `Cataclysm_GDD_v2.md`, applied. Issue #546. Colours
given by the project owner; two were changed and the changes are recorded below.

### The rule, which matters more than the eight colours

**A world surface may not exceed 30% brightness. An effect's primary may not fall
below 60%.**

To clear 3:1 — the accessibility threshold for a graphical object that is not
text — against the brightest surface the world is allowed to have, a colour must
reach **60.5%** brightness. That single derivation replaces checking eight
colours against eight environments one pair at a time.

**It makes the environment cap load-bearing rather than a style preference.** A
floor built brighter than 30% does not look slightly wrong; it silently breaks
the readability of every effect in the game.

### The palette

| Damage type | Primary | Secondary |
| :-- | :-- | :-- |
| Demonic | `#FF7A2E` | `#3A0A02` |
| Death | `#8FD8EC` | `#0C1418` |
| War | `#9AA7B4` | `#1C2229` |
| Famine | `#BCA95F` | `#221A0E` |
| Pestilence | `#B4E84A` | `#16200A` |
| Void | `#B978F5` | `#140A20` |
| Celestial | `#FFE9A8` | `#3A2A08` |
| Chaos | `#D6D6D6` | `#232323` |

Two colours each, because an effect coloured like its damage type is at its least
readable in the environment that damage type generates. The secondary is a dark
anchor, not decoration — the same conclusion the attack warning marker reached
when a single colour could not survive both black and gold environments.

### Two colours are not what was asked for

**Void was asked for as "darker purple" and is not dark.** A dark effect on a dark
floor cannot be seen, and Void's own environment is the darkest of the eight. Its
darkness lives in its secondary and its form — erasure, pulling inward, a centre
darker than the floor — rather than in its primary. This is the attack warning
marker's lesson applied again: what reads as absence is a bright rim around a dark
centre.

**War was asked for as "basic looking" and is gunmetal rather than white.** A
neutral white steel measured as confusable with Chaos at a perceptual difference
of **5.0**, where 15 is the line. Chaos is defined as having no hue at all, so War
is the one that moves. Gunmetal takes the pair to **19.7** and keeps War reading
as armour rather than as an element. Blood red stays as War's accent for bleeding
specifically; it is not the damage type's colour.

### Where hue cannot separate a pair, form does

Three damage types are built on darkness and two share brown. Death settles
downward, Void pulls inward, Chaos never settles; Famine drains and drifts,
Pestilence spreads and clings. **A shape and motion difference survives twenty
enemies attacking at once and a subtle hue difference does not.** That is Final
Fantasy XIV's approach, where Sage is separated from White Mage by form rather
than by colour.

### These are base values, not the finished look

The project owner made the point plainly: "there's just something wrong with
looking at a flat color and trying to envision it as part of a video game. Needs
a material and texture and all that."

**That is correct and the document now says so.** The palette fixes the starting
constraint — the hue an effect reads as, and the guarantee it stays visible. The
finished appearance comes from the material: emissive multiplier, spawn rate,
velocity and motion curve, which `Niagara_Conventions.md` already carries as
separate per-damage-type values so colour is one axis of five.

**So the palette cannot be signed off from a swatch and is not being asked to be.**
Expect the values to move once the first effect exists in the sandbox. The two
brightness numbers are what must not.

### Held by a test

`tools/tests/test_effect_palette_stays_readable.py` reads the palette out of the
design document's own table rather than restating it, and reads the eight damage
types from the workbook. It fails if a primary drops below the floor, if two
become confusable, if a secondary stops being dark enough to anchor, if any
clashes with the attack warning red, or if the two numbers stop being stated.

All three guards were proven to fail, including making Void dark as originally
asked for.

---

## 2026-08-13 — Particle effects are one system per shape, not one per damage type

**Affects:** adds `Niagara_Conventions.md` to this folder. Nothing in
`Cataclysm_GDD_v2.md` changes. Issue #19. Nothing is implemented: the project has
zero Niagara assets.

### Why this was written before the first effect exists

The full game needs effects for roughly 400 skill rows, built by one person with
no visual effects background. **Eight effect shapes times eight damage types is
64 assets if built the wrong way and 8 assets plus 8 data rows if built the right
way.** That difference decides whether the work is possible, and it is cheap to
choose now and expensive to change later.

### What was decided

**One Niagara system per effect *shape*.** An impact, a projectile, a beam, an
aura, a death. The eight damage types are eight rows in a new table,
`DT_ElementVisuals`, keyed by the gameplay tags that already exist. No damage
type ever appears in an asset name; typing `NS_Impact_Demonic` means the template
has failed.

**Asset prefixes use the `N` family — `NS_`, `NE_`, `NMS_` — not Epic's
documented `FXS_`, `FXE_`, `FXF_`.** This one is worth recording because it goes
against Epic's own published page.

**Epic does not follow its own recommendation.** A search of the local Unreal
5.8 install found 23,140 asset files and **not one** using `FXS_`, `FXE_` or
`FXF_`. What Epic ships is `NS_`, 40 times, in the Third Person and Top Down
templates among others. The same search finds 560 `BP_` and 1,251 `M_`, so it
works. Since this project has no visual effects artist and will import bought
asset packs that all ship `NS_`, choosing `FXS_` would guarantee a permanently
mixed project for no benefit.

Everything that is not Niagara stays on Epic's page, which this project already
follows.

**Four effect type assets, authored before the first system.** This is the part
that decides whether twenty enemies attacking at once holds frame rate.

### The finding that matters most

**A newly created Niagara effect type performs no culling whatsoever, and a
project ships with no default effect type at all.** Verified by reading the
engine source in the local 5.8.1 install rather than from documentation: every
culling switch is false, every limit is zero, there is no significance handler,
and the update frequency is "spawn only".

**So "use the default" means "no scalability".**

Two of those defaults are traps rather than off switches:

- The default cull reaction is the **most aggressive** one, not a disabled
  safeguard. It is simply inert until a cull switch is turned on.
- **"Spawn only" survives turning the cull switches on.** Ticking "cull by
  distance" on an otherwise default effect type still will not remove a running
  effect as the player walks away. The update frequency must be raised *and* a
  significance handler assigned.

A third structural fact: the cull reaction **cannot be overridden per effect**,
so one-shot and looping effects must be separate effect types. That is why there
are four rather than one.

### What this does not change

**The attack warning marker is not becoming a particle system.** It is four
meshes with an unlit material and a measured contrast guarantee pinned to the
design document by two test files. Niagara would buy nothing and put that
guarantee at risk. The sweep section XIII promises is a material parameter driven
by elapsed time, not a particle system — issue #544.

**The rule: Niagara is for effects, not for readability guarantees.** Anything the
player must see to survive is geometry and a material with a measured contrast
figure.

### How it was researched, and what verification caught

Four independent research passes, every finding adversarially checked before use,
three refuted and dropped. The checking earned its place again: it caught "Epic's
only official asset-naming document" when a second official page exists, and a
claim that several prefixes came from one table when they come from three tables
on the same page.

**One section is explicitly marked unverified.** The research did not cover
Niagara's reuse features, so the section on module scripts and emitter
inheritance says so rather than presenting general knowledge as sourced.

### What is left open

The eight effect hues are not chosen — the design document fixes eight
*environment* themes but no effect palette, and that decision comes before
anything can be built. Emitter inheritance behaviour in 5.8 is unverified. There
is no performance budget and no profiling method, and the 8 GB graphics card is
the binding constraint on this machine.

### Sources

Named in full in `Niagara_Conventions.md`. The engine defaults were read from
`Engine/Plugins/FX/Niagara/` in the local install, not from documentation.

---

## 2026-08-13 — The telegraph is red on three rings with a see-through middle, and Chaos is achromatic motion

**Affects:** section XIII of `Cataclysm_GDD_v2.md`, applied, and
`CataclysmTelegraphMarker.cpp`, built. Issue #19 for the design, #539 for the
implementation.

### The colour changed from cyan to red

**Settled by the project owner by looking at it in the sandbox**, which is what
the live console override was added for the day before. Their words: "I like the
orange one better personally… blue just doesn't feel threatening."

The measured objection to red was raised once and is real: `FF3020` on the
previous two-band arrangement reaches only **2.47:1** at worst, below the 3:1
threshold, and its weakest environment is Demonic lava, which is the one this
project has any art direction for.

**It was resolved by changing the structure rather than the colour.** Adding a
warm near-white line between the red ring and the fill takes the worst case to
**3.92:1** — better than the cyan arrangement it replaced, which was 3.22:1. The
project owner gets the colour they judged by eye and the marker is more readable
than it was.

| Band | Width | Colour | Which environments it carries |
| :-- | :-: | :-- | :-- |
| Outer ring | 6 cm | `#0A0F12` | Celestial, Chaos |
| Bright ring | 8 cm | `#FF3020` | Death, Void, Demonic rock, Famine |
| Inner line | 4 cm | `#FFD9CF` | War steel, Demonic lava |
| Fill | — | `#FF3020` at 35% | none — it only tints |

### The fill is see-through, and only the fill

Reported as "it's really solid". **A translucent band cannot carry a contrast
guarantee**: its contrast against the ground beneath it falls toward 1:1 as it
fades, reaching 1.54:1 at 25% over War's steel grey. So the three rings stay
fully opaque and carry it, and the fill is free to be light because it carries
nothing.

**Shipped practice agrees, and treats the opposite as a bug.** Diablo IV's
general manager Rod Fergusson said of the Necromancer's Blighted Corpse
Explosion: "We know that because of the opaqueness of that effect, sometimes
there can be an explosion waiting for you underneath there and you can die."
Blizzard shipped an opacity reduction around patch 1.0.3. WildStar composed
telegraphs from a border, a background and a fill with separate alpha values,
which is the same split adopted here.

**One caveat worth keeping.** WildStar's layering did not survive density — its
10v10 arenas are remembered as an unintelligible wash. Alpha layers alone do not
solve overlap, and this design commits to twenty Imps at once. That is not
settled here.

### Chaos is achromatic motion at constant brightness

The project owner's first description was "black and white flashing", then
corrected: "I think you're thinking too literally when I said flashing… more like
black/white streaking".

**The buildable version: light moves across the surface and the amount of light
on screen does not change.** Chaos is the only damage type with no hue, and that
absence is what identifies it.

**The randomness lives in the per-cast roll, not the per-frame animation.** One
instability value per instance drives movement speed, ramp contrast, displacement
and streak density. One instance looks stable; two look different. The player
reads the roll before the damage lands, which suits Chaos having the widest
damage range in the game. Risk of Rain 2's Void Fiend is the shipped precedent —
a Corruption gauge whose value is continuously readable as shake amplitude.

**The streaking is made by moving textures against each other rather than
switching between states**: two greyscale samples multiplied at mismatched tiling
rates and scroll speeds, read through a gradient driven by elapsed time. That is
Destiny 2's method, described by Senior Visual Effects Artist Mike Stavrides.
Nothing pops, because nothing steps.

### The safety rule that is not about flashing

Three rules follow from the published flash thresholds: never a full black-to-
white swap, keep any one point alternating below twice a second, and displace
geometry rather than spawning and removing it.

**The fourth is the one that would have been missed.** Xbox's accessibility
guidance fails alternating high-contrast bands with **no flashing at all**, when
the contrast difference exceeds 10% and the pattern covers about a fifth of the
screen. **"Black and white streaking across a floor" is literally that pattern.**
So Chaos streaks must be irregular in spacing and direction and must never settle
into parallel bands.

The epilepsy-safe mode therefore clamps Chaos rather than disabling it.

### How this was researched

Three independent research passes, each finding adversarially verified before
being used. **The verification mattered**: it caught a claim that Bungie "does not
use flipbooks" when the source says "currently don't" and a later Bungie source
says "very rare", and it caught a time-driven scroll being described as
"unpredictable" when it is deterministic and periodic. Both corrections are
reflected above.

### Sources

- Destiny 2 visual effects method, Mike Stavrides: <https://80.lv/articles/destiny-2-vfx-production-tips>
- Diablo IV ground effect opacity: <https://www.pcgamesn.com/diablo-4/ground-effects>
- WildStar telegraph composition: <https://www.greenyneko.com/2019/07/wildstars-legacy-telegraph-based-combat.html>
- Xbox Accessibility Guideline 118, including the no-flashing band failure: <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/118>
- Flash thresholds: <https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html>
- Risk of Rain 2 Void Fiend Corruption gauge: <https://riskofrain2.wiki.gg/wiki/Void_Fiend>

---

## 2026-08-12 — Each Cataclysm has a colour theme, and the environment follows it; telegraphs do not

**Affects:** section XIII of `Cataclysm_GDD_v2.md`, applied. Issue #19, partly —
it does not close it. Themes given by the project owner.

### What was decided

| Cataclysm | Theme |
| :-- | :-- |
| Demonic | Red, fire, lava |
| War | Grey, steel, pride |
| Death | Black, shadows |
| Famine | Brown, dying |
| Void | Black and purple, nothingness |
| Celestial | Gold and white, holy |
| Pestilence | Putrid green and brown, rot |
| Chaos | Black and white, random |

**The environment follows the theme directly.** A Demonic dungeon is lava, fire
and smoke. So a Cataclysm's art direction falls out of its theme rather than
being designed separately, which removes most of what issue #34 was asking for on
the environment side.

### The part that does not fall out naturally

**A telegraph is not coloured by its Cataclysm's theme**, and cannot be. Three
reasons the design produces directly:

- **A Demonic environment is already full of glowing orange.** Lava and fire
  occupy the same brightness and hue a warm telegraph would use, so a
  theme-tinted telegraph competes with the floor.
- **Death, Void and Chaos are all built on black.** A dark telegraph in a dark
  room is not a telegraph.
- **Some themes sit next to each other.** Death and Void are both darkness;
  Famine and Pestilence share brown.

**So what makes a telegraph visible is its shape and its edge, not its colour.**
A hard-edged circle, cone, line or ring with a fill that sweeps as the wind-up
runs out. Nothing in an environment has that shape or that motion, which is what
lets it read against lava, against shadow, and against twenty enemies at once.
Colour then says which damage type a telegraph belongs to; it is not what makes
it visible.

Telegraphs are drawn with an **unlit, emissive** material so their brightness is
fixed by their own material rather than by the room's lighting. Without that no
brightness rule can hold, because the value would be whatever the lighting
produced. Issue #539 covers making that true — today
`CataclysmTelegraphMarker.cpp` assigns no material at all.

### How this got here, which is worth recording

The first attempt measured contrast ratios between flat colour swatches. The
project owner rejected the framing: a floor is a texture under dynamic lighting,
not a flat fill, so swatch contrast measures something that will not exist.

**That correction is what produced the shape rule.** Once world brightness is
understood as a lighting result rather than a colour choice, the only way to fix
a telegraph's visibility is to stop it being lit — and once it is not lit, what
distinguishes it from the world is its geometry rather than its value.

### The telegraph is one colour for the whole game

**Settled by the project owner the same day**, after the eight themes were
recorded: there is no per-damage-type telegraph colour. Their reasoning is that
eight would be tedious to author and unnecessary, because the creature's own art
and its Cataclysm's environment already say what is attacking. The warning shape
only has to say where and when.

| Part | Value |
| :-- | :-- |
| Fill | `#00B8C4`, a saturated cyan |
| Outline | `#0A0F12`, near-black, on both sides of the fill |

**Two tones rather than one, because no single colour survives both extremes.**
Death and Void are built on black and Celestial is gold and white. A colour
bright enough for the first disappears into the second. Measured against the
extreme of each theme, the cyan fill reaches 8.16:1 on Death's black but only
1.91:1 on Celestial; the near-black outline is the reverse, 1.03:1 on Death and
15.20:1 on Celestial. Taking the better of the two per environment, **the worst
case across all eight themes is 3.22:1, against War's steel grey** — above the
3:1 accessibility threshold for a graphical object that is not text.

**Cyan because no Cataclysm uses it.** The themes occupy red, grey, black, brown,
purple, gold, white and green. Cyan is the hue left over, and it is the opposite
of orange, which matters because Demonic lava is the environment most likely to
swallow a warning.

**The genre does not settle this one.** A search for telegraph colour conventions
returned material about damage number colours instead, and what it did show is
that conventions vary between games. Shipped action role-playing games generally
use red or orange for danger, and that cannot be borrowed here because this
game's own fire Cataclysm already owns those colours. **This is a judgement from
the constraints, not a shape read off another game**, and it is labelled as one in
section XIII.

### Two things left open

- **Chaos is described as black and white flashing, and section XIII commits to
  an epilepsy-safe mode that reduces flashing effects.** A Cataclysm whose
  identity is flashing is the one case that mode exists to suppress. Either
  Chaos expresses randomness another way, or the mode states what it does to a
  Chaos dungeon specifically.
- **How Death and Void environments are told apart**, and Famine and Pestilence.
  This is now an environment and enemy question only; it no longer affects
  telegraphs.

---

## 2026-08-12 — The empire's cities are Outposts, Bulwarks, Sanctuaries and the Pillar

**Affects:** section II and section VIII of `Cataclysm_GDD_v2.md`,
`Empire_Skill_Tree_Keystones.md` and `Empire_Development_Tree_Final.json`, all
applied. Issue #534. Decided by the project owner.

### What was decided

**The simulation's names win.** `Cataclysm_GDD_v2.md` was out of date.

| Ring | Canonical name | Was called | Count |
| :-: | :-- | :-- | --: |
| 0 | **Pillar** | Capital | 1 |
| 1 | **Sanctuary** | Metropolis | 4 |
| 2 | **Bulwark** | City | 8 |
| 3 | **Outpost** | Village | 12 |

**The mapping was never actually in doubt.** `sim/cataclysm_sim/world.py` has
carried it in its module docstring the whole time, and `config.py` says outright
that "GDD v0.3 still calls these Village / City / Metropolis / Capital … and the
newer names win". Nobody had applied it to the design document.

**The counts were never in dispute either.** Both documents already said
1/4/8/12. The simulation explains why: the map is a taxicab ball of radius 3, and
ring N holds exactly 4N cells, so the counts are a property of the geometry rather
than a separate design decision. That explanation is now in the design document.

### Two problems this fixes that were not merely cosmetic

**The design document's own table contradicted itself.** It counted villages and
metropolises as "cities" while "City" was also one of the four tier names. "City"
is now the general word for a place on the map at any tier, and it is not a tier
name — which is how the simulation has always used it.

**Two passive tree nodes granted a bonus that could not be computed.** Beacon of
Hope and Fortified Pillar both read "cities within 2 hexes". **The lattice has no
hexes.** It is a taxicab ball with orthogonal adjacency, so a distance in hexes
has no meaning on it, and whoever implemented the empire tree would have had to
guess or stop.

The translation is exact rather than a judgement: every orthogonal step changes
the ring by one, so a city's ring *is* its distance from the Pillar. **"Within 2
rings of the Pillar" is the four Sanctuaries and the eight Bulwarks, twelve
cities** — the set the author meant.

### One word kept, with a narrower job

**"The capital" now means the hub inside the Pillar, not another name for it.**
The Pillar is the city at ring 0, with defence and population like any other city.
The capital is the settlement the player walks around between runs, holding the
NPC services in section IX. Losing the Pillar ends the run and takes the capital
with it.

That distinction is stated in the City Tiers section rather than left to be
inferred. **If the intent was that "capital" should disappear entirely, that is a
one-line change** — but two words naming two different things is not the problem
this issue was about.

The node "Venture Capital" is untouched. It is about gold and magic find, not
about a city.

### Held by a test

`tools/tests/test_empire_vocabulary_is_one_set_of_words.py` reads the four
canonical names from `sim/cataclysm_sim/config.py` rather than restating them, so
this file cannot become one more place that drifts. It fails if any design
document uses a superseded tier name, if anything measures empire distance in
hexes, or if a passive tree node does either. All three were proven to fail with
`tools/prove_guard.py`.

---

## 2026-08-12 — Animation is shared by weapon and slot, not by damage type

**Affects:** adds `Animation_Plan.md` to this folder. Nothing in
`Cataclysm_GDD_v2.md` changes. Issue #18. Confirmed by the project owner.

### What was decided

**One animation set per weapon-and-slot combination, shared across all eight
damage types.** There are 15 weapon types and 6 slots, giving **71 combinations**,
measured from the Weapon Skills sheet. Damage type identity comes from effects,
audio, meshes, impacts and mechanical behaviour rather than from the body motion.
A reserve of roughly 24 signature animations, about three weapons per damage type,
is held back for the moments identity matters most — most likely Ultimates.

**Roughly 135 to 150 animations for the full game**, against 398 if every skill
row were bespoke.

### Why it costs nothing the design had not already spent

The condition attached to approving re-use on 2026-08-02 was that classes must not
feel like the same thing in a different colour.

**Classes were never differentiated by their skills.** Skills come from weapon
type plus damage type; classes come from damage type, three per type. The Ravager,
the Ritualist and the Masochist are all Demonic and all draw from the same Demonic
skill pool, so sharing an animation between them costs nothing they did not
already share. What separates them is the passive tree and the class resource —
the Masochist spends health and its resource is Anguish — and none of that lives
in an attack animation.

**So the risk the condition names is about the eight damage types, not the 24
classes.**

### The check that should happen before any tooling is bought

War and Demonic are both fully designed now, 61 of 61 and 51 of 51. The rule
claims a Demonic Greataxe Heavy attack and a War Greataxe Heavy attack are the
same physical motion, and **both of those skills already exist as written
descriptions**. So the rule can be falsified by reading 112 skill descriptions and
asking whether each matched pair implies the same body motion. No tooling, no art,
no spend.

### Still open

The player skeleton standard, recommended as the Unreal mannequin because this
project is already committed to retargeting — every Paragon character carries its
own skeleton, 39 to 207 bones. And the animation tooling, which should wait for
the reading check.

### Two figures in issue #18 were wrong

It says 558 skill rows and a 7.9 times reduction; the sheet holds **398**, so the
reduction is **5.61**. The 71 combinations the rule rests on are correct. And its
closing note says Phase 1 proves the rule with War skills — Phase 1 uses
**Demonic** skills, corrected by issue #61.

---

## 2026-08-12 — The game is bought once, not free to play

**Affects:** section XIV of `Cataclysm_GDD_v2.md`, rewritten and applied. Confirms
rather than changes the stash partition reasoning further down this log. Issue
#501. Decided by the project owner.

### What was decided

**Cataclysm is bought once, at $25 to $30, sold from Early Access.** No free
client, no subscription, no trial. Buying it buys all of it.

**Seasons are free content patches added permanently to the game, and they work
offline.** An online ladder runs alongside for online characters and is optional.

**Cosmetics are a supplement, not the funding model.** The game is paid for when
it is bought and again at each expansion. Expansions stay as they were, $10 to
$20 every 6 to 12 months.

### The contradiction this resolves

Two documents described two different products. Section XIV committed to a free
client, seasonal leagues and a cosmetics shop as the only revenue outside
expansions — the Path of Exile model. This log, in the reasoning for partitioning
the stash three ways, called the game "a single-player-first design with co-op
listed as a Phase 2 feature, so the market was never going to be deep".

**The real disagreement was about the size of the player base each one assumed**,
and each used its assumption to justify something. Section XIV assumed a
population large enough that a cosmetics shop pays for continuous development. The
stash decision assumed one small enough that an auction house was never going to
be liquid. Both could not be planned for.

**A second contradiction sat inside the design document alone**, and it was the
more urgent one: section XIV said all revenue outside expansions comes from
cosmetics, and section XV puts the full cosmetic system in Phase 3 while Phase 2
is the Early Access launch. So the plan as written took no money at Early Access
— it shipped the free client, the ladder, the shared stash and the auction house
first and built the only thing that charges anybody last.

### Why bought rather than free

**Free-to-play with a cosmetics shop is not a model that scales down.** It
converts a small percentage of a very large audience, so it needs the audience
first. Path of Exile reaches roughly 100,000 concurrent players at a league
launch, peaking at 229,000, on thirteen years of league cadence and a studio owned
by Tencent; it earned about $105 million in the 2024 financial year. A cosmetics
shop attached to a small population earns approximately nothing — and this design
had already ruled out the two other things such games sell, stash space and
anything affecting gameplay.

**Last Epoch is the closer comparable and it is bought once.** $35 on Steam, three
million copies since its 2024 launch, about $12 million in its first three days,
and the studio was acquired by Krafton for $96 million initial consideration in
July 2025. Its ongoing development is funded by cosmetics on top of the purchase
price, which is exactly the structure adopted here.

**Grim Dawn shows the model works at a much smaller scale**: an indie studio under
thirty people, sustained across a decade on buy-to-play plus paid expansions, with
an offline-focused single-player design. Its final expansion shipped 2026-07-23.

Three further reasons, in order of weight:

1. **It matches the architecture already chosen.** The 2026-08-10 decision on
   offline play adopted Last Epoch's offline and online split by name. Adopting
   its revenue model too makes the product coherent instead of half of one game
   and half of another.
2. **It takes revenue at Phase 2 instead of Phase 3.** The Early Access launch is
   already on the roadmap, which removes the phase during which the previous plan
   earned nothing.
3. **It does not require a population that does not exist yet.**

### The price

$25 to $30 against Last Epoch's $35, and below it deliberately: this has less
content at Early Access than Last Epoch had at 1.0.

### The risk being accepted

**A price is a barrier that free-to-play does not have**, so the audience is
smaller and grows more slowly. The seasonal ladder and the shared table of
corrupted characters are worth less with fewer online players. **If the empire
layer turns out to be a draw that would have pulled six figures of concurrent
players, this leaves money on the table.** That is the accepted risk, stated
rather than discovered later.

### Sources

- [Last Epoch statistics, LEVVVEL](https://levvvel.com/last-epoch-statistics/)
- [Krafton acquires Eleventh Hour Games, Variety, July 2025](https://variety.com/2025/gaming/news/krafton-acquires-last-epoch-developer-eleventh-hour-games-1236470366)
- [Last Epoch on Steam](https://store.steampowered.com/app/899770/Last_Epoch/)
- [The mechanics and ethics of free-to-play in Path of Exile, Game Developer](https://www.gamedeveloper.com/business/the-mechanics-and-ethics-of-free-to-play-in-i-path-of-exile-i-)
- [Path of Exile supporter packs, PoE Wiki](https://www.poewiki.net/wiki/Supporter_pack)
- [Grim Dawn's final expansion, Massively Overpowered, 2026-07-23](https://massivelyop.com/2026/07/23/oarpg-grim-dawns-biggest-and-final-expansion-fangs-of-asterkarn-is-live-today-as-crate-eyes-new-projects/)
- [Grim Dawn, Wikipedia](https://en.wikipedia.org/wiki/Grim_Dawn)

### What this unblocks

#31 co-op design, #56 co-op implementation, #57 the auction house, #58 seasonal
league infrastructure, #59 cosmetics, and #179 the shared table of corrupted
characters. All six were waiting on which product this is.

---

## 2026-08-12 — Audio is part of the readability system, and it is built on MetaSounds

**Affects:** adds an Audio subsection to section XIII of `Cataclysm_GDD_v2.md`,
applied, and adds `Audio_Design_Plan.md` to this folder. Issue #33. Nothing is
implemented: `game/Source/` has no audio code and `game/Content/` no authored
sound.

### Why audio is load-bearing here rather than decoration

Two facts in this design make that literal, and both are read out of the design
document rather than argued from taste.

**The art direction fights the combat design.** The world is deliberately dark and
low-light, and combat requires reading and dodging telegraphed attacks. An attack
that is off-screen, behind the character, or lost among twenty Imps is not
readable by sight.

**Two of the three lethality modes hide the interface.** Hardcore shows the map
overlay only; Heretic hides the heads-up display entirely. **A Heretic player has
no health bar, so the low-health cue is the only warning they get that they are
about to die.** That is the mode working as designed, and it only works if the
audio exists. The reduced ability effect opacity option has the same consequence
for anyone who turns it on.

This is why the priority order puts the telegraph first and low health second,
and why the order lives in the bus structure rather than in per-sound volume
values, which drift.

### What was decided

**An audio telegraph carries the same information as its visual telegraph, in the
same window, and never more.** It follows the section X wind-up cap of half the
attack interval, and it is positional so direction and distance are audible —
which is what makes the off-screen case work. A cue that announces something the
marker does not makes the visual channel the unreliable half.

**Damage-type identity lives in timbre, not pitch or volume.** Those two are
already spoken for: pitch carries enemy size and volume carries distance. A damage
type that announced itself by being louder would be indistinguishable from one
that is closer.

**Enemy vocalisations only. No dialogue and no narration**, because the design has
no dialogue system for voice to attach to. This also keeps the multiple-language
commitment in section XIII cheap, since a game with no spoken dialogue localises
as text alone.

**MetaSounds, and no middleware.** Three reasons: there is no audio budget line
and #501 has not settled whether the game is even sold, so a per-title licence
tied to budget or revenue is a commitment that cannot be made yet; Audio Insights
became production ready in Unreal 5.8, which is the version already committed and
was the tooling gap that historically argued for middleware; and the case that
dominates everything is twenty Imps attacking at once, where twenty copies of one
sample is a machine-gun artefact and procedural variation is what MetaSounds is
best at. The cost is engineering time and no authoring application for a
non-programmer, and that cost is real.

### A dependency worth knowing before anyone starts

**Audio telegraph authoring is blocked on #526.** A telegraph cue is fired by an
animation notify rather than a timer, because enemy attack animations are
retimed constantly — each is played to fit its designed attack interval — and a
timer would drift silently the moment a play rate changed. But nothing has
measured where inside a clip the damage lands, for any of the seven enemies. The
window cannot be filled before it is measured, so the blocker is the measurement
and not the sound design.

The low-health cue and hit confirmation need neither animation work nor
measurement, which is why the build order starts there.

### Sources

- Middleware comparison for Unreal 5 in 2026:
  [StraySpark](https://www.strayspark.studio/blog/wwise-fmod-metasounds-audio-middleware-comparison),
  [Aircada on Wwise](https://aircada.com/blog/metasounds-vs-wwise),
  [Aircada on FMOD](https://aircada.com/blog/metasounds-vs-fmod)
- Audio telegraphing and readability:
  [Designing for Difficulty: Readability in ARPGs, Game Developer](https://www.gamedeveloper.com/game-platforms/designing-for-difficulty-readability-in-arpgs),
  [Sound design for Wolcen, Oliver Smith](https://www.oliversmithsound.com/blog/sound-design-for-an-arpg-wolcen-lords-of-mayhem)
- Positional audio for off-screen threats:
  [Xbox Accessibility Guideline 103](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/103)

---

## 2026-08-12 — The Forge's residue penalty is one global rule, and the Crafting sheet is three tables

**Affects:** section VII of `Cataclysm_GDD_v2.md`, applied. Issue #28. No design
changed — this reconciles a document that was thinner than the data behind it.

### The sheet is not what it looks like

`docs/All_Things_Cataclysm.xlsx`, the Crafting sheet, reads as 37 materials. It
is **three tables stacked in one sheet**:

- Rows 1 to 18, the **materials**, with tier, source and use.
- Rows 5 to 11, in columns 7 to 9 only, a six-row table of the **global residue
  penalty scale**. Those columns have nothing to do with the material on the same
  row; the inner table's own header sits on the Dismantling Dust row.
- Row 19, a header row whose first cell is the literal word "Action".
- Rows 20 to 37, the eighteen **Forge operations**, each with its material, the
  residue it adds and its base days.

**Issue #28 read it at face value and drew the wrong conclusion**, which is worth
recording because anyone else will: it suspected that materials each carry their
own residue formula, because `(CR / 50) + 1` and `CR / 100` appear on the
Aetherial Shard and Chaos Stabilizer rows.

### What was decided

**They are the global rule.** Both formulas reproduce every row of the inner
scale table exactly:

| Item's CR | `(CR/50)+1` | Sheet says | `CR//100` | Sheet says |
| --: | --: | --: | --: | --: |
| 0 | 1.00 | 1x | 0 | +0 days |
| 50 | 2.00 | 2x | 0 | +0 days |
| 99 | 2.98 | 3x | 0 | +0 days |
| 100 | 3.00 | 3x | 1 | +1 day |
| 200 | 5.00 | 5x | 2 | +2 days |
| 500 | 11.00 | 11x | 5 | +5 days |

Six rows out of six, on both formulas. A material does not change the penalty; it
chooses which operation is available and how much residue that operation adds.

**Section VII now carries all eighteen operations and all eighteen materials**,
with the two formulas stated beside the scale so the scale is derivable rather
than a copy. Copies drift, and this drift is what #28 found.

`tools/tests/test_crafting_section_matches_the_sheet.py` holds it, in both
directions: an operation in the sheet and not the document fails, and a material
named in the document that the sheet does not have fails. Both were proven to
fail by breaking them with `tools/prove_guard.py`.

### What this leaves open

**Whether the gold multiplier is rounded in the calculation or only for
display.** The 99 row is 2.98 shown as 3x and every other row is exact, so the
scale does not disambiguate it. It matters below 100 CR: at 10 CR the multiplier
is either 1.2 or 2. Stated as unsettled in section VII rather than guessed at.

**Seven of the eighteen materials have no stated source.** Where they drop is not
designed, and the loot tables that would answer it do not exist. Filed as issue
#531, blocked on #44.

**Affects:** adds `Save_System_Design.md` to this folder. Nothing in
`Cataclysm_GDD_v2.md` changes. Issue #21. Nothing is implemented yet — a search of
`game/Source/` for `USaveGame` returns nothing.

### What was decided

**One save file was never possible.** The game persists three things with three
different lifetimes, and in co-operative play with three different owners:

| Record | Lifetime | Owner in co-op |
| :-- | :-- | :-- |
| Account | Permanent | Each player their own |
| Character | Permanent, survives a failed run | Each player their own |
| Run | Discarded when the run ends | **Shared by the party** |

**The co-op split is not a Phase 2 addition, it is this boundary.** The design
already says the empire is shared in co-op and builds are individual — section
XVI's risk table says so directly, and the co-operative play section charges the
death penalty "once against the shared empire clock". So the run record is the
shared thing and the character record is the individual thing, in solo play as
well. A four-player session is one run record with four character records
attached, not a different shape.

That is why this was worth designing before Phase 2. Splitting a single save file
into shared and per-player halves later is the retrofit issue #21 warned about;
starting split costs nothing.

**Partitioning enforces a rule the design already made.** Each lethality mode has
its own empire tree, its own stash and its own market, and nothing moves between
them. A Solo Self-Found character has its own tree shared with nothing at all and
no stash. The save layout is the only place that rule can be enforced, so the
partition key is the lethality mode, and a Solo Self-Found character touches no
account record — its tree lives inside its own character record and its file is
self-contained.

**JSON, not the engine's default binary.** `USaveGame` subclasses for the engine
integration and console support, serialised as JSON, with an integer schema
version as the first field of every record.

### Why JSON, when the engine's default is binary

Both ends of this trade are occupied by shipped games in the genre. **Last Epoch
writes JSON** for its offline saves; **Grim Dawn writes a custom binary `.gdc`
format**. Neither is wrong.

JSON is right for this project for a reason specific to it rather than general:
issue #21's real worry is that a format which cannot migrate means discarding
player progress at every patch, and this game will change constantly through
development and Early Access. A migration written against a readable format can
be inspected, diffed and tested by hand. A migration written against binary
cannot, and every bug in one produces a corrupt save rather than an error.

There is a consistency argument too. Decision #505 adopted Last Epoch's offline
and online split by name; its saves are the closest working example of exactly
this design.

**The cost is size and load time**, and the decision is to accept it and measure.
If load time is measured and found to be a problem, binary is the answer — but
measure first rather than switching on principle.

### Migration rules that are not obvious

- **A migration never reads `game/Data/`.** It transforms one schema into the
  next using only what is in the record and constants frozen into the migration
  itself. A migration that reads the current data tables breaks the moment those
  tables change, which is the thing most likely to change.
- **Migrations are a chain of single-step functions**, never a jump between
  distant versions. A chain is testable; a jump is not.
- **A save newer than the build is refused, not guessed at.**
- **The version test uses committed example saves, one per historical schema
  version.** A test that writes a save with the current code and reads it back
  proves only that the code agrees with itself, which is the failure mode this
  criterion exists to prevent.

### What this leaves open

Whether the offline and online populations also have separate stashes and
separate empire trees. **The design document does not say**, and the design
assumes they are separate because a shared stash both can reach is a transfer
route between two populations that decision #505 made deliberately
non-transferable. Filed as issue #528. If it is decided the other way, only the
partition section changes.

### Sources

- Last Epoch offline saves are JSON, in `AppData\LocalLow\Eleventh Hour Games\Last Epoch\Saves`:
  [save location](https://steamcommunity.com/app/899770/discussions/0/4338725580143851622/),
  [save editor thread describing the format](https://fearlessrevolution.com/viewtopic.php?t=17089)
- Grim Dawn uses a custom binary `.gdc` format, in `Documents\My Games\Grim Dawn\save`:
  [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Grim_Dawn)

---

## 2026-08-12 — An enemy at zero health dies; a player's death is still undesigned

**Affects:** nothing in the design documents yet. It builds behaviour the design
assumed and never stated. Issue #517.

**Nothing in the project reacted to health reaching zero.** Damage was dealt and
health did drop — the automation tests measured it — and then nothing happened.
An enemy at zero health kept chasing, kept swinging and could not be removed from
the level. The project owner reported it while playing:

> "None of the actual combat is implemented I don't think. Or at least I can't
> tell when playing it. There's no health bars or damage numbers"

That reading was correct in effect even though the arithmetic ran. **A fight that
cannot end is indistinguishable from a fight that is not happening**, and it also
made every combat figure impossible to judge by play, which is how this project
settles them.

### What an enemy's death is

Three things, in order, when its health first reaches zero:

1. **It is marked dead**, with a new `State.Dead` gameplay tag.
2. **Whatever it was doing stops.** A charge is cancelled, its movement is
   halted, and its collision is turned off so a corpse cannot push the player.
3. **It is destroyed on the next tick.**

**On the next tick rather than immediately**, because the killing blow is
resolved inside a gameplay effect callback and destroying the actor there would
tear down the ability system component still running.

### Why a tag rather than a flag

A stun is a tag, and the two controllers that must refuse to drive a dead
character already ask about state that way. A flag would have needed both of them
to know the creature's own class.

**It is the one state tag here that is not timed.** Every other is granted for a
duration and expires; this one is added loosely and never removed, because the
character carrying it is being taken out of the level. It is also what makes
dying happen once — health can be written at zero repeatedly, by a burn ticking
on a corpse or by two hits in one frame.

### A player's death is deliberately not built

`ACataclysmCharacterBase::HandleDeath` is inert on the base and only the enemy
overrides it, so a player at zero health is exactly as before.

**That is a scope decision, not an oversight.** A player's death owes a death
penalty, a corruption cost and the Last Stand mechanic (#43), none of which is
designed. The enemy half needs none of that and is what unblocks judging every
creature by play.

### What is still missing

**A death animation.** The creature vanishes in one frame. The Paragon packs ship
death clips — `ParagonGrux` has `Death_A` and `Death_B` — and playing one is
per-creature work, because the Abyssal Warden queues clips in C++ with no
animation Blueprint (#387), the Brute uses generated montages, and five of the
seven creatures have no art at all. Issue #522.

**Health bars and damage numbers**, which is issue #518 and the other half of why
combat could not be judged in play. There is no user interface code anywhere in
`game/Source/`.

---

## 2026-08-12 — A skill's own tags say whether it can be evaded

**Affects:** `Cataclysm_GDD_v2.md`, the Avoidance subsection. Applied in the same
change. Issue #513.

Evasion has always been defined as avoiding a direct attack only, with area
damage landing regardless. **Nothing in the game had ever said which attacks were
which**, so every hit arrived as a direct one and an evasive character dodged
explosions centred on itself. The Brute's stomp and the Abyssal Warden's ring
were both affected.

### What decides it

**The skill's own gameplay tag list**, which already carried the answer:

| Tag | On | Meaning | Evadable? |
| :-- | --: | :-- | :-- |
| `Type.AOE.PointBlank` | 33 skills | an explosion centred on the caster or target | no |
| `Type.AOE.Aura` | 4 skills | a radius that moves with the caster | no |
| `Type.AOE.Persistent` | 26 skills | ground effects, clouds, zones | **yes** |
| no area tag | the rest | one blow at one target | yes |

**`Type.AOE.Persistent` describes the ground a skill leaves, not the blow it
lands**, and that is the whole subtlety. Flamedart is tagged `Keyword.Charge,
Type.AOE.Persistent`: the charge makes contact and can be evaded, and the fire
trail damages whatever stands in it afterwards. A zone's own ticks are area
damage, decided where the zone deals them. Matching on the `Type.AOE` parent
instead would look correct and would silently make 26 designed skills
unevadable.

### The shape this was almost built as

The first attempt decided it in C++ at each damage site, from the skill's shape:
every Strike was area damage, every charge was contact, a projectile was direct
in flight and area when it detonated. **That was wrong and the data said so.**
Cinderslash is `Type.Strike, Type.Melee` and nothing else — one sword blow — and
a shape-based rule would have made it, and every other melee skill, impossible to
evade.

The project owner asked whether the gameplay tag system was being used for this,
which is what surfaced it:

> "Just to clarify, you're using the GAS tagging system for this work right?
> Basically, if an ability is tagged with AOE evade doesn't trigger kinda thing?"

**The lesson is narrower than "use tags".** The answer was already authored in
`game/Data/WeaponSkills.csv` for 37 skills, and a rule was being invented in code
beside it. Checking what the data already said should have come first.

### Enemy abilities are the exception, and it is temporary

An enemy ability is C++ constants on the creature class and carries no tag list,
so the Brute's stomp and the Abyssal Warden's ring set the flag directly at the
call site. That is a second route to one answer, which this repository usually
treats as worth removing. **Issue #519 is that removal**, requested by the
project owner in the same exchange: "enemy skills should carry tags as well".

### One more property travels the same way

**Whether a hit is damage over time**, which decides whether an energy shield
absorbs it. A shield that soaked burn would be a second health bar rather than a
distinct defence, and damage over time is the design's stated answer to shield
stacking. It is NOT read from a skill's tags: a skill tagged `Keyword.DoT.Burn`
applies a burn, and its own direct hit is still a direct hit. It is set by the
two paths that genuinely deal damage over time — the periodic effect and the
burning ground zone.

### What is still not carried

**Armour penetration and the weapon sub-type**, which is issue #520. Armour
penetration has no attribute anywhere in the project to read, even though three
enchantments and the piercing weapon sub-type all grant it, and enemy armour is
now the largest mitigation layer on the most armoured creatures. The weapon
sub-type needs the equipped weapon's row joined to the hit, and its four values
land in four different places.

---

## 2026-08-12 — Enemies resist generically; only enemy damage carries a type

**Affects:** `Cataclysm_GDD_v2.md` section X, the enemy resistance subsection.
Applied in the same change. Issue #486.

**The ruling, from the project owner:**

> "for this issue, enemies will have a generic all res. The only damage that
> should actually be typed is enemy damage so the player's resistances can take
> effect."

### What was wrong

Every hit in the running game resolved as an untyped hit.
`FCataclysmIncomingHit` was built with only its damage assigned, so its damage
type stayed empty, the resistance lookup selected none of the defender's eight
resistances and returned zero, and step four of the eight-step mitigation order
multiplied by one. **No resistance on either side of a fight did anything.**

Three separate pieces of design were decoration in the editor because of it:

- **The Abyssal Warden's 35%**, which is the entire mechanical content of the
  phrase "high damage resistance" in its description.
- **The player's eight resistances**, which exist because eight Cataclysms
  attack them.
- **Resistance penetration**, a whole player stat with affixes behind it,
  reducing a number that was already zero.

### What the ruling settles

The two sides of a fight need different things from a damage type, and only one
of them needs one at all:

| | How it resists | How its damage is typed |
| :-- | :-- | :-- |
| Enemy | one generic figure, met by a hit of any type | as its Cataclysm's damage type |
| Player | eight figures, one per damage type | not typed at all |

**A player's hit stays untyped and that is the point, not a gap.** An enemy
resists everything equally, so naming a type on a player's hit would be choosing
between eight copies of one number.

### The one thing that had to be built rather than only wired

**An enemy stops holding the eight typed resistances and holds a single
all-damage one instead.** It used to write its one figure into all eight typed
slots, which is the same thing as an all-damage resistance *only as long as every
hit names a type*. Player damage deliberately names none, so the lookup had no
slot to pick and skipped all eight — the creature resisted nothing.

**It is a separate attribute set, not a ninth attribute on the existing one.**
The first attempt was the ninth attribute and the project owner rejected it:

> "noooo not a ninth resistance. Either remove all of the 8 resistance types on
> enemies and give them an all res, or make all 8 values the same. The first is
> probably the better option."

A separate set is the only shape that expresses "an enemy does not have the
eight". An attribute set is all-or-nothing — a character that registers one gets
every attribute in it — so a ninth attribute would have given every player an
all-damage resistance no player can have, and left every enemy holding the eight
typed resistances it must not have. The character sheet is 45 stats either way,
and with two sets it needs no exception written into the count.

### What this does not touch

**Four fields of an incoming hit are still never populated**, and that is issue
#513. Armour penetration, whether the hit is area damage, whether it is damage
over time, and its weapon sub-type. **The live consequence is that an area attack
can still be evaded**, which this document says it cannot: the Brute's stomp and
the Abyssal Warden's ring are both dodged outright by an evasive character today.
#486 said to split itself if it turned out to be two jobs, and it did.

**Every hit still resolves at difficulty tier 1**, and that is issue #514. Armour
is `armor / (armor + 800 x tier)`, so the Abyssal Warden's 5,954 hits the 75% cap
at tier 1 where it removes 48.19% at tier 8. Every armoured creature in the
running game is 2.07 times harder to hurt on the armour layer than its design
says. There is no difficulty tier anywhere in `game/Source/` to read instead,
which is why it is filed rather than fixed here.

---

## 2026-08-12 — Every defensive layer an enemy has now reaches the arithmetic

**Affects:** `Cataclysm_GDD_v2.md` section X, the paragraph on enemy resistance
and penetration. Applied in the same change. Issue #481.

An enemy's armour, evasion and energy shield were computed by
`sim/cataclysm_sim/enemy_stats.py`, exported to `game/Data/EnemyArchetypes.csv`,
written onto engine attributes, checked for sanity by the tests, and **applied to
no arithmetic anywhere**. Nothing in `sim/` ever built a `damage.Defender` from an
`EnemyStats`, so `player_damage_to_kill_in` — the function that says what damage
gear has to produce — applied resistance and nothing else.

Every "damage needed to kill" figure this project has derived was therefore too
low, in proportion to how armoured the creature is:

| Enemy, tier 8, last floor of 50 | Armour | Evasion | Was | Is | |
| :-- | --: | --: | --: | --: | --: |
| Common Imp | 0 | 25% | 39 | 52 | 1.33x |
| Common Hellhound | 202 | 20% | 94 | 121 | 1.29x |
| Elite Succubus | 183 | 10% | 209 | 239 | 1.14x |
| Elite Brute | 2,751 | 0% | 542 | 775 | 1.43x |
| Legendary Corrupted Sentinel | 2,748 | 0% | 858 | 1,226 | 1.43x |
| Herald Abyssal Warden | 5,954 | 0% | 3,929 | 7,584 | 1.93x |
| Cataclysm Boss Gatekeeper | 8,224 | 0% | 18,925 | 43,243 | 2.28x |

Damage per swing to kill in 30 swings. **The Gatekeeper's real figure is more
than twice what the model reported.**

### No new formula was invented here

The mitigation order — evasion, block, armour, resistance, flat reduction, mana,
energy shield, health — was settled under issue #93 and is what
`sim/cataclysm_sim/damage.py` and `UCataclysmDamageCalculation::ResolveHit`
already run. This change routes the enemy side into that existing order rather
than proposing a second one. The armour curve, `armor / (armor + 800 x tier)`
capped at 75%, is likewise unchanged.

### What was genuinely a judgement

**Evasion is counted, as its expectation.** Evasion is a per-hit avoidance roll
rather than a proportional reduction, so folding it into a single "share of a hit
that gets through" figure means averaging over the roll. Three reasons it is in
rather than out:

- The figure answers "damage per swing needed to kill in so many swings", and a
  player counts swings. An Imp that avoids a quarter of them genuinely needs a
  third more damage per swing to die on schedule.
- `damage.average_damage_taken` already averages over the same roll on the
  player's side. Two conventions for one question would be worse than either.
- Evasion is the Imp's and the Hellhound's designed defence, at 25% and 20%.
  Leaving it out makes swarm fodder that dodges identical to swarm fodder that
  does not.

Area damage still ignores it, which is the design's own rule, so the figure takes
a flag saying whether the hit can be evaded.

**The difficulty tier is carried on the stat block, and has no default.** Armour
is worth `armor / (armor + 800 x tier)`, so the same armour figure means very
different things at different tiers: the Abyssal Warden's 5,954 hits the 75% cap
at tier 1 and removes 48.19% at tier 8. Asking the caller to supply a tier at the
moment of use would let a block built at tier 8 be judged at tier 1 with nothing
to notice, and would be wrong by more than a factor of two. There is now one tier
per stat block and it cannot disagree with itself.

**Penetration is clamped at the enemy's own resistance inside `enemy_stats.py`,
and #482 is still open.** `damage.effective_resistance` lets penetration overshoot
into negative resistance, which turns over-stacked penetration into a damage
multiplier — the thing section X of the design document forbids. `enemy_stats.py`
has always clamped, so routing it through `damage.resolve` without a local clamp
would have imported the defect rather than fixed it. The clamp is local on
purpose: the shared fix belongs in `damage.py` under #482, and it is a design
question with three answers rather than a one-line correction.

### What this does not change

**Enemy damage to the player is untouched.** `DAMAGE_AT_COMMON` at 0.65 and
`DAMAGE_PER_STEP` at 1.40 are fitted against the reference geared character in
`reference_build.py` and are about how hard an enemy hits, not how hard it is to
hit. `sim/tests/test_survivability.py` still measures them unchanged.

**The player's own damage target is still wrong, and is issue #511.**
`damage_target` in `sim/cataclysm_sim/affixes.py` divides the average Common
enemy's health by how many hits it should take and applies no mitigation at all,
so it is 10.5% low. It was left alone deliberately: it anchors every offensive
number in the affix pool, and moving it means re-checking the flat and increased
damage constants fitted to it.

---

## 2026-08-12 — The Abyssal Warden was judged by play and accepted

**Affects:** nothing. This entry records a judgement so that it is not asked
again. No number changes.

The project owner played the Abyssal Warden after four changes landed and said:

> "the abyssal warden looks good for now"

That covers all four, and each is now settled unless play says otherwise:

| Change | What it did |
|---|---|
| #491, #498 | Gave it a charge, so a creature that moves at 2.8 m/s against player classes at 3.5, 4.0 and 4.6 can close the gap at all. Before this it could be walked away from and never fought. |
| #499, #503 | Stopped the monster brain steering the creature mid-charge. It no longer turns to face the player as it passes. |
| #487, #496, #502 | Grew its ring from 5.6 to 6.5 metres, the largest the telegraph rules allow, cutting the slowest class's spare time from 0.657 s to 0.400 s. |

**The charge speed rule is accepted**, stated separately because it was the one
figure no shipped game could settle: a charge covers its designed range in the
length of its own animation clip, which is 11.43 m/s here. Judged on 2026-08-09
as "the speed is pretty good".

**"For now" is doing work in that sentence and the reserve lever is recorded so
it is not lost.** The ring's geometry is exhausted — 6.5 m is the cap, so it
cannot be made bigger. If it later reads as too easy, the next lever is what the
attack leaves behind: it currently leaves nothing, and the riders already exist
and are used by the Hellhound's charge. `GroundRadius: 6.5, GroundDuration: 6,
Burn: 1` would deny the melee ground for half of every 12 second cycle. The
project owner chose on 2026-08-09 to ship the size change alone and judge it
first, which is what this entry closes.

---

## 2026-08-10 — "Is a boss" derives from rarity, and rarity lives on the enemy

**Affects:** `ACataclysmEnemyCharacter` and `UCataclysmSkillEffects::ApplyStun`
in `game/Source/Cataclysm/`, and `enemy_stats.py` in `sim/cataclysm_sim/`.
Applied. Closes #395.

### The question

Section VI of the design document gives stun three anti-stun-lock rules. The
third — "a boss cannot be stunned at all" — was unenforceable because nothing in
the engine could identify a boss: no flag, no subclass, no tag, no data column.
Issue #395 listed the candidates and asked for a decision.

### The decision, and whose it was

The project owner delegated the choice on 2026-08-10 with one steer, quoted:

> "I'll let you decide where 'is a boss' lives. I'm personally not sure if it
> should be a tag or a boolean on the characterbase. But, the enemy generator
> should be creating x of each rarity based on the pool weights so wherever
> that would work best for that. I think the initial weights are in the
> DungeonSimulator app."

**Decision: rarity lives on the enemy as an integer step — the `Step` column of
`game/Data/EnemyRarities.csv`, set by whoever spawns it, exactly as health,
damage and armour are — and boss-ness DERIVES from it.** Steps 4 (Boss) and 5
(Cataclysm Boss) are bosses; Herald at step 3, the Abyssal Warden's reference
rarity, is deliberately below the line because a mini-boss is not a boss.

Neither a tag nor a standalone boolean, and the steer is the reason: the enemy
generator has to assign each enemy a rarity from the pool weights anyway, so
boss-ness follows from what it already sets and there is no second thing to
forget. A generator cannot create a Cataclysm Boss that is stunnable.

It also keeps the two-layer rule intact in the only defensible way. Rarity
scales magnitude and archetype sets behaviour; this rule is the one behaviour
the design itself states in rarity language — the hits-to-kill table says
"Cataclysm Boss Gatekeeper" — so hanging it on rarity is reading the design as
written rather than adding a third layer.

### The boundary

`FirstBossRarityStep = 4` in `CataclysmEnemyCharacter.h`, pinned to
`RARITY_ORDER.index("Boss")` in the model by a test that runs on every pull
request, because continuous integration builds no C++. The rule outranks
`bStunIsDesigned`: that flag skips the damage threshold, not this. "At all"
means at all.

### The finding about the weights, stated plainly

The steer said the initial pool weights are in the DungeonSimulator app.
**Checked: they are not.** The only rarity weights in that app are POWER
weights — `rarityWeights` in `src/utils/calculateScores.tsx` line 56, the
fraction of a tier's power gap each rarity adds — and those are already ported
as `scoring.RARITY_WEIGHTS`. No spawn-pool, count or distribution table exists
anywhere in that app. The spawn-pool weights the generator needs still have to
be decided, and that has its own issue rather than an invented number here.

---

## 2026-08-10 — Offline play is a commitment, and the corrupted character table carries build shapes rather than numbers

**Affects:** Section VIII of `Cataclysm_GDD_v2.md`, the Corrupted Stalker dungeon
modifier and its cross-references in section VII. Applied. Leaves #179 open for
the backend work and #504 open for the missing workbook row.

### What was decided

1. **The game ships an offline mode.** It was previously conditional — section
   VIII said "the game requires a network connection by default" and "if an
   offline mode is offered". Both are gone.
2. **A character is created as either offline or online and never changes**, in
   either direction. Offline characters have no auction house, no ladder, and
   never contribute a snapshot to or draw one from the shared table of corrupted
   characters.
3. **The Corrupted dungeon modifier is renamed Corrupted Stalker.**
4. **A corrupted character's drops carry the shape of a build and none of its
   numbers.** Every affix value is generated at the encountering player's tier
   from the game's own affix tables.
5. **Offline, the modifier is filled from the authored pool** rather than being
   excluded from dungeon generation, which is what the section previously said.

### Why offline play survives, when the reason for dropping it was cheating

The concern was real: a local save file can be edited, and a ladder fed from
edited saves is worthless. Diablo III's console versions are the worked example
— they ship local hero saves and their leaderboards were permanently polluted,
which Blizzard never fixed.

But deleting offline play is not the genre's answer to that, and it is not
necessary. Last Epoch ships a true offline mode alongside online leaderboards and
a trading Bazaar, and keeps them honest by making the two character populations
permanently non-transferable — the stated reason being precisely that local files
can be edited. Diablo II did the same thing in 2000 with its open and closed
realms. The cost of this rule is one flag set at character creation and two gates,
on ladder submission and on auction house access.

There is a second reason to want it. Last Epoch's 1.0 launch on 2024-02-21 spent
roughly five days in cascading failure — 1.4 million logins in the first week, a
server matcher that crashed under load and capped capacity at an estimated
120,000 to 150,000 players, and a point at which their own deployment tooling
broke so fixes could not be shipped. That was a team of about 105 people. Offline
mode was the only thing that kept the game playable.

### Why the rename

"The Corrupted" collided with three things already in `Cataclysm_GDD_v2.md`: the
Corrupted Sentinel enemy archetype, the Corrupted Mote crafting material, and the
corrupted double in section VII, which is a different mechanic — the player's own
character, built locally, needing no network. "Stalker" names the behaviour the
section already describes, that it hunts the player across floors rather than
waiting to be found. This follows the Magic Find precedent recorded on
2026-08-05: one thing gets one name, because two names in the shipped tables is a
defect.

### Why the drops carry shapes rather than numbers

**The problem this does not solve, and the one it does.** Nioh and Nioh 2 ship
this mechanic almost exactly — a dead player's character appears in other
players' worlds and drops copies of the equipment it was wearing — and it became
a deliberate item transfer channel. The documented technique is to strip to a
single item, die in a known place, and have a friend farm that grave, which
carries a visible marker when it belongs to someone on your friends list.

**That attack does not exist here and no defence against it was adopted.** This
game draws an entry at random from a global pool into a procedurally generated
dungeon. There is no way to select a specific entry, so there is no channel to
hand an item to a chosen person. An earlier proposal to have the enemy drop
server-rolled loot instead of the player's equipment was rejected by the project
owner on exactly this ground, and rejected correctly — it would have removed the
point of the feature, which is that a real player's build drops real top-tier
equipment.

**The narrower risk is item fabrication, and the scaling rule already closes it.**
If the game client is what creates items, a modified client could fabricate an
item, cross the Consumption Threshold on purpose, lose to its double on purpose,
and inject that item into the pool, where it reaches strangers and then the
auction house. That needs no targeting.

The rule that closes it is not a new one. Scaling has to work upward as well as
downward, because a character consumed at tier 3 and met at tier 7 must be raised
to peak tier 7 power. Equipment cannot be raised by multiplying stored numbers,
because affix tiers have defined value ranges; raising an item means rolling new
values inside the higher tier's range, from the game's own tables. Running that
same generation in the downward direction costs nothing and means the shared
table cannot carry a number the game would not itself have produced.

Matching a target Power Score is not sufficient on its own. A target can be met
with one extreme value offset by weak values elsewhere, and a single extreme
affix is worth having by itself once items can be traded.

### Why the authored pool fills the slot offline

Three shipped games substitute an authored or game-controlled stand-in for
missing player data rather than removing the content: Nioh uses developer-placed
graves, Dragon's Dogma 2 keeps official Capcom pawns hireable, and Deathloop has
Julianna invade under game control. In all three the encounter still happens and
only the source of the character changes. The authored entries needed for launch,
described under Seeding, serve this case at no extra cost.

Caution on that evidence: two forum sources disagree about whether Nioh's
developer graves actually appear with the network disconnected, and Nioh 2 has a
setting that disables graves outright. Treat it as a design pattern worth
copying, not as proof the slot is always filled.

### Two things this creates and does not settle

**The scaling rule requires inverting the power model.** Given a target Power
Score, produce a gear set. `sim/cataclysm_sim/scoring.py` is already a copy of
`calculateScores.tsx` in the separate DungeonSimulator repository, and `CLAUDE.md`
records that this copy has silently drifted twice. Whatever performs this scaling
is a third implementation that has to agree with both.

**A snapshot parser is a place other people's data enters your process.**
CVE-2022-24126 is a 9.8-severity out-of-bounds write in Dark Souls III's parser
for other players' session data, allowing remote code execution through the
matchmaking servers. The snapshot format must be fixed-width and bounds-checked
on the receiving client, not only validated on the server. #179 carries this.

### Still open after this entry

- Whether co-operative multiplayer runs on dedicated servers or on a host among
  the party. Dragonkin: The Banished was researched as a candidate model and its
  co-op architecture is attractive — host-based, friends only, no matchmaking, no
  dedicated servers, and fully playable offline — but its rule that characters
  move freely between offline and online cities is only safe because it has no
  global marketplace, no leaderboards and no seasons. That specific rule is
  rejected here for the reason in the section above.
- Whether combat runs on the server. This is the expensive question and it is not
  answered by wanting a marketplace: a marketplace needs a server-authoritative
  item ledger, which is a web service and a database, not a fleet of game servers.
- The auction house design in #57, and whether it ships at all.

### Sources

[Last Epoch characters cannot switch between offline and online — Prima Games](https://primagames.com/news/psa-last-epoch-characters-cant-switch-between-offline-and-online),
[Last Epoch 1.0 launch retrospective — Eleventh Hour Games](https://forum.lastepoch.com/t/1-0-launch-retrospective/69374),
[Diablo III console leaderboards topped by modified saves — Blizzard forums](https://us.forums.blizzard.com/en/d3/t/playstation-leader-boards-topped-by-cheaters/49097),
[Diablo II realms: closed characters are stored on the realm — Battle.net](https://classic.battle.net/diablo2exp/faq/realms.shtml),
[Nioh 2 Revenants and what they drop — Fextralife](https://nioh2.wiki.fextralife.com/Multiplayer),
[Nioh item transfer through graves — Steam discussions](https://steamcommunity.com/app/485510/discussions/0/1488866180607648818/),
[Dragon's Dogma 2 offline pawns — DualShockers](https://www.dualshockers.com/dragons-dogma-2-how-to-play-offline/),
[Deathloop single-player Julianna — TheGamer](https://www.thegamer.com/deathloop-turn-off-pvp-invasions/),
[FromSoftware servers do matchmaking and asynchronous data, never gameplay — Tim Leonard](https://timleonard.uk/2022/05/29/reverse-engineering-dark-souls-3-networking),
[CVE-2022-24126, remote code execution in the Dark Souls III session parser — NVD](https://nvd.nist.gov/vuln/detail/CVE-2022-24126),
[Dragonkin is designed to be fully playable offline — developer statement](https://x.com/DragonkinGame/status/1886082149612147070),
[Dragonkin characters transfer between offline and online cities — developer, Steam discussions](https://steamcommunity.com/app/1863430/discussions/0/797838547624287910/).

---

## 2026-08-09 — The Gatekeeper: three phases that add and never take away

**Affects:** the Vertical Slice Enemy Behaviour section of
`docs/Cataclysm_GDD_v2.md` (new Gatekeeper subsection, and the Abyssal Warden's
"only melee enemy" and "only designed" sentences), `ABILITIES`,
`PHASE_TRANSITIONS`, the `phase` field on `Ability` and `ATTACK_REACH` in
`sim/cataclysm_sim/enemy_abilities.py`. Applied. Closes #354.

### What was designed

The boss. Four abilities across three phases; the full table and prose are in
the design document and the machine-readable copy is the model. In one line
each: a telegraphed 2 m cone every 3 s that is half a kill; a lobbed mortar
that leaves burning ground and shrinks the arena; a summon of 3 Imps to a cap
of 6 from phase 2; and the Warden's 6.5 m cap-sized ring at 400% from phase 3.

### What the research settled, with sources

Run before designing, as `CLAUDE.md` requires. Two of four research passes
completed (Path of Exile 1 and 2, and Last Epoch); the Diablo pass and the
general-craft pass died with the tool that ran them and were NOT completed. The
two that finished agreed independently on every structural question:

- **Phases trigger on health percentage.** Timers trigger phases nowhere in
  either game; a timer appears only as a fail-window inside a transition, and
  mechanic completion is an exit condition, not an entry condition. (PoE:
  quarters at 75/50/25 — Atziri, Sirus, Uber Elder; PoE2 pinnacle at 50. LE:
  Aberroth at 77/63/49/35, community-derived.)
- **Two or three phases** for a boss of this scope. Higher counts in PoE are
  inflated by intermission fights against separate creatures.
- **The stat block does not change per phase.** Across ten bosses, not one
  gains damage, armour, attack speed or crit at a transition. Escalation is
  adding a named ability or using an existing one more often. **This is the
  finding the design leans on hardest: a phase owns only which abilities are in
  the rotation, so the two-layer rule survives a multi-phase boss untouched.**
- **A transition is brief and partial, never a stop.** LE removed its full
  invulnerability system (Boss Ward) after player backlash; its replacement is
  90% damage reduction for 4.5 s with the boss still killable.
- **Phase bands are uneven**, with a long opening that teaches the base kit.
- **Arena change means persistence** — ground that accumulates — not
  replacement.

Sources: Maxroll's Aberroth, Shaper and boss guides; poe-vault; the Last Epoch
1.1 *Harbingers of Ruin* dev blog (the Boss Ward removal). All phase figures
are community-derived from play, not developer-published; poewiki.net and
pathofexile.fandom.com were unreachable during the research, so most PoE claims
rest on a single guide site.

### What is a judgement, labelled

- **Thresholds 60% and 30%.** The shape is the genre's; the figures are not
  published anywhere to copy.
- **Dread Cleave's 2.0 m radius**, bounded by the 1 m floor and the 3.85 m its
  interval allows.
- **Soul Harvest's 20 s cooldown**, inside the Ultimate band, above the
  Warden's 12 because this creature kills in 6.0 seconds.
- **Three phases rather than two.** The document promises "each phase
  introduces new mechanics", plural; two phases introduce one.

Everything else is reused from the other six enemies: the mortar figures are
the Sentinel's, the ground riders the Hellhound's, the adds are Imps, the ring
is the Warden's.

### How the model gained phases without gaining a phase system

An `Ability` now carries `phase: int = 1`, meaning "available from phase N
onward", and `PHASE_TRANSITIONS` maps an enemy to its thresholds as health
fractions. That is the whole representation, and it is deliberately the
smallest one the research finding permits: since a phase may only select the
rotation, it needs no stat block, no per-phase overrides and no new tables. Six
of the seven enemies never mention it and are unchanged. An import-time check
(`_check_every_phase_is_reachable_and_starts_at_one`) refuses a phase nothing
transitions into, an empty first phase, and thresholds that do not strictly
descend within (0, 1).

### What this deliberately does not decide

- **How the engine represents a phase.** Nothing in `game/Source/` can express
  a boss or a phase today; #395 records that even "a boss cannot be stunned" is
  unenforceable. Building the Gatekeeper needs #395 first and an engine phase
  mechanism second, and both are build-time concerns the design does not
  constrain beyond the model's own shape.
- **The transition visual.** The design says the soul-siphon channel plays for
  about 2 seconds at 90% damage reduction; the exact figure is tuned by play.
  Sevarog's `Stage_1..Stage_4` clips are 0.03 s marker poses, not animations,
  so they are not the transition visual on their own.

---

## 2026-08-09 — A charge in flight is committed, but a stun still stops it

**Affects:** `ACataclysmEnemyController::Think` and `ACataclysmEnemyCharacter` in
`game/Source/Cataclysm/Character/`. Applied. Closes #499.

### What was wrong

The brain had no idea a charge was running. `ContinueWindUp` returns true only
while a creature is winding *up*; it clears the wind-up and returns on the pass
that *lands* the ability, which for a charge is the pass the travel starts on. So
for the whole of a charge — 0.70 seconds for the Abyssal Warden, two or three
thinking passes — the brain ran its ordinary logic four times a second on a
creature already in flight. It turned the creature to face its target, ordered a
walk toward it, and stopped its movement when the target came within reach.

The project owner saw the first of those while playing: "he turns around mid
charge to face you as he's flying past".

**It was not only cosmetic.** The design says a miss costs the creature because it
ends up past the player **facing away** and has to turn before walking back. A
creature that turned to track the player during the charge arrives already
pointed at them, so it skips the turn the design counts as part of that cost.

### The decision that had to be made

Whether a stun stops a charge already travelling. Nothing in the design document
answers it, because until #491 nothing could travel.

**Decision: a stun stops the charge where it is.** Two rules pull in opposite
directions and one of them turns out not to apply:

- The design says a stunned target "cannot act at all". A creature still crossing
  the ground would be acting.
- The commitment rule says a charge runs its full distance whether or not
  anything is still there. **That rule is about the target, not about the
  creature.** It exists so the attack cannot track the player, and it is why a
  miss costs the walk back. It says nothing about crowd control, and reading it
  the other way would make a charge the one attack in the game that interrupting
  cannot answer.

**The charge is spent even though it did not finish.** An ability is stamped onto
its cooldown when it lands, and a charge lands at the moment it sets off. That is
deliberately harsher than an interrupted wind-up, which is not spent at all,
because an interrupted wind-up did not happen and an interrupted charge did.

### How it shipped in the first place

**Every test of the charge drove it directly and none of them ran the brain.** The
three automation tests written with #491 call `BeginCharge` and `AdvanceCharge`
and never call `Think`, so the brain was not in the picture and nothing could
notice it was still steering. The new test runs the brain while a charge is in
flight, which is the only arrangement that can see it.

That is the general lesson and it is worth carrying: a test that drives an ability
directly does not test the creature that uses it.

---

## 2026-08-09 — One telegraph tier, with a ceiling on the wind-up

**Affects:** the Attack Telegraphs subsection and the Abyssal Warden's subsection
of `docs/Cataclysm_GDD_v2.md`, `fits_its_cycle` and the Warden's ability table in
`sim/cataclysm_sim/enemy_abilities.py`, and
`ACataclysmAbyssalWardenCharacter` in `game/Source/Cataclysm/Character/`.
Applied. Closes #487 and #496.

### What was wrong

The design had two tiers of telegraph: small ones walked out of, and large ones
that cost a Movement skill. Issue #487 reported the second tier was unreachable —
the walk-out limit grows at 1.75 m per second of cooldown while its 8 metre cap
did not grow at all, so above a 5.36 second cooldown every legal radius was
already a walk-out radius. **Measured across all fourteen designed abilities of
the six designed enemies: eight are telegraphed and all eight were in tier one.
The tier was empty and always had been.**

Issue #496 was the same defect seen from play. The project owner played the
Abyssal Warden and said its 5.6 metre ring was "too easy to escape". Making it
bigger could not help, because the wind-up formula returns exactly as much ground
as a bigger radius takes away — the escape margin was 2.3 metres at every radius.

### The finding that changed the answer

**The second tier would not have worked either, and nobody had computed that.**
Its formula is `0.8 + Radius / 16`, which is the same radius-cancelling shape with
a more generous allowance and a faster assumed escape. Its escape margin is
**13.7 metres at every radius**, against the walk-out tier's 2.3. It was between
identical and twice as forgiving, never harder.

So issue #496's own recommendation — fix #487, then move Molten Roar into tier two
— would have made the attack *easier*, while shortening its warning from 2.00 s to
1.15 s. It would also have broken the art: the longest wind-up tier two can
produce at any radius is 1.30 s, and the `Ultimate_Roar` clip is 1.4000 s.

### The decision

**Delete the second tier. One wind-up formula, held to a ceiling.**

```
Wind-up seconds = min(0.4 + Radius / 3.5, 2.0)
Cap on radius   = 3.5 x (2.0 - 0.4) + contact     = 6.50 m for a 0.48 m body
```

Below a 5.6 metre radius nothing changes at all. Above it the warning stops
growing while the ground to cross keeps growing, so **the margin falls by one
metre per metre of radius.** Radius finally means difficulty, which is the thing
the second tier was supposed to buy and never did.

The cap is not chosen. It is the radius at which the slowest class still has
exactly the 0.4 second reaction allowance and nothing more, so "every class clears
every telegraph with at least the stated reaction allowance" became a property of
the rules rather than something to check attack by attack.

**Zero abilities changed classification.** The largest existing marker was 5.6 m
and the next largest 3.5 m, so no existing wind-up moved.

### What is a judgement, and it is labelled one everywhere

**The 2 second ceiling.** Nothing derives it. It was already the longest telegraph
in the game, so adopting it changed no shipped attack, and no comparator publishes
a telegraph duration to check it against.

### What the research settled and did not

**Settled: no shipped ARPG derives a telegraph class from arithmetic.** Path of
Exile's monster abilities carry a cast time and an area and no category field.
Diablo III's affix numbers are public and fit no single rule — escape margins run
from about −6 yards through 0 to +9 — and that spread is deliberate.

**Settled: nobody makes an attack un-walk-out-able by making it bigger.** The
levers that appear repeatedly are a shorter wind-up on the same circle, persistent
ground denial, removing or redirecting the escape, and covering everything while
providing a safe spot.

**Settled: a mandatory traversal skill is a known mistake.** Eleventh Hour Games
wrote in the Last Epoch 1.1 dev blog that "most builds currently feel forced to
include a traversal skill purely for evading incoming attacks, which has a
limiting impact on potential build diversity", and answered it with a universal,
slot-free Evade. Diablo IV does the same.

**Not settled: any of this project's constants.** No published number validates
0.4, 3.5, 8, or 2.0. Path of Exile's distance unit is explicitly not metres, and
Blizzard publishes no telegraph radius or wind-up at all. Nothing outside this
project can price the 2 second ceiling.

**Sources.** [Last Epoch 1.1 *Harbingers of Ruin* dev
blog](https://forum.lastepoch.com/t/harbingers-of-ruin-dev-blog/); [Diablo III
monster affixes](https://www.purediablo.com/diablo4/Monster_Affixes); [PoE monster
skill data, PoEDB](https://poedb.tw/us/Fighting_Bull); [Maxroll Aberroth
guide](https://maxroll.gg/last-epoch/).

### What this did for the Abyssal Warden's ring

Its radius went from 5.6 to **6.5 metres**, which is the cap. Against the slowest
class the escape margin falls from 2.30 m to 1.40 and the spare time from 0.657 s
to 0.400 — a 39% cut in both. The warning stays 2 seconds, so the 1.4 second roar
animation is untouched.

**This is a modest change and that is stated rather than oversold.** The escape is
still a walk. The geometry is now exhausted: 6.5 m is the ceiling, so if it still
reads as too easy the answer cannot be more radius. The next lever is what the
attack leaves behind, and it currently leaves nothing — the riders already exist
and the Hellhound's charge already uses them. The project owner chose on
2026-08-09 to ship the size change alone first and judge it by playing.

### Rejected

**Making the second tier reachable.** It would have made the attack more
forgiving, shortened the warning below the length of its own animation, and bought
a tier whose only effect is to forbid walking rather than to make escaping hard.

**Declaring the tier by intent rather than geometry.** Same problem: it still
promotes the attack into the more forgiving formula.

**Crediting the player only for the ground they actually cross**
(`0.4 + (Radius - 0.9) / 3.5`). It cuts the margin at every radius, which is
tempting, and it breaks the result that the swarm enemies produce no markers — the
smallest cycle that could carry a one metre marker drops to 0.857 s, which puts the
Imp's 0.9 s attack interval above the threshold.

### Still open

**The cap is 6.5 metres for every creature, including a future boss.** Nothing in
the game may mark a larger area, so a boss cannot have a bigger telegraph than
this mini-boss. Raising the ceiling raises the cap — 2.43 seconds would give 8
metres — but it also pushes the point where radius starts to matter upward, so it
is a real trade and not a free dial. Worth revisiting when #395 designs a boss.

---

## 2026-08-09 — A charge covers its range in the length of its own clip

**Affects:** the Abyssal Warden's charge subsection of `docs/Cataclysm_GDD_v2.md`,
`ACataclysmEnemyCharacter` and `ACataclysmAbyssalWardenCharacter` in
`game/Source/Cataclysm/Character/`. Applied. Closes #491.

### What was missing

Two of the seven vertical slice enemies are designed with a charge — the Abyssal
Warden's Stampede and the Hellhound's Hellrush — and the engine could not execute
one. The marker code had a case for a Strike and a case for a Projectile and none
for a Movement shape, and nothing anywhere moved a creature along a fixed path.

The consequence was specific rather than cosmetic. The Abyssal Warden walks at
2.8 metres per second with no chase speed, against player classes at 3.5, 4.0 and
4.6, so **a player who walked backwards was never caught and the creature could
never be fought at all.** That is exactly the failure its charge exists to
prevent.

### What the design already fixed, so none of it was decided here

All of it from section X of `Cataclysm_GDD_v2.md`:

- A charge hits everything on the way, where a leap hits only where it lands.
- The lane is fixed when the wind-up starts and does not follow the player.
- The creature is committed and runs the full distance whether or not anything is
  still there. The overshoot is the window the telegraph buys.
- The player leaves the lane when their centre leaves it.
- The marker is a lane of width 2 × `Radius` running to `Range`.

### The one thing that had to be decided: how fast a charge travels

**The research did not settle it, and that is said plainly rather than glossed
over.** Path of Exile's monster charge (`BullCharge`, used by the Fighting Bull
and the Bull variants) publishes a 4 second cooldown, a 2.75 second cast time and
"deals 15% more Damage", and **no travel speed**. Neither Last Epoch nor Diablo
publishes one either.

What the research did settle is the surrounding shape, and it agreed with what
this design already said: a charge is a cooldown ability with a long wind-up
rather than free movement, it is worth barely more than a basic attack, and it
passes through bodies rather than stopping on them — Path of Exile's monster
Shield Charge "pushes enemies in the way to the side".

**Decision: a charge covers its designed range in the length of its own animation
clip.** For the Warden that is 8 metres in the 0.700 second `Stampede` clip,
which is 11.43 metres per second.

**This is a judgement and it is labelled one** in the header, in the design
document and here. What makes it defensible rather than invented:

- It is the rule the project already follows everywhere else — the Brute's
  montage delays, the Warden's jog play rate against its measured stride — so the
  speed follows from two measured numbers rather than being chosen.
- It must beat the fastest class or the charge closes nothing. 11.43 against 4.6
  is two and a half times.
- It must beat walking during the wind-up, which is the design's own test, stated
  for the Hellhound: "a charge shorter than that would be strictly worse than not
  winding up at all". At the creature's own 2.8 metres per second the same 8
  metres would take 2.86 seconds, longer than its whole attack interval.
- The exchange it produces is the one the design describes: 0.70 seconds of
  closing bought with 2.86 seconds of walking back after a miss.

`Cataclysm.Warden.StampedeSpeed` sets it from the console, with 0 meaning the
designed figure, because a judgement is what a play session is for.

**Sources.** [BullCharge on the Fighting Bull,
PoEDB](https://poedb.tw/us/Fighting_Bull); [Skill:SpikerBullCharge, PoE
Wiki](https://www.poewiki.net/wiki/Skill:SpikerBullCharge); [Shield Charge, PoE
Wiki](https://www.poewiki.net/wiki/Shield_Charge).

### Two smaller decisions the issue asked for

**What stops a charge: the level, not bodies.** It is swept by object type against
`WorldStatic` rather than by the `WorldStatic` *channel*. That distinction is
load-bearing and it cost a build to find: a Pawn capsule blocks the WorldStatic
channel, so a channel sweep stopped the creature dead on its own capsule and
travelled nothing, and would have stopped it on the player too — the opposite of
the committed overshoot the design describes.

**A charge advances per frame, not per thinking pass.** The brain thinks four
times a second and this charge covers 2.86 metres in one of those, so a
brain-driven charge would move in visible jumps and could step straight over the
player without the lane ever containing them. Every enemy therefore ticks; one
that never charges pays a single boolean test per frame.

### What is deliberately not built

`Leap` and `Blink` modes. Only `Charge` has a customer: the Warden's design chose
`Charge` over `Leap` because the art is one clip rather than five, so neither of
the other two modes has anything to execute yet.

---

## 2026-08-09 — No positional weak points, and the Abyssal Warden instead

**Affects:** the Vertical Slice Enemies table and the Vertical Slice Enemy
Behaviour section of `docs/Cataclysm_GDD_v2.md`, `ABILITIES` and `ATTACK_REACH`
in `sim/cataclysm_sim/enemy_abilities.py`, and the module docstring of
`sim/cataclysm_sim/enemy_stats.py`. Applied. Closes #353.

**This supersedes two earlier entries in this log** that describe the Abyssal
Warden as having a positional weakness: the 2026-08-06 entry on not using
Behaviour Trees, which lists "a mini-boss with a positional weakness" among the
seven, and the entry recording what the enemy stat work did not settle, which
names "the Abyssal Warden's positional weak points" as open behaviour. Both were
true when written. Neither is now. They are left as written, in the way this log
leaves its pre-2026-08-02 wording, rather than edited.

### The decision that came first

Asked of the project owner and answered on 2026-08-09, in these words:

> "we don't do positional weak points. That's too tedious in a diablo like arpg"

The design document's one-line description of this creature had been "Massive
stone and lava demon. High damage resistance but vulnerable at legs and back"
since the documents were first imported. **Nothing in the project ever
implemented it.** `FCataclysmIncomingHit`, the struct that describes a hit to the
mitigation pipeline, carries eight fields and not one of them is a position, a
bone, a hit box or a facing angle. `UCataclysmDamageCalculation::Resolve` has no
access to the attacker's location at all. No test anywhere would have failed if
the phrase had been deleted.

So the clause described behaviour that did not exist and was not going to. It is
now removed, and the description reads "Massive stone and lava demon. High damage
resistance."

### The second decision, which reframed the rest

Said immediately afterwards, and it is the more consequential of the two:

> "'High damage resistance' can be a combination of things. Enemies get layers of
> defense just like the player. So armor/resistances/damage reduction/etc"

That is not a detail. It says the creature's defensive identity is however many
mitigation layers it is given, in the same way the player has several — 53% off
from armour, 70% resistance, 28% block chance and 16% flat reduction for a geared
character at tier 8, multiplying to about a tenth of a hit landing.

**The Abyssal Warden's layers are the two highest in the slice and no others:**
a 3.50 armour share against the Brute's 3.00, and 35% resistance against the
Gatekeeper's 30%. Zero evasion, zero energy shield. At Herald rarity on the last
floor of a 50-floor Cataclysm dungeon that is 5,954 armour, worth 48.19% at tier
8, plus 35% resistance: **66.3% of a hit stopped**.

**Giving enemies the two layers only the player has** — block chance and flat
damage reduction — is a change to every enemy rather than to this one, and it has
its own issue.

### What the creature does

| Ability | Slot | Shape | Parameters | Runs on |
|---|:-:|:-:|---|:-:|
| Sunder | Basic | Strike | `Radius=0.9; Angle=90; MaxTargets=1` | its 2.4 s attack interval |
| Stampede | Movement | Movement | `Mode=Charge; Range=8; Radius=1.5` | a 5 s cooldown |
| Molten Roar | Ultimate | Strike | `Radius=5.6; Angle=360` | a 12 s cooldown |

**The charge exists because this is the only designed MELEE enemy that cannot
catch anybody.** The Succubus cannot catch the fastest class either and does not
need to, because it reaches 8 metres; being unable to close only matters for a
creature that has to. It moves at 2.8 metres per second with a chase speed of 0.0, against
player classes at 3.5, 4.0 and 4.6. Without it a player walks backwards and it
never fights. Range 8 is the shortest Movement-shape skill range in
`game/Data/WeaponSkills.csv`, which is the right one for the slowest creature,
and it still passes the test the Hellhound's charge sets: the Warden could walk
2.32 metres during the 0.83 second wind-up, and eight is more than three times
that.

**It repeats the Hellhound's Charge mode, and the art decided that rather than
taste.** A Leap was proposed first, on the argument that a leap clears a ring of
bodies where a charge meets it. Measuring the Grux pack on 2026-08-09 with
`tools/probe_warden_animation.py` settled it the other way: `Stampede` is a
single 0.700 second clip that fits inside the 0.83 second wind-up at its authored
speed, where a leap has to be stitched from five — `Jump_Start` 0.333,
`Jump_Up` 0.333, `Jump_Loop` 1.333, `Jump_Fall` 1.333 and `Jump_Land` 0.900 —
which the current one-clip-at-a-time playback cannot do without the animation
Blueprint work in #387. `Bound` read like the leap from its name and is 0.0333
seconds, a single pose.

**Molten Roar is the largest telegraph in the game**, 5.6 metres against the
Brute's stomp at 3.5, and the first thing in the game to use the Ultimate slot.
Its 12 second cooldown is derived: a Herald Abyssal Warden kills the reference
geared character in 5 hits, which at a 2.4 second interval is 12.0 seconds, so a
longer cooldown could come round zero times in a fight the player is losing. It
is also the bottom of the Ultimate slot's 12-to-40 second band. At that slot's
400% it lands at about four ordinary hits, four fifths of what the reference
build survives, for something that warns for two seconds and is avoided
completely by walking out. `Ultimate_Roar` is 1.4000 seconds, so the wind-up
holds the whole clip at authored speed.

### One property of the wind-up rule that nobody had stated

**A bigger marker is not harder to escape.** The wind-up is
`0.4 + Radius ÷ 3.5`, so the slowest class walks `1.4 + Radius` metres during it,
while a player at contact has to cross `Radius − 0.9`. The difference is **2.3
metres at every radius**. Molten Roar at 5.6 metres and the Brute's stomp at 3.5
give exactly the same margin.

A marker's size therefore says how much ground it denies and how long it warns
for, and nothing about difficulty. It is now written in the design document,
because "bigger marker" reads as "harder" and is not.

### What is a judgement rather than a derivation

- **Molten Roar's 5.6 metre radius.** Bounded below by the 2.80 metres its own
  attack interval allows and above by the 8 metre cap. Inside that window 5.6 is
  chosen because it is exactly twice the 2.80 and makes the wind-up exactly 2.0
  seconds.
- **Three abilities rather than two.** Without the charge the creature can be
  walked away from and never fought.

### What the research found that was bigger than this creature

Thirteen readers went over the repository and the genre for this design. Seven
defects were found, verified directly, and filed rather than fixed here:

| Issue | What is wrong |
|---|---|
| #481 | The simulation never applies enemy armour, so "damage needed to kill" is too low — 3,929 against this creature where 7,584 is right. **Fixed on 2026-08-12**; the entry at the top of this log records what changed |
| #482 | Penetration past an enemy's resistance becomes a damage multiplier, which this document says it must not |
| #483 | The guard against an immune enemy checks one layer where its own docstring says it should check the combination |
| #484 | This document says enemy damage multiplies by 1.55 per rarity step; the model uses 1.40 |
| #485 | An enemy's energy shield never reaches the engine |
| #486 | Every hit resolves as untyped, so no resistance on either side does anything in the running game |
| #487 | The Movement-skill telegraph tier is arithmetically almost empty |
| #488 | Enemies have no block chance and no flat damage reduction |

**#486 bears directly on this creature.** Its 35% resistance is the entire
mechanical content of "high damage resistance", and in the editor today it does
nothing. Its armour is the only defensive number it has that currently works.

**#487 changed the shape of this design.** This document says a telegraph larger
than walking can clear "is what makes a mini-boss or a boss feel different from a
Brute", and the Abyssal Warden looked like that tier's first customer. Computing
the window showed the tier is only reachable on a cooldown between 5.00 and 5.36
seconds and is empty above that. Molten Roar is therefore a walk-out marker that
is simply the biggest one, rather than the other tier.

### Two mechanisms the genre recommended and this project had already refused

**A stagger or heavy-stun meter.** Two independent researchers put it first;
Path of Exile 2 and Diablo IV converged on it. Section VI of
`docs/Cataclysm_GDD_v2.md` records the same survey and its conclusion: "Diablo IV
routes crowd control into a separate stagger meter that must be filled before any
of it applies. Outright immunity is the simplest of the four and it is what was
chosen." Building one now would reverse a decision made on the same evidence.

**Facing-gated damage reduction** — taking less damage from the front. That is
positional counterplay under another name, and it is what the project owner ruled
out.

### What is not settled

**Nobody has played it, and nobody can.** There is no Abyssal Warden class in
`game/Source/Cataclysm/Character/`, which holds one enemy, the Brute. Every
figure that turned out wrong on the Brute was wrong in a way no test could see.

---

## 2026-08-09 — The Corrupted Sentinel: a bolt down a marked lane, and a shell lobbed over cover

**Affects:** `ABILITIES` and `ATTACK_REACH` in
`sim/cataclysm_sim/enemy_abilities.py`, and the Vertical Slice Enemy Behaviour
section of `docs/Cataclysm_GDD_v2.md`. Applied. Closes #352.

### The question

Every one of the seven vertical slice enemies has had a full stat block since the
enemy stat work landed, and none of them said what the creature DOES. #352 is the
Corrupted Sentinel's turn, and it asked four things: what its projectile is, what
it does when the player is out of range or behind cover, whether it has a minimum
range, and whether its telegraph is a ground marker or a wind-up read off the
creature.

**One fact decides all four.** Its movement speed is **0.0 at every rarity**, so
it cannot close a gap, cannot retreat from one, and cannot step round an obstacle
to see past it.

### What was decided

Two abilities.

| Ability | Slot | Shape | Parameters | Runs on |
|---|:-:|:-:|---|:-:|
| Siege Bolt | Basic | Projectile | `Range=14; Radius=2.1; Pierce=0; Speed=1400` | its 2.0 s attack interval |
| Brimstone Mortar | Special | Projectile | `Range=14; Radius=3.0; Pierce=0; Arc=0.25` | an 8 s cooldown |

**Its reach is 14 metres, the longest range any player attack has.** Emberbolt on
the wand and Hellbrand on the greatsword both state `Range=14` in
`game/Data/WeaponSkills.csv` and nothing states more. It gets all of it because
reach is the only tool it has: at the Succubus's 8 metres, any ranged build could
stand at 9 and kill it for nothing, which is the free kill #352 asked this design
to prevent. Two rows do reach 15 metres — Subjugate, a Debuff, and Open the Rift,
a Summon — and neither is an attack.

**Its bolt's speed is decided rather than chosen.** The wind-up takes 1.0 second
of the 2.0 second cycle, so the flight has the other 1.0, and fourteen metres in
one second is 1400 centimetres per second. That is one of the ten speeds the
player skill table uses, and it is the slowest of them that gets the shot there
in time. **So the creature's cycle is exactly two seconds with nothing idle in
it**: one second of marker on the ground, one second of flight, and the next
marker appearing as the shot lands. That is "forces the player to stay mobile" as
a number rather than as an adjective.

**It does not lead the player.** The lane is fixed when the wind-up starts, which
is already the rule for every telegraph in this game. The genre confirms it from
the other side: leading a moving target is the standard way to make a projectile
land, which is exactly why a telegraphed one must not do it.

**Geometry blocks the bolt, so cover works.** Path of Exile's projectiles travel
until they hit an enemy or an obstacle. Diablo IV players file enemies shooting
through walls as bugs, so the expected behaviour there is that hiding works.
Breaking line of sight is real counterplay, and it is the counterplay a
stationary creature ought to have.

**The mortar is the only answer it has to cover.** A creature that shoots in
straight lines and cannot walk is answered by one pillar. The shell arcs over,
using the `Arc` parameter established for the Brute's rock rather than a new
mechanic. At its full fourteen metres it is in the air 1.69 seconds and lands at
1171 cm/s, under the 1200 the Brute's rock is held to.

**It has no minimum range on the bolt and needs none.** A Projectile's marker is
a LANE running from the caster out to its range, so a melee character standing
against the Sentinel is standing in that lane and has to step out of it every two
seconds like everybody else. At contact range that means walking around the
creature. **That is its whole answer to melee.** The rule that an attack must not
mark the ground its own caster stands on was written for a LOB marking a circle,
and it does apply to the mortar: 3.0 + 0.48 = **3.48 metres**.

**The rest of its answer to melee is in the stat block rather than in an
ability.** 2.20 armour share, an energy shield worth 35% of its health, 20%
resistance and a 1.30 health share. `sim/cataclysm_sim/enemy_stats.py` already
says in terms that a creature which cannot retreat has to be able to take hits.
It survives being stood on rather than preventing it.

### What is a judgement rather than a derivation

`CLAUDE.md` asks for this to be separated out. Three things:

- **The mortar's 3.0 metre radius.** Bounded below by the bolt's 2.1, so the two
  markers read as different sizes, and above by the 8 metre escape cap and by
  the 12.6 metres its cooldown allows. Inside that window 3.0 is chosen because
  it is the second largest `Radius` any player Projectile uses, Blood Pyre's.
- **Two abilities rather than one or three.** The Imp's section argues one
  ability can be a complete design and the argument could have been made here.
  It was not, because nothing but the mortar answers cover.
- **The 8 second cooldown.** Inside the Special slot's 3 to 10 second band, and
  it puts exactly four bolts between one shell and the next, but the band is
  wide.

### Two rules that were only in prose, and are now checked

**A Projectile states `Speed` or `Arc` and never both.** The docstring on
`SHAPE_PARAMS` has said so since #474 and nothing checked it. The Sentinel is the
first enemy carrying one of each, which is what made the gap worth closing.
`_check_every_projectile_states_a_speed_or_an_arc_but_not_both` now enforces it.

**The lobbed minimum range had two definitions and now has one.** It lived in
`ACataclysmBruteCharacter` as `RockThrowMinimumRangeCm = 258.0f` and in prose. It
is now `lob_minimum_range` in `sim/cataclysm_sim/enemy_abilities.py`, and
`tools/tests/test_the_rock_throw_minimum_range.py` holds the C++ constant to it.
Before this the C++ was only checked against itself — three constants out of one
header, confirming the arithmetic between them — which cannot catch the header
drifting away from the design.

### What this does not settle

**Its notice radius.** The 14 metre reach is unreachable unless the creature
notices at least that far, and no enemy has a designed notice radius: the Brute's
1000 cm was set by playing. That is #383, and it now has a hard floor for one of
the seven.

**Nobody has played it.** There is no Corrupted Sentinel class in the project —
`game/Source/Cataclysm/Character/` holds one enemy, the Brute. Every figure that
turned out wrong on the Brute was wrong in a way no test could see, so none of
these numbers should be treated as settled until somebody has fought one.

**Where its shot leaves the barrel.** #478. Nothing has measured the release
moment inside `Fire_Planted`, so the one second wind-up cannot yet be lined up
with the animation. That blocks building the creature, not designing it.

**Sources for the genre claims:**
[Projectile, Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Projectile);
[Enemy shooting through walls, Diablo IV forums](https://us.forums.blizzard.com/en/d4/t/enemy-shooting-thru-walls/42337);
[Predictive Aim Mathematics for AI Targeting, Game Developer](https://www.gamedeveloper.com/programming/predictive-aim-mathematics-for-ai-targeting).

---

## 2026-08-09 — An enemy's attack animation is played to fit its designed attack interval

**Affects:** `game/docs/enemy-source-assets.md`, and the Corrupted Sentinel's row
in its timing table. Applied. Closes #369. It changes no number in
`sim/cataclysm_sim/enemy_stats.py` and no table in `docs/Cataclysm_GDD_v2.md`,
which is part of the argument for it.

### The question

The Corrupted Sentinel is played by the siege lane minion from the Paragon
Minions pack. Its only rooted firing animations, `Fire_Planted` and
`Fire_Planted_B`, are **2.40 seconds**, and its designed attack interval is
**2.0 seconds**. Its unrooted alternatives are 2.80, which is worse. There is
nothing shorter in the pack. Re-measured 2026-08-09 with
`tools/probe_sentinel_animation.py`, which reads each clip's play length inside
the editor; every figure matched what was written on 2026-08-07.

#369 set out three ways out and asked which:

| Option | What it costs |
|---|---|
| Play the clip at 1.20 | A judgement about how a slow, heavy unit reads sped up |
| Cut the clip | Art work, and the release moment has not been measured for any enemy |
| Lengthen the interval to 2.4 s | Damage per second falls by 2.0 ÷ 2.4, so the damage share has to be re-derived, and the largest telegraph the creature may draw rises from 2.1 m to 2.8 m |

### What the genre does, and it is nearly unanimous

**Path of Exile makes the animation follow the rate.** Its action speed stat is
described as the rate at which a character's animations play, and it is what
changes how fast attacking, casting and moving actually happen. Attack speed and
animation playback are one knob, not two that have to be reconciled.

**Diablo IV does the same, and states the arithmetic.** An attack's baseline
duration is a frame count, and the game divides that count by the effective
attack speed multiplier to get the duration actually played. The animation is
derived from the rate.

**Diablo II is the counter-example, and it is the older one.** Monster attack
animation speed is not in `MonStats.txt` at all — movement speed is, but the
attack's pace lives in `animdata.d2` as frames per direction, so a monster's
attack duration is whatever its animation was authored at. There the rate is
derived from the animation.

So two of the three derive the animation from the designed rate and the third,
from 2000, derives the rate from the animation. **Modern practice is the first.**

**On how far a clip may be sped up before it reads wrong**, the useful figure
found was a developer's report that around 150% still looks fine, 200% starts to
read as haste, and 250% and above looks ridiculous. 1.20 is 120%.

Sources: [Path of Exile Wiki, Action speed](https://pathofexile.fandom.com/wiki/Action_speed);
[Diablo 4 attack speed frame caps and breakpoints](https://diablodamagecalculator.com/attack-speed-frame-caps-dual-wielding-breakpoints-math.html);
[Attack Speed Mechanics in Diablo 4, Maxroll](https://maxroll.gg/d4/resources/attack-speed-mechanics);
[The Phrozen Keep, how to increase monster attack speed in Diablo II](https://d2mods.info/forum/viewtopic.php?t=9434);
[Frames and Animations in Diablo II](https://www.mannm.org/d2library/faqtoids/frames_eng.html);
[Hive Workshop, attack animation speed](https://www.hiveworkshop.com/threads/attack-animation-speed.290765/).

### What was decided

**The attack interval is the designed number. The animation is played to fit it**,
at `clip length ÷ attack interval`, and never slowed down to fill a gap. For the
Corrupted Sentinel that is 2.40 ÷ 2.00 = **1.20**.

**This is not a new rule. It is the rule the project already has, written down.**
`ACataclysmBruteCharacter::PlayOneShot` and `MontageRateFor` both compute
`FMath::Clamp(FMath::Max(1.0f, Length / Hold), MinimumPlayRate, MaximumPlayRate)`.
The Brute already plays its walk at 1.11, its chase at 1.43 and its rip-and-throw
montage at 1.67. A rate of 1.20 on the Sentinel is the gentlest scaling anything
in this project does, and the only reason the question arose at all is that the
Sentinel has no C++ class yet for the existing rule to have been applied by.

**The ceiling is `MaximumPlayRate`, 2.50.** That is a hard bound rather than an
aesthetic one, and it is why the rule can still fail a creature: both functions
clamp to it, so a clip needing more than 2.50 is played at 2.50 and still
overruns its interval. The header's own words for it are that above this it reads
as a blur, and the genre figure above agrees that somewhere past 200% is where a
sped-up clip stops reading correctly.

### Why not lengthen the interval, which is the honest-to-the-asset option

**Because the asset is placeholder art and the interval is design.** The decision
of 2026-08-06 casts all seven vertical slice enemies from free Paragon packs,
which are stand-ins for models this project does not yet have.
`sim/cataclysm_sim/enemy_stats.py` says in terms that the archetype supplies the
profile — attack interval, criticals, movement, resistances — and that those are
set on the enemy's own terms. Letting a free placeholder clip move a design figure
is the wrong direction, and it would have to be moved back when the real art
arrives.

**And it would have moved four other things.** The damage share would need
re-deriving by 2.0 ÷ 2.4 to keep the creature's contribution the same; the
largest radius it may telegraph would rise from 2.1 m to 2.8 m, which is a
different enemy to fight; the telegraph table in section X of
`docs/Cataclysm_GDD_v2.md` would change; and #352, which designs this creature's
abilities, would have to be designed against the new figure. The play rate moves
none of them.

### What this does not settle, and it is the next thing

**Where the shot leaves the barrel inside `Fire_Planted` is unmeasured**, so
nothing yet lines the release up with the end of the telegraph's wind-up. The
Brute needed exactly this and it was measured rather than guessed:
`tools/measure_animation_impact.py` found its throw releasing at 0.539 s into a
0.87 s clip. Issue #478 is the Sentinel's.

**At 1.20 there is no gap between one shot and the next.** 2.40 ÷ 1.20 is exactly
2.00, so the clip ends as the next begins. Two rooted firing clips exist and
alternating them is what stops that reading as one clip looping.

**This has not been watched.** #369 was labelled `needs-operator` on the grounds
that nobody had seen the creature sped up, and that is still true — there is no
Corrupted Sentinel in the project to watch. The decision is reversible and costs
one constant when the class is built. If it reads wrong in play, cutting the clip
is the option to take next, and #478 is its prerequisite either way.

---

## 2026-08-09 — A lob's arc is a fraction of the distance thrown, not a fixed flight time

**Affects:** `ACataclysmBruteCharacter`, the Rip and Toss row in
`sim/cataclysm_sim/enemy_abilities.py`, and `docs/Cataclysm_GDD_v2.md`. Applied.
This reverses half of the entry below, which was made the same day.

**A fixed flight time fixes the whole vertical part of the trajectory, whatever
the distance.** That is what the entry below did not account for. Gravity and the
time together decide the launch's vertical speed and the height reached, and
neither depends on how far the rock travels. Measured from the shipped code:

| Range | Flight | Ground speed | Upward at launch | Rise above the hand |
|---|---|---|---|---|
| 1.5 m | 1.40 s | 107 cm/s | 570 cm/s | 166 cm |
| 3.0 m | 1.40 s | 214 cm/s | 570 cm/s | 166 cm |
| 10.0 m | 1.40 s | 714 cm/s | 570 cm/s | 166 cm |

At three metres the rock rises 166 cm and crosses the ground at 214 cm/s. That is
a near-vertical mortar, not a throw, and only the ten metre case — the one the
figure was chosen against — read as a lob.

**What is designed is now the arc: 0.25 of the distance thrown.** That is not a
chosen number either; a projectile launched at 45 degrees, the angle that throws
an object furthest, reaches an apex of one quarter of its range. It is what the
design carried before #465.

**The flight time is derived from it.** A parabola sags `g × t² ÷ 8` below its own
chord, so an arc of `0.25 × range` is in the air for `√(8 × 0.25 × range ÷ g)`:
1.43 seconds at ten metres, 0.78 at three, 0.55 at one and a half. The ten metre
figure is within rounding of the 1.4 that was designed for a few hours, so the
longest throw is unchanged and only the short ones moved.

**Everything #465 established is kept.** Constant horizontal speed, an
accelerating descent, real gravity, and the projectile still taking a flight
time — the conversion from an arc to a time belongs on the creature, not in
`ACataclysmProjectile`. The defect #465 fixed was real and its fix stands; only
the choice of which quantity to hold fixed was wrong.

**What is given up, and it is a real loss.** The reaction window is no longer
constant. The player's time to move is the wind-up plus the flight, so it runs
from about 1.6 seconds at close range to 2.4 at maximum range rather than a flat
2.4. That was the whole argument for a fixed flight time, and it is worth less
than a lob that reads as a throw at the ranges a melee creature actually fights
at. The marker still promises the place, the wind-up is unchanged, and at close
range the player is inside the Brute's melee reach with other things to react to.

**The genre evidence in the entry below still stands and is simply outweighed
here.** Shipped games do give some attacks a fixed delay regardless of distance.
Those are attacks that arrive from off-screen or from nowhere in particular. This
one is a creature visibly throwing a rock, and the visible throw is what the
shape has to serve.

**Settled by watching, later the same day.** The project owner played it with the
launch point corrected and reported "the rock throw looks much better". That
judgement is the one that counts, and it could not have been made earlier: the
launch point fault was large enough to mask everything else, because the rock was
being spawned 6.68 metres in front of the creature.

So the shape now stands on play rather than on argument, and the four figures it
rests on are settled unless somebody plays it again and disagrees: an arc of 0.25
of the distance thrown, real gravity at 980 centimetres per second squared, a
launch from the `hand_r` bone, and a landing on the floor the marker is drawn on.

---

## 2026-08-09 — A lobbed attack will not be thrown at something standing against the creature

**Affects:** `ACataclysmBruteCharacter::EnemyAbilities`, and the Brute section of
`docs/Cataclysm_GDD_v2.md`. Applied.

**Asked for by the project owner:** "I think we also need to implement a minimum
range for the rock throw, so it's not trying to throw a rock at you from point
blank range."

**One existed and refused nothing.** `RockThrow.MinRangeCm` was
`DesignedMeleeReachCm`, 90 cm. The Brute's capsule radius is 48 and the player's
is 42, so 90 cm is exactly the distance at which the two bodies are touching. A
minimum range set to contact distance can never turn an ability down.

**The rule, which generalises to any future lobbed enemy attack: an attack that
marks a circle must not mark the ground its own caster is standing on.** Below
`marked radius + caster body radius` the creature is inside the area it is about
to hit, which makes the attack a melee attack wearing a thrown attack's
telegraph. For the Brute that is 210 + 48 = **258 centimetres**.

**It is checked when the ability is chosen, not when it lands, and that is
deliberate.** The wind-up runs for a second afterwards, during which a player
walking at 400 cm/s can close four metres, so no minimum survives a determined
approach. Letting the throw land where it was marked is the rule the entry below
states for every telegraphed attack, and it is the same reasoning that settled
`#454`: a player who walks inside the minimum range during the wind-up has dodged
the throw, not found a special case.

**What could not be established.** `CLAUDE.md` requires looking up how shipped
games solve a problem before proposing a mechanic. That research was started and
**failed part way through on a session token limit**, so this figure is derived
from the project's own numbers and is not a genre answer. The derivation is
sound and the comparison is missing; if the number is ever disputed, that is the
work to do first.

---

## 2026-08-09 — The Brute's pacing: swings every 1.2 s, stomps every 8, throws every 12

**Affects:** `ARCHETYPES["Brute"]` in `sim/cataclysm_sim/enemy_stats.py`, both
Brute abilities in `sim/cataclysm_sim/enemy_abilities.py`,
`ACataclysmBruteCharacter`, and the Brute section of `docs/Cataclysm_GDD_v2.md`.
Applied. Closes the question issue #452 was opened for.

**All three were settled by playing, and they are one question rather than
three.** The project owner reported on 2026-08-08 that the creature "basically
uses an ability, waits a second, attacks once, uses another ability, waits a
second, attacks once". What that describes is the number of ordinary swings
falling between abilities, which no single number controls.

**The arithmetic, so the next creature can be aimed rather than guessed.** Every
attack is gated by the attack interval, and an ability that lands spends an
interval slot exactly as a swing does. With an interval I and abilities on
cooldowns C1 and C2, the ordinary swings between abilities are
`(1/I - 1/C1 - 1/C2) / (1/C1 + 1/C2)`.

| Interval | Stomp | Throw | Swings between abilities |
|---|---|---|---|
| 1.6 s | 5 s | 5 s | 0.56 — what was reported as the problem |
| 1.6 s | 10 s | 10 s | 2.12 — tried live |
| **1.2 s** | **8 s** | **12 s** | **3.00** — settled |

**The two cooldowns are no longer equal, and that changes the shape as well as
the count.** At 5 and 5 both abilities came up together and the creature used
them as a pair, which is why the original report describes a pair. At 8 and 12
they drift in and out of phase over a 24 second cycle, three stomps to every two
throws, so what the player meets is not a repeating pattern.

**Both cooldowns still clear their floors, and both floors are unchanged.** The
stomp must sit at or above the 5 second stun immunity window or it spends stomps
on a target that cannot be stunned; it now clears it by 3. The throw must sit
above the 2 second approach time or the Brute throws twice on the way in and
reads as a ranged enemy; it now clears it by 10.

**The interval is close to a hard floor that was measured rather than argued.**
`Attack_Biped_Melee_A`, the swing clip, is 1.0000 seconds long, measured in the
editor on 2026-08-09. Nothing rate-scales it: `PlayAttackAnimation` passes no
window, so it runs at its authored speed. At 1.2 seconds there is a fifth of a
second between one swing ending and the next starting. Below 1.0 the creature
would start a swing it had not finished.

**Two properties were given up deliberately, and neither is flavour.**

*The Brute no longer swings more slowly than a generic enemy.*
`ACataclysmEnemyCharacter` carries 1.5 seconds as its class default, so at 1.6
the Brute was slower than anything unconfigured and at 1.2 it is faster.
"Heavily armored slow melee. Can be outmaneuvered" survives that, because the
reading is carried by the 180 degree turn rate against every other enemy's 480,
and by the 250 cm/s patrol speed. Swing speed was a third supporting property
and has now been spent. The tests that asserted it now assert the direction it
was moved in, so putting it back fails and sends the reader here.

*The Brute's basic attack can no longer be telegraphed at any radius.* The
wind-up rule caps a telegraph at half the cycle, so a 1.2 second interval allows
a marker of only 0.70 metres against a 1.00 metre floor. At 1.6 it allowed 1.40.
Nothing changes in practice — the Slam reaches 0.9 metres and was already under
the floor — but the reason it draws no marker is now two independent reasons
rather than one. The stomp is unaffected, because an ability on a cooldown is
telegraphed against that cooldown.

**What this does not change.** The balance sweep in `sim/experiments.py` does not
read `enemy_stats.py` or `enemy_abilities.py` at all, checked by following the
imports, so it did not need re-running. The Brute's damage per swing is
unchanged; only how often it arrives.

---

## 2026-08-09 — A lobbed attack is real projectile motion, measured by a fixed flight time

**Affects:** `ACataclysmProjectile`, the Brute's Rip and Toss in
`sim/cataclysm_sim/enemy_abilities.py` and `docs/Cataclysm_GDD_v2.md`, and how
every future lobbed enemy attack states its speed. Applied.

**The shape was already right and the movement along it was not.** #459 gave the
rock a parabola. #462 then held the speed THROUGH THE AIR constant along that
parabola, so the steeper the path the less ground the rock covered per second.
Over a ten metre throw from a hand to the floor its horizontal speed was 80% of
the designed figure at launch, 97% halfway and 62% at the landing; over a two
metre throw, 97% and 41%. It crossed the ground early and then sank slowly onto
the marker. The project owner reported it from play: "it throws it super fast,
and then it appears slowly dropping above the impact zone."

**Nothing that is thrown moves that way.** Gravity acts downward and nothing acts
sideways, so a projectile holds its HORIZONTAL velocity from launch to landing
and only the vertical one changes. The visible consequences are the opposite of
what was happening: steady ground speed throughout, and a descent that speeds up.

**The rule.** A lobbed attack states a FLIGHT TIME, not a speed. Its ground speed
is the distance divided by that time, its arc height is `g × t² ÷ 8`, and both
follow from the one number. `Speed` remains the parameter for anything that
travels flat, which is all 398 player projectile rows.

**Why a time rather than a launch speed, an angle, or the minimum-speed
solution.** A ballistic solve needs exactly one input beyond the two ends and
gravity, and the four candidates are not equivalent. For a TELEGRAPHED attack the
flight is part of the warning: the marker appears when the wind-up starts, so the
player's window to leave is the wind-up plus the flight. Fixing the time makes
that window a designed number instead of an accident of how far away the player
happened to be standing. Under the old speed the same throw took 1.95 seconds at
ten metres and 0.55 at two, so the warning shrank exactly as the danger closed in.

Shipped games choose fixed delays deliberately for this. The Old School RuneScape
wiki documents both models in one game: most ranged attacks scale their hit delay
with distance, while the Tonalztics of Ralos, the Grasp and Demonbane spells and
the Volatile and Eldritch staff specials are each given "a fixed hit delay of 2
ticks regardless of the distance". Diablo 4's ground-circle attacks, already cited
in the entry below for the marker itself, land a set moment after the circle
appears rather than a moment that depends on range.

**Why not the engine's own solvers, having read them.** Unreal 5.8 has three, in
`Engine/Source/Runtime/Engine/Private/Kismet/GameplayStatics.cpp`.
`SuggestProjectileVelocity` (line 2640) fixes a launch speed and solves for the
angle; at earth gravity a 600 cm/s launch cannot reach ten metres at all, since
its maximum range is `v² ÷ g`, 3.67 metres. `SuggestProjectileVelocity_CustomArc`
(line 3035) fixes a direction and solves for the speed, which leaves the flight
time free. `SuggestProjectileVelocity_MovingTarget` (line 3073) is the one that
takes a flight time, and its solve is a single line — `(Δp − ½g t²) ÷ t` — but it
insists on a live `AActor*` to aim at, and a lob aims at a point on the ground.
The same two-line arithmetic is now in `ACataclysmProjectile::Fire`.

- https://oldschool.runescape.wiki/w/Hit_delay
- `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\Private\Kismet\GameplayStatics.cpp`

**Mass does nothing, and it is deliberately absent.** The project owner asked
whether projectile weight belonged here. In a vacuum every object falls at the
same rate whatever it weighs, so mass changes nothing about where a rock goes or
when it arrives; it matters only with air resistance, which this game does not
model. Unreal agrees. `UProjectileMovementComponent`, the engine's own projectile
mover, contains no reference to mass at all — a case-sensitive search of both its
header and its implementation returns nothing — and even its `AddForce` is added
straight to acceleration without being divided by one. What reads as weight is
the shape of the arc and the accelerating descent, which is what a fixed flight
time under real gravity produces. A weight field would have been a knob that did
nothing.

**The number, which is a judgement bounded on three sides.** Flight is 1.4
seconds. Gravity is Unreal's own `DefaultGravityZ`, 980 cm/s². Together they set
an arc of 240 cm above the chord, which over the full ten metre throw is 0.24 of
the range — within rounding of the 0.25 the design carried before, so the longest
throw kept its silhouette. They also set the landing speed, and the rock must not
outrun the Succubus's Soulfire at 1200 cm/s: at 1.4 seconds a ten metre throw
lands at 1121, and at 1.0 second it would land at 1244 and break that rule. And
the flight must be long enough to see, which is what #463 was about.
`Cataclysm.Brute.RockHangTime` moves it live. It replaced both
`Cataclysm.Brute.RockSpeed` and `Cataclysm.Brute.RockArc`, because gravity ties
the timing and the arc together and a second knob would have meant inventing a
gravity to go with it.

**What has not been judged by watching.** Short throws are now visibly steeper.
The arc height depends only on the flight time, so it is the same 240 cm whether
the rock travels two metres or ten; at two metres that is taller than the throw is
long. That is what a thrown object does when its hang time is held fixed, and it
is the one thing about this change that has to be looked at rather than argued.

**Not decided here.** Whether a lobbed attack should lead a moving target. The
project owner settled that on 2026-08-09: it should not. The marker promises a
place and the rock honours it, which is the rule in the entry below, and leading
the target would break the promise the circle makes.

---

## 2026-08-09 — A telegraphed thrown attack arcs onto its marked circle

**Affects:** the Brute's Rip and Toss in `sim/cataclysm_sim/enemy_abilities.py`,
`ACataclysmProjectile`, and how every future telegraphed ranged enemy attack is
shaped. Applied.

**A ground marker and a flat projectile are two different promises, and pairing
them is a known mistake.** The Brute's rock throw drew a lane on the floor and
then fired a flat, fast projectile at where the player stood a second earlier.
The marker said "this place, shortly", which the player answers by leaving, and
the attack delivered "this line, now", which the player answers by not standing
in front of it. Neither answer worked, and the project owner reported it from
play as the rock going in seemingly random directions.

**The rule.** A telegraphed enemy attack that marks a place must ARRIVE at that
place. If it is marked with a circle on the ground, it is lobbed and lands there.
If it travels flat and hits what it passes, it is marked with a lane and not a
circle. The shape of the warning and the shape of the delivery have to agree.

**Why the genre settles this rather than taste.** Diablo 4 pairs a ground circle
specifically with a lobbed, delayed-arrival attack: Mephisto's fire comets are
telegraphed with red circles showing where they will land seconds later, and
Mother's Judgement marks the player's position with a circular marker before
firing. The Game Developer article on enemy attacks and telegraphing names the
mismatch directly, observing that when players are trained to avoid ground
indicators but projectiles do not follow that visual language, the result reads
as inconsistent and unfair.

- https://skycoach.gg/blog/diablo-4/articles/mephisto-guide
- https://www.sportskeeda.com/mmo/how-beat-mother-s-judgement-diablo-4
- https://www.gamedeveloper.com/design/enemy-attacks-and-telegraphing

**What it settles that was previously a defect.** The aim point is fixed when the
wind-up begins and does not follow the player. Against a flat projectile that was
simply a miss, because a player walking at 4 metres per second leaves a 2.1 metre
blast within the 1 second wind-up. Against a lob onto a marked circle it is
correct: the marker promises the ground and the rock honours it, and the second
becomes the reaction window every ground telegraph in the genre gives.

**What is a judgement rather than derived.** The apex is one quarter of the
distance thrown, which is what a projectile launched at 45 degrees reaches, so
the trajectory is real rather than invented. Whether it READS as a heavy creature
heaving a boulder is not settled by physics and had not been judged by watching
when this was written. `Cataclysm.Brute.RockArc` set the fraction live for that
purpose.

**Superseded in part on the same day.** The apex is no longer stated as a
fraction of the throw, and `Cataclysm.Brute.RockArc` no longer exists. The entry
above replaced both with a flight time, from which the arc height follows. The
rule this entry sets — that a telegraphed attack marking a place must arrive at
that place — is unchanged and still holds.

**Not decided here.** Whether a lobbed attack should lead a moving target. It
does not today, and with the marker now honest there is an argument that it
should never need to. **Settled 2026-08-09:** it should not, for that reason.

---

## 2026-08-09 — The Succubus enemy modifier is renamed Beguiling

**Affects:** the Enemy Modifiers sheet of `All_Things_Cataclysm.xlsx`, and
therefore `game/Data/EnemyModifiers.csv` and `DT_EnemyModifiers`. Applied.

**A creature and a modifier must not share a name.** A Demonic enemy modifier
was called Succubus, and so is one of the seven vertical slice enemies. A
modifier is rolled onto an individual enemy, one per rarity above Common, from
its own Cataclysm's pool and the Generic one — so an Elite Succubus could roll
the Succubus modifier and be named "Succubus Succubus" anywhere both are shown.
That is not a display problem to hide: the two mean different things, and the
modifier does something the creature does not do innately.

**The modifier moved, not the creature.** The archetype name comes from the
design document's own enemy table and is used in `sim/cataclysm_sim/enemy_stats.py`
and in two test files. The modifier name exists in one workbook cell and what is
generated from it.

**Beguiling, because it matches the naming already in the sheet.** Most modifier
names are a descriptive noun phrase — Hellfire Aura, Unholy Sigils, Sacrificial
Bond — and Haunting shows a single adjective-as-noun is already in use. Beguiling
is the same form as Haunting and says what the modifier does without taking a
creature's name. The effect text is unchanged.

**The general rule this sets:** no enemy modifier may be named after an enemy
archetype, in any Cataclysm. That is stricter than the existing rule about
abilities duplicating a modifier's effect, which permits a clash across
Cataclysms because the creature can never roll it. A name is read by a person,
and two different things called the same thing are confusing either way.
`test_no_enemy_modifier_is_named_after_an_enemy` holds it.

**A shared word is not a shared name.** Abyssal Aura is a modifier and the
Abyssal Warden is an enemy. Names are compared whole, so that stands.

---

## 2026-08-08 — Where the Brute's rip crater goes, and how craters avoid accumulating

**Affects:** nothing in the design documents. Implementation detail, recorded
because issue #432 asked for four judgements and two of them turned out not to be
judgements at all.

**Where it goes was measured, not chosen.** Following both hands through
`Ability_RipNToss_Rip` with `AnimPoseExtensions`, `hand_l` bottoms out at 0.2833 s
and `hand_r` at 0.2644 s, both 2.9 cm off the floor. Their midpoint is 52.9 cm in
front of the creature and 0.6 cm off its centre line. That half-centimetre is the
check on the whole measurement: a two-handed rip is symmetric, so a midpoint
anywhere else would have meant the wrong bones or the wrong axis.

**When it appears was measured too** — the earlier of those two times, because
that is when the ground is first broken. Divided by the montage's play rate,
since the rock throw is compressed by about 1.67.

**How long it stays is a judgement, and it is set by an invariant rather than by
eye.** Four seconds, chosen to sit under the throw's own five-second cooldown.
That means one Brute can never have two craters, which answers the fourth
question — per-Brute or per-place — without a manager, a cap or an eviction rule.
Nobody has watched it; the number is free to move as long as it stays under the
cooldown, and there is a test holding exactly that.

**Craters reuse `ACataclysmDebrisBurst`** rather than getting an actor of their
own. That actor was built generic in #422 — it names no project content and knows
nothing about rocks — precisely so the next thing wanting a mesh on the floor for
a few seconds would not need its own. A crater is that mechanism with one piece
and no spread.

**The general rule this sets:** where an animation already contains the answer,
measure it rather than tuning it by eye; and where a lifetime could accumulate,
pick one that makes accumulation impossible instead of managing it.

---

## 2026-08-08 — A telegraph is counted in thinking passes, not compared against a clock

**Affects** `game/Source/Cataclysm/Character/CataclysmEnemyController.h` and
`.cpp`. Applied in full.

### The question

Issue #413. An enemy's ability landed on "the first thinking pass whose clock has
gone past the deadline". That sounds exact and is not. A timer callback runs on
the first FRAME past its deadline, so every pass carries up to a frame of
overshoot, and the overshoot on the pass that starts a wind-up is not the
overshoot on the pass that should land it.

Where a telegraph sits clear of a pass boundary this is invisible. Where it sits
ON one, a difference of a few milliseconds decides a whole quarter of a second.
The Brute's rock throw sits exactly there: a 1.000 second telegraph against a
0.250 second pass. Simulating the engine's own timer arithmetic over 500 jittery
frames landed it on the later pass 246 times out of 500.

### Counted, because counting cannot be affected by frame timing

The telegraph is turned into a number of passes once, when the wind-up begins,
and counted down. A wind-up begun on one pass lands exactly so many passes later
whatever the frame rate is doing, because nothing in that sentence involves a
clock.

The clock comparison stays as a second condition, and it earns its place twice
over. A hitch long enough to skip several thinking passes would otherwise hold an
attack open past its own telegraph while the count worked through. And every
automation test in this project moves the world clock by hand and calls the
thinking function directly rather than letting the timer run, so without it no
test could land an ability at all.

### Rounded up, never down

A telegraph that is not a whole number of passes takes the pass after it. The
Brute's 1.4 second stomp is six passes and lands at 1.5, which is what it already
did.

That direction is not arbitrary. The design states a telegraph as the time the
player has to walk clear, so an attack must never land sooner than it. Rounding
to the nearest pass would land the stomp at 1.25 -- a tenth of a second sooner
than the player was told they had.

The cost is that the real telegraph can exceed the designed one by up to a pass,
and the design's other bound on a telegraph -- that it fits inside half the
ability's cycle -- has to be checked against the real figure rather than the
designed one. `tools/tests/test_wind_up_lands_on_a_counted_pass.py` does that.

### A defect this uncovered in the same day's work

The ground telegraph marker added for #396 carries its own lifespan as a
backstop, and it was given the DESIGNED telegraph. For the stomp that is 1.4
seconds against a real landing at 1.5, so the marker took itself off the floor a
tenth of a second before the ring it warned about went off. The marker is now
shown for the effective telegraph.

---

## 2026-08-08 — A projectile's appearance is passed in by whoever fires it, and sized from the mesh's own bounds

**Affects** `game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.h` and
`.cpp`, `CataclysmBruteCharacter.h` and `.cpp`, and
`game/docs/enemy-source-assets.md`. Applied in full. Raises issues #421 and #422
for the two halves of #404 that are not this.

### The question

Issue #404. Every projectile in the game flew a grey engine sphere, including the
Brute's thrown rock, and the Paragon Rampage pack ships the rock itself. The mesh
could not simply be swapped in: `ACataclysmProjectile` is generic and all 398
rows of `game/Data/WeaponSkills.csv` fire through it, so a rock in its
constructor would have armed every player fire bolt with one.

The issue offered three homes for the choice: an optional argument on `Fire`, a
property on the firing character, or a column in the skill data.

### An optional argument on Fire

The data column was ruled out first, and not on taste. `game/Data/WeaponSkills.csv`
is generated from the design workbook and holds player skills; the Brute is not
in it and no enemy is, so a column there could not have served the caster that
actually needed one.

A property on the firing character would have meant the projectile asking its
instigator what it should look like, which couples a generic actor to a character
interface it otherwise knows nothing about.

So `Fire` takes a trailing `UStaticMesh*` defaulting to null. Every existing
caller is unchanged and keeps the sphere. The Brute passes its rock. The default
is what keeps the 398 player skills out of it, and there is a test whose whole
job is that the default stays.

### Sized from the mesh's own bounds, not from an assumed cube

The placeholder was scaled by `(BodyRadiusCm * 2) / 100`, where 100 is the size
of the engine's basic shapes. That is a fact about `/Engine/BasicShapes` and not
about meshes in general: applied to the pack's rock it drew it at 82.6 cm
half-width against a 40 cm body, more than twice too large.

Sizing now reads the mesh's own `BoxExtent` and scales so that the larger
horizontal half-extent equals `BodyRadiusCm`. For the engine sphere, whose box is
50 cm each way, that produces the same 0.8 it always did, and a test insists on
exactly that number so the rewrite is provably a no-op for everything that was
already working.

The **horizontal** half-extent rather than the bounding sphere radius: a bounding
sphere takes in the corners of the box, so the engine sphere would come out at
86.6 rather than 50 and every projectile in the game would have shrunk.

### What was split off

#404 as filed contained three independent things. The rock in flight is this.
The rock in the creature's hand during the wind-up, and the crater where it was
torn out, are #421 -- that needs the Rampage skeleton's socket list, which
nothing in this repository records, and a judgement about placement that has to
be made by watching. The five fragments on impact are #422, which needs a
decision about where impact effects live in general and carries a physics cost.

---

## 2026-08-08 — A telegraph marker is an engine shape drawn by the controller, and it runs to where the shot was aimed

**Affects** `game/Source/Cataclysm/AbilitySystem/CataclysmTelegraphMarker.h` and
`.cpp` (new), `CataclysmCharacterBase.h`, `CataclysmBruteCharacter.cpp`,
`CataclysmEnemyController.h` and `.cpp`. Applied in full.

### The question

Issue #396. The design's wind-up rule is 0.4 seconds plus radius over 3.5 metres
per second, where 3.5 is the slowest class's walk speed. That formula only means
anything if the player can see the area. Nothing in the project drew a ground
marker of any kind, so the Brute's Stomp waited the designed 1.4 seconds for its
3.5 metre ring and the player had to judge that ring from an animation.

Three things had to be decided: what draws it, who draws it, and how far a
projectile's lane reaches.

### What draws it: engine primitives, not a decal or a Niagara system

The issue listed all three as options. A decal needs a material and a Niagara
system needs a particle asset, and this project's own Content folder holds
neither -- either would be the first authored art asset in the repository and
would land in Git LFS.

`/Engine/BasicShapes` is what the player's body, every enemy's body and every
projectile already use for exactly this reason. A flattened cylinder reads as a
circle on the floor and a flattened cube reads as a lane. It costs the repository
nothing and replacing it later is a content change that touches none of the
behaviour.

### Who draws it: the controller, not the character

`ACataclysmCharacterBase::BeginEnemyAbilityWindUp` is the obvious hook and is the
wrong one. It is a virtual a subclass may override without calling its parent,
and the Brute's override does exactly that, so drawing there would be a rule each
enemy could silently opt out of by writing ordinary code.

`ACataclysmEnemyController` already owns the whole wind-up: it picks the ability,
fixes the point it was aimed at, knows when it lands, and knows when a stun
abandons it. Drawing there means every enemy telegraphs without doing anything,
and the marker is removed at all four places a wind-up can end.

### How far a lane reaches: to where it was aimed, not to the ability's range

The issue said the lane should run "to `Range`". Reading
`ACataclysmProjectile::Fire` shows it takes `RemainingRangeCm` from the distance
between the two points it is given, so a rock stops where it was aimed rather
than flying on to the ability's maximum. A lane drawn out to the full 10 metres
would mark four metres of ground that nothing is going to happen on, and a marker
that covers ground where nothing lands teaches the player to distrust the next
one. The lane is drawn to the aim point.

### The rule that keeps a marker honest

`FCataclysmEnemyAbility::MarkerRadiusCm` is filled from the same C++ constant the
ability's own damage uses -- `StompRadiusCm` for the ring, `RockThrowRadiusCm`
for the lane -- and `tools/tests/test_telegraph_markers.py` checks that it is
filled from the constant by name rather than from a literal. A marker showing a
different circle from the one that hurts is worse than no marker, because the
player would have learnt to trust it.

The one metre floor from the design document is enforced in one place, in the
marker itself, and pinned against `SMALLEST_USEFUL_MARKER_METRES` in the model.
The Brute's ordinary slam reaches 0.9 metres and so draws nothing, which is the
design working rather than an omission.

---

## 2026-08-08 — The player's movement speed is an attribute, and its placeholder is the shared class stat line

**Affects** `game/Source/Cataclysm/Character/CataclysmPlayerCharacter.h` and
`.cpp`. Applied in full. Raises issue #417, which the project owner has to
answer.

### The question

Issue #391: `ACataclysmPlayerCharacter` configured its movement component and
never assigned `MaxWalkSpeed`, so the player moved at Unreal's engine default of
600 cm/s. The design gives the three Demonic classes 4.6, 3.5 and 4.0 metres per
second and none of those reached the game. Two things had to be settled: what the
player walks at when no class has been chosen, and whether the speed is a number
or a stat.

### It is a stat, because everything in the design already treats it as one

`game/Data/Affixes.csv` carries an increased movement speed suffix, four boot
bases in `ItemBases.csv` carry one as an implicit, Agility scales it in
`Attributes.csv`, and both enchantment tables move it. A pawn holding a fixed
number would leave every one of those changing a stat and nothing about the
character -- the same defect one step later.

`UCataclysmCombatAttributeSet` already had a `MovementSpeed` attribute, in metres
per second, starting at 4.0 and replicated. Nothing read it. So the work was to
connect what already existed rather than to build anything: the pawn subscribes
to that attribute's change delegate when the ability system comes up, and writes
`MaxWalkSpeed` from it.

### The placeholder is 4.0 metres per second, and it is not a new number

There is no class selection yet, so something has to be walked at before a class
exists. 4.0 is the shared `Default` line in `game/Data/ClassStats.csv`, which is
what a class inherits when it does not override movement speed. It is also
exactly what the attribute already started at, and what the Masochist uses. So
the placeholder is a designed figure rather than another arbitrary one, and the
pawn does not visibly change speed when the player state arrives.

### A zero movement speed is refused rather than written

An ability system holding no combat attribute set reports zero rather than
failing. Writing that through would leave a character who cannot move with
nothing on screen to say why. Refusing it leaves the last usable speed, which for
a pawn whose attributes have not arrived is the placeholder. Nothing in the
project roots the player; a designed root would stop movement through the
movement mode rather than by setting a speed of zero.

### What this broke, and was not fixed here

The Brute chases at 500 cm/s, set by the project owner on 2026-08-07 by playing
it against the 600 the game was then using. At 400 the same figure makes the
Brute faster than the player. It cannot simply be scaled down: `ABP_Brute`
chooses the four-legged chase gait above 375 cm/s, and the Ritualist is designed
at 350, so no chase speed both triggers that gait and can be walked away from by
every class. That is a design question and it is issue #417.

---

## 2026-08-08 — A stun is a state the target is in, not a row in the status effect table

**Affects** `game/Source/Cataclysm/AbilitySystem/CataclysmSkillEffects.h` and
`.cpp`, `CataclysmEnemyController.h` and `.cpp`, `CataclysmPlayerController.h`
and `.cpp`, `CataclysmBruteCharacter.h` and `.cpp`, and the Tags sheet of
`All_Things_Cataclysm.xlsx`. Applied in full. Raises issues #395 and #396 for the
two parts that could not be answered.

### The question

The Brute's Stomp is designed to stun for 1.5 seconds and nothing in the project
could stun anything. Issue #371 asked for the stun and proposed a specific home
for it: "a `Stun` row in `game/Data/StatusEffects.csv` so its duration is data
rather than a constant".

### The proposed home turned out to be the wrong one

Three findings, in the order they closed the question.

**That file is generated, not authored.** `game/Data/StatusEffects.csv` is
produced by `tools/generate_datatables.py` from the Buffs, Debuffs and DoTs
sheets of `docs/All_Things_Cataclysm.xlsx`. A row cannot be added to it directly;
it would have to be added to one of those three sheets.

**Its `EffectKind` column only ever holds `Buff`, `Debuff` or `DoT`**, across all
50 rows. A stun is none of the three, so it would have had to be filed under a
kind that misdescribes it or the table would need a fourth kind.

**The design already separates the two.** `Cataclysm_GDD_v2.md:1568` draws an
explicit table of what the anti-stun-lock rule covers, and it puts stun and
knockdown on one side and slows such as Cripple on the other:

| | Covered | Why |
|---|---|---|
| Stun | Yes | The target cannot act at all |
| Slow, such as Cripple | No | Slower is still able to act |

Cripple *is* a Debuff row in that table. Stun is not in the table at all — it is
in section VI with the three anti-stun-lock rules, which are combat rules rather
than a named effect a skill applies.

**So the duration comes from where every other Brute number comes from.**
`StunSeconds: 1.5` on the Stomp in `sim/cataclysm_sim/enemy_abilities.py`, copied
into a C++ constant and pinned by `tools/tests/test_brute_matches_the_model.py`.
That is the shape the project already uses for the Brute's speed, reach, notice
radius and attack interval, and it is guarded the same way.

### Two new tags, in a new family, typed into the sheet by hand

The stun is held as a gameplay tag for a duration, because that is how every
other timed effect in this project already works —
`UCataclysmSkillEffects::ApplyTagForDuration` builds a transient duration effect
and `HasTag` answers "is it up?". Nothing new had to be written for the timing.

Two tags were added: `State.Stunned` and `State.StunImmune`.

**Not under `Status.`**, which is the branch generated one-per-row from the
effect sheets. A hand-typed tag there would sit in a namespace that is otherwise
entirely derived, and the generator treats a hand-typed `Status.*` as an override
of a generated one rather than as an addition.

**A new `State.` family instead**, typed into the Tags sheet of the workbook.
There is precedent: the five `Cooldown.*` tags are machinery typed into that
sheet by hand, and they sit outside the sheet's Excel table exactly as these two
now do.

**Not declared natively in C++**, which would have avoided touching the workbook
at all. `UCataclysmSkillEffects::BurnTag` states the project's reason for
refusing that: a native declaration creates the tag whether or not the workbook
still lists it, which hides exactly the disagreement that matters.

### The immunity window is the part that existed nowhere

`sim/cataclysm_sim/damage.py:150` says in terms that it resolves one hit with no
clock, that it therefore implements only the damage threshold and boss immunity,
and that **the game enforces the five second window**. The game had no stun, so
nothing enforced it anywhere. This is its first implementation.

It is granted as a second tag at the moment the stun lands, so it belongs to the
target rather than to whoever swung. Two Brutes cannot take turns.

**The window starts when the stun starts, not when it ends.** Five seconds of
immunity against a 1.5 second stun therefore leaves 3.5 seconds in which the
target can act and still cannot be re-stunned. That is the intent: the rule
exists to stop chain-stunning, not to add a cooldown to the player's recovery.

### A stun cancels a wind-up rather than pausing it

`ECataclysmBrainAction` recorded that the design gives the wind-up five rules and
that the fifth — interrupting cancels it — was unimplemented "because nothing in
the project can interrupt anything yet". A stun is the first thing that can.

**Abandoned, not paused.** Left set, the wind-up's landing deadline would still
be in the past when the stun wore off, so the first pass afterwards would land a
stomp whose telegraph the player had already survived.

**And not put on cooldown.** The ability's cooldown is stamped when it lands, so
an attack interrupted before it landed was never spent and comes round again at
once. An interrupt that also cost the enemy its Heavy would be paying the player
twice for one action.

### What could not be answered

**Boss immunity is not implemented, because nothing in the project is a boss.**
The third anti-stun-lock rule is "a boss cannot be stunned at all". There is no
flag, class, tag or data column anywhere in `game/Source/` that identifies one.
Issue #395. Nothing is wrong today only because no boss enemy exists yet.

**Nothing draws a ground telegraph marker.** Issue #371 bundled that with the
stun; it is a separate concern and is now issue #396. The wind-up timing already
honours the design's formula, but the area the formula promises the player can
walk out of is not drawn, so the only warning is still the animation. That
matters more now than it did: misreading a Stomp costs control of the character
rather than only health.

---

## 2026-08-07 — An enemy can roam, but only if it asks to, and the Brute notices at seven metres not fifteen

**Affects** `game/Source/Cataclysm/Character/CataclysmEnemyController.h` and
`.cpp`, `CataclysmCharacterBase.h`, `CataclysmBruteCharacter.h` and `.cpp`.
Applied in full. Raises issue #383 for the part that could not be answered.

### The question

The Brute stood still whenever nothing was in range, and noticed the player from
fifteen metres. Both were asked to change: roam when there is nothing to fight,
notice from closer, and go back to roaming when the player leaves.

### Roaming is opt-in, and that is the substantive decision

`ECataclysmBrainAction` gained a fourth value, `Roaming`. The obvious way to use
it would be to make every enemy roam when it has no target. That is wrong for
two of the three characters `ACataclysmEnemyController` drives:

- **A summoned imp** exists to fight what its summoner is fighting. One that
  strolled off between fights would be a bug wearing the clothes of a feature.
- **The Corrupted Sentinel** is designed stationary.

So `ACataclysmCharacterBase` gained a fifth controller-facing virtual,
`RoamRadiusCm()`, defaulting to zero, and zero means never roam. Only the Brute
overrides it. The evidence that this was the right shape is that all four
existing automation tests asserting a character with nothing in sight is `Idle`
still pass **unedited**, and breaking the default to a non-zero value makes five
tests fail.

`Roaming` was appended to the enumeration rather than inserted beside `Idle`,
which it reads like. It is a `UENUM`, so inserting would renumber `Chasing` and
`Attacking`.

### The Brute notices at 700 cm

Derived from the Brute's own two designed numbers as the ground it covers in one
attack cycle: `move_speed x attack_interval`, 2.5 m/s x 2.8 s = 7 m. The meaning
is that noticing the player leads to a fight within one cycle rather than to a
walk that never arrives.

**The old 1500 cm was not arbitrary and this log said so.** The judgement table
in the 2026-08-04 entry below records it as "the same distance Subjugate reaches,
which is the longest range the designed Demonic skills use", and `Range=15` in
`game/Data/WeaponSkills.csv` is indeed the joint longest. That reasoning is
symmetry: a monster notices you from as far as you could hit it. It suits a
caster. It does not suit an enemy moving at 2.5 m/s against a player moving at
3.5 to 4.6, which cannot close fifteen metres against a player who does not want
it closed.

**The check that decided it** was the sandbox. `ACataclysmGameMode` spawns the
Brute 1200 cm from the player start, so at 1500 cm it notices the player as the
level opens, never roams, and the new behaviour cannot be seen at all.

**Genre precedent agrees. It was gathered after the number was derived, as a
check on it rather than as its basis**, which matters because none of it is
strong enough to derive from. Reported figures, with what kind of source each
came from:

| Game | Reported aggro radius | Source quality |
|---|---|---|
| Path of Exile | 8 m for ordinary monsters; 3 m for Defensive and Meat Shield | community documentation |
| Path of Exile | 5.0 m in one official patch note | official, but about one specific change |
| Path of Exile 2 | 8 m, raised from 6 m | community documentation |
| Diablo II | 35 subtiles hardcoded default, 55 hard ceiling | decompiled game source and `monstats.txt` |
| Diablo III, Diablo IV, Last Epoch, Grim Dawn | **no published figure found** | searched, nothing usable |

So the band the genre uses for an ordinary monster is roughly 5 to 8 metres, and
7 sits inside it. Treat that as corroboration and not as a citation: the Path of
Exile numbers are community measurements rather than published constants, and
Grinding Gear Games has never documented a base aggro radius.

Diablo II is the most useful of the five for a different reason than its number.
`aidist` is a per-monster column, which is the shape this project already copied
when it made `NoticeRadiusCm` a per-class property rather than one constant.

**Nothing was found for a leash distance, a wander radius or a pause in any of
the five.** The pause of 2 seconds and the roam radius of 600 cm are therefore
judgements, not precedent, and they are labelled as such in the code. The roam
radius is at least bounded by arithmetic rather than taste: 4000 cm of floor,
spawning 1200 cm out, less the 48 cm agent inset, leaves about 752 cm of
headroom to the navigation bounds.

### What this does not answer

**Six enemies still have no notice radius**, and the derivation used here cannot
give them one — applied to the stationary Corrupted Sentinel it produces zero.
Issue #383 asks for the rule.

**There is still no leash.** A character that has noticed the player follows for
as long as the player stays inside its notice radius, however far from home that
takes it. Roaming now gives every controller an anchor
(`ACataclysmEnemyController::RoamAnchor`), so the missing half is only a rule for
when to abandon a chase and walk back. Noticing and un-noticing are also the same
distance, so a player walking the boundary makes the Brute flip between chasing
and roaming; hysteresis is the usual fix and is not built.

---

## 2026-08-07 — Third-party packs stay out of git, and authored enemy work lives under Content/Enemies

**Affects** `.gitignore`, `game/README.md`, and a new `game/docs/content-layout.md`
which is the convention document. Applied in full. Issue #370.

### The question

Six free Paragon character packs, 17.31 GB, were imported to supply the art for
the seven Demonic vertical slice enemies. Three things were undecided:

1. Do third-party packs go in git?
2. If not, what happens if a pack is delisted?
3. Where does work *derived* from them live — a retargeted animation, an
   animation Blueprint, a material instance?

The third was blocking. The Brute was built entirely in C++ partly because there
was nowhere agreed to save a Blueprint.

### What the sources actually say, which is less than expected

**Epic has never published a recommended Content folder structure.** It has one
page, on asset naming, and that page says it reflects how Epic names assets in
the In-Camera VFX Production Test — a virtual-production sample. That is why it
has rows for `OCIO_` and `SNAP_` and none for Gameplay Ability or Input Action.
Every "Epic-recommended folder layout" circulating online is community inference.

**The community standard is the Gamemakin UE Style Guide**, which does cover
folder structure. Its headline rule is a `Content/ProjectName/` wrapper.

**Neither has a `ThirdParty/` or `External/` folder.** That layout is repeated on
blogs and was asserted twice during this research; the strings do not appear in
the style guide at all. The guide's separation mechanism is a side effect of the
project wrapper: anything outside your project folder is not yours.

**Epic's own Lyra follows neither consistently.** It uses `B_` for Blueprints
against Epic's documented `BP_`, and uses the type folders the style guide
forbids. Where it is the only source that has faced an asset type — Gameplay
Abilities, Enhanced Input, Niagara — this project follows it.

### What was decided

**No `Content/Cataclysm/` wrapper**, departing from the style guide's headline
rule. Its stated rationale is migration safety, so that migrating content into
another project cannot overwrite same-named assets; this project will never do
that, so the benefit does not apply while the cost does — every hard-coded
`/Game/` path in the C++, the generators, the config and the tests. Continuous
integration does not run the C++ tests, so a missed path would ship silently. The
guide also defers to a project's existing conventions, and Lyra has no wrapper.

**Third-party packs stay where Fab installs them, at the Content root, and out of
git.** Fab offers no destination setting, and moving a pack leaves redirectors
and risks breaking in-place updates — a cost paid on every pack update to buy a
folder name. Keeping them out of git is a cost decision: 17.31 GB against the
10 GiB of Git LFS storage and bandwidth a GitHub Free or Pro account includes per
month, drawn on by every clone and every continuous integration checkout.

**Authored enemy work goes in `game/Content/Enemies/<Cataclysm>/<Enemy>/`**, so
the Brute's is `game/Content/Enemies/Demonic/Brute/`. The Cataclysm folder names
are the eight `Element.*` tag names from `game/Config/Tags/CataclysmTags.ini`,
verbatim. Retargeting copies rather than edits — Unreal's own operation is
"Duplicate Anim Assets and Retarget" — so the destination is a free choice, and
the vendor folder is the one choice that loses the work.

**If a pack is delisted, the recovery is an offline archive**, not a second
repository. One archive per pack on the development machine plus one off-machine
copy. Not built yet; see below.

### The rule that fails silently, and what now catches it

An asset saved inside `game/Content/ParagonRampage/` is dropped by `git add` with
no error and no warning, and the loss surfaces on somebody else's clone.

**Nothing guarded the ignore rule itself.** Deleting the one line from
`.gitignore` passed the entire test suite and the linter.
`tools/tests/test_third_party_packs_are_not_committed.py` now checks four things:
the vendor list is readable, every pack folder present on disk is ignored by
`git check-ignore`, no vendor file is already tracked, and
`Enemies/Demonic/Brute/` is *not* ignored. Verified by deleting the pattern and
watching it fail.

**The vendor list is written down once.** `.gitignore` holds it between two
marker comments, `tools/third_party_content.py` reads it, and both that test and
`tools/tests/test_game_readme_is_true.py` read that. Previously the test and
`.gitignore` each decided independently that a third-party folder is one starting
with "Paragon", so a pack from any other vendor would have been counted as
project content by one and committed by the other.

### One figure this corrects

`.gitignore` and issue #370 both said GitHub's free Git LFS allowance is 1 GB. It
is **10 GiB** of storage and 10 GiB of bandwidth on a Free or Pro account
(https://docs.github.com/en/billing/concepts/product-billing/git-lfs). The
conclusion does not change — 17.31 GB exceeds it, and bandwidth is drawn on every
clone — but the number was wrong.

### What is not settled

- **The offline archive does not exist yet.** Until it does, a delisted pack is
  an unrecoverable loss of the art for all seven vertical slice enemies. The
  measurements in `game/docs/enemy-source-assets.md` survive; the art does not.
- **Whether the Fab licence permits committing packs to a public repository.**
  The relevant clause was found only in a forum reproduction, not in the licence
  itself, so it is recorded as unverified. It does not affect this decision,
  which keeps them out of git on cost grounds regardless.
- **`Characters/`, `Abilities/`, `Items/`, `Empire/`, `Effects/`,
  `MaterialLibrary/`, `UI/` and `Audio/`** are reserved names in
  `game/docs/content-layout.md`, not folders. They get created when something
  needs them.

---

## 2026-08-06 — The seven vertical slice enemies are cast from the free Paragon character packs

**Affects** issues #17 (3D asset generation pipeline) and #18 (animation
pipeline). No design document changes: this decides which existing art plays each
enemy, not what the enemies are. All six packs were imported into
`game/Content/` on 2026-08-07 and every claim below is now measured from the
imported assets. The per-asset reference is `game/docs/enemy-source-assets.md`.

### One enemy fails its timing budget, and that comes first

**The Corrupted Sentinel does not fit.** Its firing animation runs 2.40 seconds
against a 2.0 second attack interval. Every other enemy passes. That needs a
decision and is filed as its own issue; see "What is not settled" below.

**Two predictions in the first draft of this entry were wrong, and the
measurements reversed both.** The draft said the Imp was the enemy at risk and
that the Corrupted Sentinel was a weak match because no Paragon character stands
still. The Imp passes with room to spare, and the Minions pack contains a siege
minion built to root itself in place and fire. The failure is the siege minion's
animation being too slow, not the choice of character.

### The question

Issue #17 assumes every model in the game is generated, and names asset
production as the largest single risk to the project shipping. It proposes
trialling Trellis 2 locally against hosted tools such as Meshy and Tripo, then
writing a pipeline. That is a multi-week investigation standing in front of the
first enemy existing.

The vertical slice needs seven enemies. The project owner already has the free
Paragon character packs in their Fab library.

### What was decided

Cast the seven vertical slice enemies from Paragon characters rather than
generating them.

| Enemy | Paragon mesh | Triangles | Confirmed by |
| :-- | :-- | :-: | :-- |
| The Imp | `Minion_Lane_Melee_Dawn` | 9,786 | Ten attack animations at 0.80–0.83 s, inside the 0.9 s interval |
| The Succubus | `SM_Countess` | 78,957 | Primary attack in three speeds: 0.60, 0.90 and 1.50 s |
| The Hellhound | `IggyScorch`, Scorch half | 92,562 | `Scorch_Primary_Fire_Med` 0.97 s, and a fire breath with start, loop and end |
| The Brute | `Rampage` | 52,174 | `Ability_GroundSmash_Start` 0.83 s gives the stomp a holdable wind-up |
| The Corrupted Sentinel | `Minion_Lane_Siege_Dawn` | 10,379 | Roots itself: `PlantedIntro`, `Idle_Planted`, `Fire_Planted`, `PlantedExit` |
| The Abyssal Warden | `GruxMolten` | 82,225 | The molten skin exists as its own mesh, not a material swap |
| The Gatekeeper | `Sevarog` | 85,163 | Pack is present. Carries `Stage_1` to `Stage_4` marker poses |

Three candidates were considered and rejected. **Khaimera** is fast and feral
rather than heavy, so he does not read as the Brute; he suits an elite Hellhound
variant instead. **The Fey** floats and casts, so she cannot play the Imp, which
has to be a fast ground swarmer. **Iggy** is a single character carrying a full
animation set, which is more than a swarm unit needs; the plain minions cost less
per spawned body.

### This does not answer #17

#17 stays open and stays unanswered. It covers gear for 24 classes, 15 weapon
types across eight rarity tiers, enemies for eight Cataclysms, and city and
dungeon environment art. Paragon supplies none of that. What this decision does
is take the vertical slice's seven enemies out of #17's scope, so the generation
pipeline investigation no longer stands in front of Phase 1.

### The measurement, run 2026-08-07

Enemy attack intervals are fixed, in `ARCHETYPES` in
`sim/cataclysm_sim/enemy_stats.py`. They cap how long an attack animation can
run. Every duration below is the `SequenceLength` asset registry tag on the
imported animation, read through the editor:

| Enemy | Attack interval | Shortest usable attack animation | Length | Verdict |
| :-- | :-: | :-- | :-: | :-- |
| Imp | 0.9 s | `Attack_A_SetA`, and nine more | 0.80 s | Passes |
| Hellhound | 1.1 s | `Scorch_Primary_Fire_Med` | 0.97 s | Passes |
| Corrupted Sentinel | 2.0 s | `Fire_Planted` | 2.40 s | **Fails by 0.40 s** |
| Abyssal Warden | 2.4 s | `PrimaryAttack_LA` | 1.13 s | Passes |
| Succubus | 2.6 s | `Primary_Attack_Normal` | 0.90 s | Passes |
| Brute | 2.8 s | `Attack_Melee_A` | 0.97 s | Passes |
| Gatekeeper | 3.0 s | `Swing1_Medium` | 1.13 s | Passes |

**The Imp passes because the minion ships variant sets.** Its plain `Attack_A`
through `Attack_D` run 1.00 s and would have overrun. The `_SetA` variants run
0.80 s and the `_SetB` variants 0.83 s, giving ten attacks inside the 0.9 second
interval with room left.

**The Corrupted Sentinel is the one that fails.** The siege minion's
`Fire_Planted` and `Fire_Planted_B` are both 2.40 s against a 2.0 s interval, and
its unplanted `Fire_A`, `Fire_B` and `Fire_C` are 2.80 s, which is worse. The
options are a playback rate of 1.2, cutting the animation, or lengthening the
Sentinel's attack interval to 2.4 s. The last one lowers its damage output, so it
is not free.

**Wind-up durations were not measured and remain unknown.** The telegraph rule in
section X of `docs/Cataclysm_GDD_v2.md` caps the wind-up at half the attack
interval, which is 1.0 s for the Sentinel up to 1.5 s for the Gatekeeper.
`SequenceLength` gives the whole animation, not the point inside it where the
damage lands. Finding that needs the animation notifies read one at a time, and
it is separate work.

### What this costs

**These are the most widely used free assets in Unreal.** A game sold with
unmodified Grux and Rampage will be recognised as having done that. For a
vertical slice proving the combat loop it does not matter. For anything sold it
does, so this covers Phase 1 only and Phase 2 has to plan replacement.

**17.31 GB of disk, measured after import.** The six packs are 1.35 GB for
Sevarog up to 4.76 GB for the Minions pack. The machine has 1157 GB free, so this
is not a constraint.

**Eight gigabytes of video memory is the binding hardware limit** on the
development machine's RTX 3070. Paragon characters ship with 4K textures. Seven
distinct characters plus a swarm needs a texture size cap set at import. That is
a settings problem rather than a blocker.

**Triangle counts say the swarm choice was right by a factor of about nine.** The
melee minion playing the Imp is 9,786 triangles. The hero characters run 52,174
for Rampage up to 92,562 for Iggy and Scorch. The design commits to twenty Imps
standing within reach of one player, which is roughly 196,000 triangles with the
minion and roughly 1.85 million with a hero mesh.

**Licensing was not verified.** Epic released the Paragon characters at no cost
for use in Unreal Engine projects. Read the current Fab listing terms before
anything ships rather than relying on that summary.

### What is not settled

- **The Corrupted Sentinel's timing.** Its firing animation is 2.40 s against a
  2.0 s interval, and every alternative in the pack is slower. #369 sets out the
  three ways out and what each costs.
- **Where the damage lands inside each animation.** `SequenceLength` gives the
  whole animation. The telegraph rule needs the wind-up, which is the part before
  the hit, and that has to be read from the animation notifies one asset at a
  time. Until it is, no telegraph duration is confirmed for any enemy.
- **How physically wide any of these enemies are.** Only the Imp has a measured
  `body_radius`; the other six take the 0.48 m default, which is roughly a person
  and contradicts calling the Warden "massive" and the Gatekeeper "towering".
  #366. Triangle counts are now known but say nothing about width.
- **Whether enemies keep their own Paragon skeletons or are retargeted onto one
  shared skeleton.** #18 asks for a single locked skeleton standard. The imported
  packs use per-character skeletons with bone counts from 39 on the siege minion
  to 207 on Rampage, so one shared skeleton would mean retargeting all seven.
  Enemies never share animations with the player, so keeping the Paragon
  skeletons is workable, but #18 has to say so rather than leave the two answers
  to contradict.
- **Whether the Gatekeeper's four stage poses are usable as its phases.** Sevarog
  carries `Stage_1` through `Stage_4`, each 0.03 s, which are marker poses rather
  than animations. The design calls the Gatekeeper a multi-phase boss where each
  phase introduces new mechanics. Whether those poses correspond to anything
  useful has not been checked. #354 designs the phases.

---

## 2026-08-06 — The Brute's ordinary hit sits exactly on the stun threshold, so only its stomp stuns

**Affects** `docs/Cataclysm_GDD_v2.md`, a new "The Brute" subsection in section X
and one new rider in section V's list; `sim/cataclysm_sim/enemy_abilities.py`;
`sim/cataclysm_sim/enemy_stats.py`; and `tools/generate_datatables.py`. Applied
in full. Issue #351, a child of the enemy design epic #29.

### The question

The Brute is the first thing in the game that stuns the player, and the
anti-stun-lock rule in section VI is written for a "target" without ever saying
that a player is one. Issue #351 asked for the stomp's radius, wind-up, stun
duration and pacing, and for what "can be outmanoeuvred" means mechanically.

### What was decided

**The three anti-stun-lock rules apply with the player as the target**, and each
of the three does real work on this enemy.

**Its ordinary slam lands at exactly 10% of the reference build's effective
health, which is exactly the stun damage threshold.** An Elite Brute at
difficulty tier 8 kills that character in 10 hits. A stun sitting precisely on
its own threshold is decided by rounding, so **the slam does not stun at all**
and only the stomp does. This was not arranged; it was found by computing the
figure. It is the clearest evidence the threshold rule is doing work.

**The stomp is a Heavy-slot ability at the Heavy slot's 250%**, which lands at
25% of the same pool — two and a half times the threshold, so it stuns through
more mitigation than the reference build carries.

**Its cooldown is the 5 second stun immunity window, not the Heavy slot's.** The
whole Heavy cooldown band is 1 to 4 seconds, entirely inside the window, so a
Brute stomping on the Heavy cadence would spend most of its stomps on a player
who cannot be stunned. **Any ability whose stated effect is a stun sits at least
5 seconds apart, whatever slot it is in.** That is a new general rule.

**It stuns for 1.5 seconds, the longest any designed player skill grants.** The
four skills that stun run 0.75, 0.75, 1.0 and 1.5. An enemy holding the player
still for longer than the player's own best hold is the failure the whole
section is written against.

**The stomp takes the largest marker its attack interval allows, 3.5 metres**, so
its wind-up is exactly half that interval. That is the same choice the Succubus's
bolt makes and it is now a pattern: an enemy's signature heavy attack takes the
largest marker its own attack interval allows. Sizing it by the 5 second cooldown
instead would allow 7.35 metres, which the design document rules out by saying
the larger kind of telegraph "is what makes a mini-boss or a boss feel different
from a Brute".

**It is a ring rather than a cone**, because a cone on an enemy that turns at
half speed is answered once and never again.

**A new field, `turn_rate_degrees`**, defaulting to the 480 every enemy is built
with in C++. The Brute turns at 180. The ceiling on it is derived: a player
circling at the Brute's own 0.9 metre reach sweeps 223 degrees per second even in
the slowest Demonic class, so anything under that can be got behind by every
build. 180 is the round figure inside it.

**A clarification the Brute forced.** Being in the telegraph table's Yes column
says how big a marker an enemy could draw, not that every attack draws one. The
Brute's slam reaches 0.9 metres, under the one-metre floor, so it gets no marker
even though the Brute can telegraph. `is_telegraphed` was checking only the
largest marker the cycle allowed and not the ability's own radius; it now checks
both.

**A new rider, `StunSeconds`.** There was nowhere to put a stun duration. It goes
in `tools/generate_datatables.py`, in section V's rider list, and in the enemy
module.

### What could not be settled here

**There is no `Stun` row in `game/Data/StatusEffects.csv`**, and the four player
skills that stun state their durations only in prose. So the stomp expresses its
stun as a `StunSeconds` rider rather than the more consistent `Effect=Stun`.
Issue #363.

### Evidence

`tools/tests/test_enemy_abilities.py` grows from 44 tests to 54. The 10% figure
is recomputed against `sim/cataclysm_sim/reference_build.py` rather than quoted,
the 1.5 second ceiling is recomputed by reading stun durations out of the skill
descriptions, and the 223 degree ceiling is recomputed from the class stat table
and the Brute's own reach. All 21 deliberate breaks were caught by the intended
test.

**One break was not caught on the first attempt**, and for the third time in a
row it was the same shape of mistake: the subsection states the circling rate
twice, in a table and in prose, so changing one left the other satisfying the
test. The test now checks every "N degrees per second" figure in the subsection
against the three real ones.

---

## 2026-08-06 — The Hellhound's charge must beat walking, and its fire burns everything including itself

**Affects** `docs/Cataclysm_GDD_v2.md`, a new "The Hellhound" subsection in
section X and one new rider in section V's list;
`sim/cataclysm_sim/enemy_abilities.py`; and `tools/generate_datatables.py`.
Applied in full. Issue #350, a child of the enemy design epic #29.

### The question

The Hellhound is the fastest thing in the vertical slice and the only source of
friendly fire the design describes. Issue #350 asked for the charge's range,
wind-up, speed, whether it can turn and what a miss costs; the trail's width,
duration, damage and rate; whether the trail outlives the creature; and its
telegraph.

### What was decided

**Two abilities: a bite and a charge.** The fire trail is riders on the charge,
exactly as the player's Flamedart already carries them, not a third ability.

**Its bite reaches contact and no further**, 0.9 metres, which is the player's
0.42 body radius plus its own 0.48. Through the ring arithmetic from the Imp's
design that makes **one rank of Hellhounds five**, against twenty Imps. That is
the mechanical difference between a charger and a swarm, and it means the two
enemies the telegraph section groups together as "the two swarm enemies" do not
arrive in comparable numbers. That grouping is only about both being too fast to
telegraph a basic attack.

**A charge must go further than the creature could walk while winding up.** This
is the new general rule and it is what sets the range. The wind-up is 0.83
seconds, during which the Hellhound stands still, and at 7.5 metres per second it
could have covered 6.2 metres by simply walking. A charge shorter than that is
strictly worse than not winding up at all. Ten metres clears it, and ten is also
what three of the four player Charge-mode skills use.

**The corridor is 1.5 metres to either side**, the narrowest radius any player
Charge-mode skill uses. A wide corridor is a wall to run from; a narrow one is a
lane to step out of, which is what a telegraph is for.

**The cooldown is 5 seconds**, the Movement slot's typical cooldown. That is what
puts the charge on the telegraph rule's cooldown clock rather than its 1.1 second
attack interval, which the Attack Telegraphs subsection already names this enemy
as the example of.

**A miss punishes itself and needs no recovery rule.** The Hellhound is committed
once the wind-up starts, so it runs the full ten metres and ends up ten metres
past the player facing away. Covering that ground again is 1.33 seconds at its
own speed. The punish window falls out of the geometry.

**One new rider, `GroundHitsAllies`.** Burning ground normally hurts only the
caster's enemies, which is what all eight player skills that leave some want. The
Hellhound's trail is the only thing in the game that sets this, and it is what
makes the trail the single source of friendly fire. It is a property of the
ground rather than a change of side, in the same way the Madness debuff is an
attitude override rather than a third team.

**The trail burns the Hellhound too.** That is the simple version of the rule
rather than an exception bolted onto it, and it earns its place: a Hellhound
whose return path crosses its own lane takes its own fire.

**The trail outlives the Hellhound**, and the two cases follow from rules already
stated. Killed during the wind-up, the charge is cancelled and there is no trail,
because interrupting cancels an attack. Killed mid-charge, it stops where it fell
and what it has already burned keeps burning.

**The trail is worth one bite over its whole life**, so a quarter of a bite per
tick across four one-second ticks. It exists to take ground away rather than to
kill.

### What could not be settled here

**There is no data field for what burning ground deals per tick, for any skill.**
`ACataclysmGroundZone` takes a `DamagePerTick` and nothing supplies it from data,
and the eight player skills that leave burning ground state only a radius and a
duration. The Hellhound's figure is therefore prose in the design document.
Issue #361 adds the field, and it is a player-skill problem before it is an enemy
one.

### Evidence

`tools/tests/test_enemy_abilities.py` grows from 30 tests to 44. Two of the new
ones compare the shape, rider and Movement-mode vocabulary in
`sim/cataclysm_sim/enemy_abilities.py` against `tools/generate_datatables.py`,
which is the authoritative copy — this project has had a silently drifting copy
twice. All 23 deliberate breaks were caught by the intended test.

**One break was not caught on the first attempt**, and it was the same shape of
mistake as the previous one: removing the rider from section V's enumeration left
it in the paragraph below, so a paragraph-wide search still passed. The test now
slices out the enumeration sentence alone.

---

## 2026-08-06 — The Succubus is a support enemy, and its buff is an aura so that killing it works

**Affects** `docs/Cataclysm_GDD_v2.md`, a new "The Succubus" subsection in
section X, and `sim/cataclysm_sim/enemy_abilities.py`. Applied in full. Issue
#349, a child of the enemy design epic #29.

### The question

The Succubus is the only enemy in the vertical slice that makes the others
stronger. Its role line says it "debuffs player and buffs nearby allies", and
neither half had a mechanism, a number or an effect name. Issue #349 asked which
debuff, what the ally buff is and whether it is an aura or a cast, what its
telegraph is, and whether its energy shield differs from the player's.

### What was decided

**Three abilities**: a telegraphed bolt on its attack interval, a curse on a ten
second cooldown, and an aura held on while it lives.

**Its wind-up is exactly half its attack interval, which is the most the
telegraph rule allows.** The cap for a 2.6 second interval is 3.15 metres, and
the Succubus uses all of it, which makes the wind-up 1.3 seconds. That is what
"slow but powerful attacks" has to mean mechanically for an enemy with the second
highest damage share in the slice.

**The curse applies Withered Touch**, chosen from `game/Data/StatusEffects.csv`
rather than invented. Five seconds, because Battle Cry and Final Curse are the
only enemy-applied debuffs in that table that state a duration and both say five.
Ten seconds of cooldown, so the player has as long without it as with it; ten is
also the top of the Support slot's cooldown band.

**A new general rule: an innate ability must not duplicate a modifier its own
Cataclysm can roll.** An enemy carries one modifier per rarity above Common, from
its own Cataclysm's column of `game/Data/EnemyModifiers.csv` and the Generic one.
An ability duplicating a modifier the same creature could roll would let it hold
the effect twice with nothing saying what that means. A modifier belonging to a
different Cataclysm is not a clash, because that enemy can never roll it. Two
Demonic modifiers, Abyssal Aura and Infernal Brand, are also effect names and are
therefore closed to Demonic enemies as innate abilities.

**The ally buff is Commander** — "all nearby allies gain 20% increased stats",
already in the effect table — **held on as an Aura rather than cast.** This is
the load-bearing choice. A cast buff lasts its duration and survives the caster,
so killing the Succubus achieves nothing until the timer runs out. An aura ends
the instant it dies, which is what makes target priority the lesson this enemy
teaches. Its radius is the Succubus's own attack range, because that is how far
from the fight it stands.

**Three of the seven shapes have no ground marker**, and that is not an omission
in the telegraph table: there is nothing for a curse to be drawn on. SelfBuff,
Summon and Debuff are read off the caster's animation and answered by
interrupting, which is the counter the Attack Telegraphs subsection already
names.

**It stands at 8 metres and does not retreat.** Eight is the shortest
Movement-shape skill range in `game/Data/WeaponSkills.csv`, the same anchor the
telegraph work used; a ranged enemy beyond it could not be closed on by every
build. It does not kite because at 3.5 metres per second it matches the slowest
class and loses to the other two, so retreating produces a chase rather than a
test, and the enemy whose job is to punish standing still is the Corrupted
Sentinel.

**Its energy shield behaves exactly like the player's**, and nothing needed
adding. Three of the five existing rules decide the fight: damage over time
passes through it and holds it empty, magic weapons strip 10% more, and it
refills three seconds after the last damage. 42 of the 51 designed Demonic
skills carry `Burn=1`, so a Demonic player already carries the answer without
building for it.

**An enemy ability now declares a slot**, one of the seven in
`game/Data/SkillSlots.csv`. Basic runs on the archetype's attack interval, Aura
is held on, and the other five run on their own cooldown. Before this an ability
with no cooldown was assumed to be the basic attack, which cannot express an aura
that is simply on.

### What was rejected

**Giving the Succubus a debuff invented for it.** The effect table already holds
fifty entries and the issue asked for a choice from it.

**A cast ally buff with a duration.** It makes killing the support enemy pointless
for as long as the timer runs, which is the opposite of what a support enemy
should teach.

**Kiting.** It cannot outrun any class, so it would only produce a chase, and it
would take the Corrupted Sentinel's job.

### Evidence

`tools/tests/test_enemy_abilities.py` grows from 19 tests to 30. The wind-up, the
telegraph verdict, the aura radius, the 8 metre stand-off and the burning-skill
count are all recomputed from `sim/cataclysm_sim/enemy_stats.py`,
`game/Data/WeaponSkills.csv`, `game/Data/StatusEffects.csv` and
`game/Data/EnemyModifiers.csv`. All 22 deliberate breaks were caught by the
intended test.

**One of the tests caught a real mistake in the design while it was being
written.** The first version of the modifier-clash rule compared against every
modifier in the table, and it rejected Commander. Commander is a War modifier, so
a Demonic Succubus can never roll it and there is no clash; the rule was narrowed
to the enemy's own Cataclysm plus Generic, which is the pool an enemy actually
draws from.

---

## 2026-08-06 — The Imp has one attack, and how many can reach you is decided by geometry

**Affects** `docs/Cataclysm_GDD_v2.md`, new section "Vertical Slice Enemy
Behaviour" in section X, and `sim/cataclysm_sim/enemy_abilities.py`, a new file.
Applied in full. Issue #348, a child of the enemy design epic #29.

### The question

All seven Demonic enemies have had complete stat blocks in
`sim/cataclysm_sim/enemy_stats.py` for some time — health, damage, armour,
attack interval, movement speed, evasion, resistance. None of them had a single
ability, so nothing said what any of them does with its turn.

Issue #348 asked three things about the Imp: what its basic attack is, how a
pack behaves, and whether it has a second ability at all.

### What was decided

**An enemy ability is written exactly like a player skill.** A `Shape` and its
parameters, the same two columns `game/Data/WeaponSkills.csv` already carries,
in the same units. There is no separate enemy vocabulary. The Attack Telegraphs
subsection added the day before already draws a ground marker from those same
numbers, so this is the second thing to reuse them and the reason to keep doing
so: one executor in the engine, one authoring format, and a marker that cannot
disagree with the attack it warns about.

**The Imp has exactly one ability: a Strike called Rend, radius 1.32 metres,
90 degree cone, one target.** It runs on the archetype's 0.9 second attack
interval and it is not telegraphed, which the existing telegraph rule already
decided by that interval alone.

**Refusing a second ability is the decision, not an omission**, for two reasons.
Whatever an Imp does is multiplied by the pack, so one extra ability on ten Imps
is ten of it at once — the "twenty markers on screen" failure arriving from the
one enemy the telegraph rule clears. And the smallest useful marker of 1 metre
needs a cycle of at least 1.4 seconds, which is longer than the Imp's whole
attack interval, so any telegraphed ability it had would be the slowest thing it
does on the creature whose role is being individually ignorable.

**How many Imps can hit a player at once is geometry, not a rule.** Bodies
cannot overlap, so a swarm queues in rings. A body of radius `r` at distance `D`
from the centre covers `2 × arcsin(r ÷ D)` of the circle, so a ring holds
`pi ÷ arcsin(r ÷ D)` of them.

**The Imp's body radius is 0.30 metres**, the same as the lesser imp minion's
capsule in `game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp`, because it
is the same creature. The player's is 0.42, from `CapsuleRadius` in
`game/Source/Cataclysm/Character/CataclysmPlayerCharacter.cpp`. That gives 7 in
the first rank at 0.72 m and 13 in the second at 1.32 m.

**Rend's radius is 1.32 metres because that is exactly the second rank.** It was
not chosen for looking right. Section X already states that ten Imps kill a
geared character in 4.9 seconds and twenty in 2.4. One rank is seven, and seven
take 6.9 seconds. A reach that let only the rank in contact swing would make
both of the document's own figures false.

**Twenty is therefore the cap on a swarm, and nothing else enforces it.** The
third rank is out of reach. Twenty is the same twenty the document already calls
the lethal pack.

**A pack is ten**, which is the other figure the document already commits to. It
is three more than one full rank, so the second rank does something in an
ordinary fight rather than only in a swarm event.

**A Leap and a Blink clear a ring of bodies; a Charge does not.** This is new.
Section V says what each Movement mode hits, not whether it passes through
bodies, so this fills that gap. It is what stops "surrounded" meaning "dead
whatever you built".

### What was rejected

**An attack-token rule**, which is the standard answer in the genre: Doom (2016)
makes an enemy request a token before attacking and the Batman Arkham games
allow two or three attackers at a time. It is the wrong answer here because this
document has already committed to ten Imps killing in 4.9 seconds and twenty in
2.4. A token limit of two or three makes both false and flattens the difference
between a pack of ten and a pack of twenty to nothing. Physical crowding gives
the same protection — never more than twenty on you — without capping damage the
design already promised.

**Chasing the exact figures rather than the rules.** The 4.9 and 2.4 second
kill times are fast, and whether they survive contact with a real fight is a
balance question for play rather than for this issue. They are recorded as
concerns on #264 and #234 and are not re-derived here.

### Evidence

`tools/tests/test_enemy_abilities.py`, 19 tests, recomputes the ring table, the
reach and the telegraph verdict from `sim/cataclysm_sim/enemy_stats.py` and the
two C++ capsule constants rather than comparing the document against a copy of
itself. All 25 deliberate breaks were caught by the intended test. One of the 25
was not caught on the first attempt: the word "blink" survived in a neighbouring
sentence, so the test now slices the verdict sentence out and checks each mode
against it.

---

## 2026-08-06 — A telegraph is a wind-up long enough to walk out of, and swarm enemies get none

**Affects** `docs/Cataclysm_GDD_v2.md`, new section "Attack Telegraphs" in
section X. Applied in full. Issue #347, the first child of the enemy design epic
#29.

### The question

The design document said twice that the player "must read and dodge telegraphed
enemy attacks" and nowhere said what a telegraph is. The word appeared in two
sentences of overview prose and nowhere else: no shape, no duration, no rule
about what the player can do during one, and no statement of which attacks get
one at all.

Six sibling issues each ask "what is this enemy's telegraph". Without this they
would each have answered differently.

### What was decided

**A telegraph adds no new vocabulary.** It is an ordinary attack in one of the
shapes the skill system already runs — Strike, Projectile, Aura, Movement, from
the `Shape` column of `game/Data/WeaponSkills.csv` — drawn on the ground before
it lands. An enemy ability is authored the same way a player skill is, and the
marker is drawn from the ability's own numbers rather than authored a second time
and allowed to disagree.

**The wind-up duration is derived, not chosen.** The player escapes by walking or
by using a Movement skill, and that gives two sizes of telegraph:

| | Walk out of it | Spend a Movement skill |
| :-- | :-- | :-- |
| Wind-up | 0.4 + Radius ÷ 3.5 seconds | 0.8 + Radius ÷ 16 seconds |
| Maximum radius | 3.5 × (attack interval ÷ 2 − 0.4) | 8 metres |
| Gate | attack interval | ability cooldown of 5 seconds or more |

Every figure except the reaction allowance comes from somewhere else in the
project. 3.5 metres per second is the Ritualist, the slowest of the three Demonic
classes in the class stat table. 8 metres is the shortest of the ten
Movement-shape skill ranges in `game/Data/WeaponSkills.csv`. 5 seconds is the
Movement slot cooldown in `game/Data/SkillSlots.csv`. The attack intervals are
`ARCHETYPES` in `sim/cataclysm_sim/enemy_stats.py`.

**Designing against the slowest and the shortest is deliberate.** Every build can
then clear every telegraph, and the faster ones clear it with margin rather than
only just.

### The result nobody chose, which is the reason to trust the rule

Applied to the seven vertical slice enemies, the rule says the Imp cannot
telegraph anything larger than 0.2 metres and the Hellhound's basic attack
nothing larger than 0.5. Both are smaller than the creature standing in the
marker, so there is nowhere to walk. **Neither is telegraphed.**

That is the design section X already asserts. It says "A single Common enemy is
not the threat. A pack is", and an Imp that telegraphed would be individually
dangerous and would stop being swarm fodder. The rule produces that outcome from
the attack intervals without anybody deciding it per enemy, which is the main
evidence that it is the right rule rather than a plausible one.

**It also answers the dense-pack readability problem for free.** Only enemies
with an attack interval of 2 seconds or more can telegraph, and the two swarm
enemies are excluded by their own attack speed, so a pack cannot fill the screen
with markers. That is the genre's best-known telegraph failure and it needed no
separate mechanism.

### The one figure that is not derived

**0.4 seconds of reaction allowance**, and 0.8 for the larger telegraphs where
the player must also decide whether to spend a cooldown. Simple visual reaction
time is measured at 200 to 250 milliseconds and reaction to a new on-screen
stimulus at 300 to 500 milliseconds. 0.4 sits inside that band and is what an
ordinary player can do reliably while doing something else. It is stated in the
document as the one figure that comes from outside the project.

### What the genre does

**Path of Exile 2** makes telegraphs the centre of its combat and answers them
with a dodge roll carrying invincibility frames. **Diablo IV** uses ground
markers and has shipped complaints that they disappear into the visual noise of a
dense fight, and that it is inconsistent about which attacks get the dramatic
treatment.

**This game has neither a dodge roll nor an evade button**, which is why the
duration is derived from walking speed rather than copied from either. That
difference is the reason the numbers here are not Path of Exile 2's: a game where
the answer is an invincibility frame can telegraph anything at any size, and a
game where the answer is walking cannot.

**Confidence is not uniform.** The Diablo IV and Path of Exile 2 claims come from
web search summaries and community sources rather than from developer
documentation. The reaction time figures are from published measurement summaries
rather than a primary paper. The project's own figures — movement speeds, skill
ranges, cooldowns, attack intervals — are read directly from the files named
above and are certain.

### What the project owner added

The first version of this section treated the Movement skill as an emergency
option and derived everything from walking. The project owner corrected it: the
player also uses movement abilities for escape. The larger tier of telegraph, the
8 metre cap and the 5 second cooldown gate are that correction applied.

### What is not settled

**Nothing here has been played.** Every duration is arithmetic against a reaction
figure and a walking speed. Whether a 1.5 second wind-up feels fair is a question
for a playable build, and the constants are expected to move once there is one.

**How a large telegraph looks different from a small one is stated as a
requirement and not designed.** The player has to know which kind they are
looking at before deciding whether to spend a cooldown, and radius alone is not
readable at a glance. That is visual design and waits on issue #19.

**Still open.** Issues #348 to #354 are the seven per-enemy designs that use this
vocabulary. Issue #355 publishes the archetype numbers as game data so the engine
can read them. Issue #39 implements all of it.

Sources:
- Rage and telegraphs in Path of Exile 2: https://mobalytics.gg/poe-2/guides/rage
- Dodge roll mechanics in Path of Exile 2: https://dving.net/guides/path-of-exile-2-guides/dodge-roll
- Ground marker visibility in dense fights, Diablo IV forums: https://us.forums.blizzard.com/en/d4/t/option-to-reduce-or-hide-other-players%E2%80%99-spell-effects-for-better-ground-affix-visibility/249901
- Inconsistent attack telegraphs, Diablo IV forums: https://us.forums.blizzard.com/en/d4/t/i-wish-those-falling-swords-had-the-dangerous-attack-telegraph/243631
- Reaction time measurement summary: https://backyardbrains.com/pages/the-science-of-your-reaction-time
- Human perception timings: https://www.pubnub.com/blog/how-fast-is-realtime-human-perception-and-technology/

---

## 2026-08-06 — The Masochist resource is Anguish, and healing is what decays it

**Affects** `docs/Cataclysm_GDD_v2.md` (the Class Resource Systems section),
`docs/Masochist_Class_Tree_Final.json` (new), `docs/README.md`. Applied in full.
Issue #63.

### What was already decided, and what was left to this change

The 2026-08-04 entry below, "DECISION 4: the Masochist resource has two
generators, and the passive tree decides which one a build leans on", recorded
the project owner's answer that the resource is built by **both** spending health
and taking damage. It deliberately left the build rate, the cap, the decay and
what spends it to be designed with the tree, because the owner's answer made
them properties of tree nodes rather than of the class.

This entry fills those in. It also names the resource, which the 2026-08-02
entry on the three Demonic class stat lines explicitly left to this work.

### The resource

**Anguish.** Base pool 100, which is the Masochist's `Class Resource` value in
the class stat table and matches the Ravager's. It is generated in one unit,
percent of maximum health, through the two generators the owner decided on:

- 1 Anguish per 1% of maximum health **lost to damage**
- 1 Anguish per 1% of maximum health **spent as an ability cost**

Both generators use the same unit deliberately, so that neither dominates
because of pool size and a build can be measured against either.

**Healing removes Anguish, at 1 per 1% of maximum health restored. There is no
timer.** That is the part that is new here and it is the load-bearing choice, so
the case for it is set out below.

### Why healing and not a timer

Resolve and Fury decay on a timer out of combat. Preparation does not decay at
all. A fourth timer resource would have been a fourth copy of Resolve, which is
the resource Anguish is closest to in every other respect: both are built by
being hit and both feed retaliation.

Tying decay to healing does three things a timer cannot.

**It makes the class stat line mean something.** The Masochist has by far the
largest health regeneration of the three Demonic classes: 37.6 per second
against 2,526 maximum health, which is 1.49% per second. Under this rule that is
1.49 Anguish per second, so a full pool drains in about 67 seconds standing
still — comparable to a slow timer, but derived from a number the design already
set rather than invented next to it.

**It is the only resource decay a player controls.** Every other one runs on its
own. This one is a build choice, which is what the design document asks class
resources to be: "not optional stat bars, they are the engine of the build".

**It explains the refusals the stat line already makes.** The 2026-08-02 entry
says the Masochist "refuses evasion and energy shield: evading is missing out,
and a shield absorbs the damage the class needs to convert". That was an
assertion with nothing behind it. Under this rule it is arithmetic: both stats
reduce health lost, and health lost is the resource.

### What the genre does, and where this leaves the genre behind

**A resource built by taking damage is normal.** Path of Exile's Rage is
sustained by being hit — a character loses Rage over time only if they have not
taken damage or gained Rage recently.

**A tree node that changes the decay rule is normal.** Last Epoch's Rancour node
stops rage decaying outside combat. That is the same shape as the `Sanguine
Ledger` keystone here, which stops health regeneration removing Anguish at the
cost of halving that regeneration.

**Rewarding a character for staying injured is well established, and one shipped
skill is close enough to be worth naming.** Path of Exile's Petrified Blood
converts part of the immediate life loss from hits into damage over time,
prevents the character recovering life above the low-life threshold by any means
except flasks, and adds a life cost to skills while above that threshold. Three
things in this tree are the same shapes: the `Point of No Return` keystone caps
healing at 50% in exchange for damage, `Martyr's Wellspring` turns an ability's
health cost into a damage-over-time debuff instead of an immediate loss, and the
whole Low Life branch pays out below fixed health thresholds.

**No shipped game was found where healing consumes a class resource.** That was
searched for specifically and nothing turned up. So the decay rule is not copied
from anything, and it is the one part of this design with no precedent behind it.
It is stated here as untested rather than as established practice.

**Confidence is not uniform.** The Rage, Rancour and Petrified Blood claims come
from web search result summaries, not from the wiki pages themselves;
`pathofexile.fandom.com` returns HTTP 402 to this project's fetch tool, which is
a known trap recorded in the working notes. The Masochist stat numbers are read
directly from `docs/Cataclysm_GDD_v2.md` and are certain.

### The tree

`docs/Masochist_Class_Tree_Final.json`. 74 nodes — 55 basic, 15 keystones, 4
capstones — 69 edges, 440 spendable points against the 230 point budget. That is
the same node count, keystone count, capstone count and point total as
`docs/Bulwark_Class_Tree_Final.json`, and the same per-node point distribution.

One spine, "The Path of Suffering", carries the resource and the four nodes the
prose design named. Four branches hang off it: Flesh Craver (built around health
lost to damage), Soul Scourge (built around health spent as an ability cost),
Flagellant (built around debuffs on the character), and the Low Life path. Each
branch ends in a keystone that leans the resource one way — `Flesh Craver` gives
30% more Anguish from damage and 50% less from costs, `Soul Scourge` does the
reverse. That is what "the passive tree decides which one a build leans on"
means in practice.

### The prose design's three thresholds became four tiers

The Drive document "Masochist Passive Tree" put its "but-for" choices at 25, 50
and **75** points. The design document specifies four capstone tiers at 25, 50,
**100** and 200. The issue asked for these to be reconciled.

**The three written vows keep their contents and move to 25, 50 and 100.** The
200 tier is new and had to be designed: *The Martyr, but For the Flesh* reflects
all damage taken onto nearby enemies and sets health regeneration to zero,
*Apotheosis, but For the Mind* removes the Anguish cap in exchange for 1% more
damage taken per 100 Anguish held, and *The Undying, but For the End* consumes
all Anguish to survive lethal damage.

The tiers are recorded in the `threshold` field, and the three choices per tier
in the `options` field, which is what the editor at
`C:\Projects\PassiveTreeCreator` reads for decision nodes and what
`docs/Empire_Development_Tree_Final.json` already does. The three March 2026
class trees say only "Choose one of three oaths" in the description and record
the oaths nowhere, so what those choices are is lost. This tree does not repeat
that.

### Wording corrected against the design document's own rule

Section IV reserves "more" and "less" for gems, passive tree keystones and
enchantments, and requires everything else to say "increased". The prose design
broke this on basic nodes — "Your Life Leech effects are 10% more effective per
point" — and those are written as "increased" here. Keystones still use "more"
and "less", which is what the rule allows.

### What is not settled

The numbers are not calibrated against combat, because there is still no
combat to calibrate against. They are internally consistent and sized against
the Bulwark tree, which is the closest sibling; that is all that can be claimed.

The prose design called the Soul Scourge branch "Caster & Mana/ES-Based", but
the Masochist's base Energy Shield is 0 and the stat line refuses it
deliberately. The branch is built on mana, and one node and no keystone touches
Energy Shield, on the basis that gear can still grant it. Issue #345 records
that tension rather than resolving it here.

**Still open.** Issue #38 implements this tree in the engine and is what the
Phase 1 vertical slice needs next. Issue #24 covers the other 21 class trees.
Issue #343 and issue #344 are two defects in the older trees found while reading
them for this work.

Sources:
- Rage in Path of Exile 2: https://mobalytics.gg/poe-2/guides/rage
- Rage, Path of Exile wiki: https://pathofexile.fandom.com/wiki/Rage
- Rancour and rage decay, Last Epoch forum: https://forum.lastepoch.com/t/movement-abilities-shouldnt-start-rage-decay-for-rancour/46636
- Forge Guard overview, Last Epoch: https://www.icy-veins.com/last-epoch/forge-guard-overview
- Petrified Blood, Path of Exile wiki: https://pathofexile.fandom.com/wiki/Petrified_Blood
- Petrified Blood mathematical analysis: https://devtrackers.gg/pathofexile/p/09e2e187-petrified-blood-a-mathematical-analysis

---

## 2026-08-06 — Minions have their own stats, and reach the summoner through three channels

**Affects** `docs/Cataclysm_GDD_v2.md`. The rule is applied in full; the numbers
are not set. Issue #209.

### What was reversed

The document stated one rule for every minion: "A minion's attack deals 30% of
its summoner's weapon damage, and it attacks once per second. It has no damage of
its own." It rejected a minion stat family on the grounds that Path of Exile's
route "needs a whole separate family of minion-only stats to scale, which this
game does not have and does not want for two skills."

The project owner reversed this on 2026-08-04, on the grounds that a player who
builds around minions and finds no minion gear will be unhappy. Three parts:
each minion skill gets its own stats, base scaling comes from an attribute rather
than from weapon damage, and minion affixes exist on gear on top of that.

### The old reasoning did not survive contact with the data

**"For two skills" was already false.** `game/Data/WeaponSkills.csv` holds six
skills tagged `Type.Minion`: Bolt Turret, Ballista, Iron Fortress, Cinder Swarm,
Summon Imp and Open the Rift. The document also lists 24 classes, at least two of
which are built entirely on minions.

**Two minions already had stats.** Bolt Turret's description states "The turret
has 200 HP" and that it fires every 1.5 seconds. Ballista states 500 health and
every 2 seconds. Neither attacks once per second, so the rule the document stated
was contradicted by the shipped data.

**Minion gear already existed.** `game/Data/EnchantmentsPositive.csv` and
`game/Data/EnchantmentsNegative.csv` carry 17 positive and 6 negative
minion-related enchantments, including two that raise the minion count and one
that grants partial inheritance of armour and resistances. The document said
minions had no stats while the enchantment table sold modifiers for them.

### What replaced it

A minion reaches its summoner through three channels and nothing else: its side,
its base health and damage raised by the summoner's level, and increased damage
from one primary attribute declared per minion type. Everything else is blocked
unless a modifier names minions.

**The one named exception is deliberate.** The enchantment "Summoned minions
inherit 10%-25% of your armor and resistances" already exists, so a blanket
zero-inheritance rule would have contradicted the data on the day it was written.
That is the same failure this log already records for the affix audit that read
data without reading the prose rules, arrived at from the other direction.

### Why this removes the double-scaling problem, in two layers

**The first layer is the reversal itself.** Under the old rule, weapon damage
affixes already scaled minions, so adding minion damage affixes on top would have
scaled them twice from one investment. Once minion damage no longer reads the
summoner's weapon, that double count cannot happen.

**The second layer is structural and is this game's advantage over every game
surveyed.** `docs/Cataclysm_GDD_v2.md` already states that no ordinary affix is a
multiplier: an affix is flat or increased. Every catastrophic minion scaling
failure found in the survey was multiplicative — Diablo III's Carnevil fetishes,
where attack speed raised both the fire rate and the damage; Diablo IV's stacked
multiplier aspects on top of full stat inheritance; Path of Exile's The Baron,
where one attribute fed minion stats, minion count and player sustain at once.
Here an attribute's contribution and an affix's contribution add. They cannot
multiply each other.

### Minion count stays in the enchantment table permanently

**This reuses a rule this document already wrote.** The maximum resistance
decision states that every affix has seven tiers and can appear on several
pieces, and that a modifier which does not tolerate that range belongs in the
enchantment table instead.

Count fails those tests harder than maximum resistance does. It has no meaningful
seven-step curve. There are **eight ring slots**, so a "+1 maximum minions"
suffix would be +8 from rings alone before the necklace, relic, belt or boots. And
it multiplies every other minion investment at once, because damage, effective
health and rider uptime all scale with how many minions are alive.

The enchantment table is already safe for it, because enchantments are unique per
character: each can appear once across all equipped gear, so the total is bounded
permanently and each one costs a slot on a Legendary item or better.

**The genre agrees and three of the four games surveyed have clawed count back.**
Diablo IV withdrew its freely-imprintable minion count aspects from the game
entirely. Path of Exile moved count off unique items onto skill gems in patch
3.8.0, with the stated reason of "making summoners less reliant on specific unique
items". Path of Exile 2 ships no generic rollable minion count affix at all.
Diablo II is the counter-example and the warning: count there scales continuously
off gear that grants skill levels, and geared summoners reach roughly 13 to 17
skeletons.

### What is deliberately not decided here

Four numbers, all tracked as issues rather than guessed: which attribute (#335),
the per-type base health and damage, which need the simulation (#336), the four
minion affixes and their values (#337), and moving the three deployable skills'
numbers out of prose into data (#338).

**One value was deliberately omitted rather than forgotten.** A flat added minion
damage affix is not proposed. Its correct value cannot be set until the per-type
base damage curve exists, and a flat addition is multiplied by the number of
active minions, which is already the most dangerous quantity in the system. If
minion builds feel numerically flat after the first tuning pass, this is the first
thing to add, and it should be split by attack type when it lands. Recorded here
so the next affix audit does not report it as a gap.

### Where the evidence came from, and how good it is

Eleven agents: five researching Diablo IV, Path of Exile 1, Path of Exile 2, Last
Epoch and the genre's failure modes, five adversarially trying to refute those
findings, one synthesising. Every claim was graded as a page actually read, a web
search summary, or an inference.

**Three of the survey's findings were refuted by the verification pass and the
corrections are what this decision rests on.** Diablo IV was reported as having no
minion life itemisation, which is false — it has a Maximum Minion Life affix, and
building on the supposed absence would have produced the wrong recommendation
here. A widely-quoted figure that ground-pathing minions in Path of Exile 2
"attack empty space 30-40% of the fight" was refuted as an uncited single-author
number, so the qualitative point is used and the magnitude is not. A supposed
100% cap on minion attack speed in Diablo IV was found to be probably a
conflation with a different mechanic, so no cap is copied.

**Diablo IV's inheritance rule is deliberately not copied**, because nobody can
establish what it is. The only official source says minions receive 100% of the
player's *attributes*; the widely-repeated reading that they inherit 100% of
everything is unsupported and partly contradicted.

**The strongest evidence was this project's own data**, not the genre. The
enchantment table, the six minion skills, and the affix values were read directly
and settled more of the design than any external source did.

---

## 2026-08-05 — Cripple, Weaken, Shred and Madness scale by chance alone

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #300.

### The question

The six damage over time effects have three rollable affixes each: a chance to
apply, a damage affix and a duration affix, the last two added by issue #205. The
four weakening effects — Cripple, Weaken, Shred and Madness — have one, a chance
to apply. Issue #300 asked whether that asymmetry is a gap.

### What was decided

**No magnitude affix and no duration affix are added.** Chance to apply stays the
only rollable stat for these four, because it is already all three levers.

The rule that makes it so is `ailment_application` in
`sim/cataclysm_sim/affixes.py`, settled by the project owner on 2026-08-03:
chance caps at 100% and everything past it multiplies the effect's magnitude. The
second half is in the design document's own table: magnitude raises the strength
to its cap, then extends the duration instead. So one affix raises whichever of
chance, magnitude and duration the build has not yet filled, in that order.

### The measurement

`sim/analyse_weakening_ailments.py`, run 2026-08-05, at the top affix tier on
fully upgraded gear. Every figure is read from `game/Data/StatusEffects.csv`,
`game/Data/Gems.csv` and `sim/cataclysm_sim/affixes.py` rather than typed in.

| Effect | base | cap | chance needed to fill the cap | affixes alone | filled by |
|---|---|---|---|---|---|
| Cripple | 30% | 80% | 267% | 165% | affixes + 1 of 45 sockets |
| Weaken | 20% | 80% | 400% | 165% | affixes + 4 of 45 sockets |
| Shred | 10 resistance points | none | — | 165% | no percentage cap to fill |
| Madness | none | none | — | 165% | magnitude is duration |

Eleven gear pieces can carry a chance to apply, one each, because `ailment_group`
puts every roll of the same chance in one group and a piece cannot hold two from
a group. Eleven at 15% is 165%.

**Neither cap is filled by affixes alone, and both are filled by affixes plus a
handful of sockets.** A build that wants one of these effects reaches the cap; a
build that does not, does not. That is the shape a scaling stat should have.

**Shred and Madness have no percentage cap at all.** Shred stops when the
resistance it is reducing reaches zero, which depends on the enemy rather than on
a number here. Madness reduces nothing — it redirects the enemy — so its
magnitude goes straight to duration and a magnitude affix would have nothing to
scale.

### The case against, and it is the stronger half of the evidence

**Every comparison game gives its equivalents a second lever.** The survey was
not close and it argues the other way:

| Game | What it sells separately |
|---|---|
| Path of Exile | "increased Effect of Chill" and "increased Effect of Withered" on items, passives and cluster jewels. Withered is 6% increased chaos damage taken per stack, and 100% increased effect doubles what each stack is worth. |
| Diablo IV | a Crowd Control Duration affix, rolling on amulets and on the Sorcerer focus, covering Slow, Immobilize, Stun, Chill, Freeze and the rest. |
| Last Epoch | Slow stacks up to three times rather than scaling in magnitude, and the game has a stated Ailment Duration and Effectiveness axis. |

**The evidence for these three claims is web search result summaries rather than
the pages themselves.** `WebFetch` gets HTTP 402 from `pathofexile.fandom.com`
and `diablo.fandom.com` and an access-denied page from `poewiki.net`.

**Why this design differs anyway.** In all three of those games a chance to apply
stops paying at 100%. Every point past it is dead, so a separate magnitude stat
is the only way an ailment build keeps scaling. Here chance does not stop paying,
which is the whole reason `ailment_application` exists — its docstring names the
dead-point problem as the thing it was written to avoid. One stat therefore does
the work those games need two or three stats for.

### What would reverse this

**Play, not argument.** Two things would show the decision is wrong:

1. **A build reaches the cap and then has nothing to buy.** Past the cap, chance
   buys duration, and duration on a 4-second slow may stop feeling like progress
   long before the numbers stop rising. That is a feel question.
2. **The four turn out to be worth less than the six per affix slot.** This
   decision says nothing about that. It says a second affix would add no new
   lever; it does not say one lever is priced correctly against three.

Reversing it is cheap: three rows in the Affixes sheet of
`docs/All_Things_Cataclysm.xlsx`, then `python tools/generate_datatables.py` and
the DataTable asset generator.

### What this deliberately does not decide

Whether Madness is a hard stun for the anti-stun-lock rule. That is issue #303
and it is waiting on the project owner. Nothing here depends on the answer: the
magnitude of Madness is its duration either way.

---

## 2026-08-05 — A run ending costs the run, not the character

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #315.

### The question

`docs/Cataclysm_GDD_v2.md` described a run ending in two ways that could not both
be true. The Worn Residue section said a character killed by its own corrupted
double "is consumed" and that "the run ends", and the Cataclysmic Forge
introduction priced the whole residue system on that being permanent: residue was
"the only way the Forge can cost a player anything permanent". Meanwhile the
project owner's answer on issue #286 said a run ending keeps the character.

Nothing in the document said, in one place, what a run ending costs.

### What was decided

**By the project owner, 2026-08-05, on issue #315:**

> For the consume character part, it says the run ends. Which means you restart
> the tier you're currently on. You just keep your character/gear/passive
> trees/empire tree. Your second example, you read that wrong. It's saying why we
> don't replace the character with a fresh one. The worn residue trigger doesn't
> actually consume the character, it consumes it in the sense that their
> character will get added to the pool of Nemesis characters for that dungeon
> modifier. No need to overthink it, if the nemesis created from worn residue
> kills you, you fail the run.

Three separate rulings, all applied:

1. **A run ending never costs the character.** The same character plays the next
   run with its levels, its equipment, its class passive trees and its empire
   upgrade tree. This holds for all four ways a run ends: defeating the boss
   dungeon, losing the capital, dying in the Last Stand, and being killed by the
   Worn Residue double.
2. **A failed run replays the same tier.** Only defeating the boss dungeon adds a
   Cataclysm to the next run. What a failed run costs is the empire map, the
   cities, the days spent, and the progress toward the active Cataclysms.
3. **"Consumed" names where the character goes, not what is taken from the
   player.** A snapshot joins the shared library of corrupted characters that The
   Corrupted dungeon modifier draws from. The character itself is untouched.

### What changed in the document

| Place | What it said | What it says now |
|---|---|---|
| Section II, new subsection Ending a Run | nothing; the cost of a run ending was never stated in one place | the four ways a run ends, that none costs the character, what a failed run does cost, and that ordinary death in a dungeon is not a run ending |
| Cataclysmic Forge introduction, section VII | residue is "the only way the Forge can cost a player anything permanent" | the Forge cannot cost a player anything permanent; its worst outcome ends a run |
| Worn Residue and Consumption, section VII | "the character is consumed" with no definition | a paragraph headed "Consumed does not mean destroyed", and a paragraph naming what being consumed actually costs |
| "Why the run ends rather than the character being replaced mid-run" | read as a description of what happens | labelled as the alternative the design rejected, which is what the owner said it was |
| Empire-Wide Upgrades, tree survival rule | gave "Worn Residue can consume a character outright" as the reason the rule matters | says nothing in the design destroys a character, so the rule is a safeguard rather than a live case |
| The Corrupted, section VIII | "a player could lose a high-tier character on purpose" | "get a high-tier character consumed on purpose", and notes that doing so is now cheap |

### The case against

**It makes the Consumption Threshold warning guard a smaller stake than its
presentation implies.** The threshold shows a confirmation prompt, and the
document calls crossing it "always a decision the player made on purpose". The
consequence is now the loss of a run. That is still the largest non-permanent
setback in the game — a high-tier empire and every day spent building it — but it
is not what the original wording suggested. The Worn Residue section now says
this plainly rather than leaving the reader to notice it.

**Deliberately being consumed is cheap, and it feeds the shared table.** A player
can now cross the threshold on purpose to put a high-tier character into the
corrupted-character pool at no cost beyond the run. The Scaling rule in The
Corrupted section already blocks the profitable version of this — the drawn
character is rebuilt at the drawing dungeon's tier, not the tier it was consumed
at — and that rule now carries more weight than when it was written.

**The empire tree survival rule from issue #286 now has no trigger.** It says a
lost Solo Self-Found character's tree passes to the next Solo Self-Found
character in that mode. Nothing in the design loses a character any more. The
rule is kept as a safeguard and the document says so. Whether a player can delete
a character, which would give the rule a trigger, is issue #325.

### What this does not decide

**Whether a failed run should cost anything permanent at all.** The design now
has no permanent cost for failure of any kind. That is a coherent roguelike
position and it is what the owner chose, but it is a balance question that needs
play rather than argument, so it is recorded here and not raised as an issue.

---

## 2026-08-05 — The carried inventory is 48 slots and nothing increases it

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #308.

### The gap

Issue #260 settled that `docs/Empire_Skill_Tree_Keystones.md`, the prose
description of the empire tree, predates the passive tree editor, so an idea in
it with no node in `docs/Empire_Development_Tree_Final.json` was never built. One
of the three removed that way was the only thing anywhere in the design that
granted inventory slots:

> **NOTABLE: Weightless Spoils:** Adds 10 inventory slots.

With it confirmed as never built, nothing scaled inventory and nothing stated a
size either. Searching the node graph for the word finds no node.

### What was decided

**48 slots, four rows of twelve, one item per slot, and nothing increases it.**
No empire upgrade node, no affix, no city upgrade.

### Why nothing increases it

**It is what the genre does.** Diablo IV fixes its inventory at 33 slots and
cannot be increased, and Blizzard stated the reason: "To avoid interrupting
gameplay with pockets of inventory management, we're not planning to bring back
different-sized items." Path of Exile's 12 by 5 grid never grows; Last Epoch's
answer is loot filters rather than more space.

**And a scaling source would weaken a pressure this design created on purpose.**
A dungeon floor costs a day, so a dungeon is a long way from anywhere to put
things down, and how much can be carried is part of how deep it is worth going.
That is a live tension in a game whose whole strategy layer is a day budget.
Adding slots would be a flat power gain with nothing traded for it.

### Why 48 and not 33

Diablo IV's dungeons are minutes long with a free town portal at the end. Here a
dungeon is many floors at a day each, so the gap between chances to put something
down is far larger and the number should sit above that anchor rather than at it.
Diablo III used 60. 48 is four rows of twelve, which is Path of Exile's grid
width, and it sits between the two. **The number is a tuning value; the rule is
that it does not change.**

### The Explorer quadrant does not need a replacement node

The issue asked whether removing Weightless Spoils leaves the Explorer quadrant's
tier 3 one node lighter. **It does not, and the premise is worth correcting.**
Issue #260 established that the prose file predates the node graph, so Weightless
Spoils was never in the graph to be removed. The prose file lost an entry; the
tree lost nothing and is exactly the size it always was. There is no hole to fill.

### What argues against it

**A fixed bag in a game with day-priced dungeons is harsher than a fixed bag
anywhere else.** In all three surveyed games a full inventory costs seconds. Here
it may cost days or a whole dungeon, and this entry does not say which, because
the design does not say. That is issue #323, filed with this decision and
labelled `needs-operator`, and it is the question that decides whether 48 is
generous or crippling.

**It removes the only stated reward for the Explorer quadrant's depth theme.**
Weightless Spoils was thematically the node that paid a deep-diving player for
carrying more. Nothing replaces that, and the entry above explains why nothing
needs to structurally — but the theme is now one idea lighter than the prose file
suggested it was.

**48 is a construction, not a measurement.** It is built from two other games'
numbers and one grid width. No play has tested it.

### Sources

Search-result summaries rather than the pages themselves.

- Diablo IV: 33 fixed slots, one item per slot, cannot be increased, with
  Blizzard's stated reason.
  https://www.gamepur.com/guides/how-to-increase-inventory-space-in-diablo-iv and
  https://www.vhpg.com/diablo-4-inventory/
- Diablo III used 60 slots, and the comparison is a standing player complaint.
  https://us.forums.blizzard.com/en/d4/t/inventory-is-too-small-d3-60-slots-d433-slots-and-rings-and-gems-take-up-twice-the-space/13691

---

## 2026-08-05 — Gold is held by the account, once per lethality mode

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #306.

### The gap

The design document mentioned gold four times and never said who owns it. Gold is
earned from dungeons and side quests and spent at the capital on crafting,
enchanting and socketing, and nothing said whether one character's gold is
reachable by another character on the same account.

That mattered because issue #285 partitioned the shared stash and the auction
house by lethality mode so gear cannot pass from a Standard character to a
Heretic one. Whether that closed the gold route depended entirely on this
question. The rule written for #285 was phrased to hold under both readings —
"anything the account shares between characters is held once per lethality mode"
— so nothing in the document was wrong; what was missing was the fact that
decides which branch applies.

### What was decided

**Gold is held by the account, once per lethality mode.** Three balances, nothing
moving between them, and a Solo Self-Found character's gold private to itself
like its empire tree.

### The argument that settled it came from inside the document

The Empire-Wide Upgrades section already says:

> Making another character in a mode already being played costs that character's
> levels and gear **and nothing else**, and the meta-progression carries over.

Gold held per character would be a third thing lost, and that sentence would be
false. The design had already committed to the answer without noticing.

It is also the reading that needs no new rule. The general partition rule was
written to cover anything the account shares, so account-held gold is partitioned
by it automatically. Per-character gold would have needed a sentence saying gold
is the exception to a rule everything else follows.

### The genre survey, re-checked

**This issue's body originally recommended the opposite**, on the grounds that
three of four surveyed games hold gold per character. That count was wrong, a
correction was posted to the issue the same day, and both corrected claims were
re-checked on 2026-08-05 before this decision was made.

| Game | Gold |
|---|---|
| Diablo IV | Account-shared, partitioned by Hardcore: hardcore characters share gold only with other hardcore characters |
| Last Epoch | Account-shared in softcore; hardcore is completely separate, with its own stash and gold |
| Diablo III | Account-shared from patch 2.0, partitioned by Hardcore and by season |
| Path of Exile | Per character within a league |

So three of four hold gold on the account and partition it on the same axis as
the stash, which is exactly the shape decided here. The original recommendation
had the survey backwards.

**Last Epoch is stricter than this design and the difference is deliberate.** A
Last Epoch hardcore character shares with nothing, not even another hardcore
character. Here, characters in the same lethality mode share a tree, a stash and
a balance, because the tree partition already carries the cost of switching modes
and doubling it per character would make a second Heretic character start from
nothing twice over.

### What argues against it

**It is one more thing to partition, and each partition is re-grind.** The entry
for issue #277 already records that Diablo IV removed the permanent power of
Altars of Lilith rather than keep making players re-earn it across partitions,
and that Last Epoch answered the same complaint with catch-up mechanics. Gold
joins the empire tree and the stash on that list. A first Heretic character now
starts with no tree, no stash and no gold.

**Per-character gold would have been simpler to build.** One balance on one
record, no partition key, and no question about what happens to a balance when a
character is lost. Account gold needs the balance keyed by mode everywhere it is
read.

**It makes gold a worse sink for the stash question.** Issue #305 decided the
stash is fixed rather than expandable for gold, partly because gold's owner was
unknown. That reason is now gone, so a gold-priced stash expansion is available
if a gold economy is ever designed. The entry for #305 says what would reopen it.

### Sources

Search-result summaries rather than the pages themselves; `WebFetch` cannot reach
the wiki hosts several of these sit on.

- Diablo IV: gold shared account-wide, hardcore characters excluded from the
  shared stash and gold except with other hardcore characters.
  https://segmentnext.com/diablo-4-shared-between-characters/ and
  https://maxroll.gg/d4/resources/hardcore-guide
- Last Epoch: gold shared among softcore characters, hardcore completely
  separate. https://www.vhpg.com/last-epoch-transfer-items-between-characters/
  and https://forum.lastepoch.com/t/any-way-to-transfer-items-from-sc-to-hc/49680
- Diablo III: gold account-shared since patch 2.0, split by Hardcore and season.
  https://www.diablowiki.net/Stash

---

## 2026-08-05 — The shared stash is 600 fixed slots, free, and holds no gold

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #305.

### The gap

`docs/Cataclysm_GDD_v2.md` referred to a shared stash five times and never
described one. Every mention was a statement *about* the stash — Solo Self-Found
does not have one, it is partitioned by lethality mode, it carries no fees — and
none said what it is. Issue #285 partitioned it in a document that had never said
how big it was, what it held, or whether a character had one of its own.

### What was decided

There is no operator answer on this issue. The section was written under the
constraints the document already carries, and every choice below names what
constrained it.

| Question | Answer | What decided it |
|---|---|---|
| How large | 600 slots, six tabs of 100 | Anchored to Path of Exile 2's four free tabs of 144 = 576 |
| Does it grow | No | See below |
| Tabs, and are they bought | Six, cosmetic only, nothing to buy | The monetisation section already says "no stash or storage fees of any kind" |
| What it holds | Gear, gems, crafting materials | Gold is a balance, not an item |
| Does a character have a private stash | No | Path of Exile, Diablo IV and Last Epoch all have one shared store and a carried inventory |
| Does the auction house draw from it | Yes | It is why Solo Self-Found loses the market and the stash together |

### Why fixed rather than sold for gold

**Selling stash tabs for in-game gold is the common answer and it was not taken.**
Diablo IV starts a player at 50 shared slots and sells a second 50 for 100,000
gold. Last Epoch sells tabs for gold up to a limit of 200. Both would fit a game
that has already ruled out selling storage for money.

Two things stopped it.

**It rested on an undecided question, and that reason has since gone.** A stash
shared by the account, bought with gold, needs to know whether gold belongs to
the account or to the character. That was issue #306. It was answered the same
day, later than this entry: gold is an account balance held once per lethality
mode, which is the reading that needs no extra rule for one character funding an
account-wide expansion. **So only the second reason below still stands.**

**There is no other gold sink written down.** This design prices every capital
service in days rather than gold. Gold has sources — side quests, The Midas Touch
capstone, the auction house — and no stated sink at all. Setting the first one in
isolation would be a number with nothing to calibrate against, and a wrong first
sink is worse than none because everything after it is priced relative to it.

**What would argue for revisiting it.** Half of the condition is already met:
issue #306 settled gold as account-held on 2026-08-05. If a gold economy is
designed with more than one sink in it, a gold-priced expansion becomes the cheap
and genre-normal answer. The rule that would change is only "it does not grow";
nothing else in the section depends on it.

### The case against a fixed stash

**It removes a reward.** Diablo IV grants tabs for seasonal participation and
Last Epoch for accumulated gold, and in both cases the extra space is something
to work towards. A fixed stash gives a player nothing to earn in that direction
at all.

**600 may be badly wrong in either direction, and nothing here can tell.** This
game has 18 equipment slots, eight Cataclysm types with type-specific affixes
that reward keeping more than one set, gems, and crafting materials. That argues
for more storage than a game with fewer item categories. Against that, the
document commits to no storage fees, so a stash that is too small cannot be fixed
by paying. The number is an anchor, not a measurement, and it says so where it is
written.

**It interacts with an already-cut node.** Issue #260 recorded that Weightless
Spoils, an empire tree node granting 10 inventory slots, exists in the prose
description of the tree and not in the node graph, so it was never built. With
this decision, neither the carried inventory nor the stash has any scaling source
anywhere in the design. Whether that is right for the carried inventory is issue
#308; this entry settles only the stash.

### Sources

All are search-result summaries rather than the pages themselves, because
`WebFetch` cannot reach the wiki hosts several of these sit on.

- Path of Exile 2 stash tabs: four free tabs, 144 slots each; premium and quad
  tabs sold for points. https://mobalytics.gg/poe-2/guides/stash-tabs and
  https://vulkk.com/2025/02/20/path-of-exile-2-stash-tabs-explained/
- Diablo IV: 50 shared slots to start, a second 50 for 100,000 gold, further tabs
  through seasonal participation. https://vhpg.com/diablo-4-stash/ and
  https://us.forums.blizzard.com/en/d4/t/max-number-of-stash-tabs/211105
- Diablo IV: inventories are per character, the stash is shared, and gold and
  materials move between characters.
  https://primagames.com/tips/diablo-4-shared-stash-how-to-share-equipment-between-your-characters
- Last Epoch: tabs cost in-game gold only, rising in price, limit 200, with
  naming and colour-coding. http://www.vhpg.com/last-epoch-stash-tabs/ and
  https://www.icy-veins.com/last-epoch/stash-tab-organization

---

## 2026-08-05 — The empire tree can be respecced in days, and its four capstone choices are inherited

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #288.

### Two questions, answered together

**Could the empire upgrade tree be respecced?** `docs/Cataclysm_GDD_v2.md` listed
one respec service, "Trainer | Respec passive skill points". Everywhere else the
document says "passive skill point" it means the class trees — "Per level: 1
passive skill point". The empire tree is spent with empire upgrade points, a
different currency with a different name, so the Trainer row did not cover it and
nothing else did.

**And were the four capstone choices meant to be inherited already made?**
`docs/Empire_Development_Tree_Final.json`, the empire tree node graph, holds four
decision capstones at 25, 50, 100 and 200 points, each one choice from three:

| Threshold | Capstone | The three options |
|---|---|---|
| 25 | Foundations of the Empire | The Aegis of Hope, The Delver, The Hoarder |
| 50 | Edicts of Power | The Sentinel, The Collector's Decree, The Soul Forge |
| 100 | The Imperial Vanguard | The Warlord, The Master Tinkerer, The Midas Touch |
| 200 | The Imperial Zenith | Imperial Prowess, The Last Stand, The Flood Barrier |

Under the account-wide rule from issue #273, the second character in a lethality
mode arrives at a tree where all four are already chosen and never faces the
decisions.

### Why they are one decision and not two

Respec is the mechanism that would let a player revisit an inherited capstone. If
the tree can be respecced, inheriting the choices costs little. If it cannot, the
first character in a mode fixes four decisions permanently for every character
that follows it, for the life of that mode's tree.

### What was decided

Confirmed by the project owner on issue #288, 2026-08-05: "Your recommendation is
correct."

**The empire tree can be respecced, at the Trainer, for a cost in days.** The
number of days is left as a tuning value. **The four capstone choices belong to
the tree and therefore to the lethality mode**, so they are inherited already
made and can be changed by respeccing like any other allocation.

### Why a cost in days rather than gold or nothing

`docs/Cataclysm_GDD_v2.md` already says every capital service is paid for in
time: "All services cost time, reinforcing the time pressure." Pricing an empire
respec the same way needs no new currency and no new rule, and it keeps the
choice real, because a day spent at the capital is a day not spent defending the
empire. A free respec would make the capstones not decisions, which is what the
node graph calls them.

### What argues against it

**It weakens the capstones as identity.** A choice that can be undone for a fixed
number of days is a preference, not a commitment. The strongest version of these
capstones would be permanent per lethality mode, and that version was rejected
because of what it does to inheritance rather than because of what it does to the
choice.

**The day cost is unspecified, so the decision is only half-priced.** A respec
costing 1 day is effectively free and a respec costing 60 is effectively
permanent, and the decision above is compatible with both. The number has to come
from play. This is recorded rather than solved.

**It gives a mature account a lever a new one does not have.** A player with a
200-point tree can re-cut all four capstones for one payment in days, which is a
larger swing than any single choice a new character makes. That follows from the
tree being account-level and is not specific to respec.

---

## 2026-08-05 — A Solo Self-Found empire tree survives the character that earned it

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #286.

### The question

`docs/Cataclysm_GDD_v2.md` says that a character consumed by Worn Residue is
gone, the run ends, and "Empire progress is kept". That sentence was written when
empire progress had one meaning: the account keeps it. Issue #273 then made the
empire upgrade tree account-owned **except** under Solo Self-Found, where the
tree belongs to one character and is shared with nothing.

So for a Solo Self-Found character, consumption destroys the only owner of its
tree, and "Empire progress is kept" had no referent. Kept by whom?

### What was decided

**By the project owner, 2026-08-05, on issue #286:**

> Regardless of what mode you're playing on, the empire tree persists. The game
> operates like a rogue like in that way. Death isn't the end, but you restart the
> tier you were on while keeping your gear/levels/empire tree

Applied to the question this issue asked: **the tree is never destroyed, in any
mode.** A lost Solo Self-Found character's private tree is held, and the next
Solo Self-Found character created in the same lethality mode inherits it instead
of starting from nothing.

The "a second Solo Self-Found character starts from nothing again" rule still
holds and is not in conflict. It is about a second character played *alongside*
the first. The rule added here is about the *successor* to one that was lost.

### Why inheritance rather than destruction

Nothing else in this design destroys empire upgrade points. A failed run keeps
them; a death keeps them; the meta-progression system is built on the promise
that a run is never wasted, and the document states that promise twice.

Destroying the tree would have made being consumed the single failure in the game
that costs meta-progression, and it would have fallen only on Solo Self-Found —
the harshest flag, and the one where the tree took longest to build because it
starts from nothing and is fed by one character. It would also have made a tree
worth less the larger it grew, because the amount at risk rises with every point
spent, which inverts the reason to invest in it.

For scale: a Hardcore death costs 10 days and about 1.8 of 18 equipped pieces,
and the run continues. Destroying an arbitrarily large tree for one lost fight is
not the same order of penalty as anything else in the design.

### The case against

**Worn Residue is described as the one permanent cost, and this softens it.**
`docs/Cataclysm_GDD_v2.md` says residue "is the only way the Forge can cost a
player anything permanent". Consumption still costs the character — its levels,
its gear, and its place in the run — which is permanent. But for Solo Self-Found
it no longer costs the meta-progression, which was the largest thing it could
have cost.

**A player can now lose a Solo Self-Found character deliberately and keep the
tree.** There is no gain in doing so: the successor starts at level one with no
gear and the same tree, which is strictly worse than not being consumed. So the
exploit does not pay, but it does mean consumption cannot be used as a
meta-progression sink.

**The inheritance needs a rule for which character receives it.** The document
says the next Solo Self-Found character created in the same lethality mode. That
is simple and needs no player choice, but it means a player who wants a fresh
private tree must first create a character to absorb the held one. Recorded as a
consequence rather than solved; it surfaces when the save format is designed,
which is issue #21.

### What this deliberately does not decide

The owner's answer also says "you restart the tier you were on while keeping your
gear/levels/empire tree". Read literally, keeping gear and levels means a run
ending does not cost the character, which would contradict consumption destroying
it and contradict the paragraph headed "Why the run ends rather than the character
being replaced mid-run". **That is a separate question and was not decided here.**
It is issue #315, which quotes the sentence and lists both readings.

**It has since been answered, on 2026-08-05, and the answer is that a run ending
does not cost the character.** See the entry headed "A run ending costs the run,
not the character" in this file. That answer does not change anything decided
above; the tree still survives, and the reason it survives is now one the
document never needs to use, because nothing destroys a character.

---

## 2026-08-05 — Repeated displacement is limited by halving its distance, not by immunity

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #302.

Issue #297 put displacement outside the anti-stun-lock rule, because being pushed
four meters costs the target the distance and nothing else — it can act on
arrival. The document then said, correctly, that this did not mean displacement
should be repeatable without limit, and pointed here for the limit.

**The rule: each displacement applied to a target that has already been displaced
within the last 5 seconds moves it half as far as the one before.** Full
distance, then half, then a quarter. The count resets after 5 seconds with no
displacement applied to that target. No damage threshold, no immunity flag, no
boss exemption.

## What the genre does, and why the two shipped answers differ

| Game | How it limits repeated knockback |
|---|---|
| Path of Exile | It does not need to. Knockback does not interrupt the target's actions at all, which is the documented difference from stun |
| Path of Exile 2 | Treats **distance** as the quantity. Skills carry increased knockback distance; defensive modifiers such as Hunker Down reduce the distance an incoming knockback moves you. No immunity flag |
| Diablo IV | Escalating resistance. Each knockback adds a flat **40%** hard crowd control resistance, 20% per tick for effects that apply it continuously. Knockback and pull stop working once that resistance reaches **65%**, so two applications is the practical limit. Separately, each second of hard crowd control suffered adds 10%, capping at 95% |

**The two Diablo IV sources issue #302 flagged as disagreeing are both right.**
One said the hard crowd control pool excludes knockback; the other gave knockback
a flat 40% per application with immunity at 65%. Those compose: knockback is
excluded from the *duration-based* accumulation that everything else feeds, and
has its own per-application escalation with its own lower threshold.

## Why this design took Path of Exile 2's axis and Diablo IV's escalation

**Immunity works in Diablo IV because knockback there comes from skills that can
be repeated quickly. Here it cannot.** All nine displacing skills are in the
Heavy or Movement slot and nothing in any other slot displaces, so the case
Diablo IV's threshold exists to prevent is already bounded by which slots the
effect lives in. A Heavy attack is the slow one by design and a Movement skill
goes on cooldown. Adding a hard immunity on top of that would be a second limit
on a problem the slot layout already limits.

That count is checked against `game/Data/WeaponSkills.csv` by a test rather than
asserted here, because it is the load-bearing fact and it will move.

**Halving is the only option under which no skill ever visibly does nothing.**
Bull Rush and Cinder Rush charge through a crowd "knocking them aside". Under an
immunity flag the player would run through enemies that do not react, which reads
as a defect rather than as a rule. A halved shove still looks like a shove.

**It reuses the one number the section already has.** The 5 second window is the
stun immunity window. Diablo IV's shape would have needed three new numbers: an
amount per application, a threshold and a decay rate.

**No boss exemption, unlike stun.** A boss cannot be stunned at all, because a
boss held still is not a fight. A boss pushed four meters is still fighting, so
the reason does not carry across. Making a boss unpushable would also make the
two charge skills pass through it with no effect, which is the visible-failure
problem again.

## What argues against it

**It is not what the only game that solved this problem did.** Diablo IV chose a
hard threshold and this chose a soft curve, and the reason given — that the slot
layout already bounds repetition — depends on a fact about this project's current
skill list rather than on a principle. **If a displacing skill is ever added
outside the Heavy and Movement slots, this decision should be re-read**, because
the argument for the soft curve weakens immediately. A test fails when that
happens.

**Halving never reaches zero.** After six applications a 4 meter shove is 6
centimetres, which is not a problem in play but is not a clean stop either. A
threshold gives an exact answer to "can this be repeated" and a curve does not.

## What this does not change

Outright immunity to displacement still exists as a skill effect. Living Pyre,
Unstoppable Force and Forge Stance state that their user cannot be knocked back,
and Bull Rush and Cinder Rush grant immunity to all crowd control while charging.

**Nothing in the game can currently knock the player back.** `EnemyModifiers.csv`
contains no displacement and neither does `StatusEffects.csv`, so those five
clauses are written against a threat the data does not yet contain. Issue #310
carries that; the rule above is written for both directions so it does not depend
on the answer.

Sources:
[Knockback — Path of Exile 2 Wiki](https://www.poewiki.net/wiki/poe2wiki:Knockback),
[PoE 2 Guide: Knockback Explained — Mobalytics](https://mobalytics.gg/poe-2/guides/knockback),
[Knockback — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Knockback),
[Crowd Control — Diablo Wiki](https://diablo.fandom.com/wiki/Crowd_Control),
[Diablo 4 Knockback — vhpg](https://vhpg.com/diablo-4-knockback/).

**Evidence limit.** All five are search result summaries rather than fetched
pages; `WebFetch` gets HTTP 402 from the Fandom hosts and an access-denied page
from `poewiki.net`. The Diablo IV numbers, 40% per application and immunity at
65%, are the most specific claims here and came from two independent summaries
that agreed, which is the strongest the evidence gets without a fetch.

---

## 2026-08-05 — Three empire tree ideas in the prose were never built, not lost

**Affects** `docs/Empire_Skill_Tree_Keystones.md` and `docs/README.md`. Applied in
full. Issue #260.

**Decided by the project owner on 2026-08-05:**

> For now, the empire tree json is the authoritative source. The prose was just
> there when I was originally brainstorming before I built the PassiveTreeCreator
> app.

## What the question was

The empire passive tree is described twice. `docs/Empire_Development_Tree_Final.json`
is the node graph the passive tree editor reads and writes.
`docs/Empire_Skill_Tree_Keystones.md` is prose describing the same tree. Issue #25
reconciled them and found the graph newer and larger: 173 distinct names against
the prose's 108, with 105 of the prose's names appearing in the graph.

Three did not, and **nobody had recorded whether they were cut on purpose when the
tree was rebuilt or lost in the rebuild.** The distinction mattered because two of
the three were load-bearing: one was a whole tier's namesake and one was the only
thing in the design granting inventory slots.

## The answer, and why it settles all three at once

The prose predates the tool. It is a brainstorm written before the passive tree
editor existed, so it was never a description of a built tree that could lose
nodes — it is a list of ideas, some of which were later built and some of which
were not. **An idea in the prose with no node in the graph was never built.**
There is nothing to restore.

That is also consistent with the only other evidence available.
`docs/Empire_Development_Tree_Final.json` has two commits in its entire history,
both from 2026-08-02 or later, and it arrived from Google Drive already in its
current state. There is no earlier version of the graph in which the three nodes
could have existed and then been deleted.

## What the three were, recorded here because the prose no longer holds them

| Name | Where in the prose | What it did |
|---|---|---|
| **Bounties** | Treasury quadrant, tier 1 | Every dungeon has a 10% chance to spawn a "Bounty Target" (Elite) that drops a massive sack of Gold. |
| **Weightless Spoils** | Explorer quadrant, tier 3 | Adds 10 inventory slots. |
| **The 4 Decision Nodes** | Architect quadrant, tier 3 | (Demonic/Celestial, etc.) — Choose one for each to get 25% Resistance. |

These are the exact bullets that were deleted. Anyone who wants one of them back
builds it in the editor at `C:\Projects\PassiveTreeCreator`, which opens and saves
the JSON directly. The prose follows the graph and never leads it.

**The graph has a node named Bounty**, granting +5% Loot Quantity per point. Same
word, unrelated effect. It is not a survival of the prose's Bounties and was not
treated as one.

**The Architect tier 3 heading changed.** The prose called it "The Adaptive
Bulwark (Decision Tier)", named after the decision node that turns out never to
have been built. The parenthetical is gone; the tier is still The Adaptive
Bulwark. Nothing else names it — the graph's on-canvas tier labels read plainly
"TIER 1" through "TIER 4".

## What this leaves open

**Dropping Weightless Spoils leaves nothing in the design granting inventory
slots, which is issue #308.** Searching the graph for the word finds no node, and
`docs/Cataclysm_GDD_v2.md` does not state an inventory size either. The likely
answer is that inventory is fixed and does not scale, which is what Path of Exile,
Diablo IV and Last Epoch all do, but it has to be written rather than left absent.
That question overlaps issue #305, the shared stash having no design of its own.

The other two leave nothing open. Gold-dropping Elites and Cataclysm-type
resistance both have other sources in the tree.

---

## 2026-08-05 — The shared stash and the auction house are partitioned by lethality mode

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #285.

**Decided by the project owner on 2026-08-05.** The issue offered three options
and the answer was option 1, the widest one: one stash and one auction house per
lethality mode, not one per account.

## What was wrong

The empire upgrade tree was partitioned by lethality mode on 2026-08-05, for the
reason the project owner gave on issue #277:

> so you can't run up your empire tree in normal and then switch to the hardest
> mode and have a huge head start.

That sealed the meta-progression route and left the equipment route open. Gear
and crafting materials could still pass from a mature Standard character to a
first Heretic character through the shared stash or by self-trading on the
auction house, and **a geared handoff is a larger head start than any number of
empire upgrade points.** A rule that seals the small channel and leaves the large
one open is harder to explain than sealing both or sealing neither.

## What the genre does

Every game surveyed partitions its stash on the same axis as its
meta-progression. None partitions the two on different axes.

| Game | Meta-progression partition | Stash partition |
|---|---|---|
| Path of Exile | Atlas passive tree, per league, where league includes the Hardcore flag | Stash contents, same league partition. Purchased tab *slots* cross; the items in them do not |
| Diablo III | Paragon, four tallies across realm and Hardcore | Stash, same partition |
| Diablo IV | Renown, Altars of Lilith, across realm and Hardcore | Stash **and gold**, same partition. Four stashes per account |
| Last Epoch | Cycle, plus the Hardcore and Solo flags | Stash, **gold** and crafting materials, same partition |
| Grim Dawn | Shared progress stored separately for Hardcore and Softcore | Stash, same partition, in two separate files: `transfer.gst` for Softcore and `transfer.gsh` for Hardcore |

**Grim Dawn is the cleanest case**, because it carries both kinds of axis on one
character-creation screen and treats them differently on purpose. Hardcore, a
categorical permadeath flag, splits the account layer completely. Veteran, a
numeric difficulty step, does not: Veteran characters share a stash with
non-Veteran ones. This design's lethality mode is the first kind, and its
difficulty tier is the second, which is why the partition follows the mode.

**Path of Exile draws a line worth copying.** Buying a stash tab with real money
grants the slot in every league, including Hardcore. The items inside it stay in
the league they were put in. So the entitlement crosses and the contents do not.
If this game ever sells storage the same split applies — although the
monetisation section already rules out selling it, at line 3109 of
`docs/Cataclysm_GDD_v2.md`: "no stash or storage fees of any kind".

## What argues against it

**Three markets instead of one, in a game that will not have a deep market
anyway.** Auction house liquidity falls with population and this splits the
population three ways on top of any seasonal split. The cost is real. It is
smaller here than in the games above because this is a single-player-first design
with co-op listed as a Phase 2 feature, so the market was never going to be deep,
and because the harder modes will hold a minority of players — which means the
Heretic market is the one that will feel thin.

**Confirmed 2026-08-12.** Issue #501 challenged this sentence, because section XIV
of the design document described a free-to-play live service at the time and that
is not a single-player-first product. The business model decision that day settled
it the other way: the game is bought once, following Last Epoch rather than Path
of Exile. **So this reasoning stands as written**, and the premise it rests on is
now stated in section XIV rather than contradicted by it.

**Nothing was migrated, because there is nothing to migrate.** No save format
exists, no player has a stash, and the partition is being written before any
storage code. Had this been decided after launch it would have needed a migration
rule for items already in a shared stash, and that rule has no good answer.

## The rule as written

Anything the account shares between characters is held once per lethality mode,
never once for the account. Anything a character holds by itself needs no rule at
all, because the lethality mode is locked at character creation and never
changes, so a character stays in one partition for its whole life.

Solo Self-Found is unaffected. It already has no auction house and no shared
stash, so there is nothing for the partition to divide.

## What this deliberately does not settle

| Issue | Question |
|---|---|
| #305 | The shared stash has no design beyond its name. Nothing says how large it is, whether it has tabs, or what it can hold. |
| #306 | The document does not say whether gold is held by the character or by the account, so it cannot say whether this rule touches gold. Two of the five games above partition gold with the stash. |

The rule above was written to be true under either answer to #306, which is why
it names the container rather than the currency.

Sources:
[Stash — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Stash),
[Stash — Diablo Wiki](https://diablo.fandom.com/wiki/Stash),
[Diablo 4 Hardcore Survival Guide — Maxroll](https://maxroll.gg/d4/resources/hardcore-guide),
[Any way to transfer items from SC to HC? — Last Epoch forums](https://forum.lastepoch.com/t/any-way-to-transfer-items-from-sc-to-hc/49680),
[Hardcore shared stash question — Grim Dawn discussions](https://steamcommunity.com/app/219990/discussions/0/141136086937425918/).

**Evidence limit, stated because the project rule requires it.** These five are
search result summaries of those pages, not the pages themselves. `WebFetch`
receives HTTP 402 from `pathofexile.fandom.com` and `diablo.fandom.com` and an
access-denied page from `poewiki.net`. The Grim Dawn file names `transfer.gst`
and `transfer.gsh` are the most specific claim here and are the one worth
re-checking first if any of this is ever relied on.

---

## 2026-08-05 — A knockdown is covered by the anti-stun-lock rule; a knockback is not

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #297.

**The governing test, stated by the project owner on 2026-08-05:**

> Slow is not covered by anti stun rule. Only hard stuns, things that completely
> stop you from operating any part of your character. The rest is manageable from
> a player perspective.

That is now the criterion in the document, and it answers this issue and several
others at once. An effect is covered when it completely stops the target
operating any part of its character. A slow, a damage reduction, a displacement
and a disarm all leave the target able to act, so none of them is covered. A stun
and a knockdown do not, so both are.

**The question as filed was whether knockback carries the stun damage threshold
and the 5 second immunity window. That turned out to be the wrong question**,
because this project has two different effects under that one word and the
criterion above sends them opposite ways.

Reading `game/Data/WeaponSkills.csv`, twelve skills displace an enemy and two
knock one down:

| Effect | Example | What it does to the target |
| ----- | ----- | ----- |
| Displacement | Haymaker, "knocks them back 4 meters" | Moves it. It can act on arrival |
| Knockdown | Warlord's Decree, "knocked down for 2 seconds" | Stops it acting for a stated time |

**A knockdown is a hard stop.** A target on the floor for 3 seconds is operating
no part of itself. That also matches the document's existing reasoning, written
before the criterion above was stated: Cripple's slow caps below total "because a
full stop is a stun", and Weaken's reduction caps because "an enemy that deals no
damage is harmless, which is a stun by another name".

**The numbers make leaving it out untenable.** Every stun a skill grants runs 0.75
to 1.5 seconds. Warlord's Decree knocks down for 2 and Cataclysm for 3. So if
knockdown were outside the rule, the longest hold in the game would be the one
nothing limits, and Cataclysm would hold a boss still for 3 seconds while Shield
Bash's 1.5 second stun does nothing to it. The stated reason for boss immunity is
that "a boss that can be held still is not a fight", and that reason does not care
which word the skill used.

**The cost of this decision, stated plainly.** Warlord's Decree and Cataclysm are
Ultimates, and their knockdown now does nothing to a boss. That is a real loss to
two signature skills. It is accepted because the alternative is a rule that can be
stepped around by wording, and because both skills keep their damage, their armor
shatter and their fissure, which is most of what they are. It is written in one
paragraph and is reversible if play says otherwise.

**They share one immunity window rather than one each.** Two 3 second holds taken
in turn is exactly the failure the window exists to stop, and giving stun and
knockdown separate windows would allow it.

**Displacement is not covered**, because it does not hold the target still. No
damage threshold, so a weak hit can still shove, and a boss can be pushed. That is
not the same as saying it should be repeatable without limit, and what limits it
is issue #302.

### What the genre does

| Game | Knockback | Act-prevention |
| ----- | ----- | ----- |
| Path of Exile | Displacement only; explicitly does not interrupt the target's actions | Stun, separately |
| Path of Exile 2 | Movement, to create space | Heavy Stun; the target counts as Immobilised and is harder to Heavy Stun again for a short time |
| Diablo IV | Knockback pushes. **Knock Down is a separate effect that pins the target in place, and Diablo IV's own documentation says it does not count as a Stun** | Stun, plus a hard crowd control resistance that accumulates |

All three separate displacement from act-prevention. **Diablo IV separates them
under exactly these two names**, which is the strongest support for the split, and
also shows that not being a stun does not mean being unlimited: it limits repeated
knockback by an escalating resistance instead of by a damage threshold.

Diablo IV's treatment of Knock Down is a counter-case worth recording: it says
Knock Down does not count as a Stun, where this decision says a knockdown is
covered by the stun rule. The difference is that Diablo IV routes all hard crowd
control through one accumulating resistance, so its Knock Down is limited by that
pool whether or not it is called a stun. This project has no such pool, so leaving
knockdown outside the rule would leave it limited by nothing at all.

### Sources

- [Knockback — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Knockback)
- [Knockback — Path of Exile 2 Wiki](https://www.poewiki.net/wiki/poe2wiki:Knockback)
- [Heavy Stun explained — Path of Exile 2](https://mobalytics.gg/poe-2/guides/stun)
- [Knock Down — Diablo 4](https://www.purediablo.com/diablo4/Knocked_Down)
- [Crowd Control — Diablo Wiki](https://diablo.fandom.com/wiki/Crowd_Control)
- [Crowd control status effects — Diablo IV, Icy Veins](https://www.icy-veins.com/d4/guides/crowd-control-status-effects/)
- [Stun — Last Epoch Wiki](https://lastepoch.fandom.com/wiki/Stun)

**Evidence limit.** The two Path of Exile Fandom pages and the Diablo Fandom page
could not be fetched directly; both return an access error to an automated
request. What is recorded above comes from search summaries of those pages rather
than from the pages themselves. The Diablo IV sources also disagree about whether
knockback feeds the same resistance pool as stun. Neither affects this decision,
which rests on the split between displacement and act-prevention that all three
games make, but it should be confirmed before #302 copies any number.

---

## 2026-08-05 — A slow is not covered by the anti-stun-lock rule, and the document only says it once now

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #296.

The section "Stun and the Anti-Stun-Lock Rule" said two contradictory things four
lines apart. One paragraph said a slow is not a stun and is not covered by the
rule. The next said whether a slow carries the same threshold and window as stun
was still open. Both cannot be true.

**The first is correct, and the reason it is correct is already load-bearing
elsewhere in the document.** The ailment section says Cripple's slow caps below
total "because a full stop is a stun, and stunning is a separate mechanic with
its own counter in Crowd Control Resistance", and says Weaken's reduction caps
for the same reason. That cap only makes sense if a slow and a stun are governed
separately. If the anti-stun-lock rule covered slows, the cap would be doing the
rule's job twice.

So the second paragraph was the error. It was written when the open questions
about crowd control gear were first listed, and it over-stated what was open by
sweeping a settled thing in with two unsettled ones.

**Knockback stays open, and is not the same question.** A knockback takes control
away completely for the moment it applies, which is what the rule limits, but it
is brief and does not hold the target still afterwards, so it resembles neither a
stun nor a slow closely enough to settle by analogy. Issue #297 carries it.

**What was not changed.** The rule itself, its three parts, and the numbers in
them. This entry records which of two existing statements is the surviving one,
not a new decision about how slows behave. Nothing in
`sim/cataclysm_sim/damage.py` changed, because the model never applied the rule
to slows in the first place.

---

## 2026-08-05 — The empire upgrade tree belongs to the account, except under Solo Self-Found

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied in full. Issue #273 settled who
owns the tree; issue #277 settled what the sharing is scoped to. Both halves are
now written; see "What the sharing is scoped to" below.

## The question

`docs/Cataclysm_GDD_v2.md` said twice that empire upgrade points "persist through
all runs — including failed ones", and never said anything about characters. So
it was unstated whether a new character starts the primary meta-progression
system over.

## The answer, 2026-08-05

The project owner: **"account wide, unless solo self found."**

Every character on the account shares one empire upgrade tree. A Solo Self-Found
character has its own, shared with nothing.

## Why the exception follows from what the flag already means

A Solo Self-Found character has no auction house and no shared stash. Inheriting a
mature account's empire tree would be a larger handout than either of those, and
would be the one shared resource the flag did not close off. The flag means
starting the whole game from nothing, so the empire tree has to be included.

## Why account-wide for everyone else

The design document calls the tree "empire-wide" and "the primary meta-progression
system", and the pitch it is written against — no run is wasted, each attempt is
stronger than the last — reads as a property of the game rather than of one
character. A per-character tree would make that promise much weaker than the
wording suggests.

It also makes issue #255 cheap. That issue locked the lethality mode and the Solo
Self-Found flag at character creation, and the main argument for locking was that
rerolling is affordable. That is only true because the empire tree survives a new
character.

The cost is that a new character on a mature account skips the early difficulty.
That is real, and it is what the second half of the answer was aimed at.

## What the sharing is scoped to

The rest of the answer was: **"And it should only apply to the same difficulty
tier so you can't run up your empire tree in normal and then switch to the hardest
mode and have a huge head start."**

That sentence scopes the sharing, but the game has two difficulty axes and the
sentence used words from both. "Difficulty tier" is the T1 to T8 content axis.
"Normal" and "the hardest mode" read as the lethality axis, where the modes are
Standard, Hardcore and Heretic. Three trees per account or eight is not a small
difference: `docs/Empire_Development_Tree_Final.json`, the empire passive tree,
has 159 nodes and 1,248 allocatable points.

**Nothing was written for that half at the time**, because writing a scope rule
that turns out to be on the wrong axis is harder to notice and undo than leaving
it absent. Issue #277 carried it with both readings.

**Answered 2026-08-05: the lethality mode.** Standard characters share one empire
upgrade tree, Hardcore characters share a second, Heretic characters share a
third, and each Solo Self-Found character has its own on top of that. Three shared
trees per account plus one per Solo Self-Found character.

## Why the points are scoped and not only the tree

The most natural way to build "three trees" is one account-wide balance of empire
upgrade points with three separate allocations of it. **That would not do what was
asked for.** A player could farm points on Standard and spend them into Heretic,
which is precisely the head start the answer was aimed at.

So the design document states both: a point is earned into the earning character's
lethality mode and can only be spent there, and there are three balances rather
than one.

This also settles where a point goes in co-op without needing a co-op rule. It
goes to the lethality mode of the character that earned it. Whether a party may
mix lethality modes at all is a different question and belongs to #31.

## Why the lethality mode and not the difficulty tier

**No shipped game in the genre partitions meta-progression by a numeric difficulty
step.** The pattern is unanimous across six games, and in every case the numeric
axis is a source of progression or a gate on rewards rather than a divider between
pools.

| Game | Account-wide progression | Partitioned by | Never partitioned by |
|---|---|---|---|
| Path of Exile | Atlas passive tree | League, which includes the Softcore/Hardcore and Solo Self-Found flags | Map tier T1 to T16 |
| Path of Exile 2 | Atlas passive tree | League, the same way | Waystone tier T1 to T15 |
| Diablo III | Paragon levels | Realm and Hardcore, giving exactly four tallies | Torment level, Greater Rift tier |
| Diablo IV | Renown, Altars of Lilith, Codex of Power | Realm and Hardcore | World Tier, Torment |
| Last Epoch | Stash, gold, materials, faction rank | Cycle, Hardcore, and the two Solo flags | Monolith corruption |
| Grim Dawn | Recipes, illusions, shared progress | Hardcore | Veteran, which shares the stash |

**Grim Dawn is the cleanest single piece of evidence**, because it carries both
kinds of axis and treats them differently on purpose. Hardcore is chosen at
character creation, cannot be changed afterwards, and splits the account layer
completely: the transfer stash, recipes and illusions are all stored separately
for Hardcore and Softcore. Veteran is a difficulty setting that can be switched on
and off from the menu before loading a character, and it splits nothing — a
Veteran character shares the stash with its non-Veteran counterparts. The axis
that is chosen once and never changed partitions; the axis that is a difficulty
dial does not.

**Path of Exile 2 is the sharpest counter-case to the difficulty-tier reading.**
Its Atlas passive points are earned two per waystone tier, T1 through T15. The
numeric difficulty ladder is how a player fills one tree. Partitioning by it would
be architecturally backwards.

**The reason behind the unanimity applies directly here.** A numeric difficulty
step is something one character climbs during one campaign. Partitioning by it
would make a character's own progress fall out of its own pool as it advanced, and
every tier-up would orphan the work done below. A categorical mode is different in
kind: it is chosen once, never climbed, and never left.

**It also composes with the character-creation lock.** Issue #255 locked the
lethality mode at creation, in either direction and including on death, so no
character ever changes partition and the boundary needs no enforcement beyond
that.

## What argues against it, recorded because it is real

Mode partitioning is the genre norm and no clear counter-example was found. The
arguments against it are about the cost it imposes, not about whether anyone else
does it differently.

**Diablo IV shipped this exact partition and players read it as a defect.** The
game director stated publicly that Altar of Lilith unlocks are earned "once —
they're account based". The shipped behaviour partitioned them by realm and mode,
and produced a bug report titled "Altars of Lillith not account wide, only realm
wide". The lesson is not that partitioning is wrong; it is that "account-wide" is
heard by players as genuinely account-wide, so a partition has to be stated where
the player can see it. That is why the rule is now written into the Difficulty
Options section as well, which is where the choice is actually made.

**Blizzard later walked the mechanism back.** In Diablo IV Season 11 the Altars
stopped granting permanent power at all, and Renown became Eternal-realm-only.
Permanent account power that has to be re-earned in each partition generated years
of re-grind complaints, and the eventual resolution was to remove the permanent
power rather than keep re-earning it.

**Last Epoch met the same complaint and answered it with catch-up, not more
partitions.** A player thread asking for monolith corruption to be shared across
characters put it as "Imagine if you had to complete your atlas on every new char
in PoE", and the studio's stated answer was catch-up mechanics for alternate
characters.

**The Solo Self-Found rule here is stricter than the genre.** In Path of Exile,
Solo Self-Found is a league, so Solo Self-Found characters on one account share an
Atlas with each other — a second shared pool, not a private one. Only Last Epoch's
Solo Character Found matches a private per-character rule, and even that was
softened in patch 1.0.1 with a merge path. A private, permanent tree with no exit
is the strictest version anyone has shipped.

**The fan-out is wider than any surveyed game.** Three lethality modes plus one
private tree per Solo Self-Found character exceeds Path of Exile's flag
multiplication, which is itself a standing complaint about fragmenting the player
base. The cost falls on exactly the players most worth recruiting into the harder
modes.

None of this changes the decision. It is recorded so that if the re-grind cost
turns out to be the problem those studios found it to be, the response is already
known: reduce the cost of re-earning rather than remove the partition.

## What this deliberately does not settle

Five questions surfaced while writing the rule. Each is its own issue.

| Issue | Question |
|---|---|
| #285 | The shared stash and the auction house are not partitioned, so gear can still cross the boundary the tree no longer crosses. **Answered 2026-08-05: they are now partitioned the same way. See the entry at the top of this file.** |
| #286 | A Solo Self-Found character consumed by Worn Residue is the only owner of its tree, so "Empire progress is kept" has no referent. **Answered 2026-08-05: the tree is never destroyed and the next Solo Self-Found character in that mode inherits it. See the entry near the top of this file.** |
| #287 | Whether a seasonal league is a fourth partition. Every game in the table above that has leagues, cycles or realms partitions by them first; Grim Dawn, which has none, does not. Still open, and now labelled `needs-operator`. |
| #288 | Whether the empire tree can be respecced, and whether the four tier capstones being inherited already chosen is intended. **Answered 2026-08-05: it can be respecced at a cost in days, and the inheritance is intended. See the entry near the top of this file.** |
| #289 | Whether Heretic's 25% extra surge dungeons over-compensate for its tree starting empty. |

**One worry was checked and dismissed.** Content difficulty does not assume the
player has an empire tree: Enemy Score is built from the width of the difficulty
tier, and player Power Score is four additive terms — level, gear, gems and
resistances — stated in the Power Score section of `docs/Cataclysm_GDD_v2.md`.
The empire tree appears in neither. A first Heretic character at tier 1 with an
empty tree is in exactly the position of any first character on any account. What
the empty tree costs is the empire layer's run-time and resolve-timer levers, not
survivability.

## What was checked in the repository rather than looked up

- `docs/Empire_Development_Tree_Final.json`, parsed on 2026-08-05: 159 nodes and
  1,248 allocatable points, being 110 basic nodes worth 1,199, 41 keystones worth
  41, and 8 capstones worth 8.
- The Difficulty Options section of `docs/Cataclysm_GDD_v2.md` lists exactly three
  lethality modes, which is what makes the count three shared trees rather than
  some other number.
- No file under `sim/`, `game/Source/` or `game/Data/` contains "lethality",
  "SSF" or "Self-Found". Nothing in code models any of this yet.

Sources:
[Atlas completion is per account and league — Path of Exile forum](https://www.pathofexile.com/forum/view-thread/3241482),
[Atlas passive tree — Path of Exile 2 forum](https://www.pathofexile.com/forum/view-thread/3678292),
[Paragon — Diablo Wiki](https://diablo.fandom.com/wiki/Paragon),
[Altars of Lilith — Maxroll](https://maxroll.gg/d4/resources/altar-of-lilith),
[Altars of Lillith not account wide, only realm wide — Blizzard forums](https://us.forums.blizzard.com/en/d4/t/altars-of-lillith-not-account-wide-only-realm-wide/88037),
[Diablo IV Season 11 changes how Altars of Lilith work — Icy Veins](https://www.icy-veins.com/d4/news/diablo-4-season-11-changes-how-altars-of-lilith-work/),
[Game difficulties — Grim Dawn official guide](https://www.grimdawn.com/guide/game-settings/game-difficulties/).

**Confidence is not uniform and the entry should not be read as if it were.** The
Path of Exile, Diablo III, Diablo IV and Grim Dawn claims come from the sources
above and are stated with confidence. The Last Epoch patch 1.0.1 merge behaviour
and the Torchlight Infinite cross-boundary sharing were reported during research
but not confirmed against a primary source, so treat them as supporting colour.
No shipped ARPG was found that deliberately shares a spendable meta-progression
tree across its hardcore and softcore ladders, which is the absence the decision
leans on; an absence is weaker evidence than a positive finding and is recorded
here as such.

---

## 2026-08-05 — The lethality mode and the Solo Self-Found flag are both locked at character creation

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied. Issue #255.

## The question

`docs/Cataclysm_GDD_v2.md` defined difficulty as two independent choices — one
lethality mode of Standard, Hardcore or Heretic, plus an optional Solo Self-Found
flag — but never said when those choices are made or whether they can change
later. Three sub-questions were open: are they set only at character creation,
does a Hardcore death take the mode off, and can Solo Self-Found be switched off
part-way.

## The answer, 2026-08-05

The project owner chose option 1 of the three the issue offered: **"they're
locked in."** Both choices are set at character creation and never change.

## Why the genre does not settle this one

The two games this two-axis shape was taken from both resolve it with
permadeath, which this game does not have.

| Game | What it does on a Hardcore death | Why |
|---|---|---|
| Last Epoch | Converts the character to softcore, keeping its Solo Challenge flag | The character is dead and otherwise unplayable |
| Path of Exile | Migrates the character to the Standard league | Same reason |

Both convert because the run is over and the character would otherwise be
unusable. **Here the run continues.** A Hardcore death costs 10 days and drops on
average 1.8 of the 18 equipped pieces. Converting the character on death would
take a run that is still going and change its rules part-way through, on one
unlucky moment. That is a larger intervention than the games it would have been
borrowed from make, not a smaller one.

## What the alternatives cost

**Freely switchable downward** makes the Solo Self-Found flag mean nothing: play
self-found until it is inconvenient, then switch the auction house on.

**Convert to Standard on a Hardcore death** is the paragraph above.

**Locked** is the only option under which both flags mean the same thing for the
whole life of the character, which is what makes a Hardcore Solo Self-Found
character worth anything to have.

## What it costs the player

A player who wants to try a harder mode makes a new character. That is cheaper
than it first looks, because of the answer to issue #273: empire upgrade points
are account-wide except under Solo Self-Found, so rerolling costs a character's
levels and gear but not the empire meta-progression.

## Where it is enforced

`tools/tests/test_difficulty_modes.py` asserts that the section states the rule
and names all three of its consequences. No code enforces it yet, because no code
models a lethality mode or the Solo Self-Found flag — a search of every `.py`,
`.cpp`, `.h` and `.csv` file in the repository on 2026-08-05 found no occurrence
of "Self-Found", "SSF" or "lethality" outside the design document and its tests.

---

## 2026-08-05 — The anti-stun-lock rule: a damage threshold, a five second window, and bosses immune

**Affects** `docs/Cataclysm_GDD_v2.md` and `sim/cataclysm_sim/damage.py`. Applied.
Issue #216. The fourth question that issue asked is deferred and carried by #270.

## The requirement

Stated by the project owner: crowd control must not become tedious the way it is
in many games in the genre, where the smallest hit can stun and a player can be
stun-locked until they die.

## The answer, 2026-08-05, point by point against the four questions

**1. Which crowd control effects exist.** "Cripple is the only CC affix we have
right now. But I'm pretty sure enemy modifiers have wording like stun and such."

**2. The anti-lock rule.** "I agree it should be a combination of the two.
Players with a lot of health just don't get stunned from small hits at all, and
players with lower health can be stunned, but then they should get at least a 5
second stun immunity window."

**3. Does it apply to enemies.** "Bosses are immune to stun."

**4. What offensive affixes exist.** "Unknown/not full implemented yet."
Deferred.

## The check the owner asked for, and what it found

The owner asked whether enemy modifiers already use stun wording.
**`game/Data/EnemyModifiers.csv` contains no stun wording at all.** The nearest
thing is the Void Corrupted modifier, which disables player abilities while the
player stands in corrupted ground — a loss of control, but not called a stun.

Stun does already exist in the shipped data, in three other places:

| Where | What |
|---|---|
| `docs/Cataclysm_GDD_v2.md`, Weapon Sub-Types | Blunt: 10% chance to stun for 0.75 seconds, on every hit |
| `game/Data/WeaponSkills.csv` | Shield Bash 1.5s, Shockwave Leap 1s, Lunge 0.75s, Whip Swing unstated |
| `game/Data/EnchantmentsPositive.csv` | Brute's Heart 10-piece set bonus, 3 seconds |

Two Ultimates — Living Pyre and Unstoppable Force — grant the player immunity to
stun, slow and knockback for 6 seconds. One negative enchantment stuns the player
for 0.5 to 1 second after a charge skill. The `Keyword.CC` gameplay tag is
declared in `game/Config/Tags/CataclysmTags.ini` as "Crowd Control
(Stun/Slow/Freeze)" and appears on 56 rows across the three tables.

So the owner's point 1 was right about affixes and the expectation about enemy
modifiers was not borne out.

## The three rules

| Rule | What it stops |
|---|---|
| A hit must take at least **10%** of the target's maximum health to stun | Constant interruption by small hits |
| A stunned target cannot be stunned again for **5 seconds** | Being chain-stunned by large hits |
| **A boss cannot be stunned at all** | The player holding a boss still for the whole fight |

**Both of the first two are needed.** A damage threshold alone still allows chain
stunning by large hits. An immunity window alone still allows constant
interruption by small ones. The owner said the same thing independently.

**The threshold reads damage actually dealt, not damage swung.** A hit that armor
and resistance reduced to a scratch is a scratch. That is what makes defensive
investment stop the interruption rather than only reduce the damage, and both
surveyed games do the same.

**A skill whose stated effect is to stun ignores the damage threshold.** Shield
Bash, Shockwave Leap, Lunge and Whip Swing all state that they stun, and a
threshold that made them fail against a healthy target would leave them doing
nothing they were written to do. Such a skill does not ignore boss immunity and
does not ignore the immunity window.

## Where the numbers come from

**The 10% threshold is the middle of what the genre ships**, and the three games
surveyed do not agree with each other:

| Game | Threshold to be able to stun |
|---|---|
| Last Epoch | More than 5% of maximum health |
| Path of Exile | About 10% of effective maximum life, because a computed stun chance at or below 20% is discarded |
| Path of Exile 2 | 15%, below which the chance is zero |

Taking the middle rather than the strictest is deliberate. This design also has a
5 second immunity window, which is longer than Last Epoch's 1 second and longer
than the 4 seconds Path of Exile gives its unique bosses. The window is doing most
of the anti-lock work, so the threshold does not also need to be the harshest.

**The 5 second window was stated by the owner**, not derived. It is longer than
every stun the game can currently apply — the longest is the Brute's Heart set
bonus at 3 seconds — which is what makes it a real gap between stuns rather than
a formality.

**Immunity for bosses is the simplest of four options and none of the surveyed
games uses it.** Path of Exile makes a unique boss immune only while stunned and
for 4 seconds after. Last Epoch counts a boss as having 50% more health for the
stun calculation. Diablo IV routes crowd control into a separate stagger meter
that must be filled before any of it applies.

## A consequence worth stating

**Four shipped player skills lose their stun against a boss.** Shield Bash,
Shockwave Leap, Lunge and Whip Swing still deal their damage and still move the
player, and their stun does nothing in a boss fight. That follows directly from
the owner's answer to point 3 and is not a defect, but it is a change in what
those four skills are worth in the fight they matter most in.

## A slow is not a stun

Cripple reduces an enemy's movement and attack speed by 30% and leaves it able to
act. The design document already said its reduction caps below total because a
full stop would be a stun by another name, and the same reasoning applies to
Weaken. The anti-stun-lock rule names stun only. Whether knockback and slow carry
the same threshold and window is open and is carried by #270.

## What was built

`sim/cataclysm_sim/damage.py`, the model of one hit's resolution, gained
`STUN_DAMAGE_THRESHOLD`, `STUN_IMMUNITY_SECONDS`, `can_be_stunned`,
`Defender.is_boss` and `Attacker.stun_is_designed`, and its `resolve` now gates
the stun roll on all of it.

**The immunity window is recorded but NOT enforced anywhere.** It is a rule about
time and `resolve` has no clock. Nothing in this repository implements it yet.
The constant exists so the design document, the model and whatever the game
eventually implements cannot disagree about the figure, and
`sim/tests/test_anti_stun_lock.py` says this plainly rather than leaving it to be
assumed.

## Evidence

```
1315 passed in 10.23s
ruff check . — All checks passed!
```

Sources:
[Stun — Last Epoch Support](https://support.lastepoch.com/hc/en-us/articles/46361891772443-Stun),
[Stun — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Stun),
[Crowd Control — Diablo 4 Wiki, PureDiablo](https://www.purediablo.com/diablo4/Crowd_Control),
[Diablo 4: Staggering Explained — Game Rant](https://gamerant.com/diablo-4-d4-staggering-explained/).

---

## 2026-08-05 — A maxed player losing about one Cataclysm dungeon run in five is intended

**Affects** `docs/Cataclysm_GDD_v2.md`. Applied. Issue #250. No number changed.

**STATED BY THE PROJECT OWNER, 2026-08-05:** "This is fine as it is. Go with
option A." Option A on the issue was to write the intent into the design document
rather than change any figure.

## What was measured

The Cataclysm boss dungeon is the final dungeon of a run and dying in it ends the
run — `Engine._resolve_dungeon` in `sim/cataclysm_sim/engine.py` sets `lost` and
returns. Its last-floor enemy, the Cataclysm Boss, **out-scores the maximum
player Power Score of its own difficulty tier at every tier**, so no amount of
gear reachable within a tier makes the fight safe.

Re-measured on 2026-08-05 against the player power anchors set that day. The
figures on the issue itself were computed against the previous anchors and are
stale.

| Tier | Player ceiling | Cataclysm Boss | Boss ÷ ceiling | Death chance at the ceiling |
|---|---|---|---|---|
| 1 | 385 | 773 | 2.01 | 16.7% |
| 2 | 883 | 1,413 | 1.60 | 18.0% |
| 3 | 1,508 | 2,189 | 1.45 | 18.6% |
| 4 | 2,225 | 3,035 | 1.36 | 19.6% |
| 5 | 3,078 | 4,044 | 1.31 | 19.6% |
| 6 | 4,057 | 5,173 | 1.28 | 19.7% |
| 7 | 5,120 | 6,361 | 1.24 | 20.4% |
| 8 | 6,327 | 7,729 | 1.22 | 20.2% |

Measured at 125 floors, the midpoint of the 100 to 150 the Cataclysm boss dungeon
spans, with no dungeon modifiers and no subtype.

**The band is 16.7% to 20.4%**, and it now climbs almost monotonically with the
tier. Before the anchors were reset earlier the same day it ran 15.9% to 21.5%
with tier 5 measurably a breather compared with the tiers either side of it. That
change was made for a different reason — tier width not climbing monotonically —
and this is a second, unlooked-for consequence of it worth recording.

## Why it is built this way, and why it is not a defect

Overwhelm has no hard gate: an enemy above the player's Power Score strips
mitigation in proportion to the gap rather than refusing entry. A Cataclysm Boss
the player could out-score would make the final fight of a maxed run a formality.
Leaving it above the ceiling means the last fight is decided by the odds, and the
empire layer is what the player spends between runs to change those odds.

The consistency across all eight tiers is itself evidence the Overwhelm model is
doing what its own documentation says. Overwhelm is rated against tier width
precisely so that the same relative shortfall costs the same at every tier, and a
figure that lands within four percentage points across eight tiers is that claim
holding.

## The two alternatives, and why neither was taken

**Lower the Cataclysm Boss score so a maxed player can out-score it.** The death
chance at the ceiling would fall to zero and the fight would stop mattering.

**Raise the player power ceiling, or lower `boss_risk_multiplier` (6.0) or
`per_floor_risk` (0.010) in `sim/cataclysm_sim/config.py`.** Tunes the risk
without changing the shape, and was not needed: nothing said the current figure
was wrong, only that nothing said it was right.

## What was missing, and what now fills it

Nothing stated the intent, so there was no way to tell whether 20% was the
target, twice the target, or half of it. `docs/Cataclysm_GDD_v2.md` now has a
section, "The Final Fight Is Never Safe, and That Is Deliberate", in the Overwhelm
chapter. It states the band, that it is a run rather than an attempt that ends,
how far the boss out-scores the ceiling at each end of the tier range, the
measurement conditions, and which test fails if the figure moves.

**Every figure in that section is re-derived by a test rather than restated.**
Six tests in `sim/tests/test_power_threshold.py` parse the numbers out of the
document and compare them against the model, so the prose and the model cannot
drift apart. That failure mode is not hypothetical: issue #6 was three analysis
scripts whose printed conclusions went stale when the anchors changed, and issue
#253 was a second copy of those anchors that nothing was checking.

## Evidence

```
1287 passed in 10.15s
ruff check . — All checks passed!
```

---

## 2026-08-05 — An attribute is a whole number of points, rounded to the nearest, in the maths and not only on the screen

**Affects** `docs/Cataclysm_GDD_v2.md`, `sim/cataclysm_sim/character.py` and
`game/Source/Cataclysm/AbilitySystem/CataclysmPrimaryAttributeSet.cpp`. Applied.
Issue #225.

**STATED BY THE PROJECT OWNER, 2026-08-05, verbatim:**

> ARPG players are obsessed with math and calculations. If you show them a
> number that isn't true, they'll keep digging and complaining. Round to the
> nearest whole number.

## The question

Each of the eight primary attributes has exactly one affix and it is a
percentage increase, decided when gear was allowed to grant attributes on
2026-08-04. A percentage of a whole number is usually not one. A character with
33 Spirit wearing a top-tier +12% Spirit affix reaches **36.96**, and nothing
said whether that was 36, 37 or 36.96.

## The answer, and the part of it that matters most

Round to the nearest whole number. A half rounds up: 36.5 becomes 37.

**The rounding is applied to the value, not to how the value is printed.** That
is the whole content of the owner's reasoning. The arrangement to avoid is a
character screen reading 37 Spirit while the calculations keep 36.96, because a
player then works out what 37 Spirit should give, is handed something else, and
reports it as a bug. There is one value, it is whole, and every reader gets the
same one.

The issue's own written recommendation was the opposite — keep the fraction in
the maths and round only for display — and it was **overruled**. Recorded so the
recommendation is not re-proposed as though it had never been considered.

## Two alternatives, and why each was rejected

**Flooring.** Never grants more than was earned, and it was rejected because it
takes +12% of 4 Spirit from 4.48 back to 4, so an affix on a lightly invested
attribute is worth **exactly nothing**. The affix is a percentage so that it is
*weak* when spread thin, which rewards a decision the player already made. Being
worth zero below a threshold is a different thing and reads as broken rather
than as a trade-off.

**Keeping the fraction.** Loses nothing numerically, and it is the one
arrangement in which the screen and the maths can disagree.

## Round half up, not round half to even

Python's built-in `round` rounds a half to the nearest even number, so it gives
4 for 4.5 and 36 for 36.5. A player reads "nearest whole number" as 4.5 becoming
5. Halves are reachable — 10% of 5 points is 5.5 — so the rule is spelled out
rather than delegated to a language default.
`sim/tests/test_attribute_rounding.py::test_the_built_in_round_really_would_disagree`
holds the three cases where the two differ, so the reason for a separate
function cannot quietly stop being true.

## What had to be built before the rule had anywhere to live

**The simulation could not represent an attribute affix at all.**
`sim/cataclysm_sim/character.py` validated `Gear.increased` against the
character sheet's stats, and the eight attributes are not stats, so
`Gear(increased={"agility": 0.12})` raised. The eight affixes added by pull
request #224 existed in the affix pool and reached no character. Three things
changed:

- `Gear.increased` now admits an attribute name as well as a stat name.
  `Gear.flat` and `Gear.weapon_base` still do not: there is no flat attribute
  affix, and a weapon supplies a base only for the stats it owns.
- `Character.attribute(name)` returns the allocated points raised by any gear
  increase and rounded, and `Character.attribute_line()` returns all eight.
- `Character.increases(stat)` reads each attribute through `Character.attribute`
  instead of off the raw allocation. Reading the allocation is what made an
  attribute affix grant nothing.

`Attributes.increases_for` still reads the allocation alone and is kept for
asking what levelling by itself bought. Its docstring says so.

**In the game**, `UCataclysmPrimaryAttributeSet::PreAttributeChange` already
clamped an attribute at zero and now rounds as well, through a named
`RoundedPoints` function so a test can check the rule without building an
ability system component.

## Evidence

```
1281 passed in 10.11s
ruff check . — All checks passed!
```

Worked example, measured rather than asserted: a character with 33 Vitality and
a +12% Vitality affix has exactly the maximum health of a character with 37
Vitality and no affix, which is the mismatch this rule exists to prevent.

---

## 2026-08-05 — Damage over time has three levers, each worth 52% at top tier, and they were priced together

**Affects** `docs/Cataclysm_GDD_v2.md`, `docs/All_Things_Cataclysm.xlsx`, the
generated tables in `game/Data/`, `sim/cataclysm_sim/character.py`,
`sim/cataclysm_sim/affixes.py` and
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.h`. Applied.
Issues #205 and #258.

## What was missing

Ten affixes applied an ailment. **One** affix touched damage over time at all,
and it changed only how often the effect ticked. Nothing anywhere made a damage
over time effect hit harder or run longer, and the stats to hold those numbers
did not exist either — `CataclysmCombatAttributeSet.h` had `DotFrequency` and
nothing else. A player could build entirely into applying ailments and then have
no way to make any of them do more.

The character sheet now carries three damage over time stats where it carried
one, so it has 45 stats rather than 43:

| Stat | What it raises | Baseline |
|---|---|---|
| Damage over Time | How much one tick deals | 100% |
| Damage over Time Frequency | How many ticks happen per second | 100% |
| Damage over Time Duration | How long the effect runs | 100% |

Each has one affix, a suffix rolling on gloves, necklaces, relics, rings and
weapons, worth **52% at T7**.

## Why the three had to be priced in one change

They multiply each other, which the design document already stated: a character
with 48% on each deals 324% of base, not 148%. So setting any one of them against
an existing affix sets the wrong number. Three affixes at Increased Damage's 125%
would multiply damage over time by 8.5 × 8.5 × 8.5 instead of 8.5.

That is why issue #258 was labelled `blocked` on #205 rather than being a
one-line edit, and it is why both are closed by the same change.

## Where 52% comes from

Solved, not chosen. Six affix slots spent on Increased Damage multiply a
direct-hit build's damage by 8.5, and six slots is the build every other damage
number in `sim/cataclysm_sim/affixes.py` is fitted against —
`REFERENCE_INCREASED_DAMAGE_AFFIXES`. Two slots on each of the three levers has
to reach the same 8.5:

    (1 + 2v)³ = 1 + 6 × 1.25 = 8.5     →     v = 52.04%

Rounded down to 52%, which leaves a damage over time build at ×8.49 against ×8.50
and so errs on the low side, which is the safer direction for levers that
compound. `dot_lever_top_value()` computes the solve and
`sim/tests/test_dot_levers.py` checks the shipped constant against it, so the
value follows Increased Damage if that ever moves.

## What the one affix that already existed was wrong about

"Increased damage over time frequency" was **12%**, set to match increased armour
and increased maximum health. That was priced under the assumption that ticking
faster changed only when damage arrived. The project owner answered on 2026-08-04
that a damage over time effect deals a **fixed amount per tick**, which makes tick
rate a damage multiplier, so 12% was about a tenth of what it should be. It is now
52% with the other two.

## The Efficacy attribute was deliberately left alone, and this is the measurement

Issue #258 listed `game/Data/Attributes.csv` as carrying a second wrong value:
Efficacy grants 1% increased damage over time frequency per point, set under the
same assumption. **It was measured rather than scaled, and it does not move.**

- Scaling it by the same factor the affix moved, 52 ÷ 12, would put it at 4.33%
  per point. At 100 points that multiplies damage over time output by **5.33**.
- At the 1% it already has, 100 points multiply damage over time output by **2.0**.
- 100 points of Ferocity — the attribute a direct-hit build buys, through critical
  strike chance and multiplier together — multiply an expected hit by **1.56**.

So 1% per point is already worth more to a damage over time build than Ferocity is
to a direct-hit one, and 4.33% would be about three and a half times it.

**Efficacy also drives exactly one of the three levers and must keep driving only
one.** One attribute point buying three multiplying increases would make Efficacy
strictly the best attribute for any damage over time build, and no other attribute
compounds within itself that way.
`test_efficacy_drives_one_lever_and_only_one` checks that no attribute drives more
than one of the three.

## One set of levers, not one set per ailment

Six of the ten ailment affixes apply a damage over time effect — bleed, poison,
disease, void splinter, necrosis and burn — and every Demonic skill applies burn
outright as well. Three levers each would be eighteen affixes serving one build
archetype, against the eight the whole damage-against-a-target's-type family
costs. It also matches the stat that already existed: there has only ever been one
damage over time frequency, shared by everything.

## What this does not settle, and it is a real risk

**The equal-value pricing holds at six affix slots and nowhere else, and it cannot
hold anywhere else.** An additive bracket and a product of three brackets cross
exactly once. Below six slots a damage over time build is behind a direct-hit
build spending the same; above it, ahead.

| Offensive affix slots | Direct-hit | Damage over time | Ratio |
|---|---|---|---|
| 3 | ×4.75 | ×3.51 | 0.74 |
| 6 | ×8.50 | ×8.50 | 1.00 |
| 12 | ×16.00 | ×29.27 | 1.83 |
| 18 | ×23.50 | ×70.06 | 2.98 |

There are 48 offensive affix slots on a full set of gear, so eighteen is not
theoretical. The compounding itself is deliberate and the design document says so.
The **size** of the gap at heavy investment has not been played and is filed as
issue #264 with four alternative ways to close it.

## Evidence

```
1254 passed in 10.18s
ruff check . — All checks passed!
```

Five breaks were fed to the new guards and every one was caught: one lever priced
differently from the other two, the top value drifting back to 12%, a lever
baselining at zero, Efficacy driving two levers, and a lever dropped off the sheet
entirely.

Sources for how the genre handles it, carried over from the 2026-08-05 entry on
tick rate:
[Damage over time — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Damage_over_time),
[Damage Over Time — Official Last Epoch Wiki](https://lastepoch.fandom.com/wiki/Damage_Over_Time),
[Ailments Explained — Last Epoch, Maxroll](https://maxroll.gg/last-epoch/resources/ailments-explained).

---

## 2026-08-05 — The player power anchors for tiers 2 to 7 were reset so tier width climbs at every tier

**Affects** `src/utils/calculateScores.tsx` in the separate
`sdubois777/DungeonSimulator` repository, and every copy of the anchors in this
one. Applied. Issue #7.

**The anchors are now 385, 883, 1508, 2225, 3078, 4057, 5120, 6327.** They were
385, 871, 1457, 2144, 3251, 4166, 5209, 6327.

**STATED BY THE PROJECT OWNER, 2026-08-05:** take the candidate derived from the
player Power Score model, and make sure the change reaches everywhere player and
dungeon scores are calculated.

## What was wrong

Tier width is the previous tier's maximum subtracted from this tier's, and it
multiplies **every** weighted term in the Enemy Score formula: dungeon type,
subtype and enemy rarity. The widths did not climb monotonically. Tier 5 was
1,107 wide where the surrounding trend was about 790, and tier 6 was **narrower**
than tier 5 at 915.

Two consequences, measured on 2026-08-05 and not previously recorded:

| tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Boss minus Common on the same floor, before | 116 | 146 | 176 | 206 | **332** | **274** | 313 | 336 |
| Chance a player at their own tier's ceiling dies in a Cataclysm dungeon, before | 16.7% | 18.4% | 19.5% | 20.3% | **15.9%** | 20.8% | 20.8% | 21.5% |

A tier 6 Boss stood out less against its own trash than a tier 5 Boss did, and
tier 5 was measurably a breather compared with the tiers either side of it.

## Why these values, and the correction to what was first proposed

The new anchors come from `sim/cataclysm_sim/player_power.py`, the model that
scores a reference character whose level, gear rarity, gear upgrade level, gem
count and resistances all advance smoothly with the tier. Its curve is a
quadratic pinned through the tier 1 and tier 8 anchors and nothing else, so
tiers 2 to 7 are predictions rather than fits.

**The proposal first posted on issue #7 moved all seven of tiers 1 to 7, and that
was wrong.** Moving tier 1 re-pins the quadratic, which changes the derived
weights, which changes every prediction. Installing that proposal and re-running
the model was measured: it then predicted 383, 882, 1506, 2223, 3076, 4056, 5119
against the anchors just installed, disagreeing with itself at seven of the eight
tiers.

**Leaving tiers 1 and 8 alone avoids that entirely.** The curve does not move, so
the predictions do not move, and the anchors can simply be set to them. Measured
after installing: tiers 2 through 8 are exact and tier 1 is one point off, 384
against 385. That single residual is the reference character's level and filled
socket count being whole numbers where the continuous curve asks for 12.5 and
5.625.

It also moves six anchors instead of seven.

## What the widths are now

| tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| width | 385 | 498 | 625 | 717 | 853 | 979 | 1063 | 1207 |
| step over the tier below | | 1.29x | 1.25x | 1.15x | 1.19x | 1.15x | 1.09x | 1.14x |

Every tier is wider than the one below it, and the step ranges from 1.09 to 1.29
times against the previous 0.83 to 1.61.

## Every place the change had to reach

| File | What it holds |
|---|---|
| `src/utils/calculateScores.tsx` in `sdubois777/DungeonSimulator` | The authoritative table. Commit 6c9be8b. |
| `sim/cataclysm_sim/scoring.py` | The verified Python copy |
| `sim/cataclysm_sim/combat.py` | A docstring quoting what a maxed tier 8 player would take under a rejected alternative rule; 54% became 56% |
| `sim/analyse_scoring.py` | A hand transcription of the design document's ranges table |
| `sim/tests/test_player_power.py` | A test that asserted the old residual signature, rewritten to assert the residual is gone, plus a new test that widths climb |
| `sim/tests/test_enemy_stats.py` | Two pinned Overwhelm figures |
| `docs/Cataclysm_GDD_v2.md` | Two anchor tables, the Overwhelm sentence in section IV, and two paragraphs describing the anomaly |
| `docs/DECISIONS.md` | The Overwhelm comparison table in the 2026-08-03 entry |
| `game/Source/Cataclysm/Player/CataclysmPowerScore.cpp` | The C++ anchor table |
| `game/Source/Cataclysm/Tests/CataclysmPowerScoreTests.cpp` | The C++ mirror of the residual test, rewritten the same way |

Nothing else in the repository holds a copy: a search of every Python, Markdown,
C++ and header file for the six retired values returns nothing.

**The Overwhelm figures moved and three documents quoted them.** A player at the
tier 8 ceiling now loses 8.4% of their mitigation to a Common enemy, 12.2% to a
Herald and 20.9% to a Cataclysm Boss, where the figures were 8.9%, 12.6% and
21.4%. Those three numbers are the stated argument in `docs/DECISIONS.md` for why
enemies carry no per-rarity Penetration stat. The argument is unchanged: Overwhelm
still exceeds the retired per-rarity figure at Common and still sits below it at
Herald and Cataclysm Boss.

## What was not done

`sim/experiments.py`, the tuning sweep, has **not** been re-run. It is about
25,000 simulated campaigns and roughly eighteen minutes, and there is no saved
baseline from before this change to compare against, so a single run would
produce numbers with nothing to measure them against. Any tuning conclusion drawn
from a sweep run before 2026-08-05 was computed on the old curve and should be
treated as unverified, in the same way issue #6 treated the analysis scripts.
---

## 2026-08-05 — The empire tree is the node graph; the keystones document is commentary on it

**Affects** `docs/README.md`, `docs/Empire_Skill_Tree_Keystones.md`. Applied.
Issue #25.

**`docs/Empire_Development_Tree_Final.json` is the empire passive tree.**
`docs/Empire_Skill_Tree_Keystones.md` is prose describing the same tree and is
authoritative for nothing the graph also states. Nothing had said which, and two
open balance issues, #4 and #5, are arguments about branches of a tree whose
contents were ambiguous.

**Decided by measuring rather than by preference.** The graph is newer, by its own
`metadata.updatedAt`: 2026-03-05 against the prose's 2026-02-10. It is also
larger. 105 of the prose's 108 bullets name a node in the graph, and the graph has
68 names the prose never mentions. So the prose is an earlier draft of the same
tree rather than a rival description of it. Only names were compared; the two word
the same effect differently in many places and reconciling the wording is not what
the issue asked for.

**Three prose entries have no counterpart in the graph** and nobody recorded
whether they were cut or lost: Bounties, Weightless Spoils, and the Architect
quadrant's four decision nodes. The graph has a node named Bounty granting
something unrelated, mentions inventory nowhere, and has five decision nodes none
of which is the fourth. Listed in the prose file's own header and in issue #260.

**The quadrant is called Treasury.** The prose used three names within forty
lines: Treasurer in its branch list, Tyrant in its capstone list and Treasury in
its section heading. The graph uses Treasury nine times, the other two never, and
its on-canvas label reads TREASURY. The prose now matches. The **Treasurer** city
upgrade in `game/Data/CityUpgrades.csv` is a different thing and keeps its name.

**The prose file claimed Google Drive was its source of truth**, which had been
false since 2026-08-02, when this repository's copies became authoritative. That
header is replaced.

`tools/tests/test_empire_tree_documents_agree.py` holds the comparison.
---

## 2026-08-05 — A damage over time effect deals a fixed amount per tick, not a total spread across a duration

**Affects** the "Applying Damage Over Time and Other Effects" section of
`docs/Cataclysm_GDD_v2.md`, the main design document. Applied. Issue #220.

**STATED BY THE PROJECT OWNER, 2026-08-04.** A damage over time effect deals a
fixed amount per tick. The worked example given: 20 damage per tick, ticking once
per second, lasting 5 seconds. Increasing tick rate makes it tick faster than once
per second, and the total goes up.

**Three independently scalable metrics: damage per tick, tick rate, duration.**
Raising any one of them raises the total damage the effect deals.

**This is the opposite of what the genre does, and it was chosen knowing that.**
Both games surveyed define a damage over time effect as a rate and derive the
per-tick amount from it, so ticking is delivery rather than damage:

- **Last Epoch** states it directly: an effect applies its total damage over a set
  length of time divided into ticks per second, and each tick deals the total
  divided by the time. Raising duration raises total damage and leaves damage per
  second alone.
- **Path of Exile** goes further and has an explicit family of modifiers for this
  — "Ignited Enemies Burn faster" and its relatives raise damage per second and
  shorten the duration by the same factor, so the total is unchanged. It is a
  delivery lever by construction.

Neither has a stat that adds damage by adding ticks. The research recommended
copying Path of Exile and was overruled, which is a legitimate outcome: the shape
a shipped game uses is evidence, not a rule, and damage over time builds in this
genre commonly do scale damage and duration separately.

**The arithmetic consequence, recorded once so it is a choice rather than a
surprise.** All three levers multiply the same output. Against the owner's
example:

| | Damage per tick | Ticks per second | Duration | Total |
|---|---|---|---|---|
| Base | 20 | 1 | 5s | 100 |
| +48% tick rate only | 20 | 1.48 | 5s | 148 |
| +48% duration only | 20 | 1 | 7.4s | 148 |
| +48% damage per tick only | 29.6 | 1 | 5s | 148 |
| **+48% on all three** | 29.6 | 1.48 | 7.4s | **324** |

A direct-hit build with +48% increased damage ends at 1.48 times base, because
increases add inside one bracket of `(base + flat) × (1 + increases) × more1 ×
more2`. A damage over time build with the same +48% on each of its three levers
ends at 3.24 times.

This document's "What Affixes Do Not Grant" section says no ordinary affix is a
"more" multiplier, and that multiplicative sources come from gems, passive tree
keystones and enchantments. Three separate affixes multiplying the same output is
not literally a "more" affix, but it produces the same curve. **That is why the
three levers have to be priced together and not one at a time.**

**Two of the three levers do not exist yet.** Only tick rate does, as
`DotFrequency` in
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.h`, which no code
reads. There is no affix or attribute for flat damage over time damage and none
for duration. Both are recorded in issue #205.

**The one shipped value is now known to be wrong and is deliberately left alone.**
"Increased damage over time frequency" is 12% at top tier in
`game/Data/Affixes.csv`, set to match increased armour and increased maximum
health on the assumption that ticking faster only changed when damage arrived.
The affix that is straightforwardly a damage multiplier, "Increased damage", is
125%. Re-pricing it now would mean setting one of three multiplying levers in
isolation, so it is filed as issue #258, blocked on #205, and
`INCREASED_DOT_FREQUENCY` in `sim/cataclysm_sim/affixes.py` carries a comment
saying the value is wrong and why rather than a guess that reads as considered.

Sources:
[Damage over time — Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Damage_over_time),
[Damage Over Time — Official Last Epoch Wiki](https://lastepoch.fandom.com/wiki/Damage_Over_Time),
[Ailments Explained — Last Epoch, Maxroll](https://maxroll.gg/last-epoch/resources/ailments-explained).

---

## 2026-08-05 — Difficulty is two independent choices, and no mode grants extra loot

**Affects** the "Difficulty Options" section and the risk table of
`docs/Cataclysm_GDD_v2.md`, the main design document. Applied. Issue #32.

**Solo Self-Found is a flag, not a mode.** It was listed alongside Standard,
Hardcore and Heretic as if choosing it meant not choosing Hardcore. It changes
where items come from and does not change how easily a player dies, so it belongs
on its own axis. A character now picks one lethality mode — Standard, Hardcore or
Heretic — and separately may set Solo Self-Found. Hardcore Solo Self-Found and
Heretic Solo Self-Found are both real.

Settled by research, not judgement. Last Epoch lets one character carry Hardcore
and Solo Challenge at the same time; its wiki states that one or more challenges
can be selected at once and names Hardcore Solo Self Found as an example. Path of
Exile ships Hardcore SSF as a league in its own right.

**No difficulty mode grants increased loot.** Three of the four entries said
"Increased loot drops" with no number, and the number was never going to arrive,
because there is nothing in the genre to copy it from.

- Path of Exile's Solo Self-Found league has drop rates identical to the trade
  league. There is no bonus of any kind, and players complain about exactly that.
- Diablo IV attaches drop rate to the World Tier, which is the difficulty of the
  content, and gives the Hardcore flag no drop bonus at all. Its Hardcore rewards
  are titles and cosmetics.

That is the same split this game already has: **drop rate belongs to the
difficulty tier**, which scales content, and not to a flag describing what happens
to the player on death.

**This is a judgement and it is cheap to reverse.** The alternative is a
compensating bonus for Solo Self-Found, on the argument that a self-found player
here has no auction house at all and so has strictly fewer items available. It
was not taken because Path of Exile's players are in the same position and get
nothing, and because inventing a constant with nothing to measure it against is
what left the document saying "increased" for a year. Reversing it needs only a
figure, expressed as increased loot quantity so it lands in the same currency as
the Luck attribute and the Increased Loot Quantity affix.

**Hardcore drops each equipped piece with a 10% chance.** The document already
described the mechanic — a per-piece chance on death — and only the number was
missing. 10% is Tibia's figure for an unblessed character, and Tibia is the game
that shipped this exact mechanic. Over the 18 pieces a character wears, that is
1.8 pieces on an average death.

**Heretic doubles the rate to 20% and keeps its floor of 2 pieces, which is 3.7
on average.** A rate is needed here, not just a floor, and this is the judgement
in it. At Hardcore's own 10% rate a floor of two produces 2.4 pieces on average
against Hardcore's 1.8, so the two modes would have felt the same on death while
reading as a ladder. Doubling the rate makes Heretic cost about twice what
Hardcore costs, and the floor then binds on about one death in ten rather than on
nearly half of them.

The piece count is 18 — seven armour pieces, eight rings, a necklace, a relic and
a weapon — read from `GEAR_PIECES` in `sim/cataclysm_sim/affixes.py`. It is what
turns a per-piece chance into an amount, so `tools/tests/test_difficulty_modes.py`
reads it from the model rather than from the document, and recomputes both stated
averages from the stated rates.

**Casual stays removed.** It was referenced in the risk table and defined nowhere.
That was settled earlier on this issue; the risk table now names the three
lethality modes and does not name Solo Self-Found, which does not change urgency.

**Still open, filed separately:** nothing says whether a player may change their
lethality mode or their Solo Self-Found flag after character creation, or what
happens to a Hardcore character's flag when it dies. Last Epoch converts a dead
Hardcore character to softcore; this game's death costs days rather than ending
the character, so that answer does not carry over.

Sources:
[Path of Exile Solo Self-Found wiki](https://pathofexile.fandom.com/wiki/Solo_Self-Found),
[Last Epoch character creation guide](https://lastepoch.fandom.com/wiki/Guides:Character_Creation),
[Diablo IV Hardcore mode rewards](https://primagames.com/gaming/what-are-the-diablo-4-hardcore-mode-rewards),
[Tibia death and blessings](https://tibia.fandom.com/wiki/Blessings).

---

## 2026-08-05 — Why Overwhelm is rated against tier width, re-measured; and analysis scripts compute their conclusions

**Affects** `sim/cataclysm_sim/combat.py`, `sim/cataclysm_sim/scoring.py` and the
three `sim/analyse_*.py` scripts. Applied. No design document changes.

**The decision to rate Overwhelm against tier width still stands, and the numbers
that were given as its reason were wrong.** Issue #6. The argument is in the
opening docstring of `sim/cataclysm_sim/combat.py`, the module that turns a power
shortfall into lost mitigation. It said a flat 50-point step is worth 17% of a
tier 1 tier width but only 6% of a tier 8 one, which made a maxed tier 1 player
eat 13% penetration at their own final boss while a maxed tier 8 player ate 47%.

Re-measured against the player power anchors issue #2 installed:

| Figure | As written | Measured now |
|---|---|---|
| A flat 50-point step, as a share of the tier 1 width | 17% | 13% |
| The same step, as a share of the tier 8 width | 6% | 4% |
| Penetration a maxed tier 1 player takes at their final boss | 13% | 16% |
| Penetration a maxed tier 8 player takes at their final boss | 47% | 54% |

Every number moved and the conclusion did not: a flat point step still punishes
the top tier several times harder than the bottom one for the same situation, and
the ratio between the two is unchanged at about 3.5. So the mechanic is right for
the reason given, but nobody had checked that since the anchors moved.

**`sim/analyse_penetration.py` is kept rather than deleted, and now says at the
top that it analyses a proposal the project rejected.** That script is where the
four figures came from. The rule it examines — enemies above your Power Score
gain resistance penetration — does not exist: the entry dated 2026-08-03 in this
log records that enemies carry no Penetration stat because Overwhelm already does
that job, shrinks as the player out-powers the content, and strips armour, block
and evasion rather than resistance alone.

Deleting it was the alternative. Keeping it wins because it is the measurement
the decision was made on, and a decision whose evidence has been thrown away
cannot be re-checked. It gains a section that runs the same comparison against
Overwhelm as it shipped, so a reader is not left with two rules neither of which
the game uses. **This is a judgement, not a reading.**

**Working rule, from here on: a conclusion printed under a table in an analysis
script is computed, not typed.** All three scripts had typed their conclusions,
so when the anchors moved each one printed a table and a sentence directly
underneath it that disagreed with the table. Nothing raised, because a wrong
sentence inside a `print` is not an error.

What changed as a result of re-running them, beyond the four figures above:

- `sim/analyse_scoring.py` said 7.5 times the depth buys 22% more difficulty. Its
  own table samples 8 to 150 floors, which is 18.8 times the depth, and buys 18%.
- `sim/analyse_dungeons.py` said dungeon modifiers are about 4% of a tier width at
  tier 1. The table above the sentence said 2.9%.
- `sim/analyse_penetration.py` said routine content "barely triggers" the
  proposed rule. A player at 70% of their tier now out-powers a mid-floor Basic
  dungeon at every one of the eight tiers, so it does not trigger at all.
- `sim/cataclysm_sim/scoring.py`'s own docstring quoted four tier 1 Dungeon Scores
  by depth. All four were the pre-issue-#2 values.

`sim/tests/test_analysis_scripts.py` runs each script, recomputes each conclusion
from the model, and requires the recomputed string to appear in what the script
printed. It also keeps the retired figures out by the exact phrase each appeared
in. Each of its six guards was proved to fail by re-typing the figure it guards.

**Four bare numbers in `sim/cataclysm_sim/scoring.py`'s formula are now named
constants** — `BASELINE_WEIGHT`, `PROCEDURAL_DIVISOR`, `PROCEDURAL_PER_FLOOR` and
`DEPTH_TENSION_PER_TIER`. No value changed. They are named so the analysis scripts
can refer to the formula instead of retyping it, which is how two of them came to
disagree with it. `CLAUDE.md` forbids hand-editing that file's constants because
it is a port; naming a literal is not editing it, and `sim/verify_scoring_port.py`
was run afterwards and reproduced the reference across 96,768 values.

---

## 2026-08-05 — A drop rolls one affix tier above the difficulty tier, and crafting has no tier gate at all

**This reverses part of the entry below dated the same day**, "The difficulty
tier caps which affix tier an item can reach, by drop or by crafting". That entry
labelled two of its parts as judgements rather than readings and said the
drop-only question was the one most open to reversal. The project owner answered
on issue #241 and reversed a different part.

**STATED BY THE PROJECT OWNER, 2026-08-05, verbatim in substance:**

- A player may wear equipment with an affix tier higher than their current
  difficulty tier. There is no restriction on equipping.
- Equipment drops with affixes **equal to or lower than the current difficulty
  tier plus one**.
- Crafting may raise an affix **as high as the player can afford**, at any
  difficulty tier. With limited investment in the empire tree's crafting
  branches it will simply be too expensive in resources, time and gold.

**THE RULE.** A drop rolls uniformly from T1 up to `min(7, difficulty tier + 1)`.
Crafting has no tier gate. Equipping has no tier gate.

**What the plus one buys, and why it is the better answer.** With the cap sitting
exactly on the difficulty tier, every affix a player found was one the forge
could already produce, so a dungeon was worth running for quantity and never for
quality. One tier above means the best thing a dungeon can drop is something
crafting has not yet reached. That is a reason to run a dungeon that survives
however much crafting investment a player has made, and it is the same job Last
Epoch gives its two drop-only tiers without making any tier uncraftable.

**Why capping crafting was wrong.** The earlier entry argued that capping only
the drop would leave the gate doing nothing, because a tier 1 player would craft
to T7 rather than find one. That reasoning treated cost as free. It is not: the
Potency Crystal raises one tier at a time,
`game/Data/CraftingMaterials.csv` prices the deterministic affix craft at one day
per tier of affix, and a day at the forge is a day not defending the empire. The
empire tree's crafting investment is what moves that from impossible to merely
expensive, which is a decision a player makes rather than a rule the game
enforces.

**Where the eight-against-seven mismatch now falls.** The drop cap reaches T7 at
difficulty tier 6 and stays there for tiers 6, 7 and 8. That is where it costs
least, because gear rarity, gear upgrade level and filled sockets are all still
rising through those tiers.

**What is unchanged from the earlier entry.** The gate is still the difficulty
tier, which is the design's own gate three times over. Every tier at or below the
cap still stays in the pool, so a drop is better on average without being
predictable; Path of Exile and Last Epoch both gate that way and neither removes
the low tiers. No affix tier is drop-only.

**Affects.** `docs/Cataclysm_GDD_v2.md`, the section "What Tier an Affix Can Roll
At" in section VI. `docs/All_Things_Cataclysm.xlsx`, the Crafting sheet, where
the Potency Crystal's function no longer claims a difficulty tier gate, and the
generated `game/Data/CraftingMaterials.csv` and
`game/Content/Data/DT_CraftingMaterials.uasset` with it. **Applied.**

**In the model.** `sim/cataclysm_sim/affixes.py` replaces the single
`max_affix_tier(tier)` with three: `max_affix_tier_on_a_drop(tier)`,
`max_affix_tier_by_crafting(tier)`, and `max_affix_tier(tier)` kept as the best
of both for callers that do not care which route. `roll_affix_tier` draws against
the drop cap. The import-time check now tests the drop cap, and its lower bound
changed from "equals T1" to "at least T1", because a cap of T2 at difficulty
tier 1 still drops T1 items.

---

## 2026-08-05 — The stat that makes better loot drop is called Magic Find, and only that

**The question.** Issue #244. One stat had three names. It is `magic_find` on
the character sheet in `sim/cataclysm_sim/character.py`, the column
`luck_magic_find` in `game/Data/Attributes.csv`, the affix `Flat magic find` in
`game/Data/Affixes.csv` and the gameplay attribute `MagicFind` in
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.h`. It was also
written as **"rarity find"** in three shipped tables and as **"loot rarity"** in a
fourth. A player reading the gem Of The Goblin, which said "Increases Rarity Find
by .5%", had no way to know that is the same number the affix calls magic find and
stacks with it.

**THE DECISION. Magic Find, everywhere.**

**Why that one and not Rarity Find.** Not because it is the better phrase. It is
already the name the value lives under in every place the number is actually
stored, so keeping it is a change to four prose descriptions, while switching to
Rarity Find would rename the stat on the character sheet, the affix, the column in
the attribute table, the gameplay attribute in C++, the gameplay tags and every
test that names any of them.

**The case for Rarity Find, recorded rather than lost.** This game names its eight
gear qualities Everyday, Quality, Superb, Masterful, Legendary, Mythical,
Ascendant and Cataclysmic. None of them is called "magic". So "magic find" is a
term inherited from another game, naming a rarity this one does not have, while
"rarity find" says plainly what the stat does. If the project owner prefers Rarity
Find, the rename is larger but not difficult, and it is much better done before
issue #44 builds loot generation on top of the current name. Issue #244 has the
full list of what it would touch.

**What changed.** Four cells in `docs/All_Things_Cataclysm.xlsx`, the design
workbook, and the generated tables and DataTable assets below them:

| Sheet | Row | Was | Now |
|---|---|---|---|
| Gems | Of The Goblin | Increases Rarity Find by .5% | Increases Magic Find by .5% |
| City Upgrades | Treasurer | 10% increased rarity find | 10% increased magic find |
| Dungeon Modifiers | Infernal Beacons | stacking loot rarity find buff | stacking magic find buff |
| Dungeon Modifiers | Reality Twister | increased loot rarity | increased magic find |

Plus the Luck row of the attribute table in `docs/Cataclysm_GDD_v2.md`, whose stat
column said Magic Find while its effect column said rarity find.

**The word "rarity" on its own is untouched and stays.** Gear rarity is the
eight-tier ladder above; enemy rarity is the Common to Cataclysm Boss ladder in
section X. Both are real and neither is this stat. The dungeon modifier Volatile
Evolution, "higher rarity mobs", and the enemy modifier Parasite Host, "upgrade
them to the next rarity", are both left exactly as they were.

**NOT DONE HERE, and deliberately: the empire tree documents.**
`docs/Empire_Skill_Tree_Keystones.md` uses "Loot Rarity" in five places and
`docs/Empire_Development_Tree_Final.json` in eight. They are left alone for two
reasons. They already disagree with each other, and issue #25 is open to reconcile
them, so changing one of them now would interfere with that work. And the JSON is
authored by a separate tool outside this repository, so a hand edit risks being
overwritten. Filed as its own issue.

> **Done on 2026-08-05 by issue #247**, which renamed all thirteen. The first
> reason above held and cost nothing: the rename touches node descriptions, not
> node identity or structure, so issue #25's reconciliation is unaffected and the
> two files now agree on this word instead of disagreeing in a new way. **The
> second reason was wrong.** The passive tree editor at
> `C:\PassiveTreeCreator` holds no tree data — `src/utils/serialization.ts`
> reads a JSON file the user opens and downloads one back, so
> `Empire_Development_Tree_Final.json` is the data rather than an export of it.
> Anyone opening it in the editor carries the edit through rather than
> overwriting it. `tools/tests/test_magic_find_has_one_name.py` now covers both
> files and no longer excludes anything.

**One consequence worth knowing.** The row key in `game/Data/CityUpgrades.csv` is
derived from the description text, so renaming the description renamed the row from
`Treasurer_Dungeons_here_have_10_increased_rarity` to
`Treasurer_Dungeons_here_have_10_increased_magic_f`. Nothing referenced the old
key; that was checked before the change.

**Affects.** `docs/Cataclysm_GDD_v2.md`, the Luck row and a new paragraph in the
Character Stats section stating the stat has one name. `docs/All_Things_Cataclysm.xlsx`
and the generated `game/Data/Gems.csv`, `game/Data/CityUpgrades.csv`,
`game/Data/DungeonModifiers.csv` and their three DataTable assets. **Applied.**

**In the tests.** `tools/tests/test_magic_find_has_one_name.py` refuses the two
retired phrases across every generated table found by glob and across the design
document, checks the three rows that carried them still describe what they do, and
checks the stat is still called magic find in all five places the value lives. It
deliberately does not search for the bare word "rarity".

---

## 2026-08-05 — The difficulty tier caps which affix tier an item can reach, by drop or by crafting

**The question.** Issue #129. Affixes have seven tiers and T7 is worth seven
times T1, and nothing said which of them a drop could reach. Without a gate a
tier 1 dungeon drops a T7 affix and the seven-tier curve does nothing for
progression. The issue left three things open: what the gate is, whether it is a
hard cap or a weighted range, and whether any tiers are drop-only.

**THE RULE. `affix tier <= min(7, difficulty tier)`, applied to the drop and to
crafting alike. Below the cap, a drop rolls uniformly from T1 up to it.**

**DERIVED, NOT CHOSEN: the gate is the difficulty tier.** It is this design's own
gate three times already. Gear and gem rarity equal the difficulty tier. The best
upgrade stone that can drop is capped by the current difficulty tier. A weapon
rolls damage types up to the lower of its own limit and the tier it dropped on,
which is `min(own limit, tier)` — exactly the shape used here. This is a fourth
use of an existing mechanism rather than a new one. The other two candidates the
issue named were dungeon depth and enemy rarity; depth already pays in days and
in enemy score, and neither has a precedent in the document.

**RESEARCHED: a hard cap on the maximum, with every tier at or below it still in
the pool.** Path of Exile gates modifier tiers on item level, and item level
expands which tiers are *available* rather than removing the low ones, so a high
item level gives better potential and guarantees nothing. Last Epoch gates the
same way on area level, and its top tier needs area level 90. Sources:

- https://pathofexile.fandom.com/wiki/Modifiers
- https://www.lastepochtools.com/news/article/introducing-tier-6-and-7-item-affixes-22279
- https://www.icy-veins.com/last-epoch/crafting-guide

A uniform draw from T1 to the cap is the simplest rule with that property and it
invents no constant. At difficulty tier 8 it gives a mean of T4 and reaches T7
about one drop in seven. That the draw is uniform rather than weighted is the
part most likely to want tuning against real play; the shape is the decision, the
distribution inside it is a starting point.

**JUDGEMENT, NOT DERIVED: the doubled tier goes at the top.** There are eight
difficulty tiers and seven affix tiers, so one affix tier serves two difficulty
tiers. Tiers 7 and 8 both reach T7. Doubling at the bottom instead — `max(1, tier
- 1)` — would leave tiers 1 and 2 both stopping at T1. Early progression has
fewer other things climbing alongside it; at the top, gear rarity, gear upgrade
level and filled sockets are all still rising, so a flat step there costs less.

**JUDGEMENT, NOT DERIVED: the cap applies to crafting as well as to the drop.**
Capping only the drop would leave the gate doing nothing, because the Potency
Crystal raises an affix one tier at a time and a tier 1 player would craft to T7
rather than find one. The design already gates progression rather than the
source: the upgrade stone rule caps what can drop by the current difficulty tier
for the same reason. The consequence — carrying an old piece forward into a
higher tier and raising its affixes again — is wanted rather than tolerated.

**NO AFFIX TIER IS DROP-ONLY.** Last Epoch makes its top two tiers uncraftable.
Its developers' stated reason is that with tier 5 as the crafting ceiling it was
too easy to reach near-perfect items, which made hunting for gear less exciting
than gambling and crafting. The tier cap answers that here instead: crafting
cannot outrun the player's own progress, so it cannot produce a near-perfect item
early. A dropped high tier also still saves the days at the forge that raising it
would have cost, and a day at the forge is a day not defending the empire. A
drop-only band remains addable later without changing anything else, because the
drop cap and the crafting ceiling are two separate numbers. **This is the part of
the decision most open to reversal**; it is raised for the project owner in the
issue filed alongside this entry.

**Affects.** `docs/Cataclysm_GDD_v2.md`, new section "What Tier an Affix Can Roll
At" in section VI, and the opening of the Affix Tiers section, which now points
at it. `docs/All_Things_Cataclysm.xlsx`, the Crafting sheet, where the Potency
Crystal's function now says it never raises an affix above the current difficulty
tier, and the generated `game/Data/CraftingMaterials.csv` and
`game/Content/Data/DT_CraftingMaterials.uasset` with it. **Applied.**

**In the model.** `sim/cataclysm_sim/affixes.py` gained `max_affix_tier(tier)`
and `roll_affix_tier(tier, rng)`, and a module-level check that the gate reaches
T7, starts at T1, never leaves the affix tier range and never falls as the
difficulty tier rises. The loot roll itself is issue #44.

---

## 2026-08-05 — One affix per group: an affix belongs to a group for every stat it grants

**The question.** Issue #128. `docs/Cataclysm_GDD_v2.md` restricted affixes by
gear slot and by prefix against suffix, and by nothing else. Nothing stopped a
four-affix Masterful piece rolling "Flat maximum health" four times. The issue
left three things open: what a group is, whether a hybrid affix collides with its
own halves, and whether prefixes and suffixes can share a group.

**THE RULE. An affix belongs to a group for every stat it grants, named by the
stat and the kind together. One piece holds at most one affix from any group.**

**RESEARCHED, NOT INVENTED.** Path of Exile calls this a mod group and it is the
only mechanism making two modifiers on one item mutually exclusive. The published
specification of the Path of Exile 2 data states it as "a string identifier
shared by one or more modifiers" and "the sole mechanism for mutual exclusion
between mods on a single item", with the constraint "only one modifier from any
given mod group may exist on an item at any time". Craft of Exile's mod groups
page and the Path of Exile wiki state the same rule for Path of Exile 1: the
existence of a modifier on an item prevents one of the same mod group being
added. Sources:

- https://github.com/Isayi9999/sift-public/blob/main/POE2_MOD_GROUPS_SPEC.md
- https://www.craftofexile.com/modgroups
- https://pathofexile.fandom.com/wiki/Modifiers

I could not confirm from public sources whether Last Epoch or Diablo 4 forbid a
duplicate affix on one item. Searches returned crafting guides that describe two
prefixes and two suffixes per item but state no duplicate rule either way. That
part is therefore not evidence and is not cited in the design document.

**DERIVED, NOT AUTHORED: the group comes from what the affix grants.** Both Path
of Exile games write the group onto each modifier by hand. This project derives
it from the stat and the kind, so a new affix cannot be added without a group and
two affixes granting the same stat in the same kind cannot be given different
groups by mistake. That also means `game/Data/Affixes.csv` needs **no new
column**: the group is computable from the columns already there, `Stat` and
`ValueKind` for a stat affix, `HybridPart1` and `HybridPart2` for a hybrid,
`Ailment` for an ailment affix, and the rolled damage types for a resistance
affix. Issue #128 proposed a column; a derived group cannot drift from what the
affix grants and a column can, which is the same argument that removed the
duplicated resistance cap in #233 and the duplicated hybrid ratio before it.

**JUDGEMENT, NOT DERIVED: a hybrid occupies the group of each of its halves.**
Path of Exile 2 goes the other way and gives a hybrid its own group, so both the
pure and the hybrid version can sit on one item. Two reasons to differ. A hybrid
here grants each half at 70% of the single affix, so a piece carrying "Health and
armor" beside "Flat maximum health" carries the same stat twice, which is what
the rule exists to stop. And a piece here has two prefix slots against that
game's three, so the same allowance concentrates a piece far more. This is the
part of the decision that is a call rather than a reading, and it is the part to
revisit if items come out feeling too constrained in play.

**FALLS OUT OF THE RULE: resistance is grouped by damage type, not by family.**
The eight resistances are eight stats on the character sheet, so they are eight
groups. A single-resistance roll occupies one of them, so two single rolls
covering different types may share a piece. An all-resistance roll occupies all
eight and so excludes every other resistance affix on that piece. No separate
rule was needed for the three families.

**ALREADY ANSWERED: prefixes and suffixes cannot share a group.** The design
already says a stat appearing as a prefix never appears as a suffix, and
`sim/cataclysm_sim/affixes.py` has enforced it since the pool was built. The two
pools are group-disjoint without a rule of their own.

**What it does not do.** It constrains one piece, not one character. Capping a
resistance takes roughly twelve affix slots across a set and is meant to.

**Affects.** `docs/Cataclysm_GDD_v2.md`, new section "One Affix Per Group" in the
Affixes part of section VI. **Applied.**

**In the model.** `sim/cataclysm_sim/affixes.py` gained `stat_group`,
`ailment_group`, `resistance_group`, `groups_of`, `may_join`,
`draw_without_repeating_a_group`, `everything_for` and `distinct_groups_for`, and
a module-level check that every gear slot still offers enough distinct groups to
fill its two prefix and two suffix slots. The loot roll itself is issue #44 and
is not built here.

---

## 2026-08-05 — Increased damage against a target's damage type is worth 400%, against the generic affix's 125%

**The question.** The project owner approved eight affixes on 2026-08-04, one per
damage type: increased damage against War, Demonic, Death, Pestilence, Famine,
Celestial, Chaos or Void enemies, and stated that they must give a larger
increase than the generic Increased Damage affix. Issue #213 left four things
open: how much larger, whether a two-type or all-type version exists, prefix or
suffix, and whether it multiplies or adds.

**STATED BY THE OPERATOR.** Eight affixes, one per damage type. The generic
Increased Damage affix stays. The type-specific ones must be worth more, so they
are a real choice and so a player has a reason to change equipment when the
Cataclysm they are fighting changes.

**DERIVED, NOT CHOSEN: 400% at T7.** It is the ratio this project already pays
for narrowing a modifier from all eight damage types to one. The resistance
families give 20% per type at breadth one and 6% per type at breadth eight, so
narrowing is worth 20/6, about 3.33 times. The generic damage affix is the
breadth-eight case, because it applies whatever the target is. 125% times 3.33 is
417%, rounded to 400% because the design document quotes round numbers for the
resistance ladder too.

**What 400% produces, which is the point of it.** A run starts with one Cataclysm
active and adds one each time a Cataclysm is defeated, up to eight. The generic
affix is worth 125% whatever stands in front of the player; a type-specific one
is worth 400% against its own type and nothing against the other seven, so across
C active Cataclysms it averages 400/C. The two are equal at C = 3.2. So the
type-specific affix is the better use of a prefix for the first three Cataclysms
of a campaign and the generic one from four onward. That is the same crossover
shape the resistance ladder already has at 3.33, and it is a difficulty curve the
affix produces on its own with no further tuning.

**A BALANCE RISK RECORDED RATHER THAN ARGUED TO A CONCLUSION.** Resistance is
capped at 70%, so its 3.33 ratio cannot run away. Damage is not capped. Four
prefix rolls of this affix give +1600% increases against one type where four
generic rolls give +500% against everything, and in a one-Cataclysm run that is a
3.2 times swing on the largest damage bucket. The ratio is defensible; the
magnitude is a question for real play. Filed separately so it is measured rather
than debated.

**DERIVED: it adds into the increases bracket rather than becoming a third
multiplier.** The pipeline is (base + flat) x (1 + increases) x more1 x more2.
Diablo 4 states the rule outright — a damage bonus with a stated condition is
additive, not multiplicative — and Last Epoch sums all compatible increased
damage modifiers. A separate multiplier would be far stronger and much harder to
balance. Sources: the Maxroll in-depth damage guide for Diablo 4, and the Maxroll
damage calculations page for Last Epoch.

**DERIVED: prefix, in the same slots as the generic Increased Damage affix.**
Gloves, Necklace, Relic, Ring and Weapon. The project's own rule is that a prefix
makes a number bigger and a suffix changes how much of something gets through,
and this makes a number bigger. Putting the two in the same position and the same
slots is what makes them compete for one slot on one piece; splitting them across
positions would let a player take both and remove the choice.

**JUDGEMENT, NOT DERIVED: no two-type or all-type version.** The all-type version
already exists — it is the generic Increased Damage affix, so a second one would
be the same affix twice. A two-type version would sit between the two, in the way
the two-resistance affix sits between single and all resistances, and it is
deliberately not built: the two ends have to be played before a middle rung can be
priced, and a Stat-kind affix has no breadth mechanic today, which only
Resistance-kind affixes have.

**Why this shape works where the weapon-side version did not.** An earlier
proposal increased a specific damage type the player *deals*, from their weapon.
It was rejected and closed as #206, because a weapon carries several damage types
at once, so the affix would always be diluted and the player could not concentrate
to fix it. This one reads the target's type instead, which the design already
has: an enemy has a damage type of its own, which is its Cataclysm's, and that is
already what decides which of the player's eight resistances applies.

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change: a new "Damage Against
a Target's Type" subsection after Health and Damage Affixes, and the Character
Sheet section's stat count, which goes from 35 to 43.
`All_Things_Cataclysm.xlsx`, Affixes sheet, eight new rows, 70 to 78.
`game/Data/Affixes.csv` and `game/Content/Data/DT_Affixes.uasset`, both
regenerated. `sim/cataclysm_sim/character.py` gains eight stats in the Offense
group; `sim/cataclysm_sim/affixes.py` gains the eight affixes;
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.h` and `.cpp`
gain eight attributes. Nothing reads them yet: applying a conditional increase
needs the damage calculation to know the target's type, which belongs with the
affix system in #44.

---

## 2026-08-04 — The weapon damage type column is named as a maximum, because that is what it holds

**The question.** The column that says how many damage types a weapon can carry
was called `Damage Types` in the workbook, `DamageTypeSlots` in the generated
CSV, `damage_type_slots` in the simulation model and `DamageTypeSlots` in the
Unreal row struct. Since the decision recorded below, the number in it is the
MOST damage types the weapon can ever hold, rolled down when the item drops and
capped again by the difficulty tier. A name reading as a count on a value that
is a limit is a name that will be read wrongly. Issue #218.

**The decision: rename it in all five places.** `Max Damage Types` in
`docs/All_Things_Cataclysm.xlsx`, `MaxDamageTypes` in `game/Data/ItemBases.csv`
and in `FCataclysmItemBaseRow` in
`game/Source/Cataclysm/Data/CataclysmDataRows.h`, and
`max_damage_types_on_base` in `sim/cataclysm_sim/affixes.py`.

**Why the simulation field is the long one.** `sim/cataclysm_sim/affixes.py`
already has a module-level function `max_damage_types(hands, tier)` returning the
effective cap at a difficulty tier, which is a different number: the lower of the
base's own limit and the tier. A field called `max_damage_types` would read as
the same thing. The `_on_base` suffix says which of the two limits it is.

**Why it was worth its own change.** The rename had to move the workbook sheet
header, the header lookup in `tools/generate_datatables.py`, the simulation
model, two test files and the Unreal row struct together, and the last of those
needs a C++ rebuild and a DataTable asset rebuild. Doing it inside the change
that altered the meaning would have buried that decision under mechanical edits.

**A guard was added because the existing ones could not see the whole chain.**
`Cataclysm.Data.EveryGeneratedTableImports` catches a CSV column with no matching
struct property, and it fired when tested. But it reads the CSV, not the shipped
DataTable asset, and `Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt`
compares the asset against the CSV through the same struct, so a column both
sides fail to match would agree at zero and pass. The new
`Cataclysm.Data.ItemBasesHoldADamageTypeLimit` in
`game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp` reads the asset and
checks the numbers are four on a one-hander, eight on a two-hander and zero on
anything that is not a weapon. Proved by rebuilding the assets from a CSV with
the old header and watching it fail.

**Affects:** `All_Things_Cataclysm.xlsx`, Item Bases sheet header, applied in this
change. `Cataclysm_GDD_v2.md`, the Weapon Bases table header and the sentence
above it, applied. `game/Data/ItemBases.csv` and
`game/Content/Data/DT_ItemBases.uasset`, both regenerated.

---

## 2026-08-04 — Attribute affixes are percentage increases, suffixes, and roll where the stats they drive roll

**The question.** Gear granting primary attributes was reversed in the entry
below, so the eight affixes had to be designed and built. The project owner set
three of the four properties and left the rest to be derived.

**STATED BY THE OPERATOR.** Percentage increases only, never flat. No hybrids.
Suffixes.

**Why percentage only is the interesting choice, and not just a smaller one.** A
flat "+5 Spirit" is worth the same to every character. A percentage increase is
worth little to a character spread across several attributes and a great deal to
one that has specialised. So the affix pays out on a decision the player already
made rather than handing out the same value regardless. That is the whole design,
and a flat version would undo it.

**DERIVED, NOT CHOSEN: the value is 12%, matching every other "increased" affix.**

The arithmetic lands somewhere defensible. An attribute grants 2% of its main
stat per point, so +12% of an attribute held at V points is worth 0.12 x V x 2%
of that stat. That equals a dedicated 12% single-stat affix at exactly V = 50,
which is heavy specialisation out of the 100 points levelling gives. Below 50 the
attribute affix is worse than the dedicated one; above it, better. It also drives
the attribute's second stat for free, which is affordable because attribute
affixes are suffixes and the dedicated increases are prefixes, so they never
compete for the same slot.

This is a first number rather than a tuned one. It should move against real play.

**DERIVED, NOT CHOSEN: which slots each one rolls on.**

An attribute affix rolls wherever the stats that attribute drives already roll,
read from `game/Data/Attributes.csv` and the existing pool. Nothing was picked by
hand.

That produces the right answer for weapons without needing a rule about weapons.
The pool already only puts offensive suffixes on a weapon. Ferocity drives
critical strike and Efficacy drives area of effect, both of which roll on weapons,
so those two attributes reach a weapon. Vitality drives health and Constitution
drives armour, which do not, so those two cannot. **Ferocity and Efficacy are the
only two of the eight that can appear on a weapon**, and no new rule was written
to make that true.

**AN ATTRIBUTE IS NOT A STAT, AND THE MODEL HAD TO BE TOLD SO.**
`sim/cataclysm_sim/affixes.py` refuses any affix naming something outside
`AFFIXABLE_STATS`, which exists so an affix cannot silently grant something
nothing reads. Attributes failed that test while being read by more of the model
than most stats are: `character.py` keeps them in `ATTRIBUTE_EFFECTS` rather than
in the stat groups, because an attribute holds no value of its own and turns each
point into increases on the stats it drives.

They are admitted by name rather than by widening the test, so the guard still
refuses a typo. A test asserts both halves: that an attribute affix constructs,
and that an invented stat still raises.

**FOUR PLACES PINNED THE OLD RULE AND ALL FOUR HAD TO MOVE TOGETHER.** This is
the part worth remembering, because missing any one of them would have failed
somewhere far from the change:

  - `docs/Cataclysm_GDD_v2.md` said "There are no attribute affixes" under a
    heading "What Affixes Do Not Grant". That rule is replaced by a new
    "Attribute Affixes" section; the second rule under that heading, that no
    ordinary affix is a "more" multiplier, is untouched and the heading stays.
  - `tools/tests/test_what_affixes_do_not_grant.py` asserted the old rule against
    all three copies of the affix pool. Its own docstring anticipated this:
    reversing a rule means changing the design document and that file together.
    The tests are reversed rather than deleted, because the failure they guard
    against — an affix in one copy of the pool and not the others — has not
    changed. It now also asserts the old sentence is **gone** from the design
    document, so reinstating either half without the other fails.
  - `sim/tests/test_affixes.py` had a test named
    `test_there_are_no_attribute_affixes`.
  - `game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp` pins the affix row
    count, which rose from 60 to 68. That one fails only inside Unreal, so a
    Python-only test run would have missed it entirely.

**Affects:** `All_Things_Cataclysm.xlsx`, Affixes sheet, 8 new rows.
`game/Data/Affixes.csv`, regenerated, 60 rows to 68. `Cataclysm_GDD_v2.md`, a new
Attribute Affixes section. `sim/cataclysm_sim/affixes.py`, the eight affixes and
the widened stat guard. Three test files as above.

**Still open.** Rounding. A percentage of an integer attribute is fractional, and
nothing says whether attributes carry fractions or round. The simulation treats
attribute points as numbers and does not care; Unreal's attributes are floats, so
fractions work there. It needs stating before the interface shows a player their
attributes.

---

## 2026-08-04 — Maximum resistance stays an enchantment, and the cap it raises is hard capped at 90%

**The question.** Issue #215. The project owner raised that maximum resistance is
worth having and cannot be an ordinary affix: every affix has seven tiers and can
appear on several of the ten equipment slots, and that range is far too wide for a
modifier multiplicative with every other defensive stat. Four things needed
deciding — the placement, whether it covers one damage type or all eight, the
value, and whether there is a ceiling.

**Three of the four were already answered by the data, which the audit that
produced #215 did not read.** `game/Data/EnchantmentsPositive.csv` already holds
"You have +10 maximum resists" at weight 1, and `game/Data/EnchantmentsNegative.csv`
holds three enchantments that lower the maximum. So:

| Question | Answer | Where it came from |
|---|---|---|
| Affix or enchantment? | Enchantment | Already is one |
| One damage type or all eight? | All eight, in one enchantment | Already is |
| What value? | +10 | Already is, and weight 1 is the rarest and most powerful tier |
| Is there a ceiling? | **No, and that was the real gap** | Nothing anywhere stated one |

**The decision: the maximum is hard capped at 90%.** `MAX_RESISTANCE_CEILING` in
`sim/cataclysm_sim/damage.py`, stated in the design document's new Maximum
Resistance subsection.

**Why a ceiling and not a tuning pass.** Damage taken is proportional to 100%
minus resistance, so the last points are worth far more than the first. 70 to 80
removes a third of what still gets through; 80 to 90 removes half of what is left;
100 removes all of it. A modifier that is worth more the more of it you take needs
a hard stop, not careful pricing, because no price is right at every quantity.

**Where 90 comes from.** Path of Exile caps resistances at 75% and hard caps
maximum resistance at 90%, reached in 1% steps from rare modifiers, and it exists
there for this reason. It is the only figure available from a shipped game. The
ratio carries over: 75% to 90% is 2.5 times less damage taken there, and 70% to
90% is 3 times here. With one enchantment worth +10, two reach the ceiling and a
third is wasted, which is the property the ceiling is for.

**A second decision, smaller and needed to stop this recurring: no affix may raise
the maximum.** The Caps table in `Cataclysm_GDD_v2.md` read "Soft. Affixes may
raise the cap itself", which is wrong twice over. No affix raises the cap, and the
sentence conflates two different things that the new subsection now separates:

    over-capping         having more than 70% resistance. Any resistance affix
                         does it, and it is worth having because penetration and
                         Overwhelm are subtracted before the cap is applied.
    raising the maximum  moving the 70% itself, so more of a hit is stopped.

`tools/tests/test_maximum_resistance.py` holds the rule against all three copies
of the affix pool and against both enchantment tables.

**What was not done, and why.** No per-damage-type maximum resistance enchantment
was added. The design's ordinary resistance affixes come in breadths of one, two
and eight, so a per-type version would be consistent, but the +10 value was set
for an enchantment covering all eight, and eight narrower ones would each need a
value, a weight and a tag set. That is a design of its own rather than part of
answering this issue.

**Affects:** `Cataclysm_GDD_v2.md` gains a Maximum Resistance subsection after
Overwhelm; its Caps table row for resistances is corrected; its Resistances
paragraph now says which sources do which thing. `sim/cataclysm_sim/damage.py`
gains `MAX_RESISTANCE_CEILING`. `tools/tests/test_maximum_resistance.py` is new.
No data changed: the enchantment and its three negative counterparts already
existed. Source:
[Resistance, Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Resistance).

---

## 2026-08-04 — Leech is defined, and gains mana and energy shield alongside life

**The question.** Issue #214. `game/Data/Affixes.csv` had exactly one leech
affix, flat life leech, while the design already relied on two more:
`Cataclysm_GDD_v2.md` describes the Energy Leech class as draining enemy mana to
refill its own pool, names leech as a recovery stat group, and lists it as a
suffix category. Neither mana leech nor energy shield leech existed as a stat, so
nothing could grant them and the class could not be geared for.

**A second gap sat underneath it, and had to be closed first.** Nothing anywhere
said what leech DOES. The stat existed, the Ravager had 3% of it, an enchantment
doubled it, and no document said whether it was a share of damage or a flat
amount, whether it arrived at once or over time, or whether overkill counted.
Adding two more undefined stats would have made that worse.

**The definition, now in the "Leech" section of `Cataclysm_GDD_v2.md`.**

| Rule | Value |
|---|---|
| What is leeched | A percentage of damage actually dealt |
| Damage counted | After the target's mitigation, capped at the target's remaining health |
| Payout period | 3 seconds from the hit |
| Concurrent payouts | Unlimited; each hit pays out separately |

**Where the shape came from.** Both games surveyed treat leech the same way and
neither makes it instant. Last Epoch pays a leeched amount out over a fixed
3-second period, excludes overkill, and calculates from the damage the target
really took after its resistances and block. Path of Exile instead caps each leech
instance at 10% of maximum life and the total recovery rate at 20% of maximum life
per second. Last Epoch's shape was taken because it is one rule rather than a
system of caps, which matches how this design already handles ailments: one stack
per enemy, and chance above 100% becoming magnitude.

**Why it is not instant, which is the part worth keeping.** Instant leech makes a
character that is winning unkillable and does nothing for one that is losing,
because the recovery only arrives as fast as the damage does. Spreading it over
3 seconds means a burst of damage is not a burst of health, and that is what makes
leech a sustain stat rather than a second health pool.

**Why overkill is excluded.** Without the rule, the killing blow on every trash
enemy is the largest heal in the game, which rewards overkilling rather than
fighting.

**Two new stats, two new affixes.** The character sheet goes from 33 stats to 35:
`mana_leech` and `energy_shield_leech` join `life_leech` in the Recovery group.
Flat mana leech and flat energy shield leech join flat life leech in the affix
pool, as suffixes on the offensive slots, which is where a hit comes from.

**ALL THREE HAVE THE SAME TOP VALUE, 0.5, AND THAT IS A JUDGEMENT RATHER THAN A
DERIVATION.** Leech is a percentage of the same damage number in every case, so
the same percentage returns the same absolute amount; what differs is only which
pool it fills, and choosing between the pools should be the player's decision
rather than one the numbers make for them. Path of Exile prices mana leech below
life leech, and the reason does not carry over: its mana pools are an order of
magnitude smaller than its life pools, where here they are comparable for the
class that wants each. At level 100 the Ritualist, the caster, has 1,278 mana
against 1,060 health and 832 energy shield.

**Energy shield is leechable even though the Vampire class cannot use one.** That
is a restriction on one class rather than on the stat. The Ritualist is the class
with an energy shield and it is the one this is for.

**Both numbers are expected to move.** The 3-second period and the 0.5 top value
are starting points to be tuned against real play, in the way this project tunes
every constant.

**Affects:** `Cataclysm_GDD_v2.md` gains a "Leech" section under Stat
Calculation, its Character Sheet section now says 35 stats and lists the two new
ones. `All_Things_Cataclysm.xlsx` gains two rows in the Affixes sheet.
`game/Data/Affixes.csv` and `game/Content/Data/DT_Affixes.uasset` are regenerated.
`sim/cataclysm_sim/character.py` and `sim/cataclysm_sim/affixes.py` carry the
model. `game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.h` and
`.cpp` gain `ManaLeech` and `EnergyShieldLeech`; nothing reads any of the three
yet. `tools/tests/test_leech.py` holds the rules to the document. Sources:
[Health Leech, Last Epoch Support](https://support.lastepoch.com/hc/en-us/articles/46361876598555-Health-Leech),
[Leech, Path of Exile Wiki](https://pathofexile.fandom.com/wiki/Leech).

---

## 2026-08-04 — Nine decisions from an audit of the affix pool, including two reversals

**The question.** The affix pool holds 59 rollable affixes. The project owner
thought it was missing a great deal, named minion and damage-over-time affixes
specifically, and asked for an audit.

**How the audit was done, and where it fell short.** Every attribute across the
five attribute sets in `game/Source/Cataclysm/AbilitySystem/` was cross-referenced
against the `Stat` column of `game/Data/Affixes.csv`. That found every attribute
that has no affix, and every mechanic that has neither.

It was a **data-level reading only, and it never searched the design document for
rules about what affixes deliberately do not do.** That is why it reported the
absence of attribute affixes as a hole when `Cataclysm_GDD_v2.md` already had a
section headed "What Affixes Do Not Grant" saying otherwise. An audit that reads
data and not prose will keep making that mistake. Recorded here so the next one
reads both.

**REVERSAL 1: gear grants primary attributes after all.**

`Cataclysm_GDD_v2.md` states under "What Affixes Do Not Grant" that there are no
attribute affixes, because the design gives one point per level and the Maw
consumes items and enemies for more, so gear granting them would be a new
mechanic rather than a filled gap. That rule was recorded on 2026-08-03 and pinned
by a test on 2026-08-04.

**The operator has reversed it: attributes must be slottable on gear.** The
reasoning that supported the old rule is not disputed; the decision changed.

Two consequences that are easy to miss. `tools/tests/test_what_affixes_do_not_grant.py`
asserts the old rule against all three copies of the affix pool and will fail on
the first attribute affix, so it has to be amended rather than worked around. And
the Maw still grants attribute points, so an attribute affix has to be priced
against what the Maw already gives, not against level-up points alone.

**REVERSAL 2: minions get stats of their own.**

`Cataclysm_GDD_v2.md` says a minion deals 30% of its summoner's weapon damage,
attacks once per second, and has no stats of its own, and states outright that a
minion stat family "is not wanted for two skills".

The operator has reversed this. Each summoning skill gets its own minion stats,
base scaling comes from an attribute rather than from the summoner's weapon
damage, and minion affixes exist on gear.

**Moving base scaling off weapon damage is what makes the rest safe.** Under the
old rule, weapon damage affixes already scaled minions; adding minion damage
affixes on top would have scaled them twice from one investment. Scaling from an
attribute instead removes the double count, and makes reversal 1 a prerequisite:
if minions scale off an attribute and gear cannot grant attributes, the only lever
left is level-up points, which is worse than what it replaces.

**DECISION 3: damage against a target's damage type, not damage of a type.**

An affix increasing a damage type the player *deals* was proposed and rejected. A
weapon carries several damage types at once, so such an affix would always be
diluted, and the player could not concentrate to fix it.

Eight affixes are added instead, keyed to the **target**: increased damage against
War, Demonic, Death, Pestilence, Famine, Celestial, Chaos and Void enemies. The
type-agnostic "Increased damage" affix stays, and **the type-specific ones must
give a larger increase**, or they are a strictly worse roll rather than a choice.

This works because an enemy already has a damage type of its own, which is its
Cataclysm's, and because it reads the enemy rather than the weapon it does not
care how many types the weapon carries. It also sharpens over a run: early runs
face one Cataclysm so the matching affix is reliably strong, and late runs face up
to eight so the generic increase becomes the safe choice.

**DECISION 4: ailments and damage over time get scaling.**

Nine affixes apply an ailment and exactly one touches damage over time, changing
only how often it ticks. Nothing makes a damage-over-time effect hit harder or an
ailment last longer. The operator confirmed this needs flat damage-over-time
damage, duration, chance, and more sources of tick frequency than the single
existing affix.

**DECISION 5: leech exists for mana and energy shield, not only life.**

Only "Flat life leech" exists. The design already relies on the others: one class
drains mana to refill its own pool, another is built on life leech, leech is named
as a recovery stat group, and the gameplay tag `Stat.Recovery.Leech` exists.

**DECISION 6: maximum resistance is an enchantment, not an affix.**

It is worth having, and it does not survive seven affix tiers. A tier 7 roll worth
+7% maximum resistance, repeatable across ten equipment slots, reaches immunity.

Enchantments solve every part of that: they are not tiered, they are Legendary
rarity and above only, and they already carry a weight from 1 to 4 and tags
controlling which items they roll on. The design also already frames choosing
between an affix and an enchantment as a core build decision, which is exactly
what a modifier this strong should be.

**DECISION 7: the anti-stun-lock rule comes before any offensive crowd control
affix.**

Crowd control resistance exists; nothing inflicts crowd control. The operator's
requirement is that crowd control must not become tedious, with no stun-locking
and no chance for the smallest hit to stun.

**The ordering is the decision.** Whatever rule stops the player being stun-locked
is the same rule that stops the player chain-stunning a boss. Building the
offensive affixes first means retrofitting that rule around numbers already tuned.

**DECISION 8: effectiveness multipliers are wanted, and are folded into the
skill-behaviour work.**

Armour effectiveness and block effectiveness. Both have the same shape as the rest
of that gap: the quantity exists as an attribute, and the multiplier governing what
the quantity does is not modelled. Neither has anything to multiply until the
damage mitigation formulas for armour and block are written down, which they are
not.

**DECISION 9, AND THE ONE MOST LIKELY TO BE RE-PROPOSED: no affix may modify a
discrete action the player takes somewhere safe.**

A family of affixes touching the empire and time layer was proposed — reduced days
lost on death, reduced crafting time, reduced crafting cost, reduced Cataclysmic
Residue, reduced travel time, faster city upgrades. The core tension of the game is
that every action costs days, and no affix engages with it.

**The operator rejected it, and the reason generalises into a rule.** Reduced
crafting time is free: the player walks to the capital, swaps to crafting gear,
crafts, and swaps back. Nothing was traded for the benefit.

So: **an affix whose benefit applies to a discrete action performed in a safe place
is not a cost, because the player can wear it only for that action.** That rules
out every item in the list above. It also rules out the one candidate the operator
was willing to consider, reducing a dungeon's floor count on entry, because entry
is a moment the player controls completely and would gear for.

The only escape is restricting when equipment may change, as Path of Exile does
inside a map. That is a far larger decision than an affix family and was not taken.

**This rule exists to stop the idea being re-proposed.** It sounds good every time.

**Affects:** no design document yet. Every decision above is filed as its own
issue with the open questions it still carries: #204 attributes, #205 ailments and
damage over time, #207 skill behaviour and effectiveness multipliers, #209
minions, #213 damage against a target type, #214 leech, #215 maximum resistance,
#216 crowd control ordering. This entry records the reasoning; the issues carry
the work.

**A process note, because it caused a wrong outcome today.** Two sessions worked
this backlog at once. A decision made in conversation is invisible to the other
session until it reaches the repository, and issue #204 was closed against a rule
the operator had already reversed. Five collisions happened; that was the only one
that produced a wrong result rather than duplicated effort. Decisions have to reach
an issue or this log before other work continues.

---

## 2026-08-04 — A weapon holds 1 to 8 damage types, rolled on drop and capped by tier

**The question.** The project owner stated that a weapon can carry anywhere from
one damage type to all eight. The data and the design document both said
otherwise, and had done since they were written.

**What was actually there.** Three sources agreed with each other and disagreed
with the owner. `game/Data/ItemBases.csv` gave every one-handed weapon 2 damage
types and every two-hander 3. `sim/cataclysm_sim/affixes.py` hardcoded the same
two numbers. `Cataclysm_GDD_v2.md` said "the base says only how many it holds"
and built the whole justification for dual wielding on the 2-versus-3 split:
"Two one-handed weapons hold four damage types against a two-hander's three."

The owner confirmed the intended rule and asked for the data and documents to be
corrected rather than the rule.

**THE RULE, as stated by the owner.** A one-handed weapon can hold at most four
damage types; a two-handed weapon at most eight. The count on a particular weapon
is **rolled when it drops**, from one up to the lower of that limit and the
difficulty tier the item dropped on. A one-hander and a two-hander are therefore
identical up to tier 4, and diverge from tier 5, where a two-hander can begin
rolling five.

**WHY THE DUAL WIELDING ARGUMENT SURVIVES, AND WHY IT HAD TO BE REWRITTEN.** The
old sentence compared raw counts: four against three. Under the new rule the raw
limits tie, because two one-handers reach eight and so does a single two-hander.
Compared that way, dual wielding would lose its stated reason to exist.

The tier cap is what saves it, and it makes a better argument than the one it
replaces:

| Tier | Dual wielding | One two-hander | Dual wielder's lead |
|---|---|---|---|
| 1 | 2 | 1 | +1 |
| 2 | 4 | 2 | +2 |
| 3 | 6 | 3 | +3 |
| 4 | 8 | 4 | +4 |
| 5 | 8 | 5 | +3 |
| 6 | 8 | 6 | +2 |
| 7 | 8 | 7 | +1 |
| 8 | 8 | 8 | tie |

A dual wielder leads at every tier from 1 to 7, by the widest margin at tier 4,
and is matched only at tier 8. Dual wielding is the route to multiclassing for
seven eighths of the game and the two-hander finally catches up at the end, while
staying ahead on raw damage throughout. That is a progression curve rather than a
flat advantage, and nobody designed it deliberately — it falls out of the rule.

**A GUARD THAT COULD NOT FIRE WAS WRITTEN AND THEN REMOVED, WHICH IS WORTH
RECORDING BECAUSE IT NEARLY SHIPPED.** The check protecting the dual wielding
claim was first written as two conditions: is the dual wielder behind, and have
they tied before the last tier. The first can never fire. A two-hander gains
exactly one damage type per tier, so it cannot overtake a dual wielder without
passing through equality first, and the tie condition always catches it. The
branch was dead code that read like a safety net. It is now one condition, and
`sim/tests/test_affixes.py` proves it fires by lowering the one-handed limit to
two and watching a two-hander draw level at tier 4.

**What is NOT done, and is filed separately.** The column is still called
`DamageTypeSlots` in `game/Data/ItemBases.csv` and `Damage Types` in the workbook,
and both now mean a maximum rather than a count. The name is misleading and should
be changed, but renaming it touches the workbook sheet header,
`tools/generate_datatables.py`, the simulation model, its tests and the Unreal row
struct together, so it was kept out of this change.

*Done since, on 2026-08-04, by issue #218. The column is now `Max Damage Types`
in the workbook and `MaxDamageTypes` in the CSV and the Unreal row struct. The
entry at the top of this file records it.*

**The drop roll itself does not exist yet.** This change records the rule and sets
the limits. Rolling a count between one and the cap belongs with loot generation.

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change: the Weapon Bases
section's damage type column, the paragraph saying the base decides how many, and
the dual wielding paragraph. `All_Things_Cataclysm.xlsx`, Item Bases sheet, 14
weapon rows. `game/Data/ItemBases.csv`, regenerated. `sim/cataclysm_sim/affixes.py`
and `sim/tests/test_affixes.py`.

---

## 2026-08-04 — The rule that gear grants no primary attribute stands, and both rules in "What Affixes Do Not Grant" are now pinned by a test

> **SUPERSEDED THE SAME DAY, in part.** The project owner reversed the rule about
> attributes within the hour: gear must be able to grant primary attributes. See
> reversal 1 of "Nine decisions from an audit of the affix pool" above. The
> attribute half of this entry is history. The other half stands: no ordinary
> affix is a "more" multiplier, and
> `tools/tests/test_what_affixes_do_not_grant.py` still pins that.

**The question.** Issue #204 reported that none of the eight primary attributes —
Agility, Ferocity, Constitution, Vitality, Mind, Spirit, Efficacy, Luck — can be
found on gear, having cross-referenced every attribute against the `Stat` column
of `game/Data/Affixes.csv`. It asked whether attribute affixes should exist, and
said the absence "looks like an omission rather than a decision".

**The decision. Nothing changes in the affix pool.** It was already a decision,
made when the pool was built out for issue #79 and written in two places:
`Cataclysm_GDD_v2.md` has a section headed "What Affixes Do Not Grant" saying
"There are no attribute affixes", and this log's entry of 2026-08-03, "The affix
pool: prefixes, suffixes and implicits", gives the reasoning. The design gives
one attribute point per level and one other source,
the Maw, which consumes items and enemies for them. Gear granting attribute
points appears nowhere in the design, so an affix for it would add a mechanic
rather than fill a gap.

**Why it was reported anyway, which is the part worth fixing.** The audit read
the data and not the prose. A rule that exists only in a design document is
invisible to anyone checking the generated tables, so it gets re-reported. That
is what happened here, and it will happen again to the second rule in the same
section — "No ordinary affix is a 'more' multiplier" — unless the same fix is
applied to both.

**What was built instead of an affix.**
`tools/tests/test_what_affixes_do_not_grant.py` asserts both rules against all
three copies of the affix pool: the Affixes sheet of `All_Things_Cataclysm.xlsx`,
the generated `game/Data/Affixes.csv`, and `sim/cataclysm_sim/affixes.py`. It
also checks that the design document still states both rules, so the tests cannot
outlive the design they enforce, and it reads the eight attribute names from
`game/Data/Attributes.csv` rather than listing them, cross-checking those names
against `game/Source/Cataclysm/AbilitySystem/CataclysmPrimaryAttributeSet.h` so a
rename cannot quietly empty the guard.

**What is not settled by this.** Whether Luck is worth an attribute point at
+0.01% rarity find per point is issue #81 and is unaffected: gear reaches magic
find directly through the flat magic find affix and the increased loot quantity
affix, so the affix pool is not what limits Luck.

**Affects:** no design document change; both rules already read as decided. New
file `tools/tests/test_what_affixes_do_not_grant.py`.

---

## 2026-08-04 — Burn gets the Of Embers gem and the chance to burn affix, and an ailment affix is always five points above its gem

**The question.** Issue #152. Burn was the only one of the ten player-applicable
effects with no gem and no affix behind it. `Cataclysm_GDD_v2.md` says chance to
apply caps at 100% and everything above becomes magnitude, summed across affixes,
gems, keystones and enchantments. With neither, a Demonic character's burn chance
from gear was always zero and its magnitude could never rise above the base —
while every one of the sixteen designed Demonic skills applies burn. The skills
worked, because a skill applies its effect outright with no chance roll, which is
why this was invisible for a month.

**The decision, part one: a gem called Of Embers, at 10% chance, with the same
quality ladder as Of Rending.** The name follows the shape of the nine that exist
and ties to vocabulary the damage type already uses — Emberhurl is a designed
Demonic skill. The numbers are copied from Of Rending, the gem that applies
bleed, rather than from Of The Viper, which applies poison at twice the chance.

**Why bleed and not poison.** Bleed is War's signature damage over time and burn
is Demonic's: fifteen War skills apply bleed and sixteen Demonic skills apply
burn, and the two sit in the same place in their damage types. Making them cost
the same is the choice that keeps a Demonic ailment build and a War ailment build
comparable. Copying poison instead would have made burn the cheapest damage over
time in the game to reach, for no reason other than that it was added last. This
is a judgement, not a derivation.

**The decision, part two: an ailment affix's Top Value is its gem's stated
chance plus five.** This was already true of all nine — a 20% gem has a 25 affix,
a 15% gem a 20, and the seven 10% gems a 15 — but nothing said so, so the value
for a new ailment was a free choice. It is now written into the design document
and checked by `tools/tests/test_every_player_applied_effect_can_be_built.py`.
Elevating an observed regularity to a rule is the decision here; the burn value
of 15 then follows from it rather than being picked.

**What went wrong that let this happen.** The check that every gem effect is
reachable as an affix was a hand-written set of names in
`sim/cataclysm_sim/affixes.py`. Burn was in neither the set nor the sheet, so
nothing disagreed with anything. The new test reads the workbook instead, and its
load-bearing rule is the other direction: an effect whose own description calls
itself player-applied must have both a gem and an affix. That is what would have
caught burn.

**Affects.** `All_Things_Cataclysm.xlsx` Gems, Affixes and DoTs sheets;
`Cataclysm_GDD_v2.md`, "Ailment Affixes". Applied. The design document's table
also gained the chance to necrose row, which had been missing since the Of Wasting
gem was added.

---

## 2026-08-04 — Three backlog blockers cleared: Casual mode, rarity tiers, and the Masochist resource

**The question.** A pass over the open backlog looking specifically for issues
blocked on an operator decision or on a stale design document, so the loop session
could get on with implementation work that was waiting behind them.

Two implementation issues were blocked, and both by design gaps rather than by
code: issue #38, implementing a Demonic passive tree, waits on issue #63; and
issue #39, implementing the seven Demonic enemies, waits on issue #29.

**FIRST, SOMETHING ALREADY DONE.** Issue #61, the rule that the active Cataclysm
determines the player's damage type, is written into `Cataclysm_GDD_v2.md` and the
Phase 1 roadmap has already been corrected from War/Bulwark to Demonic/Masochist.
That issue was checked and closed rather than worked. Worth recording because the
issue text still described the contradiction as live.

**SECOND, SOMETHING DONE BY SOMEONE ELSE WHILE THIS WAS BEING WRITTEN, AND WORTH
RECORDING AS A PROCESS PROBLEM.** Issue #138, the stale control table, was fixed
independently and merged as pull request #203 while the same fix was being written
here. Both versions did the same job. The one on `development` is the better of
the two, because it names the `DefaultMappingContext` setting in
`game/Config/DefaultGame.ini` that chooses the scheme, and covers the mouse wheel
and the gamepad stick. The duplicate written here was discarded rather than merged.

Two sessions working the same backlog at once will keep doing this. The specific
cost here was small, but the same collision has now happened four times on
`DECISIONS.md`, where both sessions append a new entry to the top of the file and
every parallel change conflicts. Nothing about that is hard to resolve; it is
simply a recurring tax on running two sessions.

**DECISION 2: Casual is removed rather than defined.**

The risk table referred to a "Casual" difficulty mode that the difficulty table
never defined. The operator chose removal over definition: four modes stand, and
the risk table now names the four that exist. Defining a fifth would have added a
difficulty to tune, balance and support permanently in exchange for a sentence
nobody had written on purpose.

**Not fixed, and still open in issue #32.** That risk table has two further
problems this change does not touch: SSF is listed as a lethality mode when it is
really an orthogonal option that changes loot rules rather than difficulty; and
three modes say "increased loot drops" with no number while Hardcore gives an
equipment drop chance with no probability. Both need numbers that were not asked
for here.

**DECISION 2b: what accessibility covers, and what it does not. A claim made in
issue #32 was wrong and is withdrawn.**

Issue #32 asserted that Hardcore and Heretic hiding the heads-up display
contradicts the accessibility commitments in section XIII. Checked against what
section XIII actually lists — multiple language support, colour-blind palettes,
scalable interface elements, reduced ability effect opacity, and an epilepsy-safe
mode — there is no contradiction. Not one of those promises a heads-up display.
"Scalable HUD elements" governs a display when one is present; it does not
guarantee one exists.

The operator's position, adopted and now written into the design document:
accessibility means removing barriers that make the game **impossible** to play
for some people, which is perception, language and physical safety. It does not
mean removing difficulty, and it does not entitle a player to any particular
interface element. Hiding the display is a difficulty choice the player opts into.

This is recorded as a scope statement rather than a list item because it decides
future arguments rather than a single case. Any later proposal to add or keep an
interface element "for accessibility" has to show it removes a barrier of that
kind, not that it is helpful.

**DECISION 5: enemy telegraphs are readable, and punishing when ignored.**

Issue #29 leaves the seven Demonic enemies with one sentence each and no telegraph
specification, which blocks issue #39. The design target is now set: a clear
wind-up with enough time to react if the player is paying attention, and real
damage if they are not.

This rules out both a reflex-heavy soulslike shape, which raises the floor for who
can play and makes the empire layer feel like an interruption, and a forgiving
shape where build strength alone carries the fight. It also rules out varying the
target per enemy role, which was offered and not taken.

The reaction windows themselves are not set here. They should be derived from
shipped games in the genre when the enemy design is written, not invented, and the
target above is what that research has to hit.

**DECISION 3: the enemy rarity tiers follow the power model, and two multipliers
are left unset rather than invented.**

The design document listed Common, Elite, Rare, Legendary, Boss. The power model
lists Common, Elite, Legendary, Herald, Boss, Cataclysm Boss. `CLAUDE.md` states
that the power model is a port of an authoritative source and that the source
wins, so the model's list of tiers is correct and the document's was not. Rare does
not exist. Herald sits between Legendary and Boss; Cataclysm Boss sits above Boss.

**The numbers could not be aligned, and that is a finding rather than a shortfall.**
The two lists are in different units. The document gives a multiplier; the model
gives a weight expressed as a fraction of tier width. Converting one into the other
requires deciding what a multiplier means relative to a weight, which is a tuning
decision nobody has made. The two new tiers are therefore written as "not set",
with a note saying why. Filling them in by interpolation would have produced two
numbers that look authoritative and are not.

**DECISION 4: the Masochist resource has two generators, and the passive tree
decides which one a build leans on.**

Issue #63 records that the Masochist has no class resource. Its prose says
abilities cost health instead of mana, which is a substitution rather than a
resource, and the design document requires every class to have one.

Three shapes were put to the operator: a resource built by taking damage, a
resource built by spending health, or no resource at all with power scaling off
missing health. The answer was that it is built by **both** spending health and
taking damage, through nodes in the passive tree that do not exist yet.

That is a direction rather than a specification, and it is recorded as one. It
settles the part that was genuinely undecided — whether the resource has one
generator or two, and whether the health-cost rule replaces the resource or sits
alongside it. It leaves build rate, cap, decay and what spends it to be designed
with the tree, because the operator's answer makes them properties of tree nodes
rather than of the class.

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change, in three places: the
risk table in section XVI, the Rarity Multipliers table in section X, and the
Accessibility section in section XIII. The Masochist resource direction is recorded
here only, because it is not yet specific enough to state as design. The Controls
and Key Bindings section is not touched by this change; pull request #203 rewrote
it independently and that version stands.

**Still open.** Issue #32 for the rest of the difficulty table. Issue #30 for the
two unset multipliers. Issue #63 for the Masochist node graph and resource numbers.

---

## 2026-08-04 — Built lighting data for a map is not committed, because this project never bakes lighting

**The question.** Issue #140: opening the Unreal editor produced two working-tree
changes on its own. `game/Content/Maps/L_Sandbox.umap`, the sandbox level, was
resaved and grew from 26,748 to 35,728 bytes, and
`game/Content/Maps/L_Sandbox_BuiltData.uasset`, generated lighting and reflection
data, appeared as a new untracked file of 175,928 bytes. Both go through Git LFS,
so every resave stores another full copy. The issue asked for a deliberate choice
between committing built data and ignoring it.

**The decision.** Ignore it. `.gitignore` now has `*_BuiltData.uasset`.

**Why ignoring is right here and would be wrong elsewhere.** Built lighting data
is normally committed alongside its map, because for a game that bakes lighting
the bake IS part of the level and a fresh clone without it renders wrong. Three
facts make this project the other case.

- It renders with Lumen, which computes global illumination at run time. There is
  no bake to preserve.
- Dungeon floors are generated at run time, so they do not exist when a bake would
  have to happen and cannot be baked at all.
- `tools/generate_input_assets.py`, the script that builds the sandbox level, sets
  both the directional light and the sky light to Movable specifically so that no
  lighting build is ever required. That change landed in pull request #143 while
  fixing a different problem, and it is why no `L_Sandbox_BuiltData.uasset` exists
  today. The ignore rule is the second line of defence, not the first.

GitHub's own `UnrealEngine.gitignore` template carries the same rule, under the
comment "Built data for maps". That is a weaker argument than the three above,
because that template targets projects in general rather than this one, but it
means the choice is not unusual.

**The escape hatch is per-map, not a deletion.** If some future map does want
baked lighting, the right change is one exception line for that map
(`!game/Content/Maps/L_Whatever_BuiltData.uasset`) rather than removing the rule,
which would let every other map's regenerated data back in. The comment above the
rule in `.gitignore` says so.

**What this does not fix.** The map being resaved on open is a separate symptom
with a separate cause and it is not addressed by an ignore rule; ignoring a
tracked file does nothing. Measured on 2026-08-04: the editor had been running for
several hours with `/Game/Maps/L_Sandbox` loaded and `L_Sandbox.umap` on disk had
not been written since 2026-08-03, so the resave no longer happens on open either.
Whether the in-memory package is marked dirty, which would make the next manual
save write those bytes, was not measured.

**Affects.** `.gitignore` at the repository root. No design document changes.
Applied.

---

## 2026-08-04 — A projectile is an actor that sweeps each step, and Radius means two different things

**The question.** Issue #164: `UCataclysmProjectileSkill` turned a Speed into a
delay. It worked out `distance / speed`, waited that long on a timer, and then
resolved the whole hit using positions at the moment of impact. Nothing occupied
the space in between, so an enemy that stepped into the path after the throw and
out of it before the landing was never touched, one that stepped in just before
the landing was hit even though the projectile had already passed behind it, and
a wall stopped nothing.

**The decision.** A real actor, `ACataclysmProjectile`, that moves in steps.

**It sweeps each step rather than testing where it arrived.** Every step asks who
is inside the capsule from where the projectile was to where it now is, which is
the volume it actually passed through. Testing only the end of a step lets
anything narrower than the gap between two steps fall through it. This is not
theoretical: a step is capped at a tenth of a second, which at Blood Pyre's 1400
centimetres per second is 140 centimetres, and the projectile body is 40
centimetres wide. `Cataclysm.Skills.AProjectileHitsWhatItPassedThroughNotOnlyWhereItLanded`
fails if the sweep is replaced with a point test.

**A step is capped at a tenth of a second, and a long frame becomes several
steps.** A swept step is correct however long it is, but ORDER is not: a
projectile that does not pierce should stop at the first enemy and then not exist
for the second, and one step long enough to contain both makes that ordering a
property of the sweep rather than of time.

**Radius means two different things, and Pierce decides which.** For a piercing
skill it is the half-width of the line it hits along: Emberhurl, Chain of Coals,
Hellbrand and Infernal Lance are all `Radius=1.5`. For one that does not pierce
it is the blast where it stops: Blood Pyre is `Radius=3` and Magma Quake
`Radius=4`, which are the sizes of the pyre and the crater. The old code already
made this distinction — it searched a line for one and a sphere for the other —
but nothing said so, and building the projectile revealed why it matters: using
Blood Pyre's three metres as the width of the thing in the air stopped it three
metres short of the enemy it was thrown at, and the pyre then went off in front
of them rather than against them. A flying projectile is therefore 40
centimetres across unless it pierces, which is a little narrower than an enemy's
48 centimetre capsule so that a near miss stays a miss.

That 40 is a judgement, not a figure read off anything. The design does not state
how big a thrown axe is. It is a constant on the class rather than a column in
the Weapon Skills sheet, because it would otherwise be the same number written
into 398 rows; it becomes a shape parameter the first time a skill needs its own.

**Geometry stops it, characters do not.** The flight traces against the
visibility channel, which is the one that answers whether something solid stands
between two points. Pawns do not block that channel, so one enemy is never cover
for the enemy behind them: what a projectile passes through is decided by Pierce,
and what stops it is the world.
`Cataclysm.Skills.AnEnemyIsNotCoverForTheEnemyBehindThem` guards that, because
tracing against a channel characters blocked would silently make Pierce mean
nothing.

**Why not `UProjectileMovementComponent`.** Two reasons, and the second is the
real one. It moves only when the world ticks, and every automation test in this
project builds a world with `UWorld::CreateWorld` and never ticks it — which is
why `SwingOnce`, `Pulse`, `Land` and `SummonOne` are all public. And its collision
handling is built around blocking hits, whereas everything this project hits
responds with overlap; the component's `OnProjectileStop` path would never fire.
What it offers beyond that — gravity, bouncing, homing — is a list of things
these projectiles must not do.

**Where a projectile stops is now a real place, and the burning ground follows
it.** A throw halted by a wall burns half the path, not all of it. A throw that
returns finishes back at the caster, so the ground is measured to the furthest it
got rather than to where it ended up; measuring to where it ended up leaves a
patch at the caster's feet, which is what the first version of this change did.

**A Speed of zero is still a beam.** Infernal Lance is written `Speed=0` and its
description says it arrives at once. No actor is spawned and the skill resolves
the hit itself, exactly as before. Aiming at your own feet resolves the same way,
because there is no path to fly along.

**Affects.** No design document. `All_Things_Cataclysm.xlsx` is unchanged; every
number this uses was already in the Weapon Skills sheet.

---

## 2026-08-04 — A buff's magnitude is a tag-scoped modifier on the character, not a per-damage-type attribute

**The question.** Issue #166: `UCataclysmSelfBuffSkill` applied a self buff's
duration and nothing else. Burning Wrath grants "4% increased fire damage for
every enemy currently burning within 15 meters"; the count was taken and there
was nothing to multiply by it. The issue named the underlying gap correctly:
`UCataclysmStatPipeline` is a static calculator over numbers passed in, and
nothing fed it. It also suggested a fix — "the per-damage-type attributes the
designed buffs name" — and that suggestion is the part this decision rejects.

**The decision.** No per-damage-type attributes. A character's increases live in
a list of `FCataclysmStatModifier` on `UCataclysmAbilitySystemComponent`, and a
skill's own buff adds one and takes it away again. Each modifier is scoped by the
tags of the SKILL IN HAND, so "increased fire damage" is an increase requiring
`Element.Demonic`, which is the tag every Demonic row of the Weapon Skills sheet
already carries.

**Why not eight attributes.** Because increased fire damage is not one number per
character. It is one number for a skill tagged `Element.Demonic` and a different
number for one that is not, and a gameplay attribute is a single float per
character with no way to express that. Adding `IncreasedFireDamage` would work
for exactly this one case and then fail the first time something scoped an
increase by anything other than a damage type — by weapon class, by melee against
ranged, by skill type. The Affixes sheet already contains modifiers scoped that
way, and `Scope.MeleeOnly`, `Scope.RangedOnly`, `Scope.BasicOnly`,
`Scope.WhileMoving` and `Scope.WhileStationary` are registered gameplay tags in
`game/Config/Tags/CataclysmTags.ini`. Eight attributes would also become
sixty-four when the same scoping is wanted on defence.

`UCataclysmStatPipeline`'s own header already stated the reason, before this
change and about gear rather than buffs: "A character's area of effect has no
single value — it is one number for an area skill and another for a
single-target one — so a plain attribute read cannot express it." A buff is the
same problem arriving from a different direction.

**Why this is how the genre does it.** Path of Exile's modifiers are stats with
tag conditions rather than one stat per element: "increased fire damage" is a
stat whose applicability is decided by the tags of the skill being used, which is
why a support gem can change what a modifier applies to without any new stat
existing. Last Epoch's affixes carry the same shape, and its skill trees grant
modifiers scoped to the skill they sit on. Neither game has an "increased fire
damage" character attribute that everything reads. The three-bucket pipeline this
project already ported from those games is only usable this way; scoping by the
ability in hand is not an extra, it is what makes the buckets mean anything.

**What a skill buff may grant.** A `More` multiplier, unlike a gear affix. The
rule that ordinary gear may not is about a ROLLED modifier staying readable on a
drop — the reason an enchantment is worth an item slot an affix could have taken.
A skill buff is authored, in the same way a gem, a passive keystone and an
enchantment are authored, so it joins those three rather than the affix pool. No
designed skill grants one yet; the rule is stated and tested so the first one that
does not have to argue it.

**Where the magnitude comes from.** The Shape Params cell, like every other
number a skill template reads. Burning Wrath's is now
`Duration=10; Radius=15; Burn=1; IncreasePerBurning=4`, and
`tools/tests/test_buff_magnitudes.py` fails if that 4 stops matching the 4% in
the skill's own description. The scope comes from the row's Tags cell, so a self
buff written for another damage type scopes to its own element with no code
changing.

**One thing is priced early on purpose.** A patch of burning ground works out
what a tick is worth when it is created and keeps that figure. It now includes
the caster's modifiers at that moment. The alternative — reading the caster's
modifiers on every tick — would mean a patch stopped paying part way through when
the buff that created it expired, and the design says the ground burns for a
duration, not that it tracks its caster.

**What this does not do.** Martyr's Ember is still only a duration. "Store 40% of
all damage you take and spend it as bonus fire damage on your hits" needs a
damage-taken signal and a store that drains as it is spent, and it needs a
judgement the documents do not make: how much of the store one hit spends. Split
out as issue #192.

**Affects.** `All_Things_Cataclysm.xlsx`, Weapon Skills sheet, the Burning Wrath
row's Shape Params cell. Applied.

---

## 2026-08-04 — A minion deals 30% of its summoner's weapon damage, once a second, and that is one rule for all minions

**The question.** Issue #165: `ACataclysmMinion::DamagePercentOfSummoner` was 25
and its own comment said the number was a judgement rather than a design figure,
because nothing in `docs/All_Things_Cataclysm.xlsx` or `Cataclysm_GDD_v2.md`
stated what a summoned imp hit for. Summon Imp's description gives a lifetime, a
cap and an explosion radius and no damage at all. The issue asked two questions:
whether minion damage is a share of the summoner's weapon damage or a number of
its own, and whether the attack interval belongs to the minion or to the skill.

**RECONNAISSANCE CHANGED WHERE THE ANSWER GOES.** The issue asks for "a figure
the design states, in the same place the other skill numbers live", which is the
Shape Params column of the Weapon Skills sheet. Answering the second question
settles that this is the wrong place. If the interval belongs to the minion
rather than to the skill, then so does the damage, and neither is a per-skill
number. Summon Imp, Open the Rift and Cinder Swarm differ in how many minions
they make and how long those last — already `Count`, `MaxActive` and `Duration` —
and not in what one minion's swing is worth. A per-skill column would be the same
figure written three times, with three chances to disagree.

So the figures are stated as a rule in the "How a Skill Behaves: the Seven
Shapes" section of `Cataclysm_GDD_v2.md`, and the code keeps them as constants on
`ACataclysmMinion` rather than reading them from a skill row.

**The decision.**

| | |
|---|---|
| Damage per attack | 30% of the summoner's weapon damage |
| Attacks per second | 1 |

**Why a share of the summoner's weapon damage, and not damage of its own. The
genre is genuinely split on this, and the two answers are not interchangeable.**

- **Path of Exile gives minions damage entirely their own.** Its own rule is that
  if a modifier does not say "minion", it does not affect them: weapon damage,
  critical strike and life on the player do nothing for a minion. Minions scale
  from minion-specific passives, minion support gems, auras and a few uniques.
- **Diablo IV does the opposite.** Its Necromancer minions gain 30% of the
  player's weapon damage, and take their attack rate from the player's weapon
  rather than having one of their own.

Diablo IV's shape is the one taken, for a reason about this game rather than
about that one: **Path of Exile's route needs a whole separate family of
minion-only stats to scale, and this game has none.** There is no minion damage
affix, no minion support gem and no minion passive anywhere in the design. To
adopt it, all of that would have to be invented, for two skills in the vertical
slice. Against that, every other number in this design is already expressed as a
percent of weapon damage, so a share needs nothing new at all.

**Why 30 rather than the 25 that was there.** 25 was invented. 30 is Diablo IV's
own figure for the same shape, and a figure that survived contact with real
players is evidence in a way an invented one is not. The budget argument that
produced 25 still holds at 30: Summon Imp caps at three active, so three at 30%
attacking once a second is 90% of weapon damage per second, and an automatic
basic attack is 128% to 150% per second depending on weapon speed. A summoner is
still better off attacking than not.

`tools/tests/test_minion_damage.py` reads both figures out of the design document
and out of `CataclysmMinion.h` and fails if they disagree, and separately fails if
three minions would ever out-damage the slowest basic attack. Confirmed able to
fail: changing the design to 45% failed both of those and nothing else.

**Not taken from Diablo IV: the attack rate coming from the player's weapon.**
Diablo IV ties minion attack speed to the weapon's. This game states a flat one
per second instead, because the `AttackSpeed` attribute exists but nothing reads
it for the player's own attacks yet either, so tying minions to it would be
building on something that is not there. When the automatic basic attack starts
using weapon speed, the minion should follow it, and that is worth revisiting
then rather than guessing now.

**Sources.** The Path of Exile wiki on Minion and on Minion Damage Support, for
minions having their own damage and for the "if it does not say minion" rule; the
Diablo IV community's minion testing threads and the Diablo 4 wiki's "Damage with
Minions" page, for the 30% of weapon damage figure and for minions sharing the
weapon's attack speed.

**Affects:** `Cataclysm_GDD_v2.md`, which gains a paragraph and a small table in
its "How a Skill Behaves: the Seven Shapes" section. Applied. No workbook change,
for the reason given above.

---

## 2026-08-04 — Burning ground is a capsule, and a circle is the case where its two ends meet

**The question.** Issue #167: four skills say the path they took burns, and all
four left one patch at the far end instead. Emberhurl leaves "its flight path
burning for 4 seconds", Chain of Coals and Hellbrand are written the same way,
and Cinder Rush "leaves a trail of fire behind you". An enemy standing halfway
along took the passing hits and then stood on ground that was not burning. The
issue named two ways to fix it: a ground zone that can be a line, or a chain of
overlapping circles laid along the path.

**The decision. One zone, shaped as a capsule.** `ACataclysmGroundZone` gained a
far end. A round patch is the case where the far end equals the near end, and
both are swept by the same search, because `UCataclysmTargeting::IsInLine`
already treats a segment of no length as a circle at its start. There is no
branch between the two shapes and therefore no branch to get wrong.

**Why not a chain of circles.** It reuses the existing spawn call unchanged,
which is its only advantage. Against that: it spawns as many actors as the path
is long divided by the spacing, each with its own timer and its own sweep of the
world; the spacing is a number nobody can derive, because too wide leaves gaps in
the trail and too narrow multiplies the cost; and an enemy standing where two
circles overlap takes two ticks a second instead of one. That last one is not
hypothetical — Path of Exile's Flame Wall, the shipped ground effect nearest to
this, is a single line-shaped area, and its own stated rule is that an enemy can
only be damaged by one Flame Wall at a time. A shipped game that reached for the
overlap problem solved it by preventing the overlap.

**Which skills get which shape, and the rule that decides it.** For a projectile,
whether it pierces. A projectile that pierces travelled along a line and hit what
it passed; one that does not landed at a point. That rule is already how
`UCataclysmProjectileSkill::Land` chooses between a line search and a sphere
search, so the ground now follows the same rule as the damage rather than a
second, separate list of skill names. The Weapon Skills sheet agrees exactly:
Emberhurl, Chain of Coals and Hellbrand all carry `Pierce=99` and leave ground;
Blood Pyre and Magma Quake leave ground and carry no `Pierce` at all.

For a movement skill, the mode. A charge leaves a trail along its run; a leap
leaves a pool where it landed; a blink burns both of its two points and nothing
between them, which was already true and is unchanged.

**A separate fault found while doing this, and fixed in the same change.**
`ACataclysmGroundZone` had no components of any kind. An actor whose components
are all non-scene components gets no root component, and an actor with no root
component reports its location as the world origin however it was spawned. So
**every patch of burning ground in the project was sweeping around (0,0,0)**
rather than around where the skill left it. It went unnoticed because the only
test of it spawned the zone at the origin. This is the same fault that issue #163
found in `ACataclysmMinion`, from the same cause, and it is worth stating as a
general rule: in this project, an actor that needs a position needs a scene
component, and a test that places something at the origin cannot tell whether it
is there on purpose.

**Sources.** The Path of Exile wiki on Flame Wall, for a line-shaped ground
effect being a shape shipped games use, and for its rule that an enemy can only
be damaged by one Flame Wall at a time; `game/Data/WeaponSkills.csv`, generated
from the Weapon Skills sheet of `docs/All_Things_Cataclysm.xlsx`, for which
skills pierce and which leave ground.

**Affects:** no design document. The design already says these paths burn; this
is the implementation catching up to it.

---

## 2026-08-04 — The navigation mesh does update for destroyed geometry; the problem is that it takes four seconds

**What this corrects.** The entry below excluded the walkable surface from
destruction on the grounds that "the navigation mesh is reported not to update
reliably when Geometry Collections are destroyed". That came from community forum
reports. It is wrong for Unreal Engine 5.8. The exclusion still stands, but the
reason had to be replaced with the real one.

**How this was checked.** By reading the installed engine source at
`Engine/Source/Runtime/Experimental/GeometryCollectionEngine`, and by querying the
running editor for this project's own navigation settings. Not by measurement of
frame cost, which is still outstanding.

**FINDING 1: Geometry Collections are navigation-relevant in 5.8, and they do ask
for updates.** `UGeometryCollectionComponent` implements `INavRelevantInterface`,
sets `bHasCustomNavigableGeometry` to `Yes` in its constructor, implements
`DoCustomNavigableGeometryExport`, and its `bUpdateNavigationInTick` flag defaults
to true. The flat claim that navigation does not update is false.

**FINDING 2: the update is a blunt 256-frame poll, and that is the real problem.**
`UpdateNavigationDataIfNeeded` calls `UpdateNavigationData` only on frames where
`(GFrameCounter + NavmeshInvalidationTimeSliceIndex) & 0xff == 0`. The index is a
per-component offset taken from a global counter, so components stagger against
each other, but each one still updates once every 256 frames. At 60 frames per
second that is up to about 4.3 seconds. Epic's own comment in that function admits
the cause: "Need way of seeing if the collection is actually changing." There is no
event, so it polls.

Four seconds is not survivable for a floor. Enemies would walk over a hole that has
already been there for several seconds. `UpdateNavigationData` can be called
directly to skip the wait, but that puts a navigation rebuild inside combat, which
is the cost the exclusion exists to avoid.

**FINDING 3: it only runs in a game world.** The update is gated on
`MyWorld->IsGameWorld()`. Destruction tested in the editor viewport shows no
navigation update at all. That likely explains part of why the forum reports say it
does not work.

**FINDING 4: this project could not update navigation at runtime today anyway.**
`game/Config/DefaultEngine.ini` contains no navigation settings at all, and the
`RecastNavMesh-Default` actor in `L_Sandbox` reports `RuntimeGeneration = Static`.
`ARecastNavMesh::SupportsRuntimeGeneration` returns false for Static, so the
generator is disabled outright. Any future work that needs runtime navigation
changes has to change this first, and it is a global cost, not a local one.

**FINDING 5, useful and free: small debris is already excluded from navigation.**
The console variable `p.GeometryCollectionNavigationSizeThreshold` defaults to 20
centimetres, measured as the diagonal of a leaf node's bounds, and pieces below it
are not exported for navigation. Rubble does not pollute the navigation mesh
without anyone configuring anything.

**FINDING 6, a packaging trap worth knowing before it costs a day.** If a Geometry
Collection asset has `bStripOnCook` set, its data is gone at cook time and there is
nothing left to export for navigation. The engine logs a message telling you to use
`bUseRootProxyForNavigation` instead. This works in the editor and silently
degrades in a packaged build, which is the worst shape a defect can have.

**DECISION: the walkable surface stays excluded, and the design document's stated
reason is corrected.** The reason is now latency and cost, both specific: a
256-frame update cycle, an explicit rebuild landing inside combat if that cycle is
bypassed, and a project-wide switch from static to dynamic navigation generation.

**What is still unmeasured.** Every frame-cost figure remains unverified. It cannot
be measured yet for a reason that is worth recording: the project contains no
Geometry Collection assets and no static meshes of its own. The entire `/Game`
folder is maps, data tables and input assets. There is nothing to fracture and no
representative dungeon room to fill. Those measurements are blocked on the project
having art content, not on tooling.

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change. The "Why the walkable
surface is excluded" paragraph in the Destructible Environment subsection of
section VIII is replaced, because its stated reason was factually wrong about the
engine.

---

## 2026-08-04 — Enemy behaviour in C++ rather than a behaviour tree, and Madness as an attitude

**The question.** Issue #163: nothing in the project except the player had a
controller. A summoned imp stood where it was put for its whole twenty seconds,
a monster stood where it was spawned, and the Madness debuff — "the enemy
attacks anything nearby, friend or foe, for 3 seconds" — granted a gameplay tag
that nothing read. Two questions had to be settled before any of it could be
built: what a monster's decision-making is written in, and what Madness actually
changes.

**FIRST DECISION: the behaviour is C++, not a Behaviour Tree.** Unreal's usual
answer is a Behaviour Tree asset driven by a Blackboard asset, run by a
`AAIController` that owns a `UBehaviorTreeComponent`. This project uses a plain
`AAIController` subclass, `ACataclysmEnemyController`, with three states — idle,
chase, attack — expressed in about twenty lines.

The reason is the same one that put the skill behaviours in C++ rather than in
Blueprints. A Behaviour Tree and its Blackboard are binary `.uasset` files.
Every other rule in this project is text that a pull request shows a diff of,
and every other behaviour is covered by an automation test that runs headless
with no editor. Three states as a tree would be assets nobody can review and
nothing can test, to say what twenty lines say directly.

This is a decision with a stated expiry. A Behaviour Tree earns its cost when the
logic is deep enough that a designer needs to change it without a programmer, and
when the same subtrees are shared between many different agents. Issue #39's
seven Demonic enemies — a swarming imp, a ranged caster, a charger, a stomping
tank, a stationary turret, a mini-boss with a positional weakness, and a
multi-phase boss — are the point at which that should be reconsidered rather than
assumed. Recording it here so that the next person knows it was a choice.

**SECOND DECISION: Madness is an attitude override, not a change of side.**
`UCataclysmTeams::AttitudeBetween` returns Hostile whenever either actor carries
the `Status.Madness` tag, before any other rule including ownership. Three
consequences follow, and all three are what the design text says:

- A maddened monster attacks other monsters, because they are no longer friendly
  to it.
- Its neighbours attack it back, because the rule is read symmetrically. Without
  that, a maddened monster hits things that stand there and take it.
- It would attack its own summons, because a thing it owns is a friend and
  "friend or foe" does not except them.

The nearest shipped mechanic in the genre is Path of Exile's Conversion Trap,
which moves a monster onto the caster's side for a duration and makes it fight
that side's enemies. That is deliberately **not** what Madness is. Conversion
switches a side; Madness removes one.

**Numbers that are judgements, not design figures.** The design states none of
these. They are labelled as judgements in the code and are expected to change:

| Number | Value | Why |
|---|---|---|
| A monster's reach | 200 cm | A little over twice its capsule radius, which is about where two placeholder cylinders look like they are touching. |
| A monster's notice radius | 1500 cm | The same distance Subjugate reaches, which is the longest range the designed Demonic skills use. |
| Seconds between a monster's attacks | 1.5 | Slow enough to walk out of. |
| An imp's notice radius | 1500 cm | Far enough that an imp summoned across a room goes to a fight rather than standing still, which was the whole of the report. |
| A training dummy's attack damage | 20 | Five of them at 20 every 1.5 seconds is about 67 a second against a character starting at 100 health, so standing still in the ring is fatal and walking out of it is not. |

Per-monster rather than one constant for all monsters, which is the shape Diablo
II uses: its `monstats.txt` gives every monster type its own vision distance in
an `aidist` column. A charging Hellhound and a stationary Corrupted Sentinel
cannot share one number.

**What was deliberately left out, and should be built when it is needed.**

- **No leash.** A monster that has noticed the player follows for as long as the
  player stays inside its notice radius, rather than giving up and returning to
  where it started. Path of Exile monsters do break off and return. It is a real
  shape and it is not needed to make an imp chase what it is attacking.
- **No target memory.** The nearest hostile is re-chosen every quarter second, so
  two equally distant targets can be swapped between.
- **No telegraphs.** Issue #39 lists wind-up, telegraph shape and cancel rules as
  core rather than polish, and none of that exists.

**Sources.** The Unreal Engine 5.8 source of `AIController.h` and
`GenericTeamAgentInterface.h` for the controller and attitude machinery; the Path
of Exile wiki on Conversion Trap for how a shipped action role-playing game moves
a monster between sides; the Diablo II modding community's documentation of
`monstats.txt` for `aidist` being a per-monster vision distance; a 2012 Path of
Exile forum thread on monster AI for monsters breaking off and returning when the
player gets far enough, which is the evidence that a leash is a real shape rather
than an invented one.

**Affects:** no design document yet. Enemy behaviour beyond one-sentence
descriptions is issue #29, which records that enemy design exists only for the
Demonic vertical slice.

---

## 2026-08-04 — The environment reacts through physics, and nothing about a crater is authored

**This supersedes the fourth decision in the entry below.** That entry recommended
pre-authored crater assets: a decal, a prepared depression mesh and debris, spawned
on impact. That was an answer to the wrong question.

**What the question actually was.** The operator's example was a meteor leaving a
crater. It was read as a request for craters, and the resulting recommendation was
to hand-make them. The operator corrected it: the meteor was an example, and what
is wanted is an environment that reacts through the physics system generally, so
that nothing has to be custom-made per event. They suggested Chaos already does
this and had not researched it. They were right.

**DECISION: Chaos Destruction, applied generically, with the walkable surface
excluded.**

**Why this answers it and the earlier recommendation did not.** A Geometry
Collection is a mesh fractured once, in the editor. After that, damage from any
source breaks it according to physics and its own material: projectile hits,
radial explosions and melee all go through the same path. Chaos Fields apply force
and strain over a volume at runtime, and the engine ships a prebuilt field,
`FS_MasterField`, that applies external strain to fracture a Geometry Collection.
Nanite can be enabled on Geometry Collections.

The authoring is therefore **per asset, once** — fracture the wall — and never per
event. A new skill, enemy attack or hazard needs no destruction work at all; it
deals damage and things break. That is exactly the property the operator asked for,
and it is the property the pre-authored crater recommendation destroyed.

One implementation note worth recording because it has already changed once:
applying damage to Geometry Collections through the older Apply Damage path is
deprecated. Radial impulse with strain is the current route.

**The cost model is the reason this is affordable.** Cost scales with pieces
actively simulating, not with pieces that exist. A level can hold a large number of
fracturable assets cheaply so long as few are moving at any moment. The controls
are a cap on simultaneously simulating pieces, and putting debris to rest or
removing it once it settles.

Numbers from one studio's published write-up, **not from Epic and not measured on
this project**: a level with 50 Geometry Collections of 100 pieces each running
well with only a handful simulating; a global cap of 200 simultaneous active
bodies; roughly 4ms of physics cost for roughly 2 seconds before debris settles;
and authoring a full building interior taking about 4 hours by hand or under 45
minutes with automation. Treat all of those as the right order of magnitude to plan
against and none of them as verified here.

**THE CONSTRAINT THAT DECIDES THE BOUNDARY: the navigation mesh does not update
reliably when Geometry Collections are destroyed.** This is a reported problem, not
a theoretical one: destroying Geometry Collections leaves the dynamic navigation
mesh stale, and enemies end up unable to path across ground that looks passable.
Working around it means custom navigation-relevance work per destructible asset.

So the walkable surface is excluded from destruction. Walls, pillars, statues,
railings, furniture, fixtures, ceiling sections and decorative layers on top of the
floor are all fully destructible and fully reactive. The ground characters stand on
keeps its shape. Scorching, cracking and rubble appear on it; holes do not.

This costs the player almost nothing they would notice, because everything they hit
still breaks. It removes the entire class of pathfinding failure, and it is the
same boundary that ruled out deformable terrain, arrived at from a different
direction.

**What is still out.** Actual landscape terrain deformation. Chaos does not deform a
landscape heightmap; a landscape is not a Geometry Collection. Nothing in this
decision changes that.

**Sources.** Chaos Fields user guide, Unreal Engine 5.8 documentation. Chaos
Destruction overview, Epic documentation. Nanite virtualized geometry, Unreal 5.8
documentation. Performance and authoring figures: StraySpark, "Chaos Destruction in
UE5: Building Destructible Environments That Don't Tank Your Frame Rate". Navigation
mesh staleness with destroyed Geometry Collections: Epic Developer Community forum
reports, "GeometryCollection, updating the NavMesh" and "Does NavMesh work with
Chaos physics?".

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change. The Destructible
Environment subsection of section VIII is rewritten: the split between "objects that
break" and "surfaces that are marked" is removed, because that split was the
too-literal reading, and replaced with one physics-driven rule plus the walkable
surface exclusion.

**Still open.** None of the performance figures above have been measured on this
project. The technical spike is tracked separately and has been rewritten to ask
this question instead of the crater question it originally asked.

---

## 2026-08-04 — Sides: two teams, three attitudes, and no side means hostile

**The question.** Issue #162: nothing in the project had any concept of which
side an actor was on. `UCataclysmTargeting::IsHostileTo` decided what a skill
could hit by asking three things — is it valid and not the caster, does it have
an ability system component, and does neither actor own the other. That made
every enemy a legal target for every other enemy's skills, made a second player
in a co-operative session a legal target for the first, and gave no way at all to
find an ally, which is why the ally half of Conflagration and Blood and Iron
("allies within it deal 8% increased fire damage") could not be built.

**The decision.** Use the engine's own `IGenericTeamAgentInterface` and
`FGenericTeamId`, with two sides: `Players` (0) and `Monsters` (1). A character
is born onto a side in its constructor. A summon is given its summoner's side
when it is spawned. Anything else — a patch of burning ground, a projectile —
takes the side of whatever owns it, found by walking the owner chain.

**Why the engine's mechanism and not a new one.** `IGenericTeamAgentInterface`
lives in `AIModule`, which this project already depends on for click-to-move, so
it costs no new dependency. Using it means the AI perception component, the
`AAIController` base class and the environment query system all understand this
project's sides without being told. That matters immediately: issue #163, enemy
and minion behaviour, is the next thing built on top of this.

**Why not Lyra's shape.** Epic's own Lyra sample does *not* use the generic
interface. It declares `ILyraTeamAgentInterface` and a `ULyraTeamSubsystem` that
owns team assignment through `ChangeTeamForActor`. That machinery pays for itself
when a game mode assigns teams at runtime, drives team-based spawn points and
scores per team. None of that exists here. There are two sides, they are fixed,
and a character is born onto one; a subsystem would be a layer with nothing in
it. Lyra also collapses the comparison to same-team or different-team, and this
project keeps all three of `ETeamAttitude`'s values so that a genuinely neutral
actor — a shrine, a destructible prop, a town guard — can be added later without
every caller changing shape.

**Having no side means hostile, not neutral.** This is a choice of failure mode
and it is the opposite of the engine's own default attitude solver, which treats
two actors that both have no team as equal and therefore friendly. An enemy class
that forgets to set its side is still something the player can kill. Treating no
side as neutral instead would make it silently immune to every skill in the game,
which is far harder to notice than the reverse. `UCataclysmTeams` therefore does
not call `FGenericTeamId::GetAttitude`.

**Summons, allied players and neutral actors are not three cases.** They are one
question — which side — asked of three kinds of thing:

| Kind | How it gets a side |
|---|---|
| A player character | Its constructor. `Players`. |
| An enemy character | Its constructor. `Monsters`. |
| A summoned minion | Copied from its summoner at spawn. Not a side of its own. |
| Burning ground, and anything else a skill leaves behind | The owner chain. |
| Anything else | No side, and therefore hostile to everything. |

Ownership is checked *before* the side numbers are compared, not instead of them.
In the ordinary case the two agree. Ownership is what still holds when they do
not: anything a character puts into the world is on that character's side even if
nothing gave it a team.

**What Madness does is deliberately not part of this.** The design says a
maddened enemy "attacks anything nearby, friend or foe". That is a change of
attitude for a tagged actor, not a change of side, and it belongs with the enemy
behaviour that would let a maddened enemy act on it. Issue #163. Path of Exile's
Conversion Trap is the nearest shipped shape to the *other* half of this — it
moves a monster onto the player's side for a duration — and it is not what
Madness is: Madness removes a side rather than switching one.

**Sources.** Epic's Lyra sample game, via X157's Lyra Team System notes, for the
`ULyraTeamSubsystem` and `ILyraTeamAgentInterface` shape and for friendly fire
being filtered at the ability level; the Unreal Engine 5.8 source of
`GenericTeamAgentInterface.h` and `AIInterfaces.cpp` for `FGenericTeamId`,
`ETeamAttitude`'s three values, and the default attitude solver being
`A != B ? Hostile : Friendly`; the Path of Exile wiki on Conversion Trap for how
a shipped action role-playing game moves a monster between sides temporarily.

**Affects:** no design document. This is an implementation decision that the
design documents do not describe and do not need to; `Cataclysm_GDD_v2.md` names
allies in its aura descriptions and says nothing about how they are identified.

---

## 2026-08-04 — Craters are seen and not walked around, and a party is rescued by whoever gets out

**The question.** Two follow-ups from the operator on the same day. First, that
authored breakables alone are not enough: dropping a meteor on something should
leave a crater. Second, three answers on how over-corruption behaves in a
co-operative party, which turned out to settle the general rule for dying in
co-op as well.

**FOURTH DECISION: craters are visual, and never change navigation.**
**(SUPERSEDED the same day by the entry above. The conclusion that destruction must
not change the walkable surface survives. The recommendation of pre-authored crater
assets does not: it answered a request for craters, when the request was for an
environment that reacts through physics without per-event authoring.)**

The distinction that decides this is not how a crater is made. It is whether the
crater changes where anything can walk. A crater that only changes what the
surface looks like costs a fixed, known amount and never touches pathfinding. A
crater that changes collision forces the navigation mesh to rebuild while enemies
are pathing across it, which is the exact cost that ruled out deformable terrain
in the decision below. Same objection, same answer.

So: impacts leave visible depressions with scorching, debris and dust, and every
character walks across them exactly as before.

Three routes were looked at for producing the visual, and the differences matter
enough to record.

  - **Runtime Virtual Texture deformation.** The standard shipped technique for
    snow, sand and mud. The displacement exists only on the GPU, so collision does
    not follow it, which for this decision is a feature rather than a limitation.
    Two cautions: reading the deformation back to the CPU for collision requires
    custom global shaders and is not a small job, and there is a reported defect
    where Runtime Virtual Texture output breaks when a landscape material uses
    displacement. That report is against 5.6 and has not been checked on 5.8.
  - **Geometry Script mesh boolean at runtime.** Produces real geometry and real
    collision. Rejected on cost growth rather than cost: each boolean builds an
    axis-aligned bounding box tree for both meshes and runs pairwise triangle
    intersection, and successive booleans accumulate triangles, so the hundredth
    crater in a dungeon costs far more than the first. That is the wrong shape for
    a game where a floor is fought over for a long time.
  - **Pre-authored crater assets.** An impact spawns a decal, a shallow prepared
    depression mesh and Chaos debris. Fixed cost per crater, no accumulation, no
    navigation change. This is the recommendation.

None of this has been tested in this project. A technical spike is filed
separately rather than assumed.

**FIFTH DECISION: dying in co-op moves a player to spectator, and one survivor
rescues everyone.**

The operator's rule, adopted as stated: a player who dies during a dungeon run
becomes a spectator for the rest of it, with no penalty applied at that moment.
If every player is dead, the run has failed and the death penalty applies. If at
least one player leaves alive, every dead team-mate is recovered and nobody pays
anything.

This is a general co-op rule rather than an over-corruption rule, and it is the
first real content behind the single line "Multiplayer co-op support" in the
Phase 2 roadmap. It makes a surviving player's escape valuable to the whole party
and turns a single death into a setback instead of an ending.

**The death penalty is paid once for the party, not once per player.** The empire
and its day clock are shared in co-op, so charging five days four times would make
a four-player wipe cost twenty days against a shared clock — a far larger penalty
than the same wipe costs a solo player, and a penalty that grows with the number
of friends you play with. Raised as an inference from the operator's wording and
confirmed by them the same day.

**SIXTH DECISION: every marked player produces a double, and all of them are
present for the whole party.**

Three marked players in a party of four means three doubles, fought by all four.
A player who managed their residue still fights their team-mates' doubles.

The operator's reasoning for accepting that this lets a careless player rely on
the group: enemy health and damage already scale with party size, so a double
copied from an over-equipped character arrives scaled for four players. The player
who ignored residue management is handing the party a party-scaled copy of their
own build, and the consequences sit with them.

Four marked players in a four-player party produces four doubles, each scaled for
four players. That was raised as a tuning risk, on the grounds that it is a larger
multiplication than any other encounter in the design and may not be survivable.
**The operator confirmed it as intended**, and the reasoning behind that is worth
more than the specific case.

**SEVENTH DECISION, and the one most likely to be argued with later: party play is
held to the same standard as solo play.**

Co-operative play in this genre is commonly easier than solo play. Scaling is
applied loosely, groups outpace it, and the result is that a party stops feeling
consequences a solo player still feels. That is the normal outcome, not an unusual
failure, and it is what this game is deliberately not doing.

The rule that follows: a consequence a solo player would feel is a consequence a
party feels too. Party scaling exists to keep that true, not to make group play
comfortable. A player who chooses to ignore residue management gets to make that
choice once, whether they are alone or with three friends.

This is a principle rather than a number, and it is recorded because it will decide
arguments that have not happened yet. Any future proposal to soften a penalty
"because it is unfair in a party" runs into it. The four-doubles case is simply the
first place it came up.

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change. Section VII gains an
"In a party" paragraph under Worn Residue and Consumption. Section VIII gains a
"Co-operative Play" subsection carrying both the once-per-party penalty rule and
the same-standard principle, and its Destructible Environment subsection is
rewritten to cover impact craters.

**Nothing from this entry is open.** Both items originally flagged — whether the
death penalty is paid once or per player, and whether four simultaneous
party-scaled doubles is intended — were confirmed by the operator on the same day
and are written above as decisions rather than questions.

---

## 2026-08-04 — Destruction is authored not volumetric, and residue can consume a character who ignores it

**The question.** Two design proposals from the operator, raised together. First,
that the world should be destructible, possibly using a voxel system. Second,
that Cataclysmic Residue should have a threshold past which the character is
consumed by the Cataclysm, and that consumed characters should return as an enemy
in a dungeon modifier, dropping their own equipment scaled to the tier of the
fight.

**FIRST DECISION: destruction is authored breakables, using the engine's own
Chaos Destruction. No voxel terrain.**

The genre is unanimous, and that is the strongest evidence available. Diablo 2, 3
and 4, Path of Exile 1 and 2, Last Epoch, Grim Dawn and Torchlight all use fixed
level geometry with authored breakable props. Not one of them lets the player
reshape terrain. Three reasons hold them all in the same place:

  - Combat in this genre is built on stable geometry. Chokepoints, line of sight
    and kiting lanes stop being designable if a wall can be removed.
  - Enemy pathfinding needs a navigation mesh. Rebuilding one continuously, for a
    procedurally generated dungeon, at the enemy counts this genre uses, is the
    expensive part, and it scales with exactly the things this game wants more of.
  - Loot density and encounter pacing assume the player moves through a level
    rather than tunnelling past it.

Games that do carry full terrain destruction alongside combat — Deep Rock
Galactic, Teardown, 7 Days to Die, Enshrouded, Astroneer — are ones where digging
is the primary way the player moves. None is a top-down action role-playing game.
The shape is proven, but not for this kind of game.

On technology: Chaos Destruction ships inside Unreal Engine 5.8 at no cost, with
no third-party licence and no dependency risk, and it covers every case the
design actually needs. Voxel Plugin (voxelplugin.com) states on its product page
that it supports dynamic navigation mesh generation. That claim has not been
tested here, and a product page is not evidence it holds at this project's enemy
counts on the development machine, which has 8GB of video memory and is short on
disk. Voxel data is heavy on both.

The timing argument is the one that decided it. Procedural dungeon generation
(#40), loot generation (#44) and the heads-up display (#49) are all still open.
Choosing a foundational terrain technology before the combat loop exists commits
the performance budget to a need that has not been demonstrated.

**SECOND DECISION: residue stays a pure cost, and the threshold is a fight rather
than an instant death.**

The first proposal reviewed was to make high residue grant power, so that
approaching the threshold would be a gamble in the way Path of Exile's Vaal Orb
corruption is a gamble. The operator rejected this, and the reasoning is sound:
residue is a cost that is manageable through crafting technique, and it becomes
dangerous only if the player ignores the management tools that already exist. It
is a penalty for negligence, not a temptation. That is a different design from a
risk-and-reward gamble and it does not need the same justification.

What it does need is a warning, which is why the threshold cannot be crossed
without a confirmation that states the resulting total and the consequence.

The consequence itself changed during the conversation. The operator ruled out
deleting the character while the run continues, and the reason is correct: a run
is played at a fixed tier, so replacing a tier 5 character with a fresh one leaves
the player at a tier they cannot survive. That is a loss presented as a
continuation.

Instant permanent death was also rejected, as too extreme for a cost that is
otherwise about gold and days.

What was adopted is a fight. Crossing the threshold marks the character; a
corrupted double spawns on the next dungeon floor and hunts them. Winning clears
residue to zero and the run continues. Losing consumes the character and ends the
run, with empire progress kept, which is the rule the Last Stand already uses.

Three things make this the right shape rather than a compromise:

  - It gives the player agency at the moment the penalty lands, which an
    accumulating counter otherwise does not.
  - It reuses the corrupted-character enemy from the dungeon modifier below. One
    system, two uses, and the more expensive of the two pays for both.
  - It invents no new category of death. A run already ends on death in the Last
    Stand.

**THIRD DECISION: consumed characters go into a shared table, drawn at random by a
dungeon modifier, and scaled to the tier they appear at.**

This has direct precedent. Rogue Exiles in Path of Exile are randomly placed
enemies that use player skills and player equipment and drop one item from every
equipment slot on death; they have run in a live game for years. Diablo 3's
Nemesis system sent the monster that killed a player into that player's friends'
games. Dark Souls invaders are the same idea in a third form.

Scaling the rebuilt character to the tier of the dungeon it appears in, rather
than the tier it was consumed at, closes the obvious exploit: losing a high-tier
character on purpose and farming its equipment where the fight is trivial. The
scaling must cover level, item level, affix tiers and residue, not only health and
damage.

The table is shared across the whole player base, which turns a rare event into a
usable content source. The game requires a network connection by default.
Co-operative multiplayer is already a Phase 2 item in the roadmap in section XV of
`Cataclysm_GDD_v2.md`, so this is not a new commitment, only a use of one already
made.

**The two halves separate cleanly, and that decides the build order.** The double a
player fights when they cross their own threshold is built from their own character
on their own machine. It needs no table, no service and no connection. Only the
dungeon modifier, which draws a character somebody else lost, needs the shared
table.

So the consumption fight can be built and shipped before any backend exists, and
the modifier added once one does. This removes what would otherwise be a hard
dependency between a combat feature and a service that does not exist yet. If an
offline mode is ever offered, it keeps the consumption fight and drops the
modifier.

The server-side requirements the shared table brings are recorded as their own
issue rather than as design, because they are engineering constraints rather than
design decisions.

**A patent boundary, recorded so it is not rediscovered later.** Warner Bros holds
US patent 10,926,179, "Nemesis characters, nemesis forts, social vendettas and
followers in computer games", granted February 2021 and in force until 2035. Its
claims cover non-player character hierarchies whose members have individual traits
and a rank that evolves from events, together with sharing those hierarchies
between separate players' games.

The design here appears to fall outside those claims: there is no hierarchy, no
rank that rises, no memory of previous encounters, and no outcome that flows back
to the original player. What it does share is a read-only snapshot. The distance
is real but it is not large, and the design should not grow toward a roster of
corrupted characters that remember the player, gain rank by killing them, and are
sent into specific other players' games. This is not legal advice and has not been
reviewed by a lawyer.

**Sources.** Rogue Exiles: the Path of Exile 2 wiki at
`pathofexile2.wiki.fextralife.com/Rogue+Exiles` and the Path of Exile wiki at
`pathofexile.fandom.com/wiki/Rogue_exile`. Diablo 3 Nemesis:
`diablo.fandom.com/wiki/Nemesis_Kills`. Patent:
`patents.google.com/patent/US10926179B2/en`. Vaal Orb corruption, reviewed and then
not used: `maxroll.gg/poe/resources/corruption`. Voxel Plugin: `voxelplugin.com`.

**Affects:** `Cataclysm_GDD_v2.md`, applied in this change. Section VII gains a
"Worn Residue and Consumption" subsection, and its opening paragraph is corrected,
because it previously stated the Forge never destroys anything, which the
consumption rule contradicts. Section VIII gains "The Corrupted (Dungeon
Modifier)" and "Destructible Environment".

**Still open.** The consumption threshold number is not set and needs tuning
against real play. The weight of The Corrupted modifier is not set, because no
dungeon modifier list exists in either `Cataclysm_GDD_v2.md` or
`All_Things_Cataclysm.xlsx` yet.

---

## 2026-08-04 — How a skill behaves: a shape column, a parameter bag, and seven shared templates

**The question.** Issue #37: build the sixteen Demonic skill behaviours. The
issue asks for shared templates rather than sixteen one-off implementations,
because the full weapon-and-damage-type matrix is 398 rows and bespoke work on
the first sixteen would make the other 382 unaffordable. It also asks one
question first: the Weapon Skills sheet carries no column naming which shape a
skill uses, nor its radius or duration, and whether those become columns had to
be settled before any code was written.

**RECONNAISSANCE CHANGED WHAT THE WORK WAS, AND BY MORE THAN THE LAST TWO
TIMES.** The issue reads as though the machinery is finished and sixteen
`ActivateAbility` bodies are missing. It is not. **Nothing in the project could
find a target or damage one.** A search of `game/Source/` for a sphere overlap,
a line trace or a gameplay effect applied to another actor returned nothing at
all. `UCataclysmDamageCalculation::Resolve` — the whole eight-step mitigation
order — was reachable from exactly one place, the defender's own attribute set
when its `Damage` meta attribute changed, and nothing in the project ever
changed it. There was also no attribute holding a character's weapon damage, so
every "250% weapon damage" in the design was a percentage of nothing.

So the first part of this work is not sixteen skills. It is the layer all
sixteen stand on, and building it once is what makes the templates shared.

**FIRST DECISION: the shape becomes a column, and it is NOT read off the Tags
column.**

The temptation is real, because the Tags column already carries
`Type.Projectile`, `Type.AOE.PointBlank`, `Type.Strike` and the rest. Two
reasons not to, and the second is the one that would have caused a bug:

  - **The tags do not decide it.** Molten Cleave carries `Type.AOE.PointBlank`,
    `Type.Strike` AND `Type.AOE.Persistent` at once, and nothing says which is
    the primary behaviour. Infernal Plunge is a leap and carries no tag saying
    so. Cinder Rush's only clue is `Keyword.Charge`, in a different namespace.
  - **The tags already have a job.** `UCataclysmStatPipeline::ModifierApplies`
    scopes every gear increase by the tags of the skill in hand — that is what
    lets increased area of effect apply to area skills and nothing else.
    Dispatching on them too would mean adding a tag to make a skill's shape work
    silently changed which gear applied to it.

Path of Exile draws exactly this line in its shipped data. Its `gems.json`
carries `types` — the internal list whose stated purpose is deciding which
support gems may support a skill — separately from the `ActiveSkills.dat`
identifier that names the skill's behaviour, and separately again from the
player-facing gem `tags`. Three lists, three jobs.

**SECOND DECISION: the numbers are a `Key=Value` bag, not a column each.**

Path of Exile stores per-skill numbers as named stat entries (`stats`: an array
of id and value pairs) rather than as columns, and the reason transfers directly:
different shapes read different numbers. The union across the seven shapes here
is sixteen parameters, of which a typical row fills three. Sixteen columns of
which thirteen are blank on every row is worse to read and worse to edit than one
cell saying `Radius=4; Angle=120; Burn=1`.

The cost of a bag is that a misspelling reads as nothing. That is paid for by
refusing it at generation time: `tools/generate_datatables.py` rejects an unknown
shape, a parameter the shape does not read, a repeated parameter, a non-numeric
value, and an `Effect` naming a status effect that does not exist. **This matters
more here than anywhere else, because a radius of zero produces a skill that
activates, spends mana, starts its cooldown and hits nothing — which is
indistinguishable from a skill somebody forgot to finish.** It is the same shape
of failure as issue #155's cooldown of zero, which went unnoticed across all 77
designed skills.

**THIRD DECISION: seven shapes, and the burning ground is not one of them.**

| Shape | The slice skills it runs |
|---|---|
| Strike | Molten Cleave, Searing Hook, Pyroclasm |
| Projectile | Emberhurl, Blood Pyre, Infernal Lance |
| SelfBuff | Burning Wrath, Martyr's Ember |
| Movement | Infernal Plunge, Cinder Rush, Emberstep |
| Summon | Summon Imp, Open the Rift |
| Aura | Conflagration, Living Pyre |
| Debuff | Subjugate |

The list is not invented. Path of Exile's own `active_skill_types` list, which
ships in its data files, carves the same joints: `Projectile`, `Melee`,
`MeleeSingleTarget`, `Movement`, `Blink`, `Travel`, `Aura`, `Buff`, `Minion`,
`CreatesMinion`, `Channel` and `AppliesCurse` are separate entries in it. Seven
is that list collapsed to what the sixteen designed skills actually need.

**Issue #37 listed the persistent ground zone as a seventh shape. It is a
rider.** Eight of the sixteen leave one behind on top of whatever else they are:
Molten Cleave is a strike that also drags slag, Emberhurl is a projectile that
also leaves its path burning. The issue's own table hints at it — every other
entry names skills and that one says "used by most of the above". So any shape
may carry `GroundRadius` and `GroundDuration`, and four other riders work the
same way.

**FOURTH DECISION: burn lasts 4 seconds and is worth 20% of the hit that caused
it.**

Burn had neither number anywhere. Fifteen of the sixteen designed Demonic skills
apply it, the design document says "every Demonic skill applies burn", and
`game/Data/StatusEffects.csv` gave it no duration and no damage — so every one of
those skills applied an effect made of nothing. Necrosis states 5 seconds and
Void Splinter states 1% of current health per second over 4 seconds; burn stated
neither.

**The duration is settled by research. The damage share is not.** Path of Exile
and Path of Exile 2 both give Ignite a base duration of 4 seconds. They disagree
completely on what it is worth: Path of Exile 1 deals 50% of the hit per second
for those 4 seconds, which is 200% of the hit in total, and Path of Exile 2 gives
the ignite 20% of the hit spread across the same 4 seconds. 20% is taken here,
and it is a judgement rather than a derivation. The reason for the conservative
end: burn rides on *every* Demonic skill, so at Path of Exile 1's ratio the rider
would be twice the hit it rides on and the skills themselves would stop
mattering. Expect it to move once the game is playable.

Both numbers live in columns B and C of the DoTs sheet, so they are a workbook
edit rather than a rebuild. An effect stating zero for either applies nothing at
all, which is the honest answer for an effect nobody has designed yet.

**TWO BUGS IN WHAT SHIPPED LAST SESSION, both found by building on it, and the
second one hid the first.** Issue #155 built `CheckCost`, `ApplyCost`,
`CheckCooldown` and `ApplyCooldown` on `UCataclysmGameplayAbility`, and issue #36
built the slot table they read from. Neither worked in play:

  - **Nothing called `CommitAbility`**, which is what the engine runs the two
    `Apply` halves from. So mana was checked and never spent, and cooldowns were
    checked and never started.
  - **`GiveAbilityInSlot` never set the ability's `Slot`.** It added the slot
    *tag*, which is what lets a key press find the ability, and left the slot
    *property* at `None` — which is what the ability reads to find its own
    cooldown, mana cost and damage multiplier. All three came back zero.

Nothing reported either, because the only ability that existed was the
placeholder that ends immediately: it spends nothing and waits for nothing, so
both faults were invisible. Both are fixed, and
`Cataclysm.Skills.UsingASkillSpendsManaAndStartsItsCooldown` fails if either is
reverted — confirmed by reverting each and watching it fail.

**WHAT IS REAL AND WHAT IS NOT.** Said plainly, because "the sixteen skills are
implemented" would overstate it. Real: target finding, damage through the full
mitigation order, burn, burning ground that re-tests who is standing in it,
knockback, minion summoning and its cap and the explosion when the cap is
exceeded, movement, the aura's drain and its switching off when the mana runs
out, and the doubling of Madness against a burning target. Not real: the
magnitude of any buff or debuff (#166), minions moving and Madness changing who
an enemy attacks (#163), a projectile occupying space while it flies (#164), a
burning trail following a path rather than sitting at its end (#167), and what a
summoned minion should hit for (#165). None of those is a skill that was skipped;
each is a system underneath that does not exist yet.

**Sources.** The RePoE export of Path of Exile's own data files, for
`gems.json`'s separation of `types`, `tags` and the active skill identifier, for
per-skill numbers being named stats rather than columns, and for the
`active_skill_types` list; the Path of Exile wiki on Ignite for the 4 second base
duration and the 50% per second figure; the Path of Exile 2 wiki on Ignite for
the 20% figure.

**What the research does not settle.** Which seven shapes this game needs, as
opposed to which shapes exist in the genre — that is a judgement about these
sixteen skills. And every number in the parameter cells, which were read off the
written descriptions where the description states one and chosen where it does
not.

**Affects:** `Cataclysm_GDD_v2.md`, which gains a "How a Skill Behaves: the Seven
Shapes" subsection in section V. `All_Things_Cataclysm.xlsx`, whose Weapon Skills
sheet gains Shape and Shape Params columns and whose DoTs sheet gains a duration
and a share of the hit. **Applied.**

---

## 2026-08-04 — The remaining 35 Demonic skills, completing all ten of its weapon types

**The question.** Issue #62 designed sixteen Demonic skills for the vertical
slice's three weapon types and left the other seven undesigned: Sword,
Greatsword, Dagger, Axe, Wand, Whip and Warhammer, five slots each.

**All 51 Demonic rows are now designed.** The 35 new ones follow the sixteen
rather than reopening anything: every one applies burn, none counts stacks, and
none is named after a class.

**Each weapon keeps the character its speed and its damage sub-type give it.**
The Dagger is the fastest weapon in the game at 1.50 attacks a second, so its
Ultimate strikes four times a second for four seconds and its Movement is a
blink. The Warhammer is the slowest at 1.20, so its Heavy knocks back and its
Ultimate lands one enormous blow a second. The Whip's Heavy reaches five metres,
which is further than any other one-handed heavy blow, because reach is what a
whip is for. The Wand is the only one-handed caster, so its Special summons and
its Support is the only Demonic skill that applies Shred.

**Three rules were followed that the tests now hold.** Every designed row names a
shape and states at least one number, or it is a skill that runs and does
nothing. Every row that touches an enemy carries `Burn=1`, checked on the data
rather than on the prose, because a description saying "setting each one alight"
with no parameter beside it would read correctly and do nothing. And
`FinalHitPercent` appears only on a skill that repeats, because the Strike
template lands its closing hit from the timer that ends a repeating swing, so a
non-repeating skill would carry a number nothing reads.

**Two tag names were wrong and generation refused them.** `Stat.Defense.Reduction`
and `Stat.Offense.AttackSpeed` do not exist; the real names are
`Stat.Defense.Global` and `Stat.Offense.Speed`. This is the tag validator doing
its job: an undefined tag on a skill row means every gear increase scoped to it
silently stops applying.

**Affects:** `All_Things_Cataclysm.xlsx`, Weapon Skills sheet, 35 rows.
`Cataclysm_GDD_v2.md`, whose Demonic Skill Examples section now says all ten
weapon types are designed. **Applied.** Issue #62 is closed by this.

---

## 2026-08-04 — What a skill costs: a cooldown per slot, a flat mana cost, and mana back from the automatic basic attack

**The question.** Issue #155. No skill in the project stated a cooldown or a
resource cost. Not one of the 61 War rows and not one of the 16 Demonic rows.

**THIS IS THE SAME FAILURE AS ISSUE #120, at a larger scale.** Around the missing
base cooldown the project had already built: a reduction formula
(`Final Cooldown = Base Cooldown / ((1 + increases) × more)`), the Efficacy
attribute granting 1% per point, a cooldown reduction affix on five gear slots, a
Reliquary implicit, and **41 enchantments that mention cooldown**. Every one of
them divided zero. Mana costs were in the same state: four enchantments change a
skill's mana cost, including a ten-piece set bonus reading "your ultimate ability
no longer has a cooldown, instead its mana cost is doubled every time you use
it", and no skill had a cost for either half of that sentence to act on.

**WHERE THE BASE BELONGS WAS ALREADY SETTLED.** The design document's stat source
table says the skill being used supplies "off this sheet, the base cooldown,
projectile count and duration". So this is the design becoming real rather than a
change to it.

**FIRST DECISION: cooldown and cost belong to the slot, not to a column on the
skill sheet. This reverses what issue #155 itself recommended.** The issue argued
for columns. Reconnaissance changed it: no designed skill states either number,
so a column would be 77 copies of six values, and the damage multiplier already
solved this exact problem the other way. It lives in the slot table, and a skill
states its own only when it differs, which is how Skull Splitter says 500%.

**SECOND DECISION: the numbers, set by the project owner.** A first set was
anchored on Diablo 4, whose ultimates cluster at 50 to 60 seconds and whose
defensive skills sit near 20. The project owner judged those too long to play and
gave the values below directly. Movement kept its 5 seconds.

| Slot | Cooldown | Band | Mana at level 100 |
|---|---|---|---|
| Basic Attack | none | — | restores 6 on hit |
| Heavy Attack | 1.5s | 1–4s | 15 |
| Support | 4s | 2–10s | 25 |
| Special | 5s | 3–10s | 40 |
| Movement | 5s | 3–10s | 20 |
| Ultimate | 20s | 12–40s | 150 |
| Aura | none | — | 20 per second |

Diablo 4 still set the shape rather than the values: a cooldown per slot, an
ultimate that is the longest wait by a wide margin, and a primary damage button
that returns fastest. Its own numbers assume a resource system this design does
not use, which is the reason not to take them directly.

Only two slots have no cooldown, for different reasons: the Basic Attack is
automatic so attack speed sets its rate, and the Aura is a toggle so there is
nothing to wait for. A guard refuses any other slot reading zero, because a zero
cooldown is also what a forgotten one looks like — which is exactly how this went
unnoticed for so long.

**THIRD DECISION: mana costs are flat numbers, the same for every class.** An
earlier version made a cost a percentage of the player's own maximum mana. The
project owner rejected it: it "just feels bad". It was also wrong on its own
terms, because it made a large mana pool buy nothing — the pool and the price
rose together, so the Ritualist's 1,278 mana bought exactly as many casts as the
Ravager's 436.

Flat costs give the opposite and correct result. The same 15 mana Heavy Attack is
9 casts for a Ravager and 27 for a Ritualist, and every source of maximum mana —
the Mind attribute, two affixes and a hybrid — is pure gain.

**Why the costs still scale with character level.** Nothing in this project
raises a skill's cost the way a gem level does in Path of Exile, and a Ravager's
pool runs from 40 at level 1 to 436 at level 100. A number that never moved would
be crippling at one end and beneath notice at the other. Costs ride the default
mana progression, so a skill takes the same share of a pool at both ends. On the
default line that share is exactly constant; a class with its own mana curve
drifts under 20% across 100 levels, and a test holds it there. What the player
reads is still a flat quantity of mana.

**FOURTH DECISION: the automatic basic attack restores 6 mana on hit, and this
is deliberately not a generator.**

The project owner raised the concern while asking for it: the generator and
spender pattern "is often just annoying". The complaint is well documented.
Diablo 4 players describe generators producing 3 to 4 resource against spenders
costing 30 to 40, so roughly five filler casts buy one real skill, and describe
the result as casting boring spells to earn the right to cast interesting ones.

Two things structurally prevent that here, and the second is enforced by a guard
rather than left to intent.

  - **The basic attack is automatic.** The design document has said so from the
    start. There is no button to press and no rotation to perform, so there is no
    filler action to resent. It is income for being in a fight.
  - **The Heavy Attack is affordable from mana regeneration alone.** Used the
    moment it returns it costs 10 mana per second against 10.9 per second of
    default regeneration, so the primary damage button works with no basic
    attacks landing at all. Mana on hit pays for the other slots. A check refuses
    any Heavy Attack cost that breaks this, and a second check refuses a
    mana-on-hit value large enough to become a character's main income.

Path of Exile treats mana on hit as ordinary sustain alongside regeneration and
leech, and it draws none of the same complaint, because there the skill doing the
hitting is the one the player wants to use. The same is true here.

**What this produces at level 100, with no gear and no attribute points:**

| Class | Mana | Regen | Income while fighting | Everything on cooldown lasts |
|---|---|---|---|---|
| Ravager | 436 | 10.9/s | 18.6/s | 25s |
| Ritualist | 1,278 | 26.8/s | 34.6/s | effectively unlimited |
| Masochist | 644 | 10.9/s | 19.6/s | 40s |

Using every skill the moment it returns costs 35.75 mana per second, the same for
all three because the costs are flat. A character can spend everything for about
half a minute and must then choose what to keep using. The Ritualist is the
exception and is meant to be: sustaining a whole kit is what its pool and
regeneration are for.

**The Aura runs out for two of the three classes, and that is the right answer
rather than a gap.** It drains 20 mana per second, emptying a Ravager standing
still in 48 seconds and a Masochist in 71. The Ritualist's 26.8 per second
regeneration covers the drain, so it alone can hold an aura indefinitely. Issue
#36 requires the aura to switch off when the resource is exhausted; that is
reachable, which is what the requirement needs, and a class being able to avoid
it is a class difference rather than a missing limit.

**A consequence of the Support cooldown, recorded and not resolved.** At 4
seconds, and with the designed Support buffs lasting 8 to 10 seconds, every
Support buff has more than full uptime. The slot becomes a permanent stat rather
than something used at a moment, and its 25 mana is then the only real limit on
it. This is a constant rather than a structure, so it is left for play to settle.

**A TENSION THE SHORTER COOLDOWN RESOLVED, which the longer one did not.** The
design calls the Heavy Attack "often the primary damage button". At the 6 second
cooldown first proposed it was not: 250% every 6 seconds is 41.7% of weapon
damage per second, against 130% per second from an automatic basic attack dealing
100% at 1.3 attacks per second. The basic attack out-damaged it three times over
and the design's own words were false.

At 1.5 seconds the Heavy Attack deals 166.7% per second and is the larger source
for every weapon in the game, from 1.11 times the basic attack with the fastest
weapon to 1.39 times with the slowest:

| Weapon rate | Basic attack | Heavy Attack is |
|---|---|---|
| Dagger, 1.50/s | 150%/s | 1.11x |
| Fist, 1.45/s | 145%/s | 1.15x |
| Crossbow, Wand, Spear, 1.35/s | 135%/s | 1.23x |
| Greataxe, 1.28/s | 128%/s | 1.30x |
| Shield, Warhammer, 1.20/s | 120%/s | 1.39x |

The margin is deliberately not large. The basic attack is meant to be a real part
of a character's damage rather than a formality, and it is also the mana income.
The Heavy Attack stops being the larger source above 1.67 attacks per second, and
the fastest weapon in the game is the Dagger at 1.50, so there is room but not
much. A test holds it, because raising the Heavy Attack's cooldown or a weapon's
rate could quietly reverse it again.

**Sources.** The Diablo 4 forums and a widely cited write-up of its resource
problem, for the generator ratio and the complaint against it; Maxroll and Icy
Veins on Diablo 4 cooldown reduction and per-skill cooldowns; the Path of Exile
wiki on mana and on cooldown, for mana on hit and leech being ordinary sustain
and for cooldown and cost being separate limiters; the Last Epoch wiki on skills
and mana, for each skill carrying both a mana cost and a cooldown.

**What the research does not settle.** Every number in the table. No reference
game has this game's six slots, and the cooldowns were set by the project owner
against how the game should feel rather than derived. The research settles the
shape: cooldown per slot, cost separate from cooldown, and the specific rule that
keeps mana on hit from becoming a generator.

**Affects:** `Cataclysm_GDD_v2.md`, which gains a "What a Skill Costs"
subsection and a "The Basic Attack Restores Mana, and This Is Not a Generator"
subsection in section IV. `sim/cataclysm_sim/character.py`, where `SkillSlot`
carries the cooldown band and the flat mana cost. **Applied.** No change to
`All_Things_Cataclysm.xlsx`: these numbers are per slot, and the sheet holds
per-skill rows.

---

## 2026-08-04 — The Demonic skills for the vertical slice, and Burn becoming an effect the player can apply

**The question.** Issue #62: design the Demonic skills for the vertical slice's
three weapon types. The slice targets the Demonic Cataclysm, and no Demonic skill
existed.

**RECONNAISSANCE CHANGED WHAT THE WORK WAS, twice.**

*The issue was stale on its own premises.* It says there are 71 Demonic rows with
tags already filled in. There are 51, because the weapon availability table from
issue #23 was applied and cut the sheet from 558 rows to 398. And the tags are
not filled in: only 91 of the 398 rows carry any tag at all — all 61 War rows,
plus a stub pair of weapon-and-element tags on the five Sword rows of six other
damage types. Void has none. So the work included writing the tags, not only the
names and descriptions.

*The blocker was not the one the issue named.* Issue #62 said it was blocked on
#23. That is closed and applied. The real blocker was that **Burn did not exist
as something a player could do.** The design document's table of effects a player
can inflict lists nine and Burn is not among them. `game/Data/StatusEffects.csv`
carried a Burn row reading only "Applied by the Infernal Brand and Hellfire Aura
enemy modifiers" — both enemy modifiers. No gem applies burn and no affix grants
chance to burn, where all nine other effects have both.

Demonic is "Fire, Lava, Rage based effects". Its signature damage over time
effect was something only the enemies could do. Writing sixteen skills that set
enemies alight would have produced sixteen skills that do nothing, which is the
same failure as issue #120 and issue #146: a thing referenced everywhere with
nothing behind it.

**FIRST DECISION: Burn takes the same shape as Bleed, Poison and Disease.** Those
three are already identical in the design — damage over time, magnitude scales
the damage — and differ only in what applies them. Burn becomes the fourth. This
was read off the existing convention rather than chosen.

Research was checked before settling it and it does not point anywhere else that
this project could follow. Path of Exile's ignite does not stack and only the
highest-damage one deals damage at a time; Last Epoch's stacks without limit;
Diablo 4's refreshes from the same source and stacks across different ones. This
project already decided on 2026-08-03 that an enemy carries at most one stack of
any effect, which is Path of Exile's answer, so the stacking question was already
settled and Burn inherits it. The remaining differences between those games are
in duration and front-loading, and this project has not defined a duration for
Bleed, Poison or Disease either, so there is nothing to differentiate against
yet.

**A gap this leaves, deliberately, and it is tracked.** There is still no burn
gem and no chance-to-burn affix, so a Demonic burn build cannot scale burn
magnitude the way a War bleed build scales bleed. That is an item system change
rather than a skill system one and is a separate issue.

**SECOND DECISION: the three weapons are Greataxe, Fist and Staff, one per
Demonic class.** The design document gives Demonic three classes, and the roadmap
in section XV names the Masochist as the slice's passive tree. A slice that
cannot equip one of its three classes does not test the design.

| Weapon | Class it serves | What it exercises |
|---|---|---|
| Greataxe | Ravager, the frontline melee aggressor | Two-handed heavy melee, and the two-handed damage multiplier |
| Fist | Masochist, which converts damage taken | Fast close melee, health as a resource, retaliation |
| Staff | Ritualist, the summoner | Spells and minions, which nothing else in the slice tests |

Greataxe and Fist reuse the War animation sets for the same weapon and slot,
under the rule in issue #18 that animation follows weapon and slot rather than
damage type. So the Staff is the only new animation set the slice buys. That cost
is worth paying: spells and minions are the largest untested pieces of the combat
system, and commit `f0f317f` gave the Staff 66 flat damage two days ago
specifically so spells would deal something. Nothing used it until now.

**THIRD DECISION: the descriptions do not count stacks, and the War ones do.**
Fifteen of the 61 War descriptions say things like "applies 2 bleed stacks",
written before the single-stack rule of 2026-08-03. When that rule landed,
Necrosis was corrected to fit it and the skill sheet was not. The Demonic set
follows the rule, and a test refuses any Demonic description that counts stacks.
The War rows are a separate issue.

Where a War skill's shape depended on counting, the Demonic equivalent counts
burning enemies instead: War's Blood Frenzy gives 5% per bleed stack within 15
meters, and Demonic's Burning Wrath gives 4% per burning enemy within 15 meters.

**Sources.** The Path of Exile wiki and Mobalytics on ignite, for ignite not
stacking and only the strongest dealing damage; Maxroll's Diablo 4 damage over
time write-up and Icy Veins, for burn refreshing from the same source and
stacking across different ones, and for half-second ticks; Maxroll's Last Epoch
damage calculation page, for ignite stacking without limit; the Path of Exile
wiki on Rage, for a melee resource that builds on hits and decays out of combat,
which is the shape the Ravager's skills assume without naming a resource.

**What the research does not settle.** Which of Demonic's ten weapons the slice
takes. No other game has this game's weapon list or its damage types. The choice
above is a judgement, made against the three class identities in section IV and
the roadmap's choice of the Masochist tree.

Also not settled by research: every radius, duration and percentage below. Those
are first numbers to be tuned against play, chosen to sit beside the War figures
for the same slot.

**Affects:** `Cataclysm_GDD_v2.md`, which gains a Burn row in its table of
effects, a sentence stating that a skill may apply an effect outright with no
chance roll, and a Demonic Skill Examples section beside the War one.
`All_Things_Cataclysm.xlsx`, sixteen Weapon Skills rows and the DoTs sheet's Burn
row. **Applied.**

---

## 2026-08-04 — The Wand and the Staff get flat damage, because a spell is a percent of weapon damage too

**The question.** Issue #146, raised by the project owner while reviewing the
attack speed work: "wand and staff should have flat damage... spells need damage
too you know."

**Measured, and it was worse than a dead implicit.** Every skill deals a percent
of weapon damage. `Skill.weapon_damage_percent` in `character.py` returns the
skill's own multiplier or the typical one for its slot, and there is no separate
path for spells. Weapon damage comes from a base's flat attack damage implicit
and nowhere else. The Wand and the Staff had none, giving only INCREASED spell
damage. So a character holding either dealt exactly zero with every skill: a
percent of zero is zero. The Ritualist's 160 spell damage at level 100 and the
Staff's own 32% increased spell damage were both multiplying nothing.

That is the same failure as issue #120, one layer up: a multiplier with nothing
to multiply.

**THE NUMBERS WERE NOT CHOSEN, THEY WERE READ OFF THE ORDERING ALREADY SET.** The
fourteen attack speed values decided earlier the same day are ordered inversely
to each weapon's flat attack damage. A one-handed weapon at 1.35 attacks per
second sits where the Crossbow does, at 38 damage. A two-handed weapon at 1.30
sits where the Two-Handed Crossbow does, at 66. The Wand is 1.35 and the Staff is
1.30, so the ordering gives 38 and 66 with nothing left to decide.

Both weapons therefore tie an existing base on damage and rate and differ only in
sub-type and second implicit, which is the intended shape: the Crossbow pairs 38
damage with 20 critical strike multiplier, the Wand pairs it with 18% increased
spell damage.

**The alternative was considered and rejected.** Putting them at the low end of
their class instead — the Wand at 26 like the Dagger, the Staff at 64 like the
Spear — is what the inverse ordering would give if their rates were free. They
are not free: the one-handed rates average to 1.35 and the two-handed to 1.28, a
test asserts both, and those averages are what the shipped two-handed multiplier
of 2.0 was derived against. Moving one weapon's rate has to be paid for by
another. Chosen by the project owner: take the numbers the existing ordering
gives and move nothing that is already balanced.

**A risk accepted rather than solved.** A weapon with middling damage that also
carries the strongest secondary implicit in its class may simply be the best
pick. That is a tuning question real play answers better than argument, and the
constants in this project are tuned against play rather than argued to death
first.

**A guard was added, because nothing had reported this.**
`_check_every_weapon_but_the_shield_supplies_damage` in `affixes.py` refuses a
weapon base with no flat attack damage. The Shield is the one exemption, for the
same reason it is exempt from the check that no weapon base defends: it is not
there to hit anything.

**Affects:** `Cataclysm_GDD_v2.md`, the weapon base table in section V, whose
Wand and Staff rows now read "38 flat damage, 18% increased spell damage" and
"66 flat damage, 32% increased spell damage". **Applied.**
---

## 2026-08-04 — Attack speed comes from the weapon as a rate, not an implicit, and every skill crits 5% by default

**The question.** Issue #120. Attack speed and critical strike chance both had a
base of zero everywhere in the project. Attributes and affixes only ever scale a
base, so every increase to either was worth exactly nothing. Eight of the
reference character's 72 affix slots did nothing at all.

**WHERE THE BASES LIVE WAS ALREADY SETTLED and did not need deciding.** The
design document's stat source table says the equipped weapon supplies attack
speed and the skill being used supplies critical strike chance. Nothing supplied
either. So this is the design becoming real rather than a change to it.

**FIRST DECISION: a weapon's attack speed is a field on the base, not an
implicit, and that is load-bearing.** A two-handed weapon doubles every implicit
it carries. That is deliberate and is what balances four affix slots against a
dual wielder's eight. Applied to a rate it is nonsense: a Greatsword would swing
twice as fast as a Sword. Path of Exile and Last Epoch both treat a weapon's rate
as an intrinsic property listed apart from its modifiers, and Last Epoch states
its formula as skill rate times weapon rate times one plus increases. So the rate
sits beside the implicits and nothing scales it but increases.

**SECOND DECISION: the numbers, and they were anchored rather than invented.**
`sim/analyse_two_handed_multiplier.py` already carried the answer, read off Path
of Exile's base weapon table when the two-handed multiplier was derived: one
handed weapons average 1.35 attacks per second and two-handed 1.28. That script
also records an earlier attempt to derive rates instead, which produced 1.25
against 0.85 and was rejected as nothing like what a shipped game uses.

Those two averages are load-bearing. The two-handed multiplier of 2.0 is already
shipped and was measured against them, so per-weapon rates that do not average
back to them would move a multiplier nobody meant to move. The fourteen values
are ordered inversely to each weapon's flat attack damage and average to exactly
1.35 and 1.28. A test in `tools/tests/test_affix_sheets_match_the_model.py`
asserts both averages, so this cannot drift quietly.

| One-handed | Attacks/sec | | Two-handed | Attacks/sec |
|---|---|---|---|---|
| Dagger | 1.50 | | Spear | 1.35 |
| Fist | 1.45 | | Two-Handed Crossbow | 1.30 |
| Whip | 1.40 | | Staff | 1.30 |
| Crossbow | 1.35 | | Greataxe | 1.28 |
| Wand | 1.35 | | Greatsword | 1.25 |
| Sword | 1.30 | | Warhammer | 1.20 |
| Axe | 1.25 | | | |
| Shield | 1.20 | | | |

The spread is narrow on purpose. Path of Exile's whole range is 1.10 to 1.60 and
its two-handed swords are as fast as its daggers; a two-hander earns its
advantage through much larger base damage, not through swinging much more slowly.

**THIRD DECISION: every skill supplies 5% base critical strike chance unless it
names its own.** 5% is Path of Exile's base for a plain melee weapon, and it is
already what this project gives an ordinary enemy in `enemy_stats.py`, so the
player and the enemies start from the same place. It is a default and not a
floor: a skill that states 1% gets 1%, which is what lets a skill be designed to
crit less than average. Only 61 of 558 skill rows are designed, so a default plus
per-skill overrides is the only practical shape.

**WHAT RESEARCH SAYS WE ARE DOING DIFFERENTLY, stated rather than hidden.** Path
of Exile splits critical strike chance by source: weapon attacks take it from the
weapon and only spells take it from the skill gem. This project applies one rule
to both. That is the design document's stat source table, it is simpler, and it
was kept deliberately after the divergence was put to the project owner.

**Sources.** Path of Exile's base weapon table by way of incendar.com, giving
attacks per second and base critical strike chance per weapon class; the Path of
Exile wiki on critical strike, for attacks taking base critical strike chance
from the weapon and spells from the skill gem; Last Epoch's damage calculation as
written up by Maxroll, for the skill-rate times weapon-rate times increases form
and for dual wielding averaging the two weapons' rates.

**What the research does not settle.** Which of this game's fourteen bases gets
which number. Path of Exile's weapon classes do not map onto them. The ordering
is a judgement: inversely to flat attack damage, constrained to hit the two
averages already in use.

**Affects:** `Cataclysm_GDD_v2.md`, the weapon base table in section V, which now
carries an attacks per second column. **Applied.** The base critical strike
chance default is recorded here and in `character.DEFAULT_SKILL_CRIT_CHANCE`; the
design document's stat source table already said the skill supplies it and needed
no change.
---

## 2026-08-03 — The control scheme: what the left mouse button does, and why there are two schemes rather than one

**The question.** Issue #16. The design document's control table gives eight
bindings, and two of them could not be built as written.

**FIRST PROBLEM: the left mouse button was given two jobs.** The control table
says it is "Player movement and basic attack". Every game in the genre that
overloads that button needs a rule for which job a click means, and the issue
asked for one.

Research first. Path of Exile 2 resolves it by what is under the cursor: a click
on the ground moves, a click on an enemy attacks, and holding shift attacks
without moving. Diablo 4 does not resolve it at all — it avoids it, by shipping
two control presets, and in the keyboard preset the mouse buttons are pure skill
buttons because the movement has gone to WASD.

**But this game does not have the problem, and that is the decision.** Section
"Combat System" of `Cataclysm_GDD_v2.md` says basic attacks are handled
automatically, and `ECataclysmAbilitySlot` in the code agrees — the slot is
labelled "Basic Attack (automatic)". If the basic attack fires on its own, the
left mouse button has no second job. **It moves, and only moves.** Clicking an
enemy walks toward it exactly as clicking the ground does.

This removes the disambiguation rule rather than choosing one, which is worth
more than picking well between two options.

**A contradiction inside the design document is now visible and is NOT yet
fixed.** The control table still says the left mouse button fires the basic
attack; the combat section still says basic attacks are automatic. The
implementation follows the combat section. The project owner chose to record this
decision without editing the table, so the table is stale on purpose and is
tracked separately.

**SECOND PROBLEM: W is listed twice.** The control table puts the Support ability
on W and also lists WASD as optional directional movement. One key cannot be
both, and nothing in the document says which wins.

This is exactly why Diablo 4 and Path of Exile 2 ship presets rather than one
scheme. Under keyboard movement the skills have to move off the movement keys.
So the game now has two mapping contexts, and only ever one of them is active:

| Context | Movement | The change from the design table |
|---|---|---|
| `IMC_MouseMovement` | left mouse button, plus the gamepad stick | none; the table exactly |
| `IMC_KeyboardMovement` | WASD, plus the gamepad stick | Support moves from W to 1, and the left mouse button is left unbound |

Which one the game starts in is one line in `game/Config/DefaultGame.ini`. There
is no way for a player to change it yet, because there is no settings screen.

**THIRD DECISION: shift means stand still, not force move.** Last Epoch shipped
shift as force *move* and has a long-running player complaint asking for the
opposite; Path of Exile 2 uses shift for attack-in-place. The shape with the
better evidence is the one that keeps the character still, so that is what it
does.

**FOURTH DECISION: a key press names a slot, never an ability.** Input is bound
to the `Slot.*` gameplay tags, and the ability system activates whichever granted
ability carries the tag. This is the pattern in Epic's own Lyra sample, and it is
what lets the equipped weapon change all six abilities without a code change,
which is what issue #36 needs.

Because the slot list is now written down twice — as `ECataclysmAbilitySlot` in
C++ and as the generated `Slot.*` tags from the workbook — the test
`Cataclysm.Input.EveryAbilitySlotHasAGeneratedTag` checks both directions. That
is the same drift risk that produced `verify_scoring_port.py`.

**Sources.** Path of Exile 2 controls, Fextralife wiki and Game8; Diablo 4
keyboard movement presets, Turtle Beach and Dexerto guides; the Last Epoch forum
threads asking for force stand still; Epic's Lyra input documentation as written
up by unrealcode.net and X157's notes.

**What the research does not settle.** The camera distance and angle. Every game
in the genre differs and the right answer depends on art that does not exist yet.
The starting values are taken from Unreal's own top-down template and are
expected to change.

**Affects:** `Cataclysm_GDD_v2.md`, "Controls and Key Bindings". **Applied** in
issue #138. The section now has one table per scheme, says the basic attack is on
no key, and says which scheme ships as default and where that is set.
`tools/tests/test_controls_table_matches_the_input_assets.py` compares both tables
against `MOUSE_MAPPINGS` and `KEYBOARD_MAPPINGS` in
`tools/generate_input_assets.py`, so the document and the bindings cannot drift
apart again without a test naming the binding that differs.

---

## 2026-08-03 — The Power Score anchors describe the ceiling, and do not move

**The question.** Issue #125. Rarity became a label for what fills an item's four
slots, which means a Cataclysmic item spends all four on enchantments and carries
no regular affixes at all. The Expected Character by Tier table says a difficulty
tier 8 character is Cataclysmic on all eighteen pieces, so that character has 72
enchantments and no ordinary stats — while every affix value in the project was
fitted against 72 regular affix slots, which is a full set of Masterful gear.

Three ways out were put to the project owner: move the expected character, treat
rarity as a ceiling rather than a count, or refit the affix values against a
character whose power is all enchantments.

**The decision, stated by the project owner:**

> I would argue against refitting the power score anchors. All cataclysmic gear
> is what pushes you towards the max power score. I think it's fine as long as we
> keep that in mind.

**So nothing moves.** The anchors stay, the affix values stay, and the Expected
Character by Tier table stays. What changes is what the table is understood to
describe: the **ceiling** a tier can produce, not a typical build.

**The measurement, which is why this is worth recording.** A level 100 character
with eighteen pieces at +10, 45 Cataclysmic gems and all eight resistances
capped, scored by `sim/cataclysm_sim/player_power.py`:

| Gear on every piece | Power Score | Against the tier 8 anchor |
|---|---|---|
| Cataclysmic | 6,327 | 100% |
| Ascendant | 5,932 | 94% |
| Mythical | 5,536 | 88% |
| Legendary | 5,141 | 81% |
| Masterful | 4,745 | 75% |
| A mix of 4 Cataclysmic, 4 Ascendant, 5 Mythical, 5 Legendary | 5,690 | 90% |

**A real build sits below the anchor and that is the design working.** Every gear
rarity is a trade rather than a straight upgrade: a Legendary gives up a regular
affix for an enchantment, and an enchantment carries a drawback as well as a
benefit. A character that keeps some ordinary stats scores less than one that
gave them all away, and chasing Cataclysmic gear is what pushes toward the
maximum.

**WHAT TO KEEP IN MIND, which is the whole of the risk here.** Two figures now
describe different characters, and neither is wrong:

| Figure | The character it describes |
|---|---|
| The tier 8 anchor of 6,327 Power Score | Eighteen Cataclysmic pieces, 72 enchantments, no regular affixes |
| The 72 regular affix slots every affix value was fitted against | Eighteen Masterful pieces, no enchantments |

Anything that reads one and assumes the other will be wrong. The reference
character in `sim/cataclysm_sim/reference_build.py` is the second of the two and
says so in its docstring.

**Affects:** `Cataclysm_GDD_v2.md` section VII. **Applied 2026-08-03:** the
Expected Character by Tier paragraph now says the table is the ceiling rather
than what a player is expected to look like, and the measurement above was added
beneath it.

---

## 2026-08-03 — Class stat lines and attribute effects become data

**The decision.** The three Demonic class stat lines and the eight attribute
effects now live in two new sheets of `All_Things_Cataclysm.xlsx`, generated into
`game/Data/ClassStats.csv` and `game/Data/Attributes.csv`. Same arrangement as
the affix pool: the workbook is authoritative, and a test compares it against
`sim/cataclysm_sim/classes.py` and `character.py`.

**Why it was needed.** Issue #130. The Unreal project had the eight attributes as
Gameplay Ability System attributes and nothing that said what a point of one was
worth, and no class stat line at all. The test that builds the reference geared
character had to quote both from the Python model as literals, so a change to the
Ravager's stat line would not have failed anything on the game side. It now reads
both from the generated tables and still reaches 11,023 maximum health.

**THE DEFAULT LINE IS A ROW SET, NOT A SPECIAL CASE.** A class named "Default"
carries the stat line every class inherits, and each real class overrides only
what expresses its identity. Anything reading it resolves a value by looking for
the class's row, then Default, then zero.

That shape is not a storage trick. There are 33 stats and 24 classes planned, so
writing every class out in full would be 792 rows of which almost all would
repeat — but more importantly it is what the design means by a class. The three
War trees each commit to three or four stats and ignore the rest, so a class is
defined as much by what it refuses as by what it takes. The Ravager overrides 7
of the 33, the Ritualist 9 and the Masochist 5.

**Attribute effects are stored as percent per point.** Vitality reads 2, meaning
2% maximum health per point. The model stores the same figure as a fraction. The
sheet uses percent because that is what a designer editing it means, and it
matches the percentage-point convention the Unreal stat pipeline already uses;
the drift test converts in the open rather than either side hiding it.

**Attributes only ever scale, and the generator now says when that costs
something.** A point adds to a stat's sum of increases and the sum multiplies a
base, so a point does nothing until something supplies that base. The class is
not the only thing that can — gear implicits and affixes supply block chance,
critical strike chance and evasion — so this is reported as a note rather than
treated as an error. Five stats currently have no class base:

| Stat | Where its base has to come from |
|---|---|
| block chance | gear implicits and affixes |
| critical strike chance | gear implicits and affixes, and the skill |
| evasion | gear implicits and affixes |
| magic find | nothing yet; see issue #81 |
| loot quantity | nothing yet |

The note exists because attack speed had no base anywhere at all for some time,
which made every attack speed affix on every item worth exactly nothing, and
nothing reported it. That is issue #120. Cooldown reduction is excluded from the
note: it is the accumulated sum of increases rather than a value, so a base of
zero is correct for it.

**`ClassDefinition.spends_health` was not ported.** It is declared in
`character.py` and read nowhere. The Masochist's "uses health instead of mana" is
delivered by a passive tree node converting mana into health, which is a build
choice rather than a class property, so the field appears to be a superseded
first attempt.

**Affects:** no design document change. Section VI already describes attributes
as scaling rather than creating, and the class stat lines were recorded in the
2026-08-02 entry on the three Demonic classes; this is that design becoming data
the game can load.

---

## 2026-08-03 — The affix pool moves into the workbook, and the workbook wins

**The decision.** The 55 item bases and 59 rollable affixes now live in two new
sheets of `All_Things_Cataclysm.xlsx`, generated into `game/Data/ItemBases.csv`
and `game/Data/Affixes.csv` like the nine tables before them. **The workbook is
authoritative.** It is what a person edits, and it is what Unreal loads.

Chosen by the project owner over the two alternatives. Generating the sheets
from `sim/cataclysm_sim/affixes.py` would have made Python authoritative and put
a dependency from the game's build tooling onto the simulation, which the layout
rules keep separate. Authoring the tables by hand in the Unreal editor would have
created a second source of truth with no way to compare the two.

**THE SIMULATION KEEPS ITS COPY, AND THAT IS THE RISK.** `affixes.py` still holds
the same pool, because that is where the design rules are enforced — a stat
cannot be both a prefix and a suffix, every slot must be able to fill all four of
its affix slots, no weapon may roll a defensive affix — and where tuning happens.
Two copies of the same numbers drift. This project has already been bitten by
exactly that: `scoring.py` is a copy of a file in another repository and drifted
silently twice, which is why `verify_scoring_port.py` exists.

So `tools/tests/test_affix_sheets_match_the_model.py` compares them, one test per
kind of thing that can disagree. It was confirmed to fail when a value was
changed in the sheet and the tables regenerated, which is the case that gets past
the staleness check.

**Only stored values are compared.** The seven-tier curve, the roll band, the
gear level multiplier and the two-handed multiplier are formulas in `affixes.py`
and appear in no sheet, so they are checked by their own tests instead.

**Four affix kinds share one table**, distinguished by a column: a single stat, a
resistance family covering one, two or eight damage types, an ailment chance, and
a hybrid granting two stats at a reduced share. Splitting them into four tables
would mean a drop had to roll against four pools and know their relative weights.

**Two cross-checks the generator now runs**, both of which fail silently without
it. An affix naming a slot no item base occupies simply never rolls, on any drop.
A hybrid naming a part that is not an affix grants half of what it says. Neither
produces an error anywhere. Both are checked by comparing the two sheets against
each other rather than against a hard-coded list, so adding a slot to the design
needs no change to the generator.

**Affects:** no design document change. Section VI already describes the affix
pool, the tiers and the slot restrictions; this is that design becoming data the
game can load.

---

## 2026-08-03 — Dual wielding, and what a two-handed weapon is worth

**This is the first decision made under the rule that formulas are researched
rather than invented**, which `CLAUDE.md` now carries. It is worth saying what
that changed, because two answers reached by reasoning alone were wrong and the
research replaced both.

**What the project owner decided.** Dual wielding gives a real second weapon
piece, so 19 equipped pieces and 76 affix slots against a two-handed character's
18 and 72. The balance comes from a two-handed weapon's affixes being worth more
per affix rather than from equalising the slot count: "yes dual wielding is a
thing. This is compensated for by 2h affixes having more value than 1h affixes."
Two weapons **sum** their base damage.

**Last Epoch already does exactly this**, which is the strongest evidence the
shape is sound. It gives a dual wielder the stats of both weapons combined,
averages their attack speed, and balances two-handed weapons by giving them an
inherent bonus to their affixes **and their implicit stats**. The decision was
reached independently and then found to be a shipped design.

**THE MULTIPLIER IS 2.0 AND IT IS DERIVED.** Two one-handed weapons hold eight
affix slots against a two-hander's four, so 2.0 is the value that makes the two
loadouts worth the same in affixes. Section VII of the game design document
already requires that equality: it states that two one-handed weapons count as
one equipped piece for Power Score so dual wielding is not worth free power. The
rating model scores both loadouts the same, so whichever side had the larger
affix budget would carry power its rating does not count.

**IT APPLIES TO IMPLICITS AS WELL AS AFFIXES, and that is the part research
supplied.** Two rounds of measurement had framed this as a choice between two
levers, and both were wrong:

| Lever considered | Why it fails |
|---|---|
| Raise the affix multiplier alone | Reaching a damage edge needs about 2.75, handing the two-hander three affix slots the dual wielder does not have — the free power section VII forbids, pointed the other way |
| Raise the five two-handed base damage numbers | Reaches the same place but changes numbers that did not need changing, by about 1.64 times |

Last Epoch's answer is neither: one multiplier on both. A weapon's base damage is
an implicit here, so the 2.0 already derived covers it, and **no weapon base
damage number changes at all**.

**Why the implicit half is load-bearing.** Two one-handed bases sum to more than
any two-handed base — an Axe and a Sword give 86 against a Greatsword's 78, and
across the family the five two-handed bases average 1.03 times two one-handed
ones. With the affix half alone the two-hander loses on damage while holding one
fewer damage type, which makes it strictly worse than dual wielding at
everything. Doubling the implicit is what reverses that.

| Measured at affix tier 7 on +10 gear | Result |
|---|---|
| Damage per hit | Two-hander 1.33x |
| Damage per second | Two-hander 1.26x |
| Damage types | Dual wield 4, two-hander 3 |
| Affix budget | Exactly equal |

**Attack speed is the average of the two weapons.** Both Last Epoch and Path of
Exile do this; Path of Exile reaches it by alternating hands, which produces the
average. It is what stops summed damage becoming a strict advantage: a dual
wielder deals more per swing than either weapon alone but does not also swing at
the faster weapon's rate. Summing the damage does not settle output on its own,
because output is damage times rate — at a one-handed rate, summed damage makes
dual wielding 1.43 times a two-hander before any affix.

**No defensive penalty for dual wielding.** Last Epoch charges 8% more damage
taken, reduced from 9%. Rejected by the project owner, and its own forums carry
threads asking for the removal, so the reception is evidence rather than only
taste.

**A wrong number this corrected.** Weapon attack rates had been derived on the
assumption that base damage per second should be even within a hand class, giving
1.25 attacks per second for one-handed weapons against 0.85 for two-handed, a 32%
gap. Path of Exile's actual base rates are 1.15 to 1.55 one-handed and 1.15 to
1.45 two-handed. The ranges overlap; a two-hander is only slightly slower and
earns its advantage through much larger base damage. The per-weapon numbers are
still open on issue #120.

**What is guarded.** `_check_the_two_loadouts_have_equal_affix_value` in
`sim/cataclysm_sim/affixes.py` asserts the equality rather than trusting the
arithmetic, and `_check_only_a_two_handed_weapon_multiplies_its_values` asserts
that nothing else quietly gains a multiplier, which would break the equality
without changing any count. Both were confirmed to fail when broken.

**Sources.**

- Last Epoch dual wield mechanics, developer commentary: https://devtrackers.gg/last-epoch/p/8bcd18da-dual-wield-mechanics
- Last Epoch gear walkthrough: https://maxroll.gg/last-epoch/resources/gear-walkthrough
- Last Epoch Season 3 patch notes, the dual wield penalty: https://maxroll.gg/last-epoch/news/season-3-patch-notes
- Path of Exile dual wielding: https://pathofexile.fandom.com/wiki/Dual_wielding
- Path of Exile base weapon table: https://www.incendar.com/poe_weapons.php

**Affects:** `Cataclysm_GDD_v2.md` section VI. **Applied 2026-08-03:** a Two-Handed
Weapon Is Worth Double subsection and a What a Dual Wielder Has subsection were
added after the weapon base table, and the affix slot count sentence was
corrected to mention the dual wielder's 76. The working model is
`TWO_HANDED_MULTIPLIER` in `sim/cataclysm_sim/affixes.py`, measured by
`sim/analyse_two_handed_multiplier.py`.

---

## 2026-08-03 — The three buckets in Unreal, and what the engine already does

**The finding that shaped this.** Unreal's Gameplay Ability System already
implements the design's stat pipeline. Its attribute aggregator computes

    ((Base + AddBase) * MultiplyAdditive / DivideAdditive * MultiplyCompound)
        + AddFinal

and in `GameplayEffectAggregator.cpp` the `MultiplyAdditive` modifiers are summed
with a bias of 1.0 while the `MultiplyCompound` ones are multiplied separately.
So `AddBase` is the flat bucket, `MultiplyAdditive` is the increased bucket, and
`MultiplyCompound` is the more bucket. The design's arithmetic and the engine's
are the same arithmetic.

This was checked rather than assumed, and it was checked because the opposite was
initially believed. A test builds a real `FAggregator`, feeds it the same
modifiers, and asserts the two produce the same number. That test matters beyond
this change: gear will eventually be applied as ordinary gameplay effects, at
which point the engine does the arithmetic instead, and a silent disagreement
between the two would be very hard to find.

**What the engine does not do, which is what the new class is for.**

| Rule | Why the engine cannot express it |
|---|---|
| An increase is scoped by the tags of the skill being used | An aggregator modifier filters on the source and target actors' tags, not on the ability in hand |
| Only a gem, keystone or enchantment may grant a more multiplier | The engine has no notion of where a modifier came from |
| A less multiplier cannot reach −100% | Nothing stops a modifier that zeroes or inverts a stat |

**A TAG-SCOPED STAT HAS NO SINGLE VALUE**, and that is the load-bearing
consequence. A character's area of effect is 140 with an area skill in hand and
100 with a single-target one. A plain attribute read has no skill context, so it
cannot answer the question. `Evaluate` therefore takes the skill's tags, and a
character sheet showing one number per stat will have to say which skill it is
showing.

**Percentage points here, fractions in the tuning rig.** The Python model stores
an increase as 1.25 for +125%; this class stores 125. Everything else in the
Unreal module already uses points — the resistance cap is 70.0, evasion's soft
cap is 60.0, the damage calculation divides by 100 throughout — so mixing the two
conventions inside one module would be worse than differing from the model. The
conversion happens at the boundary and a test pins a value against the model.

**An illegal modifier is ignored or clamped at runtime, never honoured.** The
Python model raises an error, which is not available while a game is running. A
more multiplier from a gear affix is **ignored**, because honouring it would
break the rule the whole split rests on. A less multiplier below −100% is
**clamped to −99%**, because ignoring it would make the stat larger than the data
asked for, while clamping keeps the direction and the invariant. Both are counted
in the value the caller gets back, so a test can prove one happened and a
character sheet can tell a player that something on their gear is being ignored.
`ValidateModifier` gives data import the reason so it can refuse the row instead.

**One limit that is documented rather than hidden.** Displayed cooldown reduction
is (divisor − 1) / divisor, which rounds to exactly 1.0 in single precision once
the divisor passes about 8.4 million, showing a player 100% while the cooldown is
still above zero. The mechanical guarantee is unaffected, because the calculation
divides. Reaching it needs roughly 34 compounding 50% sources on one stat against
six gem sockets, so it is not reachable; no display ceiling was invented, because
that is the interface work's decision.

**A gap this closed.** `FinalCooldown` in
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.cpp` divided by
the increases only. The design document has carried the more multiplier in that
formula since it was corrected earlier today; the code had not caught up.

**Affects:** no design document change. Section IV already carries the three
buckets and the cooldown formula including the more multiplier. The working model
is `game/Source/Cataclysm/AbilitySystem/CataclysmStatPipeline.h`, covered by
`game/Source/Cataclysm/Tests/CataclysmStatPipelineTests.cpp`.

---

## 2026-08-03 — The item slot vocabulary, corrected to the design's eleven slots

**The problem.** Issue #106. The Tags sheet declared 14 `Item.Slot` gameplay
tags. Section VI lists eleven gear slots plus the potion slots. Nothing compared
the two, so the disagreement sat there from the moment the tag list was first
generated.

| Tag | What was wrong |
|---|---|
| `Item.Slot.OffHand` | Section V states plainly there are no offhand items |
| `Item.Slot.Bracers` | Appears in no design document, no data sheet and no affix |
| `Item.Slot.Feet` | The design calls the slot Boots |
| `Item.Slot.Neck` | The design calls the slot Necklace |

**Nothing referenced them, which is why this was cheap.** Every cell of every
sheet was searched for `Item.Slot` references. Only the Tags sheet declares
these four, and only `Item.Slot.Weapon` is used anywhere else, by three
enchantments. So the two renames and two deletions broke no existing data.

**`Item.Slot.Potion` stays and is not part of the mismatch.** Section VI lists
four potion slots. They are consumables rather than gear and carry no affixes,
which is why `GEAR_SLOTS` in `sim/cataclysm_sim/affixes.py` leaves them out and
sums to 18 pieces rather than 22. A tag for them is still needed, because they
hold gems.

**Why a wrong tag is worse than a missing one.** A tag that does not exist fails
loudly: the generator's `--strict` mode rejects a reference to it. A tag that
exists but names the wrong thing fails silently — an affix restricted to
`Item.Slot.Feet` simply matches nothing, and no error is produced. That is the
same silent-mismatch failure the generated data tables were built to prevent.

**Two guards, because either half can drift alone.** A Python test compares the
sheet's `Item.Slot` tags against `GEAR_SLOTS`, so the vocabulary cannot diverge
from the design's slot list. An Unreal automation test names all twelve tags and
asserts the four wrong ones do not resolve, so the engine is proved to have
loaded the corrected list rather than a stale one. Both were confirmed to fail
when the condition they guard against was reintroduced.

**What this exposed and did not settle.** The design says eighteen equipped
pieces including one weapon, and also that a player may equip two one-handed
weapons, which would be nineteen. Filed as #117, because it changes the affix
budget every value in the pool was fitted against.

**Affects:** no design document change. `Cataclysm_GDD_v2.md` section VI already
lists Boots and Necklace and already states there are no offhand items; the
generated tag list was what disagreed with it. **Applied 2026-08-03** to the Tags
sheet of `All_Things_Cataclysm.xlsx`, which regenerates
`game/Config/Tags/CataclysmTags.ini` from 117 tags to 115.

---

## 2026-08-03 — What a skill is worth, in weapon damage

**The problem.** Issue #107. The design said every weapon type paired with every
damage type produces a set of skills, and never said what any of them was worth.
So every weapon damage figure in the project was really weapon-and-skill
together, and the two differed by however much a skill multiplied.

**The concept was already in the data, unsystematically.** Four of the 61
designed skills in `game/Data/WeaponSkills.csv` state a multiplier in prose:

| Skill | States |
|---|---|
| Skull Splitter | 500% weapon damage to a single target |
| Annihilator | The final hit deals 300% weapon damage |
| Bulwark | Stored damage capped at 200% weapon damage |
| Haymaker | An additional 100% weapon damage on wall impact |

So skills multiply weapon damage and the design already says so. This was not
invented, only made systematic.

**The Basic Attack is 100% by definition, and that is what makes settling this
cost nothing.** Every damage figure fitted so far — the tier 8 target of 1,681,
the affix values, what a weapon must supply — was fitted to an ordinary hit, and
an ordinary hit is the basic attack. Anchoring the scale there leaves all of it
standing and lets the other six slots multiply from it.

| Slot | Typical | Range |
|---|---|---|
| Basic Attack | 100% | fixed |
| Movement | 100% | 75-150% |
| Support | 0% | 0-100% |
| Aura | 25% per second | 15-40% |
| Special | 150% | 100-250% |
| Heavy Attack | 250% | 175-350% |
| Ultimate | 400% | 300-500% |

**The Ultimate range is derived, not chosen.** It is exactly the two designed
Ultimates: Annihilator at 300% and Skull Splitter at 500%. A test reads them out
of the data and asserts the band matches, so the two cannot drift apart.

**Weapon damage means the whole base bracket**, the weapon plus flat added damage
from gear. That is what a player reads "500% weapon damage" to mean, and it is
why flat added damage affixes are worth taking at all.

**Support's typical value is zero, not its maximum.** Most buffs, shields,
stances, curses and banners deal no damage; the range goes to 100% because a
support skill is not forbidden from dealing any, and Bulwark already does.

**What this settles elsewhere.** `weapon_base_damage_needed` in
`sim/cataclysm_sim/affixes.py` returned the weapon and the skill together and
could not separate them. It now returns the weapon, because the hit it is solving
for is a basic attack.

**Affects:** `Cataclysm_GDD_v2.md` section V. **Applied 2026-08-03:** a What a
Skill Is Worth subsection was added before Skill Acquisition. The working model
is `SKILL_SLOTS` in `sim/cataclysm_sim/character.py`.

---

## 2026-08-03 — Enemy damage fitted to what a geared character actually survives

**The problem.** Issue #108. Enemy damage had been set on the enemy's own terms,
like everything else in `enemy_stats.py`, and never checked against a player.
Every figure that claimed to check it assumed a flat "70% mitigation" rather
than computing one.

**The measurement.** A reference character was assembled from the real affix
pool — a level 100 Ravager spending all 36 prefix and all 36 suffix slots, half
on staying alive and half on killing things, on chosen bases at top tier and full
upgrade level. It reaches 11,023 health, 7,299 armour, 28% block chance, 15.9%
damage reduction and capped resistance.

| Layer | What it removes |
|---|---|
| Armour against the tier 8 curve | 53.3% |
| Resistance, at the cap | 70.0% |
| Block chance, removing half a hit | 14.0% on average |
| Damage reduction | 15.9% |
| **All four** | **89.9%** |

So a hit lands for about a tenth of itself. Against that, an average Common enemy
needed **176 hits** where the project owner had asked for 8 to 10, and the
Cataclysm Boss needed 8. Trash and elites did nothing at all.

**The fix.** `DAMAGE_AT_COMMON` went from 0.09 to 0.65 and `DAMAGE_PER_STEP` from
1.55 to 1.40. The floor rose because it was twenty times too low; the slope fell
because raising the floor alone would have made the boss a guaranteed one-shot.

| Enemy at tier 8 | Hits to kill the reference build | Seconds |
|---|---|---|
| Common Imp | 54 | 48.6 |
| Common Hellhound | 24 | 26.4 |
| Elite Brute | 10 | 28.0 |
| Herald Abyssal Warden | 5 | 12.0 |
| Cataclysm Boss Gatekeeper | 2 | 6.0 |

**The 8-to-10 target was a PACK target, and could never have been a solo one.**
One Imp cannot be both trivial alone and lethal in a group of twenty. One takes
48 seconds to kill a geared character; ten take 4.9 seconds and twenty take 2.4.
That is what makes the design's own "weak individually, overwhelming in packs"
mechanical rather than flavour.

**This reverses the direction for one number and only one.** Enemy health is
still set freely with player damage following from it. Enemy damage cannot be,
because it only means something against mitigation. That is written into
`enemy_stats.py` so the next person to change those two constants knows what they
were fitted against, and `tests/test_survivability.py` measures it so they cannot
drift.

**A bug found while doing this.** `damage.hits_to_kill` reported one hit too many
on every count. `resolve` clamps a hit to the health remaining, which is right
when reporting what one hit dealt, but `hits_to_kill` was feeding it a shrinking
health value and averaging the clamped figures, so the running total crept toward
zero instead of crossing it. A Cataclysm Boss landing 6,635 on 11,023 health
reported 3 hits when the answer is 2. Every survivability figure produced before
this was one hit too generous.

**Affects:** `Cataclysm_GDD_v2.md` section X. **Applied 2026-08-03:** a How Long
a Geared Character Survives subsection was added. The reference character is
`sim/cataclysm_sim/reference_build.py`.

---

## 2026-08-03 — Chance to apply an effect caps at 100% and overflows into magnitude

**Decision, stated by the project owner:**

> DoT chance caps at 100%, anything beyond 100% applies to the magnitude of the
> DoT's effect. So you can only ever have 1 stack of something on an enemy,
> however if you have 800% chance to apply it, it gets a 700% multiplier.

So an enemy carries at most one stack of any effect the player applies, and
chance past 100% multiplies the effect instead of being wasted.

| Chance from all sources | What happens |
|---|---|
| 60% | Applies on 60% of hits, at normal magnitude |
| 100% | Applies on every hit, at normal magnitude |
| 250% | Applies on every hit, at 2.5x magnitude |
| 800% | Applies on every hit, at 8x magnitude, a 700% increase |

**Why the overflow is not wasted.** Ailment chance comes from two sources that
both scale hard: gear affixes, and gems, where the gem applying bleed reaches
150% chance on its own at Cataclysmic rarity. Without this rule a build would hit
the cap and every point past it would be dead, so an ailment build would stop
progressing at exactly the point it was coming together.

**Why one stack rather than many.** It is what makes the overflow rule possible
at all, and it keeps a screen full of enemies readable: one enemy is bleeding or
it is not, with no stacks to count.

**WHAT MAGNITUDE SCALES DEPENDS ON THE EFFECT, and it is never wasted.** An
effect with uncapped damage takes it as damage. An effect whose strength has a
cap, such as a slow, takes it as strength up to that cap and then as duration. An
effect with no strength axis at all takes it as duration.

**Three effects were defined**, all of which were already applied by a gem and
by an affix while saying nothing about what they did. The project owner gave the
shape of each; the numbers below were chosen and are expected to move.

| Effect | What it does | Magnitude scales |
|---|---|---|
| Madness | The enemy attacks anything nearby, friend or foe, for 3 seconds | The duration |
| Cripple | Reduces the enemy's movement and attack speed by 30% for 4 seconds | The reduction, to a cap of 80%, then the duration |
| Shred | Reduces the enemy's resistance by 10 for 6 seconds | The reduction, until that resistance reaches zero, then the duration |

Cripple's slow caps below total because a full stop is a stun, and stunning is a
separate mechanic with its own counter in Crowd Control Resistance. Shred stops
at zero resistance for the same reason armour penetration does: reducing a
defence below nothing grants no bonus.

**Necrosis was changed to fit the rule.** `game/Data/StatusEffects.csv` described
it as "a stacking dot that reduces healing by 10% per stack", which the
single-stack rule rules out. It now reduces healing by 25% and deals damage over
time for 5 seconds, in one application, with magnitude scaling both.

**These rows are generated, not hand-written.** `game/Data/StatusEffects.csv`
comes from `docs/All_Things_Cataclysm.xlsx` via `tools/generate_datatables.py`,
so the workbook was edited and the CSVs regenerated. The other eight CSVs came
out byte-identical, which is the evidence that reading and rewriting the workbook
damaged nothing. The row count assertion in
`game/Source/Cataclysm/Tests/CataclysmDataTableTests.cpp` moved from 46 to 49.

**Weaken and Wither are two different effects**, decided by the project owner
2026-08-03. Weaken is applied by the player and reduces an enemy's damage by 20%
for 5 seconds; magnitude raises the reduction to a cap of 80% and then extends
the duration. Wither is applied by an enemy to the player and reduces the
player's movement and attack speed, which is unchanged. Cripple is the player's
equivalent of Wither, so Weaken taking damage rather than speed keeps the two
from overlapping.

Weaken's cap has the same reason Cripple's does: an enemy that deals no damage is
harmless, which is a stun by another name, and stunning is a separate mechanic
with its own counter.

**Necrosis was given a gem.** It was the one effect in
`game/Data/StatusEffects.csv` that nothing applied, while every other named a
gem, an enchantment, an enemy modifier or a dungeon modifier. The project owner's
reading was that the gems sheet was simply incomplete, so the Of Wasting gem was
added with a 10% chance to apply necrosis, on the same rarity curve as Of
Rending, the other 10% damage-over-time gem, so a new gem does not arrive
stronger than its peers. A matching affix was added, because an import-time check
requires every gem-applied effect to be reachable as an affix as well.

**Tuning expected.** The project owner: "That might need tuning later, but I
think that's how I want it to work."

**Affects:** `Cataclysm_GDD_v2.md` section IV. **Applied 2026-08-03:** an
Applying Damage Over Time and Other Effects subsection was added after Overwhelm.
The working model is `ailment_application` in `sim/cataclysm_sim/affixes.py`.

---

## 2026-08-03 — The affix pool: prefixes, suffixes and implicits

**Decision.** The affix pool grows from 7 entries to 35 stat affixes plus the
three resistance families, split into prefixes and suffixes, with an implicit on
every item base.

**Prefixes and suffixes are separate pools, two of each per piece.** All three
games surveyed — Path of Exile, Last Epoch and Torchlight Infinite — do this.
Without it, four affix slots means four of whatever is strongest and one item can
carry a whole build. With it, every piece gives something up.

Prefixes carry magnitude: how big a character's numbers are. Suffixes carry rates
and qualifiers: how often, how fast, how much gets through. A stat appearing in
both would let one item hold four of it, which is what the split exists to
prevent, so an import-time check rejects that.

**THE IMPLICIT BELONGS TO THE BASE, NOT THE SLOT.** A first version put one
implicit on each slot. The project owner corrected it: every category of gear has
several bases, and each base has its own implicit. A chest is not one item with
one inherent stat, it is a choice between a chest built for armour, one built for
evasion, one built for health and one built for energy shield.

That is where most of the interest in gearing lives. A player who wants evasion
is not waiting for an evasion affix to roll; they are looking for an evasion
base, and every base they pick is a defensive layer committed to before any affix
is involved.

There are **55 bases across the 11 slots**, at least three per slot, because one
base in a slot is not a choice. Two bases granting the same implicits would be
one base written twice, so that is rejected as well.

**A weapon base carries two things no other item has:** a physical sub-type from
the design's Weapon Sub-Types table, and a number of damage type slots. There is
a base for each of the fourteen weapon types the design lists, and all four
sub-types are reachable.

**Which damage types fill those slots is not a property of the base.** Loot is
biased toward the Cataclysm being fought, so the types are decided when the item
drops. The base says only how many.

**A one-handed weapon holds two damage types and a two-hander holds three**, so
two one-handers hold four against a two-hander's three. That is what makes dual
wielding the primary route to multiclassing the design says it is, since every
damage type unlocks that type's three class trees, while the two-hander stays
ahead on raw damage.

**The Shield is the one weapon whose base defends.** The design lists it among
the one-handed weapon types and states there are no offhand items, so it is a
weapon with nowhere else to be. The rule that a weapon defends nothing therefore
applies to AFFIXES only: what a base IS may be defensive, what a drop happened to
roll on a weapon may not. A check confirms no other weapon base defends, so the
exemption stays one named exception rather than a hole.

**Hybrid affixes grant two stats at 70% each.** That ratio is read off the
two-resistance affix against the single-resistance one rather than written twice,
so the whole pool moves together if it changes. A hybrid is worth 1.4 affixes
spread over two stats against a single affix's 1.0 concentrated in one, so it
wins a slot when a build needs both and loses when it needs one badly.

**Ailment affixes apply the effects the gems already grant.** `Gems.csv` designs
eight gems that apply an effect on hit — bleed, poison, disease, void splinter,
madness, cripple, weaken and shred — and the project owner asked for the same
effects to be reachable as affixes, on weapons above all. They roll on weapons,
necklaces, relics and rings only, because an ailment only makes sense where a hit
comes from.

The gem stays the stronger source: the gem applying bleed reaches 150% chance at
Cataclysmic rarity against the affix's 15% at top tier, so a socket is still
where an ailment build lives. Having both means a build can chase an ailment two
ways, and one that wants it badly can do both.

**There are no attribute affixes, and that is deliberate.** The design gives one
attribute point per level, plus the Maw, which consumes items and enemies for
them. Gear granting attribute points appears nowhere, so an affix for it would be
adding a mechanic rather than filling the pool.

**How the values were set.** Not one formula, because the stats are not on one
scale. Three anchors, and each affix records which it used:

| Anchor | Used for | Example |
|---|---|---|
| Against the class base | Stats a class already has; top value about 6% of the level 100 figure | Mana, 38 against a base of 644 |
| Against the requirement | Stats whose class base is near zero but whose endgame requirement is large | Armor, 250 |
| By convention | Percentages with no base at all, anchored on how many slots should reach a useful figure | Evasion, 4 points a piece so fifteen slots reach the soft cap |

Armor is the one place the first two anchors disagree enough to matter. A Ravager
has 371 armor, but the armor curve divides by 800 times the difficulty tier, so
6,400 armor is worth half damage taken at tier 8 and 371 is worth 5%. Six percent
of the class base would be 22 per affix, which fifteen slots could never turn
into anything. That is exactly what the design means when it says armor earned
early does not keep its value and gear has to carry it.

**A gap this work found and fixed.** Shoulders had been left out of the defensive
slot list. It was an oversight rather than a decision — shoulders are armor — and
without it the slot could roll nothing but resistance and energy shield, leaving
it unable to fill its own four affix slots. The check that every slot can fill
both its prefix and its suffix slots is what found it.

**Affects:** `Cataclysm_GDD_v2.md` section VI. **Applied 2026-08-03:** subsections
added for prefixes and suffixes, implicits, and what affixes do not grant; the
slot restriction table corrected for Shoulders. The working model is
`sim/cataclysm_sim/affixes.py`, covered by `sim/tests/test_affixes.py`.

---

## 2026-08-03 — The multiplicative bucket, and gear level multiplying affixes

**Background.** The project owner asked for research into how Path of Exile,
Last Epoch and Torchlight Infinite calculate damage, and their affix pools. All
three use the same skeleton with different names:

    (Base + Added) x (1 + sum of all increases) x More1 x More2 x ...

The additive bucket has diminishing returns and each multiplicative source does
not. That gap is what makes gearing a puzzle: the question a player answers is
which independent multiplier they are missing, not which number is biggest.

**This was already in the design document and was never implemented.** Section
IV has carried `Final Value = Base Value x (1 + Sum of Increases) x Product of
More Multipliers` all along, and reserved the "more" wording for enchantments and
keystones. `sim/cataclysm_sim/character.py` implemented only the first two
brackets. This entry records the implementation and the decisions made alongside
it, not the invention of the rule.

**Decisions taken.**

**Terminology is flat, increased and more**, chosen by the project owner, and the
same words Path of Exile and Last Epoch use.

**Gems join keystones and enchantments as multiplicative sources.** The design
document named only enchantments and keystones. Ordinary gear affixes are still
excluded, which `MORE_SOURCES` enforces: an affix is flat or increased and never
more. That keeps a rare drop readable and gives the 961 designed enchantments a
job ordinary affixes cannot do.

**A more multiplier is scoped by tag exactly as an increase is**, so a gem
granting more area damage does not help a single-target skill.

**A more multiplier divides for cooldown reduction rather than multiplying.**
Cooldown reduction is a rate: an increase makes the interval shorter, so a more
source has to as well, or a cooldown reduction gem would lengthen the cooldown.
Because both buckets divide, no number of them reaches zero, which is why the
stat still needs no cap.

**A less multiplier cannot reach -100%**, or one source could zero a stat
outright or invert it.

**Damage conversion is not needed and will not be built.** Player damage is
adaptive: a weapon deals one damage number rather than one pool per damage type.
See the separate entry on enemy resistance, which follows from the same decision.

**GEAR UPGRADE LEVEL MULTIPLIES EVERY AFFIX ON THE PIECE**, using the factor
already in the Power Score model rather than a second copy of it. A +10 piece
gives about 3.52 times what the same piece gives at +0. Affix values stated
anywhere are therefore the +10 figures.

**A known imbalance, raised and accepted.** Gear level multiplies both brackets
of the pipeline at once, so its effect on final damage is roughly squared, while
Power Score counts it once. Measured with everything else held at tier 8
maximum:

| How gear level applies | Damage growth from +0 to +10 | Power Score growth |
|---|---|---|
| Every affix, as chosen | 9.58x | 1.56x |
| Flat and increased, weapon fixed | 4.46x | 1.56x |
| The flat bracket only | 1.64x | 1.56x |
| Every affix, factor cut to 0.0268 | 1.56x | 1.56x |

Hits to kill a Common enemy fall from 19.1 to 2.0 across that range, so gear
level is over-rewarded relative to what a character is rated at. The project
owner chose every affix anyway and to tune it against real play: "We'll figure
out how to make it work, for now let's just continue forward. Numbers and stuff
can be changed once we have a working prototype and can see how it plays." The
measurement is recorded here so it is findable when that tuning happens.

**Affects:** `Cataclysm_GDD_v2.md` section IV. **Applied 2026-08-03:** gems added
to the list of "more" sources, the diminishing-returns comparison table added,
the cooldown formula corrected to include the more multiplier, and the gear level
rule stated. The working model is `sim/cataclysm_sim/character.py`, covered by
`sim/tests/test_character.py`.

---

## 2026-08-03 — Player damage is adaptive, so an enemy has one resistance

**Decision.** Player damage is **adaptive**: a weapon deals one damage number
rather than one pool per damage type. There is no damage conversion mechanic and
none is needed. The project owner's reason: a weapon carrying eight damage types
would be unworkable to calculate damage on.

**The consequence, which the project owner raised themselves.** This contradicts
the rule recorded the same day that enemies resist and are weak to specific
damage types. Once player damage adapts, a per-type enemy profile changes no
outcome, so it is authoring work that buys nothing. Enemies now have **one
resistance figure applied to all incoming damage**.

| Enemy | Resistance |
|---|---|
| Imp | 0% |
| Hellhound | 10% |
| Succubus | 10% |
| Brute | 15% |
| Corrupted Sentinel | 20% |
| Abyssal Warden | 35% |
| Gatekeeper | 30% |

The Abyssal Warden is highest because the design describes that one, and only
that one, as having high damage resistance.

**The player still has all eight resistances defensively.** Unchanged and
unrelated: eight Cataclysms attack the player. An enemy still has a damage type
of its own, which decides which of the player's eight applies when it hits them.

**Enemy resistance is what player resistance penetration works on**, and
penetration beyond an enemy's resistance grants no bonus, so over-stacking it
cannot become a damage multiplier against the enemies that need it least.

**A guard replaces the one that is gone.** Enemy resistance is a single unbounded
number now, so nothing else caps it. An import-time check rejects any archetype
at or above 70%, which is where the design caps resistance, because the design
states plainly that no combination of defensive layers reaches immunity.

**Affects:** `Cataclysm_GDD_v2.md` section X. **Applied 2026-08-03:** the
per-damage-type table was replaced with a single-resistance table and the rule
above. The working model is `sim/cataclysm_sim/enemy_stats.py`.

---

## 2026-08-03 — The gear affix pool, and where its numbers come from

**Decision.** The ordinary affix pool now exists. Issue #79 recorded that 961
enchantments were designed and not one ordinary affix, so gear granted no stats
at all while the Power Score model assumed gear supplies half a character's
power.

**Seven tiers, on a linear curve.** Tier N is worth N/7 of the affix's top
value. Linear rather than front-loaded, because a front-loaded curve hands over
most of an affix's value in the first few tiers and makes the later ones easy to
skip — which relieves exactly the pressure the design wants to keep applying,
that a day at the forge is a day not defending the empire. The cost side already
curves, since gear upgrade levels cost 2^N − 1 stones, so diminishing returns
arrive through rising cost rather than falling value.

**Every tier is a range, reaching 25% below its top.** Without ranges two
crafting materials are dead content: the Corrupted Mote rerolls an affix value
and the Primal Spark perfects a roll, and neither means anything if a tier has
one value. A first version measured the band against the gap between tiers
instead of against the affix's own value, which made a perfect set of resistance
affixes save about one slot out of 72 — not a difference anyone would craft for.

**Bands overlap by exactly one tier, and that is a choice rather than an
accident.** A perfect T6 roll can beat a poor T7 one. With seven tiers there is
no way to have both non-overlapping bands and rolls large enough to change a
build, because a band worth caring about is necessarily wider than the gap
between tiers. The bound is provable rather than tuned: a tier's floor is 0.75 of
its own fraction, so tier N is undercut by tier N−1 only when N is above 4 and by
tier N−2 only when N is above 8, which seven tiers cannot reach.

**Three resistance families rather than one**, at the project owner's proposal:
one resistance at 20%, two at 14% each, all eight at 6% each. The efficient
family changes as a run goes on, because each difficulty tier adds a Cataclysm,
so the number of resistances that matter grows from one to eight. That
progression is the whole reason for having three.

**Health and damage come in a flat and an increased kind**, entering the two ends
of the stat pipeline. Neither is strictly better: a flat affix is multiplied by
every increase already present, and an increased affix multiplies every flat
point already there. So flat wins early in a build and increased wins later.

**Flat damage is 18, and it is derived rather than picked.** The project owner
set increased damage at 125%, which is what forces the flat side to be small: a
character with six increased damage affixes multiplies by 8.5, so the bracket
they multiply has to stay near 200 at tier 8. 18 is the value that puts the
crossover between the two kinds where a real build actually crosses it, after
about two flat affixes. Both neighbouring values fail that test. At 60, the
first value tried, the crossover is 528 and no build reaches it, so flat wins
always. At 12 it is 106, below where a build starts, so increased wins always.
Either way one of the two kinds is dead content.

**The damage target is read off the enemy statistics, not chosen.** An average
Common enemy at tier 8 has 3,362 effective health and should take 2 non-critical
hits to kill, giving 1,681 damage per hit. A Common enemy is the right anchor
rather than a boss, because the spread between them is 117 times and no single
hits-to-kill figure suits both; trash is what the player fights almost all of the
time.

This ordering matters. A first version took a player damage figure that had been
derived backwards from player-side targets, and under it the 125% increased
damage affix the project owner wanted was eight times what the target could
absorb. Setting the enemy side first and reading the target off it resolved that
rather than deferring it.

**Affixes are restricted by gear slot.** Damage goes on the weapon, rings, relic,
necklace and gloves; health on head, chest, belt, pants, boots and rings;
resistance on everything except the weapon. Without restrictions every slot is
interchangeable and gearing has no puzzle in it. Rings take every kind on
purpose: there are eight of them, so they are the flexible slots a build uses to
fix whatever it is short of.

**Two things this exposed and did not settle.** Skills have no damage multiplier
anywhere in the project, so every weapon damage figure here is really weapon and
skill together (#107). And enemy damage has never been checked against how much
health a geared player has: a Common enemy takes 92 hits to kill a fully geared
Ravager at 70% mitigation, against a target of 8 to 10 stated early in this work
(#108).

**Affects:** `Cataclysm_GDD_v2.md` section VI. **Applied 2026-08-03:** an Affixes
subsection was added covering tiers, ranges, the three resistance families, the
health and damage pairs, the damage target and the slot restrictions. The working
model is `sim/cataclysm_sim/affixes.py`, covered by `sim/tests/test_affixes.py`.

---

## 2026-08-03 — Enemy stat blocks: rarity, archetype, and no enemy penetration

**Decision.** Enemy Score is a power rating and says nothing about statistics.
Two layers turn it into a stat block, and they own different things.

| Layer | What it sets |
|---|---|
| Rarity | Magnitude only: health, damage, armor, energy shield |
| Archetype | Attack interval, criticals, movement, evasion, shield fraction, resistances, and how big this kind of creature is relative to average |

**Rarity scales magnitude and nothing else.** An earlier version of this model
put attack interval, criticals, movement and resistance on the rarity, which
said a Cataclysm Boss winds up more slowly than a Common enemy purely because it
is rarer. Winding up slowly is a statement about what kind of creature something
is, not about how large it is, so it belongs to the archetype. Under the split, a
Legendary Imp is a bigger Imp rather than a different animal, and an Elite
Succubus and an Elite Brute share a score and share nothing else.

**Armor is not forced to zero at Common.** The earlier model gave Common enemies
no armor as a rarity rule, which contradicts the design's own Brute, described as
heavily armored. Whether a creature has armor is the archetype's call; the Imp's
share is zero and the Brute's is high, at every rarity.

**Health grows faster than damage: 1.85 per rarity step against 1.55.** Across
the six rarities that is roughly 23 times the health and 9 times the hit. Growing
both at the same rate produces something unkillable and instantly lethal at once,
which is a wall rather than a fight.

**Damage growth was raised from 1.21, and attack interval no longer rises with
rarity.** At 1.21 a Cataclysm Boss hit was 2.8 times a Common enemy's, and
because attack interval also rose with rarity the two nearly cancelled: damage
per second across the whole ladder grew only 1.2 times. The rarest enemies in the
game were therefore not frightening. Attack interval is the archetype's now, so
nothing cancels the damage growth.

**Enemies carry no Penetration stat. Overwhelm already does that job.** Giving
each rarity a penetration figure was the same mechanic written twice, at roughly
double the size and disagreeing with the original. Measured at tier 8 against a
player at that tier's maximum Power Score:

| Rarity | Per-rarity penetration, now removed | Overwhelm, already present |
|---|---|---|
| Common | 0% | 8.4% |
| Herald | 15% | 12.2% |
| Cataclysm Boss | 25% | 20.9% |

Overwhelm is the better of the two copies for two reasons. It shrinks to nothing
as the player out-powers the content, where a fixed per-rarity number punishes
forever no matter how well geared. And it strips every kind of mitigation rather
than only resistance, so an armor or block or evasion build cannot sidestep it.
Over-capping resistance keeps its purpose and gets a cleaner one: the headroom
above 70% is exactly what Overwhelm eats into. The player's own offensive
Penetration stat is unaffected, and an enemy modifier may still grant penetration
as a specific effect.

**Overwhelm was in no design document.** It existed only in
`sim/cataclysm_sim/combat.py`, where it has been since the first commit of the
simulation, and the game design document still described enemy penetration
scaling instead. That is why the duplicate was written in the first place.

**Negative resistance is legal and means damage taken is increased.**

**An enemy's resistances say what it is made of and how it fights, and never
which Cataclysm it belongs to.** A first version had every enemy of a Cataclysm
resist its own damage type by 40% and take extra damage from the opposing one.
The project owner rejected it, and the objection is structural rather than a
matter of tuning.

Section IV of the game design document states that the active Cataclysm
determines the player's damage type: loot is biased toward weapons tuned to it,
and weapon damage type is what unlocks skills and class trees. A run also begins
with one Cataclysm and adds another each time one is defeated. So:

| | Cataclysms active | Damage types the player can hold | Enemies resisting their damage |
|---|---|---|---|
| First run | 1 | 1 | 100% |
| Eighth run | 8 | up to 8 | 1 in 8 |

That is a flat 40% damage loss against every enemy in the game in the first run,
with no counterplay available, because a second damage type cannot be obtained
until a Cataclysm has already been beaten. It then eases off as the player gets
stronger. The rule made the game hardest exactly where the player has the fewest
options, which is the difficulty curve running backwards.

**The rule that replaced it:** an enemy's resistance profile must not mention its
own Cataclysm's damage type in either direction. Resisting it is a tax the player
cannot avoid; being weak to it is a bonus they cannot miss. Neither is a
decision. What is left is material and role: a construct resists what kills and
sickens living things, armored flesh turns blades, a creature of the mind resists
madness and is fragile in melee. Two enemies in the same Cataclysm can then want
opposite weapons, which the Brute and the Succubus deliberately do.

**The known cost, accepted.** Nothing in the vertical slice resists Demonic
damage except the Gatekeeper, so the player's resistance penetration stat has
exactly one target in the first run. It grows into relevance as later runs add
Cataclysms and the player carries more damage types. The Gatekeeper resists
everything it is allowed to and has no weakness at all, so the last fight has no
cheap answer and penetration is the counter.

**Enemy evasion is answered by area damage**, which the design already says
evasion cannot avoid. So an evasive enemy is a reason to bring area damage rather
than a flat tax on the player's output, and no accuracy stat is needed.

**What this does not settle.** Enemy abilities: the Hellhound's fire trail, the
Brute's stomp stun, the Gatekeeper's phases and the Abyssal Warden's positional
weak points are behaviour rather than statistics, and belong with the enemy
design work in issues #29 and #39.

**Affects:** `Cataclysm_GDD_v2.md` sections IV and X. **Applied 2026-08-03:** an
Overwhelm subsection was added to section IV, an Enemy Stat Blocks subsection to
section X, and the three places that described enemy penetration were corrected.
The working model is `sim/cataclysm_sim/enemy_stats.py`, covered by
`sim/tests/test_enemy_stats.py`.

---

## 2026-08-02 — Enemy modifiers versus dungeon modifiers

**Decision.** The two are separate systems and behave differently.

| | Dungeon modifiers | Enemy modifiers |
|---|---|---|
| Applies to | A whole dungeon | One individual enemy |
| How many | One per difficulty tier, doubled for Sacrificial | One per rarity above Common |
| Carries a score | **Yes** | **No** |
| Source table | `DungeonModifiers.csv`, 116 rows | `EnemyModifiers.csv`, 79 rows |

**Common enemies carry no modifiers at all**, because the count is one per rarity
*above* Common.

**Enemy modifiers deliberately do not change an enemy's score.** They are
mechanical effects rather than stat increases: a burning aura deals its own
damage, and a charm stops the player dealing damage for a few seconds. Scoring
them as well would count the same difficulty twice, once in the effect and once
in the larger health and damage pool a higher score produces after the conversion
recorded in issue #97.

Dungeon modifiers are the opposite case and do carry a score. An environmental
effect applies to everything inside the dungeon, so a score is the only way its
difficulty is expressed at all.

**The two data tables already reflect this.** `DungeonModifiers.csv` has a Weight
column on all 116 rows, taking values of 5, 10, 15 or 20, and the simulation
already sums the weights of a dungeon's modifiers into the Modifier Score.
`EnemyModifiers.csv` has no weight column. That asymmetry was first read as a
gap in the enemy table and filed as issue #99; it is the design.

**Why this was written down.** Neither rule was in the design document. The
dungeon rule was only visible in `sim/cataclysm_sim/engine.py`, which implements
it, and the enemy rule was nowhere at all — it was stated by the project owner
after a wrong inference from the dungeon rule was applied to enemies.

**Affects:** `Cataclysm_GDD_v2.md` sections VIII and X. **Applied 2026-08-02:** a
Dungeon Modifiers subsection was added to section VIII and an Enemy Modifiers
subsection to section X, each stating the count, the scoring behaviour, and why
the two differ.

---

## 2026-08-02 — The damage calculation

**Decision.** One incoming hit resolves in this order: evasion, block, armor,
resistance, flat damage reduction, mana, energy shield, health.

The design named every defensive stat and never said how any combined. The
consequence was concrete: the Ritualist's 832 energy shield and the Ravager's
371 armor were both declared, replicated, and completely inert, because nothing
read them.

**Armor uses a curve, not a subtraction.** `armor / (armor + K)`, K being 800
times the difficulty tier, capped at 75%. The curve never reaches 100%, so no
amount of armor is immunity, and it has natural diminishing returns. K rising
with tier is what stops armor earned early from keeping its value forever: 371
armor is worth 32% at tier 1 and 5% at tier 8, so a class identity built on armor
holds early and gear has to carry it later.

**Penetration is applied before the 70% resistance cap, not after.** The most
load-bearing choice in the whole calculation. Against 30 penetration a character
at 100 resistance still sits at the cap, while one at exactly 70 drops to 40.
Capping first would make every point above 70 worthless and would contradict the
design's own allowance for over-capping via affixes. This is the reason the cap
is soft rather than hard.

**Armor penetration and resistance penetration are separate stats.** The
enchantment tables already treat them separately, granting armor-ignoring on
skills, on critical hits, on traps and on first hits. Piercing adds its 20% on
top of whatever gear provides, up to all of a target's armor.

**Blunt stuns instead of doing bonus damage against armor.** Its original
property put it in direct competition with piercing, which already beats armor
and has at least six affixes scaling it, while nothing anywhere scales damage
against armored targets — blunt was a flat 10% with nowhere to grow. It now has
a 10% chance to stun for 0.75 seconds, deliberately the shortest duration any
designed skill uses, so a sub-type that stuns on every hit does not outclass
skills whose whole purpose is stunning. Crowd control resistance reduces the
chance proportionally. An evaded hit never stuns; a blocked hit still can,
because a block reduces damage rather than preventing contact.

Stun was already a designed mechanic rather than a new one: `Keyword.CC` is a
generated gameplay tag, several War skills stun for 0.75 to 3 seconds, and one
ultimate grants immunity to it.

**Energy shield is a distinct defence, not a second health bar.** Four of its
rules were already designed and sitting only in the generated enchantment tables,
stated in no design document. An enchantment that removes a property proves the
property exists by default, which is how they were found:

| Rule | Where it was hiding |
|---|---|
| Does not absorb damage over time | `EnchantmentsNegative.csv` line 165, "Energy shield can now be effected by bleed" — only a drawback if it normally is not |
| Has a recharge delay | `EnchantmentsPositive.csv` line 118, "regeneration begins immediately after taking damage with no delay" |
| Recharges toward a maximum that can be capped below full | `EnchantmentsNegative.csv` line 89 |
| Being broken is a distinct event | A set bonus that triggers on it |

The recharge delay is **3 seconds after the character last took damage, restarted
by taking damage again inside that window**. Damage over time restarts it too.
That last part matters: the shield already absorbs no damage over time, so
without it a bleeding character would keep refilling their shield and energy
shield would be strongest against exactly the damage it ignores. With it, damage
over time bypasses the shield and holds it empty, which is a counter rather than
a stat check.

**Damage over time can be routed to mana before health**, from a positive
enchantment, so it is off by default and is a mana-stacking build choice. Mana is
applied before the shield, so a character with both sees mana take it first.

**No combination of layers reaches immunity.** Every one has either a cap or a
curve that cannot reach zero damage. A test asserts it.

**Still absent, and not blocked by this.** There are no enemy damage numbers
anywhere in the project. The whole calculation answers "of a hit of X, how much
reaches health" and never "how big is a hit".

**Affects:** `Cataclysm_GDD_v2.md` sections IV and VI. **Applied 2026-08-02:** a
Damage Calculation subsection and an Energy Shield subsection were added, and the
Weapon Sub-Types table entry for Blunt was changed. The working model is
`sim/cataclysm_sim/damage.py`, covered by `sim/tests/test_damage.py`.

---

## 2026-08-02 — Stat lines for the three Demonic classes

**Decision.** Ravager, Ritualist and Masochist each get a stat line. The vertical
slice needs all three, because a damage type unlocks all three of its class
trees, so shipping one would leave two of the three classes a player can select
visibly empty.

The design document gave one sentence per class. Everything else here was
proposed and reviewed.

**The method, taken from the three War trees that exist as data.** Each of them
commits to three or four stats and ignores the rest: the Bulwark to health, armor,
block and retaliation; the Berserker to resource, damage, critical strikes and
leech, with almost no armor and no evasion at all; the Saboteur to deployables
and evasion, with no armor, no crit and no leech. A class is defined as much by
what it refuses as by what it takes. Each Demonic class leaves most of the 33
stats at the default line deliberately.

**Ravager: the consistent fighter, not a second Berserker.** The Berserker
already occupies angry melee and wins through critical strikes and leech while
being deliberately fragile. Making the Ravager a bigger version of that would
make one of them redundant.

So the Ravager is the one that cannot be stopped rather than the one that hits
hardest. In the project owner's words: where the Berserker is a shock troop, the
Ravager is the more consistent fighter. It takes the most armor of the three,
flat damage reduction, enough leech to hold a line, crowd control resistance, and
the fastest movement so it is always in contact. It refuses evasion and energy
shield entirely.

**Ritualist: the caster, and the only one with an energy shield.** The Saboteur
already covers deployables, but it deploys objects that sit where they are put.
The Ritualist commands things that were alive, and in the case of possession
things that still belong to the enemy.

It has the frailest health of the three at 1,060 before gear, roughly half the
Ravager. That is deliberate and was queried during review: attributes, gear and
multiclassing all scale it, so the low base is a starting position rather than a
ceiling. It is the class the energy shield rule points at — give it to classes
that thematically warrant it, such as casters.

**Masochist: wants to be hit, which makes the usual defences work against it.**
It has the largest health pool and by far the largest regeneration, because for
this class health regeneration is resource regeneration. It takes retaliation and
low armor and refuses evasion and energy shield: evading is missing out, and a
shield absorbs the damage the class needs to convert.

It keeps a normal mana pool. "Uses HP instead of mana" is delivered by a passive
tree node converting mana into health, so the conversion is a build choice rather
than a starting condition.

**Class resource behaviour is deliberately not decided here.** Only pool size is
set. What a resource does, how it builds and how it decays belongs with the
passive trees in issue #63, and naming them was left to that work as well.
Suggested shapes were carried into that issue: the Ravager building while in
melee contact and decaying out of it, the Ritualist reserving rather than
spending so its ceiling is how much it can hold under control, and the Masochist
building from damage taken. Each differs from all three War resources, which
build up and are spent.

**Nothing here is calibrated against the Bulwark.** Its tree is written against
Maximum HP thresholds up to 25,000, and it is a dedicated tank at full
investment. None of these three approaches that from base values, and a test in
`sim/tests/test_classes.py` asserts it stays that way.

**Not validated against combat.** There are still no damage numbers anywhere in
the project, so none of these values has been checked against what an enemy
actually does. They are internally consistent starting values for testing.

**The remaining 21 classes have no stat line** and use the shared default until
they are designed.

**Affects:** `Cataclysm_GDD_v2.md` section IV. **Applied 2026-08-02:** a Demonic
class stat line table and the reasoning for each class were added to the Class
Stat Lines area. The working model is `sim/cataclysm_sim/classes.py`, covered by
`sim/tests/test_classes.py`.

---

## 2026-08-02 — The character sheet, where bases come from, and tag scoping

**Decision.** A character has **33 stats** in five groups, matching the Stat tag
categories already generated into `game/Config/Tags/CataclysmTags.ini`. The full
list is now in `Cataclysm_GDD_v2.md` section IV.

**Attributes only ever scale.** There is one way an attribute point acts: it adds
to that stat's sum of increases, and the sum multiplies a base. There is no
second kind of attribute effect.

A proposal to add one was made and rejected. It came from a wrong diagnosis: nine
of the seventeen attribute effects appeared to produce nothing, and that was read
as the per-point values being broken. They were not. A stat with no base
correctly gains nothing from its attribute. The zeroes were in a placeholder stat
line in the simulation, not in the design.

**Every stat's base comes from one of three places:**

| Source | Stats |
|---|---|
| The class | Vitals, recovery, defences, resistances, movement speed, area of effect, damage over time frequency |
| The equipped weapon | Attack speed, and off the sheet, attack range and attack damage |
| The skill being used | Critical strike chance, and off the sheet, base cooldown, projectile count and duration |

**A class does not need a base above zero for every stat**, only for every stat
it wants its attributes to scale. Declining to give a stat a base is how a class
declines to care about it.

**Critical strike chance belongs to the skill.** Each skill carries its own base
chance and the character's gear and attributes scale it. A character has no
critical strike chance in the abstract. This is what makes the attribute worth
having: read as a class stat with a 5% base, Ferocity moved it only to 7.5%
across a character's whole budget.

**Area of effect and damage over time frequency belong to the character**, even
though both concern skills. The character holds one percentage that applies to
every skill tagged for it. Their baseline is 100%, not zero, because they are
percentages of whatever the skill itself does.

**Movement speed is in metres per second**, a tank at roughly 3, scaled as
`3 * (1 + increases)`.

**Increases are scoped by gameplay tag.** Every skill carries tags, which is how
the game knows which enchantments and effects apply to it. The character holds
all of its own increases, and an increase reaches a skill when the tags match.
An item granting increased area of effect is not a property of one skill; the
character holds it and it applies to everything tagged for area.

Matching is hierarchical: a modifier requiring `Type.AOE` applies to a skill
tagged `Type.AOE.PointBlank`. `Scope.Global` matches everything. A modifier
requiring several tags needs all of them.

This uses structure the design already had. The Weapon Skills sheet tags every
skill and both enchantment tables tag every enchantment.

**Class stat lines share a default.** 24 classes times 33 stats times two numbers
each is 1,584 values, so every class starts from one shared default line and
overrides only the stats that express its identity. A class may override any
stat.

**Per-level scaling is linear, provisionally.** Whether it should stay linear is
not settled and will be decided by testing rather than argument.

**Movement speed at three times base from full Agility is accepted.** 100 points
of Agility triples movement speed, reaching 12 metres per second from a base of
4. Flagged as possibly too large, and accepted: how it feels in game is the real
test. Recorded so the number is a decision rather than an oversight.

**No attribute per-point value changed.** Every number in the attribute table is
as originally written.

**Still open.** Luck gives +0.01% rarity find per point, which is +1% across a
character's entire budget. Issue #81.

**Affects:** `Cataclysm_GDD_v2.md` section IV. **Applied 2026-08-02:** four
subsections were added covering the sheet, the three base sources, tag scoping
and class stat lines. The working model is `sim/cataclysm_sim/character.py`,
covered by `sim/tests/test_character.py`.

---

## 2026-08-02 — Attribute scaling, caps, and how avoidance works

**Decision.** The attribute table gave every attribute as a percentage per point
without saying what the percentage applied to or how sources combined. These are
the rules.

**Attributes scale, they do not create.** Health, mana and energy shield come
from class base values, per-level scaling, and flat values from gear. Vitality's
+2% HP multiplies that result. Those base values are still undesigned; see
issue #77.

**Increases are additive within one bucket per stat, applied once:**

```
Final Value = Base Value * (1 + Sum of Increases) * Product of More Multipliers
```

Attribute points and gear affixes worded "increased" share one bucket. Only
"more" and "less" multiply separately, and that wording stays reserved for
enchantments and keystones.

**Why additive rather than compounding.** At 2% per point compounding, 100
points of Vitality is 7.2 times health. Additive it is 3 times. The Power Score
ranges per tier rise about 16 times in total from tier 1 to tier 8, so a single
attribute producing 7 times on its own leaves no room for gear.

**Regeneration percentages are increases to a base rate, not percentages of the
maximum.** `Final Regeneration = Base Regeneration * (1 + Sum of Increases)`.
Read literally, 50 points of Vitality would return half a character's health
every second.

**Cooldown reduction divides rather than subtracts:**

```
Final Cooldown = Base Cooldown / (1 + Sum of Increases)
```

The skill supplies the base cooldown. The interface shows the effective
reduction, `Increases / (1 + Increases)`, so a character shown at 25% reduction
turns a 4-second skill into a 3-second one.

**Why division.** Efficacy gives +1% per point, so subtracting would reach zero
cooldowns at 100 points. Dividing, 100 points halves every cooldown, gear pushes
further with each point worth progressively less, and zero is unreachable. The
alternative considered was subtraction with a lower per-point value and a hard
cap; it was rejected because it creates a dead zone where every further Efficacy
point and every cooldown affix is worth nothing.

Damage-over-time frequency uses the same form, being a rate. Area of effect
stays additive.

**Caps:**

| Stat | Cap | Hard or soft |
|---|---|---|
| Resistances | 70% | Soft — affixes may raise the cap |
| Evasion | 60% | Soft — gear enchantments may exceed it |
| Crit chance | 100% | Hard |
| Block chance | none | No cap |
| Cooldown reduction | none | No cap needed; the formula cannot reach zero |

**Avoidance works two different ways, and the design document did not say so.**

- Evasion avoids an attack completely but applies only to direct attacks. Area
  damage lands regardless. This is why its cap can be soft: even at 100%
  evasion a character is not immune.
- Block reduces a blocked hit's damage by 50% rather than preventing it. Block
  chance is the chance that reduction applies.
- Block applies to area damage as well as direct attacks; evasion does not. The
  reasoning is thematic — a raised shield helps against an explosion in a way
  that dodging does not.
- Block chance therefore needs no cap. At 100% block chance a character has 50%
  damage reduction, which is strong but is not immunity. An earlier proposal of
  a 75% block cap was rejected once it was established that a block is not a
  full avoid.

**Where the base block value came from.** It is not in `Cataclysm_GDD_v2.md`. It
is in the generated enchantment tables: `game/Data/EnchantmentsPositive.csv`
line 40 reads "You block for 65%-75% of damage instead of the normal 50%". Those
rows were carrying combat rules the design document never stated.

**One enchantment was removed as a consequence.** A weight 1 positive
enchantment read "Your block chance applies to AOE damage at 50% effectiveness".
Once block applies to area damage by default at full effectiveness, that
enchantment halved a benefit the player already had, making it strictly harmful
while sitting in the most powerful weight band. It was deleted from the
Enchantments sheet of `All_Things_Cataclysm.xlsx` and the generated tables were
rebuilt, taking the positive enchantment count from 381 to 380. Issue #80.

**Still open.** Luck gives +0.01% rarity find per point, which is +1% at 100
points and almost nothing next to gear affixes. The value is deferred until loot
tables and gear quality drop rates exist, at which point it can be set to
whatever makes the attribute competitive. Recorded as issue #81.

**Affects:** `Cataclysm_GDD_v2.md` section IV. **Applied 2026-08-02:** a Stat
Calculation subsection was added covering all of the above, and the attribute
table's per-point column was reworded where the meaning changed — the three
regeneration entries now read "increased regeneration" rather than "per second",
and the evasion entry no longer carries an inline cap now that the cap is soft
and listed with the others.

---

## 2026-08-02 — The Power Score formula

**Decision.** Power Score is four additive terms:

```
Power Score = LevelWeight      * character level
            + GearWeight       * sum over 18 equipped pieces of rarity * (1 + UpgradeFactor * gear level)
            + GemWeight        * sum over filled sockets of gem rarity
            + ResistanceWeight * sum over 8 resistances of percent, each counted only to 70
```

| Weight | Value |
|---|---|
| LevelWeight | 6.3270 |
| GearWeight | 6.2330 |
| UpgradeFactor | 0.2525 |
| GemWeight | 5.2725 |
| ResistanceWeight | 1.1298 |

**Gem quality and gem level are the same axis.** The design document sentence
named seven inputs; there are six. A gem has one position on the eight-tier
rarity scale. Gear alone has two independent axes, its rarity and its +1 to +10
upgrade level.

**Why gear upgrade multiplies rarity instead of adding to it.** A fully upgraded
Cataclysmic piece has to be worth far more than a fully upgraded Everyday one.
It is also the only place in the formula where two inputs multiply, and it is
what makes the player's power curve rise with the square of the difficulty tier
rather than in a straight line. The fixed tier anchors already have that shape.

**The weights are derived, not chosen.** Given the reference character below,
they follow from the tier 1 and tier 8 anchors. The only free decision was what
share of a finished character's score comes from each source, set to 50% gear,
30% gems, 10% level, 10% resistances.

That share allocation does **not** affect how well the formula matches the
anchors. Worst-case error stays between 5.29% and 5.35% across allocations as
different as 8/60/24/8 and 18/50/22/10. What it does control is what one gear
upgrade is worth: at the chosen shares a +10 piece is 3.5 times a +0 piece,
where giving level an 18% share would force it to 11.9 times.

**Three rules settled at the same time:**

| Rule | Reason |
|---|---|
| Socket count gets no weight of its own | It is the number of terms in the gem sum |
| Two one-handed weapons count as one equipped piece | They already give the same 6 sockets as a two-handed weapon; dual wielding must not be worth free Power Score |
| Resistance above 70% adds no Power Score | Over-capping stays legal and useful against penetration, but it is headroom rather than power |

**The reference character.** The formula cannot be checked against the anchors
without saying what character is being scored, so the expected character at the
end of each tier is part of this decision. Gear and gem rarity equal the tier;
gear level is tier + 2 capped at +10; level, filled sockets and resistances rise
evenly to their maximums at tier 8.

It is a calibration reference, not a requirement. Leveling is player-driven —
one player may clear a hundred dungeons in a tier where another clears forty —
but it should be smooth across the tiers, which is what the reference assumes.

**What does not fit, and why it is not this formula's fault.** The reference
character lands on 6,327 exactly at tier 8 and 384 against 385 at tier 1. The six
tiers in between are within 5.3%, and the entire residual sits at the tier 4 to
tier 5 boundary, where tier 5 is 1,107 points wide against a surrounding trend of
about 790. A character progressing smoothly produces a smooth curve, and a smooth
curve cannot pass through a kink.

Issue #7 records the same anomaly from the enemy side. A hypothesis that the
jump was deliberate — tier 5 being the first tier a player wears Legendary gear,
the first rarity carrying enchantments — was tested and rejected: adding that
step made the fit worse, 11.3% against 5.3%, and no step at any other tier helped
either.

**Power Score does not read class base stats.** Its inputs contain no health,
mana or energy shield, so this decision did not have to wait for the class base
values in issue #77, and #77 does not have to wait for it.

**Affects:** `Cataclysm_GDD_v2.md` section IV. **Applied 2026-08-02:** the Power
Score section now carries the formula, the weights, the four rules and the
reference character table. The working model is
`sim/cataclysm_sim/player_power.py`, covered by `sim/tests/test_player_power.py`.

---

## 2026-08-02 — City upgrades: one-time use, and the unbranched upgrade

**Decision.** A trailing asterisk on a branch name in the City Upgrades sheet
(`Architect*`) marks a **one-time use** upgrade: it fires once and is spent,
rather than being a standing improvement. Four upgrades are one-time use.

The one row with no branch is also one-time use. It has **no tiers at all** and is
a last resort rather than a city improvement:

> Cleanse every player city of half of the dungeons on them excluding Quest and
> Fallen City dungeons. The cities lose 50% of their remaining defenses and
> population. Can only purchase once, and will only be available on T3 and above.

**Which branch it belongs to has not been decided.** It is carried with an empty
branch and marked, not dropped.

**Also decided.** The four tier-value notations in the same sheet mean:

| Notation | Meaning |
|---|---|
| `0.3` | A percentage increase, stored as a fraction |
| `10` | A flat improvement, in whatever unit the effect names |
| `3x` | A multiplier |
| `10/10%` | Two values at once: the trigger interval in days, and the magnitude. The effect reads "every X days ... Y%" and the tier improves both. |

**Affects:** the City Upgrades sheet in `All_Things_Cataclysm.xlsx`. **Applied
2026-08-02:** `tools/generate_datatables.py` strips the asterisk into an
`IsOneTimeUse` flag and parses the tier notations into a kind, a value and an
interval. `FCataclysmCityUpgradeRow` carries all of it.

**Still open:** which branch the unbranched upgrade belongs to.

---

## 2026-08-02 — Gems: all eight rarity tiers have a value

**Decision.** The Gems sheet is correct as written. The Everyday value is stated
inside the effect text — "10% chance to apply void splinter" means Everyday is
10% — and the seven numeric columns continue the series from there.

Verified across all 25 gems: every one states a percentage in its text, and in
each case the numeric columns continue from it.

**Affects:** nothing needs changing in the sheet. **Applied 2026-08-02:**
`tools/generate_datatables.py` extracts the Everyday value so consumers get eight
numbers rather than seven and a sentence.

---

## 2026-08-02 — The Belt has 4 gem sockets

**Decision.** Add a Belt row to the socket table with **4 sockets**.

**Why.** The socket table in `Cataclysm_GDD_v2.md` section VI sums to 41, but the
same section states the total is 45. The Belt appears in the item slot list
(Head, Chest, Shoulders, Gloves, Pants, Boots, Belt) and has no row in the socket
table. Four sockets on the Belt makes the total exactly 45.

The 45 figure is the one to preserve, because the expected player Power Score
maths — including the per-tier anchors in `sim/cataclysm_sim/scoring.py` — was
derived assuming 45 sockets. Changing the total would invalidate those anchors.

Socket count after this decision and the quiver removal below:

| Slot | Sockets |
|---|---|
| Chest | 6 |
| Pants | 4 |
| Relic | 4 |
| **Belt** | **4** |
| Helmet, Shoulders, Gloves, Boots | 2 each |
| Rings (×8), Necklace | 1 each |
| Potion slots (×4) | 1 each |
| Weapons | 6 (a two-handed weapon, or two one-handed weapons at 3 each) |
| **Total** | **45** |

**Affects:** `Cataclysm_GDD_v2.md` section VI socket table. **Applied 2026-08-02:**
Belt row added with 4 sockets, total confirmed at 45.

---

## 2026-08-02 — Quivers and offhands are removed

**Decision.** Quivers are removed from the game. There is no offhand slot.

Quivers were never really weapons; they were an extra gear piece providing related
stats and gem sockets. Ranged two-handed weapons now behave like every other
two-handed weapon and carry **6 gem sockets**.

**Affects:** `Cataclysm_GDD_v2.md` section VI. **Applied 2026-08-02:** the
"Offhands: Quivers" line and the "Offhands | 3" socket row are both removed, and
the weapon type list now states there are no offhand items.

**Consequence for socket totals.** With the offhand row gone, a two-handed weapon
gives 6 sockets and two one-handed weapons give 3 + 3 = 6, so the weapon
contribution is 6 either way. See the open question on the Belt below.

---

## 2026-08-02 — Dual wielding, damage types, and skill selection

**Decision.** Dual wielding exists. The rules:

- A player may equip either one weapon or two.
- **A single weapon can carry multiple damage types.**
- The set of damage types across **all** equipped weapons determines what the
  player has access to.
- Every damage type present unlocks its **three** class trees. Four damage types
  across the player's weapons unlocks 12 classes.
- Every damage type present also unlocks every skill matching the combination of
  an equipped **weapon type** and that **damage type**.
- **The player does not get a button for every available skill.** They choose
  which skills to use from the available pool and assign them to hotkeys.

**Affects:** `Cataclysm_GDD_v2.md` sections IV and V. **Applied 2026-08-02:** a
"Dual Wielding and Damage Types" subsection was added to section VI, and the
Skill Slots text in section IV was rewritten to say the weapons determine the
pool rather than the contents of each slot.

This is a meaningful clarification of the skill slot system. The design document
says "Each player has six skill slots. The skills available in each slot are
determined by the combination of weapon type and damage type." That reads as the
weapon fixing the contents of each slot. It actually means the weapon and damage
types determine the **pool**, and the player builds a loadout from that pool.

The design document already supports multiple damage types per weapon — section IV
says "Players with multiple damage types on their weapon can invest in multiple
class trees simultaneously" — but never states it as a rule of itemization.

---

## 2026-08-02 — Weapon availability per damage type

**Decision.** The table below is approved provisionally. Expect to revise it as
classes are fleshed out.

| Weapon | War | Demonic | Death | Pestilence | Famine | Celestial | Chaos | Void |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Sword | Y | Y | Y | Y | Y | Y | Y | · |
| 2H Sword | Y | Y | Y | · | · | Y | Y | Y |
| Dagger | Y | Y | Y | Y | Y | · | Y | Y |
| Axe | Y | Y | · | · | Y | · | Y | · |
| 2H Axe | Y | Y | Y | · | · | · | Y | · |
| Spear | Y | · | Y | Y | · | Y | Y | Y |
| Fist | Y | Y | Y | Y | Y | · | Y | Y |
| Shield | Y | · | · | · | · | Y | Y | · |
| Whip | Y | Y | Y | Y | Y | · | Y | Y |
| Crossbow | Y | · | · | Y | · | Y | Y | · |
| 2H Crossbow | Y | · | · | Y | · | · | Y | · |
| 2H Warhammer | Y | Y | · | · | Y | Y | Y | Y |
| Wand | · | Y | Y | Y | Y | Y | Y | Y |
| Staff | · | Y | Y | Y | Y | Y | Y | Y |
| **Weapons** | **12** | **10** | **9** | **9** | **8** | **8** | **14** | **8** |

War is unchanged from the design document. Chaos is all 14, which the design
document already stated. The other six rows were derived from the three class
identities of each damage type, so that every class has weapons that suit it.

Verified: all 24 classes have at least two available weapons, and no weapon is
unused by every damage type.

**Effect on scope:** the Weapon Skills sheet drops from 558 rows to 398, a 29%
reduction. The animation count is unaffected at 71 shared sets, because animation
follows weapon and slot rather than damage type.

**Affects:** `Cataclysm_GDD_v2.md` section V, and the Weapon Skills sheet in
`All_Things_Cataclysm.xlsx`. **Applied 2026-08-02:** the six TBD rows are filled
in, and the Weapon Skills sheet is pruned from 558 rows to 398. No row carrying a
designed skill was removed; all 61 War skills survive.

---

## 2026-08-02 — Animation re-use, three tiers

**Decision.** Animation is shared across damage types, on three tiers:

1. **Shared motion, 71 animation sets.** The physical animation is determined by
   weapon type and slot. One two-handed axe heavy attack, used by all eight
   damage types.
2. **Damage type identity comes from everything except the skeleton.** Effects,
   audio, projectile and deployable meshes, impact reactions, and above all what
   the skill mechanically does.
3. **Signature animation budget**, roughly three unique animations per damage
   type for identity moments, mostly Ultimates. About 24 in total.

Estimated total: 135–150 animations, against 558+ if every skill were bespoke.

This is a constraint on skill **design**, not only on animation production. Two
skills sharing a weapon and a slot share a motion, so what distinguishes them
must be what they do.

**Affects:** the animation pipeline, and how every skill is written.

---

## 2026-08-02 — The Cataclysm determines the player's damage type

**Decision.** The Cataclysm being fought is the player's damage type. Fighting the
Demonic Cataclysm biases drops toward Demonic-tuned weapons, and because weapons
determine both skills and available class trees, that is what unlocks the Demonic
classes.

**Consequence.** The Phase 1 vertical slice targets the Demonic Cataclysm, so it
needs Demonic player content, not the War content the roadmap currently names.

**Affects:** `Cataclysm_GDD_v2.md` sections IV, VI and XV. **Applied 2026-08-02:**
the rule is stated in the Game Start section, and the Phase 1 roadmap now names
Demonic / Masochist and Demonic skills rather than War / Bulwark and War skills.

---

## 2026-08-02 — Engine version

**Decision.** Unreal Engine **5.8.1** (`++UE5+Release-5.8`, changelist 56057345).

Chosen over the already-installed 5.7 because the project is at day zero, 5.8 is
the last planned major Unreal Engine 5 release before Epic moves to UE6, and
migrating an Unreal project between engine versions later is real work.

Unreal Engine 5.7 remains installed and takes 74 GB. Uninstall it once 5.8 is
confirmed working.
