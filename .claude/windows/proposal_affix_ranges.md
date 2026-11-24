# Affix ranges: whole numbers, one range per tier (second version, under the owner's rulings)

**A proposal, 2026-11-24. Nothing in the game, the CSVs or the design workbook is changed.**
Branch `proposal/affix-ranges`. The script is `tools/affix_range_proposal.py` and its tests are
`tools/tests/test_affix_range_proposal.py`; every table below is printed by that script. Issue
#2883 holds the rulings; the enemy rebalance they defer to is issue #2884.

The project owner, 2026-11-24, verbatim:

> take a look at the values and ranges of affixes. Some of them feel arbitrarily low. Like 1.2% and
> stuff like that. I really hate decimals, and they should be removed in all cases, but also, each
> affix tier should have it's own range, and that range should be something a PLAYER finds
> reasonable. Not math. So if your range is .5-3% for something like flat damage, that's probably
> dumb.

Their rulings on the first version (issue #2883), quoted:

| # | Question | Answer |
| :-- | :-- | :-- |
| 1 | Do upgrades multiply every affix? | "upgrade everything, once we've fixed the ranges and values, we can look at rebalancing enemies to fit." |
| 2 | Do tiers overlap? | "No overlap (Recommended)" |
| 3 | The twelve small stats | "Fewer slots (Recommended)" |
| 4 | Flat damage | "It's fine, keep the 6 slots and upgrade the damage. We'll fix it with the enemy balance audit like for the first question." |

The first version recommended the opposite of ruling 1. This version is rebuilt on it.

## Headline: before and after

| | Today | Proposed |
| :-- | --: | --: |
| Single-stat, resistance and ailment affixes | 73 | 71 (2 retired, D5) |
| ... showing a decimal at some tier or upgrade level | 73 | 0 |
| ... whose adjacent tiers overlap | 73 | 0 |
| ... with a tier holding no whole number on a fresh drop | 48 | 0 |
| Increased maximum health, tier 1 on a fresh drop (the owner's "1.2%") | 1.2-1.6% | 3-4% |
| Increased maximum health, tier 7 at +10 | 12% | 38-40% |
| Flat damage, tier 1 at +0 / tier 7 at +10 | 2.2-2.9 / 22 | 2-3 / 35-38 |
| Affixes keeping today's top at +10 | | 14 |
| Affixes whose top rises | | 57, median x3.33, largest x8.75 (4 -> 35); three of them only 52 -> 53, a rounding |
| Affixes on fewer slots (ruling 3) | | 12 |
| Damage per hit of the reference character at difficulty tier 8 (6 flat + 6 increased slots) | 1,853 | 2,660 (+44%), for #2884 |

## The rule

**A tier is a range of whole numbers written in the workbook as its +0 value, contiguous with the
tiers either side and never overlapping them, holding at least two values. At an upgrade level each
end of the range is multiplied by 1 + 0.15 x level and rounded to the nearest whole number, a half
going up, always from the +0 end; a two-handed weapon doubles inside that rounding. A roll is a
position in the range, kept when the piece is upgraded, and gives one of the whole numbers in the
range at the piece's level, each equally likely. A hybrid at tier N grants each half as that half's
own affix at tier N - 2. Implicits are whole +0 numbers, multiplied the same way.**

| # | Rule | Where it comes from |
| :-- | :-- | :-- |
| 1 | Each affix row carries T1 Min, T1 Max ... T7 Min, T7 Max at +0: whole numbers, `T(n+1) Min = T(n) Max + 1`, at least two values per tier | Rulings 2; the owner's original request. Contiguous, non-overlapping tiers are Path of Exile's and Last Epoch's shape (Research) |
| 2 | Every affix and every implicit grows with upgrades | Ruling 1; the 2026-08-03 rule stands |
| 3 | **How rounding works at each upgrade step.** The level's multiplier is 1.00, 1.15, 1.30, 1.45, 1.60, 1.75, 1.90, 2.05, 2.20, 2.35, 2.50 for +0 to +10. Each end of the +0 range is multiplied by it and rounded to the nearest whole number, a half going up. Every level is computed from the +0 end, never from the level below, so rounding never compounds. The arithmetic is done in whole hundredths, because in floating point 10 x 1.15 is 11.499999999999998 and would round the wrong way; an engine implementation must do the same | "How an Attribute Rounds": "A half rounds up" and "The rounded number is the only number" |
| 4 | A roll is a position in [0, 1], as an item stores it today. At the piece's level it picks one of the whole numbers in the tier's range there, each with an equal share. So every number a tooltip's range shows can occur, an upgrade never lowers a roll, a perfect roll is the top and the worst roll the bottom | `UCataclysmItemValues::EnchantmentValue`, already the enchantment rule ("every written value is equally likely") |
| 5 | A hybrid rolls tiers 3 to 7 only, and at tier N grants each half at that half's tier N - 2 | D6 |
| 6 | `Floor`, `TIER_FRACTIONS`, `ROLL_BAND_FRACTION` and `WORST_MULTIPLIER` stop producing values. The written tier 1 minimum is the floor | The owner's 2026-09-04 "a number per affix, not one rule" becomes a range per tier |

**Why no tier can meet another at any level, and why no number repeats.** Two whole numbers one
apart are at least one apart after a multiplier of one or more, and rounding half up keeps them
apart. So the tier above always starts above where this one ends. `python
tools/affix_range_proposal.py check` does not rely on that argument. It enumerates every affix, every
tier, every upgrade level +0 to +10, one-handed and two-handed, and checks:
- every number is whole;
- no number appears twice;
- every tier shows at least two numbers;
- every tier sits wholly above the one below;
- no sampled roll position falls when the piece is upgraded.

It reports **0 problems**.

**One thing the rounding does that a player may notice.** One upgrade adds 0.15 of a +0 number,
which is under one for any number below 7. So a range end below 7 does not move on every upgrade:
- a +0 end of 1 shows 1 until +4, then 2, then 3 at +10;
- a +0 end of 6 is unchanged only by the upgrade to +6;
- every end of 7 or more moves on every upgrade.

The low ends are tiers 1 to 3 of the small-number ladders. Decision D7 prices starting every ladder
at 7 instead.

## Research

Sources fetched on 2026-11-24 (unchanged from the first version). Pages that refused the fetch are
listed at the end.

| Game | What the page shows | Source |
| :-- | :-- | :-- |
| Path of Exile | Fire resistance suffixes, item level 1 to 84: 6-11, 12-17, 18-23, 24-29, 30-35, 36-41, 42-45, 46-48. Whole, contiguous, never overlapping | Official item data, https://www.pathofexile.com/item-data/mods |
| Path of Exile | Maximum life: 3-9, 10-24, 25-39 ... 160-174, 175-189. Global attack speed: 5-7, 8-10 ... 23-25, 26-27. All elemental resistances: 3-5, 6-8, 9-11, 12-14, 15-16. Item rarity suffixes: 6-10, 11-14, 15-20, 21-26 | Same page |
| Path of Exile | Its decimals: physical attack damage leeched as life (0.2-0.4)%, (0.6-0.8)%, (1-1.2)%; life regeneration per second (2.1-8), (8.1-16) ... | Same page; https://poedb.tw/us/Life_Leech |
| Path of Exile | Global critical strike chance on amulets: 10-14, 15-19, 20-24, 25-29, 30-34, 35-38, an increase reading the weapon's or skill's base | https://poedb.tw/us/Critical_strike |
| Path of Exile | Movement speed on boots: single values 10, 15, 20, 25, 30, 35 | https://poedb.tw/us/Movement_speed |
| Last Epoch | Developer blog (2020-08-03): Chance to Bleed T5 32%-42%, T6 43%-49%, T7 50%-60%; "+7 to All Attributes" at T7; "T5 and T7 affixes generally have a similar range while T6 usually has a smaller range" | https://forum.lastepoch.com/t/introducing-tier-6-and-7-item-affixes/22279 |
| Last Epoch | Patch notes posted by a community member: Added Health T6 95-120, T7 121-160; Increased Health T6 15-20%, T7 21-26% | https://forum.lastepoch.com/t/last-epoch-harbingers-of-ruin-patch-notes/71790/10 |
| Last Epoch | Seven tiers, each with a roll range | https://maxroll.gg/last-epoch/resources/gear-walkthrough |
| Diablo IV | Season 15: "+6.5-8.5% Critical Strike Chance" (gloves), "+3.5-5% Critical Strike Chance" (ring), "+325-400 Resistance to All Elements", "263-316 Life On Hit". Percentages carry a decimal, flat values are whole, no tiers shown | https://op.gg/diablo4/affixes |

**What the research settles.**
- Contiguous, non-overlapping tiers are the genre shape. Every ordinary Path of Exile ladder read
  starts each tier one above the last, and so do both Last Epoch fragments. The exceptions are
  separate variants rather than rungs of one ladder.
- Whole numbers are the norm and not the absolute. Path of Exile uses decimals for leech and
  regeneration, and Diablo IV uses one on every percentage.
- Path of Exile puts crit chance on gear as an increase (D5).

**What it does not settle.** Neither Path of Exile nor Last Epoch multiplies an affix by an upgrade,
so ruling 1's combination has no shipped precedent among the pages read. Diablo IV moves its ranges
with item power and prints decimals; its masterworking was not fetched. Where tier 1 starts and how
wide tiers are is this game's judgement.

**Refused or unreadable, not used:**
- `www.lastepochtools.com` (403)
- `support.lastepoch.com` (403)
- `www.vhpg.com` (404)
- `mobalytics.gg` (403)
- `www.icy-veins.com/d4` (403)

## Today

- **Where affixes live.** They are defined in the `Affixes` sheet of `docs/All_Things_Cataclysm.xlsx`
  and generated into `game/Data/Affixes.csv` by `tools/generate_datatables.py`.
- **How a value is rolled.** `UCataclysmItemValues::AffixValue` takes the top value x 3^((tier-7)/6),
  then a continuous roll between 75% and 100% of that, then divides by 2.5 and multiplies by
  1 + 0.15 x level, then applies the floor's map, then x2 on a two-handed weapon.
- **How it is shown.** `UCataclysmItemTooltip::NumberInWords` prints one decimal unless the value is
  within 0.005 of a whole number.

Appendix B is every affix and every tier as it is today.

## What the rulings move, measured

Measured with `python tools/affix_range_proposal.py measure`. It runs the simulation's own functions
with its value methods patched to the proposal; the reference build is left as written, which is
where #2884 starts.

| Design rule | Today | Proposed | Status |
| :-- | --: | --: | :-- |
| Increased damage, spell damage, minion damage at T7 (owner's 125%) | 125 | 125 | kept |
| Damage against one type, 400% | 400 | 400 | kept |
| Flat health 120, flat armour 250, flat shield 50 | same | same | kept |
| Attribute affix = dedicated affix at 50 points | 12 = 12 | 40 = 40 | kept (both moved) |
| Damage over time levers solve (1 + 2v)^3 = 8.5 at 52 | 52 | 53 (8.74) | moves by a rounding: 21 is the nearest whole +0 top |
| Flat damage 22, fitted to the 1,860 target with 6 slots | 22 | 38 | moves; ruling 4 keeps 6 slots: tier 8 hit 1,853 -> 2,660 |
| Flat vs increased damage crossover (8 increased in place) | 194 | 334 | moves with flat damage |
| Flat vs increased health crossover (+200% increases) | 3,000 | 900 | **moves**: at level 100 (base 2,110) increased health now wins from the first slot; R1 |
| Resistance families 40 / 28 / 12 | 40 / 28 / 12 | 58 / 50 / 40 | moves |
| Capping all eight at tier 8 costs about 12 slots | 12.1 | 3.6 | **moves**; R2 |
| Ailment chance = its gem's + 5, and the gem stays the stronger source | 15 / 25 / 20 | 38 / 40 / 38 | "+5" breaks; the gem stays stronger one-handed, and on a two-handed weapon (x2 = 76) the two 60% gems, madness and withering, fall below; R3 |
| Movement speed 35% on boots | 35 | 55 | moves (tier 1 starts at today's floor, 9); R4 |
| Energy shield refills in 5 s (flat shield / flat regeneration) | 5 s | 1.3 s | moves |
| Crit multiplier per affix | 20 | 38 | moves |

**Damage per non-critical hit of the reference character, by difficulty tier** (6 flat + 6
increased slots, as ruled; target 1,860 at tier 8):

| Difficulty tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| Today | 221 | 316 | 458 | 668 | 983 | 1,458 | 1,650 | 1,853 |
| Proposed | 233 | 368 | 587 | 863 | 1,290 | 2,075 | 2,343 | 2,660 |
| Change | +5% | +16% | +28% | +29% | +31% | +42% | +42% | +44% |

**The reference build cannot be carried over as written.** It spends four slots each on block,
damage reduction and penetration. Damage reduction and penetration now roll on one piece and block
on two, so four of each are no longer possible.

Read literally, the simulation gives that build at tier 8:

| Stat | Today | Proposed |
| :-- | --: | --: |
| Block | 20 | 140 |
| Damage reduction | 16 | 140 (cap 75) |
| Penetration | 16 | 140 |
| Resistance per type | 144 | 480 |
| Increased health | 72% | 240% |
| Increased armour | 48% | 160% |
| Increased attack speed | 60% | 160% |
| Increased crit chance | 100% | 400% |

Re-fitting it, and then `enemy_stats.py`, is #2884's.

## The proposed table

**Each cell is the tier's range at +0 / at +10.** Every level between is in Appendix A, one table
per distinct ladder (the letter in the "Ladder" column). A two-handed weapon doubles inside the
rounding. "Pieces" counts the eight rings as eight.

| Affix | T1 | T2 | T3 | T4 | T5 | T6 | T7 | Top at +10 (today) | Pieces (today) | Ladder | Change |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: | --: | :-: | :-- |
| Flat maximum health | 12-13 / 30-33 | 14-16 / 35-40 | 17-20 / 43-50 | 21-24 / 53-60 | 25-29 / 63-73 | 30-35 / 75-88 | 36-48 / 90-120 | 120 (120) | 14 | A |  |
| Increased maximum health | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Flat maximum mana | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 16-17 / 40-43 | 43 (38) | 13 | C | raised: the smallest top that holds seven two-value tiers |
| Increased maximum mana | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 13 | B | raised: the smallest top that holds seven two-value tiers |
| Flat maximum energy shield | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 17-20 / 43-50 | 50 (50) | 12 | D |  |
| Increased maximum energy shield | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Flat armor | 25-29 / 63-73 | 30-35 / 75-88 | 36-42 / 90-105 | 43-49 / 108-123 | 50-59 / 125-148 | 60-74 / 150-185 | 75-100 / 188-250 | 250 (250) | 14 | E |  |
| Increased armor | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Flat evasion | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 1 (14) | F | one piece (35) instead of fourteen (56): two would be 70, past evasion's soft cap of 60 before implicits |
| Increased evasion | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Flat damage | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (22) | 12 | G | raised: the smallest top that holds seven two-value tiers |
| Increased damage | 13-15 / 33-38 | 16-18 / 40-45 | 19-21 / 48-53 | 22-26 / 55-65 | 27-31 / 68-78 | 32-37 / 80-93 | 38-50 / 95-125 | 125 (125) | 12 | H |  |
| Increased spell damage | 13-15 / 33-38 | 16-18 / 40-45 | 19-21 / 48-53 | 22-26 / 55-65 | 27-31 / 68-78 | 32-37 / 80-93 | 38-50 / 95-125 | 125 (125) | 12 | H |  |
| Flat maximum class resource | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (7) | 2 (11) | G | two pieces (76) instead of eleven (77) |
| Flat health regeneration | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (8) | 3 (12) | G | three pieces (114) instead of twelve (96) |
| Increased health regeneration | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Flat mana regeneration | retired into Increased mana regeneration | | | | | | | -- (0.65) | -- (12) | | retired: 0.65 a second would have to become 35, a twentieth of a level 100 pool a second; every class has a base for the increase |
| Increased mana regeneration | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Increased energy shield regeneration | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Flat life leech | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 1 (12) | F | the weapon only: 35, or 70 two-handed or with an offhand, instead of twelve pieces (48) |
| Flat block chance | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (5) | 2 (14) | F | two pieces instead of fourteen: 70 either way |
| Flat damage reduction | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 1 (14) | F | one piece (35) instead of fourteen (56): two would be 70 against a cap of 75, from gear alone |
| Flat retaliation | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 2 (14) | F | two pieces (70) instead of fourteen (56) |
| Flat crowd control resistance | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (5) | 2 (14) | F | two pieces instead of fourteen: 70 either way |
| Flat critical strike chance | retired into Increased critical strike chance | | | | | | | -- (5) | -- (12) | | retired: as whole numbers it would be 33-35 points of crit a piece at +10; folded into the increase, which reads the skill's base |
| Increased critical strike chance | 10-11 / 25-28 | 12-13 / 30-33 | 14-16 / 35-40 | 17-20 / 43-50 | 21-24 / 53-60 | 25-29 / 63-73 | 30-40 / 75-100 | 100 (25) | 12 | I | absorbs the flat affix: 100% increased is 5 points at the 5% default base, the old flat affix's top |
| Flat critical strike multiplier | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (20) | 12 | G | raised: the smallest top that holds seven two-value tiers |
| Increased attack speed | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (15) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Increased area of effect | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Increased damage over time frequency | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 17-21 / 43-53 | 53 (52) | 12 | J | rounding: 21 is the nearest whole +0 top, and shows 53 at +10 |
| Flat penetration | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 1 (12) | F | the weapon only: 35 is the most any enemy resists, so more is wasted; 70 two-handed or with an offhand |
| Increased movement speed | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 17-18 / 43-45 | 19-20 / 48-50 | 21-22 / 53-55 | 55 (35) | 1 | K | raised: the smallest top that holds seven two-value tiers |
| Flat cooldown reduction | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (12) | 12 | F | raised: the smallest top that holds seven two-value tiers |
| Flat magic find | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (10) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Increased loot quantity | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (8) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Single resistance | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 16-17 / 40-43 | 18-19 / 45-48 | 20-21 / 50-53 | 22-23 / 55-58 | 58 (40) | 17 | L | raised: the smallest top that holds seven two-value tiers |
| Two resistances | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 17-18 / 43-45 | 19-20 / 48-50 | 50 (28) | 17 | M | raised: the smallest top that holds seven two-value tiers |
| All resistances | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 17 | B | raised: the smallest top that holds seven two-value tiers |
| Chance to bleed | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to poison | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (25) | 11 | B | raised: the smallest top that holds seven two-value tiers |
| Chance to disease | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (20) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to apply void splinter | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to necrose | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to madden | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to cripple | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to weaken | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to shred | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to burn | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Chance to stun | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (15) | 11 | G | raised: the smallest top that holds seven two-value tiers |
| Increased agility | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Increased ferocity | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Increased constitution | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Increased vitality | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 15 | B | raised: the smallest top that holds seven two-value tiers |
| Increased mind | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Increased spirit | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Increased efficacy | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Increased luck | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Flat mana leech | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 1 (12) | F | as life leech |
| Flat energy shield leech | 1-2 / 3-5 | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 35 (4) | 1 (12) | F | as life leech |
| Increased damage against War enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Demonic enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Death enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Pestilence enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Famine enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Celestial enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Chaos enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage against Void enemies | 40-47 / 100-118 | 48-59 / 120-148 | 60-69 / 150-173 | 70-84 / 175-210 | 85-99 / 213-248 | 100-119 / 250-298 | 120-160 / 300-400 | 400 (400) | 12 | N |  |
| Increased damage over time | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 17-21 / 43-53 | 53 (52) | 12 | J | rounding: 21 is the nearest whole +0 top, and shows 53 at +10 |
| Increased damage over time duration | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 17-21 / 43-53 | 53 (52) | 12 | J | rounding: 21 is the nearest whole +0 top, and shows 53 at +10 |
| Increased minion damage | 13-15 / 33-38 | 16-18 / 40-45 | 19-21 / 48-53 | 22-26 / 55-65 | 27-31 / 68-78 | 32-37 / 80-93 | 38-50 / 95-125 | 125 (125) | 12 | H |  |
| Increased minion health | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (12) | 14 | B | raised: the smallest top that holds seven two-value tiers |
| Increased minion attack speed | 3-4 / 8-10 | 5-6 / 13-15 | 7-8 / 18-20 | 9-10 / 23-25 | 11-12 / 28-30 | 13-14 / 33-35 | 15-16 / 38-40 | 40 (15) | 12 | B | raised: the smallest top that holds seven two-value tiers |
| Flat energy shield regeneration | 2-3 / 5-8 | 4-5 / 10-13 | 6-7 / 15-18 | 8-9 / 20-23 | 10-11 / 25-28 | 12-13 / 30-33 | 14-15 / 35-38 | 38 (10) | 3 (12) | G | three pieces (114) instead of twelve (120) |

"raised" means the top moved to the smallest value that holds seven two-value tiers above the
tier 1 start, and no further.

**How the ladders were built** (a judgement; the workbook would hold the numbers, which a person
then edits freely):
- Tier 1 starts at today's worst +0 roll. That is the owner's 2026-09-04 floor where there is one,
  otherwise a tenth of the top. It is never below 3 for an increase, 1 for percentage points or 2
  for an amount (D7).
- The +0 top is today's top / 2.5. Where that does not leave fourteen whole numbers above the
  start, it rises to the smallest top that does.
- Tier 7 starts at three quarters of the top, where today's top band starts.
- Tiers 1 to 6 share the rest on a constant ratio, back-loaded like today's ladder, each at least
  two values. Boundaries from 50 snap to fives, and from 200 to tens.
- The twelve small stats keep the piece count whose ceiling (top x pieces) is nearest today's
  (ruling 3). It is rounded down where a cap is close: damage reduction against its 75 cap, and
  evasion against its soft cap of 60.

### Hybrids (D6)

A hybrid at tier N grants each half as that half's own affix at tier N - 2, and rolls tiers 3 to 7.
Two tiers down on today's ladder is 3^(-1/3) = 0.693, the design's 70% within one per cent.

| Hybrid | Halves | Hybrid T3 at +0 | Hybrid T7 at +0 | Hybrid T7 at +10 | Share of the single affix at T7 | Slots (today) |
| :-- | :-- | --: | --: | --: | --: | :-- |
| Health and armor | Flat maximum health; Flat armor | 12-13; 25-29 | 25-29; 50-59 | 63-73; 125-148 | 0.61; 0.59 | Belt, Boots, Chest, Head, Pants, Ring, Shoulders |
| Health and energy shield | Flat maximum health; Flat maximum energy shield | 12-13; 5-6 | 25-29; 13-14 | 63-73; 33-35 | 0.61; 0.70 | Chest, Head, Ring, Shoulders |
| Armor and evasion | Flat armor; Flat evasion | 25-29; 1-2 | 50-59; 9-10 | 125-148; 23-25 | 0.59; 0.71 | Chest (Boots, Chest, Head, Pants, Ring, Shoulders) |
| Evasion and energy shield | Flat evasion; Flat maximum energy shield | 1-2; 5-6 | 9-10; 13-14 | 23-25; 33-35 | 0.71; 0.70 | Chest (Chest, Head, Ring, Shoulders) |
| Mana and energy shield | Flat maximum mana; Flat maximum energy shield | 4-5; 5-6 | 12-13; 13-14 | 30-33; 33-35 | 0.77; 0.70 | Chest, Head, Relic, Ring |
| Increased health and armor | Increased maximum health; Increased armor | 3-4; 3-4 | 11-12; 11-12 | 28-30; 28-30 | 0.75; 0.75 | Belt, Boots, Chest, Head, Pants, Ring, Shoulders |
| Attack speed and critical strike chance | Increased attack speed; Increased critical strike chance | 3-4; 10-11 | 11-12; 21-24 | 28-30; 53-60 | 0.75; 0.60 | Gloves, Necklace, Relic, Ring, Weapon |
| Critical strike chance and multiplier | Increased critical strike chance; Flat critical strike multiplier | 10-11; 2-3 | 21-24; 10-11 | 53-60; 25-28 | 0.60; 0.74 | Gloves, Necklace, Relic, Ring, Weapon |
| Health and mana regeneration | Flat health regeneration; Increased mana regeneration | 2-3; 3-4 | 10-11; 11-12 | 25-28; 28-30 | 0.74; 0.75 | Belt, Chest, Necklace (Belt, Boots, Chest, Necklace, Ring) |
| Penetration and critical strike multiplier | Flat penetration; Flat critical strike multiplier | 1-2; 2-3 | 9-10; 10-11 | 23-25; 25-28 | 0.71; 0.74 | Weapon (Gloves, Necklace, Relic, Ring, Weapon) |
| Block chance and crowd control resistance | Flat block chance; Flat crowd control resistance | 1-2; 1-2 | 9-10; 9-10 | 23-25; 23-25 | 0.71; 0.71 | Belt (Belt, Boots, Chest, Head, Pants, Ring, Shoulders) |
| Magic find and loot quantity | Flat magic find; Increased loot quantity | 3-4; 3-4 | 11-12; 11-12 | 28-30; 28-30 | 0.75; 0.75 | Belt, Boots, Necklace, Relic, Ring |
| Minion damage and minion health | Increased minion damage; Increased minion health | 13-15; 3-4 | 27-31; 11-12 | 68-78; 28-30 | 0.62; 0.75 | Ring |

Two halves are renamed because their affix is retired (D5). Five hybrids lose slots because a half
narrowed. The rule is "A hybrid can never appear on a slot one of its halves could not", and the
narrowed pieces were chosen so every hybrid keeps at least one slot. `check` reports that today's
70%-and-round rule would break the new rules in 83 places at +0, because two whole numbers one apart
are 0.7 apart after the multiplier and round together.

### Implicits (D9)

Restated as whole +0 values, at least 1, upgraded by the same rule. 46 of 76 keep their +10 figure;
the 30 that move:

| Base | Implicit | Today +0 | Today +10 | Proposed +0 | Proposed +10 |
| :-- | :-- | --: | --: | --: | --: |
| Hood | evasion flat | 1.6 | 4 | 2 | 5 |
| Circlet | energy_shield_regen flat | 4.4 | 11 | 4 | 10 |
| Visage | crowd_control_resistance flat | 1.6 | 4 | 2 | 5 |
| Vestment | energy_shield_regen flat | 9.6 | 24 | 10 | 25 |
| Mantle | evasion flat | 1.4 | 3.5 | 1 | 3 |
| Epaulets | health_regen flat | 0.44 | 1.1 | 1 | 3 |
| Spaulders | retaliation flat | 4.4 | 11 | 4 | 10 |
| Grips | attack_speed increased | 3.6 | 9 | 4 | 10 |
| Vambraces | attack_damage flat | 4.8 | 12 | 5 | 13 |
| Treads | movement_speed increased | 4.8 | 12 | 5 | 13 |
| Sollerets | movement_speed increased | 2.4 | 6 | 2 | 5 |
| Cord | health_regen flat | 0.52 | 1.3 | 1 | 3 |
| Signet | crit_multiplier flat | 6.4 | 16 | 6 | 15 |
| Circle | mana_regen flat | 0.2 | 0.5 | 1 | 3 |
| Pendant | crit_chance flat | 2.4 | 6 | 2 | 5 |
| Locket | energy_shield_regen flat | 4.4 | 11 | 4 | 10 |
| Dagger | attack_damage flat | 10.4 | 26 | 10 | 25 |
| Axe | attack_damage flat | 18.4 | 46 | 18 | 45 |
| Whip | attack_damage flat | 12.8 | 32 | 13 | 33 |
| Whip | area_of_effect increased | 4.8 | 12 | 5 | 13 |
| Shield | block_chance flat | 4.8 | 12 | 5 | 13 |
| Greataxe (x2 two-handed) | attack_damage flat | 28.8 | 72 | 29 | 73 |
| Greataxe (x2 two-handed) | crit_multiplier flat | 8.8 | 22 | 9 | 23 |
| Spear (x2 two-handed) | attack_damage flat | 25.6 | 64 | 26 | 65 |
| Spear (x2 two-handed) | penetration flat | 2.4 | 6 | 2 | 5 |
| Staff (x2 two-handed) | attack_damage flat | 26.4 | 66 | 26 | 65 |
| Staff (x2 two-handed) | spell_damage increased | 12.8 | 32 | 13 | 33 |
| Two-Handed Crossbow (x2 two-handed) | attack_damage flat | 26.4 | 66 | 26 | 65 |
| Two-Handed Crossbow (x2 two-handed) | crit_chance flat | 2.8 | 7 | 3 | 8 |
| Warhammer (x2 two-handed) | attack_damage flat | 33.6 | 84 | 34 | 85 |

### Enchantments (D8)

Enchantments are not upgraded, so the fix is a rewrite of the sentence. Weights and tags are
untouched.

| Row today | Proposed sentence | Rate |
| :-- | :-- | :-- |
| Your damage is increased by 0.01%-0.05% permanently for every 1000 enemies killed this run | ... by 1%-5% ... for every 100,000 enemies killed this run | same |
| Your maximum HP is increased by 0.01%-0.05% permanently for every 1000 enemies killed this run | ... by 1%-5% ... for every 100,000 enemies killed this run | same |
| Each class point spent above 100 grants 0.5%-1% increased damage | Every 2 class points spent above 100 grant 1%-2% increased damage | same |
| Your melee skills have +0.5-1 metre reach | ... +50-100 centimetres reach | same |
| Your skills deal 1.5%-2.5% less damage for every 10 class points spent above 100 (negative) | ... 3%-5% less damage for every 20 class points ... | same |
| Your maximum HP is reduced by 1.5%-2.5% for every 10 class points above 50 (negative) | ... 3%-5% for every 20 class points ... | same |
| Brute's Heart 10-piece: "reduced by 0.5 seconds per kill" | reduced by 1 second for every 2 kills | same |
| Enemies you stagger are also briefly rooted for 0.5-1.5 seconds | ... 1-2 seconds | stronger |
| Your heavy attack cooldown is reduced by 0.5-1.5 seconds each time you land a critical strike | ... 1-2 seconds ... | stronger |
| Each spell cast reduces your next spell cooldown by 0.5-1.5 seconds | ... 1-2 seconds | stronger |
| Point blank AOE skills have a 0.75-1.5 second cast delay before firing (negative) | ... 1-2 second ... | harsher |
| Critical strikes trigger a 0.5-1 second global cooldown on all your skills (negative) | ... 1-2 second ... | harsher |
| After using a charge skill you are briefly stunned for 0.5-1 second (negative) | ... 1-2 seconds | harsher |

## Recommendations on the smaller choices (D5 to D9), re-decided under ruling 1

**D5: retire flat crit chance into increased crit chance (top 100% at +10), and flat mana
regeneration into increased mana regeneration. Recommended: yes, both.**
- Raising the values does not rescue flat crit chance. As whole numbers it is 33-35 points of crit
  at T7 +10, so two pieces nearly cap crit chance on their own.
- Every skill has a 5% base, and four implicits add flat crit. An increase reads that base, and it
  is the form Path of Exile uses on gear. 100% increased at the 5% default base is 5 points, the old
  flat affix's top.
- Flat mana regeneration at 0.65 a second would have to become 35 a second, about a twentieth of a
  level 100 pool per piece per second. Every class has a base for the increase to read.
- Cost: two row keys disappear. A saved item carrying one needs an alias at load, as the 2026-09-30
  enchantment alias did.

**D6: hybrids grant each half at tier N - 2 and roll tiers 3 to 7. Recommended: yes.**
- Share of the single affix at T7 is 0.59 to 0.77, against the design's 0.70.
- No hybrid can drop at difficulty tier 1, whose cap is tier 2.

**D7: tier 1 starts at 3 for an increase, 1 for percentage points, 2 for an amount.
Recommended: yes.**
- With these starts, the low ends of tiers 1 to 3 on the small-number ladders move only every
  second or third upgrade (above).
- The alternative is to start every ladder at 7, so every upgrade moves every range end. It costs
  more power: the median raise becomes x4.17 instead of x3.33, and every 12% increase, every small
  stat, flat damage and every ailment chance becomes 50 at T7 +10.

**D8: enchantment durations under two seconds become whole seconds, 1-2. Recommended: yes.**
- Three bonuses get stronger and three drawbacks harsher.
- The alternative is to keep half seconds for time only.

**D9: implicits as whole +0 values, upgraded by the same rule. Recommended: yes.**
- The largest relative moves are small numbers: Epaulets and Cord health regeneration go
  1.1 / 1.3 -> 3 at +10, and Circle mana regeneration 0.5 -> 3.
- The Axe + Sword pair the damage target is fitted to goes 86 -> 85 at +10.

## Remaining choices, for the owner or for #2884

| # | What moved | Recommendation |
| :-- | :-- | :-- |
| R1 | Flat health, armour, shield and mana keep today's tops while their increased twins go 12 -> 40, so at level 100 an increased affix wins from the first slot. The design's "flat wins early and increased wins later" still holds across a run, with the crossover far earlier (3,000 base -> 900) | Leave the flat tops and let #2884 decide. Raising the four flat pools x3.33 (to about 400 health, 830 armour, 165 shield and 140 mana at +10) would restore today's crossover and inflate player pools |
| R2 | Capping all eight resistances at tier 8 falls from about 12 slots to 3.6 | Let #2884 raise the difficulty tier's resistance penalty (issue #1229). Seven whole tiers rule out smaller resistance values (all resistances cannot top below 35 at +10) |
| R3 | An ailment affix is no longer its gem + 5 (38 against 15). On a two-handed weapon the madness and withering gems (60% at Cataclysmic) fall below a doubled affix (76) | Accept "+5" as retired; raise those two gems' top rarities in #2884 if the gem must stay stronger two-handed |
| R4 | Movement speed tops at 55 (boots only), because tier 1 starts at the owner's 2026-09-04 floor | Accept, or start tier 1 at 5 for a top of 45 |
| R5 | Damage over time levers 53, not 52 (21 is the nearest whole +0 top; 20 would give 50) | Accept 53 |
| R6 | The reference build cannot hold four damage reduction, block or penetration affixes any more | Re-fit in #2884 |

## What an implementation would touch

For planning only.

- **Workbook.** `Affixes` gains T1 Min ... T7 Max; 17 slot lists change, 2 hybrid halves are renamed
  and 2 rows retired. `Item Bases` gains "Implicit 1 At +0" and "Implicit 2 At +0". `python
  tools/affix_range_proposal.py apply --out <copy>` does exactly this to a copy.
- **Generator.** `tools/generate_datatables.py` reads and validates the new columns into
  `FCataclysmAffixRow`; `Top Value` and `Floor` retire.
- **Game.** `UCataclysmItemValues::AffixValue` becomes a lookup of the +0 range, then each end
  upgraded in whole hundredths with half-up rounding, then the roll picking a whole number in the
  resulting range. `ImplicitValue` works the same way from a whole +0 value. Hybrids roll tier
  N - 2, and the drop roll and the forge bound hybrid tiers to 3 to 7. The tooltip could print the
  tier's range at the item's level, "(tier 3: 18-20)".
- **Saves.** An item stores tier and roll, so it is revalued on load without a migration. The two
  retired row keys, and any hybrid stored at tier 1 or 2, need mapping.
- **Simulation.** `sim/cataclysm_sim/affixes.py` and `reference_build.py`, then the enemy constants
  fitted against it (#2884).
- **Tests that pin today's shape.** These would change; the list is read from their names and
  figures, and none was run against the proposal:
  - `sim/tests/test_affixes.py`: the overlap, ladder, floor, crossover and slots-to-cap tests;
  - `tools/tests/test_an_energy_shield_refills_in_five_seconds.py`, `test_leech.py`,
    `test_attribute_affixes.py`;
  - C++: `Cataclysm.Item.BandsOverlapByExactlyOneTier`,
    `Cataclysm.Items.TheHybridShareIsTheResistanceRatio` and the tooltip tests that walk every affix.

## Findings outside this proposal

Stale text in `docs/Cataclysm_GDD_v2.md` (also recorded on #2883):
- "The curve is linear. Tier N is worth N/7"; the code has been geometric since 2026-09-03.
- Tier N is undercut by N - 1 "only when N is above 4"; every boundary overlaps today (Appendix B).
- Resistances listed at 20 / 14 / 6; the data has 40 / 28 / 12.
- Flat damage listed at 18; the data has 22.

In code comments, `UCataclysmItemValues::HybridFraction` cites "14 against 20", and the class comment
still describes a linear ladder and "about 3.52 at +10".

## Evidence

Every command below was run in this worktree on `proposal/affix-ranges`.

```
$ python tools/affix_range_proposal.py check
0 problems, at every upgrade level +0 to +10, one- and two-handed.
Information: 6 of the +0 numbers in the table show the same number at some pair of adjacent upgrade levels (one upgrade adds 0.15 of a number, under one below 7):
  1: unchanged by the upgrade to +1, +2, +3, +5, +6, +7, +8, +9
  2: unchanged by the upgrade to +1, +3, +4, +6, +7, +8, +10
  3: unchanged by the upgrade to +1, +3, +5, +7, +9
  4: unchanged by the upgrade to +2, +4, +7, +9
  5: unchanged by the upgrade to +3, +7
  6: unchanged by the upgrade to +6
Not the proposal: today's 70% hybrid rule, rounded, would break the new rules in 83 places at +0.

$ python -m pytest tools/tests/test_affix_range_proposal.py
37 passed in 4.43s

$ python -I <scratchpad>/affixproof/prove_ladder.py      # tools/prove_guard.break_and_run, two breaks
== ladder minimum width   ("starts[k - 1] + MIN_VALUES_PER_TIER" -> "starts[k - 1] + 1")
PROVED: 10 failed, 27 passed in 3.40s | restored: 37 passed in 4.20s
  (first named: test_the_proposal_has_no_problems,
   test_every_live_affix_has_seven_whole_contiguous_tiers_of_two_or_more)
== upgrade rounding half up   ("return (scaled + 50) // 100" -> "return scaled // 100")
PROVED: 3 failed, 34 passed in 4.22s | restored: 37 passed in 4.02s
  test_an_upgrade_rounds_exactly_and_a_half_goes_up
  test_every_upgrade_level_is_the_plus_zero_number_times_its_multiplier
  test_flat_damage_grows_with_upgrades_on_the_owners_six_slots

$ python tools/affix_range_proposal.py apply --source docs/All_Things_Cataclysm.xlsx --out <scratchpad>/affixproof/applied_copy_v2.xlsx
wrote ...applied_copy_v2.xlsx: 71 tier rows, 17 slot lists, 2 hybrid halves, 2 retired rows, 76 implicits

$ python -u -m pytest --junit-xml=<scratchpad>/affixproof/junit2.xml      # the whole suite, once
8253 passed, 9 skipped in 349.17s (0:05:49)
# JUnit: tests 8262, failures 0, errors 0, skipped 9 -> 8253 passed

$ python -m ruff check .
All checks passed!
```

`apply` refuses the real workbook as its destination. `test_apply_refuses_the_real_workbook` shows
that against a stand-in copy, and the copy test checks the real workbook's hash is unchanged. Six
tests break the table, the rounding or the slot lists on purpose and show each check fires.

## Appendix A: what a player sees at every upgrade level

One table per distinct +0 ladder; every affix named under a ladder shows exactly these numbers.
Each cell is the tier's range at that level; a two-handed weapon doubles each end before the
rounding.

#### Ladder A: Flat maximum health

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 12-13 | 14-16 | 17-20 | 21-24 | 25-29 | 30-35 | 36-48 |
| +1 | 1.15 | 14-15 | 16-18 | 20-23 | 24-28 | 29-33 | 35-40 | 41-55 |
| +2 | 1.30 | 16-17 | 18-21 | 22-26 | 27-31 | 33-38 | 39-46 | 47-62 |
| +3 | 1.45 | 17-19 | 20-23 | 25-29 | 30-35 | 36-42 | 44-51 | 52-70 |
| +4 | 1.60 | 19-21 | 22-26 | 27-32 | 34-38 | 40-46 | 48-56 | 58-77 |
| +5 | 1.75 | 21-23 | 25-28 | 30-35 | 37-42 | 44-51 | 53-61 | 63-84 |
| +6 | 1.90 | 23-25 | 27-30 | 32-38 | 40-46 | 48-55 | 57-67 | 68-91 |
| +7 | 2.05 | 25-27 | 29-33 | 35-41 | 43-49 | 51-59 | 62-72 | 74-98 |
| +8 | 2.20 | 26-29 | 31-35 | 37-44 | 46-53 | 55-64 | 66-77 | 79-106 |
| +9 | 2.35 | 28-31 | 33-38 | 40-47 | 49-56 | 59-68 | 71-82 | 85-113 |
| +10 | 2.50 | 30-33 | 35-40 | 43-50 | 53-60 | 63-73 | 75-88 | 90-120 |

#### Ladder B: Increased maximum health, Increased maximum mana, Increased maximum energy shield, Increased armor, Increased evasion, Increased health regeneration, Increased mana regeneration, Increased energy shield regeneration, Increased attack speed, Increased area of effect, Flat magic find, Increased loot quantity, All resistances, Chance to poison, Increased agility, Increased ferocity, Increased constitution, Increased vitality, Increased mind, Increased spirit, Increased efficacy, Increased luck, Increased minion health, Increased minion attack speed

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 |
| +1 | 1.15 | 3-5 | 6-7 | 8-9 | 10-12 | 13-14 | 15-16 | 17-18 |
| +2 | 1.30 | 4-5 | 7-8 | 9-10 | 12-13 | 14-16 | 17-18 | 20-21 |
| +3 | 1.45 | 4-6 | 7-9 | 10-12 | 13-15 | 16-17 | 19-20 | 22-23 |
| +4 | 1.60 | 5-6 | 8-10 | 11-13 | 14-16 | 18-19 | 21-22 | 24-26 |
| +5 | 1.75 | 5-7 | 9-11 | 12-14 | 16-18 | 19-21 | 23-25 | 26-28 |
| +6 | 1.90 | 6-8 | 10-11 | 13-15 | 17-19 | 21-23 | 25-27 | 29-30 |
| +7 | 2.05 | 6-8 | 10-12 | 14-16 | 18-21 | 23-25 | 27-29 | 31-33 |
| +8 | 2.20 | 7-9 | 11-13 | 15-18 | 20-22 | 24-26 | 29-31 | 33-35 |
| +9 | 2.35 | 7-9 | 12-14 | 16-19 | 21-24 | 26-28 | 31-33 | 35-38 |
| +10 | 2.50 | 8-10 | 13-15 | 18-20 | 23-25 | 28-30 | 33-35 | 38-40 |

#### Ladder C: Flat maximum mana

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 16-17 |
| +1 | 1.15 | 5-6 | 7-8 | 9-10 | 12-13 | 14-15 | 16-17 | 18-20 |
| +2 | 1.30 | 5-7 | 8-9 | 10-12 | 13-14 | 16-17 | 18-20 | 21-22 |
| +3 | 1.45 | 6-7 | 9-10 | 12-13 | 15-16 | 17-19 | 20-22 | 23-25 |
| +4 | 1.60 | 6-8 | 10-11 | 13-14 | 16-18 | 19-21 | 22-24 | 26-27 |
| +5 | 1.75 | 7-9 | 11-12 | 14-16 | 18-19 | 21-23 | 25-26 | 28-30 |
| +6 | 1.90 | 8-10 | 11-13 | 15-17 | 19-21 | 23-25 | 27-29 | 30-32 |
| +7 | 2.05 | 8-10 | 12-14 | 16-18 | 21-23 | 25-27 | 29-31 | 33-35 |
| +8 | 2.20 | 9-11 | 13-15 | 18-20 | 22-24 | 26-29 | 31-33 | 35-37 |
| +9 | 2.35 | 9-12 | 14-16 | 19-21 | 24-26 | 28-31 | 33-35 | 38-40 |
| +10 | 2.50 | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 | 40-43 |

#### Ladder D: Flat maximum energy shield

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 17-20 |
| +1 | 1.15 | 6-7 | 8-9 | 10-12 | 13-14 | 15-16 | 17-18 | 20-23 |
| +2 | 1.30 | 7-8 | 9-10 | 12-13 | 14-16 | 17-18 | 20-21 | 22-26 |
| +3 | 1.45 | 7-9 | 10-12 | 13-15 | 16-17 | 19-20 | 22-23 | 25-29 |
| +4 | 1.60 | 8-10 | 11-13 | 14-16 | 18-19 | 21-22 | 24-26 | 27-32 |
| +5 | 1.75 | 9-11 | 12-14 | 16-18 | 19-21 | 23-25 | 26-28 | 30-35 |
| +6 | 1.90 | 10-11 | 13-15 | 17-19 | 21-23 | 25-27 | 29-30 | 32-38 |
| +7 | 2.05 | 10-12 | 14-16 | 18-21 | 23-25 | 27-29 | 31-33 | 35-41 |
| +8 | 2.20 | 11-13 | 15-18 | 20-22 | 24-26 | 29-31 | 33-35 | 37-44 |
| +9 | 2.35 | 12-14 | 16-19 | 21-24 | 26-28 | 31-33 | 35-38 | 40-47 |
| +10 | 2.50 | 13-15 | 18-20 | 23-25 | 28-30 | 33-35 | 38-40 | 43-50 |

#### Ladder E: Flat armor

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 25-29 | 30-35 | 36-42 | 43-49 | 50-59 | 60-74 | 75-100 |
| +1 | 1.15 | 29-33 | 35-40 | 41-48 | 49-56 | 58-68 | 69-85 | 86-115 |
| +2 | 1.30 | 33-38 | 39-46 | 47-55 | 56-64 | 65-77 | 78-96 | 98-130 |
| +3 | 1.45 | 36-42 | 44-51 | 52-61 | 62-71 | 73-86 | 87-107 | 109-145 |
| +4 | 1.60 | 40-46 | 48-56 | 58-67 | 69-78 | 80-94 | 96-118 | 120-160 |
| +5 | 1.75 | 44-51 | 53-61 | 63-74 | 75-86 | 88-103 | 105-130 | 131-175 |
| +6 | 1.90 | 48-55 | 57-67 | 68-80 | 82-93 | 95-112 | 114-141 | 143-190 |
| +7 | 2.05 | 51-59 | 62-72 | 74-86 | 88-100 | 103-121 | 123-152 | 154-205 |
| +8 | 2.20 | 55-64 | 66-77 | 79-92 | 95-108 | 110-130 | 132-163 | 165-220 |
| +9 | 2.35 | 59-68 | 71-82 | 85-99 | 101-115 | 118-139 | 141-174 | 176-235 |
| +10 | 2.50 | 63-73 | 75-88 | 90-105 | 108-123 | 125-148 | 150-185 | 188-250 |

#### Ladder F: Flat evasion, Flat life leech, Flat block chance, Flat damage reduction, Flat retaliation, Flat crowd control resistance, Flat penetration, Flat cooldown reduction, Flat mana leech, Flat energy shield leech

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 |
| +1 | 1.15 | 1-2 | 3-5 | 6-7 | 8-9 | 10-12 | 13-14 | 15-16 |
| +2 | 1.30 | 1-3 | 4-5 | 7-8 | 9-10 | 12-13 | 14-16 | 17-18 |
| +3 | 1.45 | 1-3 | 4-6 | 7-9 | 10-12 | 13-15 | 16-17 | 19-20 |
| +4 | 1.60 | 2-3 | 5-6 | 8-10 | 11-13 | 14-16 | 18-19 | 21-22 |
| +5 | 1.75 | 2-4 | 5-7 | 9-11 | 12-14 | 16-18 | 19-21 | 23-25 |
| +6 | 1.90 | 2-4 | 6-8 | 10-11 | 13-15 | 17-19 | 21-23 | 25-27 |
| +7 | 2.05 | 2-4 | 6-8 | 10-12 | 14-16 | 18-21 | 23-25 | 27-29 |
| +8 | 2.20 | 2-4 | 7-9 | 11-13 | 15-18 | 20-22 | 24-26 | 29-31 |
| +9 | 2.35 | 2-5 | 7-9 | 12-14 | 16-19 | 21-24 | 26-28 | 31-33 |
| +10 | 2.50 | 3-5 | 8-10 | 13-15 | 18-20 | 23-25 | 28-30 | 33-35 |

#### Ladder G: Flat damage, Flat maximum class resource, Flat health regeneration, Flat critical strike multiplier, Chance to bleed, Chance to disease, Chance to apply void splinter, Chance to necrose, Chance to madden, Chance to cripple, Chance to weaken, Chance to shred, Chance to burn, Chance to stun, Flat energy shield regeneration

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 |
| +1 | 1.15 | 2-3 | 5-6 | 7-8 | 9-10 | 12-13 | 14-15 | 16-17 |
| +2 | 1.30 | 3-4 | 5-7 | 8-9 | 10-12 | 13-14 | 16-17 | 18-20 |
| +3 | 1.45 | 3-4 | 6-7 | 9-10 | 12-13 | 15-16 | 17-19 | 20-22 |
| +4 | 1.60 | 3-5 | 6-8 | 10-11 | 13-14 | 16-18 | 19-21 | 22-24 |
| +5 | 1.75 | 4-5 | 7-9 | 11-12 | 14-16 | 18-19 | 21-23 | 25-26 |
| +6 | 1.90 | 4-6 | 8-10 | 11-13 | 15-17 | 19-21 | 23-25 | 27-29 |
| +7 | 2.05 | 4-6 | 8-10 | 12-14 | 16-18 | 21-23 | 25-27 | 29-31 |
| +8 | 2.20 | 4-7 | 9-11 | 13-15 | 18-20 | 22-24 | 26-29 | 31-33 |
| +9 | 2.35 | 5-7 | 9-12 | 14-16 | 19-21 | 24-26 | 28-31 | 33-35 |
| +10 | 2.50 | 5-8 | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 |

#### Ladder H: Increased damage, Increased spell damage, Increased minion damage

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 13-15 | 16-18 | 19-21 | 22-26 | 27-31 | 32-37 | 38-50 |
| +1 | 1.15 | 15-17 | 18-21 | 22-24 | 25-30 | 31-36 | 37-43 | 44-58 |
| +2 | 1.30 | 17-20 | 21-23 | 25-27 | 29-34 | 35-40 | 42-48 | 49-65 |
| +3 | 1.45 | 19-22 | 23-26 | 28-30 | 32-38 | 39-45 | 46-54 | 55-73 |
| +4 | 1.60 | 21-24 | 26-29 | 30-34 | 35-42 | 43-50 | 51-59 | 61-80 |
| +5 | 1.75 | 23-26 | 28-32 | 33-37 | 39-46 | 47-54 | 56-65 | 67-88 |
| +6 | 1.90 | 25-29 | 30-34 | 36-40 | 42-49 | 51-59 | 61-70 | 72-95 |
| +7 | 2.05 | 27-31 | 33-37 | 39-43 | 45-53 | 55-64 | 66-76 | 78-103 |
| +8 | 2.20 | 29-33 | 35-40 | 42-46 | 48-57 | 59-68 | 70-81 | 84-110 |
| +9 | 2.35 | 31-35 | 38-42 | 45-49 | 52-61 | 63-73 | 75-87 | 89-118 |
| +10 | 2.50 | 33-38 | 40-45 | 48-53 | 55-65 | 68-78 | 80-93 | 95-125 |

#### Ladder I: Increased critical strike chance

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 10-11 | 12-13 | 14-16 | 17-20 | 21-24 | 25-29 | 30-40 |
| +1 | 1.15 | 12-13 | 14-15 | 16-18 | 20-23 | 24-28 | 29-33 | 35-46 |
| +2 | 1.30 | 13-14 | 16-17 | 18-21 | 22-26 | 27-31 | 33-38 | 39-52 |
| +3 | 1.45 | 15-16 | 17-19 | 20-23 | 25-29 | 30-35 | 36-42 | 44-58 |
| +4 | 1.60 | 16-18 | 19-21 | 22-26 | 27-32 | 34-38 | 40-46 | 48-64 |
| +5 | 1.75 | 18-19 | 21-23 | 25-28 | 30-35 | 37-42 | 44-51 | 53-70 |
| +6 | 1.90 | 19-21 | 23-25 | 27-30 | 32-38 | 40-46 | 48-55 | 57-76 |
| +7 | 2.05 | 21-23 | 25-27 | 29-33 | 35-41 | 43-49 | 51-59 | 62-82 |
| +8 | 2.20 | 22-24 | 26-29 | 31-35 | 37-44 | 46-53 | 55-64 | 66-88 |
| +9 | 2.35 | 24-26 | 28-31 | 33-38 | 40-47 | 49-56 | 59-68 | 71-94 |
| +10 | 2.50 | 25-28 | 30-33 | 35-40 | 43-50 | 53-60 | 63-73 | 75-100 |

#### Ladder J: Increased damage over time frequency, Increased damage over time, Increased damage over time duration

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 17-21 |
| +1 | 1.15 | 6-7 | 8-9 | 10-12 | 13-14 | 15-16 | 17-18 | 20-24 |
| +2 | 1.30 | 7-8 | 9-10 | 12-13 | 14-16 | 17-18 | 20-21 | 22-27 |
| +3 | 1.45 | 7-9 | 10-12 | 13-15 | 16-17 | 19-20 | 22-23 | 25-30 |
| +4 | 1.60 | 8-10 | 11-13 | 14-16 | 18-19 | 21-22 | 24-26 | 27-34 |
| +5 | 1.75 | 9-11 | 12-14 | 16-18 | 19-21 | 23-25 | 26-28 | 30-37 |
| +6 | 1.90 | 10-11 | 13-15 | 17-19 | 21-23 | 25-27 | 29-30 | 32-40 |
| +7 | 2.05 | 10-12 | 14-16 | 18-21 | 23-25 | 27-29 | 31-33 | 35-43 |
| +8 | 2.20 | 11-13 | 15-18 | 20-22 | 24-26 | 29-31 | 33-35 | 37-46 |
| +9 | 2.35 | 12-14 | 16-19 | 21-24 | 26-28 | 31-33 | 35-38 | 40-49 |
| +10 | 2.50 | 13-15 | 18-20 | 23-25 | 28-30 | 33-35 | 38-40 | 43-53 |

#### Ladder K: Increased movement speed

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 9-10 | 11-12 | 13-14 | 15-16 | 17-18 | 19-20 | 21-22 |
| +1 | 1.15 | 10-12 | 13-14 | 15-16 | 17-18 | 20-21 | 22-23 | 24-25 |
| +2 | 1.30 | 12-13 | 14-16 | 17-18 | 20-21 | 22-23 | 25-26 | 27-29 |
| +3 | 1.45 | 13-15 | 16-17 | 19-20 | 22-23 | 25-26 | 28-29 | 30-32 |
| +4 | 1.60 | 14-16 | 18-19 | 21-22 | 24-26 | 27-29 | 30-32 | 34-35 |
| +5 | 1.75 | 16-18 | 19-21 | 23-25 | 26-28 | 30-32 | 33-35 | 37-39 |
| +6 | 1.90 | 17-19 | 21-23 | 25-27 | 29-30 | 32-34 | 36-38 | 40-42 |
| +7 | 2.05 | 18-21 | 23-25 | 27-29 | 31-33 | 35-37 | 39-41 | 43-45 |
| +8 | 2.20 | 20-22 | 24-26 | 29-31 | 33-35 | 37-40 | 42-44 | 46-48 |
| +9 | 2.35 | 21-24 | 26-28 | 31-33 | 35-38 | 40-42 | 45-47 | 49-52 |
| +10 | 2.50 | 23-25 | 28-30 | 33-35 | 38-40 | 43-45 | 48-50 | 53-55 |

#### Ladder L: Single resistance

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 10-11 | 12-13 | 14-15 | 16-17 | 18-19 | 20-21 | 22-23 |
| +1 | 1.15 | 12-13 | 14-15 | 16-17 | 18-20 | 21-22 | 23-24 | 25-26 |
| +2 | 1.30 | 13-14 | 16-17 | 18-20 | 21-22 | 23-25 | 26-27 | 29-30 |
| +3 | 1.45 | 15-16 | 17-19 | 20-22 | 23-25 | 26-28 | 29-30 | 32-33 |
| +4 | 1.60 | 16-18 | 19-21 | 22-24 | 26-27 | 29-30 | 32-34 | 35-37 |
| +5 | 1.75 | 18-19 | 21-23 | 25-26 | 28-30 | 32-33 | 35-37 | 39-40 |
| +6 | 1.90 | 19-21 | 23-25 | 27-29 | 30-32 | 34-36 | 38-40 | 42-44 |
| +7 | 2.05 | 21-23 | 25-27 | 29-31 | 33-35 | 37-39 | 41-43 | 45-47 |
| +8 | 2.20 | 22-24 | 26-29 | 31-33 | 35-37 | 40-42 | 44-46 | 48-51 |
| +9 | 2.35 | 24-26 | 28-31 | 33-35 | 38-40 | 42-45 | 47-49 | 52-54 |
| +10 | 2.50 | 25-28 | 30-33 | 35-38 | 40-43 | 45-48 | 50-53 | 55-58 |

#### Ladder M: Two resistances

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 17-18 | 19-20 |
| +1 | 1.15 | 8-9 | 10-12 | 13-14 | 15-16 | 17-18 | 20-21 | 22-23 |
| +2 | 1.30 | 9-10 | 12-13 | 14-16 | 17-18 | 20-21 | 22-23 | 25-26 |
| +3 | 1.45 | 10-12 | 13-15 | 16-17 | 19-20 | 22-23 | 25-26 | 28-29 |
| +4 | 1.60 | 11-13 | 14-16 | 18-19 | 21-22 | 24-26 | 27-29 | 30-32 |
| +5 | 1.75 | 12-14 | 16-18 | 19-21 | 23-25 | 26-28 | 30-32 | 33-35 |
| +6 | 1.90 | 13-15 | 17-19 | 21-23 | 25-27 | 29-30 | 32-34 | 36-38 |
| +7 | 2.05 | 14-16 | 18-21 | 23-25 | 27-29 | 31-33 | 35-37 | 39-41 |
| +8 | 2.20 | 15-18 | 20-22 | 24-26 | 29-31 | 33-35 | 37-40 | 42-44 |
| +9 | 2.35 | 16-19 | 21-24 | 26-28 | 31-33 | 35-38 | 40-42 | 45-47 |
| +10 | 2.50 | 18-20 | 23-25 | 28-30 | 33-35 | 38-40 | 43-45 | 48-50 |

#### Ladder N: Increased damage against War enemies, Increased damage against Demonic enemies, Increased damage against Death enemies, Increased damage against Pestilence enemies, Increased damage against Famine enemies, Increased damage against Celestial enemies, Increased damage against Chaos enemies, Increased damage against Void enemies

| Level | x | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| +0 | 1.00 | 40-47 | 48-59 | 60-69 | 70-84 | 85-99 | 100-119 | 120-160 |
| +1 | 1.15 | 46-54 | 55-68 | 69-79 | 81-97 | 98-114 | 115-137 | 138-184 |
| +2 | 1.30 | 52-61 | 62-77 | 78-90 | 91-109 | 111-129 | 130-155 | 156-208 |
| +3 | 1.45 | 58-68 | 70-86 | 87-100 | 102-122 | 123-144 | 145-173 | 174-232 |
| +4 | 1.60 | 64-75 | 77-94 | 96-110 | 112-134 | 136-158 | 160-190 | 192-256 |
| +5 | 1.75 | 70-82 | 84-103 | 105-121 | 123-147 | 149-173 | 175-208 | 210-280 |
| +6 | 1.90 | 76-89 | 91-112 | 114-131 | 133-160 | 162-188 | 190-226 | 228-304 |
| +7 | 2.05 | 82-96 | 98-121 | 123-141 | 144-172 | 174-203 | 205-244 | 246-328 |
| +8 | 2.20 | 88-103 | 106-130 | 132-152 | 154-185 | 187-218 | 220-262 | 264-352 |
| +9 | 2.35 | 94-110 | 113-139 | 141-162 | 165-197 | 200-233 | 235-280 | 282-376 |
| +10 | 2.50 | 100-118 | 120-148 | 150-173 | 175-210 | 213-248 | 250-298 | 300-400 |

## Appendix B: today, every affix and every tier

At +0, what a fresh drop shows; tier 7 also at +10. A band is continuous today, so the last column
counts the whole numbers a tier contains.

| Affix | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T7 at +10 | decimals | overlapping pairs | narrowest width | fewest whole numbers in a tier |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: | :-: | --: | --: | --: |
| Flat maximum health | 12-16 | 14.41-19.21 | 17.31-23.08 | 20.78-27.71 | 24.96-33.28 | 29.98-39.97 | 36-48 | 90-120 | yes | 6 of 6 | 4 | 5 |
| Increased maximum health | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat maximum mana | 3.8-5.07 | 4.56-6.08 | 5.48-7.31 | 6.58-8.78 | 7.9-10.54 | 9.49-12.66 | 11.4-15.2 | 28.5-38 | yes | 6 of 6 | 1.27 | 2 |
| Increased maximum mana | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat maximum energy shield | 5-6.67 | 6-8.01 | 7.21-9.61 | 8.66-11.55 | 10.4-13.87 | 12.49-16.65 | 15-20 | 37.5-50 | yes | 6 of 6 | 1.67 | 2 |
| Increased maximum energy shield | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat armor | 25-33.33 | 30.02-40.03 | 36.06-48.07 | 43.3-57.74 | 52-69.34 | 62.45-83.27 | 75-100 | 187.5-250 | yes | 6 of 6 | 8.33 | 9 |
| Increased armor | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat evasion | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Increased evasion | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat damage | 2.2-2.93 | 2.64-3.52 | 3.17-4.23 | 3.81-5.08 | 4.58-6.1 | 5.5-7.33 | 6.6-8.8 | 16.5-22 | yes | 6 of 6 | 0.73 | 0 |
| Increased damage | 12.5-16.67 | 15.01-20.02 | 18.03-24.04 | 21.65-28.87 | 26-34.67 | 31.23-41.63 | 37.5-50 | 93.75-125 | yes | 6 of 6 | 4.17 | 4 |
| Increased spell damage | 12.5-16.67 | 15.01-20.02 | 18.03-24.04 | 21.65-28.87 | 26-34.67 | 31.23-41.63 | 37.5-50 | 93.75-125 | yes | 6 of 6 | 4.17 | 4 |
| Flat maximum class resource | 1.75-1.94 | 1.87-2.1 | 2.01-2.29 | 2.18-2.51 | 2.38-2.78 | 2.62-3.11 | 2.92-3.5 | 5.54-7 | yes | 6 of 6 | 0.19 | 0 |
| Flat health regeneration | 2-2.22 | 2.13-2.4 | 2.29-2.62 | 2.49-2.87 | 2.72-3.18 | 3-3.55 | 3.33-4 | 6.33-8 | yes | 6 of 6 | 0.22 | 0 |
| Increased health regeneration | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat mana regeneration | 0.2-0.22 | 0.21-0.23 | 0.22-0.25 | 0.24-0.27 | 0.25-0.29 | 0.27-0.32 | 0.3-0.35 | 0.53-0.65 | yes | 6 of 6 | 0.02 | 0 |
| Increased mana regeneration | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased energy shield regeneration | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat life leech | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Flat block chance | 1.25-1.39 | 1.33-1.5 | 1.43-1.63 | 1.56-1.8 | 1.7-1.99 | 1.87-2.22 | 2.08-2.5 | 3.96-5 | yes | 6 of 6 | 0.14 | 0 |
| Flat damage reduction | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Flat retaliation | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Flat crowd control resistance | 1.25-1.39 | 1.33-1.5 | 1.43-1.63 | 1.56-1.8 | 1.7-1.99 | 1.87-2.22 | 2.08-2.5 | 3.96-5 | yes | 6 of 6 | 0.14 | 0 |
| Flat critical strike chance | 1.25-1.39 | 1.33-1.5 | 1.43-1.63 | 1.56-1.8 | 1.7-1.99 | 1.87-2.22 | 2.08-2.5 | 3.96-5 | yes | 6 of 6 | 0.14 | 0 |
| Increased critical strike chance | 2.5-3.33 | 3-4 | 3.61-4.81 | 4.33-5.77 | 5.2-6.93 | 6.25-8.33 | 7.5-10 | 18.75-25 | yes | 6 of 6 | 0.83 | 1 |
| Flat critical strike multiplier | 2-2.67 | 2.4-3.2 | 2.88-3.85 | 3.46-4.62 | 4.16-5.55 | 5-6.66 | 6-8 | 15-20 | yes | 6 of 6 | 0.67 | 1 |
| Increased attack speed | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Increased area of effect | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased damage over time frequency | 5.2-6.93 | 6.24-8.33 | 7.5-10 | 9.01-12.01 | 10.82-14.42 | 12.99-17.32 | 15.6-20.8 | 39-52 | yes | 6 of 6 | 1.73 | 1 |
| Flat penetration | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Increased movement speed | 8.75-9.72 | 9.34-10.5 | 10.04-11.44 | 10.89-12.57 | 11.9-13.92 | 13.12-15.55 | 14.58-17.5 | 27.71-35 | yes | 6 of 6 | 0.97 | 1 |
| Flat cooldown reduction | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat magic find | 2.5-2.78 | 2.67-3 | 2.87-3.27 | 3.11-3.59 | 3.4-3.98 | 3.75-4.44 | 4.17-5 | 7.92-10 | yes | 6 of 6 | 0.28 | 0 |
| Increased loot quantity | 2-2.22 | 2.13-2.4 | 2.29-2.62 | 2.49-2.87 | 2.72-3.18 | 3-3.55 | 3.33-4 | 6.33-8 | yes | 6 of 6 | 0.22 | 0 |
| Single resistance | 10-11.11 | 10.67-12 | 11.47-13.08 | 12.44-14.36 | 13.6-15.91 | 14.99-17.77 | 16.67-20 | 31.67-40 | yes | 6 of 6 | 1.11 | 2 |
| Two resistances | 7-7.78 | 7.47-8.4 | 8.03-9.15 | 8.71-10.06 | 9.52-11.14 | 10.5-12.44 | 11.67-14 | 22.17-28 | yes | 6 of 6 | 0.78 | 1 |
| All resistances | 3-3.33 | 3.2-3.6 | 3.44-3.92 | 3.73-4.31 | 4.08-4.77 | 4.5-5.33 | 5-6 | 9.5-12 | yes | 6 of 6 | 0.33 | 0 |
| Chance to bleed | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to poison | 2.5-3.33 | 3-4 | 3.61-4.81 | 4.33-5.77 | 5.2-6.93 | 6.25-8.33 | 7.5-10 | 18.75-25 | yes | 6 of 6 | 0.83 | 1 |
| Chance to disease | 2-2.67 | 2.4-3.2 | 2.88-3.85 | 3.46-4.62 | 4.16-5.55 | 5-6.66 | 6-8 | 15-20 | yes | 6 of 6 | 0.67 | 1 |
| Chance to apply void splinter | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to necrose | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to madden | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to cripple | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to weaken | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to shred | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to burn | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to stun | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Increased agility | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased ferocity | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased constitution | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased vitality | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased mind | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased spirit | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased efficacy | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased luck | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat mana leech | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Flat energy shield leech | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Increased damage against War enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Demonic enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Death enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Pestilence enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Famine enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Celestial enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Chaos enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage against Void enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage over time | 5.2-6.93 | 6.24-8.33 | 7.5-10 | 9.01-12.01 | 10.82-14.42 | 12.99-17.32 | 15.6-20.8 | 39-52 | yes | 6 of 6 | 1.73 | 1 |
| Increased damage over time duration | 5.2-6.93 | 6.24-8.33 | 7.5-10 | 9.01-12.01 | 10.82-14.42 | 12.99-17.32 | 15.6-20.8 | 39-52 | yes | 6 of 6 | 1.73 | 1 |
| Increased minion damage | 12.5-16.67 | 15.01-20.02 | 18.03-24.04 | 21.65-28.87 | 26-34.67 | 31.23-41.63 | 37.5-50 | 93.75-125 | yes | 6 of 6 | 4.17 | 4 |
| Increased minion health | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased minion attack speed | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Flat energy shield regeneration | 1-1.33 | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 7.5-10 | yes | 6 of 6 | 0.33 | 0 |
