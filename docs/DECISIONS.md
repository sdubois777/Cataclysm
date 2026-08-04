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

**Affects:** `Cataclysm_GDD_v2.md`, "Controls and Key Bindings". **Not applied.**
The table still says the left mouse button fires the basic attack and still puts
Support on W without noting the collision.

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
