# Enemy rebalance after the affix rescale (#2884): a proposal

**A proposal, 2026-11-24, measured in the simulation only. Nothing in the game, the CSVs or the design workbook is
changed.** Measurement branch `analysis/enemy-rebalance-2884` (pushed, not for merging): origin/development
`5cdd6bdc4`, then `feat/power-score-weights-2853` (`e16766979`, `0799b7b27`), `feat/drop-supply-2853` (`86bc8bc74` to
`2674149e6`) and the approved affix proposal (`0d77261e8` to `2cb507406`), all cherry-picked without a conflict, then
this work. The two rarity-cap commits at the bottom of the #2853 branches were left out: their content is
development's `5cdd6bdc4`. Script: `sim/analyse_enemy_rebalance_2884.py`. Tests: `tools/tests/test_enemy_rebalance_2884.py`.

The project owner, 2026-11-24 (#2883), verbatim: "upgrade everything, once we've fixed the ranges and values, we can
look at rebalancing enemies to fit" and, of flat damage, "It's fine, keep the 6 slots and upgrade the damage. We'll
fix it with the enemy balance audit like for the first question."

## Said first: what is uncertain or not done

1. **ENEMY DAMAGE RISES 15.35x AT TIER 8 AND 2.38x AT TIER 2.** That is what brings back today's survival table,
   because the re-fitted reference character is 15.35 times harder to kill at tier 8. **x5.73 of it is forced by the
   approved affixes and the narrowed slots; the remaining x2.68 is one judgement of mine**: where the 13 suffix slots
   go that block, damage reduction and resistance can no longer use. On Vitality and Constitution (adopted) the total
   is x15.35; left empty, x5.73; on offence where legal, x3.87. Question 1.
2. **FITTED THAT WAY, A BUILD THAT DOES NOT STACK DEFENCE AS THE REFERENCE DOES IS ONE-SHOT BY THE TIER 8
   GATEKEEPER**, and an Imp kills it in 16 to 23 hits rather than 60. Question 1.
3. **KILL TIME IN SECONDS IS NOT RESTORED, BY THE DESIGN'S OWN METHOD.** The damage target is stated in non-critical
   hits and hits are restored at every tier. But the re-fitted build swings much faster (attack speed 265% at tier 8
   against 60%) and crits harder, so it kills the tier 8 Boss in 2.8 s where today's takes 8.2 s. Question 2.
4. **THE CORRUPTED STALKER'S RULED TABLES CANNOT BE RE-FITTED WITHOUT A WINDOW.** They are fitted to the Stalker as
   the game builds it (`fight.GAME_BUILT_HEALTH`, `GAME_ARMOUR`, `GAME_BLOW`, window w60), and the Stalker wears the
   player's affixes. The simulation's own fit is given as an estimate.
5. **#2865 IS NOT ON THE ANALYSIS BRANCH**, as instructed. Both proposed factors are 1.00 at tier 1, so its ring
   scales are untouched by construction; the affix change reaches them only through the starting Greataxe, 57.6 -> 58
   attack damage at +0, in the character's favour. Checked on a temporary branch with #2865 cherry-picked on top: its
   guards pass at 57.6 and at 58 ("Tier 1 and #2865"). Its 60-seed sweep was not re-run.
6. **R3, R4 and R5 of the affix proposal (ailment chance against its gem, movement speed 55, damage over time levers
   53) are not enemy constants and are not answered here.** R1, R2 and R6 are (Questions 1 and 3, and the re-fit).
7. **The proposal is two new per-tier tables, not new values for `DAMAGE_AT_COMMON` and its siblings.** The two are
   the same at tier 8; the tables keep tier 1 where it was fitted. Judgement J1.

## Summary

| Constant | Today | Proposed | Why |
| :-- | :-- | :-- | :-- |
| **Enemy health factor by tier, 1 to 8** (new: `enemy_stats.HEALTH_SCALE_BY_TIER_2884`, applied where every creature is built) | none (1.00) | **1.00, 1.16, 1.28, 1.29, 1.31, 1.42, 1.42, 1.44** | The reference character's non-critical hit rose by these at tiers 2 to 8. An average Common takes today's hits at every tier; the tier 8 target of 2 hits holds (gap 0.99) |
| **Enemy damage factor by tier, 1 to 8** (new: `DAMAGE_SCALE_BY_TIER_2884`) | none (1.00) | **1.00, 2.38, 3.79, 5.28, 8.34, 12.31, 13.65, 15.35** | The tier's reference character is that many times harder to kill. Every creature takes today's hits to kill it; at tier 8 the design's survival table returns exactly |
| `HEALTH_AT_COMMON` 0.50, `HEALTH_PER_STEP` 1.85, `DAMAGE_AT_COMMON` 0.65, `DAMAGE_PER_STEP` 1.40, armour | | **unchanged** | Carried by the tables. At tier 8 the same as `HEALTH_AT_COMMON` 0.72 and `DAMAGE_AT_COMMON` 9.98. `DAMAGE_PER_STEP` re-solved by #108's method stays 1.40 |
| Tier 1 damage by ring (development 0.05 / 0.11 / 0.13 / 0.10; #2865 0.05 / 0.10 / 0.12 / 0.07) | | **unchanged** | Fitted to the starting character, which carries no affixes |
| The damage line of tiers 2 to 8 (0.20 at tier 1 to 1.00 at tier 8) | | **unchanged**, times the damage table | The line was fitted at its ends only |
| `HELLFIRE_AURA_BURN_SHARE_OF_WEAPON_DAMAGE` 0.09 | | **unchanged** | Fitted at tier 1; at tiers 2 to 8 the burn follows its carrier's hit |
| Reference build (`reference_build.py`) | block 4, damage reduction 4, penetration 4, all resistances 12, crit chance, multiplier and attack speed 4 each | **block 2, damage reduction 1, penetration 1, all resistances 4, crit chance, multiplier and attack speed 5 each, Increased Vitality 8, Increased Constitution 5**; prefixes unchanged (6 flat and 6 increased damage, as ruled) | The narrowed slots. Every affix placed on a piece that can roll it |
| `ENEMY_MITIGATION_CEILING` | 89% | **keep 89%**; its stated rule now gives 97% | Question 4 |
| Corrupted Stalker `HEALTH_MULTIPLIERS` / `DAMAGE_MULTIPLIERS` | 0.49 to 1.15 / 0.06 to 2.97 | **re-fit in a window once the affixes ship**; the simulation estimates 0.22 to 0.50 (0.45 at tier 8) / 0.05 to 35.98 | Its pieces gain the new affixes; its Boss hits 15.35x harder at tier 8 |
| Minion damage (player-side, fitted to the summoner's hit, not an enemy constant) | three Imps above the basic attack at every tier | **re-fit in its own slice** | Three Imps fall to 0.78x to 0.80x the summoner's basic attack at tiers 6 to 8. Question 5 |

## Method

**What was fitted against the reference character, found by searching `sim/` and `tools/tests/` for
`reference_build`, `player_damage.damage_per_hit`, `damage_target`, `fight.` and the enemy constants:**

| What | Fitted how, and where it says so | Moves with the affix change? |
| :-- | :-- | :-- |
| `DAMAGE_AT_COMMON` 0.65, `DAMAGE_PER_STEP` 1.40 | #108 (commit `6f3f00def`, 2026-08-03): "solved for two targets against that same build: an average Common enemy is a threat in a pack rather than alone, and the Gatekeeper kills in a couple of hits" at tier 8; `sim/tests/test_survivability.py`; the design's "How Long a Geared Character Survives" | **yes**: through the character's health and mitigation |
| Enemy health, through `affixes.damage_target` | Set on the enemy's own terms; the player's damage is fitted to it ("The Damage Target": an average Common at tier 8 "should take 2 non-critical strikes"); `player_damage` refuses at import a reference loadout more than 5% off it | **yes**: the player's hit rose, so health follows, as ruled |
| Damage line, tiers 2 to 8 (0.20 to 1.00) | #1569: the tier 1 end fitted to a level 20 character, the tier 8 end is the #108 fit; nothing between | through its tier 8 end |
| Tier 1 damage by ring, Hellfire Aura's 9% | #2749 and #2865: the starting character, level 1 and levelling, a gear-level-0 Greataxe, **no affixes** ("the method of 2026-11-19": the largest step of 0.01 at which every clear of the ring is won with 10% health left) | **no**, except the Greataxe's implicit at +0 (57.6 -> 58) |
| Corrupted Stalker multipliers | `fight.py`: as dangerous as the game's Boss on floor 25 of 50, against the tier's reference character | **yes**: player, Boss and Stalker all move |
| `ENEMY_MITIGATION_CEILING` 89 | "the reference geared character stops ... rounded down to a whole percent" | its rule does |
| Brute's slam under the stun threshold (`tools/tests/test_enemy_abilities.py`, 9.1%) | its hit as a share of the reference character's effective health | restored when the survival table is |

**The rule used to restore "the intended feel" at each tier is the project's own methods, re-applied per tier:**

- **Health.** The damage target is a number of non-critical hits per average Common, and the player's hit is
  `player_damage.damage_per_hit(tier)`. Health at a tier is multiplied by the new hit over today's hit, so the hits
  return. At tier 8 this is the "Damage Target" method re-applied (2 hits, gap 0.99, inside the 5% check).
- **Damage.** #108's method: hits to kill the reference character. It is re-applied at every tier with the tier's
  reference character (`fight.reference_player(tier)`: the tier's level, the build at the tier's affix tier and gear
  level), whose effective health (health over the share of a hit that lands, every layer) is divided by today's. With
  no energy shield and armour that does not depend on hit size, that share is the same for every creature, so one
  factor restores every creature's hits at once: at tier 8 exactly the design's table, and `DAMAGE_PER_STEP` stays
  1.40.
- **Tier 1 is held at 1.00 in both.** Its damage is fitted to a character with no affixes, which this change does not
  touch, and tier 1's health was never fitted to the geared character.

**THE SHAPE: A TABLE PER TIER, NOT A STRAIGHT LINE.** The factors are steeply convex (2.38 at tier 2, 15.35 at tier 8),
because the affix tiers and upgrade levels compound defensively: a straight line from tier 1 to tier 8 would make tier
2 about three times deadlier than today and tier 7 gentler. Path of Exile does the same: a monster's base damage and
life are a lookup table per monster level, and grow faster than linearly (damage 5.0 at level 1, 47 at 30, 822 at 84;
life 15, 359, 16,161), fetched 2026-11-24 from https://poedb.tw/us/Monster_level, which also says "The level of a
monster determines its life, damage, and defenses." **What the research settles:** a per-level table that grows
faster than a line is the genre's shape. **What it does not:** the numbers, which are measured here against this
game's reference character. Only that one source was fetched for this proposal; the ring slice's entry of 2026-11-19
cites Diablo II, Last Epoch and Diablo IV for monster strength belonging to the place, not re-read.

## The re-fit of the reference build (R6)

The proposal's own words: "The reference build as written cannot place four damage reduction or penetration affixes
now, and read literally reaches 140 of each." Re-fitted in `sim/cataclysm_sim/reference_build.py`; every choice is a
judgement, stated:

1. **Every affix keeps its old slot count where the pieces it can roll on allow it: min(old count, pieces).** All six
   prefixes keep theirs (8 flat health, 6 increased health, 6 flat armour, 4 increased armour, 6 flat damage, 6
   increased damage: the owner's "keep the 6 slots"), as do crit chance, crit multiplier and attack speed (4 each).
   Block 4 -> 2 (Shoulders, Belt), damage reduction 4 -> 1 (Pants), penetration 4 -> 1 (the weapon).
2. **All-resistance affixes: the count that caps all eight at tier 8**, the old build's own rule ("Twelve is roughly
   what capping all eight resistances at tier 8 costs"): `slots_to_cap` says 3.6, so 4, giving 160 against the 145
   needed. (R2: capping now costs 3.6 slots, not 12.1.)
3. **The build keeps its split between staying alive and killing things.** The 13 freed defensive suffix slots (2 of
   block, 3 of damage reduction, 8 of resistance) go on the only defensive suffixes left that can take them: the
   build's own attributes, **Increased Vitality 8 and Increased Constitution 5**, in its 60:40 split of attribute
   points. The 3 freed offensive slots (penetration) go one each on crit chance, crit multiplier and attack speed.
4. **Every affix is placed on a named piece** (`LAYOUT`), two prefixes and two suffixes, no affix group twice on a
   piece; `_check_every_affix_sits_on_a_piece_that_can_roll_it` refuses anything else at import, and three tests break
   it on purpose.
5. **Unchanged, a stated limit as before:** the Greatsword's own affixes are not doubled. All four are offensive.

| Piece | Prefixes | Suffixes |
| :-- | :-- | :-- |
| Helm | flat health, increased health | all resistances, increased vitality |
| Cuirass | flat health, increased health | all resistances, increased vitality |
| Pauldrons | flat health, flat armour | block, increased constitution |
| Gauntlets | flat damage, increased damage | increased crit chance, increased attack speed |
| Greaves | flat health, increased armour | damage reduction, increased vitality |
| Sabatons | flat health, flat armour | all resistances, increased constitution |
| Girdle | flat health, increased armour | block, increased vitality |
| Loop 1, 2 | flat health, increased health | all resistances and increased vitality; increased vitality and constitution |
| Loop 3, 4 | increased health, flat armour | increased vitality, increased constitution |
| Loop 5, 6 | flat armour, increased armour | crit chance and multiplier; crit chance and attack speed |
| Loop 7, 8 | flat damage, increased damage | multiplier and attack speed; crit chance and multiplier |
| Torc | flat damage, increased damage | crit multiplier, attack speed |
| Idol | flat damage, increased damage | crit chance, crit multiplier |
| Greatsword | flat damage, increased damage | penetration, attack speed |

**What the re-fit does to the character at tier 8** (`python -u sim/analyse_enemy_rebalance_2884.py`):

| | Today | Re-fitted | Note |
| :-- | --: | --: | :-- |
| Health | 11,023 | 39,486 | increased health 72% -> 318% from gear; vitality 60 -> 314 (8 x 53%) |
| Armour | 7,299 (53.3% of a hit) | 19,337 (75.0%, the cap) | **at the cap at every tier**, so the tier no longer changes it |
| Block | 28 | 172 (every hit blocked, halved) | 2 x 35 flat, times Constitution's increase |
| Damage reduction | 23.9 | 43.0 | |
| Resistance (after the tier 8 penalty of 75) | 144 (69%) | 160 (70%) | |
| **A hit lands for** | **9.17%** | **2.14%** | the player stops 97.86% |
| **Effective health** | **120,255** | **1,845,703** | **x15.35** |

**Where the x15.35 comes from**, each layer put back to today's alone: health x3.58, armour x1.87, block x1.72,
damage reduction x1.33, resistance x1.00.

**The judgement in choice 3 is worth a factor of 2.68 of it.** Tier by tier, effective health of the other ways to
spend the freed slots (same script, "sensitivity"):

| Re-fit | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T8 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| Today's build, today's affixes | 8,529 | 13,673 | 25,579 | 36,659 | 53,449 | 80,854 | 99,719 | 120,255 |
| **Adopted: Vitality 8, Constitution 5** | 14,300 | 32,491 | 96,965 | 193,378 | 445,981 | 995,644 | 1,360,846 | 1,845,703 |
| Freed slots left empty | 12,721 | 25,126 | 64,408 | 108,141 | 194,950 | 384,360 | 510,975 | 688,936 |
| Block on one piece, freed slots on offence where legal | 12,307 | 23,664 | 58,769 | 94,226 | 158,848 | 287,368 | 364,536 | 465,374 |
| Damage factor that would restore today, adopted / empty / offence | 1.68 / 1.49 / 1.44 | 2.38 / 1.84 / 1.73 | 3.79 / 2.52 / 2.30 | 5.28 / 2.95 / 2.57 | 8.34 / 3.65 / 2.97 | 12.31 / 4.75 / 3.55 | 13.65 / 5.12 / 3.66 | 15.35 / 5.73 / 3.87 |

## Before and after

Commands, from the repository root of the analysis branch; "before" is a `git archive` copy of `24c1c3810` (the branch
before this work: development, #2853's two branches, the proposal's files):

```
python -u sim/analyse_enemy_rebalance_2884.py --sim-root <copy of 24c1c3810>/sim --json before.json
python -u sim/analyse_enemy_rebalance_2884.py --tables today --json after_old.json      # new affixes, today's enemies
python -u sim/analyse_enemy_rebalance_2884.py --json after.json                         # new affixes, proposed enemies
python -u sim/analyse_enemy_rebalance_2884.py --fit before.json after_old.json          # prints the two tables
```

`--fit` printed (factors to four places; the proposal rounds to two):

```
   tier   hit ratio (health factor)   effective health ratio (damage factor)
      1                     1.0553                                   1.6766
      2                     1.1626                                   2.3762
      3                     1.2815                                   3.7908
      4                     1.2911                                   5.2750
      5                     1.3118                                   8.3441
      6                     1.4231                                  12.3141
      7                     1.4205                                  13.6468
      8                     1.4358                                  15.3482
   Tier 1 is held at 1.00 in both: its damage is fitted to the starting character, which carries no affixes.
```

**Per tier** (T = today; N = new affixes and re-fit with today's enemies; P = proposed):

| Tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| Reference hit, T / P | 221 / 233 | 316 / 368 | 458 / 587 | 668 / 863 | 983 / 1,290 | 1,458 / 2,075 | 1,650 / 2,343 | 1,853 / 2,660 |
| Damage target, T / P | 133 / 133 | 276 / 321 | 457 / 585 | 663 / 855 | 910 / 1,192 | 1,195 / 1,697 | 1,504 / 2,136 | 1,860 / 2,679 |
| Hits to kill an average Common, T / N / P | 1.20 / 1.14 / 1.14 | 1.75 / 1.50 / 1.74 | 2.00 / 1.56 / 1.99 | 1.98 / 1.54 / 1.98 | 1.85 / 1.41 / 1.85 | 1.64 / 1.15 / 1.64 | 1.82 / 1.28 / 1.82 | 2.01 / 1.40 / 2.01 |
| Seconds to kill the Boss (basic hits, no speed affixes), T / N / P | 7.1 / 6.8 / 6.8 | 11.3 / 9.7 / 11.2 | 13.6 / 10.6 / 13.6 | 14.1 / 10.9 / 14.1 | 13.3 / 10.2 / 13.3 | 12.0 / 8.4 / 12.0 | 13.7 / 9.6 / 13.7 | 15.1 / 10.5 / 15.2 |
| Seconds for the Boss to kill the character, T / N / P | 328.9 / 551.4 / 551.4 | 37.5 / 89.2 / 37.5 | 30.0 / 113.6 / 30.0 | 22.8 / 120.1 / 22.7 | 20.0 / 166.7 / 20.0 | 19.6 / 241.5 / 19.6 | 16.6 / 226.5 / 16.6 | 14.5 / 222.1 / 14.5 |
| Seconds to kill the Boss with the build's attack speed and crits, T / P | 5.9 / 3.4 | 9.0 / 4.9 | 10.2 / 5.0 | 9.9 / 4.4 | 8.6 / 3.5 | 6.9 / 2.6 | 7.6 / 2.8 | 8.2 / 2.8 |
| The build's increased attack speed, T / P | 14% / 80% | 18% / 105% | 24% / 130% | 32% / 160% | 41% / 195% | 53% / 230% | 56% / 245% | 60% / 265% |
| The Boss's average hit, T / P | 39 / 39 | 546 / 1,301 | 1,280 / 4,852 | 2,415 / 12,753 | 4,012 / 33,459 | 6,184 / 76,122 | 9,014 / 123,036 | 12,463 / 191,301 |
| The Boss's health, T / P | 1,780 / 1,780 | 3,979 / 4,616 | 6,835 / 8,749 | 10,181 / 13,134 | 13,970 / 18,301 | 18,343 / 26,047 | 23,287 / 33,067 | 28,518 / 41,066 |
| Stalker health x, simulation's fit, T / P (ruled today) | 0.48 / 0.22 (0.49) | 0.75 / 0.37 (0.77) | 0.85 / 0.41 (0.87) | 0.98 / 0.45 (1.01) | 1.02 / 0.44 (1.06) | 1.00 / 0.50 (1.04) | 1.14 / 0.48 (1.19) | 1.11 / 0.45 (1.15) |
| Stalker damage x, simulation's fit, T / P (ruled today) | 0.06 / 0.05 (0.06) | 0.59 / 1.28 (0.58) | 1.02 / 3.33 (1.01) | 1.41 / 6.19 (1.39) | 1.70 / 11.78 (1.68) | 1.89 / 18.07 (1.86) | 2.44 / 26.22 (2.40) | 3.01 / 35.98 (2.97) |

The Boss is the game's: `strength_on_floor("Boss", tier, "Basic", "None", 50, floor=25)`. At tier 1 the geared
reference character is 1.68 times harder to kill and 5% stronger, and nothing about the creatures moves: that is the
upgrade a geared tier 1 player feels.

**Tier 8 against `reference_build.defender(8)`, the design's "How Long a Geared Character Survives":**

| Creature at tier 8 | Its hit, today | Hits, today | Hits, new affixes and today's enemies | Its hit, proposed | Hits, proposed | Seconds, proposed |
| :-- | --: | --: | --: | --: | --: | --: |
| Common Imp | 2,019 | 60 | 915 | 30,986 | 60 | 54.0 |
| Common Hellhound | 4,625 | 26 | 400 | 70,999 | 26 | 28.6 |
| Elite Succubus | 4,185 | 29 | 442 | 64,233 | 29 | 29.0 |
| Elite Brute | 11,359 | 11 | 163 | 174,357 | 11 | 13.2 |
| Legendary Corrupted Sentinel | 4,922 | 25 | 375 | 75,552 | 25 | 25.0 |
| Herald Abyssal Warden | 25,773 | 5 | 72 | 395,623 | 5 | 12.0 |
| Cataclysm Boss Gatekeeper | 65,973 | 2 | 28 | 1,012,690 | 2 | 6.0 |
| An average Common | | 27 | 412 | | 27 | |
| 10 Imps / 20 Imps, seconds | | 5.4 / 2.7 | 82.3 / 41.1 | | 5.4 / 2.7 | |

A gearless level 100 Ravager (6,330 health) is still one-shot by the Gatekeeper, as the owner asked. Builds off the
reference, against the proposed enemies at tier 8: freed slots left empty, Imp 23 hits, Abyssal Warden 2, Gatekeeper
1; offence-leaning, Imp 16, Warden 2, Gatekeeper 1.

## Tier 1 and #2865

Both proposed factors are 1.00 at tier 1, so no tier 1 creature changes. Checked rather than argued:

- **On the analysis branch:** all 16 tests in `tools/tests/test_the_starting_character_survives_a_pack.py` pass
  (development's ring figures 0.05 / 0.11 / 0.13 / 0.10, the first-surge Outpost guard, the Hellfire Aura guard and
  their controls), in the full run below.
- **With #2865 on top:** a temporary local branch from this head with #2865's commits cherry-picked (`0720c158a` ..
  `d3d810805`; only `docs/DECISIONS.md` conflicted, resolved by keeping this branch's copy; `09426c7b8` touches only
  the log and came out empty) ran the pack tests, `test_content_relief_port.py`, `test_creature_strength.py`, #2865's
  two new test files and this branch's tests: **98 passed in 60.41s**. So #2865's 0.10 / 0.12 / 0.07 hold with #2853's
  two branches, the approved affixes and these factors. That also answers #2865's own note that its figures are
  "PROVISIONAL until the gear drop rarity fix (issue #2853) lands; sweep them again then", for the guard's 15 seeds;
  the 60-seed sweep (`analyse_opening.py` section 5) was not run.
- **With the starting Greataxe at 58** (what the approved implicit rule gives at +0; the game's CSV still says 72, read
  as 57.6), on the same temporary branch, `ItemBases.csv` nudged to 72.5 for one run and restored: **16 passed in
  51.39s**. The temporary branch was deleted.

## Tests

The whole suite on the analysis branch at `8fe1fee78`, once:

```
$ python -u -m pytest --junit-xml=<scratchpad>/junit.xml
85 failed, 8387 passed, 9 skipped in 404.27s (0:06:44)
# JUnit: tests 8481, failures 85, errors 0, skipped 9 -> 8387 passed

$ python -m ruff check .
All checks passed!
```

**3 of the 85 already fail at the base `24c1c3810`** (the same 85 node ids run there: 3 failed, 82 passed). They come
from `feat/drop-supply-2853`, whose workbook stage is not applied on a branch (its own decisions entry ran the suite on
a copy with it): `test_datatable_assets_are_current.py::...::test_every_csv_still_hashes_to_what_was_recorded`,
`test_enemy_drop_sheet_matches_the_model.py::test_every_gear_drop_rate_matches_the_model`,
`test_generate_datatables.py::...::test_the_committed_csvs_are_current`.

**The other 82 are moved by this work**, by cause:

| Cause | Tests | Files |
| :-- | --: | :-- |
| **The affix change's own shape** (the proposal's "tests that pin today's shape", and more): tier curve, floors, overlapping bands, stated tops, a roll no longer exactly doubled on a two-handed weapon (the doubling is inside the rounding: a tier 1 bleed at roll 0.5 is 13 two-handed against 7), narrowed slots, scripts and comments quoting old affix figures, the design's "roughly 12 slots" to cap resistance (now 3.6, R2), ailment caps now filled by affixes alone (cripple 418% against 267%, R3), and the proposal script's own "the simulation is restored" check (the simulation now holds the values) | 57 | `sim/tests/test_affixes.py` (24), `test_an_ailment_chance_doubles_on_a_two_handed_weapon.py` (23), `test_analysis_scripts.py` (6), `test_affix_tier_gate.py` (1), `tools/tests/test_leech.py` (1), `test_one_affix_per_group.py` (1), `test_affix_range_proposal.py` (1) |
| **The game's data not regenerated** (the workbook is untouched): the simulation's slots and hybrids no longer match `Affixes.csv` | 2 | `tools/tests/test_affix_sheets_match_the_model.py` |
| **The player's damage and the design text that quotes it**: the reference hit (1,853 -> 2,660), weapon implicits as whole numbers (a Greatsword's 156 -> 155), a two-hander now 1.22x the target where the design says 1.32x (flat damage grew, weapon bases did not), and **minions**: three Imps fall to 0.78x to 0.80x the summoner's basic attack at tiers 6 to 8, under the 1.0 the owner decided on 2026-08-15 (Question 5) | 13 | `sim/tests/test_player_damage.py` (7), `tools/tests/test_two_handed_advantage_matches_the_design.py` (4), `test_minion_stat_blocks.py` (2) |
| **The re-fitted reference character**: a hit lands for 2.14%, not "roughly a tenth"; the ceiling rule (Question 4); armour at its cap at every tier (Question 3); the design's four-layer table | 6 | `sim/tests/test_survivability.py` (3), `tools/tests/test_the_geared_character_figures_match_the_model.py` (3) |
| **The proposed enemy factors**: the game's enemy tables and strength fixture carry no factor; the Corrupted Stalker's ruled tables are fitted to the game's measured Stalker, which does not wear the new affixes (J4) | 4 | `tools/tests/test_enemy_tables_match_the_model.py` (1), `test_enemy_strength_fixture_is_current.py` (1), `sim/tests/test_the_corrupted_stalker_fight.py` (2) |

**What did not move, and guards the balance:** the design's survival table and pack figures
(`test_the_geared_character_figures_match_the_model.py`, its 10 other tests pass), the Brute's slam under the stun
threshold (`test_enemy_abilities.py`), every tier 1 guard above, `test_creature_strength.py`,
`test_content_relief_port.py`, and the 35 tests of `tools/tests/test_enemy_rebalance_2884.py`.

**Guard proof** of the new tests, with `tools/prove_guard.break_and_run` in an exported copy of the head, breaking the
tier 8 damage factor 15.35 -> 7.67:

```
PROVED: 9 failed, 26 passed in 0.40s | restored: 35 passed in 0.22s
   test_the_factors_never_fall_from_one_tier_to_the_next
   test_the_designs_survival_table_holds_against_the_re_fitted_character[Abyssal Warden]
   ... [Brute] [Corrupted Sentinel] [Gatekeeper] [Hellhound] [Imp] [Succubus]
   test_the_boss_kills_the_tiers_character_as_fast_as_today_after_the_first[8]
```

The layout check's three breaks (block on the Helm, Vitality twice on one piece, a prefix in a suffix place) are tests
that break it in memory and expect it to refuse; they pass.

## Questions for the owner, each with a recommended answer

**Q1. Which character should the enemies be fitted to, now that a defensive slot is worth so much more?** (R1, R6)
Under the approved affixes an increased defensive affix grants 53% where it granted 12%, and so do the attribute
affixes, while flat health and armour keep their tops (R1). The reference build, re-fitted by its own rule (half its
slots on staying alive), is 15.35 times harder to kill at tier 8. Fitting enemies to it restores today's game for that
character exactly, and one-shots a build that does not stack defence the same way.
- **(a) Fit to the re-fitted reference character, as proposed (Recommended).** Damage x2.38 at tier 2 to x15.35 at
  tier 8. The reference character plays as today at every tier; a build with the freed slots on offence is one-shot by
  the Gatekeeper and dies to Imps in 16 hits rather than 60.
- (b) Fit to the re-fit with its 13 freed slots left empty: damage x1.84 at tier 2 to x5.73 at tier 8. The adopted
  build then survives 5 Gatekeeper hits rather than 2, 13 Abyssal Warden hits and 160 Imp hits; the offence-leaning
  build survives 2, 4 and 41.
- (c) Lower the defensive increases first (reopen #2883's R1). Under the approved rules (tier 1 starting at 8, seven
  two-value tiers, x2.5 at +10) 53% is the smallest top an increase can have, so this would mean reopening an approved
  rule.
- **Why (a):** the reference build is how the project defines the character enemies are fitted to ("half on staying
  alive and half on killing things"), and (a) is that definition applied to the approved affixes. Tiers 2 and 3 move
  least (x2.38, x3.79), so play will show early whether ordinary builds are one-shot; (b) is the fallback and needs only
  the table changed.

**Q2. Should enemy health also absorb the faster swings and harder crits?** The design's damage target is 2
non-critical basic attacks per average Common at tier 8, and it is restored at every tier. But the reference build's
attack speed affixes went from 15% to 53% each and its crit affixes rose too, so with its own attack speed and crits it
kills the tier 8 Boss in 2.8 s, where today's takes 8.2 s (5.9 -> 3.4 s at tier 1, 9.0 -> 4.9 s at tier 2).
- **(a) No; keep the target in hits (Recommended).** Swinging faster and critting harder are what a player spends
  attack speed and crit slots for; raising every creature's health to cancel them punishes each build that did not take
  them. If fights feel too short in play, the lever is the size of the attack speed and crit affixes.
- (b) Yes: health a further 1.73, 1.84, 2.03, 2.24, 2.47, 2.66, 2.76 and 2.92 times at tiers 1 to 8 on top of the
  proposed table. An average Common would then take 5.9 non-critical hits at tier 8, not the design's 2, and
  `player_damage`'s import check and "The Damage Target" would have to be restated.

**Q3. Block becomes certain and armour reaches its cap for the reference character.** Two block affixes (35 each at
tier 7 on +10 gear) times Constitution's increase give the tier's reference character 123% block at tier 6 and 172% at
tier 8, so from tier 6 every hit is blocked and halved; the tier 8 character's 19,337 armour is at the 75% cap at every
tier, so the tier's armour curve no longer changes what it stops (`test_the_player_stops_least_at_the_last_tier` fails
for that reason). **Recommended: accept both.** `affixes.py` says block "needs no cap" because a block never removes
more than half a hit, armour has its cap already, and the enemy damage table prices both in. R2 (capping resistance costs 3.6 slots, not 12.1) is accepted the same way:
the re-fit spends 4.

**Q4. The enemy mitigation ceiling.** Its rule is "what the reference character stops at tier 8, rounded down to a
whole percent", and the re-fitted character stops 97.86%, so the rule now says 97%. **Recommended: keep 89%.** The
most any archetype can stop, read at the armour cap, is 83.75% (the Abyssal Warden), under both figures, so neither
changes a creature; the ceiling exists so no creature can become nearly immune, and 97% would let a future creature
stop all but 3% of a hit. Restate the rule as "below what the reference character stops, and 89%".
`test_the_enemy_mitigation_ceiling_is_what_the_player_actually_stops` would be restated with it.

**Q5. Minions fall behind the summoner at tiers 6 to 8.** Not an enemy constant, but fitted against the reference
hit, so the audit found it: the owner decided on 2026-08-15 that three Imps out-damage the summoner's own basic attack
at every tier (`tools/tests/test_minion_stat_blocks.py`). The summoner's hit rose 1.42 to 1.44 times at tiers 6 to 8
and the Imps' did not, so three Imps are worth 1.05, 1.39, 1.34, 1.21, 1.02, 0.78, 0.80 and 0.80 times the basic
attack at tiers 1 to 8. **Recommended: re-fit minion damage in its own slice after this one**, raising its growth so
the ratio stays above 1.0 at every tier (roughly x1.29 at tier 6), because minion damage grows with level, not tier,
and it belongs with the minion affixes (increased minion damage keeps its 125%), not with the enemy tables.

## Judgements for the coordinating session (not the owner's)

- **J1. Two per-tier tables rather than new `DAMAGE_AT_COMMON` and `HEALTH_AT_COMMON`.** Re-solving the two constants
  by #108's method gives 9.98 and 0.72, and the result at tier 8 is identical. But they multiply every tier, so tier 1
  would then need its ring scales divided by 15.35 (0.0033 on an Outpost), which the 2026-11-19 method (steps of 0.01)
  cannot express, and a tier 1 health scale of 0.70 to undo the 1.44. A factor that is 1.00 at tier 1 leaves every tier
  1 fit and guard as it is. In the game it is one more per-tier multiplier beside `UCataclysmEnemyScore::DamageScale`.
- **J2. Tier 1 held at 1.00, not at the tier 1 reference character's 1.06 and 1.68.** Tier 1 is where a new character
  starts with no affixes, and its damage is fitted to that character.
- **J3. Two decimals**, as the Stalker tables. Rounding moves no tier 8 survival figure.
- **J4. The Stalker tables wait for a window** after the affix change is in the game: measure
  `Cataclysm.DungeonModifierEffects.WhatACorruptedStalkerIsBuiltWithAtEachTier` again, update `GAME_BUILT_HEALTH`,
  `GAME_ARMOUR` and `GAME_BLOW`, then `fitted_to_the_game`.

## What an implementation would touch

For planning only; none of it is done.
- **Simulation:** `enemy_stats.py` (the two tables, applied in `stats_for`, as on this branch), the affix change's own
  `affixes.py` and `reference_build.py` (as on this branch, written properly rather than through
  `affix_ranges_2883.py`), `fight.py` after the window.
- **Game:** a per-tier health and damage factor where `UCataclysmEnemyStrength` builds a creature, read like
  `UCataclysmEnemyScore::DamageScale`; `tools/generate_enemy_strength_fixture.py` and
  `game/Tests/EnemyStrength/ExpectedFigures.csv` regenerated; the Stalker tables after a window.
- **Design document:** "The Damage Target" (1,860 -> 2,679 per hit), "How Long a Geared Character Survives" (the
  layer table and health), the enemy ceiling sentence, and the Brute's slam share.
- **Order:** the affix change and this change should ship together. Affixes alone make the tier 8 reference character
  15 times harder to kill (the Gatekeeper takes 28 hits); enemies alone make every character 15 times easier to kill.
