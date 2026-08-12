# Animation Plan

How much animation this game needs, and what decides whether two skills share a
motion. Confirmed by the project owner on 2026-08-12. It answers issue #18.

**Nothing here is built.** The only animation work that exists is on the seven
Demonic enemies, which are dressed from Paragon packs and retargeted per creature.
This is the rule player animation is built to.

---

## The rule

### Tier 1 — the body motion is decided by weapon and slot, not by damage type

**One animation set per weapon-and-slot combination, shared across all eight
damage types.** A Greataxe Heavy attack is one physical motion, used by Demonic,
War, Death, Pestilence, Famine, Celestial, Chaos and Void alike.

Measured from the Weapon Skills sheet of `All_Things_Cataclysm.xlsx` on
2026-08-12:

| | Count |
| :-- | --: |
| Weapon types | 15 |
| Slots | 6 |
| **Weapon and slot combinations** | **71** |
| Skill rows in the sheet | 398 |
| Reduction from sharing by weapon and slot | **5.61 times** |

The 15 weapon types are 2H Crossbow, All, Axe, Crossbow, Dagger, Fist, Greataxe,
Greatsword, Shield, Spear, Staff, Sword, Wand, Warhammer and Whip. The 6 slots are
Aura, Heavy, Movement, Special, Support and Ultimate. Aura appears only under the
weapon type "All", so auras are weapon-agnostic.

### Tier 2 — damage type identity comes from everything except the skeleton

Particle effects, audio, projectile and deployable meshes, impact and hit
reactions, and above all **what the skill actually does**. A Demonic heavy attack
leaving burning ground and a Death heavy attack draining life are different skills
sharing a swing.

### Tier 3 — a reserve of unique animations for the moments identity matters most

Roughly **three signature weapons per damage type, about 24 animations**, most
likely spent on Ultimates.

### The total

**Roughly 135 to 150 animations for the full game**, against 398 if every skill
row were bespoke.

---

## Why sharing costs nothing the design had not already spent

The concern the project owner raised on 2026-08-02, when approving re-use, was
that classes must not end up feeling like the same thing in a different colour.

**Classes were never differentiated by their skills.** Skills are determined by
weapon type plus damage type, and classes come from damage type — three classes
per damage type, 24 in total. So the Ravager, the Ritualist and the Masochist are
all Demonic and all draw from the same Demonic skill pool. Sharing a Demonic
Greataxe animation between them is not a compromise; **they already share the
skill itself**.

What makes those three feel different is the passive class tree and the class
resource, which the design already treats as the engine of the build. The
Masochist uses health instead of mana and its resource is Anguish. None of that
lives in the attack animation.

**So the risk the condition names is about differentiating the eight damage types,
not the 24 classes**, and tier 2 is where that work happens.

---

## How to test the rule before spending anything on tooling

**Two damage types are now fully designed**, which was not true when this rule was
proposed:

| Damage type | Rows | Skills designed |
| :-- | --: | --: |
| War | 61 | 61 |
| Demonic | 51 | 51 |
| The other six | 286 | 0 |

Tier 1 claims a Demonic Heavy attack on a Greataxe and a War Heavy attack on a
Greataxe are the same physical motion. **Both of those skills exist, written out,
with tags and shapes.** So the rule can be checked by reading rather than by
building anything:

> For each weapon-and-slot combination where both a War skill and a Demonic skill
> are designed, do the two descriptions imply the same body motion?

Where they do, tier 1 holds. Where they do not — one skill rooting the character
against one that lunges, a channel against a single swing — that pair is evidence
against the rule, and the count of such pairs is the answer to how far the rule
can be trusted.

**That is a few hours of reading 112 skill descriptions. It needs no tooling, no
art and no spend, and it should happen before any animation tool is bought.**

---

## What is still open

**The player skeleton standard.** Everything downstream depends on it and changing
it later invalidates every animation made. Not yet decided.

The recommendation is the **Unreal mannequin**, because it is the retargeting hub
the whole Unreal ecosystem targets, including Paragon content. This project is
already committed to retargeting: `game/docs/enemy-source-assets.md` records that
every Paragon character carries its own skeleton, with bone counts from 39 to 207,
so "nothing here shares an animation with anything else without retargeting".
Choosing anything else means a custom retarget path for every third-party source.

That is a judgement rather than something research settles, and it is the kind
that is cheap now and expensive later.

**Tooling.** Cascadeur is untested here; its 2026.1 release added Unreal Live Link
and root motion, which is the relevant capability. Meshy's bundled preset
animations overlap issue #17, so if Meshy is ever chosen for models that decision
partly resolves itself. Neither is urgent, and the reading check above should come
first.

---

## Two figures in issue #18 that were wrong

Recorded so they are not carried forward.

**It says 558 skill rows and a 7.9 times reduction.** The sheet holds **398** rows,
so the reduction is **5.61**. The 71 combinations the rule depends on are correct.

**Its closing note says Phase 1 proves the rule with War skills across three
weapon types.** Phase 1 uses **Demonic** skills. Issue #61 settled that the
Cataclysm determines the player's damage type and the slice targets Demonic, and
issue #37 is titled accordingly. The reasoning in that note survives; only the
damage type named in it is wrong.
