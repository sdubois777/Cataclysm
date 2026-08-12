# Audio Implementation Plan

How the audio design in section XIII of `Cataclysm_GDD_v2.md` gets built: what
system plays it, what things are called, how they are mixed, and how a telegraph
cue is authored so it stays lined up with the animation it belongs to.

**Nothing here is implemented.** A search of `game/Source/` on 2026-08-12 finds no
audio code, and `game/Content/` holds no authored sound. This is the plan
implementation is built to. It answers issue #33.

---

## 1. MetaSounds, and no middleware

**Use Unreal's own MetaSounds. Do not license Wwise or FMOD.**

| | MetaSounds | FMOD | Wwise |
| :-- | :-- | :-- | :-- |
| Cost | Free with the engine. No licensing, no per-platform fee, no revenue threshold | Budget and revenue tiered, per title | Budget-gated free indie licence, then commercial licensing |
| Procedural audio | A full synthesis environment, node based, generating sound at runtime | Primarily sample based | Primarily sample based |
| Cost of entry | Engineering time — you build what the other two ship | Works out of the box | Works out of the box |

**Three reasons, in order of weight:**

1. **This project has no audio budget line and no audio person.** A per-title
   licence tied to budget or revenue is a commitment made before there is a
   product, and issue #501 has not even settled whether the game is sold. Free
   with the engine removes a decision that cannot be made yet.
2. **The engine version is already committed.** The project is Unreal 5.8, and
   Audio Insights — the profiling and debugging view for audio — became production
   ready in 5.8. That is the tooling gap that historically argued for middleware,
   and it closed in the version this project already uses.
3. **The design wants procedural variation more than it wants a mixing desk.**
   The one case that dominates everything is twenty Imps attacking at once. Twenty
   copies of one sample is a machine-gun artefact; twenty procedurally varied
   instances is a crowd. That is what MetaSounds is best at and what sample-based
   middleware needs work to fake.

**What this costs.** Engineering time, and it is real. MetaSounds gives no
authoring application for a non-programmer sound designer to work in, so anyone
brought in later works inside the editor rather than in a tool they already know.

**What would change this.** Hiring a sound designer who works in Wwise, or
shipping to a platform MetaSounds handles badly. Neither is true today, and both
would be visible well before they bite.

---

## 2. Mixing buses

Six buses, matching the priority order in section XIII. **The order is the
design, so it belongs in the bus structure rather than in per-sound volume
values**, which drift.

```
Master
├── Telegraph        enemy attack wind-ups. Ducks everything below it.
├── PlayerState      low health, damage taken, class resource
├── Combat           hit confirmation, impacts, deaths
├── Voice            enemy vocalisations
├── World            ambience, footsteps, loot, empire events
└── Music            ducked first, restored last
```

**`Telegraph` ducks every bus below it.** That is the whole point of separating
it: in the twenty-Imp case the telegraph is the sound that must survive, and the
crowd is on `Combat` and `Voice` where it can be pushed down.

**Empire events sit on `World` but bypass its ducking.** A surge warning has to
arrive during a fight, which is exactly when `World` is quietest. This is the one
exception in the structure and it is deliberate; if a second exception appears,
the structure is wrong and should be revisited rather than extended.

---

## 3. Naming

`AudioCategory_Subject_Variant`, matching the existing asset conventions in
`game/docs/content-layout.md`.

```
MS_Telegraph_Brute_Stomp          a MetaSound source
MS_Telegraph_Sentinel_Fire
MS_Voice_Imp_Attack_01            numbered variants for procedural selection
MS_Voice_Imp_Death_01
MS_Player_LowHealth
MS_Empire_SurgeWarning
SB_Telegraph                      a sound bus
SC_Demonic                        a sound class carrying damage-type identity
```

**A telegraph is named after the ability it belongs to, not after the sound it
makes.** `MS_Telegraph_Brute_Stomp` can be found from the ability; a name
describing the sound cannot, and the thing anyone searching for it will know is
the ability.

**Damage-type identity lives in a sound class, one per damage type**, so that the
eight are a set that can be compared and adjusted against each other rather than
eight unrelated decisions spread across every skill.

---

## 4. Authoring a telegraph alongside its animation

This is the part with a real trap in it, and it is the same trap the visual
telegraph work already hit.

**A telegraph cue is fired by an animation notify, not by a timer in code.** The
notify sits on the wind-up animation at the frame the wind-up begins. That is the
only way the cue and the marker stay together when the animation is retimed, and
animations here are retimed constantly: an enemy's attack animation is played to
fit its designed attack interval, at a rate of `clip length / attack interval`,
so the same clip runs at different speeds on different enemies.

**A cue fired by a timer would drift the moment a play rate changed, and nothing
would report it.**

### The rule the cue has to satisfy

Section X caps an enemy's wind-up at **half its attack interval**. A cue placed by
notify inherits the animation's play rate, so its position in real time is
`notify time / play rate`. The Corrupted Sentinel plays `Fire_Planted` at 1.20, so
a notify at 1.30 seconds inside the clip fires at 1.08 seconds in play — over its
1.0 second allowance.

**So the check is on the scaled time, not the authored time.**

### What blocks this today

**Nothing measures where inside a clip the damage lands**, for any of the seven
vertical slice enemies. `game/docs/enemy-source-assets.md` says so plainly:
`SequenceLength` gives the whole animation, not the moment inside it. That is
issue #526.

**Audio telegraph authoring is blocked on #526** for the same reason the visual
telegraph is. The wind-up window cannot be filled before it is measured. This is
worth knowing before anyone starts: the sound design is not the blocker, the
measurement is.

---

## 5. What to build first

In this order, because each one makes the next testable.

1. **The bus structure and the six sound classes.** Silent, but it is the thing
   everything else attaches to, and getting it wrong later means re-routing every
   asset.
2. **The low-health cue.** It is the single highest-value sound in the game: in
   Heretic it is the only warning a player gets that they are about to die, and it
   needs no animation work and no measurement.
3. **Hit confirmation.** The vertical slice's combat has never been judged by play
   (#525), and one reason is that a player cannot tell a hit landed — #518 covers
   the visual half. Audio is the cheaper half of the same problem.
4. **One enemy's telegraph, end to end**, once #526 has measured its wind-up. The
   Brute, because it is the enemy with a finished ability design and it is not the
   timing-risky one.
5. **The remaining six**, following the path the first one proved.

Music and ambience come after all of it. Nothing in them is load-bearing, and
they are the first thing ducked.

---

## 6. What this plan does not settle

- **Who authors the sound.** There is no audio person on this project and
  MetaSounds does not change that. Whether sound is commissioned, licensed from a
  library, or generated is an open question and overlaps the asset generation
  decision in issue #17.
- **A subtitle or visual equivalent for every audio cue.** Section XIII commits to
  accessibility as removing barriers of perception, which cuts both ways: a player
  who cannot hear needs the telegraph too. The visual telegraph already exists for
  combat, so the gap is the empire layer, where a surge warning may be audio only.
  Not designed here.
- **Loudness targets and platform compliance.** These matter at ship and are
  measured, not argued. Nothing to decide yet.

---

## Sources

- Middleware comparison for Unreal 5 projects, 2026:
  [Wwise vs FMOD vs MetaSounds, StraySpark](https://www.strayspark.studio/blog/wwise-fmod-metasounds-audio-middleware-comparison),
  [MetaSounds vs Wwise, Aircada](https://aircada.com/blog/metasounds-vs-wwise),
  [MetaSounds vs FMOD, Aircada](https://aircada.com/blog/metasounds-vs-fmod)
- Audio telegraphing and readability in action role-playing games:
  [Designing for Difficulty: Readability in ARPGs, Game Developer](https://www.gamedeveloper.com/game-platforms/designing-for-difficulty-readability-in-arpgs),
  [Sound design for Wolcen: Lords of Mayhem, Oliver Smith](https://www.oliversmithsound.com/blog/sound-design-for-an-arpg-wolcen-lords-of-mayhem)
- Positional audio for off-screen threats and accessibility:
  [Xbox Accessibility Guideline 103, Microsoft](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/103)

The two facts that decide the priority order are not from research. They are read
out of `Cataclysm_GDD_v2.md`: the lethality mode table, where Heretic hides the
heads-up display, and the wind-up cap in section X.
