# Niagara Conventions

How this project names, parameterises, reuses and budgets Unreal's particle
systems. It answers the Niagara half of issue #19.

**The project has zero Niagara assets today.** This is written before the first
one exists, deliberately, because the full game needs effects for roughly 400
skill rows and the difference between assembling them from templates and
authoring them one at a time is the difference between that being possible and
not.

Every claim below is labelled:

| Label | Means |
| :-- | :-- |
| **Standard** | A published convention or engine fact with a source you can check. |
| **Practice** | What Epic's own shipped content or a named studio actually does, without a document mandating it. |
| **Judgement** | A decision for this project. Derived, not sourced. |
| **Unsettled** | The research did not answer it, and it is not guessed at here. |

---

## 0. The five asset types

Vocabulary first, because these are five separate things in the Content Browser
and the rest of this document assumes the difference.

- **Niagara System** — what gameplay spawns. One system is one visual effect.
  It contains emitters.
- **Niagara Emitter** — one behaviour inside a system: the sparks, the smoke,
  the flash. A system usually has three to six. An emitter can exist as its own
  asset and be shared.
- **Niagara Module Script** — one step inside an emitter's stack, such as "add
  gravity" or "fade out over life". This is the unit of shared *behaviour*.
- **Niagara Parameter Collection** — global values many systems read at once.
- **Niagara Effect Type** — carries no visuals at all. It holds the scalability
  and culling settings shared by a group of systems. Section 4, and it is the one
  that decides whether twenty enemies attacking at once holds frame rate.

---

## 1. Naming

### What Epic publishes, and why this project does not follow it

**Standard.** Epic's page *Recommended Asset Naming Conventions in Unreal Engine
projects* gives exactly three Niagara prefixes under its "Particle Effects"
heading: Niagara Emitter `FXE_`, Niagara System `FXS_`, Niagara Function `FXF_`.
The page describes itself as "only a recommendation to simplify setting up your
project. Your requirements will always take precedence."

**Practice, and this is the finding that decides it.** A search of the local
Unreal 5.8 install found **23,140 `.uasset` files and not one** matching
`FXS_`, `FXE_` or `FXF_`, case-insensitively. The same search finds 560 `BP_`,
1,251 `M_` and 1,063 `T_`, so it works. What Epic actually ships for Niagara
Systems is **`NS_`** — 40 assets, including `NS_Damage` and `NS_Jump_Trail` in the
Third Person template and `NS_TopDownAction_Destruction` in the Top Down
template. `NE_` appears twice. Each was confirmed to be a genuine
`NiagaraSystem` or `NiagaraEmitter` asset by reading its contents rather than
trusting its filename.

**So Epic does not follow its own recommendation for Niagara.**

**Judgement. This project uses the `N` family.** Three reasons, in order of
weight:

1. There are zero Niagara assets today, so it costs nothing to choose.
2. There is no visual effects artist. Any bought asset pack will ship `NS_`, and
   several hundred imported assets will not get renamed — so `FXS_` guarantees a
   permanently mixed project.
3. Every tutorial the reader follows will say `NS_`.

| Asset type | Prefix | Basis |
| :-- | :-- | :-- |
| Niagara System | `NS_` | **Practice** — 40 assets in stock Unreal 5.8 |
| Niagara Emitter | `NE_` | **Practice** — 2 assets in stock Unreal 5.8 |
| Niagara Module Script | `NMS_` | **Practice** — Epic's own *Niagara Scratch Pad Modules* page uses `NMS_ApplyOffset` as its worked example, without declaring it a convention |
| Dynamic Input Script | `NDS_` | **Judgement** — no source, chosen for consistency |
| Niagara Function Script | `NFS_` | **Judgement** — Epic says `FXF_`; changed for family consistency |
| Niagara Parameter Collection | `NPC_` | **Judgement** — it reads like "non-player character", accepted because there will be at most two in the project |
| Niagara Effect Type | `FXT_` | **Judgement** — no source anywhere |

**Unsettled.** Epic's page has no row for Module Script, Dynamic Input Script,
Parameter Collection, Effect Type or Simulation Cache — the asset types this
project will author most. The widely used community style guide has one Niagara
rule and no prefix table. There is no published table covering them.

Everything that is not Niagara stays on Epic's page, which this project already
follows: `DT_`, `M_`, `MI_`, `T_`, `SM_`, `ABP_`, `AM_`, `IA_`, `IMC_` and `DA_`
are all in `game/Content` today.

**Standard. No spaces in any Niagara identifier, ever** — including parameter
names and emitter names inside a system. The Gamemakin style guide's Niagara
section says spaces make writing shader code and scripts inside Niagara
"significantly harder if not impossible".

### Naming inside a system

**Judgement.** `NS_<Category>_<Subject>_<Variant>`:

```
NS_Impact_Slash          NS_Impact_Slash_Heavy
NS_Proj_Bolt             NS_Proj_Bolt_Homing
NS_Aura_Ground           NS_Death_Dissolve
NE_Sparks_Directional    NMS_ElementTint
```

**No damage type appears in any asset name.** That is the whole point of section
5: one asset serves all eight. Typing `NS_Impact_Demonic` means the template has
failed, and the answer is to fix the template rather than add the asset.

### Folders

**Judgement.** Extend the existing `/Game/Effects`, which already holds
`M_TelegraphMarker`:

```
/Game/Effects/
    EffectTypes/   FXT_*                 scalability assets, section 4
    Systems/
        Skills/    NS_*                  player abilities
        Enemies/   NS_*                  enemy abilities
        Impacts/   NS_*                  shared hit and death reactions
        Ambient/   NS_*                  environment loops
    Emitters/      NE_*
    Scripts/       NMS_ NDS_ NFS_
    Collections/   NPC_*
    Materials/     M_ MI_                effect-only materials
    Textures/      T_                    effect-only textures
```

**There is no folder per damage type.** Eight folders of near-identical systems is
the exact failure this document exists to prevent. Damage type is a parameter
value, not a location.

**Bought asset packs are never renamed and never moved.** They go to
`/Game/ThirdParty/<PackName>/` untouched. `tools/tests/test_third_party_packs_are_not_committed.py`
already keeps them out of git, and renaming them would break that and buy
nothing. These conventions apply to assets this project authors.

---

## 2. Parameter interface

### The feature is called User Parameters

**Standard.** This is how one system asset serves many abilities. Epic's *System
Settings Reference for Niagara Effects*: "User Parameters can only be set from
outside the Niagara simulation, such as from Blueprint logic or C++ code."

They can be read at any stage inside the system — the System, Emitter and
Particle module groups can all read the `User` namespace.

### How gameplay sets them

**Standard.** `UNiagaraComponent` exposes one typed setter per parameter type in
two forms, and **the `FString` form was deprecated in Unreal 5.3**. From
`NiagaraComponent.h` in the local 5.8 install:

```cpp
UE_DEPRECATED(5.3, "This method will be removed in a future release.  Please update to use the FName variant")
```

**Use the `FName` form.** In Blueprint, the node named "Set Niagara Variable
(LinearColor)" is the correct one; "Set Niagara Variable **By String**
(LinearColor)" is deprecated. There are sixteen `FName` setters, including
`SetVariableFloat`, `SetVariableLinearColor`, `SetVariablePosition` and
`SetVariableMaterial`.

**Pass the bare parameter name.** Niagara adds the `User.` namespace itself, so
both `"ElementColour"` and `"User.ElementColour"` work.

### The standard parameter block

**Judgement.** Every authored system exposes this block whether or not it uses
every entry. A system exposing no user parameters cannot be driven from data and
should not be committed.

| Parameter | Type | Set from |
| :-- | :-- | :-- |
| `ElementColour` | Linear Color | the damage type's row, section 5 |
| `ElementColourDark` | Linear Color | the same row's second, darker hue |
| `Intensity` | float | how loud the skill is, times its rank |
| `Scale` | float | the ability's radius in centimetres, divided by 100 |
| `Duration` | float | the ability's own duration |
| `ImpactNormal` | Vector | the hit result, for effects that align to a surface |
| `TargetPosition` | **Position** | not Vector — see the trap below |

**The names are identical across every system.** A skill row sets them without
knowing which system it spawned. The moment one system calls it `Colour` and
another calls it `Tint`, the data-driven path is dead.

### Two traps worth knowing before the first system

**Use `SetVariablePosition` and `SetNiagaraArrayPosition` for world positions, not
the Vector forms.** The Vector forms are single-precision underneath and lose
accuracy far from the world origin.

**A parameter that does not already exist on the system is silently ignored.**
Setting a name the system does not expose does nothing at all — no warning, no
error. That is the usual cause of "it compiles and the effect never changes".

### Lists of values

**Standard.** Scalar setters cannot push a list. Arrays go through
`UNiagaraDataInterfaceArrayFunctionLibrary`, which has `SetNiagaraArrayFloat`,
`SetNiagaraArrayVector`, `SetNiagaraArrayColor`, `SetNiagaraArrayPosition` and
others, plus per-element variants that write one index without replacing the
array.

**Judgement.** This is the right route for one shared effect reading twenty enemy
positions rather than twenty separate components — which is exactly the case this
design commits to.

### When to use a Parameter Collection instead

**Standard.** Epic: "Asset containing a collection of global parameters usable by
Niagara… any number of Niagara assets may reference attributes from this
parameter collection and will get new values when they are changed." A collection
can also reference a Material Parameter Collection so the two stay in sync.
Values resolve **per world**, so a collection is shared by every instance at once.

**Judgement.** If the value is the same for every instance in the world at a given
moment, it belongs in a collection. If it differs per cast, per caster or per
damage type, it is a user parameter.

One collection to start: `NPC_CataclysmTheme`, holding the current Cataclysm's
environment tint and a global effect brightness, slaved to the matching Material
Parameter Collection so effects and environment materials cannot drift apart.

**Caveat.** Epic's own comment on the runtime accessor for parameter collections
reads "This is gonna be totally reworked." The asset is stable; Epic has flagged
the runtime interface as not.

---

## 3. Reuse

**Read this first.** The research behind this document covered naming, parameter
interfaces, budgets and cross-studio practice. **It did not verify Niagara's reuse
features.** Everything in this section that is not cited is general knowledge and
should be confirmed in the editor before a library is built on it. That is said
plainly rather than dressed up.

### The unit of shared behaviour is the Module Script

**Judgement, unverified.** A Niagara Module Script is an asset that appears as a
step inside any emitter that adds it. Change the script and every emitter using
it changes. It is the equivalent of a function: `NMS_ElementTint`,
`NMS_FadeByLifetimeCurve`, `NMS_GroundAlign`.

A Dynamic Input Script plugs into a single input field rather than being a step of
its own — the right shape for "how does intensity map to spawn rate".

A parent Emitter asset is the third unit. **Unsettled:** how Unreal 5.8 labels
the choice between inheriting from a parent emitter and taking a one-time copy,
and what propagates, was not verified. Confirm before relying on it. Choosing
wrong here is silent and shows up months later when a change to the parent does
not reach its children.

### The precedent

**Practice.** Bungie's 2018 Game Developers Conference talk *The Visual Effects
Technology of Destiny* is the closest published account of running an effect
library at volume. Their mechanism for consistency was templating with
**selective** inheritance: an artist can inherit all of a template, inherit
specific parts, or bypass parts and add their own. From the speaker notes: "As new
features come online, we just modify a few templates to propagate changes across
the entire game."

**Judgement.** The transferable rule is that inheritance must be selective. A
template a designer cannot partially override gets abandoned and copied the first
time it does not quite fit. Bungie's engine is not Unreal, so this is philosophy
rather than technique.

### The traps

**Judgement, unverified in this pass.**

1. **Duplicating a system to make a variant.** It creates a disconnected copy. Do
   that eight times per element and there are thousands of assets to maintain.
   This is the failure the whole document is written against.
2. **Scratch Pad modules.** Niagara lets a module be authored inline inside one
   system. That is the fastest way to try something and is fine for that, but it
   is local and shares nothing. **Rule: no scratch pad module survives a commit.**
   Promote it to `/Game/Effects/Scripts/` or delete it.
3. **Using a Parameter Collection for per-cast values.** It is world-scoped, so
   every instance changes at once.
4. **Copying a bought system and editing it.** It brings its own effect type,
   its own parameter names and its own unbudgeted emitter counts. Take textures
   and materials from packs; author the systems.

---

## 4. Budgets and culling

This section decides whether twenty enemies attacking at once is playable.

### Nothing is on by default

**Standard.** A newly created Niagara Effect Type performs **no culling
whatsoever**. Verified against the local Unreal 5.8.1 install, in
`Engine/Plugins/FX/Niagara/Source/Niagara/Private/NiagaraEffectType.cpp`:

| Setting | Default |
| :-- | :-- |
| `bCullByDistance` | false |
| `bCullMaxInstanceCount` | false |
| `bCullPerSystemMaxInstanceCount` | false |
| `MaxDistance`, `MaxInstances`, `MaxSystemInstances` | 0 |
| `bCullWhenNotRendered` | false |
| `bCullByViewFrustum` | false |
| `bCullByGlobalBudget` | false |
| `SignificanceHandler` | none |
| `UpdateFrequency` | `SpawnOnly` |
| `CullReaction` | `DeactivateImmediate` |

A project also ships with **no default effect type at all** — the engine setting
is an empty path with no entry in the base configuration.

**So "use the default" means "no scalability".** Effect Type assets have to be
authored and committed.

**Two of those defaults are not off switches:**

- **`CullReaction = DeactivateImmediate` is the most aggressive reaction**, not a
  disabled safeguard. It is simply inert until a cull switch is turned on.
- **`UpdateFrequency = SpawnOnly` survives turning the cull switches on.**
  Scalability is only evaluated when a system spawns. Ticking "cull by distance"
  on an otherwise default effect type still will not remove a running system as
  the player walks away. The update frequency has to be raised **and** a
  significance handler assigned.

### Cull reaction

**Standard.** Five values, and the axis is whether the scalability system brings
the effect back, not whether it dies permanently.

| Value | Editor label | Behaviour |
| :-- | :-- | :-- |
| `Deactivate` | Kill | Stops spawning, particles die naturally. Not brought back. |
| `DeactivateImmediate` | Kill and Clear | Stops and kills particles now. Not brought back. **Default.** |
| `DeactivateResume` | Asleep | Particles die naturally, comes back when it passes the cull tests again. |
| `DeactivateImmediateResume` | Asleep and Clear | Killed now, comes back later. |
| `PauseResume` | Pause | Frozen, resumes when it passes again. |

**Judgement.** One-shot impacts take a Kill variant. Anything looping — auras,
environment — takes Asleep or Pause, or it never comes back when the player walks
back into range.

**Standard, and it has a structural consequence.** The cull reaction "is applied
to all effects using this effect type and can not be overridden per effect". So
one-shot and looping effects **must** be separate effect types. That is why
section 4.4 lists four rather than one.

### Update frequency

**Standard.** Five values, with these intervals:

| Value | Re-checked every |
| :-- | :-- |
| `SpawnOnly` | never after spawn |
| `Low` | 1.0 s |
| `Medium` | 0.5 s |
| `High` | 0.25 s |
| `Continuous` | every tick |

**One nuance that cuts against us.** Those are targets, not guarantees. The
engine staggers the work and hard-clamps to 50 instances per frame, so under a
heavy fight a `Low` effect may be re-checked less often than once per second.

**Judgement.** Short-lived hit effects are fine on `SpawnOnly`. Anything lasting
past about one second needs `Low` or `Medium` so it can be culled mid-life.

### Instance-count culling behaves in two different ways

**Standard.** From Epic's property documentation: "If the effect type has a
significance handler, instances are sorted by their significance and only the N
most significant will be kept. The rest are culled. If it does not have a
significance handler, instance count culling will be applied at spawn time only.
New FX that would exceed the counts are not spawned/activated."

**Judgement.** Without a significance handler, the first N effects win and the
twenty-first enemy's attack simply never appears.

**Standard.** Epic ships exactly two significance handlers: **Distance** ("closer
systems are more significant") and **Age** ("newer systems are more significant").
A custom one is a C++ class — there is no Blueprint route — so it is an
engineering task rather than a checkbox.

### The four effect types to author

**Judgement.** Four assets in `/Game/Effects/EffectTypes/`. **Every number below
is a starting point with no measurement behind it** and must be re-derived by
measuring. The setting names are exact.

**`FXT_Enemy`** — the twenty-at-once case.
Update frequency `Medium`, significance handler `Distance`, cull reaction
`Kill and Clear`, cull by distance at **4000 cm**, maximum instances **60**,
maximum instances of one system **20**, cull when not rendered and cull by view
frustum both on.

The 4000 cm comes from this project's own camera rather than a generic figure:
`CataclysmPlayerCharacter.cpp` sets the camera arm to 800 cm, clamped between 500
and 1200, at a downward pitch of 60 degrees. 4000 cm is comfortably past the edge
of the frame at maximum zoom. Measure it and tighten it.

**`FXT_PlayerSkill`** — update frequency `Low`, handler `Distance`, reaction
`Kill and Clear`, distance **6000 cm**, maximum instances **40**.

**`FXT_Ambient`** — update frequency `Low`, handler `Distance`, reaction
**`Asleep`** because it has to come back, distance **5000 cm**, cull when not
rendered on.

**`FXT_MustBeSeen`** — the deliberate no-culling escape hatch, exactly the engine
default. Only systems on a named list may use it, and adding one is a decision to
record. It exists so that "this must never disappear" is an explicit choice
rather than the accident of forgetting to set an effect type.

**Judgement.** Every authored system sets one of these four. A system with no
effect type is not reviewable and should not be committed.

### The local player exemption, and its trap

**Standard.** An effect type has a setting shown in the editor as **"Local Player
Culling"**, which defaults to **off** — meaning effects owned by, attached to or
caused by the locally controlled pawn are exempt from culling entirely.

**The trap.** An effect spawned unattached at a world location is **not**
automatically treated as a local player effect. A ground-targeted area attack
spawned at a cursor position, with no attachment and without being explicitly
marked as a player effect, is culled like anything else. Given this project's
click-to-move targeting, most player skills are exactly that shape.

**Judgement.** Mark every player-cast spawn as a player effect explicitly rather
than relying on attachment.

### Measuring

**Standard.** The Niagara Debug Heads-Up Display is a console command:

```
fx.Niagara.Debug.Hud Enabled=1
fx.Niagara.Debug.Hud SystemFilter=NS_Impact_Point
fx.Niagara.Debug.Hud OverviewEnabled=1
```

**Unsettled.** No profiling method, no graphics memory target and no frame-time
budget for effects exists anywhere in this project. The graphics card's 8 GB is
the binding constraint on the development machine and nothing quantifies what
share of it effects may use. The numbers in the four effect types above are
starting points, not measurements.

---

## 5. The template per damage type

### One system per effect shape, not per damage type

**Judgement.** The eight damage types are eight rows of data, not eight assets.
They already exist as gameplay tags in `game/Config/Tags/CataclysmTags.ini`:
`Element.Celestial`, `Element.Chaos`, `Element.Death`, `Element.Demonic`,
`Element.Famine`, `Element.Pestilence`, `Element.Void`, `Element.War`.

The shapes:

| System | What it is |
| :-- | :-- |
| `NS_Impact_Point` | a hit landing on a target |
| `NS_Impact_Ground` | an area attack landing on the floor |
| `NS_Proj_Body` | a projectile in flight, with its trail |
| `NS_Beam` | a continuous line from caster to target |
| `NS_Aura_Persistent` | a looping field around a caster |
| `NS_Cast_Windup` | the caster's own build-up |
| `NS_Death_Dissolve` | an enemy's death |
| `NS_Status_Applied` | an ailment landing |

**Eight shapes times eight damage types is 64 assets built the wrong way. Built
this way it is 8 assets and 8 data rows.**

### The data row

**Judgement.** A new table, `DT_ElementVisuals`, in `/Game/Data` alongside the
sixteen already there, keyed by the damage type's tag:

| Column | Type | Purpose |
| :-- | :-- | :-- |
| `ElementTag` | GameplayTag | `Element.Demonic` and so on |
| `PrimaryColour` | LinearColor | the hue the effect reads as |
| `SecondaryColour` | LinearColor | the darker or complementary hue |
| `EmissiveMultiplier` | float | how far above 1.0 the emissive pushes |
| `SpawnRateScale` | float | denser for Pestilence, sparser for Void |
| `VelocityScale` | float | fast for War, slow-drifting for Famine |
| `MotionCurve` | CurveFloat | the damage type's characteristic movement |
| `ImpactSound` | SoundBase | so audio and visuals share one row |

This follows the pattern the project already uses: `DT_StatusEffects` and
`DT_WeaponSkills` work this way, and the test suite already pins design document
values against generated tables.

### Choosing the eight hues

**Practice.** Final Fantasy XIV fixes a job's base colours before any individual
effect is made. Its battle effects lead, Takayasu Ishii, said deciding them "at
the outset" keeps the look consistent. Reaper was given a black and cyan scheme
specifically so it would not read as an existing dark class. But Sage's white and
blue was chosen by convergence rather than contrast — Sage is deliberately **not**
separated from White Mage by hue at all, but by **form**: White Mage's light airy
spells against Sage's geometric barriers and lasers.

**Judgement.** Fix all eight hues in one exercise before authoring any effect, and
check each against the specific hues most likely to be confused with it rather
than exhaustively against all pairs. The confusable pairs are already visible in
this project's own environment themes: **Death** is black, **Void** is black and
purple, **Chaos** is black and white — three blacks. **Famine** and
**Pestilence** share brown.

**Where hue cannot separate a pair, separate them by form**, as Final Fantasy XIV
does. Death is bone and cold and settles downward; Void erases and pulls inward.
A shape and motion difference survives twenty simultaneous instances better than a
subtle hue difference does.

**Unsettled. The eight hues are not chosen.** The design document fixes eight
*environment* themes but no per-damage-type effect palette. That decision comes
before anything in this section can be built.

### The conflict this creates

**Judgement.** Each damage type's effects will most often be seen against *its
own* environment — Demonic effects on lava, Death effects on shadow, Celestial
effects on gold and white. **An effect coloured like its damage type is at its
least readable in the environment that damage type generates.**

This is not hypothetical. The project already hit it with the attack warning
marker, whose measured worst case is 3.92:1 against Demonic lava, and 2.47:1
without its light inner line — below the accessibility threshold. The answer there
was to stop using one colour and use three bands, so a different one carries each
environment.

**The same answer applies here.** Every template exposes **two** colours, and the
darker one is not decoration: it is what keeps the effect legible when the primary
hue matches the floor.

### Lit or unlit

**Practice.** Diablo IV moved to lit effects. Its lead visual effects artist,
Daniel Briggs: "In Diablo IV, we use lit VFX that meld into the environment's
lighting." Their stated reason was cohesion, not readability — readability drove a
separate decision: "If we rely solely on environment lighting and follow true PBR
rules, then gameplay readability is muddied, particularly in dark environments
where a weapon swing would naturally be hard to see. To counteract this, many VFX
have emissivity."

**Judgement.** Gameplay-critical effects break physically based rendering; ambient
effects do not. The `EmissiveMultiplier` column is where that break is quantified
per damage type. Diablo IV is not on Unreal, so this transfers as art direction
rather than technique.

### Loudness is a second axis

**Practice.** Briggs again: "we reserve visually loud FX for powerful skills, like
ultimate abilities, while weaker skills meld into the background", and the
intensity is not baked — it rises as the player stacks upgrades that increase the
ability's power. Confirmed shipped.

**Practice, and this is the part that matters technically.** Changing intensity is
not a scale transform: "We do not uniformly scale every piece of an effect when
changing size and intensity; we modify things like spawn rate, velocity,
emissivity, and color ranges."

**Judgement.** So `Intensity` must not drive component scale. It drives spawn
rate, velocity, emissive multiplier and colour range, through a dynamic input
script whose curve is authored per system — and it must be checked at both ends of
its range.

---

## 6. What to build first

### Do not convert the attack warning marker to Niagara

**Judgement**, and it is first because it is the easiest thing to get wrong.

`ACataclysmTelegraphMarker` is four flattened engine meshes with an unlit
material and a measured contrast guarantee, pinned to the design document by
two test files. Niagara would buy nothing and would put that guarantee at risk.

The one thing section XIII promises that the marker does not do is "a fill that
sweeps as the wind-up runs out", and **that is a material parameter driven by
elapsed time, not a particle system.** It is issue #544.

**Rule: Niagara is for effects, not for readability guarantees.** Anything the
player must see to survive is geometry and a material with a measured contrast
figure.

### The order

**Judgement.** Each step proves something the next depends on.

1. **Decide the eight hues.** Nothing else can be built correctly first. It is a
   design decision and it needs the project owner.
2. **Build `DT_ElementVisuals` and its test.** Eight rows against the existing
   tags. No Niagara yet.
3. **Build the four effect type assets**, with every switch explicitly set. An
   hour of work, and the difference between twenty enemies being playable and
   not. Build them *before* the first system, so no system can be authored
   without one.
4. **Build `NS_Impact_Point`.** The first Niagara asset. It goes first because
   enemies already land hits, so it is immediately visible in the sandbox, and
   because it exercises the whole convention at once. Done means: it spawns on an
   enemy hit; changing a row in `DT_ElementVisuals` changes it with no asset edit;
   the debug display shows the instance count capping with twenty Brutes
   attacking; and it disappears at 4000 cm and does not come back.
5. **Extract `NMS_ElementTint` and `NDS_IntensityCurve`** from step 4, before
   there is a second system to keep in sync. This is where the library either
   becomes a library or becomes hundreds of copies.
6. **Build `NS_Proj_Body`.** The first real test of reuse — it should consume both
   scripts unmodified. If it does not, they were extracted wrong, and now is the
   cheap time to find out.
7. **The rest.** By this point each new system is hours rather than days.

---

## What is not settled

Each of these is worth an issue rather than a guess.

- **The eight effect hues.** Not in the design document and not in the research.
- **How emitter inheritance behaves in Unreal 5.8.** Section 3 is unverified.
- **Whether an effect type can be overridden per spawned component.** It decides
  whether one system asset can serve both enemy and player spawns.
- **Performance budgets.** The numbers in section 4 have no measurement behind
  them, and the 8 GB graphics card is the binding constraint on this machine.
- **A profiling method.** The debug display exists; what to look at and what
  number is too large is written down nowhere.

---

## Sources

- Epic, recommended asset naming conventions:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/recommended-asset-naming-conventions-in-unreal-engine-projects>
- Epic, Niagara scratch pad modules:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-scratch-pad-modules-in-unreal-engine>
- Epic, system settings reference for Niagara effects:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/system-settings-reference-for-niagara-effects-in-unreal-engine>
- Epic, key concepts in Niagara effects:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine>
- Epic, Niagara system scalability settings:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/NiagaraSystemScalabilitySettings>
- Epic, cull reaction values:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/ENiagaraCullReaction>
- Epic, significance handlers:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraSignificanceHandler>
- Bungie, *The Visual Effects Technology of Destiny*, Game Developers Conference
  2018: <https://www.gdcvault.com/play/1025282/>
- Blizzard, Diablo IV quarterly update, December 2021:
  <https://news.blizzard.com/en-us/article/23746639/diablo-iv-quarterly-updatedecember-2021>
- Square Enix, Final Fantasy XIV Backstage Investigators number 8:
  <https://na.finalfantasyxiv.com/blog/003308.html>

The engine defaults in section 4 were read from the local Unreal 5.8.1 install
under `Engine/Plugins/FX/Niagara/`, not from documentation. The naming audit
counted assets in that same install.
