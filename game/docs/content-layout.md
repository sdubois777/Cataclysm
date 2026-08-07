# Where assets go, and what they are called

The folder layout and naming convention for `game/Content/`.

Written 2026-08-07 to answer issue #370, which asked where work derived from
third-party asset packs should live. That question blocked the first enemy: The
Brute was built entirely in C++ partly because there was no agreed place to save
a Blueprint.

## What this is based on

There are only two named sources, and neither covers everything.

| Source | What it actually is | What it covers |
|---|---|---|
| [Epic's asset naming conventions](https://dev.epicgames.com/documentation/en-us/unreal-engine/recommended-asset-naming-conventions-in-unreal-engine-projects) | Epic's only published page on this | Naming only |
| [The Gamemakin UE Style Guide](https://github.com/Allar/ue5-style-guide) | Michael Allar's community guide, the de facto standard | Naming and folder structure |

**Epic has never published a recommended folder structure.** Every "Epic
recommended layout" circulating online is community inference. Epic's naming
table is also not a game-development table: the page says it "reflects how Epic
Games names Assets in sample projects, such as the In-Camera VFX Production
Test", which is a virtual-production sample. That is why it has rows for
`OCIO_` and `SNAP_` and none for Gameplay Ability, Gameplay Effect or Input
Action.

Epic's own sample game, Lyra, follows neither its naming page nor the community
guide consistently. Where it is the only source that has faced an asset type —
Gameplay Abilities, Enhanced Input, Niagara — this document follows it.

## Folder layout

```
game/Content/
├── Data/            Generated. DataTables built from game/Data/*.csv.
├── Input/           Generated. Input actions and mapping contexts.
├── Maps/            Every level, without exception.
├── Enemies/         Authored enemy work, split by Cataclysm then by enemy.
├── Developers/      Per-user sandbox. Not project content. Ignored by git.
└── Paragon*/        Third-party packs, where Fab installs them. Ignored by git.
```

Reserved names, to be created when something needs them rather than now:
`Characters/` (the 24 player classes), `Abilities/` (Gameplay Abilities, Effects
and Cues, which both characters and enemies reference), `Items/`, `Empire/`,
`Effects/` (Niagara shared across enemies and skills), `MaterialLibrary/`
(master materials and functions), `UI/`, `Audio/`.

**Empty folders are not created in advance.** Git does not track them and they
would need a placeholder file each, guarding nothing.

### Group by subject, not by asset type

No `Meshes/`, `Textures/` or `Materials/` folders. The prefix already says what
an asset is and the Content Browser filters on it, so a type folder adds path
length and no information. This follows the community guide, which puts it as:
creating a folder named `Meshes` is redundant.

One exception, also from that guide: fifteen or more closely related assets of
one kind may get their own folder. In practice that means animation. So
`Enemies/Demonic/Brute/Animations/` is fine once there are enough of them, and
`Enemies/Demonic/Brute/Meshes/` is not.

### Enemies split by Cataclysm, then by enemy

```
game/Content/Enemies/Demonic/Brute/
```

The Cataclysm folder names are the eight `Element.*` gameplay tag names, spelled
exactly as they appear in `game/Config/Tags/CataclysmTags.ini`: Celestial,
Chaos, Death, Demonic, Famine, Pestilence, Void, War.

Eight Cataclysms with roughly seven enemies each is around fifty-six leaf
folders, so the middle level earns its place from the start. `Characters/` gets
the same shape when it exists, because 24 classes divide the same way.

### There is no `Content/Cataclysm/` wrapper folder

The community guide's headline rule is that everything a project owns lives
under one folder named after the project. **This project does not do that**, for
three reasons:

1. Its stated purpose is migration safety — so that migrating content into
   another project cannot overwrite same-named assets. This project will never
   migrate its content into another project, so the benefit does not apply.
2. Adopting it would move every existing asset and require editing every
   hard-coded `/Game/` path in the C++, the generators, the config and the
   tests. Continuous integration does not run the C++ tests, so a missed path
   would ship silently.
3. The guide defers to a project's existing conventions where it has them, and
   Epic's own Lyra does not use a wrapper either.

The consequence is accepted deliberately: without a project folder, "not in the
project folder" cannot be the signal for third-party content. The next section
replaces that signal with an explicit list.

## Third-party asset packs

**They stay where Fab installs them, at the root of `Content/`, and out of git.**

Fab installs to the Content root and offers no way to choose a destination.
Moving a pack afterwards leaves redirectors behind and risks breaking in-place
updates, which is a cost paid on every pack update to buy a folder name.

The list of vendor folders lives in exactly one place: the block in `.gitignore`
between `# THIRD-PARTY-PACKS-BEGIN` and `# THIRD-PARTY-PACKS-END`.
`tools/third_party_content.py` reads it, and the tests that depend on it read
that. **To add a vendor, add one line to that block.** Nothing else needs
changing.

### Derived work never goes inside a vendor folder

A retargeted animation, an animation Blueprint, a material instance made from a
pack's material — all of that is this project's own work and belongs in git.

Unreal's retargeting operation copies rather than edits: the editor calls it
"Duplicate Anim Assets and Retarget" and writes the copy to a folder you choose.
So the destination is a free choice, and the vendor folder is the one choice
that loses the work.

**This is the rule that fails silently.** An asset saved inside
`game/Content/ParagonRampage/` is dropped by `git add` with no error and no
warning, and the loss surfaces on somebody else's clone. It is guarded by
`tools/tests/test_third_party_packs_are_not_committed.py`, which checks both
that vendor folders are ignored and that `Enemies/Demonic/Brute/` is not.

### An asset here may reference an asset that is not in git

A Blueprint in `Enemies/Demonic/Brute/` that points at a Paragon mesh will not
resolve on a clone without the packs installed. That is inherent to referencing
by path, not a flaw in this layout. The Brute's C++ handles the same case by
resolving its mesh as a soft path at run time and falling back to a primitive
shape with a logged warning; Blueprints cannot degrade as cleanly. **Treat
"the Paragon packs are installed" as a documented prerequisite for opening the
project**, recorded in `game/docs/enemy-source-assets.md`.

## Naming

Base pattern: `Prefix_AssetName_Variant`. Numbered variants are two digits from
`01`.

| Asset type | Prefix | Example |
|---|---|---|
| Blueprint | `BP_` | `BP_Brute` |
| Blueprint Interface | `BPI_` | `BPI_Damageable` |
| Blueprint Function Library | `BPFL_` | `BPFL_Cataclysm` |
| Widget Blueprint | `WBP_` | `WBP_HealthBar` |
| Skeletal Mesh | `SK_` | `SK_Brute` |
| Skeleton | `SKEL_` | `SKEL_Brute` |
| Static Mesh | `SM_` | `SM_CityWall` |
| Physics Asset | `PHYS_` | `PHYS_Brute` |
| Physical Material | `PM_` | `PM_Stone` |
| Animation Sequence | `AS_` | `AS_Brute_Jog_Fwd` |
| Animation Montage | `AM_` | `AM_BruteSlam` |
| Animation Blueprint | `ABP_` | `ABP_Brute` |
| Blend Space | `BS_` | `BS_BruteLocomotion` |
| Aim Offset | `AO_` | `AO_BruteAim` |
| Material | `M_` | `M_BruteBody` |
| Material Instance | `MI_` | `MI_BruteBody_Molten` |
| Material Function | `MF_` | `MF_Dissolve` |
| Post Process Material | `PP_` | `PP_LowHealth` |
| Texture | `T_` plus a suffix | `T_BruteBody_D`, `T_Rock_N` |
| Niagara System | `NS_` | `NS_SlamImpact` |
| Niagara Emitter | `NE_` | `NE_Embers` |
| Gameplay Ability | `GA_` | `GA_BruteStomp` |
| Gameplay Effect | `GE_` | `GE_Stunned` |
| Behavior Tree | `BT_` | `BT_Brute` |
| Blackboard | `BB_` | `BB_Enemy` |
| Data Table | `DT_` | `DT_ItemBases` |
| Data Asset | `DA_` | `DA_InputConfig` |
| Input Action | `IA_` | `IA_SlotHeavy` |
| Input Mapping Context | `IMC_` | `IMC_MouseMovement` |
| Level | `L_` | `L_Sandbox` |

Texture suffixes: `_D` base colour, `_N` normal, `_R` roughness, `_M` mask,
`_AO` ambient occlusion, `_ORM` packed occlusion-roughness-metallic. **`_M` means
mask, not metallic.** The community guide assigns `_M` to both; metallic appears
only inside packed suffixes here.

### Where this departs from a source, and why

**`SM_` for static meshes, not the community guide's `S_`.** The guide concedes
the deviation itself, noting that many projects use `SM_`. There are already 321
`SM_` static meshes on disk from the Paragon packs and zero `S_`, so `S_` would
make this project's own meshes look foreign in its own Content Browser.

**`AS_` for animation sequences, not the guide's `A_`.** `A_` is ambiguous
beside `AM_`, `AO_` and `ABP_`.

**`L_` for levels.** The community guide gives levels no prefix and role
suffixes instead; Epic's table has no Level row at all. There is no convention to
conform to, only an absence, and `L_Sandbox` already exists and is referenced
from `game/Config/DefaultEngine.ini` and from tests. When sub-levels appear, layer
the guide's suffixes on top: `L_Capital_P`, `L_Capital_Lighting`.

**`GA_`, `GE_`, `IA_`, `IMC_`, `NS_`, `NE_` come from Lyra**, which is the only
source that has faced those asset types. `IA_` and `IMC_` are already what this
project uses.

**Nothing existing is renamed.** Every prefix already in use in
`game/Content/Data/` and `game/Content/Input/` is kept.

## Three things that are easy to get wrong

1. **Derived work never goes inside a vendor folder.** It is dropped by git
   without a word.
2. **Every level goes in `Maps/`**, whatever it is for.
3. **Folder names under `Enemies/` are the eight Cataclysm tag names verbatim**,
   from `game/Config/Tags/CataclysmTags.ini`. Not abbreviations, not plurals.
