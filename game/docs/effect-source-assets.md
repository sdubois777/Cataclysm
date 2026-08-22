# Source art for the particle effects

Which imported texture each authored Niagara system draws with. This is the
effects equivalent of `enemy-source-assets.md`, which does the same job for the
seven enemies and their Paragon meshes.

**Four packs are installed and none of them is in git.** They sit directly under
`game/Content/`, and `.gitignore` excludes each one inside the fenced block
between `# THIRD-PARTY-PACKS-BEGIN` and `# THIRD-PARTY-PACKS-END`, which
`tools/third_party_content.py` parses. Issue #557 put the first one there. So a
fresh checkout has the systems but not the art they draw with, and this file is
the record of what to install to get it back.

**A committed asset referencing an uncommitted one is the arrangement this
project already has**, not a new compromise: the enemy Blueprints reference
Paragon skeletal meshes on exactly the same terms.

## The four packs

| Folder under `game/Content/` | Installed | Size | What this project takes from it |
|---|---|---|---|
| `_SplineVFX/` | 2026-08-14 | 75 MB | `T_Vfx_BasicDot`, the soft round sprite every emitter drew with until 2026-08-22, and `MI_Basic_trail05` for the projectile's ribbon streak |
| `SplineEffect2/` | 2026-08-22 | 63 MB | Nothing yet. Spline-driven beam meshes, materials and textures, for the `NS_Beam` shape that is not built |
| `Vefects/` | 2026-08-22 | 356 MB | Two shockwave meshes and four materials: the ground shockwave on the hit burst, the ground ring, and the flare and heat haze on the cast burst. Three packs in one folder: Easy Shockwaves VFX, Free Fire and Zap VFX |
| `Knife_light/` | 2026-08-22 | 41 MB | `SM_slash` and `MI_mid01`, the melee swing arc |

**The 2026-08-22 three were installed by the project owner** through the editor's
Fab tab, in answer to issue #811. They could not be installed from here: the Fab
tab is a web page embedded in the editor and exposes no widgets to the editor's
own UI automation, and the desktop mouse control refuses to attach to the Unreal
Editor because it has no Start menu entry. The Epic Games Launcher's own Fab
Library was tried instead and its **Add To Project** button reports "No
compatible user projects found" for every pack, including one already installed,
because the launcher's project list is empty for this project. So installing a
pack is an operator step.

**One of them changed `game/Cataclysm.uproject`.** A pack enabled the
`MovieRenderPipeline` plugin and stripped the file's trailing newline. The plugin
was left enabled rather than reverted, because a pack that asked for it may load
its own content with it, and disabling it would be undone by the next install.

## The first pack, in detail

| What | Value |
|---|---|
| Folder under `game/Content/` | `_SplineVFX/` |
| Installed | 2026-08-14, through the editor's Fab tab |
| Size on disk | 75 MB |
| Assets | 112 `.uasset` files |
| Textures | 41, all under `_GenericSource/Texture/` |
| Top-level folders | `Demo/`, `NS/`, `_GenericSource/` |

Its own `NS/` folder holds the pack author's Niagara systems. **Nothing in this
project uses them and nothing should.** `docs/Niagara_Conventions.md` section 3
gives the reason: a bought system brings its own effect type, its own parameter
names and its own unbudgeted emitter counts. The rule that document states is
"take textures and materials from packs; author the systems", and that is what
the table below records.

## Which pack asset each emitter uses

| System | Emitter | Draws | Through which material | From |
|---|---|---|---|---|
| `NS_Impact_Point` | `Core` | sprite `T_Vfx_BasicDot` | `M_Impact_Sprite` | `_SplineVFX` |
| `NS_Impact_Point` | `Sparks` | sprite `T_Vfx_BasicDot` | `M_Impact_Sprite` | `_SplineVFX` |
| `NS_Impact_Point` | `Shockwave` | mesh `SM_VFX_Cyl_In_Out_Floor_01` | `M_VFX_Shockwave_01` | `Vefects` |
| `NS_Proj_Body` | `Core`, `Trail` | sprite `T_Vfx_BasicDot` | `M_Impact_Sprite` | `_SplineVFX` |
| `NS_Proj_Body` | `Streak` | ribbon `T_Vfx_trail_05` | `MI_Basic_trail05` | `_SplineVFX` |
| `NS_Strike_Arc` | `Arc` | mesh `SM_slash` | `MI_Strike_Arc`, this project's own instance of `MI_mid01`'s master | `Knife_light` |
| `NS_Impact_Ground` | `Ring` | mesh `SM_VFX_Cyl_In_Out_Floor_01` | `M_VFX_Shockwave_01` | `Vefects` |
| `NS_Cast_Windup` | `Flare` | mesh `SM_VFX_In_Out_Cyl_01` | `MI_VFX_Shockwave_01_Additive` | `Vefects` |
| `NS_Cast_Windup` | `Sparks`, `Glow` | sprite `T_Vfx_BasicDot` | `M_Impact_Sprite` | `_SplineVFX` |
| `NS_Cast_Windup` | `Haze` | sprite | `MI_VFX_HeatDistortion_Light` | `Vefects` |

**`MI_Strike_Arc` exists because `MI_mid01` sets both of its colour parameters
to BLACK**, so the damage type's colour multiplied to nothing and the melee arc
shipped as a black smear on 2026-08-22. This project's own instance of the same
master material sets them to white. Issue #811.

**`MI_VFX_HeatDistortion_Light` is drawn on a SPRITE renderer and not a mesh
one, and that was measured rather than chosen.** On a mesh renderer Niagara
reports "Some materials do not have the correct usage flags set, and will use
the default material" -- the material carries `bUsedWithNiagaraSprites` and not
`bUsedWithNiagaraMeshParticles`. A usage flag lives on the master material,
which is inside a pack that is not in git, so setting it would be lost on the
next install. A camera-facing sprite is the conventional way to draw heat haze
anyway.

**The two mesh emitters are the only things in the project that are not a flat
tinted sprite**, and that is the whole point of them. The project owner said of
the sprite-only effects, twice, that they read as a placeholder: "just a basic
orb shape with some weird streak behind it". Issue #811.

**Both pack materials contain a `ParticleColor` node**, which is what lets one
system serve all eight damage types. A mesh material without one would look
correct and would silently make `User.ElementColour` do nothing. Both also carry
`bUsedWithNiagaraMeshParticles`, without which the renderer falls back to the
engine's default material in silence.

**`M_VFX_Shockwave_01` is additive and `MI_mid01` is translucent.** That matters
for `ElementColourDark`: under additive blending a near-black colour adds nothing
and disappears, and section 5 of `docs/Niagara_Conventions.md` says the darker
colour exists specifically to stay legible when the primary hue matches the
floor. The shockwave uses the primary colour only, so the additive blend costs
nothing there; anything that wants the dark colour must not be additive.

**The streak's material comes out of the pack rather than being authored here,
and that is a different arrangement from the row above it.** `M_Impact_Sprite` is
this project's own material and names the pack's texture; `MI_Basic_trail05` is
the pack's own material instance, used unchanged. A ribbon needs
`bUsedWithNiagaraRibbons` rather than `bUsedWithNiagaraSprites`, and authoring a
second material to set one flag would buy nothing while the pack already ships a
trail material with it set. The Niagara stack reports no usage warning for it,
which is the check.

**So a fresh clone has a projectile whose streak draws with the engine's default
ribbon material**, because the pack is gitignored. That is the same state the
enemy Blueprints are in without the Paragon packs, and
`Cataclysm.Effects.ProjectileHeadRidesAndTrailStaysBehind` names the material it
expects so the substitution is reported rather than silent.

Full path of the texture:
`/Game/_SplineVFX/_GenericSource/Texture/T_Vfx_BasicDot.T_Vfx_BasicDot`.

`M_Impact_Sprite` lives at `/Game/Effects/Materials/`, is authored by this
project, and is the only thing that names the texture. All four emitters across
the two systems use that one material, so re-pointing the texture is a single
edit in one place rather than an edit per emitter.

**Its name is now narrower than its use, and it was left alone deliberately.**
It is not impact-specific in any way -- it is the project's unlit translucent
sprite material, and the projectile's head and trail draw with it for exactly the
reason the impact's two emitters do. Renaming a binary asset that two systems
reference costs a re-point in both and buys a better name and nothing else, so it
is worth doing when a third system needs it and not before.

## The textures worth knowing about

Read from `_GenericSource/Texture/` on 2026-08-14. These are the five an impact,
a projectile or a death effect would reach for; the other 36 are trails, ribbons
and environment shapes.

| Texture | What it is |
|---|---|
| `T_Vfx_BasicDot` | A soft round sprite. **In use**, see above |
| `T_Vfx_CircleDot` | The same shape with a harder edge |
| `T_Vfx_stamp_light01_84` | A flash |
| `T_Vfx_Stamp_Smoke02_88` | Smoke |
| `T_Vfx_Stamp_boom_81` | A burst stamp |

## What the material does with it

`M_Impact_Sprite` is unlit and translucent, and its emissive output is the
texture's colour multiplied by the particle's colour. That multiplication is what
makes the whole arrangement work: the particle colour comes from the Niagara
system's `User.ElementColour` and `User.ElementColourDark` parameters, which the
game sets from the damage type's row in `DT_ElementVisuals`. A material that
sampled the texture and ignored the particle colour would look correct and would
silently make the data-driven colour do nothing.

**It is translucent rather than additive, and that is a decision rather than a
default.** Additive blending adds the effect's colour to whatever is behind it,
so a near-black colour adds nothing and disappears. `ElementColourDark` is
near-black for several damage types, and section 5 of
`docs/Niagara_Conventions.md` says that darker colour exists specifically to stay
legible when the primary hue matches the floor. Under additive blending it would
not be visible at all, which would remove the readability guarantee while
appearing to work.

**The material needs its Niagara sprite usage flag set.** `bUsedWithNiagaraSprites`
must be true or the renderer silently falls back to the engine's default material
and draws neither this texture nor this colour graph. The Niagara stack reports
that as a warning reading "do not have the correct usage flags set, and will use
the default material"; it is not an error, and the effect still renders, so it is
easy to miss.

## Installing more from Fab

The project owner is signed into Fab in the editor. Use the **Add to project**
button on the editor's Fab or Library tab. Do not ask them for credentials.

After installing anything, add the folder it created to the fenced block in
`.gitignore`, or the pack lands in git and the repository grows by its whole
size.
