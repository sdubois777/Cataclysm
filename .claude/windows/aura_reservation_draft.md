# Auras reserve mana instead of draining it: a draft for a ruling

Draft for the coordinating session, 2026-11-27. Nothing is built.

- **Worktree:** `C:\aur`, branch `feat/aura-reservation`, from development `efd26843a`.
- **The owner, verbatim:** "I think we may need to adjust the mana cost of auras across the board. I have a lvl 100 character with 500+ mana and I can't even keep my auras going. That feels a bit extreme. It should be difficult to maintain them at low levels, but get easier. Players will likely be running more than one at a time, and right now that's impossible."
- **On running more than one aura:** "It does, it's just not possible to sustain it."
- **On levers:** "There should probably be enchantments/passive nodes depending on class that lower reservation amounts of fervour/mana. Just another lever the player can pull to make their build."
- **What the owner chose:** "Reserve, don't drain".

**Read this first.** Every number proposed below is a judgement, not a measurement. Nothing has been run. The research settles the *shape* and does not settle the *numbers*.

## 0. Why the owner's level 100 character cannot keep an aura on

The Aura slot's mana cost, 20, is a cost per second that grows with level like every skill cost:
- cost at level L = 20 × (Default line's maximum mana at L ÷ 644);
- so the drain is 1.55 a second at level 1, 5.1 at level 20, 10.7 at level 50 and **20.0 at level 100**.

The Default class line regenerates `1 + 0.1 × (L − 1)` mana a second, **10.9 at level 100**. So one aura loses about 9 mana a second net, and a pool of about 500 empties in under a minute. A second aura doubles the loss.

Only the Ritualist (26.75 a second) and the Necromancer (19.3 a second) come close to paying for one aura at level 100.

| What | File and line |
|---|---|
| The slot's figure | `game/Data/SkillSlots.csv:6` (from the Skill Slots sheet of the workbook) |
| Scaling by level | `CataclysmSkillSlots.cpp:87-104` |
| The pulse that charges it | `CataclysmSkillTemplates.cpp:6196-6265` (`UCataclysmAuraSkill::Pulse`) |
| The line in the owner's log | `CataclysmSkillTemplates.cpp:6246-6259` |

## 1. What the research settles, and what it does not

All sources were read through a summarising fetch tool, so the quotes are probably accurate but were not checked character by character. poewiki.net and poe2wiki.net refused automated reading.

**Path of Exile 2, Spirit:**
- It is a resource separate from mana. poe2db.tw/us/Spirit: "Spirit is a reserve of power used to activate and maintain skills with permanent effects."
- A character starts at 0 and gets about 100 from campaign bosses (maxroll.gg/poe2/resources/spirit-guide). A sceptre gives 100 (poe2db.tw/us/Sceptres).
- Persistent skills reserve a **flat** amount:
  - Herald of Ash: "Reservation: 30 Spirit"
  - Grim Bargain: "Reservation: 60 Spirit"
  - Skeletal Warrior: "Reservation: (60—20) Spirit", falling with gem level
- Reservation "do[es] not deactivate on death". "Reserving a resource does not change the maximum value of that resource." (poe2db Spirit)
- **No page I read gives a per-second cost for a persistent skill.** That is inferred from how the pages describe Spirit; no page says it outright.
- Supports raise a reservation either flat ("Additional Reservation: 20 Spirit", Deadly Herald) or by a multiplier ("Reservation Multiplier: 130%", Hulking Minions).
- Reservation efficiency appears on gear, typically 4% to 12% per affix and 30% to 50% on one unique, with "less" and "reduced" modifiers as well (poe2db Spirit).

**Path of Exile 1, mana reservation:**
- Auras reserve a **percentage** of maximum mana: Hatred, Grace and Determination 50%, the Purities 35%, Heralds 25% (poedb.tw).
- Clarity is the flat exception: "Reservation: (34—279) Mana".
- poedb.tw/us/Aura: "Reserved mana can't be used or recovered until whatever is reserving it is disabled."
- Blood Magic and Arrogance reserve life instead of mana.

**Efficiency (Path of Exile 1, the 3.16 developer post, pathofexile.com/forum/view-thread/3185299):**
- "Reservation Efficiency works by dividing the cost of the Reservation Effect."
- "100% increased Reservation Efficiency would halve the cost of all Auras."
- Stacking it "does not allow for infinite Auras".

**Not settled by any source I could read:**
- A numeric cap or floor on efficiency in either game.
- Path of Exile 2's exact formula and rounding: player forum posts give `Base / (1 + efficiency)`, with no official answer.
- Last Epoch: no page was readable.

**What is this game's own judgement:**
- **The shape**, a flat reservation of maximum mana and no per-second cost, is Path of Exile 2's, and the owner chose it.
- **These are judgements:** that the resource is mana rather than a new Spirit pool (the owner's words say "mana"), every number below, and the cap.

## 2. How a character runs more than one aura today (owner: "It does, it's just not possible to sustain it")

**Each damage type adds its own aura.**
- The skill pool adds each carried damage type's aura once: `CataclysmWeaponSkills.cpp:446`, "AND THE TYPE'S AURA ONCE".
- There are 8 aura rows, one per Cataclysm type (`WeaponSkills.csv` lines 5 and 397 to 404).
- So a character carrying two damage types has two auras to choose from.

**Any skill can go on any key.**
- `CataclysmPlayerState.cpp:785-845` (`RefusalForSkillChoice`) refuses only the basic-attack key, a skill not in the pool, and a skill already on another key. GDD: "A SLOT IS A KEY, AND NOTHING ELSE."
- So the second aura goes on another key, for example Heavy. Both run, and each pays its own upkeep.
- An aura on the Ultimate key pays its upkeep doubled (ruled 2026-10-15).

**Timed aura-shaped skills in other slots** run alongside with no upkeep, paying once: Grave Muster, Grave Shroud, Fevered Blood, Living Pyre, Coin of Misrule, Aegis of Dawn.

**Counting and limits:**
- Running auras are counted by `State.AurasHeld` (`CataclysmAbilitySystemComponent.cpp:1504-1520`), which is tested with two running.
- Nothing grants an extra aura key. There is no weapon swap: any equipment change refills the keys and ends running auras (`CataclysmWeaponSlotsComponent.cpp:303`).

**So the number of auras on at once is bounded by the damage types a character carries and by the six keys, not by mana alone.**

## 3. The proposal: how reservation works (judgements unless marked)

1. **An aura on reserves a flat 50 maximum mana.** No per-second cost, and no charge for switching it on.
   - The figure is flat and does NOT grow with the character's level. That is what makes auras hard at low level and easy at high level, as the owner asked.
   - It stays in the Skill Slots sheet's Aura row, as a new "Mana Reserved" column, so it is tuned in one place.
2. **The aura skill's own level still lowers it**, by the cost factor every skill already has: 0.9642 per skill level (`CataclysmSkillLevels.h:67`). That is 50 at skill level 1 and about 25 at skill level 20, the same lever as Path of Exile 2's Skeletal Warrior (60 down to 20).
3. **It copies the health reservation that already exists** ("RESERVATION AS THE GENRE HAS IT", `CataclysmAbilitySystemComponent.h:2996-3039`):
   - maximum mana is unchanged;
   - current mana is held at or below maximum less reserved;
   - regeneration tops up only to that ceiling.
   Skill costs keep reading current mana, so they cannot spend reserved mana. This is Path of Exile 1's rule ("Reserved mana can't be used").
4. **Switching an aura on needs room against the maximum**, not against current mana: the unreserved maximum must be at least the reservation. This is how Fervour reservation is measured (`CataclysmCommand.h`, "MEASURED AGAINST THE MAXIMUM").
   - On switch-on, current mana above the new ceiling is lost, as `ClampFervourToSpendable` does for Fervour.
   - Pressing the key with no room plays the existing refusal and says why.
5. **Switching off returns the reservation at once.** The mana is not refilled; regeneration refills it.
   - Death ends auras only where `auras_end_at_death` says so, as today.
   - If maximum mana falls below what is reserved (a passive point taken back, a buff ending), the most recently switched-on aura switches off, until the rest fit.
6. **On the Ultimate key the reservation doubles,** as the upkeep does today (ruled 2026-10-15). So two keys' rules stay one rule.
7. **Paying from another pool:**
   - Water to Blood: maximum mana is 0, so the aura reserves health at the existing 3 health to 1 mana (ruled 2026-10-06), through the health-reservation code that exists.
   - Cast from Ward and "pays from health when short" do not apply. A reservation is not a payment that runs short.
8. **Conditions that read a mana percentage** (`mana_below`, `ManaPercent`) read the unreserved maximum, as Path of Exile 1 does (`CataclysmStatPipeline.h:2625` already says so).
   - Rows that scale with **maximum** mana keep reading the maximum, because reserving does not lower it: the Ritualist's Vessel ("1 Fervour for every 20 maximum mana"), regeneration as a share of the maximum, and Mana Weaver.
9. **The enemy's aura**, the Corrupted Stalker (`CataclysmCorruptedStalkerCharacter.cpp:542-560`), switches its aura off below 30% mana. That rule is about an enemy's own pool and is kept as it is. The stalker's aura will no longer drain it, so the rule will seldom fire. **Put to the ruling:** keep it, or remove it.

### How many fit, with no gear and no mind points, at 50 a level-1 aura

"Room" is enough unreserved mana for one Ultimate (150 at level 100, scaling down) and one Special (40), at that level's costs. Maximum mana is `base + per level × (L − 1)`, from `game/Data/ClassStats.csv`. Classes without their own line use the Default line: Masochist, Shadow, Soul Collector, Bulwark, Berserker and Saboteur.

| Class | L1 max / auras | L20 max / auras | L50 max / auras | L100 max / auras | First level one aura leaves room |
|---|---|---|---|---|---|
| Default line (6 classes) | 50 / 0 | 164 / 2 | 344 / 4 | 644 / 9 | 5 |
| Ravager | 40 / 0 | 116 / 1 | 236 / 2 | 436 / 4 | 13 |
| Necromancer | 70 / 1 | 241 / 3 | 511 / 8 | 961 / 15 | 1 |
| Ritualist | 90 / 1 | 318 / 5 | 678 / 11 | 1278 / 21 | 1 |

Under each figure, without the skill-level factor and without gear: 40 would allow one level-1 aura from about level 3 (Default) or 8 (Ravager), and 60 from about level 7 or 17. At high levels the keys and damage types bind, not mana. The full table for 40, 50 and 60 is in this session's notes and can be added here.

- **Mind** adds 2% maximum mana per point (`game/Data/Attributes.csv`), at 1 point per level.
- **Gear** adds flat and percent mana.
- A level-20 aura reserves about 25.

So these counts are the floor. **Recommendation: 50.** At low level it costs a large share of the pool: a level-1 Default character cannot hold one, and the Ravager waits until about level 13. Two fit for the Default line by level 20. At level 100 every class but the Ravager can hold more auras than it has keys for.

## 4. Reservation efficiency: the lever (owner: "enchantments/passive nodes ... that lower reservation amounts of fervour/mana")

**What exists, for Fervour:** `UCataclysmCommand::ReserveFor` (`CataclysmCommand.cpp:1085-1120`) works in this order:
1. Subtract a flat `minion_reserve_reduction` (the Ritualist's Crowned, 5).
2. Multiply by `max(0, 1 − Σ minion_reserve_percent_less / 100)`. The percents are **summed**, a negative row raising the cost, scoped by each row's Required Tags.
3. Multiply by the `minion_reserve` "more" rows (the Necromancer's Lone Revenant).
4. Hold the result at a **floor of 1**.

There is **no cap**: a summed 100% or more reserves 1. The reserve is fixed when each follower is made.

Existing rows on that stat:
- Enchantments:
  - "Your minions reserve 15%-30% reduced Fervour"
  - "Your deployables reserve 15%-30% reduced Fervour" [Type.Deployable]
  - "Your traps reserve 5%-25% reduced Fervour" [Type.Trap]
  - "Your minions reserve 25%-65% increased Fervour" (−25 to −65)
- Nodes: Saboteur Bag of Tricks and Minefield; Necromancer Cheap Bones (`basic_ch_c1`, "Your minions reserve 4% reduced Fervour per point", 6 points) and Lighter Bones.
- Bone-Bound (`basic_os_b0`) is a separate stat for minions raised on remains, clamped 0 to 100.

**A gap found:** the Bone Colossus (20) and the Abomination (100) reserve constants and skip `ReserveFor` entirely (`CataclysmBoneGiants.cpp:229, 281, 343, 354`). No reduction, enchantment or node reaches them today.
- **Recommended:** route them through `ReserveFor` in this change, so the levers reach them.
- **Put to the ruling:** that changes what they cost today for any build holding a reduction.

**Proposal for mana (judgement):** new stats with the Fervour shape, so one rule serves both:
- `mana_reserve_percent_less` (summed percents, a negative row raising it, scoped by tags such as Slot.Aura);
- `mana_reserve` "more" rows;
- `mana_reserve_reduction` (a flat amount off each aura).

No gameplay attribute, so negative values survive, as `minion_reserve_percent_less` does (`CataclysmPlayerClassStats.cpp:1092`).

**The cap (judgement; the sources give none):**
- Path of Exile's efficiency divides the cost, so it can never reach 0. This game's "reduced" shape subtracts, so it can.
- **Recommended:** the summed reduction stops at 75% for both mana and Fervour, and the floor stays (1 Fervour; 1 mana).
- That changes the Fervour rule, which is uncapped today, though I did not find a build that reaches 75% in the current rows. Six Cheap Bones points (24%), the best enchantment roll (30%) and Lighter Bones (16%) make 70%.
- Alternative: no cap for Fervour, as today, and 75% for mana only.

## 5. Enchantment rows (positive, judgements)

**The two existing aura-cost rows become reservation rows.** Their sentences change, so their row names change too, with aliases kept so saved items still load. Weight and Tags are unchanged:

| Row today | Becomes | Stat |
|---|---|---|
| "Your aura costs 30%-50% less mana per second" (Weight 4; Slot.Aura, Stat.Resource.Mana) | "Your auras reserve 30%-50% less mana" | `mana_reserve` more −30 to −50 [Slot.Aura] |
| Drawback: "Your auras cost 100%-200% increased mana per second" (Weight 4; Type.AOE.Aura, Stat.Resource.Mana) | "Your auras reserve 50%-100% increased mana" | `mana_reserve_percent_less` −50 to −100 [Slot.Aura] |

The drawback's range is lowered, because doubling or tripling a 50 reservation would shut auras out for most of the game. **Put to the ruling.**

**New rows**, copying "Your minions reserve 15%-30% reduced Fervour" (Generic, Weight 2):

| Sentence | Stat | Weight | Tags |
|---|---|---|---|
| Your auras reserve 15%-30% reduced mana | `mana_reserve_percent_less` 15 to 30 [Slot.Aura] | 2 | Slot.Aura, Stat.Resource.Mana |
| Each aura you hold reserves 5-15 less mana | `mana_reserve_reduction` 5 to 15 [Slot.Aura] | 2 | Slot.Aura, Stat.Resource.Mana |
| Your gadgets reserve 15%-30% reduced Fervour | `minion_reserve_percent_less` 15 to 30 [Type.Gadget] | 2 | Type.Gadget |

- The flat row is the one that helps most at low level: 15 off 50.
- The Fervour row for gadgets is optional. The deployables row already reaches gadgets and traps.

**Rows that read `mana_cost` will no longer reach an aura**, because reservation reads only the reservation stats:
- the general cost rows, such as "Skills cost 50%-75% more mana";
- the Ritualist's Ritual Focus and the Soul Collector's capstone ("mana_cost removed").

Nothing is lost by that; it is stated for the ruling.

## 6. Class passive nodes (judgements; existing nodes preferred)

| Class | Node (id) | Today | Proposed addition |
|---|---|---|---|
| Ritualist (mana) | Wider Vessel (`basic_d_c0`, 6 points) | +2% increased Maximum Mana per point | and "Your auras reserve 3% reduced mana per point" (18%) |
| Ritualist (Fervour) | Reserve (`basic_c_c1`, 6 points) | +2% increased maximum Fervour per point | and "Your minions reserve 2% reduced Fervour per point" (12%) |
| Necromancer (Fervour) | Cheap Bones, Lighter Bones, Bone-Bound already exist | — | none; the gain is routing the Colossus and Abomination through `ReserveFor` (section 4) |
| Necromancer (mana) | no mana node in the tree | — | none proposed; second-largest pool |
| Saboteur (Fervour) | Bag of Tricks, Minefield and a keystone already exist | — | none needed |
| Masochist | Beacon of Despair (`basic_fl_b2`, 8 points), an aura-themed node | aura debuff duration, damage | and "Your auras reserve 2% reduced mana per point" (16%) |
| Bulwark | Aura of Pain (`basic_tw_t3_001`, 8 points), an aura-themed node | retaliation aura | and "Your auras reserve 2% reduced mana per point" (16%) |
| Ravager | no mana, aura or minion node | — | **a new node would be needed**, if the owner wants the smallest pool to have a node; otherwise enchantments and mind are its levers |
| Berserker, Shadow | no fitting node | — | none; enchantments and mind |

**Soul Collector, kept apart**, because its tree is being reworked on `feat/soul-collector-keystones-rework`:
- Spirit to Mana (`basic_d_b1`, 6 points) has an empty stat row today. It could carry "Your auras reserve 3% reduced mana per point".
- Ossuary (`basic_a_stem0`, 8 points, +2% Fervour) could add "Your minions reserve 2% reduced Fervour per point".
- Either goes into that rework or after it.

## 7. What must change

**Data:**
- The Skill Slots sheet's Aura row: a "Mana Reserved" column of 50. Its Mana Cost goes to 0, because switching on is free.
- `tools/generate_datatables.py` reads the new column.
- The two aura enchantment rows reworded through the workbook re-apply script, with aliases in `CataclysmEnchantmentRenames.cpp` and `tools/tests/enchantment_row_names.txt`.
- The new enchantment rows and node rows.

**C++:**
- `UCataclysmAuraSkill`: no upkeep in `Pulse`, no cost at activation, and a room check and reservation hold and release.
- The ability system component: `ManaReserved()`, `UnreservedMaximumMana()` and `HoldManaToUnreserved()`, next to the health versions.
- The regeneration ceiling (`CataclysmRegeneration.cpp:682-685`), a current-mana clamp, and the percent conditions.
- `ReserveFor` (or a mana twin) for the reductions and the 75% cap.
- The bone giants routed through `ReserveFor`.
- The skill bar, the HUD and the character sheet showing reserved mana.
- Stale comments: `CataclysmGameplayAbility.cpp:352`, `CataclysmSkillTemplates.h:2390-2397`, `CataclysmDungeonModifierEffects.h:53`.

**Unreal tests that pin the drain** (`CataclysmSkillTemplateTests.cpp` unless noted):
- `AnAuraSwitchesOffWhenTheManaRunsOut` (:2725)
- `AnAuraKeepsRunningOnHealthWhenTheManaPoolBecameHealth` (:2788)
- `AnAurasUpkeepPaysWhatTheStatSaysRatherThanTheSlotsFigure` (:16104)
- `BelowTenPercentManaAnAurasUpkeepStillDrainsMana` (:16944)
- `CastFromWardAnAurasUpkeepIsPaidFromTheShieldWhenManaIsShort` (:17862)
- `ARowLeavesAnAurasRadiusAndItsUpkeepAsTheyWere` (:34723)
- the Edict of Abstinence test (:35455)
- `CataclysmVoidTests.cpp:370-408`
- `CataclysmSufferingAuraImmunityTests.cpp:419`
- `CataclysmSkillSlotTests.cpp:84-96`, which requires every slot but Basic to cost more than 0
- the Corrupted Stalker test

**Python:**
- `sim/tests/test_skill_slots.py:513-533` (the aura running out) and `:536-570` (the GDD cost cell)
- `tools/tests/test_skill_slot_sheet_matches_the_model.py:90-92`

**Simulation:** `sim/cataclysm_sim/character.py`, the Aura slot's `mana_cost=20` and `seconds_of_aura()`, which become a reservation and "auras that fit". The simulation models nothing else about auras.

**Documents:**
- GDD `docs/Cataclysm_GDD_v2.md` lines 688, 713, 756, 760, 805, 2429 and 2453.
- A `docs/DECISIONS.md` entry quoting the owner, superseding the 2026-08-04 ruling "| Aura | none | — | 20 per second |" and the upkeep rulings after it.

## 8. For the ruling

1. Flat 50, lowered by the skill's own level (to about 25 at skill level 20)? Or 40, or 60?
2. The 75% cap on summed reductions, for mana only or for Fervour too?
3. The bone giants routed through `ReserveFor`?
4. The drawback lowered to 50%-100% increased?
5. The Corrupted Stalker's switch-off rule: keep it or remove it?
6. A new Ravager node, or none?
7. Conditions reading a mana percentage read the unreserved maximum?
