# Source art for the equipped weapon

Which mesh is drawn in the character's hand for each of the fourteen weapon
bases, and the measurements behind each choice.

Written 2026-09-01 for issue #1125, which drew a weapon in the player's hand for
the first time. Before it, a player could equip a Greataxe, see its stats and its
six skills change, and see nothing at all change on screen.

**Everything measured here was read through the editor** on 2026-09-01 from the
assets in `game/Content/Medieval_Weapons/` and
`game/Content/Characters/Mannequins/`. It is not an estimate.

## The pack

**Dark Fantasy Weapons** by Hivemind, from the project owner's Fab library,
installed 2026-09-01. It lands as `game/Content/Medieval_Weapons/`, not under the
product name.

**2.7 GB, and `.gitignore` excludes it**, along with the two
`__ExternalActors__` subfolders its demo levels use — about 900 further files.
That is the same treatment the Paragon packs get and for the same reason: it is
third-party content that anyone who owns it can re-acquire, and committing it
would put gigabytes through Git LFS. This file is the durable record instead.

**It ships meshes and poses, no animations.** Its own listing says it "comes
ready with poses ... so you can quickly connect your favorite animation packs".

### Every weapon mesh in it

From `game/Content/Medieval_Weapons/Meshes/VOL2/`. Sizes are X by Y by Z in
centimetres, from each mesh's `ApproxSize` asset registry tag.

| Mesh | Size | Triangles |
|---|---|--:|
| `SM_Sword_1` | 18 x 4 x 88 | 17,907 |
| `SM_Sword_2` | 22 x 4 x 84 | 6,374 |
| `SM_Sword_3` | 23 x 3 x 93 | 8,114 |
| `SM_Dagger` | 6 x 2 x 34 | 4,300 |
| `SM_Axe` | 37 x 7 x 91 | 25,780 |
| `SM_Mace` | 18 x 17 x 83 | 11,807 |
| `SM_Scythe` | 37 x 8 x 82 | 13,402 |
| `SM_Spear` | 18 x 4 x 244 | 6,550 |
| `SM_Staff` | 17 x 9 x 106 | 10,194 |
| `SM_Shield_1` | 60 x 13 x 76 | 9,313 |
| `SM_Shield_2` | 67 x 14 x 69 | 6,584 |
| `SM_Shield_3` | 57 x 25 x 90 | 6,963 |
| `SM_Bow` | 32 x 10 x 122 | 10,776 |
| `SM_Crossbow` | 89 x 85 x 12 | 12,993 |
| `SM_ThrowingAxe` | 32 x 5 x 57 | 2,694 |

Rigged versions of the bow and crossbow also ship — `SKM_Bow`, `SKM_Crossbow`,
`SK_Bow`, with physics assets `PA_Bow` and `PA_Crossbow`. Nothing uses them; the
static meshes are what a hand holds.

**The product listing undercounted.** It named two swords and two shields. There
are three of each.

## The measurement that decided the mapping

**The character is 180.5 cm tall.** Read from `SKM_Manny_Simple`'s reference-pose
bounds: origin Z 90.25, box extent Z 90.27, so the mesh spans 0 to 180.5. Its
collision capsule is 96 cm half-height, so 192 cm.

**Against that, the pack has no two-handed melee weapon.** The three swords are
84 to 93 cm, the axe 91 and the mace 83 — all roughly half the character's
height, which is hip to shoulder, which is a one-handed weapon. Only the spear at
244 cm and the staff at 106 are two-handed as authored.

Six of the fourteen bases are two-handed. Four of those six therefore draw a
one-handed mesh scaled up.

## What each base draws

The table is the Weapon Meshes sheet of `docs/All_Things_Cataclysm.xlsx`, which
becomes `game/Data/WeaponMeshes.csv` and then `DT_WeaponMeshes`.

| Base | Hands | Mesh | Scale | Drawn length | Why |
|---|--:|---|--:|--:|---|
| Sword | 1 | `SM_Sword_1` | 1.00 | 88 cm | exact |
| Dagger | 1 | `SM_Dagger` | 1.00 | 34 cm | exact |
| Axe | 1 | `SM_Axe` | 1.00 | 91 cm | exact |
| Shield | 1 | `SM_Shield_1` | 1.00 | 76 cm | exact, three to choose from |
| Crossbow | 1 | `SM_Crossbow` | 1.00 | 89 cm wide | exact |
| Spear | 2 | `SM_Spear` | 1.00 | 244 cm | already two-handed |
| Staff | 2 | `SM_Staff` | 1.00 | 106 cm | already two-handed |
| Greatsword | 2 | `SM_Sword_3` | 1.45 | 135 cm | the longest sword, scaled |
| Greataxe | 2 | `SM_Axe` | 1.45 | 132 cm | the one-handed axe, scaled |
| Warhammer | 2 | `SM_Mace` | 1.45 | 120 cm | the mace, scaled |
| Two-Handed Crossbow | 2 | `SM_Crossbow` | 1.25 | 111 cm wide | the same crossbow, larger |
| Fist | 1 | none | — | — | **unarmed draws nothing, by design** |
| Wand | 1 | none | — | — | nothing suitable in the pack |
| Whip | 1 | none | — | — | nothing suitable in the pack |

### Why the scale figures are what they are

**A judgement, and expected to be tuned by looking at the game.** They are in the
data rather than in C++ so that changing one costs no rebuild.

Real two-handed weapons run longer than 1.45 would give: a zweihänder is 150 to
180 cm, a Dane axe 120 to 170. Scaling further was rejected because **uniform
scaling thickens the grip as well as lengthening the blade**, and a 1.8x handle
does not fit in a hand. 1.45 reads as a bigger weapon while keeping the grip
plausible.

**The Greataxe uses the axe rather than the scythe.** Issue #1125 proposed the
scythe as an alternative and answered its own question: a scythe is not an axe. A
scaled axe is still recognisably an axe, and recognising the weapon matters more
than avoiding the scale.

### Three bases draw nothing, and that is recorded rather than left blank

The workbook writes the word `None` in the Mesh column rather than leaving the
cell empty, and `tools/generate_datatables.py` refuses a blank cell. That is so a
base nobody filled in can be told apart from a weapon that is meant to be
invisible — issue #1125 asked for exactly that: "Drawing nothing is a reasonable
answer; drawing nothing silently is not."

**The Fist is a design decision and the other two are a gap in the art.** Unarmed
should show no weapon. The Wand and the Whip have nothing suitable in this pack,
so buying another pack should fix two of these three and must not fix the first.

### Four pack meshes go unused

`SM_Sword_2`, `SM_Shield_2`, `SM_Shield_3`, `SM_Scythe`, `SM_Bow`,
`SM_ThrowingAxe` and the two arrows. **The design has no Bow base** — it has
Crossbow and Two-Handed Crossbow only. Whether a Bow should exist is a question
for the project owner rather than something to invent here; it is raised on
issue #1125.

## Where it attaches

**`SK_Mannequin` already ships the sockets and neither had to be authored.**

| Socket | Bone |
|---|---|
| `HandGrip_R` | `hand_r` |
| `HandGrip_L` | `hand_l` |

It also carries `weapon_r_muzzle` on a `weapon_r` bone, for the firearm animation
sets this project does not use.

`ECataclysmGearSlot::Weapon1` draws in the right hand and `Weapon2` in the left.

**A two-handed weapon draws in the right hand only, and that is a limitation
rather than a decision.** `UCataclysmEquipmentComponent` puts a two-handed weapon
in Weapon1 and blocks Weapon2, so the left hand is empty. Making both hands hold
it needs a two-handed grip pose, and there is none in anything this project owns.

## The Paragon packs hold no weapons to borrow

Asked by the project owner on 2026-09-01. Checked across all six packs already in
`game/Content/`: every hero's weapon is skinned into the hero's own body mesh
rather than being a separate asset.

| Pack | Mesh assets under `Characters/**/Meshes/` | What they are |
|---|--:|---|
| ParagonGrux | 14 | `Grux`, six skin variants, shadow and extents proxies |
| ParagonSevarog | 8 | `Sevarog`, skin variants, proxies |
| ParagonCountess | 11 | `SM_Countess`, skin variants, physics assets |
| ParagonRampage | 12 | `Rampage`, variants, and `SM_Rock_To_Hold` |
| ParagonIggyScorch | 12 | `IggyScorch`, skin variants, proxies |
| ParagonMinions | 56 | minion bodies |

**`SM_Rock_To_Hold` is the only separately-held prop in all six packs**, and it is
the rock `ACataclysmBruteCharacter` already carries. When Epic did ship a
holdable prop they named it plainly and put it in the hero's mesh folder, and
there is exactly one of them. Searching for standalone sword, axe, hammer or
blade meshes returns only visual-effect shapes — `SM_Countess_SwordAbstractMesh`
and `SM_Sevarog_WeaponEnergy` are particle meshes, not weapons.
