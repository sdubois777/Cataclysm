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
| Common | 0% | 8.9% |
| Herald | 15% | 12.6% |
| Cataclysm Boss | 25% | 21.4% |

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
