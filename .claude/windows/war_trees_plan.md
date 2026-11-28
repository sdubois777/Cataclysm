# War passive trees complete: plan (step 1, no code), 2026-11-27

Branch `feat/war-trees-complete` at `C:\wtc`, cut from origin/development `3c996f2ad`. Nothing is changed yet.

## Said first

- **Some "exists" claims below are read from the code, not run.** Each one marked (verify) gets a generator dry run
  before its row is written.
- **R1 to R29 are already in `docs/DECISIONS.md`** (entry "2026-11-24 — The War trees' rulings pass (Q20)"), and R7's
  retaliation sentence is in `docs/Cataclysm_GDD_v2.md`. Nothing more goes into docs for the rulings themselves.
  **14 ruled sentences are not on their nodes yet**: R3, R4, R5, R8, R9, R10, R11, R12, R13, R16, R19, R20, R27
  (and R14/R15/R17/R18 are on the oaths already). The data slice writes them by node name, with the rows.
- **Three existing rows break a ruled sentence**, found on the way:
  - R14 says "a character with both has 100%" overleech. The stat `leech_overheal_absorb_percent_of_maximum_health` is
    summed, so Open Wounds' 30 plus Eternal Hunger's 100 would give 130. It needs a largest-wins rule (engine).
  - `ailment_immunity` covers damage over time only and refuses a Condition, so no row can say "cannot be stunned while".
  - The `crowd_control` event fires on the character who APPLIES crowd control, not on the one who receives it. Iron
    Mind, Dead Weight and Immovable Object need a new event.

## (a) Recount

Read from `game/Data/PassiveNodes.csv` and `PassiveEffects.csv` at `3c996f2ad`. A node counts when no effect row names
it. None of the 118 is handled by name in non-test C++.

| Class | Nodes | No effect row | Basic | Keystone |
|---|---|---|---|---|
| Berserker | 71 | 45 | 31 | 14 |
| Bulwark | 74 | 41 | 28 | 13 |
| Saboteur | 74 | 32 | 22 | 10 |
| **Total** | 219 | **118** | 81 | 37 |

Not checked: whether War nodes that already have rows cover all of their text.

## (b) The three groups

Totals: **A 29 data only, B 71 need engine work, C 18 need a ruling** (several C nodes also need engine work once
ruled).

### A. Data rows only (29)

Berserker (8): Battle Cry `trunk_007` (decay out of combat, -0.2 a point); War Cry `trunk_010` (own stack on
`melee_attack_use`, condition `class_resource_above` 99, 4 s; the pool is still full when that event fires); Crusher
`twoh_005` (`apply_status` Stagger, 8 a point, the value is the chance); Rapid Fire `dw_009` (as War Cry, attack speed,
cap 4); Flurry of Blows `dw_010` (own stacks 2 s, a MaxPoints 5 row and a MinPoints 5 row at 3 s); Blur `dw_011`;
Bloodlust `dw_016`; Feast `leech_006` (`class_resource_held` step 10; `health` action on `resource_empty`).

Bulwark (16): Battle Scars `trunk_005` (verify the Bulwark decays in combat at all, else the rider grants nothing);
Tempered Steel `trunk_006`; Blood and Sweat `keystone_trunk_002` (cooldown 0.2 s = 5 a second); Last Resort
`keystone_trunk_003`; Aegis Strike `ic_stem_004`; Resolve to Steel `ic_stem_005`; Living Fortress `ic_t3_002`;
Indomitable `ic_final_001`; Unyielding Guard `keystone_ic_t2`; War of Attrition `jg_t3_001`; Thick Hide `tw_stem_002`;
Martyrdom `tw_t2_002`; Serrated Edge `tw_t1_001`; Blood Price `tw_t1_002` (R9); Undying Rage `tw_final_001` (verify
`retaliation_cannot_be_mitigated` takes a condition); Hemorrhage `keystone_tw_t1` (R8, `apply_status_sized` Bleed 2 on
`retaliation_dealt`).

Saboteur (5): Overkill `trunk_002` (`fervour_overflow_stacks`, window stat 2 s, cap 4); Chain Reaction `trap_qty_002`
(`trigger_traps_nearby`, nearest only, cooldown 1 s from 4 points); Bleeding Ground `trap_mid_005` (verify a trap's blow
raises `deployable_hit`); Symbiosis `gadget_sus_004` (`minion_seconds_active`, skill gadgets); Synchronized
`gadget_mid_003` (`gadget_fires_at_trap_enemy`; `trigger_traps_nearby` on `gadget_destroyed`, radius a judgement).

### B. New conditions, actions, scales or stats (each named once)

Shared facts (Berserker and Bulwark):
1. **An attack made at a full pool**, a snapshot taken before Berserking spends: Overwhelming Force `twoh_008`, Seismic
   Slam `twoh_012`, Executioner `twoh_013`, Apex Predator `twoh_ks_003`, Whirlwind `dw_ks_001`, Coup de Grace
   `crit_ks_001` (and Killing Blow, C).
2. **Fervour held as a share of maximum** (scale): Thick Skin `trunk_011` (and Steady Power, Blood for Blood, C).
3. **This hit is / is not a critical strike** (condition on a stat): Vital Strike `leech_003` (R13), Relentless
   `dw_005`, Glass Cannon's penetration (C).
4. **The target's missing health** (scale): Executioner, Death Strike `dw_013`, Coup de Grace.
5. **Crowd control immunity under a condition** (Stun, Stagger, knockback, all): Unbroken `keystone_trunk_001`,
   Juggernaut `twoh_010`, Immovable Object `keystone_jg_t1` (R4).
6. **Crowd control received**, an event carrying the applier, plus "you are crowd controlled" (condition): Iron Mind
   `jg_t1_002`, Dead Weight `jg_t1_003` (R27), Immovable Object.
7. **A blow sized by your own Maximum HP or Armor** (next attack, nearby, the applier): Mountain's Wrath
   `keystone_jg_t2` (R5), Seismic Slam (Bulwark) `jg_t2_003`, Dead Weight, Immovable Object.
8. **Enemies struck within N s / unique attackers within N s** (scales): Dancing Blades `dw_007` (and Outnumbered, C).
9. **Bleeding enemies in reach** (scale) and **target is bleeding** (condition): Open Wounds `tw_t1_003` (R10).
10. **Holding a shield**, and its negation (condition): Stalwart `ic_stem_003`, Reinforced Guard `ic_t2_001`, Flesh
    Fortress `keystone_tw_t2`.
11. **Increased Maximum HP** (scale): Iron Will `leech_002`.
12. **Evasion** (scale): Callous Setup `trap_mid_007`, Calculated Risk `gadget_mid_005`.
13. **A leech event with its amount**: Sanguine `leech_005`, Vampiric Fury `leech_ks_001`.

Actions and stacks:
14. **A mark: the enemy takes N% increased damage for S s, applied by an action on an event** (#964; generalises
    Deathmark's `MarkWithDeathmark`): Open Wound `dw_006`, Marked Territory `trap_ql_004`.
15. **Own stacks removed by another event**: Consecutive Blocks `ic_t2_002`, Killing Rhythm `crit_005`, Weaving
    `dw_003`, Unbreakable Will `ic_t1_003`.
16. **The last skill used** (a different skill grants, the same one removes): Weaving (R12, with a Fervour cost per
    stack in Frenzy).
17. **Every Nth attack deals more**: Flurry `dw_002` (R11).
18. **A placed stack lowering the enemy's damage reduction**: Rend `dw_004`.
19. **`enemy_armor_removed` on `critical_strike`**: Shatter `twoh_009`.
20. **An own-stack cap that rises under a condition, and a short two-hit window**: Feeding Frenzy `dw_008`, Counter
    Stance `ic_t1_001` (ScaleOffset is refused on `own_stacks`).
21. **Strikes from Frenzy's drain** (one per 10 drained, `strike_strongest_near` on `fervour_spent`) and an event for
    each: Frenetic `dw_012`, Knife Dance `dw_018`.
22. **A delayed echo of a strike at a share, earning no Fervour**: Phantom Blades `dw_ks_003` (`hits_strike_again` is
    immediate and full).
23. **Knockback on a hit**: Whirlwind (with `melee_arc_full_circle`, which exists).
24. **A blast from an enemy killed by area damage / inside the retaliation radius**: Wrecking Ball `twoh_014`,
    Spreading Thorns `tw_t3_002`.
25. **A line-shaped shockwave**: Seismic Slam (Berserker).
26. **Next retaliation deals more**: Spiteful (C).

Keystone rules:
27. **Berserking changes**: on every melee hit below 30% health (Bloodied `trunk_012`); spends nothing (Berserker's
    Rage, C).
28. **A defence set to nought, converting what it would have been**: Flesh Fortress (R16, armour to health),
    Calculated Risk (evasion to gadget damage).
29. **Maximum HP measured without its own contribution**: Colossus `jg_stem_004` (R3).
30. **Largest-wins overleech ceiling**: Open Wounds `trunk_006`.
31. **Critical strike chance past 100% to multiplier**: Hair Trigger `crit_ks_002`.
32. **Block rules**: a block chance floor (Eye of the Storm `keystone_ic_t3`), a blocked lethal hit reprieve (The
    Last Bastion `keystone_ic_final`), blocked damage added to the next attack (Unbreakable Will).
33. **Time in combat falls slowly after leaving combat**: Grinding Advance `jg_t3_002`.
34. **A pulse sized by damage taken in the last 5 s**: Wrathful Presence `keystone_tw_t3`.

Saboteur machines:
35. **Per-gadget empowerment paid in Fervour at placement** (damage and health; attack speed over 10 s), and its cost
    reduction: Overcharge `gadget_stem_004`, Overclock `gadget_dep_002`, Rapid Deployment `gadget_dep_005`.
36. **A gadget's own health** (conditions): Last Stand Protocol `gadget_dep_004`, Persistent Systems `gadget_sus_002`.
37. **A blast sized by the destroyed gadget's maximum health**: Volatile Construction `gadget_dep_003`.
38. **Every gadget moved to the newest**: Forward Operating Base `keystone_gadget_dep`.
39. **Gadget charge and discharge**: Power Surge `gadget_sus_003`.
40. **A gadget ignores a hit, by chance, with an event**: Redundant Systems `gadget_sus_005`.
41. **Gadget range asked with a scale, the player's stillness for gadget stats, attacks through terrain**: Siege Mode
    `keystone_gadget_sus` (R19; today `seconds_stationary` reads the gadget's stillness), Combat Engineer
    `gadget_deep_001` (`minion_range` takes no scale today).
42. **Gadgets fire at what you hit**: Coordinated Strike `gadget_deep_002`.
43. **The next gadget's cooldown reduced**: Scrap and Rebuild `gadget_deep_003` (its Fervour half is data).
44. **A one-trap limit and a "traps your maximum Fervour could hold" scale**: Precision Placement `keystone_trap_ql`.
45. **The last trap deals the sum of the last 10 s**: The Last Surprise `keystone_trap_deep_001`.
46. **Traps placed without room: reserve nothing, drain, all trigger at 0**: Calculated Chaos `keystone_trap_deep_002`.
47. **Fervour spent in the last 5 s** (scale): Hot Deploy `gadget_dep_001`.
48. **Stun from a trap's first trigger**: Concealed `trap_mid_003` (R20).
49. **Distinct trap kinds active** (scale): Hair Wire Expertise `trap_mid_004`.
50. **Gadget duration extended per other active gadget**: Efficient Design `gadget_stem_003` (verify; may be data).

### C. Rulings needed (18), each with a recommended reading

| Node | Problem | Recommended reading |
|---|---|---|
| Momentum `Berserker_twoh_002` | "gained twice as fast" in Wrath: the hit count must step by 1 | "While in Wrath, the damage per hit is doubled." (data) |
| Killing Blow `Berserker_twoh_015` | "all enemies below 25%" has no radius | all enemies within 8 m; needs item 1 and an execute-nearby action |
| Colossus `Berserker_twoh_ks_001` | triggers Earthquake: the player may not hold it | only when Earthquake is in hand (`trigger_held_skill`); potency by Fervour share is new |
| Berserker's Rage `Berserker_twoh_ks_002` | Berserking spending nothing makes every full-pool melee attack a Berserking attack at 10% a Fervour | Berserking spends half the pool instead of all of it |
| Unrelenting `Berserker_dw_015` | "20% per point up to 60%" on a 6 point node; a row cannot cap | "refunds 10% of your maximum Fervour per point" (60% at 6), data |
| Carnage `Berserker_dw_017` | Frenzy has no duration (it ends at an empty pool) | "restores Fervour equal to 1 second of Frenzy's drain per point, up to 6 seconds' worth per Frenzy" |
| Unstoppable Force `Berserker_dw_ks_002` | Frenetic has no timer | a kill in Frenzy triggers one Frenetic strike; entering Frenzy strikes the nearest enemy three times |
| Glass Cannon `Berserker_crit_003` | Fervour above the maximum is never held (overflow is counted, not kept) | the rider reads `fervour_overflow_stacks`; penetration needs item 3 |
| Shield Slam `Bulwark_basic_ic_t2_003` | "block streak" is undefined | "after you block, your next attack deals bonus damage equal to 5% of your Armor" (item 7) |
| Fortified Mind `Bulwark_basic_ic_t3_001` | "200 flat damage" on the old scale (R28) | "deals damage equal to 3% of your Maximum HP back", as R4/R27 |
| The Inevitable `Bulwark_keystone_jg_t3` | "nearby" has no radius | within 4 m (R7's radius); item 7 |
| Outnumbered `Bulwark_basic_tw_stem_004` | "all nearby" has no radius | within 4 m; item 8 |
| Spiteful `Bulwark_basic_tw_stem_003` | "reach maximum from taking hits" cannot be told apart | "when your Fervour becomes full, your next retaliation deals 50% more" (item 26) |
| Blood for Blood `Bulwark_keystone_tw_final` | +1% per 5 Fervour cannot reach 100% at a full pool of 100 | "+1% for every 2% of maximum Fervour held" (item 2) |
| Scattershot `Saboteur_basic_trap_qty_004` | "spend 20 Fervour" against traps that reserve | the extra trap reserves as normal and is not placed without room (data if `place_last_trap_again` takes a chance) |
| Reinforced Housing `Saboteur_basic_trap_ql_002` | a Fervour cost per trigger against reserving traps | "each trigger after the first spends 15 Fervour, reduced by 1 per point; with too little, the trap is used up" (engine) |
| Steady Power `Saboteur_basic_gadget_sus_001` | "gadgets drain 1 Fervour per second": each, or all together | each active gadget drains 1 a second (`fervour_drain_per_second` by `gadgets_active`); damage reads item 2 |
| Jury Rigged `Saboteur_basic_gadget_mid_007` | "until repaired to full HP": nothing repairs a gadget | the bonus lasts 5 seconds, then the gadget is destroyed |

## Issue #932 (the class stat line), a ruling

No War class has a line in `ClassStats.csv`; the three Death lines exist, chosen only by a console variable. The
design already says the first point in a damage type's tree chooses that type's class
(`docs/Cataclysm_GDD_v2.md`, Multiclassing). Recommended:
1. Before any point is spent: the first class the design lists for the damage type chosen at creation (War: Bulwark).
2. After points: the chosen class with the most points; a tie goes to the class chosen first.
3. When it changes: maxima are recomputed and current health, mana and Fervour are held to the new maximum, not
   refilled (the rule gear swaps already follow).
4. Three War stat lines written as judgements under the delegation of numbers, as the Death lines were (slice A).

## (c) Slices, for one window

All on this branch, one Unreal window at the end, one C++ guard proof per engine slice (each a combined call).

| Slice | What | Proof breaks (one each) |
|---|---|---|
| E1 Facts | items 1-13 (conditions, scales, the crowd-control event, the shield condition) | the full-pool snapshot; hit is critical; shield condition |
| E2 Actions and stacks | items 14-26 (#964's mark, stacks, Nth attack, drain strikes, echo, blasts) | the mark's damage taken; stack removal; Nth attack |
| E3 Keystone rules | items 27-34 | conversion to nought; self-excluded health; overleech largest |
| E4 Machines | items 35-50 | per-gadget empowerment; one-trap limit; gadget health condition |
| D Data | the 29 A rows, the rows over E1-E4, the 14 ruled sentences, the C sentences once ruled, #932's lines | Python proofs only |

Order: D's 29 A rows can be written first (no engine); E1 before E2 and E3 (they read its conditions); E4 is apart
and could run in a second session in its own worktree.

**Size warning.** 71 engine nodes is about four of the earlier War slices. One window for all of it is possible; if a
proof fails late, it is a long window to repeat. A fallback is two windows: E1+E2+E3 with the Berserker and Bulwark
rows, then E4 with the Saboteur rows.

## Rulings received, 2026-11-27 (coordinating session's message)

ACCEPTED, with:
- Owner: Berserker's Rage: Berserking spends half the pool instead of all of it.
- Owner: Unstoppable Force: a kill in Frenzy triggers one Frenetic strike; entering Frenzy strikes the nearest enemy three times.
- Owner, verbatim, Colossus (Berserker_twoh_ks_001): "I think I wanted that to act like a passive trigger that uses the earthquake skill regardless of if they have the skill". Casts Earthquake as a passive trigger whether or not held; no button, no slot. The owner's own exception to "a class tree never adds a skill". Potency by Fervour share as planned.
- Coordinator: every other C node as recommended, EXCEPT Jury Rigged: the bonus lasts while the gadget is below full health (never repaired, so the rest of its life). Do NOT add "then destroyed".
- Coordinator: #932 four points as written (Bulwark first for War; most points, tie to first chosen; maxima recomputed, pools held; three War lines as judgements).
- Coordinator: largest-wins overleech, conditioned stun immunity, crowd-control-RECEIVED event approved as engine work, each in its slice.
- Windows: TWO. W1 = E1+E2+E3 + Berserker/Bulwark rows + 29 A rows + #932. W2 = E4 + Saboteur rows. One session, in order. Register each window before it runs.
- Go: D's 29 A rows first, then E1. Push each commit as it stands.
