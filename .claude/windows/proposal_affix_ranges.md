# Affix ranges: whole numbers, one range per tier

**A proposal, 2026-11-24. Nothing in the game, the CSVs or the design workbook is changed.**
Branch `proposal/affix-ranges`. The script is `tools/affix_range_proposal.py` and its tests are
`tools/tests/test_affix_range_proposal.py`; every table below is printed by that script.

The project owner, 2026-11-24, verbatim:

> take a look at the values and ranges of affixes. Some of them feel arbitrarily low. Like 1.2% and
> stuff like that. I really hate decimals, and they should be removed in all cases, but also, each
> affix tier should have it's own range, and that range should be something a PLAYER finds
> reasonable. Not math. So if your range is .5-3% for something like flat damage, that's probably
> dumb.

## What changes your decision, first

1. **The 1.2% is real and it is the bottom of the ladder.** Increased maximum health rolls 1.2-1.6%
   at tier 1 on a fresh (+0) drop. Flat damage rolls 2.2-2.9 at tier 1 and 6.6-8.8 at tier 7 on a
   fresh drop. All 73 single-stat, resistance and ailment affixes show a decimal somewhere; in all 73
   every pair of adjacent tiers overlaps (by design, see below); 48 of the 73 have a tier that
   contains **no whole number at all** on a fresh drop.
2. **Whole numbers plus seven separate ranges need room.** Seven tiers of at least two whole numbers
   each is fourteen whole numbers from the worst tier 1 roll to the best tier 7 roll.
3. **The upgrade multiplier is what takes the room away.** Every value stated today is the +10
   figure, and a fresh drop gets 1/2.5 of it. A 12% affix has 4.8% to spread seven tiers across on a
   fresh drop. If upgrades keep multiplying every affix, **59 of the 73 affixes need a larger top**
   to fit, median **x3.33** (increased health 12 -> 40, single resistance 40 -> 58, all resistances
   12 -> 40, crit multiplier 20 -> 38; column "If upgrades multiplied every affix" below).
4. **The recommendation therefore splits the upgrade lever by what a number measures.** Upgrades keep
   multiplying amounts (health, mana, shield, armour, attack damage, regeneration, class resource) and
   every implicit; a percentage affix is the number its tier states at every upgrade level. With that,
   **39 of 71 surviving affixes** change their top and the design's derived figures (125%, 400%, 52%,
   40/28 resistance, ailment = gem + 5, 35% movement speed, 120 health, 250 armour) stay **exactly**.
   This reverses the 2026-08-03 choice that upgrades multiply every affix (it was made "for now", with
   the measured imbalance recorded). That is decision D1.

## The rule, as one paragraph

**A tier is a range of whole numbers written in the workbook, contiguous with the tiers either side
and never overlapping them, holding at least two values. A roll lands on one of those whole numbers,
each equally likely. An affix that adds an amount is multiplied by the piece's upgrade level and
rounded once, half up; its written range is the +0 value. An affix that is a percentage is the number
its tier states at every upgrade level. A two-handed weapon doubles both. A hybrid at tier N grants
each half as that half's own affix at tier N - 2. Implicits keep the upgrade lever and are written as
whole +0 values.**

In detail:

| # | Rule | Where it comes from |
| :-- | :-- | :-- |
| 1 | Each affix row carries T1 Min, T1 Max ... T7 Min, T7 Max: whole numbers, `T(n+1) Min = T(n) Max + 1`, at least two values per tier | Owner 2026-11-24; contiguous non-overlap is Path of Exile's and Last Epoch's shape (Research) |
| 2 | A roll in [0, 1] picks one of the tier's whole numbers, each with an equal share | `UCataclysmItemValues::EnchantmentValue`, already the enchantment rule ("every written value is equally likely") |
| 3 | "Amount" is read off the sheet: a Stat row that is `flat` and whose `Percent` column says no. Those, and implicits, are multiplied by `1 + 0.15 x level` and rounded half up, once, in the maths | The rounding rule is "How an Attribute Rounds": "The rounded number is the only number." |
| 4 | Every other affix (an increase, a `Percent = yes` row, a resistance, an ailment chance) is its stated number at +0 and at +10 | Decision D1 |
| 5 | A hybrid rolls tiers 3 to 7 only, and at tier N grants each half at that half's tier N - 2 range | Two tiers down on today's ladder is 3^(-1/3) = 0.693, the design's 70% within one per cent. Decision D6 |
| 6 | The `Floor` column, `TIER_FRACTIONS`, `ROLL_BAND_FRACTION` and `WORST_MULTIPLIER` stop producing values. The written tier 1 minimum is the floor | The owner's "a number per affix, not one rule" (2026-09-04) becomes a range per tier |

Because the upgrade multiplier is never below 1, two different whole numbers stay different after it
and after rounding, so no two tiers can meet at any upgrade level. `problems()` checks that by
enumeration: every affix, every tier boundary, every upgrade level from +0 to +10, one-handed and
two-handed. It reports none.

## Research

Sources fetched for this proposal on 2026-11-24. Pages that refused the fetch are listed at the end.

| Game | What the page shows | Source |
| :-- | :-- | :-- |
| Path of Exile | Fire resistance suffix ladder, item level 1 to 84: 6-11, 12-17, 18-23, 24-29, 30-35, 36-41, 42-45, 46-48. Whole, contiguous, never overlapping, about six wide, narrower at the top | Official item data, https://www.pathofexile.com/item-data/mods |
| Path of Exile | Maximum life prefix ladder: 3-9, 10-24, 25-39 ... 160-174, 175-189 (fifteen wide). Global attack speed: 5-7, 8-10, 11-13 ... 23-25, 26-27. All elemental resistances: 3-5, 6-8, 9-11, 12-14, 15-16. Item rarity suffixes: 6-10, 11-14, 15-20, 21-26 | Same page |
| Path of Exile | **Its decimals**: physical attack damage leeched as life (0.2-0.4)%, (0.6-0.8)%, (1-1.2)%; and life regeneration per second (2.1-8), (8.1-16) ... The two stats whose natural size is a fraction | Same page; https://poedb.tw/us/Life_Leech gives "0.6% of Physical Attack Damage Leeched as Life" |
| Path of Exile | Global critical strike chance on amulets: 10-14, 15-19, 20-24, 25-29, 30-34, 35-38. An **increase**, not flat points: base crit lives on the weapon or skill | https://poedb.tw/us/Critical_strike |
| Path of Exile | Movement speed on boots: **single values** 10, 15, 20, 25, 30, 35 | https://poedb.tw/us/Movement_speed |
| Last Epoch | Developer blog (Trasochi, 2020-08-03): Chance to Bleed T5 32%-42%, T6 43%-49%, T7 50%-60%; "+7 to All Attributes" at T7 (a single value); "T5 and T7 affixes generally have a similar range while T6 usually has a smaller range" | https://forum.lastepoch.com/t/introducing-tier-6-and-7-item-affixes/22279 |
| Last Epoch | Patch notes posted by a community member: Added Health T6 95-120, T7 121-160; Increased Health T6 15-20%, T7 21-26%; a hybrid's attack speed T5 12-15%, T6 19-21%, T7 22-25% (a gap between T5 and T6) | https://forum.lastepoch.com/t/last-epoch-harbingers-of-ruin-patch-notes/71790/10 |
| Last Epoch | Seven tiers; "Each tier also has a roll range so even if two items have the same affix in the same tier" they may differ | https://maxroll.gg/last-epoch/resources/gear-walkthrough |
| Diablo IV | Season 15 affix list: "+6.5-8.5% Critical Strike Chance" (gloves), "+3.5-5% Critical Strike Chance" (ring), "+325-400 Resistance to All Elements", "100-121 Dexterity", "263-316 Life On Hit". Percentages carry a decimal; flat values are whole. No tiers are shown, one range per affix | https://op.gg/diablo4/affixes |
| Diablo IV | Players, November 2023: "Ancestral Rings: 1.8 - 5% crit chance" | https://us.forums.blizzard.com/en/d4/t/whats-the-max-crit-chance-you-can-roll-on-gloves-and-rings/140607 |

**What the research settles.**

- **Contiguous, non-overlapping tiers are the genre shape.** Every ordinary Path of Exile ladder read
  (fire resistance, life, all resistances, attack speed, critical strike chance and multiplier,
  rarity) and both Last Epoch fragments start each tier one above the last. The exceptions on the
  Path of Exile page are separate variants rather than rungs of one ladder (influenced, essence and
  item-class versions, such as two level 4 attack speed rows reading 8-15 and 15-25). Last Epoch
  sometimes leaves a gap. Today's deliberate one-tier overlap has no counterpart in what was read.
- **Whole numbers are the genre's norm and not its absolute.** Path of Exile uses decimals for leech
  and regeneration, the stats whose natural size is a fraction; Diablo IV uses a decimal on every
  percentage. "Removed in all cases" is stricter than all three games, so for the small stats it can
  only be met by changing their scale or their unit. That part is this game's judgement.
- **A crit-chance affix on gear is an increase in Path of Exile**, reading a base the weapon or skill
  supplies. That is the precedent for D5.
- **None of the three multiplies a percentage affix by an upgrade and shows it as a whole number.**
  Path of Exile and Last Epoch tie an affix's value to its tier alone. Diablo IV's percentage ranges
  move with item power and it prints decimals. This is the basis for D1. (Diablo IV's masterworking
  was not fetched and is not cited.)

**What it does not settle, and is judgement:** where tier 1 should start (Path of Exile's lowest
increases start at 5 to 10, its all-resistance at 3); how wide each tier is (Path of Exile keeps the
width roughly constant, Last Epoch varies it); and the single-value question, where the genre does
use single values (Path of Exile movement speed, Last Epoch "+7 to All Attributes") and the brief
forbids them. This proposal follows the brief.

**Refused or unreadable**, so nothing from them is used: `www.lastepochtools.com` (403),
`support.lastepoch.com` (403), `www.vhpg.com` (404), `mobalytics.gg` (403), `www.icy-veins.com/d4`
(403). A Last Epoch fire resistance ladder appeared only in a search engine's summary of a vhpg page
and is not quoted.

## Today: where affixes are defined and how a value is made

- **Defined** in the `Affixes` sheet of `docs/All_Things_Cataclysm.xlsx` (one row per affix: `Top
  Value`, `Floor`, `Percent`, `Allowed Slots`, hybrid halves), generated by
  `tools/generate_datatables.py` (`affixes()`) into `game/Data/Affixes.csv`. The `Affix Tiers` sheet
  holds only drop weights (64, 32 ... 1). The simulation's copy is `sim/cataclysm_sim/affixes.py`,
  compared by `tools/tests/test_affix_sheets_match_the_model.py`.
- **Rolled** by `UCataclysmItemValues::AffixValue` (`game/Source/Cataclysm/Items/CataclysmItem.cpp`):
  `top x 3^((tier-7)/6)`, a continuous roll in a band from 75% to 100% of that, divided by 2.5 and
  multiplied by `1 + 0.15 x upgrade level`, then the floor's affine map, then x2 on a two-handed
  weapon. A hybrid half is 70% of its part. The same function is `affix_value` in the simulation.
- **Shown** by `UCataclysmItemTooltip::AffixLine`, through `NumberInWords`, which prints a whole
  number when the value is within 0.005 of one and one decimal otherwise, then "(tier N)".
- **Implicits** (`Item Bases` sheet) are stated at +10 and divided by 2.5 at +0. Hood's 4 evasion is
  1.6 on a fresh drop; Mantle's is 3.5 even at +10.
- **Enchantments** state ranges in their sentence and are not multiplied by upgrades. 13 of 575 rows
  carry a decimal (listed under Enchantments below).

The full inventory, every affix and every tier, is Appendix A. In summary:

| Fact | Count |
| :-- | --: |
| Single-stat, resistance and ailment affixes | 73 (and 13 hybrids) |
| ... showing a decimal at some tier and upgrade level | 73 |
| ... whose adjacent tiers overlap at +0 | 73, all six boundaries each |
| ... with a tier containing no whole number at +0 | 48 |
| ... with a tier containing exactly one | 9 |

The overlap is by design. `docs/Cataclysm_GDD_v2.md`, "Every Tier Is a Range": "Bands overlap
between adjacent tiers, and that is intended."

## What constrains the values

Read from `docs/Cataclysm_GDD_v2.md` (Affixes section), `docs/DECISIONS.md` (2026-08-03 gear level,
2026-08-05 400%, 2026-08-16 flat damage 22, 2026-09-03 both ladders eased, 2026-09-04 floors), the
simulation and the tests. Each was measured before and after with `python
tools/affix_range_proposal.py measure`, which runs the simulation's own functions with its value
methods patched to the proposal.

| Rule | Today | Proposed | Status |
| :-- | --: | --: | :-- |
| Increased damage, spell damage, minion damage at T7 (owner's 125%) | 125 | 125 | kept exactly |
| Damage against one type, 400% (125 x 3.33 rounded) | 400 | 400 | kept exactly |
| Damage over time levers solve (1 + 2v)^3 = 8.5 | 52 | 52 | kept exactly |
| Resistance families 40 / 28, and the hybrid share 28/40 = 0.7 | 40 / 28 | 40 / 28 | kept exactly |
| All resistances, and "capping all eight at tier 8 costs about 12 slots" | 12, 12.1 slots | 16, 9.1 slots | **moves** (room) |
| Ailment chance = its gem's chance + 5 | 15 / 25 / 20 | 15 / 25 / 20 | kept exactly |
| Movement speed 35% on boots only | 35 | 35 | kept exactly |
| Flat health 120 (6% of a level 100 base), flat armour 250, flat shield 50 | same | same | kept exactly |
| Flat damage 22, derived from the 1,860 damage target and the Axe + Sword pair | 22 | 38 | **moves**, D3 |
| Flat vs increased damage crossover (8 increased in place) | 194 base | 334 base | **moves** with flat damage |
| Weapon base the reference build needs (6 flat + 6 increased) | 86.9 | -9.1 | **moves**; holds if the reference spends 3 flat slots |
| Flat vs increased health crossover (+200% increases) | 3,000 base | 2,250 base | **moves** (increased 12 -> 16) |
| Attribute affix = dedicated affix at 50 points | 12 = 12 | 16 = 16 | kept (both moved together) |
| Hybrid worth 1.4 affixes (70% each half) | 0.70 | 0.54 to 0.77 at T7 | **moves a little**, D6 |
| Energy shield refills in 5 s (flat shield 50 / flat regen 10) | 5 s | 50 / 38 = 1.3 s | **moves**, D4 |
| Damage reduction "the one defensive stat that would run away" (cap 75) | 4 x 14 pieces = 56 | 14 x 4 pieces = 56 | ceiling kept by narrowing, D4 |

**Damage per non-critical hit of the reference character, by difficulty tier** (the simulation's
`player_damage.damage_per_hit`; target 1,860 at tier 8):

| Difficulty tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| Today (6 flat + 6 increased) | 221 | 316 | 458 | 668 | 983 | 1,458 | 1,650 | 1,853 |
| Proposed, the reference unchanged | 255 | 411 | 661 | 977 | 1,540 | 2,320 | 2,465 | 2,660 |
| Proposed, the reference spending 4 flat slots | 216 | 337 | 526 | 761 | 1,182 | 1,760 | 1,870 | 2,014 |
| **Proposed, the reference spending 3 flat slots** | 196 | 299 | 458 | 653 | 1,003 | 1,479 | 1,572 | 1,692 |
| D3 option b: flat damage 3-22, not upgraded | 238 | 350 | 509 | 699 | 1,102 | 1,760 | 1,802 | 1,844 |

Three flat slots hold today's curve within -11% to +2% at every tier. The cause of the unchanged
reference's +44% at tier 8 is flat damage: increased damage on the reference path is within +3% to
+14% of today's (the most at difficulty tier 6, where the reference carries +8 and a tier 7 affix is
now its full 125), because the new percentage ladder was built to start where a tier 1 character
starts today (+3, worst roll: 14.5% of the top) and to end at today's top.

**The reference build as written, at tier 8, before anyone re-fits it** (`reference_build.gear`):
block 20 -> 56, damage reduction 16 -> 56, penetration 16 -> 56, increased crit chance 100% -> 400%,
increased health 72% -> 96%, resistance per type 144 -> 192. It spends four slots on each of block,
damage reduction and penetration; each is now worth 2.8 to 3.5 times as much per slot, so a
re-fitted reference spends one or two slots on each and the rest go elsewhere. That re-fit is the
implementer's, and `enemy_stats.py` constants fitted against this build must be re-checked after it.

## The proposed table

Every affix, seven tiers. **For an amount ("Upgrades? yes") the tiers are the +0 values** and a +10
piece is 2.5 times, rounded; the next table gives those. For everything else the tiers are the value
at every upgrade level. A two-handed weapon doubles any of them.

| Affix | Upgrades? | T1 | T2 | T3 | T4 | T5 | T6 | T7 | Top at +10 (was) | Pieces (was) | If upgrades multiplied every affix | Change |
| :-- | :-: | --: | --: | --: | --: | --: | --: | --: | --: | --: | --: | :-- |
| Flat maximum health | yes | 12-13 | 14-16 | 17-20 | 21-24 | 25-29 | 30-35 | 36-48 | 120 (120) | 14 | 120 |  |
| Increased maximum health | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised: the smallest top that holds seven two-value tiers |
| Flat maximum mana | yes | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 16-17 | 43 (38) | 13 | 43 | raised: the smallest top that holds seven two-value tiers |
| Increased maximum mana | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 13 | 40 | raised |
| Flat maximum energy shield | yes | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 17-20 | 50 (50) | 12 | 50 |  |
| Increased maximum energy shield | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Flat armor | yes | 25-29 | 30-35 | 36-42 | 43-49 | 50-59 | 60-74 | 75-100 | 250 (250) | 14 | 250 |  |
| Increased armor | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Flat evasion | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 4 (14) | 35 | top 4 -> 14, four pieces instead of fourteen: 56 either way |
| Increased evasion | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Flat damage | yes | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 38 (22) | 12 | 38 | raised; D3 |
| Increased damage | no | 18-23 | 24-30 | 31-40 | 41-54 | 55-69 | 70-94 | 95-125 | 125 (125) | 12 | 125 |  |
| Increased spell damage | no | 18-23 | 24-30 | 31-40 | 41-54 | 55-69 | 70-94 | 95-125 | 125 (125) | 12 | 125 |  |
| Flat maximum class resource | yes | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 38 (7) | 2 (11) | 38 | top 7 -> 38 at +10, two pieces instead of eleven: 76 against 77 |
| Flat health regeneration | yes | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 38 (8) | 3 (12) | 38 | top 8 -> 38 at +10, three pieces instead of twelve: 114 against 96 |
| Increased health regeneration | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Flat mana regeneration | | retired into Increased mana regeneration | | | | | | | -- (0.65) | -- (12) | 38 | D5: 0.2-0.65 a second has no whole numbers at all; every class has a base for the increase to read |
| Increased mana regeneration | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Increased energy shield regeneration | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Flat life leech | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 3 (12) | 35 | top 4 -> 14, three pieces (four with an offhand) instead of twelve |
| Flat block chance | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (5) | 5 (14) | 35 | top 5 -> 14, five pieces instead of fourteen: 70 either way |
| Flat damage reduction | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 4 (14) | 35 | top 4 -> 14, four pieces instead of fourteen: 56 either way |
| Flat retaliation | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 4 (14) | 35 | top 4 -> 14, four pieces instead of fourteen: 56 either way |
| Flat crowd control resistance | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (5) | 5 (14) | 35 | top 5 -> 14, five pieces instead of fourteen: 70 either way |
| Flat critical strike chance | | retired into Increased critical strike chance | | | | | | | -- (5) | -- (12) | 35 | D5: 1.25-5 points cannot hold seven whole ranges |
| Increased critical strike chance | no | 14-18 | 19-23 | 24-31 | 32-42 | 43-54 | 55-74 | 75-100 | 100 (25) | 12 | 40 | D5: absorbs the flat affix; 100% increased is 5 points at the 5% default base, the old flat top |
| Flat critical strike multiplier | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-20 | 20 (20) | 12 | 38 |  |
| Increased attack speed | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (15) | 12 | 40 | raised |
| Increased area of effect | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Increased damage over time frequency | no | 8-9 | 10-13 | 14-17 | 18-22 | 23-29 | 30-38 | 39-52 | 52 (52) | 12 | 53 |  |
| Flat penetration | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 3 (12) | 35 | top 4 -> 14, three pieces instead of twelve; past an enemy's resistance (35 at most) it is wasted anyway |
| Increased movement speed | no | 9-10 | 11-12 | 13-14 | 15-17 | 18-21 | 22-25 | 26-35 | 35 (35) | 1 | 55 |  |
| Flat cooldown reduction | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (12) | 12 | 35 | raised |
| Flat magic find | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (10) | 12 | 40 | raised |
| Increased loot quantity | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (8) | 12 | 40 | raised |
| Single resistance | no | 10-11 | 12-13 | 14-16 | 17-20 | 21-24 | 25-29 | 30-40 | 40 (40) | 17 | 58 |  |
| Two resistances | no | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 17-20 | 21-28 | 28 (28) | 17 | 50 |  |
| All resistances | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 17 | 40 | raised |
| Chance to bleed | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to poison | no | 4-5 | 6-7 | 8-9 | 10-11 | 12-14 | 15-18 | 19-25 | 25 (25) | 11 | 40 |  |
| Chance to disease | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-20 | 20 (20) | 11 | 38 |  |
| Chance to apply void splinter | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to necrose | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to madden | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to cripple | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to weaken | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to shred | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to burn | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Chance to stun | no | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 15 (15) | 11 | 38 |  |
| Increased agility | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Increased ferocity | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Increased constitution | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Increased vitality | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 15 | 40 | raised |
| Increased mind | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Increased spirit | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Increased efficacy | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Increased luck | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 12 | 40 | raised |
| Flat mana leech | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 3 (12) | 35 | as life leech |
| Flat energy shield leech | no | 1-2 | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 14 (4) | 3 (12) | 35 | as life leech |
| Increased damage against (each of the eight) enemies | no | 58-74 | 75-99 | 100-129 | 130-174 | 175-229 | 230-299 | 300-400 | 400 (400) | 12 | 400 | the same ladder for all eight |
| Increased damage over time | no | 8-9 | 10-13 | 14-17 | 18-22 | 23-29 | 30-38 | 39-52 | 52 (52) | 12 | 53 |  |
| Increased damage over time duration | no | 8-9 | 10-13 | 14-17 | 18-22 | 23-29 | 30-38 | 39-52 | 52 (52) | 12 | 53 |  |
| Increased minion damage | no | 18-23 | 24-30 | 31-40 | 41-54 | 55-69 | 70-94 | 95-125 | 125 (125) | 12 | 125 |  |
| Increased minion health | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (12) | 14 | 40 | raised |
| Increased minion attack speed | no | 3-4 | 5-6 | 7-8 | 9-10 | 11-12 | 13-14 | 15-16 | 16 (15) | 12 | 40 | raised |
| Flat energy shield regeneration | yes | 2-3 | 4-5 | 6-7 | 8-9 | 10-11 | 12-13 | 14-15 | 38 (10) | 3 (12) | 38 | top 10 -> 38 at +10, three pieces instead of twelve: 114 against 120 |

"Pieces" counts the eight rings as eight. "raised" means the top moved to the smallest value that
holds seven two-value tiers above the tier 1 start, and no further.

**The amounts on a +10 piece** (2.5 times the table, rounded half up):

| Affix | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
| :-- | --: | --: | --: | --: | --: | --: | --: |
| Flat maximum health | 30-33 | 35-40 | 43-50 | 53-60 | 63-73 | 75-88 | 90-120 |
| Flat maximum mana | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 | 40-43 |
| Flat maximum energy shield | 13-15 | 18-20 | 23-25 | 28-30 | 33-35 | 38-40 | 43-50 |
| Flat armor | 63-73 | 75-88 | 90-105 | 108-123 | 125-148 | 150-185 | 188-250 |
| Flat damage | 5-8 | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 |
| Flat maximum class resource | 5-8 | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 |
| Flat health regeneration | 5-8 | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 |
| Flat energy shield regeneration | 5-8 | 10-13 | 15-18 | 20-23 | 25-28 | 30-33 | 35-38 |

**How the ladders were built** (a judgement, labelled as one). The script builds them rather than
storing them, so the same rule makes every row and a test can hold it; the workbook would hold the
numbers, which a person then edits freely.

- **Tier 7 starts at three quarters of the top**, which is where today's top band starts.
- **Tier 1 starts where a tier 1 roll starts today** on the piece a difficulty tier 1 character
  carries: for a percentage that is 14.5% of the top (today's worst tier 1 roll at +3), for an amount
  it is today's worst +0 roll; never below the owner's 2026-09-04 floors, and never below 3 for an
  increase, 1 for percentage points, 2 for an amount (D7).
- **Tiers 1 to 6 share the rest on a constant ratio**, back-loaded like today's ladder, each at least
  two values; boundaries from 50 snap to fives and from 200 to tens, so they read like written ranges.
- **Where that cannot hold fourteen whole numbers, the top rises to the smallest that can**, and the
  ladder becomes two-wide steps (3-4, 5-6 ... 15-16).
- **Where the top had to rise several-fold, the slot list narrows so the most a character can carry
  from the stat stays where it is** (D4). That is the movement-speed rule the owner approved on
  2026-09-04: the top and the slot list move together.

### Hybrids

A hybrid at tier N grants each half as that half's own affix at tier N - 2, and rolls tiers 3 to 7.

| Hybrid | Halves | Hybrid T3 at +0 | Hybrid T7 at +0 | Hybrid T7 at +10 | Share of the single affix at T7 | Slots (was) |
| :-- | :-- | --: | --: | --: | --: | :-- |
| Health and armor | Flat maximum health; Flat armor | 12-13; 25-29 | 25-29; 50-59 | 63-73; 125-148 | 0.61; 0.59 | Belt, Boots, Chest, Head, Pants, Ring, Shoulders |
| Health and energy shield | Flat maximum health; Flat maximum energy shield | 12-13; 5-6 | 25-29; 13-14 | 63-73; 33-35 | 0.61; 0.70 | Chest, Head, Ring, Shoulders |
| Armor and evasion | Flat armor; Flat evasion | 25-29; 1-2 | 50-59; 9-10 | 125-148; 9-10 | 0.59; 0.71 | Boots, Chest, Head (was also Pants, Ring, Shoulders) |
| Evasion and energy shield | Flat evasion; Flat maximum energy shield | 1-2; 5-6 | 9-10; 13-14 | 9-10; 33-35 | 0.71; 0.70 | Chest, Head (was also Ring, Shoulders) |
| Mana and energy shield | Flat maximum mana; Flat maximum energy shield | 4-5; 5-6 | 12-13; 13-14 | 30-33; 33-35 | 0.77; 0.70 | Chest, Head, Relic, Ring |
| Increased health and armor | Increased maximum health; Increased armor | 3-4; 3-4 | 11-12; 11-12 | 11-12; 11-12 | 0.75; 0.75 | Belt, Boots, Chest, Head, Pants, Ring, Shoulders |
| Attack speed and critical strike chance | Increased attack speed; Increased critical strike chance | 3-4; 14-18 | 11-12; 43-54 | 11-12; 43-54 | 0.75; 0.54 | Gloves, Necklace, Relic, Ring, Weapon |
| Critical strike chance and multiplier | **Increased** critical strike chance; Flat critical strike multiplier | 14-18; 3-4 | 43-54; 11-12 | 43-54; 11-12 | 0.54; 0.60 | Gloves, Necklace, Relic, Ring, Weapon |
| Health and mana regeneration | Flat health regeneration; **Increased** mana regeneration | 2-3; 3-4 | 10-11; 11-12 | 25-28; 11-12 | 0.74; 0.75 | Belt, Chest, Necklace (was also Boots, Ring) |
| Penetration and critical strike multiplier | Flat penetration; Flat critical strike multiplier | 1-2; 3-4 | 9-10; 11-12 | 9-10; 11-12 | 0.71; 0.60 | Gloves, Relic, Weapon (was also Necklace, Ring) |
| Block chance and crowd control resistance | Flat block chance; Flat crowd control resistance | 1-2; 1-2 | 9-10; 9-10 | 9-10; 9-10 | 0.71; 0.71 | Belt, Head, Pants, Shoulders (was also Boots, Chest, Ring) |
| Magic find and loot quantity | Flat magic find; Increased loot quantity | 3-4; 3-4 | 11-12; 11-12 | 11-12; 11-12 | 0.75; 0.75 | Belt, Boots, Necklace, Relic, Ring |
| Minion damage and minion health | Increased minion damage; Increased minion health | 18-23; 3-4 | 55-69; 11-12 | 55-69; 11-12 | 0.55; 0.75 | Ring |

Two halves are renamed because their affix is retired (bold). Five hybrids lose slots because a half
narrowed: "A hybrid can never appear on a slot one of its halves could not."

**Why not today's 70%, rounded.** `python tools/affix_range_proposal.py check` lists 81 places at +0
where multiplying a whole-number ladder by 0.7 and rounding would make two adjacent hybrid tiers
share a value or turn a two-value tier into one (for example Increased health and armor's tier 7
becomes 11-11). Two whole numbers one apart are 0.7 apart after the multiplier and round together.

### Implicits

Restated as whole +0 values, at least 1, multiplied by upgrades and rounded. 46 of the 76 keep their
+10 figure; the 30 that move are all a rounding of today's +0 figure:

| Base | Implicit | Today +0 | Today +10 | Proposed +0 | Proposed +10 |
| :-- | :-- | --: | --: | --: | --: |
| Hood | evasion | 1.6 | 4 | 2 | 5 |
| Circlet, Locket | energy shield regeneration | 4.4 | 11 | 4 | 10 |
| Visage | crowd control resistance | 1.6 | 4 | 2 | 5 |
| Vestment | energy shield regeneration | 9.6 | 24 | 10 | 25 |
| Mantle | evasion | 1.4 | 3.5 | 1 | 3 |
| Epaulets | health regeneration | 0.44 | 1.1 | 1 | 3 |
| Spaulders | retaliation | 4.4 | 11 | 4 | 10 |
| Grips | increased attack speed | 3.6 | 9 | 4 | 10 |
| Vambraces | attack damage | 4.8 | 12 | 5 | 13 |
| Treads | increased movement speed | 4.8 | 12 | 5 | 13 |
| Sollerets | increased movement speed | 2.4 | 6 | 2 | 5 |
| Cord | health regeneration | 0.52 | 1.3 | 1 | 3 |
| Signet | crit multiplier | 6.4 | 16 | 6 | 15 |
| Circle | mana regeneration | 0.2 | 0.5 | 1 | 3 |
| Pendant | crit chance | 2.4 | 6 | 2 | 5 |
| Dagger, Axe | attack damage | 10.4, 18.4 | 26, 46 | 10, 18 | 25, 45 |
| Whip | attack damage, increased area | 12.8, 4.8 | 32, 12 | 13, 5 | 33, 13 |
| Shield | block chance | 4.8 | 12 | 5 | 13 |
| Greataxe (x2) | attack damage, crit multiplier | 28.8, 8.8 | 72, 22 | 29, 9 | 73, 23 |
| Spear (x2) | attack damage, penetration | 25.6, 2.4 | 64, 6 | 26, 2 | 65, 5 |
| Staff (x2) | attack damage, increased spell damage | 26.4, 12.8 | 66, 32 | 26, 13 | 65, 33 |
| Two-Handed Crossbow (x2) | attack damage, crit chance | 26.4, 2.8 | 66, 7 | 26, 3 | 65, 8 |
| Warhammer (x2) | attack damage | 33.6 | 84 | 34 | 85 |

The full list is `python tools/affix_range_proposal.py table`. Two-handed figures are before the x2.
The Axe + Sword pair the damage target is fitted to moves from 86 to 85 at +10.

### Enchantments

Thirteen rows state a decimal. They are not multiplied by upgrades, so the fix is a rewrite of the
sentence, and the generator's rule that an effect row's two values are a range its sentence states
carries it into the roll. Seven can keep the same rate in whole numbers; six are durations under two
seconds, where the owner has to choose (D8). Weights and tags are untouched.

| Row today | Proposed sentence | Rate |
| :-- | :-- | :-- |
| Your damage is increased by 0.01%-0.05% permanently for every 1000 enemies killed this run | ... by 1%-5% ... for every 100,000 enemies killed this run | same |
| Your maximum HP is increased by 0.01%-0.05% permanently for every 1000 enemies killed this run | ... by 1%-5% ... for every 100,000 enemies killed this run | same |
| Each class point spent above 100 grants 0.5%-1% increased damage | Every 2 class points spent above 100 grant 1%-2% increased damage | same |
| Your melee skills have +0.5-1 metre reach | ... +50-100 centimetres reach | same |
| Your skills deal 1.5%-2.5% less damage for every 10 class points spent above 100 (negative) | ... 3%-5% less damage for every 20 class points ... | same |
| Your maximum HP is reduced by 1.5%-2.5% for every 10 class points above 50 (negative) | ... 3%-5% for every 20 class points ... | same |
| Brute's Heart 10-piece: "reduced by 0.5 seconds per kill" | reduced by 1 second for every 2 kills | same |
| Enemies you stagger are also briefly rooted for 0.5-1.5 seconds | ... 1-2 seconds | stronger (D8) |
| Your heavy attack cooldown is reduced by 0.5-1.5 seconds each time you land a critical strike | ... 1-2 seconds ... | stronger (D8) |
| Each spell cast reduces your next spell cooldown by 0.5-1.5 seconds | ... 1-2 seconds | stronger (D8) |
| Point blank AOE skills have a 0.75-1.5 second cast delay before firing (negative) | ... 1-2 second ... | harsher (D8) |
| Critical strikes trigger a 0.5-1 second global cooldown on all your skills (negative) | ... 1-2 second ... | harsher (D8) |
| After using a charge skill you are briefly stunned for 0.5-1 second (negative) | ... 1-2 seconds | harsher (D8) |

## Decisions the owner must make

| # | Decision | Recommendation | Cost of the recommendation | The alternative |
| :-- | :-- | :-- | :-- | :-- |
| D1 | Do upgrades multiply percentage affixes? | **No**: upgrades multiply amounts and implicits; a percentage is its tier's number at every upgrade level | Reverses the 2026-08-03 choice "gear upgrade level multiplies every affix". A ring whose four affixes are all percentages gains only its implicit from upgrading. It also removes the recorded imbalance of that entry, where gear level multiplied both damage brackets (9.58x damage against 1.56x Power Score; "the flat bracket only" measured 1.64x) | Keep it on everything: 59 of 73 tops must rise, median x3.33, including single resistance 40 -> 58, all resistances 12 -> 40, every 12% increase -> 40, crit multiplier 20 -> 38 |
| D2 | Tiers never overlap | **Yes** | Reverses "Bands overlap between adjacent tiers, and that is intended" and the test `Cataclysm.Item.BandsOverlapByExactlyOneTier`. The reroll and perfect materials still act: every tier holds at least two values | Keep a one-tier overlap; needs fewer whole numbers per ladder |
| D3 | Flat damage, the owner's own example | **(a) 2-3 ... 14-15 at +0, 35-38 at +10, and the reference build spends 3 slots on it instead of 6** (damage curve within -11% to +2% of today at every tier) | Flat damage per slot x1.7; the flat/increased crossover moves from 194 to 334 base, so flat stays the better slot for longer | (b) 3-22, not multiplied by upgrades: +5% to +21% damage at tiers 1 to 7, the same at tier 8; (c) scale all attack damage up (weapons, flat affix, spell and minion bases, and enemy health) so flat damage reads in tens: a game-wide rescale |
| D4 | Twelve small stats (block, damage reduction, retaliation, crowd control resistance, three leeches, penetration, flat evasion, class resource, flat health and shield regeneration) | **Raise the top to fit seven whole ranges and narrow the slot list so the ceiling holds** | Each becomes a decision about 2 to 5 pieces instead of 11 to 14. Evasion's "fifteen slots reach the cap" becomes about five. The shield's 5-second refill becomes 1.3 s | Keep the slot lists: the ceilings rise x2.8 to x5.4 (damage reduction 196 against a 75 cap) |
| D5 | Two affixes that cannot be whole at any reasonable scale | **Retire flat critical strike chance into increased critical strike chance (top 100%), and flat mana regeneration into increased mana regeneration** | Two row keys disappear: a saved item carrying one needs an alias at load, as the 2026-09-30 enchantment alias did. Crit chance from gear then scales with the skill's and weapon's base | Keep them with single-value tiers, or a unit change (mana regeneration per minute) |
| D6 | Hybrids | **A hybrid at tier N grants each half at its own tier N - 2; hybrids roll tiers 3 to 7** | No hybrid drops at difficulty tier 1 (its cap is tier 2). Share of the single affix 0.54 to 0.77 at tier 7 against the design's 0.70 | 70% and round: 81 shared or single-value hybrid tiers at +0 |
| D7 | The lowest tier 1 | **3 for an increase, 1 for percentage points, 2 for an amount** | Every 12% increase becomes 3-4 ... 15-16 (top 16, x1.33) | Lowest 5 for an increase: top 18 (x1.5) |
| D8 | Enchantment durations under two seconds | **Whole seconds, 1-2** | Three positives get stronger and three negatives harsher | Keep half seconds for time only |
| D9 | Implicits as whole +0 values | **Yes** | Small moves, largest relative: Epaulets and Cord health regeneration 1.1 / 1.3 -> 3, Circle mana regeneration 0.5 -> 3 at +10 | Keep the +10 figure and round on screen only, which "How an Attribute Rounds" rejects |

## What an implementation would touch

Nothing below was changed. For planning only.

- **Workbook**: `Affixes` gains T1 Min ... T7 Max and "Scales With Upgrade" (15 columns, no Excel
  table, validation or formatting on that sheet, so appending is safe); 17 slot lists, 2 hybrid
  halves, 2 rows retired; `Item Bases` gains "Implicit 1 At +0" and "Implicit 2 At +0". `python
  tools/affix_range_proposal.py apply --out <copy>` does exactly this to a copy.
- **Generator and data**: `tools/generate_datatables.py` `affixes()` reads and validates the new
  columns; `FCataclysmAffixRow` gains them; `Top Value` and `Floor` retire.
- **Game**: `UCataclysmItemValues::AffixValue` becomes a table lookup, the `EnchantmentValue` roll, the
  multiplier for amounts only, one half-up rounding; `ImplicitValue` reads the +0 value; hybrids roll
  tier N - 2 and the drop roll and the forge bound hybrid tiers to 3 to 7;
  `UCataclysmItemTooltip::NumberInWords` then never sees a fraction. The tooltip could print the
  tier's range, "(tier 3: 7-8)", as Path of Exile's advanced description does.
- **Saves**: an item stores tier and roll, so it is revalued on load without a migration; the two
  retired row keys and any hybrid stored at tier 1 or 2 need mapping.
- **Simulation**: `sim/cataclysm_sim/affixes.py` (`affix_value`, `TIER_FRACTIONS`, floors, the pool's
  tops and slots), `reference_build.py` (spend), then the fitted constants in `enemy_stats.py`.
- **Tests that pin today's shape** and would need changing, read from their names and the figures
  they assert (none was run against the proposal): in `sim/tests/test_affixes.py`, the overlap tests
  (`test_the_bands_do_overlap_between_adjacent_tiers`,
  `test_a_perfect_roll_can_beat_the_tier_above_but_never_two_above`), the ladder tests
  (`test_the_tier_curve_is_geometric`, `test_the_two_ladders_multiply_to_about_ten`), the gear-level
  tests (`test_gear_level_lifts_increased_affixes_as_well_as_flat_ones`,
  `test_gear_level_lifts_resistance_affixes_too`), the floor tests, the crossover tests and
  `test_capping_at_tier_eight_costs_a_reasonable_share_of_slots`; in `tools/tests`,
  `test_an_energy_shield_refills_in_five_seconds.py`, `test_leech.py`, `test_attribute_affixes.py`;
  in C++, `Cataclysm.Item.BandsOverlapByExactlyOneTier`, `Cataclysm.Items.TheHybridShareIsTheResistanceRatio`
  and the tooltip tests that walk every affix.

## Judgements made here, each a judgement

- Where tier 1 starts and the minimums 3 / 1 / 2 (D7), from Path of Exile's lowest tiers and the
  owner's 2026-09-04 floors.
- The ladder shape: tier 7 from three quarters of the top, a constant ratio below it, boundaries
  snapped to fives and tens. Path of Exile keeps widths roughly constant instead; Last Epoch varies
  them. Either is defensible; this one keeps today's back-loaded feel.
- Which slots each narrowed stat keeps (D4). The rule is "hold the ceiling"; the particular pieces
  are a choice and any equal count serves.
- Increased crit chance's top of 100% (D5): one old flat affix's worth at the 5% default base.
- Mana 38 -> 43 rather than lowering its tier 1 to 2: today's worst roll, 3.8, was kept.
- Hybrids two tiers down (D6), chosen over stating 26 more ladders by hand.

## Findings outside this proposal

- `docs/Cataclysm_GDD_v2.md` "Affix Tiers" still says "**The curve is linear.** Tier N is worth N/7";
  the code has been geometric since 2026-09-03. "Every Tier Is a Range" says tier N is undercut by
  N - 1 "only when N is above 4"; on today's geometric ladder every boundary overlaps (Appendix A:
  six of six for all 73). "Resistance Affixes" gives 20 / 14 / 6 at T7 where the data has 40 / 28 /
  12 (doubled 2026-09-04); "Health and Damage Affixes" gives flat damage 18 where the data has 22
  (2026-08-16). The comment on `UCataclysmItemValues::HybridFraction` cites "14 against 20", and the
  class comment there still describes the linear ladder and "about 3.52 at +10". Not fixed here; the
  coordinator may want an issue.

## Evidence

All run in this worktree on `proposal/affix-ranges`.

```
$ python tools/affix_range_proposal.py check
0 problems.
Not the proposal: today's 70% hybrid rule, rounded, would break the new rules in 81 places at +0:
  ...

$ python -m pytest tools/tests/test_affix_range_proposal.py
29 passed in 3.08s

$ python -u -m pytest --junit-xml=<scratchpad>/affixproof/junit.xml     # the whole suite, once
8245 passed, 9 skipped in 388.38s (0:06:28)
# JUnit: tests 8254, failures 0, errors 0, skipped 9 -> 8245 passed

$ python -m ruff check .
All checks passed!

$ python -I <scratchpad>/affixproof/prove_ladder.py   # tools/prove_guard.break_and_run
# break: the ladder's minimum width, "starts[k - 1] + MIN_VALUES_PER_TIER" -> "starts[k - 1] + 1"
PROVED: 3 failed, 26 passed in 3.58s | restored: 29 passed in 3.32s
  tools/tests/test_affix_range_proposal.py::test_the_proposal_has_no_problems
  tools/tests/test_affix_range_proposal.py::test_every_live_affix_has_seven_whole_contiguous_tiers_of_two_or_more
  tools/tests/test_affix_range_proposal.py::test_the_command_line_check_passes

$ python tools/affix_range_proposal.py apply --source docs/All_Things_Cataclysm.xlsx --out <scratchpad>/affixproof/applied_copy.xlsx
wrote ...applied_copy.xlsx: 71 tier rows, 17 slot lists, 2 hybrid halves, 2 retired rows, 76 implicits
```

`apply` refuses the real workbook as its destination: `test_apply_refuses_the_real_workbook` points
the module at a stand-in copy and shows the refusal and an unchanged file; the real workbook's hash
is checked unchanged around the copy test. Four tests break a table on purpose and show each check
fires.

## Appendix A: today, every affix and every tier

At +0, what a fresh drop shows; tier 7 also at +10. Printed by `python tools/affix_range_proposal.py
inventory`. A band is continuous today, so the last column counts the whole numbers a tier contains:
the most a whole-number display could ever show of it.

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
| Chance to bleed, void splinter, necrose, madden, cripple, weaken, shred, burn, stun (each) | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Chance to poison | 2.5-3.33 | 3-4 | 3.61-4.81 | 4.33-5.77 | 5.2-6.93 | 6.25-8.33 | 7.5-10 | 18.75-25 | yes | 6 of 6 | 0.83 | 1 |
| Chance to disease | 2-2.67 | 2.4-3.2 | 2.88-3.85 | 3.46-4.62 | 4.16-5.55 | 5-6.66 | 6-8 | 15-20 | yes | 6 of 6 | 0.67 | 1 |
| Increased agility, ferocity, constitution, vitality, mind, spirit, efficacy, luck (each) | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Flat mana leech, flat energy shield leech (each) | 1-1.11 | 1.07-1.2 | 1.15-1.31 | 1.24-1.44 | 1.36-1.59 | 1.5-1.78 | 1.67-2 | 3.17-4 | yes | 6 of 6 | 0.11 | 0 |
| Increased damage against (each of the eight) enemies | 40-53.33 | 48.04-64.05 | 57.69-76.92 | 69.28-92.38 | 83.2-110.94 | 99.92-133.23 | 120-160 | 300-400 | yes | 6 of 6 | 13.33 | 14 |
| Increased damage over time, and its duration (each) | 5.2-6.93 | 6.24-8.33 | 7.5-10 | 9.01-12.01 | 10.82-14.42 | 12.99-17.32 | 15.6-20.8 | 39-52 | yes | 6 of 6 | 1.73 | 1 |
| Increased minion damage | 12.5-16.67 | 15.01-20.02 | 18.03-24.04 | 21.65-28.87 | 26-34.67 | 31.23-41.63 | 37.5-50 | 93.75-125 | yes | 6 of 6 | 4.17 | 4 |
| Increased minion health | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 3.6-4.8 | 9-12 | yes | 6 of 6 | 0.4 | 0 |
| Increased minion attack speed | 1.5-2 | 1.8-2.4 | 2.16-2.88 | 2.6-3.46 | 3.12-4.16 | 3.75-5 | 4.5-6 | 11.25-15 | yes | 6 of 6 | 0.5 | 0 |
| Flat energy shield regeneration | 1-1.33 | 1.2-1.6 | 1.44-1.92 | 1.73-2.31 | 2.08-2.77 | 2.5-3.33 | 3-4 | 7.5-10 | yes | 6 of 6 | 0.33 | 0 |

Rows with identical figures are merged here; the script prints one row per affix.
