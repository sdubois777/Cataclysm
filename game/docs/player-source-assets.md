# Source art for the player character

Which asset gives the player a body, where it came from, and the animation
durations that constrain how the character can move and attack.

Written 2026-09-01 for issue #1124, which replaced the player's placeholder
cylinder and cone with the Unreal Mannequin.

**Everything measured here was read through the editor** on 2026-09-01 from the
assets in `game/Content/Characters/Mannequins/`, using the asset registry tags
and the `SequenceLength` property. It is not an estimate.

## Where these assets came from

They were copied out of the engine install, from

```
Engine/Templates/TemplateResources/High/Characters/Content/Mannequins/
```

**That folder is not a plugin and is not a mounted content root.** It is what
Unreal copies into a project made from the Third Person template. The editor
cannot see anything in it until it is copied into a project, so referencing it
where it sits is not an option.

**The copy was an ordinary file copy, with no path fixing.** Every asset in that
folder already records `/Game/Characters/Mannequins/...` as its own package path,
so copying the files to `game/Content/Characters/Mannequins/` put them where they
already believed they were and every reference between them stayed intact.

### Why not the MoverExamples plugin

`Engine/Plugins/Experimental/MoverExamples/Content/Characters/Mannequins/` holds
a second copy of the Mannequin, and issue #1124 was originally written against
it. It is the worse source for three reasons.

1. **It is an experimental plugin that is not enabled here.** Using it meant
   enabling the plugin, restarting the editor, copying, disabling it and
   restarting again. The template resources need none of that.
2. **Its animation Blueprint cannot drive this character.** `ABP_Manny` casts its
   owner to `MoverExamplesCharacter` and reads a `CharacterMoverComponent`. This
   project does not use the Mover plugin, so that cast fails and the blend space
   receives no speed. `ABP_Unarmed`, in the template resources, casts to plain
   `ACharacter` and reads the standard `UCharacterMovementComponent`, which is
   what `ACataclysmPlayerCharacter` is and has.
3. **It ships no attack animations and one death clip.** The template resources
   ship four attacks and six deaths. See the tables below.

## What was copied, and what was left behind

47 assets, 58.4 MB, out of the 128 assets and 126 MB in the source folder.

| Taken | What it is |
|---|---|
| `Meshes/SK_Mannequin` | the Skeleton every clip below is bound to |
| `Meshes/SKM_Manny_Simple` | the body |
| `Materials/M_Mannequin`, `Materials/Manny/` | the master material and its two instances |
| `Textures/Manny/`, `Textures/Shared/` | the Manny texture set and the logo |
| `Rigs/PA_Mannequin` | the physics asset the mesh names |
| `Rigs/CR_Mannequin_FootIK` | the Control Rig `ABP_Unarmed` runs for foot placement |
| `Anims/Unarmed/ABP_Unarmed` | the animation Blueprint |
| `Anims/Unarmed/BS_Idle_Walk_Run` | the locomotion blend space |
| `Anims/Unarmed/MM_Idle`, `Walk/`, `Jog/` | idle and eight directions each of walk and jog |
| `Anims/Unarmed/Jump/` (three of five) | `MM_Jump`, `MM_Fall_Loop`, `MM_Land` |
| `Anims/Unarmed/Attack/` | four attack clips, for issue #1126 |
| `Anims/Death/` | six death clips |

| Left behind | Why |
|---|---|
| `Meshes/SKM_Quinn_Simple`, `Materials/Quinn/`, `Textures/Quinn/` | the second template body. Nothing uses it. Issue #931, which adds appearance choices at character creation, is what might. |
| `Anims/Pistol/`, `Anims/Rifle/` | firearm sets. This game has no firearms. |
| `Anims/Unarmed/Jump/MM_Dash`, `MM_WallJump` | there is no dash and no wall jump. `MM_Dash` also names `/Game/Common/Animations/MM_Dash_Forward`, which the template does not ship. |
| `Rigs/CR_Mannequin_Body`, `Rigs/CR_Mannequin_Procedural` | animation-authoring Control Rigs, 15.8 MB together. `CR_Mannequin_Body` names three `/Game/Developers/Jeremie/...` assets the template does not ship. |

### The references that dangle, and why none of them matter

Seven package paths are named by the copied assets and are not present. All seven
are editor-only preview or source links, and none is loaded at run time.

| Reference | Named by | What it is |
|---|---|---|
| `Animations/EditableAnimations/LS_Attack_01` … `LS_ChargedAttack` | the four attack clips | the Level Sequences the clips were baked from. **Missing from Epic's own folder too**, so this is the shipped state and not something the trimming caused. |
| `Meshes/SKM_Manny` | the four attack clips | the mesh they were previewed on. Also missing from Epic's folder. |
| `Meshes/SKM_Quinn_Simple` | the eight jog clips | the mesh they were previewed on. Left behind deliberately. |
| `Rigs/CR_Mannequin_Body` | `SKM_Manny_Simple` | the mesh's `DefaultAnimatingRig`, a soft pointer the editor resolves only when somebody opens the mesh in animation mode. Left behind deliberately. |

## The folder shape does not follow `content-layout.md`

`game/docs/content-layout.md` says to group by subject rather than by asset type,
and specifically that there should be no `Meshes/`, `Textures/` or `Materials/`
folders.

**This folder has all three, and cannot not have them.** The assets record their
own package paths, so the layout is fixed by the files: moving `SK_Mannequin` out
of `Meshes/` breaks every clip bound to it and every material reference. The
alternative is renaming 47 assets by hand and re-pointing every reference, which
buys a tidier path and nothing else. `content-layout.md` records this exception.

## The body

| | |
|---|---|
| Skeletal mesh | `SKM_Manny_Simple` |
| Triangles | 92,178 |
| Vertices | 48,705 |
| Bones | 89 |
| Levels of detail | 3 |
| Skeleton | `SK_Mannequin` |
| Physics asset | `PA_Mannequin` |

For scale, the seven vertical slice enemies run from 9,786 triangles (The Imp) to
92,562 (The Hellhound), so the player is at the top of the range the game already
draws.

## The death clips

Drawn at random per death by `ACataclysmPlayerCharacter::PlayDeathAnimation`,
through `UCataclysmEnemyDeath::ClipToPlay`, which is the same picker the enemies
use.

| Clip | Seconds | Root motion |
|---|--:|:-:|
| `MM_Death_Front_01` | 1.1000 | no |
| `MM_Death_Front_02` | 1.1333 | no |
| `MM_Death_Front_03` | 0.9333 | no |
| `MM_Death_Back_01` | 1.1000 | no |
| `MM_Death_Left_01` | 0.9667 | no |
| `MM_Death_Right_01` | 1.1000 | no |

**None of them carries root motion**, so the body falls where it stood rather
than sliding.

**The longest is 1.1333 seconds and the player lies dead for 3.** That gap is why
the death clip is played straight onto the mesh component in single-node mode
rather than as a montage through the animation Blueprint's `DefaultSlot`: a
montage blends back out to the locomotion graph when its clip ends, so the corpse
would stand up in an idle pose and wait there for nearly two seconds.
`ACataclysmPlayerCharacter::Revive` puts the animation Blueprint back.

**Direction is not used.** Six clips cover front, back, left and right, and
nothing carries the direction a killing blow came from, so the clip is drawn
rather than chosen. Choosing would need the hit direction carried into the death
path, which is the same thing `UCataclysmEnemyDeath::ClipToPlay` records as not
being possible for enemies either.

## The attack clips

**Three of the four are cycled through whenever the character uses any skill**,
including the basic attack. Issue #1126.
`ACataclysmPlayerCharacter::PlayAttackAnimation` plays them through the animation
Blueprint's `DefaultSlot`, so a swing blends over the locomotion graph rather
than replacing it. Two measurements from them constrain how, and both are dealt
with in that function rather than left as surprises.

| Clip | Seconds | Root motion | Cycled? |
|---|--:|:-:|:-:|
| `MM_Attack_01` | 1.0000 | **yes** | yes |
| `MM_Attack_02` | 1.0000 | **yes** | yes |
| `MM_Attack_03` | 1.6667 | **yes** | yes |
| `MM_ChargedAttack` | 1.8333 | **yes** | **no** |

**Cycled in order rather than drawn at random**, unlike the death clips. The
difference is how often they are seen: a death happens once, so a random draw
stops two deaths in a row looking identical, while a basic attack fires every two
thirds of a second and a random draw over three clips repeats one about a third
of the time, which reads as the animation sticking.

**`MM_ChargedAttack` is deliberately not cycled.** At 1.8333 seconds it is nearly
three times a fast weapon's swing interval, so putting it in an attack that fires
by itself would mean playing it at close to triple speed. It is kept in the
repository for a skill that deserves a heavier swing.

### Every clip is longer than the interval it has to fit inside

Attack speed in `game/Data/ItemBases.csv` runs 1.2 to 1.5 swings a second, which
is an interval of 0.833 down to 0.667 seconds. The shortest clip is 1.0 second,
so even the fastest attack clip is 1.2 to 1.5 times too long.

**The clip is played faster rather than cut short.** The rate is the clip's
length divided by the swing interval, floored at 1 so a clip is never stretched,
and capped at 2.5. That is the rule
`ACataclysmAbyssalWardenCharacter::PlayOneShot` already follows: never slower
than authored, only faster, and only when it must be. Stretching a short clip
across a long window was tried on the Brute and read as slow motion.

**The cap exists because attack speed has no design ceiling.** Affixes and
passives raise it and nothing caps it, so a character stacked far enough would
otherwise ask for a swing at many times speed. Past 2.5 the animation stops
keeping up with the damage rather than turning into a blur.

### Where the damage lands is not where the swing strikes

**Not fixed, and worth knowing.** Damage is applied when the ability activates,
which is the start of the clip; the animation's own impact is somewhere in the
middle of it. So a hit registers before the weapon appears to connect.

Issue #784 records the same problem one step earlier for enemies: three
telegraphed basic attacks land damage 0.46 to 1.14 seconds away from where the
animation strikes. Fixing it for the player means delaying damage to an animation
notify, which changes how combat feels and is a larger change than drawing a
swing.

### Root motion is switched off

**All four carry root motion**, unlike the death clips, and a
`UCharacterMovementComponent` takes root motion from montages by default. Left
alone, every basic attack would shove the character a step forward — several
times a second, at whatever happens to be in reach.

`ACataclysmPlayerCharacter::PlayAttackAnimation` clears
`bEnableRootMotionTranslation` and `bEnableRootMotionRotation` **on the montage**
rather than on the movement component, because the movement component's setting
is global to the character and would disable root motion for anything else that
ever wants it.

## The locomotion clips

`ABP_Unarmed` drives these through `BS_Idle_Walk_Run`, blending by speed and
direction. Nothing in this project's C++ touches them; the animation Blueprint
reads the movement component directly.

| Clip | Seconds | Root motion |
|---|--:|:-:|
| `MM_Idle` | 7.5667 | no |
| `MF_Unarmed_Walk_Fwd` | 1.5000 | yes |
| `MF_Unarmed_Jog_Fwd` | 1.7667 | yes |

Eight directions exist for both walk and jog. The root motion on them is not
applied: `UCharacterMovementComponent` defaults to taking root motion from
montages only, so the character moves at the speed its `MovementSpeed` attribute
sets and the blend space only chooses the pose.
