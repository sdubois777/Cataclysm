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

**A SHAPE USED BY BOTH SIDES GOES IN `Skills/`.** The split above sorts a system
by who casts it, and section 5's whole argument is that a shape does not belong
to a caster: `NS_Proj_Body` is fired by ten designed player skills and by the
Brute, the Corrupted Sentinel, the Succubus and the Gatekeeper, and it is one
asset for all of them. `Impacts/` was the obvious second candidate and it is
described as "shared hit and death reactions", which a projectile in flight is
not. So the rule is: **`Enemies/` holds a shape only a creature can produce, and
everything shared goes in `Skills/`** rather than a third folder being invented
for it. Added on 2026-08-21, when building the first system that has two
casters.

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

**Judgement.** Four assets in `/Game/Effects/EffectTypes/`. The setting names
are exact.

**EVERY NUMBER BELOW WAS MEASURED ON 2026-08-22 AND NONE OF THEM MOVED.** Until
then they were guesses and this paragraph said so. What the measurement found is
under "MEASURED 2026-08-22" further down; the short version is that one of the
five limits does all the work and the other four have never fired. **"The
numbers did not move" is an outcome rather than the absence of one** -- issue
#547 is what produced it.

**`FXT_Enemy`** — the twenty-at-once case.
Update frequency `Medium`, significance handler `Distance`, cull reaction
`Kill and Clear`, cull by distance at **4000 cm**, maximum instances **60**,
maximum instances of one system **20**, cull when not rendered and cull by view
frustum both on.

The 4000 cm comes from this project's own camera rather than a generic figure:
`CataclysmPlayerCharacter.cpp` sets the camera arm to 800 cm, clamped between 500
and 1200, at a downward pitch of 60 degrees. 4000 cm is comfortably past the edge
of the frame at maximum zoom. **Measured 2026-08-22: it never fires in a fight**,
because in a fight everything is near the player by definition. It is there for
effects elsewhere on the floor.

**`FXT_PlayerSkill`** — update frequency `Low`, handler `Distance`, reaction
`Kill and Clear`, distance **6000 cm**, maximum instances **40**.

**`FXT_Ambient`** — update frequency `Low`, handler `Distance`, reaction
**`Asleep`** because it has to come back, distance **5000 cm**, cull when not
rendered on.

**`FXT_MustBeSeen`** — the deliberate no-culling escape hatch, exactly the engine
default. Only systems on a named list may use it, and adding one is a decision to
record. It exists so that "this must never disappear" is an explicit choice
rather than the accident of forgetting to set an effect type.

**A SHAPE WITH TWO CASTERS TAKES `FXT_Enemy`.** The first four names split by who
casts, and section 5's whole argument is that a shape does not belong to a
caster. Where one asset serves both -- `NS_Proj_Body` is fired by ten designed
player skills and by four creatures -- `FXT_Enemy` is the one to pick: it is the
tighter of the two on distance, 4000 cm against 6000, and the looser on instance
count, 60 against 40, which is the safer pair for something that can be numerous.
That was a choice between two unmeasured sets of numbers when it was made on
2026-08-21. Both sets have since been measured and neither moved, and the
measurement supports the choice for a reason nobody had in mind at the time:
`NS_Proj_Body` costs 102 microseconds per instance, the most of any system in
the game, so the tighter distance is the right one for it.

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

### The budget

**Decided 2026-08-22, issue #547.** Until that date this project had no
performance budget for effects of any kind and no way of measuring against one,
and every number in the four effect types above was a guess that said so.

#### Frame time: 2 milliseconds on each of three thread groups

| Console variable | Budget | What it covers |
| :-- | :-- | :-- |
| `fx.Budget.GameThread` | 2.0 ms | FX work that runs only on the game thread |
| `fx.Budget.GameThreadConcurrent` | 2.0 ms | FX work on the game thread or on a task spawned from it |
| `fx.Budget.RenderThread` | 2.0 ms | FX work on the render thread |

**These are Unreal's own defaults, and keeping them is the decision rather than
the absence of one.** Epic publishes no numeric target: its page on measuring
performance in Niagara says to capture a window of your own game rather than
apply a universal figure. So 2 ms is the only published reference there is. It
is also generous here -- a frame at 60 frames per second is 16.7 ms, so this
allows effects an eighth of it, for a game whose effects are five sprite and
mesh bursts.

**THE ENGINE SHIPS THIS BUDGET SWITCHED OFF.** `fx.Budget.Enabled` and
`fx.Budget.EnabledInEditor` are both false by default. Until 2026-08-22 that
meant nothing tracked what effects cost, the Budget column of the Niagara debug
display read zero whatever was happening, and the `bCullByGlobalBudget` switch
on an effect type had no input to act on even if somebody had turned it on.
`game/Config/DefaultEngine.ini` now sets all five values under
`[SystemSettings]`.

**Tracking the budget is not the same as enforcing it.** Culling by it also
needs `bCullByGlobalBudget` on the effect type, and that is false on all four.
`tools/generate_effect_types.py` now writes that false explicitly, so it is a
decision rather than a property nothing touched. Issue #824 is the decision to
turn it on and says why it waits.

#### Video memory: 512 MB of the card's 7250 MB

**This one is a judgement and not a measurement, and the difference is the point
of this whole section.** What was measured on 2026-08-22 is the whole game:
**1331 MB on average and 1420 MB at peak**, against the 7250 MB the driver
offers on the development machine's 8 GB RTX 3070. The effects' own share of
that was not separated out. Issue #826 is the work to separate it.

512 MB is roughly a third of everything the game uses today. It is far more room
than five sprite systems can consume, and it keeps the game under half the card
even if effects grew into all of it. `docs/DECISIONS.md` records the 8 GB card
as the binding hardware constraint and Paragon's 4K character textures as what
presses on it; effects are not the pressure.

---

### How to measure against it

**Everything below is typed into the `Cmd` box at the bottom of the editor
window.** It takes about ten minutes and needs no build.

**1. Set up a repeatable floor.** A named seed so the same run can be compared
against itself later.

```
Cataclysm.DungeonSeed 547
Cataclysm.DungeonLayout 0
Cataclysm.DungeonEnemyScale 1
```

**2. Turn the Niagara debug display on BEFORE pressing Play.**

```
fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewMode=1
```

`NiagaraDebugHud 1` is the short form of the same thing. `fx.Niagara.Debug.Hud`
with no arguments prints the full list of keys it accepts to the output log.

**IT DRAWS ON THE SCREEN AND IT DOES NOT APPEAR IN A CAPTURED IMAGE.** Neither
`EditorAppToolset.CaptureEditorImage` nor `SlateInspectorToolset.Screenshot`
includes the debug-draw layer, although both capture the game's own heads-up
display drawn on the same canvas. Somebody has to look at the screen. This was
nearly recorded as "the documented profiling method does not work" during #547,
and the project owner saying "the debug hud is showing" is what corrected it.

**3. Press Play and get into a fight.** Not an empty floor -- a fight is the
case the effect types exist for. Twelve to twenty creatures around the player is
the worst realistic case; the design commits to twenty attacking at once.

**4. Capture.** 600 frames is about seventeen seconds at the frame rate a fight
runs at.

```
fx.ParticlePerfStats.RunTest 600 1 1 0
```

The four arguments are the number of frames, whether to gather per-world
statistics, whether to gather per-system statistics, and whether to gather
per-component statistics. Per-component is a great deal of output and is rarely
what the question is.

**5. Read the result.** It is written twice: to the output log, and as a file
under `game/Saved/Profiling/ParticlePerf/`. **Every figure is microseconds.**
The columns that matter:

| Column | What it is |
| :-- | :-- |
| Average PerFrame GameThread | the system's cost per frame, game thread and its concurrent tasks together |
| Average PerInstance GameThread | the same divided by how many were alive |
| Average PerFrame RenderThread | the same on the render thread |
| NumFrames, Total Instances | divide the second by the first for the average number alive at once |
| Max PerFrame GameThread | the worst single frame, which is what the budget is really about |

**Two traps in that file.** The scalar in the `Max PerFrame RenderThread` column
is not what the heading says -- the engine writes the maximum **per instance**
there and puts the per-frame maxima in the bracketed list beside it. And a
system with no instances during the capture is left out of the table entirely
rather than printed as a row of zeroes, so an absent system means "did not
happen", not "cost nothing".

**6. Read the debug display for what was culled.** Its overview names every
system and splits what was culled by why: distance, visibility, instance count,
budget. It also has a `# Player` column, which counts effects exempted as the
local player's. This is the only place that number can be seen.

**7. For the frame as a whole rather than the effects,** capture with the CSV
profiler instead. It writes to `game/Saved/Profiling/CSV/`, one row per frame,
and carries `FrameTime`, `GameThreadTime`, `RenderThreadTime`, `GPUTime`,
`GPUMem/LocalUsedMB`, `GPUMem/LocalBudgetMB` and a per-bucket breakdown
including `Exclusive/GameThread/Effects`.

```
csvprofile start
csvprofile stop
```

**WHAT THIS MEASURES IS THE EDITOR AND NOT THE GAME.** Play-in-editor runs a
Development build with the editor's own Slate drawing inside the frame. The
frame rate it reports is pessimistic and no packaged build of this project has
ever been measured. **The microsecond figures per effect are still comparable**,
because they are the same work either way, and the budget is set on those rather
than on frame rate.

---

### MEASURED 2026-08-22: what it actually costs

**Conditions.** Unreal 5.8 Development editor build, play-in-editor in the level
viewport, RTX 3070 8 GB. Level `L_Dungeon`, seed 547, Halls layout, the designed
creature density. 232 to 267 creatures on the floor. The player was placed at the
densest point on the floor, with twelve to seventeen creatures within eight
metres, and at `Cataclysm.PlayerLevel 100` so the fight lasted through the
capture. Three captures of 600 frames each.

#### Per system, with the effect types as they are

Microseconds per frame, from `fx.ParticlePerfStats.RunTest 600 1 1 0`.

| System | Game thread | Render thread | Alive at once | Worst frame, game thread |
| :-- | --: | --: | --: | --: |
| `NS_Impact_Point` | 726 | 431 | 12.8 | 2016 |
| `NS_Cast_Windup` | 95 | 19 | 1.3 | 424 |
| `NS_Strike_Arc` | 83 | 17 | 1.3 | 342 |
| `NS_Proj_Body` | 102 | 88 | 1.0 | 199 |
| `NS_Impact_Ground` | did not play | | | |
| **everything** | **906** | **468** | **15.4** | **2373** |

**One system is 80 per cent of the game-thread cost and 92 per cent of the
render-thread cost of every effect in the game.** Issue #822 is the work on it.

#### Against the budget

| Budget | Allowed | Average | Worst frame |
| :-- | --: | --: | --: |
| `fx.Budget.GameThread` | 2.0 ms | 0.12 ms | 0.95 ms |
| `fx.Budget.GameThreadConcurrent` | 2.0 ms | 0.91 ms | **2.37 ms** |
| `fx.Budget.RenderThread` | 2.0 ms | 0.47 ms | 1.05 ms |

**The worst frame is already over one of the three.** That is recorded rather
than accommodated: raising a budget to fit a measurement defeats the point of
having one.

The two game-thread rows differ because they measure different things and the
difference is easy to misread. `Exclusive/GameThread/Effects` from the CSV
profiler is work on the game thread alone. `fx.ParticlePerfStats`'s
"PerFrame GameThread" includes the concurrent tasks spawned from it, which is
by far the larger part -- 454 ms of the 543 ms total across a 599-frame capture
-- and which does not sit on the game thread's critical path.

#### What each culling number turned out to be worth

| Setting | Value | What the measurement said |
| :-- | --: | :-- |
| Distance | 4000 cm | **Never fired.** In a fight everything is near the player. It is for effects elsewhere on the floor |
| Visibility and view frustum | on | **Never fired** |
| Instances of one system | 20 | **The only limit that ever fires**, and it fires hard |
| Instances across the type | 60 | **Never approached.** The most alive at once across every system was 15, and 26 with the per-system cap off |
| Global budget | off | Fires nothing because it is off. Issue #824 |

**Switching the per-system cap off, in the same fight:**

| | Cap at 20 | Cap off | Change |
| :-- | --: | --: | --: |
| `NS_Impact_Point` alive at once | 9.8 | 23.6 | 2.4x |
| `NS_Impact_Point` game thread | 418 us | 1915 us | **4.6x** |
| `NS_Impact_Point` worst frame | 1337 us | 4894 us | **3.7x** |

So the cap is doing real work and is not being raised. **It is also throwing
effects away**: the debug display counted **70 instances of `NS_Impact_Point`
killed by instance-count culling in a single fight**, and the cull reaction is
Kill and Clear, so a hit past the cap draws nothing and one already playing can
be cut short. Issue #822.

The same reading showed **`# Player: 0` for every system** -- nothing this
project spawns is marked as belonging to the local player, which is exactly the
trap the previous subsection warned about before the first system existed. Issue
#823.

#### The frame as a whole, which is not about effects at all

From a 2382-frame CSV capture of the same fight, milliseconds.

| Game thread bucket | Average | The 100 slowest frames |
| :-- | --: | --: |
| Interface | 5.10 | 5.43 |
| Animation | 4.75 | 5.50 |
| Character movement | 2.64 | 3.30 |
| Ticking actors | 1.66 | 1.73 |
| **Effects** | **0.12** | **0.14** |

The frame averaged 23.66 ms, which is 42 frames per second, and the game thread
is all of it -- the render thread averaged 4.33 ms and the GPU 7.06 ms.

**On the hundred slowest frames the interface costs thirty-eight times what
every particle system in the game costs together.** Whatever is making a dungeon
floor slow, it is not the effects. Issue #825.

**That is also worth holding against the project owner's words on the effects,
"kinda messy but it's fine".** Messy is not a load problem in the sense of
particles crowding out the frame. The one way load could be part of it is the
70 killed instances above, which look like effects winking out.

---

## 5. The template per damage type

### One system per effect shape, not per damage type

**Judgement.** The eight damage types are eight rows of data, not eight assets.
They already exist as gameplay tags in `game/Config/Tags/CataclysmTags.ini`:
`Element.Celestial`, `Element.Chaos`, `Element.Death`, `Element.Demonic`,
`Element.Famine`, `Element.Pestilence`, `Element.Void`, `Element.War`.

The shapes:

| System | What it is | Built |
| :-- | :-- | :-- |
| `NS_Impact_Point` | a hit landing on a target | yes |
| `NS_Strike_Arc` | a melee swing leaving the caster | yes |
| `NS_Impact_Ground` | an area attack landing on the floor | yes |
| `NS_Proj_Body` | a projectile in flight, with its trail | yes |
| `NS_Beam` | a continuous line from caster to target | no |
| `NS_Aura_Persistent` | a looping field around a caster | no |
| `NS_Cast_Windup` | the caster's own build-up | yes |
| `NS_Death_Dissolve` | an enemy's death | no |
| `NS_Status_Applied` | an ailment landing | no |

**`NS_Strike_Arc` IS A NINTH SHAPE ADDED ON 2026-08-22, and adding it was a
design decision rather than only an authoring job.** This document listed eight
and a melee swing was not among them, while `game/Data/WeaponSkills.csv` gives
the `Strike` shape to 16 of the 51 designed Demonic skills against
`Projectile`'s 10 -- making it the most common skill shape in the game and the
only one that drew nothing at all. Issue #811.

**It is not a reuse of `NS_Impact_Point` and the difference is the moment, not
the look.** An impact is what a blow looks like where it LANDS: at the target,
lasting an instant, and refused entirely for a blow that connected with nothing.
A strike arc is what the swing looks like where it STARTS: at the caster,
sweeping, and drawn whether or not anything was hit, because a swing that misses
still happened. One asset cannot be both without one of those rules being wrong.

**Nine shapes times eight damage types is 72 assets built the wrong way. Built
this way it is 9 assets and 8 data rows.**

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

**Settled on 2026-08-13, in issue #546.** The project owner chose all eight
pairs and they are recorded in section XIII of `docs/Cataclysm_GDD_v2.md`, under
"The effect palette, which is not the environment palette". They are not the
eight environment themes and are deliberately different from them.

They are also in the game, as `DT_ElementVisuals`, built by issue #549. The
design document states them as sRGB hex because that is what a colour picker
shows; `tools/generate_datatables.py` converts each one to linear on the way
into the table, because that is what an `FLinearColor` is.

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

## 5A. What a finished ability effect is made of

**Written on 2026-08-22, after the project owner rejected the effects for the
third time.** The first two attempts added parts to a single system and the
verdict did not change: "Still just shooting regular looking grey orbs with some
mediocre effects on them, the big stomps and ring aoes are still nothing.
Nothing looks remotely close to a finished skill." This section is the research
that was missing, so the next attempt has a standard to build to rather than a
feeling to chase.

### One skill is three systems, not one

**Practice, and it is the structural finding.** Commercial Niagara ability packs
are organised as *muzzle*, *projectile*, *hit* — three separate systems that a
skill combines. Gabriel Aguiar's Magic Projectiles Mega Pack volume 1 ships 240
effects and states the split exactly: **91 projectiles, 83 hits and 66 muzzles.**

That is the same three beats Riot's League of Legends VFX style guide calls
**anticipation, impact and dissipation**. Its stated purpose: "Lead the brain
with anticipation and then overload the brain in that moment that it's been
waiting for, and then you want to give the brain time to process what just
happened."

**So a skill with no muzzle has no anticipation, and a player's brain is never
led.** That is a third of the effect missing, and it is missing from every skill
in this project: `NS_Cast_Windup` has never been built.

### The layers inside one system

**Practice.** A hit or a burst is conventionally built from four kinds of layer,
and each does a different job:

| Layer | What it does | Timing |
| :-- | :-- | :-- |
| **Core flash** | the bright shape at the centre | fast, bright, gone quickly |
| **Debris and sparks** | small pieces thrown outward | outlive the flash |
| **Energy and glow** | the lingering aura or afterimage | slowest to fade |
| **Distortion** | heat haze, a shockwave, camera shake | brief, and it is what sells force |

**This project had one of the four when this was written.** Every emitter drew a
sprite, which is the core flash and the sparks and nothing else.

**Two systems have all four as of 2026-08-22.** `NS_Cast_Windup` was built with
them, and `NS_Impact_Point` gained a lingering glow and a heat distortion layer
the same day. There is still no camera shake anywhere.

**BOTH OF THE HIT BURST'S ORIGINAL LAYERS HAD NO BRIGHTNESS GAIN, AND ITS CORE
FLASH WAS DRAWN IN THE DARK ANCHOR.** The forty times measurement below was
applied to the ground ring and the shockwave and not to these two, so the burst
was close to invisible against light ground: the core linked
`User.ElementColourDark`, which for Demonic is (0.042, 0.003, 0.0006), at a gain
of one. Both now use the primary hue at ten. Found by capturing the system from
the game's own camera distance, 800 cm at 60 degrees down, which is the check
worth repeating on any layer that looks thin.

### Primary and secondary shapes, and why an orb reads as a placeholder

**Practice.** Riot's guide: use "distinct primary and secondary shapes to
minimise distracting noise". The primary shape must deliver immediate clarity
without the player having to decipher it; secondary elements support it through
value and saturation rather than competing with it. Layering many similar shapes
produces visual mud.

**Judgement, and it explains the word the project owner keeps using.** A round
dot has no silhouette: it is the same shape from every angle, at every rotation,
at every distance. It cannot be a primary shape because there is nothing to
read. **A sphere, a soft round sprite and a circular glow are the same non-shape
three times over**, and stacking them is the visual mud the guide warns about.
That is why "orb" and "placeholder" have meant the same thing in every round of
feedback on this project.

### Nothing may be constant over a particle's life

**Practice, and it is the cheapest thing that separates finished work from
unfinished.** The consistent beginner diagnosis across VFX writing is the same:
do not leave size, colour or alpha at a constant. Fade out over lifetime, ease
scale in and out, and give even a "static" element a slow drift, because a
little movement reads as living and intentional.

**Judgement, stated as a rule this project can be held to:** every emitter must
change at least two of size, colour, alpha and rotation across its life. An
emitter whose particles are born and die at the same size and the same colour is
not finished.

### Value and colour have working ranges, and the ends are not in them

**Practice.** Riot's guide: avoid the extremes of 0% and 100% brightness, and
avoid 0% and 100% saturation. A fully saturated colour blends into the interface
and a fully desaturated one blends into the environment. Use one dominant colour
with supporting secondaries rather than several competing ones. A wider value
range inside those bounds draws more attention, which is how a big skill is made
to feel bigger than a small one.

**This is already half-built here.** `DT_ElementVisuals` gives each damage type a
primary and a darker secondary for exactly this reason, and section 5 above
explains why the darker one exists. What was missing is the rule that the
brightest part of an effect is not pure white and the dimmest is not pure black.

### Timing: short, and shorter than it feels

**Practice.** Riot's guide on duration: if an effect "feels long, they're waaaaay
too long". Reduce the time effects stay on screen. Outros should be subdued and
should fade through value, hue, opacity or size rather than simply stopping.

**Judgement, the working numbers for this project**, to be moved once somebody
plays them:

| Beat | Duration |
| :-- | :-- |
| Cast wind-up | the skill's own wind-up, and it must end exactly when the skill fires |
| Melee swing arc | 0.25 to 0.30 s |
| Impact core flash | 0.10 to 0.15 s |
| Impact sparks and glow | 0.4 to 0.6 s |
| Ground shockwave ring | 0.35 to 0.5 s |
| Ground area effect | the zone's own duration, which the design gives in seconds |

### Scale is meaning, not decoration

**Practice.** Riot: effect scale must match the tier of the ability; a basic
attack must not look like an ultimate. Diablo IV does the same thing at runtime
rather than per asset — its lead visual effects artist confirmed skill intensity
rises with skill points and legendary affixes, and section 5 above already
records his statement that intensity changes spawn rate, velocity, emissivity and
colour range rather than uniformly scaling the effect.

### MEASURED 2026-08-22: the colours are being used about forty times too dim

**This is the largest single fault found in the effects and it was found by
looking at one, which nothing in this project had ever done.** It is written
first because it applies to every emitter, not to one shape.

**What was measured.** A ground shockwave ring was placed in `L_Sandbox`, the
level viewport camera was pointed at it, and the viewport was captured. At the
`DT_ElementVisuals` Demonic primary colour used at face value -- linear
`(1.000, 0.195, 0.027)` -- the ring is **almost invisible against the light grey
floor**. Multiplying that colour by roughly **40** produces a bright, obviously
readable ring. Nothing else changed between the two captures except the colour
and the size.

**Why it happens.** The pack materials for meshes are additive, and additive
blending adds the effect's colour to what is behind it. Against a near-white
floor there is almost no headroom left to add into, so a colour whose channels
top out at 1.0 changes the pixel barely at all. The same colour would read
adequately against a dark floor, which is presumably why it was never caught:
nobody looked at one on light ground.

**`EmissiveMultiplier` EXISTS FOR THIS AND IS 1.0 IN EVERY ROW.** Section 5
above specifies the column and says what it is for -- "how far above 1.0 the
emissive pushes", the place where breaking physically based rendering for
gameplay readability is quantified per damage type. `game/Data/ElementVisuals.csv`
carries it, `DT_ElementVisuals` carries it, and **nothing in the game reads it**.
Every effect therefore draws at multiplier 1.

**Judgement, the working figures**, to be moved once somebody plays them:

| Where the effect is drawn | Multiplier |
| :-- | :-- |
| An additive mesh on light ground: a shockwave, a ground ring | about 40 |
| A translucent sprite over a character: an impact core, sparks | to be measured; translucency does not have the same problem |

**The rule this produces:** an effect colour is never used at face value. It is
the damage type's colour times that damage type's `EmissiveMultiplier`, and the
multiplier is a per-damage-type number in the data because a near-black
secondary colour and a near-white primary need different amounts of push.

### The verification loop that found it, which should be used every time

**Nothing in the automation suite can see an effect** -- the test command passes
`-nullrhi` and Niagara refuses to create a component when `FApp::CanEverRender()`
is false, which is issue #559. But the **editor** can, and the Unreal MCP
toolset can drive it:

1. Duplicate the system to a throwaway name.
2. On the copy, set every emitter's `Loop Behavior` to `Infinite` **and set
   `Loop Duration` to roughly the particle lifetime**. Leaving the default 5
   second loop duration under a 0.4 second particle means the effect is on
   screen 8% of the time and a capture almost always lands in the gap. That
   wasted three captures on 2026-08-22 and looked exactly like "it does not
   render at all".
3. `SceneTools.add_to_scene_from_asset` the copy into `L_Sandbox`.
4. `EditorAppToolset.CaptureViewport` with an explicit `captureTransform`.
5. Look at the image. Then delete the actor and the copy.

**A system that has never been looked at should not be described as built.**

### The two rules that follow for this project

1. **A skill is built as three systems**, and a shape with no wind-up is
   unfinished no matter how good the other two are.
2. **Every emitter answers: what is my primary shape, what changes over my life,
   and which of the four layers am I.** An emitter that cannot answer all three
   is decoration and should be deleted rather than tuned.

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

**Steps 1 to 6 are done. Step 7 is what to build next.**

1. ~~**Decide the eight hues.**~~ Done in issue #546. Nothing else could be
   built correctly first, and it was a design decision that needed the project
   owner.
2. ~~**Build `DT_ElementVisuals` and its test.**~~ Done in issue #549. Eight
   rows against the existing tags, generated from the design workbook's
   "Element Visuals" sheet. No Niagara yet.
3. ~~**Build the four effect type assets**, with every switch explicitly set.~~
   Done in issue #555. An hour of work, and the difference between twenty
   enemies being playable and not. Built *before* the first system, so no system
   could be authored without one.
4. ~~**Build `NS_Impact_Point`.**~~ Done in issue #558. The first Niagara asset.
   It went first because enemies already land hits, so it is immediately visible
   in the sandbox, and because it exercises the whole convention at once. Two of
   its five acceptance criteria are still unmet and both need somebody to press
   Play: the debug display showing the instance count capping, and the effect
   being refused past 4000 cm.
5. ~~**Extract `NMS_ElementTint` and `NDS_IntensityCurve`** from step 4.~~
   **There was nothing to extract, and that is the finding rather than a step
   that was skipped.** `GetSystemDependencies` on the finished
   `NS_Impact_Point` reports twelve modules and three dynamic inputs, and every
   one of them is a stock engine asset — no scratch pad module was ever
   authored. The element tint is not a module at all: it is the stock
   `InitializeParticle`'s `Color` input **linked** to `User.ElementColour`, and
   an intensity curve is the stock `FloatFromCurve` dynamic input. A link and a
   stock dynamic input are already the shared mechanism this step was meant to
   create, and copying them into an authored module would add an asset to
   maintain and change nothing.

   **What did need extracting was in the C++, not in Niagara.** The parameter
   spellings and the `DT_ElementVisuals` lookup lived inside
   `UCataclysmImpactEffect`, where a second system could not reach them without
   asking a class called "impact effect" for its colours. They are now
   `CataclysmEffectParameterNames` and `UCataclysmElementVisuals`, and
   `Cataclysm.Effects.ProjectileBodyExposesTheStandardParameterBlock` asserts
   that both effect classes write the same names.
6. ~~**Build `NS_Proj_Body`.**~~ Done. Two emitters: a `Core` in **local** space,
   which is one looping sprite riding the projectile as its glowing head, and a
   `Trail` in **world** space, whose sparks stay where they were born and fade
   from `User.ElementColour` to `User.ElementColourDark` while their opacity
   falls to nothing. `User.Scale` drives the sprite sizes and `User.Intensity`
   the trail's spawn rate, both through the stock `Multiply_Float` dynamic
   input, so neither touches the component's transform.
7. **The rest.** By this point each new system is hours rather than days. The
   six shapes still unbuilt are `NS_Impact_Ground`, `NS_Beam`,
   `NS_Aura_Persistent`, `NS_Cast_Windup`, `NS_Death_Dissolve` and
   `NS_Status_Applied`. `NS_Status_Applied` is the one with a bug waiting on it:
   a burn ticking currently draws nothing, because `NS_Impact_Point` was taught
   to refuse damage over time rather than give it its own shape.

---

## What is not settled

Each of these is worth an issue rather than a guess.

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

Added 2026-08-22 for section 5A:

- Riot Games, League of Legends VFX Style Guide, 2017:
  <https://nexus.leagueoflegends.com/en-us/2017/10/dev-leagues-vfx-style-guide/>
- Riot Games, the style guide itself as a slide deck, including the timing
  section: <https://www.deck.gallery/league-of-legends-2017/slide/32-importance-timing-vfx-the/>
- VFX Apprentice, ten design tips drawn from that guide, which is where the
  value and saturation percentages and the primary-versus-secondary shape rule
  are stated plainly:
  <https://www.vfxapprentice.com/blog/10-league-of-legends-vfx-design-tips>
- Gabriel Aguiar, Magic Projectiles Mega Pack volume 1, whose contents list is
  the evidence for the muzzle, projectile and hit split:
  <https://www.artstation.com/marketplace/p/9VbXb/magic-projectiles-mega-pack-vol-1>
- Blizzard, Diablo IV skill intensity confirmed to rise with skill points and
  legendary affixes:
  <https://www.wowhead.com/diablo-4/news/spell-and-skill-intensity-confirmed-dynamic-player-skills-vfx-in-diablo-iv-332192>
- Blizzard, the Diablo IV art blast covering the visual effects team's approach:
  <https://magazine.artstation.com/2023/07/blizzard-entertainment-diablo-iv-character-animation-technical-art-vfx-art-blast/>
- The four-layer breakdown of an impact -- core flash, debris and sparks, energy
  and glow, distortion -- and the "nothing constant over life" rule:
  <https://www.gamineai.com/blog/how-to-create-game-vfx-particle-systems-visual-effects>
  and <https://www.animaticsassetstore.com/2025/11/05/vfx-for-games-with-gpu-particles-a-practical-guide/>

The engine defaults in section 4 were read from the local Unreal 5.8.1 install
under `Engine/Plugins/FX/Niagara/`, not from documentation. The naming audit
counted assets in that same install.
