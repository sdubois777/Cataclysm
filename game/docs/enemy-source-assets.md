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

**The interval is the designed number and the animation is played to fit it**,
not the other way round. A clip longer than its interval is played faster, at
`clip length ÷ attack interval`. A clip shorter than its interval is played at
the speed it was authored at and the creature waits out the remainder; it is
never slowed down to fill the gap. That is not a rule invented for this table —
`ACataclysmBruteCharacter::PlayOneShot` and `MontageRateFor` already compute
exactly `FMath::Clamp(FMath::Max(1.0f, Length / Hold), MinimumPlayRate,
MaximumPlayRate)`. Issue #369 settled that it is the rule for every enemy;
`docs/DECISIONS.md` carries the reasoning and the shipped games it was checked
against, under the 2026-08-09 heading "An enemy's attack animation is played to
fit its designed attack interval".

**The play rate has a ceiling of 2.50.** That is `MaximumPlayRate` in
`game/Source/Cataclysm/Character/CataclysmBruteCharacter.h`, and it is a hard
limit rather than an opinion about how fast a clip may look: the engine clamps to
it, so a clip needing more than 2.50 is still longer than its interval after
being sped up, and the creature starts an attack it has not finished.

| Enemy | Interval | Shortest usable attack | Length | Play rate | Verdict |
|---|:-:|---|:-:|:-:|---|
| Imp | 0.9 s | `Attack_A_SetA` | 0.80 s | 1.00 | Passes as authored |
| Hellhound | 1.1 s | `Scorch_Primary_Fire_Med` | 0.97 s | 1.00 | Passes as authored |
| Brute | 1.2 s | `Attack_Melee_A` | 0.97 s | 1.00 | Passes as authored, by 0.23 s |
| Corrupted Sentinel | 2.0 s | `Fire_Planted` | 2.40 s | **1.20** | Passes, sped up |
| Abyssal Warden | 2.4 s | `PrimaryAttack_LA` | 1.13 s | 1.00 | Passes as authored |
| Succubus | 2.6 s | `Primary_Attack_Normal` | 0.90 s | 1.00 | Passes as authored |
| Gatekeeper | 3.0 s | `Swing1_Medium` | 1.13 s | 1.00 | Passes as authored |

**The Corrupted Sentinel is the only one of the seven that needs any compression,
and 1.20 is the gentlest rate scaling in the project.** Its two rooted firing
clips are both 2.40 seconds against a 2.0 second interval, and the pack has
nothing shorter — its unrooted alternatives are 2.80. For comparison the Brute
plays its walk at 1.11, its chase at 1.43 and its rip-and-throw montage at 1.67.

**At 1.20 there is no gap between one shot and the next.** 2.40 ÷ 1.20 is exactly
2.00, so the clip finishes as the next one starts. The pack ships two rooted
firing clips, `Fire_Planted` and `Fire_Planted_B`, and alternating them is what
stops continuous fire reading as one clip looping. The Brute's fifth of a second
of gap is not a designed margin to match: its clip happens to be 1.0 seconds
against a 1.2 second interval.

**Where the shot leaves the barrel inside `Fire_Planted` has not been measured**,
so nothing yet lines the release up with the end of the telegraph's wind-up. That
is the same gap this document records for every enemy below, and for the Sentinel
it is issue #478.

**The clip the Brute actually plays is not the shortest one, and it is longer
than this table's figure.** The column above records the shortest usable attack
for each enemy, which is what decides whether the interval is achievable at all.
`ACataclysmBruteCharacter::AttackAnimationPath` names
`Attack_Biped_Melee_A`, measured at **1.0000 seconds** in the editor on
2026-08-09. Nothing rate-scales it: `PlayAttackAnimation` passes no window, so it
runs at its authored speed. Against the 1.2 second interval that leaves a fifth
of a second between one swing ending and the next starting, and it is the reason
the interval must not go below 1.0.

## When each ordinary attack actually strikes

Measured 2026-08-21 by `tools/measure_attack_impact.py` for issue
[#526](https://github.com/sdubois777/Cataclysm/issues/526), which replaces the
paragraph that used to sit here saying these had never been measured.

**NOT ONE OF THE THIRTEEN CLIPS CARRIES AN ANIMATION NOTIFY.** Issue #526 asked
for the authored notify where there was one and inspection where there was not.
`tools/probe_attack_impact_sources.py` read every clip through
`unreal.AnimationLibrary.get_animation_notify_events` and every one came back
empty, each with a single empty notify track named `1`. The Paragon packs were
authored for a different game and carry no damage markers, **so every figure
below is from inspection and none is from a notify.**

`clip.get_editor_property("notifies")` does not work at all:
`UAnimSequenceBase::Notifies` is protected and Python refuses it for every clip.
`unreal.AnimationLibrary` is the route that works.

**THREE RULES, AND THEIR AGREEMENT IS THE EVIDENCE.** Choosing one definition of
"the moment it strikes" and trusting it is how the stride measurement came to
report 0.0 cm/s for three walking clips — it followed one bone that looked right
and was never driven, which is issue
[#778](https://github.com/sdubois777/Cataclysm/issues/778). So three independent
answers are computed for each clip: the sample where the striking bone moves
fastest, the sample where it is furthest from the pelvis, and the sample where it
is nearest the ground. Where they agree the answer is solid; where they do not,
the clip is left unmeasured rather than guessed at.

### The control, and one creature fails it

**AGREEMENT BETWEEN THE THREE RULES IS NOT ENOUGH ON ITS OWN.** Three rules that
all read hand motion will agree with each other on a clip where the hand simply
moves, whether or not anything is being struck. So the same measurement is run
over a clip on the same rig that strikes nothing — an idle — and the attack has
to beat it.

That control is not new. `tools/measure_sentinel_release.py` established on
2026-08-20 that hand and muzzle motion cannot say when the Corrupted Sentinel
fires: two methods were tried and `PlantedIntro`, which fires nothing, read at
least as strongly as `Fire_Planted`.

| Creature | Attack peak speed | Control clip | Control peak speed | Margin |
|---|:-:|---|:-:|:-:|
| Brute | 4717 cm/s | `Idle` | 4 cm/s | 1179× |
| Gatekeeper | 5973 cm/s | `Idle` | 20 cm/s | 299× |
| Imp | 2766–4238 cm/s | `NonCombat_Idle` | 36 cm/s | 77× |
| Abyssal Warden | 4466–4837 cm/s | `Idle` | 89 cm/s | 50× |
| Succubus | 4914 cm/s | `Idle_Relaxed` | 201 cm/s | 24× |
| Hellhound | 264 cm/s | `IggyScorch_Idle` | 72 cm/s | **3.7×, too close to trust** |
| **Corrupted Sentinel** | **526 cm/s** | `PlantedIntro` | **674 cm/s** | **FAILS: the clip that fires nothing moves faster** |

**The Corrupted Sentinel cannot be measured this way and no figure for it is
recorded below.** It is planted, so its hands barely move, and the intro clip
that roots it moves them more than the firing clip does. That is the third method
to fail on this creature; issue
[#478](https://github.com/sdubois777/Cataclysm/issues/478) carries the other two.

### The measurements

| Creature | Clip | Strikes at | Method |
|---|---|:-:|---|
| Brute | `Attack_Biped_Melee_A` | **0.331 s** | inspection; peak speed and lowest point agree, control beaten 1179× |
| Imp | `Attack_A_SetA` | **0.242 s** | inspection; peak speed and furthest reach agree |
| Imp | `Attack_B_SetA` | **not measured** | the three rules disagree by 0.167 s |
| Imp | `Attack_C_SetA` | **not measured** | the three rules disagree by 0.330 s |
| Imp | `Attack_D_SetA` | **0.278 s** | inspection; peak speed and furthest reach agree |
| Imp | `Attack_E_SetA` | **0.308 s** | inspection; peak speed and furthest reach agree |
| Hellhound | `Scorch_Primary_Fire_Med` | **not measured** | the three rules disagree by 0.338 s, and it beats its control only 3.7× |
| Abyssal Warden | `PrimaryAttack_LA_Fast` | **0.176 s** | inspection; peak speed and furthest reach agree |
| Abyssal Warden | `PrimaryAttack_RA_Fast` | **0.168 s** | inspection; peak speed and furthest reach agree |
| Corrupted Sentinel | `Fire_Planted` | **not measured** | the three rules disagree by 1.040 s |
| Corrupted Sentinel | `Fire_Planted_B` | **not measured** | the rules agreed, **but the creature fails its control** and the agreement is worthless |
| Succubus | `Primary_Attack_Normal` | **0.156 s** | inspection; peak speed and furthest reach agree, `hand_l` peaks at the same moment, control beaten 24× |
| Gatekeeper | `Swing1_Medium` | **0.282 s** | inspection; **all three rules agree within 0.057 s**, control beaten 299× |

Eight of the thirteen are measured. **Five are not, and a number was not invented
for them.**

**`Fire_Planted_B` WAS PUBLISHED AS 1.755 s AND THAT WAS WITHDRAWN THE SAME DAY.**
Its three rules agreed, which is why it was published; the control was added
afterwards and the creature fails it, so the agreement was two rules agreeing
about ordinary movement rather than about a shot. It is recorded here because a
withdrawn figure that leaves no trace gets published again.

### What that means in play

**Two of the five are fixed and three are not.**

| Creature | Play rate | Strike in play | Damage lands at | Gap |
|---|:-:|:-:|:-:|---|
| Gatekeeper | 1.00, after waiting 0.689 s | 0.971 s | 0.971 s, the wind-up's end | **none** |
| Succubus | 1.00, after waiting 1.144 s | 1.300 s | 1.300 s, the wind-up's end | **none** |
| Brute | 1.00 | 0.331 s | 0 s, before the clip starts | damage **0.33 s early** |
| Imp | 1.00 | 0.242 to 0.308 s | 0 s | damage **0.24 to 0.31 s early** |
| Abyssal Warden | 1.00 | 0.168 to 0.176 s | 0 s | damage **0.17 s early** |

**The two that are fixed wait rather than speed up.** Their clips reach the blow
sooner than the damage arrives, so the clip is started later instead of being
stretched — stretching one to fill a window was tried on the Brute and reported
from a play session as slow motion. The creature stands in its ordinary idle,
which moves, and then performs the whole attack as one movement.

`ACataclysmEnemyCharacter::StrikeAlignedPlayRate` and `StrikeAlignedDelaySeconds`
are the shared rule. The Brute has used it for its two abilities since
2026-08-08 and it moved to the base on 2026-08-21 so these two could use it
rather than a second copy of it. Issue
[#784](https://github.com/sdubois777/Cataclysm/issues/784).

**The three that are not fixed have a different cause**, which is why they are a
different issue: their ordinary attack is not telegraphed at all, so there is no
window to start a clip inside. `AttackTarget` applies the damage and then starts
the clip, so the hit always lands before the blow. Issue
[#783](https://github.com/sdubois777/Cataclysm/issues/783), and it needs a
decision about whether a delayed hit should still land if the creature dies
between the swing and the blow.

**And two creatures have no answer at all**, because their strike moment could
not be measured: the Hellhound, and the Corrupted Sentinel, which is the only one
of the seven that fails the control outright.

**Re-run the measurement after importing or replacing any of these:**

```
python tools/run_editor_python.py tools/probe_attack_impact_sources.py
python tools/run_editor_python.py tools/measure_attack_impact.py
```

Both change nothing. The results land in
`game/Saved/Logs/run_editor_python.log`, on lines beginning `PROBE|` and
`IMPACT|`. **Read the control section at the end of the second one before using
any figure from it.**

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

**Measured 2026-08-20** with `tools/probe_imp_animation.py` and
`tools/measure_animation_stride.py`, when the creature was built. Every length
recorded here on 2026-08-07 was read again and matched; everything else in this
section is new.

| Animation | Length | Note |
|---|:-:|---|
| `Attack_A_SetA` .. `Attack_E_SetA` | 0.80 each | **The five the creature uses.** All fit the 0.9 s interval as authored |
| `Attack_A_SetB` .. `Attack_E_SetB` | 0.83 each | Also fit. Unused, so that all five drawn clips are the same length |
| `Attack_A` .. `Attack_D` | 1.00 each | Longer than the interval. **Not usable** for the basic attack |
| `NonCombat_Idle` | 10.17 | Standing. **The only idle in the folder** — there is no combat idle to prefer |
| `NonCombat_JogFwd_B` | 1.50 | **The walk the creature wears.** See the speeds below |
| `NonCombat_JogFwd`, `_A` | 2.03 each | |
| `Combat_JogFwd`, `_AggroMinion` | 1.80 each | |
| `Combat_JogFwd_Start` | 0.37 | Leads into the combat walk |
| `Death_A` | 2.13 | |
| `Death_B` | 0.50 | |
| `Death_C` | 0.60 | |
| `Death_D` | 0.51 | |
| `Death_E` | 0.64 | |
| `HitReact_Front`, `_Back`, `_Left`, `_Right` | 0.67 each | Nothing plays these yet |
| `Stun` | 3.67 | |
| `KnockUp`, `KnockUp_A` | 2.00 each | |

**Five deaths, which is more than any other creature in the project has.** The
Brute ships one, the Abyssal Warden and the Hellhound two.
`ACataclysmEnemyCharacter::PlayDeathAnimation` draws one of however many it is
given, and this is the creature that dies ten at a time. All five are inside
`UCataclysmEnemyDeath::LongestCorpseSeconds`, which is 4.0.

#### The walk, and why it wears a clip named for not being in combat

Authored ground speeds, measured from the planted foot:

| Animation | Authored speed | Play rate for 650 cm/s | Verdict |
|---|:-:|:-:|---|
| `NonCombat_JogFwd_B` | **382.6 cm/s** | **1.699** | Used |
| `NonCombat_JogFwd`, `_A` | 277.9 cm/s | 2.339 | Fits, slower |
| `Combat_JogFwd`, `_AggroMinion` | 241.1 cm/s | 2.696 | **Above the 2.50 ceiling** |
| `NonCombat_Idle` | 0.0 cm/s | — | The control |

So the combat walk cannot be worn by a creature designed to move at 6.5 metres
per second, and the fastest clip in the pack is one of the non-combat variants.

**This rig is the one that found the stride measurement was lying.**
`Minion_Lane_Core_Skeleton` animates 69 bones and **not one of them is an `ik_`
bone**, and `tools/measure_animation_stride.py` read the planted foot through
Epic's `ik_foot_l` and `ik_foot_r`. A bone the skeleton does not have returns an
identity transform rather than raising, so both walks and the idle all measured
0.0 cm/s and the walks looked like idles. The tool now picks the rig from the
bones a clip really drives — this one is tracked through `foot_l` and `foot_r` up
the leg — and refuses out loud when it recognises none.

#### How wide it actually is

The reference-pose bounds say 174.2 cm across, and that is an arm span. Measured
from the skeleton instead:

| Measurement | Value |
|---|:-:|
| Shoulder to shoulder | **63.5 cm** |
| Hip to hip | 16.7 cm |
| Mesh height | 175.9 cm |

**The mesh at its authored size already is the width the design specifies.** The
Imp's designed body radius is 0.30 m, so the creature is 60 cm across, and the
shoulders are 63.5. That is why `ACataclysmImpCharacter::ImpMeshScale` is 1 and
why the note above — that a small swarming creature means scaling this mesh down
— was not followed: scaling it down would make the body narrower than its own
collision, and it could not be paid for anyway, because a smaller mesh takes a
shorter stride and needs a higher play rate. At 90 cm tall the walk would need
3.32 against a ceiling of 2.50. Issue
[#760](https://github.com/sdubois777/Cataclysm/issues/760) is whether the
resulting person-sized imp reads as a swarm.

The mesh has **one material slot**, `M00_Dawn_Melee`, so there is nothing on it
to hide or recolour by section.

### The Corrupted Sentinel — siege lane minion

Under `ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege/`.

**Re-measured 2026-08-09** with `tools/probe_sentinel_animation.py`, because
issue #369 derives a play rate from these figures and a derived number is only as
good as the measurement under it. Every length below was read again from the
asset and every one matched what was written here on 2026-08-07, except
`Idle_Planted`, which had no figure at all.

| Animation | Length | Note |
|---|:-:|---|
| `Idle_Planted` | 0.03 | Rooted idle. **A single pose, not a loop** — one frame at 30 fps, the same shape as Rampage's `Ability_GroundSmash_Loop` |
| `PlantedExit` | 0.50 | Unroots |
| `PlantedIntro` | 0.63 | Roots itself in place |
| `Fire_Planted` | 2.40 | Rooted attack. Played at **1.20** to fit the 2.0 s interval. #369 |
| `Fire_Planted_B` | 2.40 | Second rooted attack. Same length, so the same 1.20 |
| `MeleeAttack_A` | 2.40 | Close-range fallback |
| `Fire_A`, `Fire_B`, `Fire_C` | 2.80 | Unrooted, slower still. Would need 1.40 |

It also carries `HitReact_Front_Planted`, `HitReact_Back_Planted`,
`HitReact_Left_Planted` and `HitReact_Right_Planted`, **0.80 seconds each**, so it
can take hits without leaving the rooted state. This is the only Paragon character
that roots itself to attack.

**The rooted idle being one frame is what makes the rooted state cheap to hold.**
A telegraph needs a wind-up that can be held open for a variable time, and a
single pose does that with no seam. Rampage's 0.03 second
`Ability_GroundSmash_Loop` exists for the same purpose and the Brute's C++ notes
say so.

**Measured again on 2026-08-20** when the creature was built. Every length above
was read a third time and every one matched. Everything below is new.

| Animation | Length | Note |
|---|:-:|---|
| `Idle` | 7.40 | The STANDING idle. This creature never stands up, so nothing plays it |
| `Death_A` | 0.63 | |
| `Death_B` | 0.47 | |
| `Death_C` | 0.47 | |
| `Death_D` | 0.43 | |
| `Death_E` | 0.50 | |
| `Death_F` | 0.47 | |
| `Death_G` | 0.67 | |
| `Death_H` | 0.77 | |

**Eight deaths, which is the most in the project.** The Brute ships one, the
Abyssal Warden and the Hellhound two, the Imp five.
`ACataclysmEnemyCharacter::PlayDeathAnimation` draws one of however many it is
given. All eight are inside `UCataclysmEnemyDeath::LongestCorpseSeconds`, which
is 4.0, with more than three seconds to spare.

The mesh, `Minion_Lane_Siege_Dawn`, measures **196.2 cm tall** with reference-pose
bounds of 155.9 across X and 211.1 across Y, and carries **one material slot** on
the skeleton `Minion_Lane_Siege_Skeleton`. It is worn at its authored size:
`ACataclysmCorruptedSentinelCharacter::SentinelCapsuleHalfHeight` is 98.1.

#### When the shot leaves the weapon is still unknown

Issue [#478](https://github.com/sdubois777/Cataclysm/issues/478) asks for the
release moment inside `Fire_Planted`, so the telegraph and the shot can be lined
up the way `ACataclysmBruteCharacter::RockThrowStrikeIntoReleaseSeconds` lines up
the thrown rock. **Two ways of measuring it have been tried and both failed their
control**, so there is still no figure. `tools/measure_sentinel_release.py` is the
script, and it reports no release moment rather than a number nobody should use.

| Method | `Fire_Planted` | `PlantedIntro`, which fires nothing |
|---|:-:|:-:|
| Largest muzzle acceleration | 485 cm/s/s | **502 cm/s/s** |
| Slide furthest from the gun body | 55.0 cm | **102.8 cm** |

In both cases the clip with no shot in it reads at least as hard as the clip with
one. The likely reason is the warning this file already gives about Rampage: a
Paragon rig's weapon bones may be props the animator drives for effect rather
than parts of a working weapon. **Of the four `gun_` and `weapon_gun_` bones the
melee lane minion carries, this skeleton's firing clips drive only `gun_slide`**;
`weapon_gun_r`, `gun_foregrip` and `gun_stock_retractor` are not animated at all.

Until it is measured the creature fires at the end of its wind-up, which is what
every other telegraphed ability in the project does. What is lost is only the
guarantee that the visible muzzle flash agrees with it.

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
| `Death_A` | 0.7667 | Dying. **The only death clip the pack ships**, so this creature always falls the same way. Measured 2026-08-19 for issue #522 |
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
| `SM_Rampage_Rock_FragA` to `FragE` | `FX/Meshes/Debris/` | **In use.** The thrown rock breaks into them where it stops. Issue #422 |

**Where the rock goes in the creature's hand, measured 2026-08-08.** The Rampage
mesh has **no sockets at all** -- `find_socket` answers null for every name
tried, including `weapon_r`, `hand_r` and `RockSocket`. So a held prop attaches
to a bone.

**The bone is `hand_r`. It was `weapon_r` until issue #470, and that was wrong.**

`weapon_r` is the rig's prop bone, and through these clips the animator drives it
as **the rock itself** rather than as the hand. It sits at ground level during
`Ability_RipNToss_Rip`, which is the rock being torn out of the floor, and during
`Ability_RipNToss_Toss` it is flung away from the creature along the throw's own
path. Measured by `tools/measure_rock_launch_point.py` on 2026-08-09 it reaches
**1253 cm** from the creature's root. `hand_r` never exceeds **255 cm**.

| Clip | Moment | `hand_r` | `weapon_r` | Apart |
|---|---|---|---|---|
| `Idle_Biped` | start | (-90.5, -17.8, 69.3) | (-92.3, -18.0, 70.1) | 1.8 cm |
| `Ability_RipNToss_Idle` | start | (-88.0, -77.1, 76.6) | (-67.7, -31.0, 40.5) | 60 cm |
| `Ability_RipNToss_Toss` | 0.433 s | (-60.5, 81.4, 209.6) | (-45.2, 190.8, 211.1) | 110 cm |
| `Ability_RipNToss_Toss` | **0.539 s, the release** | (-16, 89, 238) | (27, 668, 455) | **620 cm** |
| `Ability_RipNToss_Toss` | 0.607 s | (-11.8, 96.3, 217.8) | (157.0, 1151.8, 468.6) | 1098 cm |

**The 0.433 second row is what misled this document.** It was the only sample
taken through the toss, it was labelled "half way", and at that moment the two
bones differ by about a metre, which reads as an ordinary prop offset. They keep
separating for another fifth of a second, and
`ACataclysmBruteCharacter::RockThrowStrikeIntoReleaseSeconds` puts the release at
0.539 seconds, after that. Launching from `weapon_r` therefore put the rock
**6.68 metres in front of the creature and 4.55 metres above its feet**, so a
throw at anything nearer than about 6.6 metres travelled BACKWARDS towards its
target. The rock held in the hand hangs off the same bone, so it was flung the
same way and the player watched it go.

Forward is +Y here, so the second number in each triple is the one that matters
and the third is height above the feet.

`hand_r` works for both halves of the animation. It reaches the floor during the
rip -- 2.9 cm above the root at 0.264 seconds -- so the rock still reads as being
torn out of the ground, and it stays with the body through the throw.

The carried rock still needs no offset and no scale: where it sits is authored in
the animation. Issue #421 expected that to be a judgement somebody had to make by
eye, and the measurement removed it.

**The skeleton also carries eleven `rock_spikes_*` bones.** They are body
armour on the creature's arms, not the thrown rock, and nothing uses them.

**The five fragments carry no material, and that is a trap.** Measured
2026-08-08: every one of `SM_Rampage_Rock_FragA` through `FragE` has
`/Engine/EngineMaterials/WorldGridMaterial` assigned, which is the engine's grey
checkerboard placeholder. So does `SM_Rampage_Rock_Rip_Crater`. Spawning any of
them as they come puts large checkered lumps on the floor, which is worse than
nothing appearing at all.

`ACataclysmDebrisBurst::Scatter` therefore takes the material as an argument, and
`ACataclysmBruteCharacter` passes `M_Rock_To_Throw` -- the material on the rock
the fragments are pieces of. Two other debris meshes beside them do carry real
materials, which is what shows the placeholder is a property of these particular
assets rather than of the pack:

| Mesh | Material assigned |
|---|---|
| `SM_Rock_To_Hold` | `M_Rock_To_Throw` |
| `SM_Rampage_Rock_FragA` to `FragE` | `WorldGridMaterial` (the engine placeholder) |
| `SM_Rampage_Rock_Rip_Crater` | `WorldGridMaterial` (the engine placeholder) |
| `SM_Rock_02` | `M_RockPile_MeshEmit_01` |
| `SM_Boulders_1` | `M_RockSlab` |

**Where the rock comes out of the ground, measured.** Issue #432 asks where the
crater goes, and the animation answers it. Following both hands through
`Ability_RipNToss_Rip` with `unreal.AnimPoseExtensions` on 2026-08-08:

| Bone | Lowest at | Position in the animation's own space |
|---|---|---|
| `hand_l` | 0.2833 s | (83.8, 62.0, 2.9) |
| `hand_r` | 0.2644 s | (-84.9, 43.7, 2.9) |

Their midpoint is **(-0.6, 52.9)**, so the rock comes out **52.9 cm in front of
the creature**, on the floor. The half-centimetre is the check on the whole
measurement: a two-handed rip is symmetric about the centre line, and a midpoint
anywhere else would have meant the wrong bones or the wrong axis.

**Forward is +Y in that space, not +X.** The Rampage mesh takes a -90 degree yaw
on its component to face the way its actor faces.
`tools/measure_animation_stride.py` records the same trap from the other side: a
first version of it measured X, got symmetric extremes about zero -- the
signature of a side-to-side axis -- and reported a nonsense jog speed.

**The clip is 1.1333 s long**, so the hands reach the ground 23% of the way
through it. The montage compresses the rip, so the wall-clock moment is that
0.2644 divided by the play rate rather than 0.2644 of wall clock.

**The fragments are also large.** Their half-widths run from 56 to 96 cm against
the whole rock's 103, so five of them at their authored size would be five more
boulders rather than one rock broken up. `Scatter` sizes each piece from its own
bounds to a width the caller asks for, which is why they come out consistent.

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

**Re-measured 2026-08-20** with `tools/probe_succubus_animation.py` and
`tools/measure_animation_stride.py`, for issue #39, which builds this creature.
Every figure below was read from the asset. The nine ability clips recorded on
2026-08-07 all came back identical.

The mesh is `SM_Countess`, **180.8 cm tall**, on `S_Countess_Skeleton` with 16
material slots. Half its height is 90.4 cm, which is the capsule half-height
`ACataclysmSuccubusCharacter` uses.

**What the creature actually plays**, from `ACataclysmSuccubusCharacter`:

| Animation | Length | What plays it |
|---|:-:|---|
| `Primary_Attack_Normal` | 0.9000 | Soulfire's wind-up. Played as authored, so the creature holds the last pose for 0.4 s of the 1.3 s telegraph. Issue [#767](https://github.com/sdubois777/Cataclysm/issues/767) |
| `Cast` | 1.1333 | Wither the Living. **A different clip from the attack on purpose**: the design's counter to the curse is interrupting it, and a player cannot interrupt what they cannot tell apart from an ordinary attack |
| `Idle_Relaxed` | 42.3333 | Standing. **The longest idle in the project by four times**, against the Hellhound's 10.0 |
| `Jog_Fwd` | 1.8000 | Walking, at a play rate of 350 / 321.0 = 1.090 |
| `Death` | 1.6667 | Dying. **One clip, the fewest in the project** along with the Brute's |

**Measured and not used:**

| Animation | Length | Why not |
|---|:-:|---|
| `Primary_Attack_Fast_V1` | 0.6000 | Would leave 0.7 s of the 1.3 s telegraph with the creature holding a pose |
| `Primary_Attack_Slow` | 1.5000 | Would fill the telegraph almost exactly at a play rate of 1.1538, and is the **only** primary attack in the pack that ships a separate recovery clip. Nothing here plays a recovery. Issue [#767](https://github.com/sdubois777/Cataclysm/issues/767) |
| `Primary_Attack_Slow_Recovery` | 1.3333 | The recovery that clip needs |
| `Idle_Straight` | 7.5000 | The alternative idle. `Idle_Pose`, `Idle_Additive` and `Idle_Facial` are all 7.5000 and are a pose and two additive layers rather than something to loop |
| `Jog_Fwd_Combat` | 1.8000 | Identical in length and in authored speed to `Jog_Fwd`; measured so the choice was made against numbers |
| `Sprint_Fwd` | 1.2333 | Nothing sprints. Authored at 399.4 cm/s |
| `Ability_Q` | 0.9667 | No ability uses a hero's ability clips; this creature has two casts and both are covered above |
| `Ability_E` | 1.1667 | |
| `Ability_RMB` | 1.3333 | |
| `Ability_Ultimate` | 3.1667 | Longer than the 2.6 s attack interval, so it could not be a basic attack even if something wanted it |
| `Hitreact_Fwd`, `_Bwd`, `_Left`, `_Right` | 1.0000 each | Nothing plays a hit reaction yet. Issue [#745](https://github.com/sdubois777/Cataclysm/issues/745) |

**The walk is the gentlest animation fit in the project.**
`tools/measure_animation_stride.py` reports **321.0 cm/s** on the -Y axis for
`Jog_Fwd`, with `Idle_Relaxed` reading 0.0 as the control -- which is what says
the method found the right axis on this rig rather than measuring nothing. The
designed speed is 350 cm/s, so the play rate is **1.090**, against the Imp's
1.699 and the Hellhound's 2.478.

The -Y forward axis confirms the engine's usual character-mesh convention holds
for this rig, which is what the -90 degree yaw in `ResolveBody` relies on.

**Re-run the measurements after importing or replacing any of these:**

```
python tools/run_editor_python.py tools/probe_succubus_animation.py
python tools/run_editor_python.py tools/measure_animation_stride.py
```

Both change nothing. The results land in
`game/Saved/Logs/run_editor_python.log`, the first on lines beginning `PROBE|`.

### The Hellhound — IggyScorch

**Re-measured 2026-08-20** with `tools/probe_hellhound_animation.py` and
`tools/measure_animation_stride.py`, for issue #39, which builds this creature.
Every figure below was read from the asset.

**THIS IS THE ONE PACK IN THE PROJECT THAT IS TWO CREATURES.** Iggy is a goblin
who rides Scorch, and the pack holds **one** skeletal mesh, `IggyScorch`, and one
skeleton for the pair. There is no Scorch half to load: of the pack's 144
animations, exactly two carry the `Scorch_` prefix and are not aim offsets, and
every other clip drives both. So `ACataclysmHellhoundCharacter` wears the rider
as well, and whether that is acceptable is issue
[#756](https://github.com/sdubois777/Cataclysm/issues/756).

**The mesh's 17 material slots say which creature each belongs to**, which is
what makes hiding the rider possible at all:

| Slots | Named | Whose |
|---|---|---|
| 0, 1, 6, 7, 8 | `M00_Mount_Front`, `M01_Mount_Rear`, `M06_Mount_Eyeshadow`, `M07_Mount_TearLine`, `M08_Mount_Eyes` | the mount |
| 2 | `M02_Mount_Oils`, material `M_OilPouch` | named for the mount; likely the rider's fuel strapped to it. **Look before deciding** |
| 3, 4, 5, 15, 16 | `M03_Items` through `M16_Thrower` | the rider's equipment and weapon |
| 9 to 14 | `M09_Pilot` through `M14_Pilot_GoggleGlass` | the rider |

**What the creature actually plays**, from `ACataclysmHellhoundCharacter`:

| Animation | Length | What plays it |
|---|:-:|---|
| `Scorch_Primary_Fire_Med` | 0.9667 | Maul, the bite. Fits the 1.1 s interval with 0.13 s to spare |
| `R_Ability_FireBreath_Start` | 1.1333 | Hellrush's wind-up, compressed to a play rate of 1.366 to fit inside 0.83 s. **The only compressed clip on this creature** |
| `IggyScorch_Idle` | 10.0000 | Standing |
| `Jog_Fwd` | 2.3000 | Walking, at a play rate of 750 / 302.6 = 2.478 |
| `Death_Front` | 1.6667 | Dying. One of two, drawn per death |
| `Death_Back` | 1.6667 | The other |

**Measured and not used:**

| Animation | Length | Why not |
|---|:-:|---|
| `Scorch_Primary_Fire_Fast` | 0.5333 | Would leave 0.57 s of the creature standing still between every bite |
| `R_Ability_FireBreath_Loop` | 3.1000 | Nothing holds a breath open |
| `R_Ability_FireBreath_End` | 1.7667 | |
| `R_Ability_Fire` | 6.0000 | Full uninterrupted breath |
| `IggyScorch_HitReact_Front` | 0.8333 | Nothing plays a hit reaction yet. Issue #745 |
| `IggyScorch_KnockBack` | 2.0000 | Nothing plays one when a creature is shoved |

**The walk is the tightest animation fit in the project.**
`tools/measure_animation_stride.py` reports **302.6 cm/s** on the -Y axis for
`Jog_Fwd`, with `IggyScorch_Idle` reading 0.0 as the control -- which mattered
more here than on any other rig, because this is the first one measured that is
two creatures and the IK foot bones could have been following either. The
designed speed is 750 cm/s, so the play rate is **2.478 against a ceiling of
2.5**. Nothing else in the project comes near it: the Abyssal Warden needs 0.994
and the Brute 1.11 to walk.

**There is no faster clip to switch to.** `Travelmode_Fwd` was measured as the
alternative and is *slower*, at 268.1 cm/s. So the creature will read as a
sped-up film rather than as a running animal, and the answers are an animation
Blueprint or a slower designed speed. **Only the project owner can judge whether
it needs one**: the automation command runs with `-nullrhi`.

**Nothing in the pack is a charge.** Hellrush plays the fire-breath start
instead, which is the closest existing motion, and its speed is therefore the
one number on this creature that was chosen rather than derived from a clip.

### The Abyssal Warden — Grux, molten

**Re-measured 2026-08-09** with `tools/probe_warden_animation.py`, and extended
past the attack clips because issue #490 builds this creature and two of its
three animations were not recorded here at all. Every figure below was read from
the asset. `GruxMolten` shares `Grux_Skeleton` with the base Grux mesh, so every
clip plays on it without retargeting.

**What the creature actually plays**, from
`ACataclysmAbyssalWardenCharacter`:

| Animation | Length | What plays it |
|---|:-:|---|
| `PrimaryAttack_LA_Fast` | 0.6333 | The first half of its basic attack |
| `PrimaryAttack_RA_Fast` | 0.6333 | The second half. Joins the clip above at **0.01 cm** |
| `PrimaryAttack_RA_Recovery` | 0.8333 | Returns it to a neutral stance. **2.1000 s for all three**, inside the 2.4 s interval with 0.3 s to spare |
| `Ultimate_Roar` | 1.4000 | Molten Roar. Fits inside the 2.0 s wind-up at authored speed, with 0.6 s of held pose after it |
| `Idle` | 24.3333 | Standing |
| `Jog_Fwd` | 1.5333 | Walking, at a play rate of 280 / 281.6 = 0.994 |
| `Death_A` | 1.6667 | Dying. One of two, drawn per death |
| `Death_B` | 1.6333 | The other. Measured 2026-08-19 for issue #522 |

**One basic attack is three clips, and the reason is measured.**
`tools/measure_warden_recovery.py` compares each clip's last pose against the
idle's first, which is the size of the jump a player sees when the animation
switches:

| Clip | How far its end is from the idle |
|---|:-:|
| `PrimaryAttack_LA` | 151.18 cm |
| `PrimaryAttack_RA` | 161.78 cm |
| `PrimaryAttack_LA_Recovery` | 16.92 cm |
| `PrimaryAttack_RA_Recovery` | 24.12 cm |
| `Ultimate_Roar` | **0.39 cm** — it already returns to neutral, so it needs no recovery |

**The fast variants were chosen over the full-speed ones by measurement.**
`LA_Fast` into `RA_Fast` joins at 0.01 cm; the full-speed pair joins at 12.57.
And only the fast pair leaves room for a recovery: left, right and a recovery at
full speed is 3.100 s against a 2.4 s interval and would need a play rate of
1.29.

**Two authored swings fit inside one attack interval, and this is the only one of
the seven that manages it.** 1.1333 + 1.1333 is 2.2667 against a 2.4 second
interval, so the pair fits with 0.1333 seconds to spare and neither clip is
compressed.

**The rest of the attack set**, measured and unused:

| Animation | Length | Note |
|---|:-:|---|
| `PrimaryAttack_Start` | 0.5667 | |
| `PrimaryAttack_LA_Fast` / `RA_Fast` | 0.6333 | |
| `PrimaryAttack_RB` | 0.8667 | |
| `PrimaryAttack_LA_Recovery` / `RA_Recovery` | 0.8333 | |
| `PrimaryAttack_LB` | 1.0667 | |
| `PrimaryAttack_FourStrikes` | 3.7000 | Exceeds the 2.4 s interval. Usable at a play rate of 1.54, under the 2.50 ceiling |

**The charge clip exists and nothing can play it.** `Stampede` is 0.7000 seconds
and `Stampede_Knockup` is 1.5333. The design gives this creature a charge, and
`ACataclysmEnemyController` cannot execute a Movement-shape ability at all —
issue #491. The 0.7000 second clip is what decided the design should be a charge
rather than a leap: a leap has to be stitched from five clips.

| Animation | Length | Note |
|---|:-:|---|
| `Bound` | 0.0333 | **A single pose, not a leap.** Its name suggests otherwise |
| `Jump_Start` | 0.3333 | |
| `Jump_Up` | 0.3333 | |
| `JumpApex` | 0.2000 | |
| `Jump_Mid` | 0.3000 | |
| `Jump_Loop` | 1.3333 | |
| `Jump_Fall` | 1.3333 | |
| `Jump_Land` | 0.9000 | |
| `Attack_Melee_Air` | 1.0000 | |
| `Stampede` | 0.7000 | The charge. Fits an 0.83 s wind-up as authored |
| `Stampede_Knockup` | 1.5333 | A charge that knocks the target up. Nothing can knock the player back yet, issue #310 |

**The four hit reactions are additive and one thing is not.** `HitReact_Front`
and `HitReact_Back` are 0.8667 seconds and both are
`AAT_LOCAL_SPACE_BASE`, so the one-clip-at-a-time playback path cannot play them
on their own — they need a layer to be added onto, which is animation Blueprint
work. `Knock_Up` is 2.0000 seconds and `AAT_NONE`, so it can be played directly.

**No animation Blueprint exists for this creature.** `ABP_Brute` was authored by
hand in the editor, and `tools/probe_brute_animation.py` established that
Unreal's Python exposes no way to connect two animation graph pins, so one
cannot be generated. `ACataclysmAbyssalWardenCharacter::ResolveAnimationBlueprint`
therefore falls back to the single-clip animation mode: the swing and the roar are
visible, and walking does not blend, so the creature slides rather than steps.

### The Gatekeeper — Sevarog

**Re-measured 2026-08-20** with `tools/probe_gatekeeper_animation.py`, for issue
[#759](https://github.com/sdubois777/Cataclysm/issues/759), which builds this
creature. Every one of the eighteen clips recorded on 2026-08-07 came back
identical.

The mesh is `Sevarog`, **311.1 cm tall**, on `Sevarog_Skeleton` with 6 material
slots. Half its height is 155.53 cm, which is the capsule half-height
`ACataclysmGatekeeperCharacter` uses and **by far the largest in the project** --
the Corrupted Sentinel, the next tallest, is 98.1.

**What the creature actually plays**, from `ACataclysmGatekeeperCharacter`:

| Animation | Length | What plays it |
|---|:-:|---|
| `Swing1_Medium` | 1.1333 | Dread Cleave, across its 0.9714 s wind-up at a play rate of 1.1667 |
| `Soul_Siphon` | 1.8333 | Soulfall, across its 1.2571 s wind-up at 1.4584 |
| `Subjugation` | 2.8667 | Call the Damned. **No wind-up to fit** -- a Summon draws no marker -- so it plays at its authored speed |
| `Ultimate_Swing_120fps` | 2.6333 | Soul Harvest, across its 2.0 s wind-up at 1.3167. Issue [#779](https://github.com/sdubois777/Cataclysm/issues/779): the pack authors this ultimate as three clips and only this one is played |
| `Idle` | 8.9000 | Standing |
| `Jog_Fwd` | 9.0000 | Walking. **The play rate is a placeholder of 1.0 and not a derivation.** See below |
| `Death_front` | 0.9667 | Dying. **One clip, the fewest in the project** along with the Brute's and the Succubus's |

**THE WALK SPEED COULD NOT BE MEASURED, AND THE PLAY RATE IS A GUESS.**
`tools/measure_animation_stride.py` **failed its own control** on this rig: run on
2026-08-20 it reported 0.0 cm/s for `Walk_Fwd`, `Jog_Fwd` and `Run_Fwd`, and
**14.7 cm/s for `Idle`**, which is the control and must read zero. Wrong in both
directions, so none of its numbers may be used. Issue
[#778](https://github.com/sdubois777/Cataclysm/issues/778) carries the evidence.

`tools/probe_gatekeeper_foot_bones.py`, written to find out why, measured how far
each leg bone really travels across each clip:

| Clip | `ik_foot_l` travel | `foot_l` travel | `pelvis` travel | Animated tracks |
|---|:-:|:-:|:-:|:-:|
| `Walk_Fwd` | **0.00 cm** | 0.00 cm | 5.64 cm | 127 |
| `Run_Fwd` | **0.00 cm** | 0.00 cm | 5.64 cm | 127 |
| `Jog_Fwd` | 27.01 cm | 0.00 cm | 27.46 cm | 155 |
| `Idle` | **159.52 cm** | 0.00 cm | 13.10 cm | 155 |

**`Walk_Fwd` and `Run_Fwd` animate no leg bone at all** -- every foot, calf and
thigh reads 0.00 cm across the whole 1.6 seconds, and both carry 127 animated
tracks where the other two carry 155. They appear to be partial clips rather than
complete ones, and should not be reached for again without checking. `Jog_Fwd` is
the only locomotion clip whose feet move, which is why it is the one in use.

**Measured and not used:**

| Animation | Length | Why not |
|---|:-:|---|
| `Swing1_FAST_v2` .. `Swing3_FAST_v2` | 0.5333 | Shorter than the 0.9714 s wind-up, so the creature would hold a pose |
| `Swing1_120fps` .. `Swing3_120fps` | 1.0000 | Would fit, and `Swing1_Medium` fills the wind-up more completely |
| `Swing2_Medium`, `Swing3_Medium` | 1.1333 | The pack ships three swing chains; only one is played today. Alternating them the way the Corrupted Sentinel alternates its two firing clips would stop continuous swinging reading as one clip looping |
| `Swing1_Slow` .. `Swing3_Slow` | 1.7000 | Needs a play rate of 1.7501 to fit the wind-up |
| `Ultimate_Targeting` | 0.8667 | The start of the three-clip ultimate. Issue [#779](https://github.com/sdubois777/Cataclysm/issues/779) |
| `Ultimate_Targeting_Loop` | 2.2333 | The hold of it |
| `Knock_back` | 2.6667 | Nothing knocks this creature back |
| `Walk_Fwd`, `Run_Fwd` | 1.6000 each | Animate no leg bone. See above |
| `Sprint_Fwd` | 4.0000 | Nothing sprints |
| `Idle_additive` | 8.9000 | An additive layer rather than something to loop |
| `Hitreact_Front`, `_Back`, `_Left`, `_Right` | 1.7667, 1.9000, 1.9667, 1.5667 | Nothing plays a hit reaction yet. Issue [#745](https://github.com/sdubois777/Cataclysm/issues/745) |
| `Stage_1` .. `Stage_4` | 0.0333 each | Single marker poses. **This creature has three phases, not four**, so at best three could be used, and nothing plays one today |

Three swing chains at four speeds each give a boss enough distinct motion to read
as having phases. The targeting animation splitting into a start and a hold is
the same shape the Brute's stomp has, and is what a telegraph needs.

**Re-run the measurements after importing or replacing any of these:**

```
python tools/run_editor_python.py tools/probe_gatekeeper_animation.py
python tools/run_editor_python.py tools/probe_gatekeeper_foot_bones.py
```

Both change nothing. The results land in
`game/Saved/Logs/run_editor_python.log`, on lines beginning `PROBE|`.
