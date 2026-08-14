# Source art for the particle effects

Which imported texture each authored Niagara system draws with. This is the
effects equivalent of `enemy-source-assets.md`, which does the same job for the
seven enemies and their Paragon meshes.

**The pack is installed and is deliberately not in git.** It sits at
`game/Content/_SplineVFX/`, and `.gitignore` excludes it inside the fenced block
between `# THIRD-PARTY-PACKS-BEGIN` and `# THIRD-PARTY-PACKS-END`, which
`tools/third_party_content.py` parses. Issue #557 put it there. So a fresh
checkout has the systems but not the art they draw with, and this file is the
record of what to install to get it back.

**A committed asset referencing an uncommitted one is the arrangement this
project already has**, not a new compromise: the enemy Blueprints reference
Paragon skeletal meshes on exactly the same terms.

## The pack

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

## Which texture each effect uses

| Effect | Texture | Through which material |
|---|---|---|
| `NS_Impact_Point` | `T_Vfx_BasicDot` | `M_Impact_Sprite` |

Full path of the texture:
`/Game/_SplineVFX/_GenericSource/Texture/T_Vfx_BasicDot.T_Vfx_BasicDot`.

`M_Impact_Sprite` lives at `/Game/Effects/Materials/`, is authored by this
project, and is the only thing that names the texture. Both of
`NS_Impact_Point`'s emitters use that one material, so re-pointing the texture is
a single edit in one place rather than an edit per emitter.

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
