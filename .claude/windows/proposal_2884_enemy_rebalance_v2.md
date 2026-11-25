# Enemy rebalance after the affix rescale (#2884): version 2, fitted to the owner's ruling

**A proposal, 2026-11-24, measured in the simulation only. Nothing in the game, the CSVs or the design workbook is
changed.** It replaces version 1 (`proposal_2884_enemy_rebalance.md`, beside this file), whose tables were fitted to a
reference character with its freed slots filled with health. Branch `analysis/enemy-rebalance-2884`, not for merging:
origin/development `5cdd6bdc4`, the two #2853 branches and the approved affix proposal, cherry-picked without a
conflict, then this work (`8fe1fee78`, `406d24023`, `49d120bc7` and this document). Script:
`sim/analyse_enemy_rebalance_2884.py`. Tests: `tools/tests/test_enemy_rebalance_2884.py`.

**The owner's rulings, 2026-11-24, on issue #2884 (comment 7149795643), verbatim:**

| # | Question | Answer |
| :-- | :-- | :-- |
| 1 | Which character are enemies fitted to? | "A middle build (Recommended)": the re-fit with its 13 freed suffix slots left empty |
| 2 to 5 | Health absorbing swings and crits; certain block and capped armour; the mitigation ceiling; minions | "Accept all four (Recommended)": the target stays in hits; both accepted; the ceiling kept at 89% with its rule restated; minion damage re-fitted in its own later slice |

## Said first

1. **Enemy damage rises x1.84 at tier 2 to x5.73 at tier 8; health x1.16 to x1.44; tier 1 is unchanged.** At tiers 2 to
   8 the reference character takes exactly today's hits from every creature and deals today's hits to an average
   Common, and the design's tier 8 survival table returns exactly (Imp 60, Gatekeeper 2).
2. **A build that fills the 13 slots with health is now about 2.7 times as hard to kill as the reference** (the tier 8
   Gatekeeper needs 5 hits, an Imp 160); **one that leans on offence takes about two thirds of the reference's hits**
   (Gatekeeper 2, Imp 41). Neither is one-shot at tier 8. At tier 1 nothing moves for either.
3. **Kill times in seconds with the build's own attack speed and crits are still shorter than today, as ruled** (2):
   the reference kills the tier 8 Boss in 2.8 s where today's takes 8.2 s; the offence build in 1.6 s.
4. **For the chosen build, block is not certain and armour is not at its cap at tier 8**: block 98% (80.6% at tier 6,
   89.1% at tier 7), armour 12,550 stopping 66.2%. Version 1's "certain from tier 6, capped at every tier" described the
   health-filled build. The owner accepted both either way.
5. **The Corrupted Stalker's ruled tables still need a window**; the simulation's estimate is below.
6. **The mitigation ceiling's rule is not yet restated on this branch.** The chosen character stops 97.05%, so its
   stated rule gives 97% and `test_the_enemy_mitigation_ceiling_is_what_the_player_actually_stops` fails; the owner
   ruled 89% kept and the rule restated, which is implementation work, not done here.
7. **#2865's ring scales were not re-run for version 2.** Nothing they read moved: tier 1's factors are still 1.00, and
   the pack and ring guards fight the starting character, not the reference build. Version 1's run (#2865 on top: 98
   passed; the Greataxe at 58: 16 passed) still stands for that reason, not because it was repeated.

## Summary

| Constant | Today | Proposed (version 2) | Why |
| :-- | :-- | :-- | :-- |
| **Enemy health factor by tier, 1 to 8** (new: `enemy_stats.HEALTH_SCALE_BY_TIER_2884`, applied in `stats_for`) | none (1.00) | **1.00, 1.16, 1.28, 1.29, 1.31, 1.42, 1.42, 1.44** (unchanged from version 1) | The reference hit rose by these at tiers 2 to 8; the slots left empty are suffixes no hit reads. An average Common takes today's hits at every tier; the tier 8 target of 2 hits holds (gap 0.99) |
| **Enemy damage factor by tier, 1 to 8** (new: `DAMAGE_SCALE_BY_TIER_2884`) | none (1.00) | **1.00, 1.84, 2.52, 2.95, 3.65, 4.75, 5.12, 5.73** (version 1: 2.38 to 15.35) | The chosen reference character is that many times harder to kill. Every creature takes today's hits to kill it at tiers 2 to 8 |
| `HEALTH_AT_COMMON` 0.50, `HEALTH_PER_STEP` 1.85, `DAMAGE_AT_COMMON` 0.65, `DAMAGE_PER_STEP` 1.40 | | **unchanged** | At tier 8 the tables equal `HEALTH_AT_COMMON` 0.72 and `DAMAGE_AT_COMMON` 3.72 |
| Tier 1 ring scales (development and #2865), the tier 2 to 8 line, Hellfire Aura's 0.09 | | **unchanged** | Fitted to the starting character, which carries no affixes |
| Reference build | block 4, damage reduction 4, penetration 4, all resistances 12, crit chance, multiplier and attack speed 4 each | **block 2, damage reduction 1, penetration 1, all resistances 4, crit chance, multiplier and attack speed 5 each, 13 suffix places EMPTY**; prefixes unchanged | The owner's ruling 1. 36 prefixes and 23 suffixes spent |
| `ENEMY_MITIGATION_CEILING` | 89% | **89%**, rule restated (ruling 4; not yet done) | The chosen character stops 97.05% |
| Corrupted Stalker tables | 0.49 to 1.15 / 0.06 to 2.97 | **re-fit in a window**; the simulation estimates 0.22 to 0.50 / 0.05 to 13.43 | Ruled to wait for a window |
| Minion damage | | **its own later slice** (ruling 5) | Unchanged by version 2: it reads the reference hit, not its defence |

## What "empty" means in the build file

`sim/cataclysm_sim/reference_build.py` holds the build piece by piece (`LAYOUT`). The 13 places version 1 gave to
Increased Vitality (8) and Increased Constitution (5) now hold `EMPTY` (`None`): the place has no affix and counts for
nothing in `gear()`. `PREFIX_SPEND`, `SUFFIX_SPEND` and `RESISTANCE_AFFIXES` are counted from `LAYOUT` and skip it, so the
build spends 36 prefixes and 23 suffixes (`EMPTY_SUFFIX_SLOTS` is 13). The 3 offensive slots penetration freed still go
on crit chance, multiplier and attack speed (5 each), as in version 1; they read no defensive figure, so the damage
factors are the same as for the sensitivity row the owner chose, which left them empty too (16 places).

**A piece with an empty place is a lower rarity.** Rarity is the label for what fills the slots
(`affixes.RARITY_COMPOSITION`): three regular affixes is Superb, two is Quality. So the character wears 8 Masterful, 7
Superb (Helm, Cuirass, Pauldrons, Greaves, Sabatons, Girdle, one Loop) and 3 Quality pieces (three Loops)
(`rarity_of_each_piece`). Nothing in the enemy fit reads rarity. The Power Score would, and is not measured here.
`HEALTH_FILLED_SUFFIXES_2884` keeps version 1's spend for the comparison below.

| Piece | Prefixes | Suffixes |
| :-- | :-- | :-- |
| Helm, Cuirass | flat health, increased health | all resistances, EMPTY |
| Pauldrons | flat health, flat armour | block, EMPTY |
| Gauntlets | flat damage, increased damage | increased crit chance, increased attack speed |
| Greaves | flat health, increased armour | damage reduction, EMPTY |
| Sabatons | flat health, flat armour | all resistances, EMPTY |
| Girdle | flat health, increased armour | block, EMPTY |
| Loop 1 | flat health, increased health | all resistances, EMPTY |
| Loop 2 | flat health, increased health | EMPTY, EMPTY |
| Loop 3, 4 | increased health, flat armour | EMPTY, EMPTY |
| Loop 5, 6 | flat armour, increased armour | crit chance and multiplier; crit chance and attack speed |
| Loop 7, 8 | flat damage, increased damage | multiplier and attack speed; crit chance and multiplier |
| Torc | flat damage, increased damage | crit multiplier, attack speed |
| Idol | flat damage, increased damage | crit chance, crit multiplier |
| Greatsword | flat damage, increased damage | penetration, attack speed |

## The fit

The method is version 1's, unchanged: health by the reference hit's rise per tier (the damage target, in non-critical
hits), damage by the reference character's effective health's rise per tier (#108's hits to kill it), tier 1 held.

```
python -u sim/analyse_enemy_rebalance_2884.py --sim-root <copy of 24c1c3810>/sim --json before.json
python -u sim/analyse_enemy_rebalance_2884.py --tables today --json after_old.json
python -u sim/analyse_enemy_rebalance_2884.py --fit before.json after_old.json

   tier   hit ratio (health factor)   effective health ratio (damage factor)
      1                     1.0553                                   1.4914
      2                     1.1626                                   1.8376
      3                     1.2815                                   2.5180
      4                     1.2911                                   2.9499
      5                     1.3118                                   3.6474
      6                     1.4231                                   4.7538
      7                     1.4205                                   5.1241
      8                     1.4358                                   5.7290
   Tier 1 is held at 1.00 in both: its damage is fitted to the starting character, which carries no affixes.
   health: (1.0, 1.16, 1.28, 1.29, 1.31, 1.42, 1.42, 1.44)
   damage: (1.0, 1.84, 2.52, 2.95, 3.65, 4.75, 5.12, 5.73)
   tier 8 check through hits_to_kill: average Common 27 -> 154 hits with today's constants; Gatekeeper 2 -> 11
```

**The chosen reference character, per tier** (`python -u sim/analyse_enemy_rebalance_2884.py`; today's in brackets):

| Tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| Health | 1,982 (1,283) | 3,364 (2,064) | 5,134 (3,067) | 7,353 (4,259) | 10,212 (5,766) | 14,353 (7,791) | 17,165 (9,401) | 20,310 (11,023) |
| Block | 12.6 (7.6) | 22.0 (9.3) | 32.2 (11.6) | 45.6 (14.5) | 62.5 (18.4) | 80.6 (23.4) | 89.1 (25.6) | 98.0 (28.0) |
| A hit lands for | 15.58% (15.05%) | 13.39% (15.10%) | 7.97% (11.99%) | 6.80% (11.62%) | 5.24% (10.79%) | 3.73% (9.64%) | 3.36% (9.43%) | 2.95% (9.17%) |
| Effective health | 12,721 (8,529) | 25,126 (13,673) | 64,408 (25,579) | 108,141 (36,659) | 194,950 (53,449) | 384,360 (80,854) | 510,975 (99,719) | 688,936 (120,255) |
| Its hit (target) | 233 (133) | 368 (321) | 587 (585) | 863 (855) | 1,290 (1,192) | 2,075 (1,697) | 2,343 (2,136) | 2,660 (2,679) |
| Hits to kill an average Common | 1.14 (1.20) | 1.74 (1.75) | 1.99 (2.00) | 1.98 (1.98) | 1.85 (1.85) | 1.64 (1.64) | 1.82 (1.82) | 2.01 (2.01) |

Tier 1's resistance is 36 against today's 52: four all-resistance affixes cap tier 8 and not tier 1 (version 1 also).

## Survival and kill times for the three builds

Each creature at the rarity it is met at, **built as the game builds it** (`strength_on_floor`: tier relief, minimum,
the tier's damage scale and these factors) on the design table's floor, the last of a 50-floor Cataclysm dungeon, on
the Pillar (whose scale applies at tier 1). Hits to kill the tier's character (`fight.reference_player`), ten Imps in
seconds, and the duel with the game's Boss on floor 25 of a 50-floor Basic dungeon: seconds to kill it with basic
hits, with the build's own attack speed and crits, and seconds for it to kill the character. The three builds: the
reference (13 places empty); "health", version 1's (Vitality 8, Constitution 5); "offence" (block on one piece, crit
chance 8, multiplier 8, attack speed 7, the most the offensive pieces hold, 6 places empty). The last two are counted
as totals, not placed piece by piece.

| Build, tier | Imp | Hellhound | Succubus | Brute | Sentinel | Warden | Gatekeeper | 10 Imps, s | Boss, s | Boss with build, s | Boss kills it, s |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: | --: | --: | --: |
| Today's build, T1 | 571 | 249 | 268 | 99 | 222 | 42 | 14 | 51.3 | 7.1 | 5.9 | 328.9 |
| **Reference, T1** | 851 | 372 | 399 | 147 | 330 | 62 | 21 | 76.5 | 6.8 | 3.4 | 490.5 |
| Health, T1 | 956 | 418 | 449 | 166 | 371 | 69 | 24 | 86.0 | 6.8 | 3.4 | 551.4 |
| Offence, T1 | 823 | 360 | 386 | 143 | 320 | 60 | 20 | 74.0 | 6.8 | 2.8 | 474.5 |
| Today's build, T2 | 154 | 68 | 73 | 27 | 61 | 12 | 5 | 13.8 | 11.3 | 9.0 | 37.5 |
| **Reference, T2** | 154 | 68 | 73 | 27 | 61 | 12 | 5 | 13.8 | 11.2 | 4.9 | 37.5 |
| Health, T2 | 199 | 87 | 95 | 35 | 79 | 15 | 6 | 17.9 | 11.2 | 4.9 | 48.5 |
| Offence, T2 | 145 | 64 | 69 | 26 | 58 | 11 | 4 | 13.0 | 11.2 | 3.8 | 35.3 |
| Today's build, T3 | 133 | 58 | 64 | 24 | 53 | 11 | 4 | 11.9 | 13.6 | 10.2 | 30.0 |
| **Reference, T3** | 133 | 58 | 63 | 24 | 53 | 11 | 4 | 11.9 | 13.6 | 5.0 | 29.9 |
| Health, T3 | 199 | 87 | 95 | 35 | 80 | 16 | 6 | 17.9 | 13.6 | 5.0 | 45.1 |
| Offence, T3 | 121 | 53 | 58 | 22 | 49 | 10 | 4 | 10.8 | 13.6 | 3.7 | 27.3 |
| Today's build, T4 | 106 | 46 | 51 | 19 | 43 | 9 | 4 | 9.5 | 14.1 | 9.9 | 22.8 |
| **Reference, T4** | 106 | 46 | 51 | 19 | 43 | 9 | 4 | 9.5 | 14.1 | 4.4 | 22.8 |
| Health, T4 | 189 | 83 | 91 | 34 | 77 | 15 | 6 | 16.9 | 14.1 | 4.4 | 40.7 |
| Offence, T4 | 92 | 41 | 44 | 17 | 38 | 8 | 3 | 8.3 | 14.1 | 3.1 | 19.8 |
| Today's build, T5 | 95 | 42 | 46 | 17 | 39 | 8 | 3 | 8.5 | 13.3 | 8.6 | 20.0 |
| **Reference, T5** | 95 | 42 | 46 | 17 | 39 | 8 | 3 | 8.5 | 13.3 | 3.5 | 20.0 |
| Health, T5 | 217 | 95 | 104 | 39 | 88 | 17 | 7 | 19.5 | 13.3 | 3.5 | 45.7 |
| Offence, T5 | 78 | 34 | 37 | 14 | 32 | 6 | 3 | 6.9 | 13.3 | 2.2 | 16.3 |
| Today's build, T6 | 95 | 42 | 46 | 17 | 39 | 8 | 3 | 8.5 | 12.0 | 6.9 | 19.6 |
| **Reference, T6** | 95 | 42 | 46 | 17 | 39 | 8 | 3 | 8.5 | 12.0 | 2.6 | 19.6 |
| Health, T6 | 246 | 108 | 119 | 44 | 101 | 20 | 8 | 22.1 | 12.0 | 2.6 | 50.8 |
| Offence, T6 | 71 | 31 | 35 | 13 | 29 | 6 | 3 | 6.4 | 12.0 | 1.6 | 14.7 |
| Today's build, T7 | 82 | 36 | 40 | 15 | 34 | 7 | 3 | 7.4 | 13.7 | 7.6 | 16.6 |
| **Reference, T7** | 82 | 36 | 40 | 15 | 34 | 7 | 3 | 7.4 | 13.7 | 2.8 | 16.6 |
| Health, T7 | 219 | 96 | 105 | 39 | 89 | 17 | 7 | 19.6 | 13.7 | 2.8 | 44.2 |
| Offence, T7 | 59 | 26 | 29 | 11 | 24 | 5 | 2 | 5.3 | 13.7 | 1.6 | 11.8 |
| Today's build, T8 | 73 | 32 | 35 | 13 | 30 | 6 | 3 | 6.5 | 15.1 | 8.2 | 14.5 |
| **Reference, T8** | 73 | 32 | 35 | 13 | 30 | 6 | 3 | 6.5 | 15.2 | 2.8 | 14.5 |
| Health, T8 | 194 | 85 | 94 | 35 | 79 | 16 | 6 | 17.4 | 15.2 | 2.8 | 38.8 |
| Offence, T8 | 49 | 22 | 24 | 9 | 20 | 4 | 2 | 4.4 | 15.2 | 1.6 | 9.8 |

**The design's own tier 8 table** ("How Long a Geared Character Survives": `stats_on_floor`, the design's creature
without the tier relief, against `reference_build.defender(8)`):

| Build | Imp | Hellhound | Succubus | Brute | Sentinel | Warden | Gatekeeper | 10 / 20 Imps, s | A hit lands for |
| :-- | --: | --: | --: | --: | --: | --: | --: | :-- | --: |
| Today | 60 | 26 | 29 | 11 | 25 | 5 | 2 | 5.4 / 2.7 | 9.17% |
| **Reference (13 empty)** | 60 | 26 | 29 | 11 | 25 | 5 | 2 | 5.4 / 2.7 | 2.95% |
| Health (version 1) | 160 | 70 | 77 | 29 | 66 | 13 | 5 | 14.4 / 7.2 | 2.14% |
| Offence | 41 | 18 | 20 | 8 | 17 | 4 | 2 | 3.6 / 1.8 | 4.36% |

The Gatekeeper's tier 8 hit is 378,027 before mitigation; a gearless level 100 Ravager (6,330 health) is still one-shot.

**The Corrupted Stalker, the simulation's own fit** (`fight.fight`, an estimate: the ruled tables are fitted to the
game's measured Stalker, which needs a window once the affixes ship):

| Tier | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| Health x, simulation (ruled today) | 0.22 (0.49) | 0.37 (0.77) | 0.41 (0.87) | 0.45 (1.01) | 0.44 (1.06) | 0.50 (1.04) | 0.48 (1.19) | 0.45 (1.15) |
| Damage x, simulation (ruled today) | 0.05 (0.06) | 0.99 (0.58) | 2.21 (1.01) | 3.46 (1.39) | 5.16 (1.68) | 6.97 (1.86) | 9.84 (2.40) | 13.43 (2.97) |

The health column did not move from version 1 (it reads the player's damage and the Stalker's own pieces, not the
player's defence); the damage column fell from 35.98 at tier 8 because the Boss it is matched to now hits 5.73 times,
not 15.35 times, as hard.

## Tests

The whole suite at `49d120bc7`, once:

```
$ python -u -m pytest --junit-xml=<scratchpad>/junit_v2.xml
83 failed, 8391 passed, 9 skipped in 481.82s (0:08:01)
# JUnit: tests 8483, failures 83, errors 0, skipped 9 -> 8391 passed

$ python -m ruff check .
All checks passed!
```

**The 83 are version 1's 85 less two**, and none is new. Version 1 measured that the same 85 node ids at the base
`24c1c3810` give 3 failures, from `feat/drop-supply-2853`'s unapplied workbook stage (`test_datatable_assets_are_current`,
`test_enemy_drop_sheet_matches_the_model`, `test_generate_datatables`'s committed-CSV check). **80 are moved by this
work:**

| Cause | Tests |
| :-- | --: |
| The affix change's own shape (tier curve, floors, overlap, stated tops, two-handed doubling inside the rounding, narrowed slots, text quoting old affix figures, the design's "roughly 12 slots" to cap resistance, ailment caps filled by affixes, the proposal script's restore check) | 57 |
| The game's data not regenerated (`test_affix_sheets_match_the_model.py`) | 2 |
| The player's damage and the design text quoting it, and the two minion tests (ruling 5) | 13 |
| The re-fitted reference character: a hit lands for 2.95%, not "roughly a tenth"; the ceiling rule and the design's ceiling sentence (ruling 4, not yet restated); the design's four-layer table | 4 |
| The proposed enemy factors: the game's enemy tables and strength fixture carry no factor; the Stalker's ruled tables (window) | 4 |

**The two that version 1 moved and version 2 does not:** `sim/tests/test_survivability.py::test_the_player_stops_least_at_the_last_tier`
(the chosen character's 12,550 armour stops 66.2% at tier 8, under the cap, so the tier matters again) and
`tools/tests/test_the_geared_character_figures_match_the_model.py::test_the_four_layers_multiply_to_what_a_hit_loses`
(block 98% rather than 172%, so the layers multiply to the model's figure again).

**Still passing:** the design's survival table and pack figures, the Brute's slam, every tier 1 opening and ring guard
(16), `test_creature_strength.py`, `test_content_relief_port.py`, and the 37 tests of `test_enemy_rebalance_2884.py`
(two new: the 13 empty places are exactly version 1's Vitality and Constitution, and the rarity labels 8 / 7 / 3).

**Guard proof** at `49d120bc7`, `tools/prove_guard.break_and_run` in an exported copy, tier 8's damage factor 5.73 ->
2.87:

```
PROVED: 9 failed, 28 passed in 0.52s | restored: 37 passed in 0.23s
   test_the_factors_never_fall_from_one_tier_to_the_next
   test_the_designs_survival_table_holds_against_the_re_fitted_character[Abyssal Warden] [Brute]
     [Corrupted Sentinel] [Gatekeeper] [Hellhound] [Imp] [Succubus]
   test_the_boss_kills_the_tiers_character_as_fast_as_today_after_the_first[8]
```

## What the remaining rulings leave to do

- **Ruling 4:** restate the ceiling's rule ("below what the reference character stops, and 89%") in `enemy_stats.py`,
  the design's sentence and `test_the_enemy_mitigation_ceiling_is_what_the_player_actually_stops`, with the change.
- **Ruling 5:** minion damage, its own slice: three Imps are 1.05, 1.39, 1.34, 1.21, 1.02, 0.78, 0.80 and 0.80 times
  the summoner's basic attack at tiers 1 to 8 (version 2 does not move this; it reads the hit, not the defence).
- **The Stalker:** a window once the affixes are in the game, then `fitted_to_the_game`.
- **Implementation:** the two tables in `stats_for` and in the game where `UCataclysmEnemyStrength` builds a creature;
  the enemy strength fixture regenerated; "The Damage Target" (1,860 -> 2,679 per hit) and "How Long a Geared Character
  Survives" (health 20,310, armour 12,550, block 98%, damage reduction 43, a hit landing for 2.95%) restated; the
  affix change and this change shipped together.
