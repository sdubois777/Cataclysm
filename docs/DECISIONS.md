# Design decisions

Decisions made outside the Google Drive documents, newest first.

**The files in this folder are authoritative.** Edit them directly. They began as
exports from Google Drive, but as of 2026-08-02 the repository copies are the
source of truth and are not synced back; treat the Drive originals as historical.

This log records the *reasoning* behind decisions, which the design documents
themselves do not carry. Each entry names which document it affects, so the
change can be applied there.

Entries below dated 2026-08-02 were written before that switch and carry wording
about folding changes into Drive. Ignore that wording; it is obsolete.

**All six of those decisions have now been applied** to
`Cataclysm_GDD_v2.md` and `All_Things_Cataclysm.xlsx`. The "Affects" line on each
entry records where the change landed. Later entries should say whether they are
applied or still pending.

---

## 2026-08-02 — The Belt has 4 gem sockets

**Decision.** Add a Belt row to the socket table with **4 sockets**.

**Why.** The socket table in `Cataclysm_GDD_v2.md` section VI sums to 41, but the
same section states the total is 45. The Belt appears in the item slot list
(Head, Chest, Shoulders, Gloves, Pants, Boots, Belt) and has no row in the socket
table. Four sockets on the Belt makes the total exactly 45.

The 45 figure is the one to preserve, because the expected player Power Score
maths — including the per-tier anchors in `sim/cataclysm_sim/scoring.py` — was
derived assuming 45 sockets. Changing the total would invalidate those anchors.

Socket count after this decision and the quiver removal below:

| Slot | Sockets |
|---|---|
| Chest | 6 |
| Pants | 4 |
| Relic | 4 |
| **Belt** | **4** |
| Helmet, Shoulders, Gloves, Boots | 2 each |
| Rings (×8), Necklace | 1 each |
| Potion slots (×4) | 1 each |
| Weapons | 6 (a two-handed weapon, or two one-handed weapons at 3 each) |
| **Total** | **45** |

**Affects:** `Cataclysm_GDD_v2.md` section VI socket table. **Applied 2026-08-02:**
Belt row added with 4 sockets, total confirmed at 45.

---

## 2026-08-02 — Quivers and offhands are removed

**Decision.** Quivers are removed from the game. There is no offhand slot.

Quivers were never really weapons; they were an extra gear piece providing related
stats and gem sockets. Ranged two-handed weapons now behave like every other
two-handed weapon and carry **6 gem sockets**.

**Affects:** `Cataclysm_GDD_v2.md` section VI. **Applied 2026-08-02:** the
"Offhands: Quivers" line and the "Offhands | 3" socket row are both removed, and
the weapon type list now states there are no offhand items.

**Consequence for socket totals.** With the offhand row gone, a two-handed weapon
gives 6 sockets and two one-handed weapons give 3 + 3 = 6, so the weapon
contribution is 6 either way. See the open question on the Belt below.

---

## 2026-08-02 — Dual wielding, damage types, and skill selection

**Decision.** Dual wielding exists. The rules:

- A player may equip either one weapon or two.
- **A single weapon can carry multiple damage types.**
- The set of damage types across **all** equipped weapons determines what the
  player has access to.
- Every damage type present unlocks its **three** class trees. Four damage types
  across the player's weapons unlocks 12 classes.
- Every damage type present also unlocks every skill matching the combination of
  an equipped **weapon type** and that **damage type**.
- **The player does not get a button for every available skill.** They choose
  which skills to use from the available pool and assign them to hotkeys.

**Affects:** `Cataclysm_GDD_v2.md` sections IV and V. **Applied 2026-08-02:** a
"Dual Wielding and Damage Types" subsection was added to section VI, and the
Skill Slots text in section IV was rewritten to say the weapons determine the
pool rather than the contents of each slot.

This is a meaningful clarification of the skill slot system. The design document
says "Each player has six skill slots. The skills available in each slot are
determined by the combination of weapon type and damage type." That reads as the
weapon fixing the contents of each slot. It actually means the weapon and damage
types determine the **pool**, and the player builds a loadout from that pool.

The design document already supports multiple damage types per weapon — section IV
says "Players with multiple damage types on their weapon can invest in multiple
class trees simultaneously" — but never states it as a rule of itemization.

---

## 2026-08-02 — Weapon availability per damage type

**Decision.** The table below is approved provisionally. Expect to revise it as
classes are fleshed out.

| Weapon | War | Demonic | Death | Pestilence | Famine | Celestial | Chaos | Void |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Sword | Y | Y | Y | Y | Y | Y | Y | · |
| 2H Sword | Y | Y | Y | · | · | Y | Y | Y |
| Dagger | Y | Y | Y | Y | Y | · | Y | Y |
| Axe | Y | Y | · | · | Y | · | Y | · |
| 2H Axe | Y | Y | Y | · | · | · | Y | · |
| Spear | Y | · | Y | Y | · | Y | Y | Y |
| Fist | Y | Y | Y | Y | Y | · | Y | Y |
| Shield | Y | · | · | · | · | Y | Y | · |
| Whip | Y | Y | Y | Y | Y | · | Y | Y |
| Crossbow | Y | · | · | Y | · | Y | Y | · |
| 2H Crossbow | Y | · | · | Y | · | · | Y | · |
| 2H Warhammer | Y | Y | · | · | Y | Y | Y | Y |
| Wand | · | Y | Y | Y | Y | Y | Y | Y |
| Staff | · | Y | Y | Y | Y | Y | Y | Y |
| **Weapons** | **12** | **10** | **9** | **9** | **8** | **8** | **14** | **8** |

War is unchanged from the design document. Chaos is all 14, which the design
document already stated. The other six rows were derived from the three class
identities of each damage type, so that every class has weapons that suit it.

Verified: all 24 classes have at least two available weapons, and no weapon is
unused by every damage type.

**Effect on scope:** the Weapon Skills sheet drops from 558 rows to 398, a 29%
reduction. The animation count is unaffected at 71 shared sets, because animation
follows weapon and slot rather than damage type.

**Affects:** `Cataclysm_GDD_v2.md` section V, and the Weapon Skills sheet in
`All_Things_Cataclysm.xlsx`. **Applied 2026-08-02:** the six TBD rows are filled
in, and the Weapon Skills sheet is pruned from 558 rows to 398. No row carrying a
designed skill was removed; all 61 War skills survive.

---

## 2026-08-02 — Animation re-use, three tiers

**Decision.** Animation is shared across damage types, on three tiers:

1. **Shared motion, 71 animation sets.** The physical animation is determined by
   weapon type and slot. One two-handed axe heavy attack, used by all eight
   damage types.
2. **Damage type identity comes from everything except the skeleton.** Effects,
   audio, projectile and deployable meshes, impact reactions, and above all what
   the skill mechanically does.
3. **Signature animation budget**, roughly three unique animations per damage
   type for identity moments, mostly Ultimates. About 24 in total.

Estimated total: 135–150 animations, against 558+ if every skill were bespoke.

This is a constraint on skill **design**, not only on animation production. Two
skills sharing a weapon and a slot share a motion, so what distinguishes them
must be what they do.

**Affects:** the animation pipeline, and how every skill is written.

---

## 2026-08-02 — The Cataclysm determines the player's damage type

**Decision.** The Cataclysm being fought is the player's damage type. Fighting the
Demonic Cataclysm biases drops toward Demonic-tuned weapons, and because weapons
determine both skills and available class trees, that is what unlocks the Demonic
classes.

**Consequence.** The Phase 1 vertical slice targets the Demonic Cataclysm, so it
needs Demonic player content, not the War content the roadmap currently names.

**Affects:** `Cataclysm_GDD_v2.md` sections IV, VI and XV. **Applied 2026-08-02:**
the rule is stated in the Game Start section, and the Phase 1 roadmap now names
Demonic / Masochist and Demonic skills rather than War / Bulwark and War skills.

---

## 2026-08-02 — Engine version

**Decision.** Unreal Engine **5.8.1** (`++UE5+Release-5.8`, changelist 56057345).

Chosen over the already-installed 5.7 because the project is at day zero, 5.8 is
the last planned major Unreal Engine 5 release before Epic moves to UE6, and
migrating an Unreal project between engine versions later is real work.

Unreal Engine 5.7 remains installed and takes 74 GB. Uninstall it once 5.8 is
confirmed working.
