# Source art for the seven vertical slice enemies

Which imported asset plays each enemy, and the animation durations that constrain
how each one can attack.

The decision to use Paragon character packs rather than generating models is
recorded in `docs/DECISIONS.md`, the log of design decisions, under the
2026-08-06 heading "The seven vertical slice enemies are cast from the free
Paragon character packs". This file is the lookup table that decision implies.

**Everything here was measured on 2026-08-07** from the assets in
`game/Content/`, by reading the `SequenceLength` and `Triangles` asset registry
tags through the editor. It is not an estimate.

## The packs

Six free Paragon packs, imported 2026-08-07, 17.31 GB total.

| Folder under `game/Content/` | Size | Assets |
|---|:-:|:-:|
| `ParagonMinions/` | 4.76 GB | 2,105 |
| `ParagonIggyScorch/` | 3.28 GB | 1,977 |
| `ParagonRampage/` | 3.25 GB | 1,751 |
| `ParagonGrux/` | 2.90 GB | 1,402 |
| `ParagonCountess/` | 1.78 GB | 2,727 |
| `ParagonSevarog/` | 1.35 GB | 1,067 |

## Which asset plays which enemy

| Enemy | Skeletal mesh | Triangles | Bones |
|---|---|:-:|:-:|
| The Imp | `Minion_Lane_Melee_Dawn` | 9,786 | 41 |
| The Corrupted Sentinel | `Minion_Lane_Siege_Dawn` | 10,379 | 39 |
| The Brute | `Rampage` | 52,174 | 207 |
| The Succubus | `SM_Countess` | 78,957 | 126 |
| The Abyssal Warden | `GruxMolten` | 82,225 | 96 |
| The Gatekeeper | `Sevarog` | 85,163 | 155 |
| The Hellhound | `IggyScorch`, the Scorch half | 92,562 | 198 |

## How big each one actually is

Reference-pose bounds read from each skeletal mesh through the editor, in
centimetres. **The width figures overstate the body**, because the reference pose
has the arms out: Rampage's 347 cm across X is its arm span, not its shoulders.
Height and the authored physics shapes are the trustworthy numbers.

| Enemy | Mesh height | Reference-pose X | Reference-pose Y |
|---|:-:|:-:|:-:|
| The Imp | 175.9 cm | 129.7 | 174.2 |
| The Corrupted Sentinel | 196.2 cm | 155.9 | 211.1 |
| The Succubus | 180.8 cm | 112.3 | 89.7 |
| The Hellhound | 212.5 cm | 81.0 | 225.2 |
| The Brute | 221.2 cm | 347.3 | 185.5 |
| The Abyssal Warden | 227.8 cm | 186.8 | 238.0 |
| The Gatekeeper | 311.1 cm | 206.9 | 307.1 |

**The Gatekeeper is 3.11 metres tall**, which is the design document's "towering"
as a measurement rather than an adjective. **The Imp's mesh is 1.76 metres tall**,
roughly a person, so playing a small swarming creature with it means scaling it
down rather than using it as authored.

### The Brute's authored collision

`Rampage_Extents`, the physics asset bound to the Rampage mesh, holds 11 bodies.
The relevant one:

| Bone | Shape | Radius |
|---|---|:-:|
| `spine_03` | Sphere | **82.1 cm** |

That is Epic's own answer to how wide the torso is, and it is 1.7 times the
0.48 m body radius the simulation assumes for every enemy except the Imp. It is
evidence for issue #366 and it is deliberately **not** used as the collision
capsule: see `game/Source/Cataclysm/Character/CataclysmBruteCharacter.h`, which
explains that an 82 cm capsule would put the Brute permanently outside its own
90 cm reach.

`GruxMolten` is a separate mesh, not a material applied to the base `Grux` mesh.
The base Grux is 46,920 triangles across 106 bones; the molten version is 82,225
across 96. Choosing the molten skin therefore changes the mesh, not just the
look. The pack also contains `GruxBeetleRed`, `GruxChestplate`, `GruxHalloween`,
`GruxQilin` and `GruxWarchief`.

Every character carries its own skeleton. Bone counts run from 39 to 207, so
nothing here shares an animation with anything else without retargeting.

## The timing constraint

Attack intervals come from `ARCHETYPES` in `sim/cataclysm_sim/enemy_stats.py`,
the file defining each enemy archetype's combat statistics. An enemy that attacks
every 0.9 seconds needs its whole attack to finish inside 0.9 seconds.

| Enemy | Interval | Shortest usable attack | Length | Verdict |
|---|:-:|---|:-:|---|
| Imp | 0.9 s | `Attack_A_SetA` | 0.80 s | Passes |
| Hellhound | 1.1 s | `Scorch_Primary_Fire_Med` | 0.97 s | Passes |
| Corrupted Sentinel | 2.0 s | `Fire_Planted` | 2.40 s | **Fails.** See #369 |
| Abyssal Warden | 2.4 s | `PrimaryAttack_LA` | 1.13 s | Passes |
| Succubus | 2.6 s | `Primary_Attack_Normal` | 0.90 s | Passes |
| Brute | 2.8 s | `Attack_Melee_A` | 0.97 s | Passes |
| Gatekeeper | 3.0 s | `Swing1_Medium` | 1.13 s | Passes |

**Wind-up durations are not in this file because they have not been measured.**
The attack telegraph rule in section X of `docs/Cataclysm_GDD_v2.md` caps the
wind-up at half the attack interval. `SequenceLength` gives the whole animation,
not the moment inside it when damage lands. Finding that needs the animation
notifies read per asset.

## How fast the locomotion animations were authored to move

A walking animation is authored for a character travelling at some speed. Play it
on a character that moves at a different speed and the planted foot slides. That
speed is not stored in the asset — none of these have root motion.

**The figure in use is set by eye, and that is deliberate.** The criterion is
whether a planted foot appears to slide, which is a judgement about what a person
sees. `tools/measure_animation_stride.py` produces an estimate to start from: it
samples the `ik_foot_l` and `ik_foot_r` bones every frame, takes whichever is
lower as the planted one, and averages how fast that foot travels backwards over
the gait cycle.

Measured and set 2026-08-07:

| Animation | Script estimate | In use | Note |
|---|:-:|:-:|---|
| `Idle_Biped` | 0.0 cm/s | — | The control. Standing still must read zero, and does. |
| `Jog_Biped_Fwd` | 242.9 cm/s | **225 cm/s** | What the Brute plays. Set by eye; the estimate agrees to 8%. |
| `Jog_Quad_Fwd` | 304.5 cm/s | **350 cm/s** | What the Brute plays while chasing. The all-fours stance. Set by eye; the estimate reads 15% low against it. |
| `Run_Fwd` | not measurable | — | Reads 0 on every axis: it does not key the IK foot bones, so the method cannot see it. |

The Brute moves at its designed 250 cm/s, so it plays `Jog_Biped_Fwd` at
250 ÷ 225 = **1.11**.

### Two gaits, chosen by what the brain is doing

The Brute plays `Jog_Biped_Fwd` while wandering and `Jog_Quad_Fwd` while chasing,
selected by brain state rather than by speed. It has to be by state: its movement
speed is the same designed 250 cm/s either way, so speed cannot tell the two
apart.

`Jog_Quad_Fwd` is the all-fours stance, and that is the point of choosing it: it
reads as having noticed the player through posture as well as pace.

**The Brute does speed up while chasing**, from 250 to 500 cm/s, which is a
designed figure in `sim/cataclysm_sim/enemy_stats.py` rather than an animation
one. So the chase animation plays at 500 ÷ 350 = **1.43**.

The 350 was set by eye on 2026-08-07. The measuring script says 304.5, and it
also said 242.9 for the walking animation where the by-eye answer was 225 — it
reads high on the walk and low here, which is within the roughly ten percent the
script's own documentation claims for itself.

**Two candidates were rejected, both measured on 2026-08-07.**

`Sprint_Biped_Fwd` returns bone poses identical to `Jog_Biped_Fwd` at every time
sampled, with the same 1.000 s length, the same 29 frames and the same 189
tracks. The pack realises a sprint by playing the jog faster rather than by
animating a second gait. `Sprint_Quad_Fwd` duplicates `Jog_Quad_Fwd` the same
way. Issue #386.

**Corroborated a second way on 2026-08-08**, because the first measurement can no
longer be repeated: it used
`unreal.AnimationBlueprintLibrary.get_bone_pose_for_frame`, and that class is not
exposed in this engine build — `tools/probe_brute_animation.py` reports it absent.
The independent check is file size. `Jog_Biped_Fwd.uasset` is 176,346 bytes and
`Sprint_Biped_Fwd.uasset` is 176,317, a difference of 29 bytes in 176 KB;
`Jog_Quad_Fwd.uasset` and `Sprint_Quad_Fwd.uasset` are 160,766 and 160,778, twelve
bytes apart. A difference that small is the asset name and its identifiers, not
different bone data.

**Measured directly on 2026-08-08 and confirmed.** `tools/compare_animation_clips.py`
evaluates both clips with `unreal.AnimPoseExtensions`, which this engine build
does expose, and compares the pelvis and both feet at 25 points through each clip.
That is a different method from the original one and from the file-size check.

| Pair | Largest difference on any bone at any sample | Verdict |
|---|--:|---|
| `Jog_Biped_Fwd` / `Sprint_Biped_Fwd` | 0.0000 cm | the same animation |
| `Jog_Biped_Bwd` / `Sprint_Biped_Bwd` | 0.0000 cm | the same animation |
| `Jog_Biped_Lft` / `Sprint_Biped_Lft` | 0.0000 cm | the same animation |
| `Jog_Biped_Rt` / `Sprint_Biped_Rt` | 0.0000 cm | the same animation |
| `Jog_Quad_Fwd` / `Sprint_Quad_Fwd` | 0.0000 cm | the same animation |
| `Jog_Quad_Bwd` / `Sprint_Quad_Bwd` | 2.3169 cm | **not identical** |
| `Jog_Quad_Lft` / `Sprint_Quad_Lft` | 1.2927 cm | **not identical** |
| `Jog_Quad_Rt` / `Sprint_Quad_Rt` | 1.8512 cm | **not identical** |
| `Jog_Biped_Fwd` / `Jog_Quad_Fwd` (control) | 149.4755 cm | different animations |

**The control is what makes the zeros mean anything.** Comparing the biped jog
against the quadruped jog gives 149 cm, so the method resolves clips apart rather
than returning the same thing for everything.

**Three quadruped pairs are not identical, and that is new.** They differ by one
to two centimetres at a foot, over a clip 0.53 seconds long. That is far too small
to be a different gait — a stride carries a foot through more than a metre — but it
is not zero, so the flat statement that every `Sprint_*` clip duplicates its jog
is not quite true. It holds exactly for all four biped directions and for the
quadruped forward clip, which are the ones that matter, because forward is the
only direction either gait is used in.

The by-eye check in the editor is no longer needed for the forward clips. Nothing
turns on the three sideways and backward quadruped pairs.

**What it means for the animation Blueprint, issue #387.** A locomotion blend
space cannot have a walk axis and a run axis, because there is only one gait per
stance. The two real choices the pack offers are the biped jog and the quadruped
jog, which is what the current C++ already selects between by brain state.

`Run_Fwd` is genuinely distinct — 0.667 s, 20 frames, 47 tracks against 189 —
but it carries no `ik_foot_l` track, which is the measured reason the script
reads zero for it. Its play rate would be a guess, and it looked like running on
the spot.

Both chase figures can be retuned live: `Cataclysm.Brute.ChaseAnimation` takes an
asset path and `Cataclysm.Brute.AuthoredChaseSpeed` takes the speed that clip was
authored for.

To change it without a rebuild, use the console variable
`Cataclysm.Brute.AuthoredWalkSpeed` during a play session. Zero returns to the
value above.

### Two earlier values were wrong, and how is worth keeping

**500 cm/s** was an outright guess. It made the play rate 0.50, so the animation
ran at half speed while the body moved at full speed, and the planted foot slid
forwards while the other leg swung.

**373.7 cm/s** came from the measuring script when it averaged only the top
quartile of its samples, on the reasoning that those were the frames where a foot
was genuinely planted. That measures the peak of the foot's velocity curve rather
than a representative speed, and was 66% high. The project owner tuning by eye
found 225, which is what exposed it. The script now averages the whole cycle and
skips the frame where the tracked foot swaps, which brought it to 242.9.

**The IK foot bones never touch the ground** — on Rampage they stay 20 cm or more
above it — so "the lower foot" is an approximation of "the planted foot". Treat
the script's output as a starting estimate good to roughly ten percent, not an
answer.

**Forward is −Y in these animations**, which is why the Brute's mesh component
carries a −90 degree yaw to face the way its actor faces. A first version of the
measurement assumed forward was X and reported a nonsense 38 cm/s jog; the
symptom was a negative median with symmetric extremes, which is what measuring a
side-to-side axis looks like.

The same script and reasoning apply to the other six enemies when their
locomotion is built.

## Per-enemy animation inventory

Durations in seconds. Only attack-relevant animations are listed; each character
also has locomotion, hit reactions, deaths and stuns.

### The Imp — melee lane minion

Under `ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee/`.

| Animation | Length |
|---|:-:|
| `Attack_A_SetA` .. `Attack_E_SetA` | 0.80 each |
| `Attack_A_SetB` .. `Attack_E_SetB` | 0.83 each |
| `Attack_A` .. `Attack_D` | 1.00 each |

The ten `_SetA` and `_SetB` variants fit the 0.9 s interval. The four unsuffixed
attacks do not, and should not be used for the basic attack.

### The Corrupted Sentinel — siege lane minion

Under `ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege/`.

| Animation | Length | Note |
|---|:-:|---|
| `PlantedIntro` | 0.63 | Roots itself in place |
| `Idle_Planted` | — | Looping rooted idle |
| `Fire_Planted` | 2.40 | Rooted attack. Too slow for a 2.0 s interval |
| `Fire_Planted_B` | 2.40 | Second rooted attack |
| `PlantedExit` | 0.50 | Unroots |
| `Fire_A`, `Fire_B`, `Fire_C` | 2.80 | Unrooted, slower still |
| `MeleeAttack_A` | 2.40 | Close-range fallback |

It also carries `HitReact_Front_Planted`, `HitReact_Back_Planted`,
`HitReact_Left_Planted` and `HitReact_Right_Planted`, so it can take hits without
leaving the rooted state. This is the only Paragon character that roots itself to
attack.

### The Brute — Rampage

| Animation | Length | Note |
|---|:-:|---|
| `Attack_Melee_A`, `_B`, `_C` | 0.97 | Basic attacks |
| `Attack_Biped_Melee_A`, `_B`, `_C` | 1.00 | Upright variants |
| `Ability_GroundSmash_Start` | 0.83 | Stomp wind-up |
| `Ability_GroundSmash_Loop` | 0.03 | Holds the wind-up open |
| `Ability_GroundSmash_End` | 0.70 | Stomp release |
| `Ability_RMB_Smash` | 1.17 | Heavier single smash |
| `Ability_Enrage_Start` / `_End` | 1.27 each | |
| `Idle_Biped` | 4.33 | Standing |
| `Jog_Biped_Fwd` | 1.00 | The walk |
| `Sprint_Biped_Fwd` | 1.00 | Not a second gait. Measured identical to the jog to 0.0000 cm; see below |
| `Jog_Quad_Fwd` | 0.53 | The chase, on all fours |
| `Sprint_Quad_Fwd` | 0.53 | Not a second gait either. Measured identical to 0.0000 cm; see below |

**Rip and Toss**, the thrown rock. Measured 2026-08-08 and never recorded here
before: issue #382 listed these figures and stated in terms that they had not
been re-measured. They are now, and every one matches what #382 quoted.

| Animation | Length | Note |
|---|:-:|---|
| `Ability_RipNToss_Rip` | 1.13 | Tears the rock out of the ground |
| `Ability_RipNToss_Idle` | 7.67 | Standing while holding it |
| `Ability_RipNToss_Toss` | 0.87 | The throw. The throwing hand reaches the top of its arc, which is where the rock is released, at **0.539 s** |
| `Ability_RipNToss_Toss_Enraged` | 0.87 | A second throw clip. Its throwing hand reaches the top of its arc at **0.539 s** as well |
| `Ability_RipNToss_Cancel` | 0.47 | Drops it without throwing |

**The enraged throw releases at exactly the same moment as the ordinary one.**
Measured 2026-08-08 with `tools/measure_animation_impact.py`, which now covers
both. Issue #416 lists switching to this clip as one of four ways to stop the
throw montage being compressed; this measurement rules that one out. The two
clips are not identical -- the left hand does something different, reaching 4 cm
at 0.173 s against the ordinary clip's 40 cm at 0.067 s -- but the right hand,
which is the one holding the rock, follows the same arc to the same peak at the
same time.

**How much the throw montage is compressed, and why.** The rock does not leave
the hand until 1.133 + 0.539 = **1.672 s** into the montage, and
`ACataclysmBruteCharacter::RockThrowWindUpSeconds` is **1.000 s**. So
`MontageRateFor` compresses the whole rip and throw to a play rate of **1.672**,
running it in 1.196 s rather than the authored 2.000 s. That is inside the 2.5
ceiling the class treats as usable, so nothing rejects it. Whether it reads as
hurried on a creature designed as "heavily armoured slow melee" has not been
judged by playing it. Issue #416.

The pack also ships the rock itself and the mess it makes. One of the four rows
below is in use.

| Asset | Folder under `game/Content/ParagonRampage/` | State |
|---|---|---|
| `SM_Rock_To_Hold` | `Characters/Heroes/Rampage/Meshes/Rocks/` | **In use.** The throw flies it. `ACataclysmBruteCharacter::RockMeshPath` names it and hands it to `ACataclysmProjectile::Fire`. Issue #404 |
| `M_Rock_To_Throw` | `Characters/Heroes/Rampage/Materials/Rocks/` | In use without being named anywhere: a static mesh carries its own material slots |
| `SM_Rampage_Rock_Rip_Crater` | `FX/Meshes/Debris/` | Not used. The hole left where the rock was torn out. Issue #421, with the carry state |
| `SM_Rampage_Rock_FragA` to `FragE` | `FX/Meshes/Debris/` | Not used. Five fragments for the rock breaking on impact. Issue #422 |

The rock is scaled to the projectile's own body width rather than shown at its
authored size, so what is drawn and what the sweep hits are one thing rather than
two numbers that can disagree. That is why it is not boulder-sized in flight.

**The rip clip is longer than the telegraph it plays inside.** The throw's
wind-up is 1.0 s and the clip is 1.13 s, so at authored speed it is cut off
before the rock comes free. `ACataclysmBruteCharacter::PlayOneShot` compresses it
to fit. That is the whole reason that function speeds a clip up but never slows
one down.

The ground smash splitting into start, loop and end matters for #351, the Brute's
ability design, which makes the stomp the only attack that stuns. A telegraph
needs a wind-up that can be held open for a variable time, and the loop does
that. The 0.83 s start fits inside the Brute's 1.4 s wind-up budget.

**Nothing uses the loop yet.** The C++ holds the last frame of the start clip
instead, because `EAnimationMode::AnimationSingleNode` plays one clip at a time
and switching to the loop mid-telegraph would add a second hard cut to the one
that already exists between the wind-up and the release. Playing start, then
loop, then end as one movement is the animation Blueprint's job, issue #387.

### How these were measured

`tools/probe_brute_animation.py`, run through `tools/run_editor_python.py`. It
reads `get_play_length()` on each asset inside the editor and prints it. Re-run it
after importing or replacing any of these:

```
python tools/run_editor_python.py tools/probe_brute_animation.py
```

It changes nothing. The results land in `game/Saved/Logs/run_editor_python.log`,
on the lines beginning `PROBE|`.

### The Succubus — Countess

| Animation | Length | Note |
|---|:-:|---|
| `Primary_Attack_Fast_V1` | 0.60 | |
| `Primary_Attack_Normal` | 0.90 | |
| `Primary_Attack_Slow` | 1.50 | |
| `Primary_Attack_Slow_Recovery` | 1.33 | |
| `Ability_Q` | 0.97 | |
| `Ability_E` | 1.17 | |
| `Ability_RMB` | 1.33 | |
| `Ability_Ultimate` | 3.17 | Exceeds the 2.6 s interval; not a basic attack |
| `Cast` | 1.13 | |

Three speeds of the same primary attack give room to tune without editing
animation.

### The Hellhound — Scorch

Scorch's own animations are prefixed `Scorch_`. The rest of the pack is Iggy.

| Animation | Length | Note |
|---|:-:|---|
| `Scorch_Primary_Fire_Fast` | 0.53 | |
| `Scorch_Primary_Fire_Med` | 0.97 | Fits the 1.1 s interval |
| `R_Ability_FireBreath_Start` | 1.13 | |
| `R_Ability_FireBreath_Loop` | 3.10 | |
| `R_Ability_FireBreath_End` | 1.77 | |
| `R_Ability_Fire` | 6.00 | Full uninterrupted breath |

The start, loop and end breath is the closest existing motion to the fire trail
the design gives this enemy.

### The Abyssal Warden — Grux, molten

| Animation | Length | Note |
|---|:-:|---|
| `PrimaryAttack_LA_Fast` / `RA_Fast` | 0.63 | |
| `PrimaryAttack_RB` | 0.87 | |
| `PrimaryAttack_LB` | 1.07 | |
| `PrimaryAttack_LA` / `RA` | 1.13 | |
| `PrimaryAttack_LA_Recovery` | 0.83 | |
| `PrimaryAttack_FourStrikes` | 3.70 | Exceeds the 2.4 s interval; use as a special |
| `PrimaryAttack_Start` | 0.57 | |

### The Gatekeeper — Sevarog

| Animation | Length | Note |
|---|:-:|---|
| `Swing1_FAST_v2` .. `Swing3_FAST_v2` | 0.53 | |
| `Swing1_120fps` .. `Swing3_120fps` | 1.00 | |
| `Swing1_Medium` .. `Swing3_Medium` | 1.13 | |
| `Swing1_Slow` .. `Swing3_Slow` | 1.70 | |
| `Ultimate_Targeting` | 0.87 | |
| `Ultimate_Targeting_Loop` | 2.23 | Holds while aiming |
| `Ultimate_Swing_120fps` | 2.63 | |
| `Soul_Siphon` | 1.83 | |
| `Subjugation` | 2.87 | |
| `Knock_back` | 2.67 | |
| `Stage_1` .. `Stage_4` | 0.03 each | Marker poses, not animations |

Three swing chains at four speeds each give a boss enough distinct motion to read
as having phases. The targeting animation splitting into a start and a hold is
the same shape the Brute's stomp has, and is what a telegraph needs.

`Stage_1` through `Stage_4` are 0.03 second poses. Whether they are usable as the
boss's four phases has not been checked. #354 designs those phases.
