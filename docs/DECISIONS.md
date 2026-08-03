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
