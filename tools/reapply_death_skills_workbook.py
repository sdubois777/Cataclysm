"""Write the Death weapon skills into the design workbook, and regenerate the CSVs. Issue #2885.

    python tools/reapply_death_skills_workbook.py              # write the cells, then run tools/generate_datatables.py
    python tools/reapply_death_skills_workbook.py --check      # say what would change, write nothing
    python tools/reapply_death_skills_workbook.py --workbook COPY.xlsx --no-generate    # a copy, nothing regenerated

WHY THIS EXISTS. `docs/All_Things_Cataclysm.xlsx` is authoritative and is a binary git cannot merge. The Death skills
are built in slices on `feat/death-skills-2885` while other branches hold workbook edits of their own, so the branch
records its edit as this script rather than as the binary: whichever workbook development holds when the branch lands,
this is run on it and redoes the Death cells, and nothing else. The owner approved the 40 drafted Death skills and the
five Death one-handed Crossbow skills on 2026-11-24 (issue #2885); `docs/DECISIONS.md` has the entry.

WHAT IT WRITES, every cell found by its row's Weapon Type, Damage Type and Slot (Weapon Skills) or its Minion Type
(Minion Types), never by position, so rows another branch added or moved do not matter:

- WEAPON SKILLS, THE 40 EMPTY DEATH ROWS: Skill Name, Skill Description, Tags, Shape, Shape Params, Crit Chance, Damage
  Percent, Cooldown, Mana Cost and Fervour Cost.
- WEAPON SKILLS, FIVE NEW DEATH CROSSBOW ROWS, appended below the sheet's last row with their Weapon Type, Damage Type
  and Slot as well: the coordinating session's judgement of 2026-11-24 under the owner's delegation gives Death the
  one-handed Crossbow (issue #2885, comment 7149138032).
- MINION TYPES, the kinds the rows built here raise: `Shade` (Wailing Host) and `Husk` (Shallow Graves) since slice
  DS1, and `BarrowGuard` (Barrow Guard), `BoneReaper` (Exhume) and `SkeletalMage` (Bone Choir) since slice DS2.

A ROW IS WRITTEN WHOLE ONLY WHEN THE CODE IT NEEDS IS BUILT. `BUILT` names the code pieces this branch has built
(the drafts' N1-N22 and the Crossbow draft's C1-C4) and the rulings made since (`KNOWN_PIECES` says which is which);
a row whose `needs` are all in it gets its Shape and Shape Params. Every other row gets its name, description, tags and
numbers and an EMPTY Shape and Shape Params, so the game grants it as the designed placeholder that does nothing yet
(`UCataclysmUndesignedSkill`) rather than a shape that would run with a key it does not read. A later slice builds a
piece, adds it to `BUILT` and runs this again: the rows waiting on only that piece gain their shape, and nothing
already written moves.

WHAT IT REFUSES. A workbook holding formulas, charts or images, because an openpyxl save can drop a chart or an image
and empty a formula without saying so; a sheet missing a column this writes; a Death row missing from the sheet (the 40
were in the matrix before), or found twice; and a cell holding neither the value it had before issue #2885 nor the
value this writes, which means somebody else changed it and a person has to look. It reads the file back from disk and
refuses if any cell other than the ones it meant to write changed. A cell holding what an EARLIER SLICE of this script
wrote there, before a ruling changed it, is its own and is rewritten (`SUPERSEDED`): the four ranges the coordinating
session's ruling of 2026-11-24 brought down to 14 m.

Run against a workbook already carrying the edit, it reports nothing to write and writes nothing, which is its
positive control. AFTERWARDS the DataTable assets need the editor step,
`python tools/run_editor_python.py tools/generate_datatable_assets.py`, as any change to a generated CSV does:
`DT_WeaponSkills` and `DT_MinionTypes`.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from typing import NamedTuple

import openpyxl
from openpyxl.utils import get_column_letter

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"

SKILLS_SHEET = "Weapon Skills"
MINIONS_SHEET = "Minion Types"
DAMAGE_TYPE = "Death"

#: The code pieces this branch has built, by the drafts' own numbering. Slice DS1, 2026-11-24: N22 (a movement lays
#: its row's `Effect`) and C3 (`Taunts=1`, a minion or a placed machine taunting the enemies near it). Slice DS2, the
#: same day: N16 (Bleed on a skill row, the owner's set widened), N1 (`Permanent=1`, a raise that stands until it is
#: destroyed), N2 (a summon that states its own damage strikes as it raises), N3 (`Detonates=1`, the caster's minions
#: in the skill's area explode), N4 (`CommandStrike=1` on a strike) and N11 (`MinionsLayEffect=1`, minions that lay the
#: row's effect with their blows). Slice DS3, the same day: N6 (`KillFervour`, Fervour for a kill or a death under the
#: skill's mark), N7 (`ScalingSource=Fervour`, the Fervour held), N8 (`HoldsFervour=1`, a buff holding Fervour from
#: decay), N10 (a flicker its deaths lengthen, re-gathering its circle) and N5 (`OnDeath=Rise; RisesAs=<kind>`, a curse
#: under which the dying rise).
BUILT = frozenset({"N22", "C3", "N16", "N1", "N2", "N3", "N4", "N11", "N6", "N7", "N8", "N10", "N5"})

#: Every code piece a row may need, so a misspelt one in `needs` is refused rather than read as never built.
#: N1-N22 are `death_weapon_skills_draft.md`'s (N20 was dropped on 2026-11-24 and is not one), C1-C4 the Death Crossbow
#: draft's. `DeployableReserve` was found while entering the Effigy on 2026-11-24: `UCataclysmDeployableSkill::DeployOne`
#: reserves no Fervour (the no-War rule, issue #1934), so a Death deployable stating `FervourReserve` would not hold it.
#:
#: `SentinelReach` WAS A RULING AND NOT CODE, AND IS RULED: the coordinating session's ruling of 2026-11-24, "Range:
#: 14 m", keeps the Corrupted Sentinel's reach the longest range any player attack has
#: (`test_the_sentinel_reach_is_the_longest_range_any_player_attack_has`, issue #352), so None Spared and Coffin Nail
#: fall from 17 m to 14 and Soul Lance and Pale Rider from 16 m to 14. No row waits on it, and it is no longer a piece.
KNOWN_PIECES = frozenset({f"N{n}" for n in range(1, 23) if n != 20}
                         | {"C1", "C2", "C3", "C4", "DeployableReserve"})

#: The Weapon Skills sheet's columns this reads or writes, by header.
WEAPON, DAMAGE, SLOT = "Weapon Type", "Damage Type", "Slot"
NAME, DESCRIPTION, TAGS = "Skill Name", "Skill Description", "Tags"
SHAPE, PARAMS = "Shape", "Shape Params"
CRIT, DAMAGE_PERCENT, COOLDOWN, MANA, FERVOUR = "Crit Chance", "Damage Percent", "Cooldown", "Mana Cost", "Fervour Cost"
SKILL_COLUMNS = (WEAPON, DAMAGE, SLOT, NAME, DESCRIPTION, TAGS, SHAPE, PARAMS, CRIT, DAMAGE_PERCENT, COOLDOWN, MANA,
                 FERVOUR)

#: The weapons Death gained with issue #2885. Their rows did not exist, so they are appended rather than found.
NEW_WEAPONS = frozenset({"Crossbow"})

#: What the Tags cell of a pre-existing Death row held before issue #2885. Every other cell of those 40 rows was empty,
#: and so was this one for the 35 rows not named here.
TAGS_BEFORE = {("Sword", slot): "Item.Weapon.Sword, Element.Death"
               for slot in ("Support", "Heavy", "Ultimate", "Movement", "Special")}

#: CELLS AN EARLIER SLICE OF THIS SCRIPT WROTE AND A RULING SINCE CHANGED, by row and column: what slice DS1 wrote there.
#: The script treats each as its own and rewrites it, where any other value it did not write is refused. The coordinating
#: session's ruling of 2026-11-24, "Range: 14 m", brought four ranges down to the Corrupted Sentinel's 14 m reach, which
#: no player attack may out-range (issue #352); their descriptions say the range, so each is listed here. Their Shape
#: Params cells were empty in DS1 (the four waited), so the cell's value before issue #2885 already covers them.
SUPERSEDED: dict[tuple[str, str], dict[str, object]] = {
    ("Crossbow", "Ultimate"): {DESCRIPTION: (
        'Take aim at everything within 17 meters and loose 12 bolts over 1.5 seconds, each at the next enemy in turn, '
        'so the bolts are shared out across a crowd and a lone enemy takes all twelve. Every bolt deals 35% weapon '
        'damage and is a critical strike.')},
    ("Crossbow", "Heavy"): {DESCRIPTION: (
        'Drive a coffin nail into the first enemy in a line up to 17 meters away. The nail stays in: for every nail an '
        'enemy carries, your critical strike chance against it is 5% higher, up to 4 nails, and they stay until 4 '
        'seconds pass without a new one.')},
    ("Spear", "Special"): {DESCRIPTION: (
        'Throw the spear through a line up to 16 meters long. It passes through every enemy in the line, and each enemy '
        'it hits gives you 1 Fervour, at most 4 a throw.')},
    ("Spear", "Ultimate"): {DESCRIPTION: (
        'Ride as Death rides. You charge 16 meters in 1.5 seconds and nothing turns you aside: everything in your path '
        'is run through for 300% weapon damage and Crippled, moving and attacking 50% slower for 4 seconds. Every enemy '
        'it kills gives you 5 Fervour.')},
}


class DeathRow(NamedTuple):
    """One Death weapon skill as approved, and the code pieces its shape needs."""

    weapon: str
    slot: str
    name: str
    description: str
    #: Without the `Slot.` tag, which the generator adds from the Slot column (`tags_with_slot`).
    tags: str
    shape: str
    params: str
    crit: float | None = None
    damage: float | None = None
    cooldown: float | None = None
    mana: float | None = None
    fervour: float | None = None
    needs: tuple[str, ...] = ()

    @property
    def waits_on(self) -> tuple[str, ...]:
        """The pieces this row needs that are not built yet. Empty for a row written whole."""
        return tuple(piece for piece in self.needs if piece not in BUILT)

    @property
    def works(self) -> bool:
        return not self.waits_on

    @property
    def csv_name(self) -> str:
        """The row's name in `game/Data/WeaponSkills.csv`, as the generator builds it (`row_name`)."""
        return f"{DAMAGE_TYPE}_{self.weapon.replace(' ', '_')}_{self.slot}"

    def cells(self) -> dict[str, object]:
        """What this writes in each column. A waiting row's Shape and Shape Params are empty."""
        return {NAME: self.name, DESCRIPTION: self.description, TAGS: self.tags,
                SHAPE: self.shape if self.works else None, PARAMS: self.params if self.works else None,
                CRIT: self.crit, DAMAGE_PERCENT: self.damage, COOLDOWN: self.cooldown, MANA: self.mana,
                FERVOUR: self.fervour}

    def cells_before(self) -> dict[str, object]:
        """What each column held before issue #2885: nothing, but a Sword row's Tags."""
        before = {column: None for column in self.cells()}
        before[TAGS] = TAGS_BEFORE.get((self.weapon, self.slot))
        return before

    def cells_superseded(self) -> dict[str, object]:
        """What an earlier slice of this script wrote in a column a ruling has since changed. See `SUPERSEDED`."""
        return SUPERSEDED.get((self.weapon, self.slot), {})


#: The 45 rows: the 40 of `death_weapon_skills_draft.csv` and the five of `death_crossbow_skills_draft.csv`, as the
#: owner approved them on 2026-11-24, with two changes recorded in `docs/DECISIONS.md`: the `Slot.` tag is left to the
#: generator, and Barrow Guard taunts (`Taunts=1` and a sentence saying so), by the ruling of open question 8 that day.
ROWS: tuple[DeathRow, ...] = (
    DeathRow(
        'Sword', 'Support', 'Cold Blood',
        ('Let your blood run cold for 6 seconds. While it lasts your Fervour does not decay, and your hits'
         ' deal 1% more damage for every 4 Fervour you hold.'),
        ('Item.Weapon.Sword, Element.Death, Type.Buff, Stat.Offense.Global, Stat.Resource.ClassResource'),
        'SelfBuff',
        ('Duration=6; MoreDamagePer=0.25; ScalingSource=Fervour; HoldsFervour=1'),
        cooldown=10, needs=('N7', 'N8')),
    DeathRow(
        'Sword', 'Heavy', 'Mercy Stroke',
        ('One slow, deliberate stroke at a single enemy for 300% weapon damage. If the stroke kills, the '
         'sword is ready to strike again at once.'),
        ('Item.Weapon.Sword, Element.Death, Type.Strike, Type.Melee, Trigger.OnKill'),
        'Strike',
        ('Radius=2.5; Angle=60; MaxTargets=1; RefundsCooldown=Self'),
        crit=10, damage=300, cooldown=4, needs=('N9',)),
    DeathRow(
        'Sword', 'Ultimate', 'Danse Macabre',
        ('Dance with death for 4 seconds. You flicker between every enemy within 10 meters, striking each '
         'from behind for 30% weapon damage as you arrive, and nothing can touch you between arrivals. '
         'Every enemy that dies during the dance gives you 5 Fervour and adds half a second to it, up to 8'
         ' seconds in all.'),
        ('Item.Weapon.Sword, Element.Death, Type.Strike, Type.Melee, Stat.Defense.Evasion, '
         'Stat.Resource.ClassResource, Trigger.OnKill'),
        'Movement',
        ('Mode=Flicker; Range=10; Duration=4; Interval=0.33; RearHits=1; Untargetable=1; '
         'DurationPer=0.5; ScalingSource=Kill; MaxDuration=8; KillFervour=5'),
        damage=30, needs=('N6', 'N10')),
    DeathRow(
        'Sword', 'Movement', 'Pale Stride',
        ('Lunge at the first enemy within 12 meters and cut it as you arrive, Crippling it so it moves and'
         ' attacks 30% slower for 3 seconds. If the cut kills, the stride is yours again at once.'),
        ('Item.Weapon.Sword, Element.Death, Type.Strike, Type.Melee, Keyword.Charge, Type.Debuff, '
         'Trigger.OnKill'),
        'Movement',
        ('Mode=Charge; Range=12; Radius=1.5; MaxTargets=1; Requires=Target; RefundsCooldown=Self; '
         'Effect=Cripple; EffectMagnitude=30; EffectDuration=3'),
        needs=('N22',)),
    DeathRow(
        'Sword', 'Special', 'Death Warrant',
        ("Sign an enemy's death warrant from up to 15 meters away. For 10 seconds it is Crippled, moving "
         'and attacking 30% slower. If it dies while the warrant holds, you gain 10 Fervour and the '
         'warrant passes to the nearest enemy within 10 meters.'),
        ('Item.Weapon.Sword, Element.Death, Type.Debuff, Stat.Resource.ClassResource, Trigger.OnDeath, '
         'Type.Ranged'),
        'Debuff',
        ('Range=15; Radius=1.5; MaxTargets=1; EffectDuration=10; Effect=Cripple; EffectMagnitude=30;'
         ' OnDeath=SpreadDebuff; OnDeathRange=10; KillFervour=10'),
        needs=('N6',)),
    DeathRow(
        'Greatsword', 'Support', 'Grave Muster',
        ('Call your dead to muster for 8 seconds. Grave-cold spreads 8 meters around you: enemies within '
         'it are Crippled, moving and attacking 20% slower, and your minions and allies within it deal 25%'
         ' increased damage.'),
        ('Item.Weapon.Greatsword, Element.Death, Type.Buff, Type.AOE.Aura, Type.Debuff, Keyword.Summon, '
         'Stat.Offense.Global'),
        'Aura',
        ('Radius=8; Duration=8; Interval=1; Effect=Cripple; EffectMagnitude=20; '
         'AllyIncreasedDamage=25'),
        cooldown=10, needs=()),
    DeathRow(
        'Greatsword', 'Heavy', 'Gravebell',
        ('Swing the greatsword through a wide arc like a tolling bell, giving Necrosis to everything it '
         'strikes. Your dead answer the bell: every minion of yours the arc passes through explodes, and '
         'every other creature you command strikes the first enemy the arc hit, wherever it is standing.'),
        ('Item.Weapon.Greatsword, Element.Death, Type.Strike, Type.Melee, Type.AOE.PointBlank, '
         'Keyword.DoT.Necrosis, Keyword.Summon'),
        'Strike',
        ('Radius=4.5; Angle=140; Ailment=Necrosis; Detonates=1; CommandStrike=1'),
        needs=('N3', 'N4')),
    DeathRow(
        'Greatsword', 'Ultimate', 'Barrow Guard',
        ('Plant the greatsword and call up two Barrow Guards, skeleton warriors that fight beside you '
         'until they are destroyed and draw the fight onto themselves: every enemy within 5 meters of a '
         'guard turns on it and attacks it, though Bosses are not fooled. They rise in a burst of grave-'
         'cold: every enemy within 5 meters takes 300% weapon damage and is Crippled, moving and attacking'
         ' 30% slower, for 4 seconds. Each guard reserves 10 Fervour while it stands.'),
        ('Item.Weapon.Greatsword, Element.Death, Type.Minion, Type.Summon, Keyword.Summon, '
         'Type.AOE.PointBlank, Type.Debuff, Stat.Resource.ClassResource'),
        'Summon',
        ('Count=2; Radius=5; Minions=BarrowGuard:2; FervourReserve=10; Permanent=1; Effect=Cripple; '
         'EffectMagnitude=30; EffectDuration=4; Taunts=1'),
        damage=300, fervour=20, needs=('N1', 'N2', 'C3')),
    DeathRow(
        'Greatsword', 'Movement', 'Barrow Leap',
        ('Leap to a point up to 10 meters away and land in a burst of grave-cold, Crippling every enemy '
         'within 4 meters so it moves and attacks 30% slower for 3 seconds. Every minion of yours within '
         'it explodes as you land.'),
        ('Item.Weapon.Greatsword, Element.Death, Type.AOE.PointBlank, Type.Debuff, Keyword.Summon'),
        'Movement',
        ('Mode=Leap; Range=10; Radius=4; Effect=Cripple; EffectMagnitude=30; EffectDuration=3; '
         'Detonates=1'),
        needs=('N3', 'N22')),
    DeathRow(
        'Greatsword', 'Special', 'Wailing Host',
        ('Five wailing shades tear out of the ground at a point up to 10 meters away and hurl themselves '
         'at the nearest enemies, which pay them little attention. Each lasts 6 seconds and reserves '
         'Fervour while it is out.'),
        ('Item.Weapon.Greatsword, Element.Death, Type.Minion, Type.Summon, Keyword.Summon'),
        'Summon',
        ('Range=10; Radius=3; Count=5; Duration=6; Minions=Shade:5; FervourReserve=3'),
        needs=()),
    DeathRow(
        'Greataxe', 'Support', 'Death Rattle',
        ('Rattle the bones of up to 8 enemies within 5 meters of a point up to 12 meters away. For 8 '
         'seconds each is Crippled, moving and attacking 20% slower, and any that dies while it rattles '
         'rises at once as a husk, the same husk Shallow Graves raises, that fights for you and turns the '
         'enemies around it on itself. Each husk lasts 8 seconds and reserves Fervour while it stands.'),
        ('Item.Weapon.Greataxe, Element.Death, Type.Debuff, Type.Minion, Keyword.Summon, Trigger.OnDeath, '
         'Type.Ranged'),
        'Debuff',
        # `RisesAs=Husk` IS SLICE DS3'S, a judgement recorded in docs/DECISIONS.md: the approved cell stated
        # `OnDeath=Rise` alone and named the husk in its sentence; the key names it in the data, so the engine reads the
        # husk from the one Death Summon row raising it (Shallow Graves) and the generator can refuse a kind none raises.
        ('Range=12; Radius=5; MaxTargets=8; EffectDuration=8; Effect=Cripple; EffectMagnitude=20; '
         'OnDeath=Rise; RisesAs=Husk'),
        needs=('N5', 'C3')),
    DeathRow(
        'Greataxe', 'Heavy', 'Grim Swath',
        ('Mow a wide swath in front of you for 200% weapon damage, 1% more for every 2 Fervour you hold, '
         'to at most 350%. Every enemy it kills gives you 2 more Fervour.'),
        ('Item.Weapon.Greataxe, Element.Death, Type.Strike, Type.Melee, Type.AOE.PointBlank, '
         'Stat.Offense.Global, Stat.Resource.ClassResource, Trigger.OnKill'),
        'Strike',
        ('Radius=4; Angle=160; MoreDamagePer=0.5; ScalingSource=Fervour; MaxDamagePercent=350; '
         'KillFervour=2'),
        damage=200, needs=('N6', 'N7')),
    DeathRow(
        'Greataxe', 'Ultimate', 'Exhume',
        ('Hew one enemy within 4 meters for 400% weapon damage and drag a Bone Reaper up out of the ground'
         ' at its feet. The Reaper fights for you until it is destroyed, swinging slowly and very hard, '
         'and reserves 15 Fervour while it stands.'),
        ('Item.Weapon.Greataxe, Element.Death, Type.Strike, Type.Melee, Type.Minion, Type.Summon, '
         'Keyword.Summon, Stat.Resource.ClassResource'),
        'Summon',
        ('Range=4; Radius=2.5; MaxTargets=1; Count=1; Minions=BoneReaper:1; FervourReserve=15; '
         'Permanent=1'),
        damage=400, fervour=15, needs=('N1', 'N2')),
    DeathRow(
        'Greataxe', 'Movement', 'Open Grave',
        ('Leap to a point up to 9 meters away and break the ground open into a grave 5 meters across. '
         'Everything caught is knocked down for 1 second and has to climb out: enemies in the grave cannot'
         ' charge or leap. Every minion of yours inside it explodes.'),
        ('Item.Weapon.Greataxe, Element.Death, Type.AOE.PointBlank, Type.AOE.Persistent, Keyword.Stagger, '
         'Keyword.CC, Keyword.Summon'),
        'Movement',
        ('Mode=Leap; Range=9; Radius=5; Terrain=Pit; TerrainSize=5; TerrainDuration=6; '
         'ForcedMovement=Knockdown; ForcedMovementDuration=1; Detonates=1'),
        needs=('N3',)),
    DeathRow(
        'Greataxe', 'Special', 'Shallow Graves',
        ('Four husks claw out of shallow graves at a point up to 10 meters away and lurch at the nearest '
         'enemies. Every enemy within 4 meters of a husk turns on it and attacks it until it falls; Bosses'
         ' are not fooled. Each lasts 8 seconds and reserves Fervour while it stands.'),
        ('Item.Weapon.Greataxe, Element.Death, Type.Minion, Type.Summon, Keyword.Summon, Keyword.CC'),
        'Summon',
        ('Range=10; Radius=4; Count=4; Duration=8; Minions=Husk:4; FervourReserve=3; Taunts=1'),
        needs=('C3',)),
    DeathRow(
        'Staff', 'Support', 'Grasping Dead',
        ('Hands of the dead claw up out of the ground at a point up to 14 meters away and seize every '
         'enemy within 5 meters of it, holding each where it stands for 2 seconds.'),
        ('Item.Weapon.Staff, Element.Death, Type.Spell, Type.Debuff, Keyword.CC, Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=5; Pierce=0; Speed=0; ForcedMovement=Pin; ForcedMovementDuration=2'),
        needs=()),
    DeathRow(
        'Staff', 'Heavy', 'Marrow Lance',
        ('Drive a lance of bone up out of the ground along a line up to 14 meters long, giving Necrosis to'
         ' everything it passes. Every minion of yours on the line explodes.'),
        ('Item.Weapon.Staff, Element.Death, Type.Spell, Type.Projectile, Keyword.DoT.Necrosis, '
         'Keyword.Summon, Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=1.2; Pierce=99; Speed=2400; Ailment=Necrosis; Detonates=1'),
        needs=('N3',)),
    DeathRow(
        'Staff', 'Ultimate', 'Bone Choir',
        ('Call two Skeletal Mages up out of the ground at a point up to 12 meters away, in a burst of '
         'grave-cold that deals 300% weapon damage within 5 meters. They fight for you until they are '
         'destroyed and keep their distance, and the burst and every bolt they throw Cripple what they '
         'hit, 20% slower for 2 seconds. Each mage reserves 10 Fervour while it stands.'),
        ('Item.Weapon.Staff, Element.Death, Type.Spell, Type.Minion, Type.Summon, Keyword.Summon, '
         'Type.AOE.PointBlank, Type.Debuff, Stat.Resource.ClassResource, Type.Ranged'),
        'Summon',
        ('Range=12; Count=2; Radius=5; Minions=SkeletalMage:2; FervourReserve=10; Permanent=1; '
         'Effect=Cripple; EffectMagnitude=20; EffectDuration=2; MinionsLayEffect=1'),
        damage=300, fervour=20, needs=('N1', 'N2', 'N11')),
    DeathRow(
        'Staff', 'Movement', 'Wraithwalk',
        ('Leave your body and drift up to 12 meters in one second, straight through anything in the way. '
         'Nothing can touch you while you drift, and every enemy you pass through is Crippled, moving and '
         'attacking 30% slower for 3 seconds.'),
        ('Item.Weapon.Staff, Element.Death, Type.Spell, Keyword.Charge, Type.Debuff, Stat.Defense.Evasion'),
        'Movement',
        ('Mode=Charge; Range=12; Duration=1; Radius=1.5; Untargetable=1; Effect=Cripple; '
         'EffectMagnitude=30; EffectDuration=3'),
        needs=('N18', 'N22')),
    DeathRow(
        'Staff', 'Special', 'Grave Mouth',
        ('Tear open a grave-mouth at a point up to 12 meters away. For 4 seconds a wisp flies out of it '
         'every half second, 8 in all, and hurls itself at the nearest enemy. When the grave closes, every'
         ' wisp still out bursts at once. Each wisp lasts until the grave closes and reserves Fervour '
         'while it is out.'),
        ('Item.Weapon.Staff, Element.Death, Type.Minion, Type.Summon, Keyword.Summon'),
        'Summon',
        ('Range=12; Radius=3; Count=1; Duration=4; Interval=0.5; MaxActive=8; Minions=Wisp:1; '
         'FervourReserve=2; CollapseBursts=1'),
        needs=('N12',)),
    DeathRow(
        'Wand', 'Support', 'Soul Snare',
        ('Snare the souls of up to 6 enemies within 4 meters of a point up to 14 meters away. For 10 '
         'seconds their Death resistance is cut by 15, and any that dies snared lets loose a wisp, the '
         'same wisp Grave Mouth tears free, that hurls itself at the nearest enemy and fights for you for '
         '4 seconds.'),
        ('Item.Weapon.Wand, Element.Death, Type.Spell, Type.Debuff, Stat.Defense.Resist, Type.Minion, '
         'Keyword.Summon, Trigger.OnDeath, Type.Ranged'),
        'Debuff',
        # `RisesAs=Wisp` as Death Rattle's `RisesAs=Husk`. AND IT WAITS ON N12 AS WELL AS N5, found in slice DS3: its wisp
        # is read from Grave Mouth's row, which has no shape (and the Wisp no Minion Types row) until N12 is built, and
        # the generator refuses a `RisesAs` no Death Summon row raises.
        ('Range=14; Radius=4; MaxTargets=6; EffectDuration=10; Effect=Shred; EffectMagnitude=15; '
         'OnDeath=Rise; RisesAs=Wisp'),
        needs=('N5', 'N12')),
    DeathRow(
        'Wand', 'Heavy', 'Hungering Soul',
        ('Loose a hungering soul at an enemy up to 14 meters away. It lodges in it, giving it Necrosis, '
         'and when its host dies it tears free and flies into the nearest living enemy within 8 meters, '
         'again and again until nothing is left in reach. Every host it leaves dead gives you 2 Fervour.'),
        ('Item.Weapon.Wand, Element.Death, Type.Spell, Type.Projectile, Keyword.DoT.Necrosis, '
         'Stat.Resource.ClassResource, Trigger.OnDeath, Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=1; Speed=2600; Ailment=Necrosis; OnDeath=Leap; OnDeathRange=8; '
         'KillFervour=2'),
        needs=('N6',)),
    DeathRow(
        'Wand', 'Ultimate', 'Requiem',
        ("Sing the requiem and spend every Fervour you hold, the Ultimate's 50 among it. A wave of grave-"
         'cold rolls out 14 meters for 250% weapon damage, 1% more for every Fervour spent, to at most '
         '700%, and Cripples everything it touches, 40% slower for 6 seconds.'),
        ('Item.Weapon.Wand, Element.Death, Type.Spell, Type.AOE.PointBlank, Type.Debuff, '
         'Stat.Offense.Global, Stat.Resource.ClassResource'),
        'Strike',
        ('Radius=14; Angle=360; Effect=Cripple; EffectMagnitude=40; EffectDuration=6; '
         'SpendsAllFervour=1; MoreDamagePer=1; ScalingSource=FervourSpent; MaxDamagePercent=700'),
        damage=250, needs=('N13',)),
    DeathRow(
        'Wand', 'Movement', 'Grave Return',
        ('Leave a shade of yourself where you stand. For 8 seconds you may return to it from anywhere '
         'within 14 meters. As you arrive it bursts, throwing every enemy within 3 meters 3 meters back, '
         'and every minion of yours within it explodes too.'),
        ('Item.Weapon.Wand, Element.Death, Type.Spell, Type.AOE.PointBlank, Keyword.Summon'),
        'Movement',
        ('Mode=Recall; Range=14; Radius=3; EffectDuration=8; Knockback=3; Detonates=1'),
        needs=('N3',)),
    DeathRow(
        'Wand', 'Special', 'Restless Spirits',
        ('Let six restless spirits loose over 2 seconds. Each flies at the nearest enemy within 12 meters '
         'for 25% weapon damage and gives it Necrosis, and every enemy a spirit kills gives you 1 Fervour.'),
        ('Item.Weapon.Wand, Element.Death, Type.Spell, Type.Projectile, Keyword.DoT.Necrosis, '
         'Stat.Resource.ClassResource, Trigger.OnKill, Type.Ranged'),
        'Projectile',
        ('Range=12; Radius=1; Speed=1800; Count=6; Duration=2; Interval=0.33; TargetMode=Nearest; '
         'Ailment=Necrosis; KillFervour=1'),
        damage=25, needs=('N6',)),
    DeathRow(
        'Fist', 'Support', 'Hemlock',
        ('Coat your knuckles in hemlock for 6 seconds. Every blow you land while it lasts Poisons what it '
         'hits.'),
        ('Item.Weapon.Fist, Element.Death, Type.Buff, Keyword.DoT.Poison, Trigger.OnHit'),
        'SelfBuff',
        ('Duration=6; Ailment=Poison; EveryBlow=1'),
        cooldown=10, needs=('N17',)),
    DeathRow(
        'Fist', 'Heavy', 'Gravetouch',
        ('Lay a hand of grave-cold on one enemy and open a wound that Bleeds. If it dies while the wound '
         'still bleeds, it bursts, dealing 10% of its maximum health to every enemy within 4 meters.'),
        ('Item.Weapon.Fist, Element.Death, Type.Strike, Type.Melee, Keyword.DoT.Bleed, Trigger.OnDeath'),
        'Strike',
        ('Radius=2; Angle=60; MaxTargets=1; Ailment=Bleed; OnDeath=Burst; '
         'BurstPercentOfMaximumHealth=10; OnDeathRange=4'),
        needs=('N14', 'N16')),
    DeathRow(
        'Fist', 'Ultimate', 'Quietus',
        ('Deliver the quietus to one enemy for 400% weapon damage. In the same instant every Poison, Bleed'
         ' and Necrosis you have laid on enemies within 10 meters deals all the damage it had left, and '
         'ends.'),
        ('Item.Weapon.Fist, Element.Death, Type.Strike, Type.Melee, Keyword.Consume, Keyword.DoT.Poison, '
         'Keyword.DoT.Bleed, Keyword.DoT.Necrosis'),
        'Strike',
        ('Radius=2.5; Angle=60; MaxTargets=1; CashInRadius=10'),
        damage=400, needs=('N15',)),
    DeathRow(
        'Fist', 'Movement', 'Severing Rush',
        ('Rush 12 meters through everything in your way, knocking each enemy 2 meters aside and opening a '
         'Bleed on it. A Bleed hurts only while its victim moves, so whatever chases you pays for it.'),
        ('Item.Weapon.Fist, Element.Death, Type.Strike, Type.Melee, Keyword.Charge, Keyword.DoT.Bleed'),
        'Movement',
        ('Mode=Charge; Range=12; Radius=2; Knockback=2; Ailment=Bleed'),
        needs=('N16',)),
    DeathRow(
        'Fist', 'Special', 'Nightshade',
        ('Flick a vial of nightshade at a point up to 12 meters away. Everything within 2 meters of where '
         'it breaks is Poisoned and Crippled, moving and attacking 20% slower for 4 seconds.'),
        ('Item.Weapon.Fist, Element.Death, Type.Projectile, Keyword.DoT.Poison, Type.Debuff, Type.Ranged'),
        'Projectile',
        ('Range=12; Radius=2; Speed=2000; Ailment=Poison; Effect=Cripple; EffectMagnitude=20; '
         'EffectDuration=4'),
        needs=()),
    DeathRow(
        'Whip', 'Support', 'Death Grip',
        ('For 6 seconds every blow you land drags what it hits 1 meter toward you. Each drag within 5 '
         'seconds of the last on the same enemy goes half as far.'),
        ('Item.Weapon.Whip, Element.Death, Type.Buff, Keyword.CC, Trigger.OnHit'),
        'SelfBuff',
        ('Duration=6; ForcedMovement=Pull; ForcedMovementDistance=1; EveryBlow=1'),
        cooldown=10, needs=('N17',)),
    DeathRow(
        'Whip', 'Heavy', 'Soul Lash',
        ('Lash everything in a long, narrow cone up to 5 meters away, giving it Necrosis and dragging it 2'
         ' meters toward you.'),
        ('Item.Weapon.Whip, Element.Death, Type.Strike, Type.Melee, Keyword.DoT.Necrosis, Keyword.CC'),
        'Strike',
        ('Radius=5; Angle=45; Ailment=Necrosis; ForcedMovement=Pull; ForcedMovementDistance=2'),
        needs=()),
    DeathRow(
        'Whip', 'Ultimate', 'Last Toll',
        ('Crack the whip in a ring 12 meters wide. Every enemy it catches is dragged to your feet for 350%'
         ' weapon damage, given Necrosis and knocked down for 1.5 seconds, and every enemy it kills gives '
         'you 5 Fervour.'),
        ('Item.Weapon.Whip, Element.Death, Type.Strike, Type.Melee, Type.AOE.PointBlank, Keyword.CC, '
         'Keyword.Stagger, Keyword.DoT.Necrosis, Stat.Resource.ClassResource, Trigger.OnKill'),
        'Strike',
        ('Radius=12; Angle=360; ForcedMovement=Pull, Knockdown; ForcedMovementDuration=1.5; '
         'Ailment=Necrosis; KillFervour=5'),
        damage=350, needs=('N6',)),
    DeathRow(
        'Whip', 'Movement', 'Soul Herd',
        ('Leap to a point up to 12 meters away and crack the whip as you land, dragging every enemy within'
         ' 6 meters 3 meters toward you.'),
        ('Item.Weapon.Whip, Element.Death, Type.AOE.PointBlank, Keyword.CC'),
        'Movement',
        ('Mode=Leap; Range=12; Radius=6; ForcedMovement=Pull; ForcedMovementDistance=3'),
        needs=()),
    DeathRow(
        'Whip', 'Special', 'Gallows Chain',
        ('Hurl a chain at the first enemy within 14 meters and haul it to your feet, Crippled, moving and '
         'attacking 30% slower for 3 seconds.'),
        ('Item.Weapon.Whip, Element.Death, Type.Projectile, Keyword.CC, Type.Debuff, Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=0.5; Pierce=0; Speed=2400; ForcedMovement=Pull; '
         'ForcedMovementDistance=12; Effect=Cripple; EffectMagnitude=30; EffectDuration=3'),
        needs=()),
    DeathRow(
        'Spear', 'Support', 'Grim Reach',
        ('Shift your grip to the end of the spear for 10 seconds. While it lasts your attacks reach 3 '
         'meters farther, and every hit you land Cripples what it hits, moving and attacking 30% slower '
         'for 4 seconds.'),
        ('Item.Weapon.Spear, Element.Death, Type.Buff, Stat.Offense.Range, Type.Debuff, Trigger.OnHit'),
        'SelfBuff',
        ('Duration=10; MeleeReachMetres=3; Effect=Cripple; EffectMagnitude=30; EffectDuration=4; '
         'EveryBlow=1'),
        needs=('N17', 'N21')),
    DeathRow(
        'Spear', 'Heavy', 'Reaping Arc',
        ('Sweep the spear through a third of a circle in front of you, 7 meters deep, for 200% weapon '
         'damage, giving Necrosis to everything it strikes.'),
        ('Item.Weapon.Spear, Element.Death, Type.Strike, Type.Melee, Type.AOE.PointBlank, '
         'Keyword.DoT.Necrosis'),
        'Strike',
        ('Radius=7; Angle=120; Ailment=Necrosis'),
        damage=200, needs=()),
    DeathRow(
        'Spear', 'Ultimate', 'Pale Rider',
        ('Ride as Death rides. You charge 14 meters in 1.5 seconds and nothing turns you aside: everything'
         ' in your path is run through for 300% weapon damage and Crippled, moving and attacking 50% '
         'slower for 4 seconds. Every enemy it kills gives you 5 Fervour.'),
        ('Item.Weapon.Spear, Element.Death, Type.Strike, Type.Melee, Keyword.Charge, Type.Debuff, '
         'Stat.Resource.ClassResource, Trigger.OnKill'),
        'Movement',
        ('Mode=Charge; Range=14; Duration=1.5; Radius=2.5; Immune=CrowdControl; Effect=Cripple; '
         'EffectMagnitude=50; EffectDuration=4; KillFervour=5'),
        damage=300, needs=('N6', 'N22')),
    DeathRow(
        'Spear', 'Movement', 'Pale Vault',
        ('Vault on the spear to a point up to 10 meters away and land point first, striking every enemy '
         'within 3 meters for 150% weapon damage and Crippling it, moving and attacking 30% slower for 4 '
         'seconds.'),
        ('Item.Weapon.Spear, Element.Death, Type.AOE.PointBlank, Type.Debuff'),
        'Movement',
        ('Mode=Leap; Range=10; Radius=3; Effect=Cripple; EffectMagnitude=30; EffectDuration=4'),
        damage=150, needs=('N22',)),
    DeathRow(
        'Spear', 'Special', 'Soul Lance',
        ('Throw the spear through a line up to 14 meters long. It passes through every enemy in the line, '
         'and each enemy it hits gives you 1 Fervour, at most 4 a throw.'),
        ('Item.Weapon.Spear, Element.Death, Type.Projectile, Stat.Resource.ClassResource, Trigger.OnHit, '
         'Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=1; Pierce=99; Speed=2400; HitFervour=1; HitFervourPerCast=4'),
        needs=('N19',)),
    DeathRow(
        'Crossbow', 'Support', 'Carrion Crows',
        ('Loose the carrion crows for 8 seconds. Each time you land a critical strike, a crow dives at the'
         ' enemy you struck for 40% weapon damage, or at the nearest enemy within 10 meters if the strike '
         "killed it. At most four crows dive each second, and a crow's own critical strike calls no other."),
        ('Item.Weapon.Crossbow, Element.Death, Type.Buff, Stat.Offense.Crit, Trigger.OnCrit, Type.Ranged'),
        'SelfBuff',
        ('Duration=8; Radius=10; StrikesOnCritical=1'),
        damage=40, cooldown=10, needs=('C2',)),
    DeathRow(
        'Crossbow', 'Heavy', 'Coffin Nail',
        ('Drive a coffin nail into the first enemy in a line up to 14 meters away. The nail stays in: for '
         'every nail an enemy carries, your critical strike chance against it is 5% higher, up to 4 nails,'
         ' and they stay until 4 seconds pass without a new one.'),
        ('Item.Weapon.Crossbow, Element.Death, Type.Projectile, Type.Debuff, Stat.Offense.Crit, '
         'Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=0.4; Pierce=0; Speed=3000; CritChanceAgainst=5; CritChanceAgainstMax=20; '
         'EffectDuration=4'),
        needs=('C1',)),
    DeathRow(
        'Crossbow', 'Ultimate', 'None Spared',
        ('Take aim at everything within 14 meters and loose 12 bolts over 1.5 seconds, each at the next '
         'enemy in turn, so the bolts are shared out across a crowd and a lone enemy takes all twelve. '
         'Every bolt deals 35% weapon damage and is a critical strike.'),
        ('Item.Weapon.Crossbow, Element.Death, Type.Projectile, Stat.Offense.Crit, Type.Ranged'),
        'Projectile',
        ('Range=14; Radius=0.4; Pierce=0; Speed=3000; Count=12; Duration=1.5; Interval=0.125; '
         'TargetMode=All'),
        crit=100, damage=35, needs=()),
    DeathRow(
        'Crossbow', 'Movement', 'Parting Shot',
        ('Leap to a point up to 10 meters away and loose a bolt as you land at the nearest enemy within 10'
         ' meters, Crippling it so it moves and attacks 30% slower for 3 seconds.'),
        ('Item.Weapon.Crossbow, Element.Death, Type.Debuff, Type.Ranged'),
        'Movement',
        ('Mode=Leap; Range=10; Radius=10; MaxTargets=1; Effect=Cripple; EffectMagnitude=30; '
         'EffectDuration=3'),
        needs=('N22',)),
    DeathRow(
        'Crossbow', 'Special', 'Effigy',
        ('Loose a bolt of grave-iron into the ground up to 14 meters away, and an effigy of you rises '
         'where it lands for 5 seconds. Every enemy that comes within 8 meters of it turns on it and '
         'attacks it until it falls; Bosses are not fooled. If you have the Siphon keystone, the effigy '
         'siphons the enemies around it too. It reserves 5 Fervour while it stands.'),
        ('Item.Weapon.Crossbow, Element.Death, Type.Deployable, Type.Minion, Keyword.CC, Type.Ranged'),
        'Deployable',
        ('Range=14; Radius=8; Count=1; Duration=5; Interval=1; MaxActive=1; Minions=Effigy:1; '
         'FervourReserve=5; Taunts=1; SiphonsAround=1'),
        needs=('C3', 'C4', 'DeployableReserve')),
)

#: The Minion Types sheet's columns, by header, in the order its rows are written.
MINION_COLUMNS = ("Minion Type", "Family", "Base Health", "Health Per Level", "Base Damage", "Damage Per Level",
                  "Attack Interval Seconds", "Move Speed", "Threat Percent", "Reach Cm", "Notice Radius Cm",
                  "Target Mode", "Tags", "Explosion Percent Of Own Damage")

#: The minion kinds the rows written whole raise, from `death_weapon_skills_draft.md`'s minion rows, each a judgement
#: of that draft against the Imp and the Mote. ONE CHANGE, a judgement of 2026-11-24 recorded in `docs/DECISIONS.md`:
#: the Shade's threat is nought where the draft wrote 20. Wailing Host's sentence says the enemies "pay them little
#: attention", and since the owner's decision of 2026-11-01 (issue #2553) a threat above nought draws nothing in an
#: ordinary choice while nought is what makes the nearest-enemy search pass a minion over.
#: Slice DS2 added BarrowGuard, BoneReaper and SkeletalMage with the three permanent raises, as drafted. The draft's
#: Wisp and the Crossbow draft's Effigy are added by the slice that builds the rows raising them (Grave Mouth, Soul
#: Snare; the Effigy).
MINION_ROWS: dict[str, tuple[object, ...]] = {
    "Shade": ("Shade", "Creature", 100, 45, 5.25, 4.725, 0.8, 5.5, 0, 350, 1500, "Nearest",
              "Type.Minion, Type.Summon, Minion.Creature, Minion.Spell", 300),
    "Husk": ("Husk", "Creature", 160, 72, 10.5, 10.5, 1.5, 4.4, 150, 200, 1500, "Nearest",
             "Type.Minion, Type.Summon, Minion.Creature, Minion.Melee", 300),
    # SLICE DS2, 2026-11-24: the three permanent raises' kinds, the draft's figures unchanged.
    "BarrowGuard": ("BarrowGuard", "Creature", 400, 180, 15.75, 15.75, 1.0, 4.4, 150, 200, 1500, "Nearest",
                    "Type.Minion, Type.Summon, Minion.Creature, Minion.Melee", 300),
    "BoneReaper": ("BoneReaper", "Creature", 300, 135, 42, 42, 2.0, 4.4, 60, 250, 1500, "Nearest",
                   "Type.Minion, Type.Summon, Minion.Creature, Minion.Melee", 300),
    "SkeletalMage": ("SkeletalMage", "Creature", 150, 67.5, 15.75, 15.75, 1.5, 4.4, 40, 900, 1500, "Nearest",
                     "Type.Minion, Type.Summon, Minion.Creature, Minion.Spell", 300),
}


class ApplyError(Exception):
    """Something about the workbook that makes writing the cells unsafe or impossible."""


class Write(NamedTuple):
    """One cell to write: where, what it held, and what it becomes."""

    sheet: str
    row: int
    column: int
    label: str
    before: object
    after: object

    def describe(self) -> str:
        letter = get_column_letter(self.column)
        return f"{self.sheet}!{letter}{self.row} ({self.label}): {self.before!r} -> {self.after!r}"


def same(value: object, wanted: object) -> bool:
    """Whether a cell holds `wanted`: a number compared as a number, an empty cell as None."""
    if value == "":
        value = None
    if wanted is None or value is None:
        return value is None and wanted is None
    numbers = (int, float)
    if isinstance(wanted, numbers) and not isinstance(wanted, bool):
        return (isinstance(value, numbers) and not isinstance(value, bool)
                and abs(float(value) - float(wanted)) < 1e-9)
    return value == wanted


def header_of(sheet, wanted: tuple[str, ...]) -> dict[str, int]:
    """The sheet's header, each wanted name to its 1-based column. Refuses a name missing or given twice."""
    header = [cell.value for cell in sheet[1]]
    found = {}
    for name in wanted:
        if header.count(name) != 1:
            raise ApplyError(f"{sheet.title!r} has {header.count(name)} {name!r} column(s), not one; refusing")
        found[name] = header.index(name) + 1
    return found


def last_row(sheet, column: int) -> int:
    """The last row holding anything in `column`, so a row appended goes below the data and not below formatting."""
    last = 1
    for row in sheet.iter_rows(min_row=1, min_col=column, max_col=column):
        if row[0].value not in (None, ""):
            last = row[0].row
    return last


def refuse_what_a_save_would_lose(book) -> None:
    formulas = sum(1 for sheet in book.worksheets for row in sheet.iter_rows() for cell in row
                   if isinstance(cell.value, str) and cell.value.startswith("="))
    charts = sum(len(sheet._charts) for sheet in book.worksheets)
    images = sum(len(sheet._images) for sheet in book.worksheets)
    if formulas or charts or images:
        raise ApplyError(f"the workbook holds {formulas} formula(s), {charts} chart(s) and {images} image(s), which an "
                         "openpyxl save can lose without saying so; make the edit by hand in Excel instead")


def check_rows() -> None:
    """Refuse a mistake in this file's own data before it reaches a workbook."""
    seen = set()
    for row in ROWS:
        key = (row.weapon, row.slot)
        if key in seen:
            raise ApplyError(f"{row.csv_name} is listed twice in ROWS")
        seen.add(key)
        unknown = sorted(set(row.needs) - KNOWN_PIECES)
        if unknown:
            raise ApplyError(f"{row.csv_name} needs {unknown}, which no draft numbers; KNOWN_PIECES names them")
        if any(tag.strip().startswith("Slot.") for tag in row.tags.split(",")):
            raise ApplyError(f"{row.csv_name} carries a Slot. tag, which the generator adds from the Slot column")
    if len(ROWS) != 45:
        raise ApplyError(f"ROWS holds {len(ROWS)} rows; the owner approved 40 and 5, which is 45")


def plan_skills(sheet) -> tuple[list[Write], list[DeathRow]]:
    """The Weapon Skills cells still to write, and the rows still to append below the sheet's last row."""
    at = header_of(sheet, SKILL_COLUMNS)
    writes: list[Write] = []
    appended: list[DeathRow] = []
    for death in ROWS:
        found = [row[0].row for row in sheet.iter_rows(min_row=2)
                 if row[at[WEAPON] - 1].value == death.weapon and row[at[DAMAGE] - 1].value == DAMAGE_TYPE
                 and row[at[SLOT] - 1].value == death.slot]
        if len(found) > 1:
            raise ApplyError(f"{SKILLS_SHEET!r} has {len(found)} {death.csv_name} rows: {found}; refusing")
        if not found:
            if death.weapon not in NEW_WEAPONS:
                raise ApplyError(f"{SKILLS_SHEET!r} has no {death.csv_name} row, and it was in the matrix before "
                                 "issue #2885; look at the sheet by hand")
            appended.append(death)
            continue
        row = found[0]
        before = death.cells_before()
        superseded = death.cells_superseded()
        for column, after in death.cells().items():
            value = sheet.cell(row, at[column]).value
            if same(value, after):
                continue
            if column in superseded and same(value, superseded[column]):
                writes.append(Write(SKILLS_SHEET, row, at[column], f"{death.csv_name}, {column}", value, after))
                continue
            if not same(value, before[column]):
                raise ApplyError(
                    f"{SKILLS_SHEET}!{get_column_letter(at[column])}{row} ({death.csv_name}, "
                    f"{column}) holds {value!r}, neither {before[column]!r} (before #2885) nor {after!r} (this "
                    "edit); somebody else changed it, so look at it by hand")
            writes.append(Write(SKILLS_SHEET, row, at[column], f"{death.csv_name}, {column}", value, after))
    return writes, appended


def plan_appended(sheet, appended: list[DeathRow]) -> list[Write]:
    """Every cell of each appended row, below the sheet's last row, in `ROWS` order."""
    if not appended:
        return []
    at = header_of(sheet, SKILL_COLUMNS)
    first = last_row(sheet, at[WEAPON]) + 1
    writes = []
    for offset, death in enumerate(appended):
        row = first + offset
        cells = {WEAPON: death.weapon, DAMAGE: DAMAGE_TYPE, SLOT: death.slot} | death.cells()
        for column, after in cells.items():
            if after is not None:
                writes.append(Write(SKILLS_SHEET, row, at[column], f"{death.csv_name}, {column}", None, after))
    return writes


def plan_minions(sheet) -> list[Write]:
    """The Minion Types rows still to add. A kind already present must match what this writes, cell for cell."""
    at = header_of(sheet, MINION_COLUMNS)
    key = at[MINION_COLUMNS[0]]
    next_row = last_row(sheet, key) + 1
    writes = []
    for name, values in MINION_ROWS.items():
        found = [row[0].row for row in sheet.iter_rows(min_row=2, min_col=key, max_col=key) if row[0].value == name]
        if len(found) > 1:
            raise ApplyError(f"{MINIONS_SHEET!r} has {len(found)} {name!r} rows: {found}; refusing")
        if found:
            for column, wanted in zip(MINION_COLUMNS, values, strict=True):
                value = sheet.cell(found[0], at[column]).value
                if not same(value, wanted):
                    raise ApplyError(f"{MINIONS_SHEET!r}'s {name} row holds {value!r} for {column!r}, not {wanted!r}; "
                                     "somebody else changed it, so look at it by hand")
            continue
        for column, wanted in zip(MINION_COLUMNS, values, strict=True):
            writes.append(Write(MINIONS_SHEET, next_row, at[column], f"{name}, {column}", None, wanted))
        next_row += 1
    return writes


def plan(book) -> list[Write]:
    """Every cell still to write, in the order it is written. Empty for a workbook already holding the edit."""
    check_rows()
    refuse_what_a_save_would_lose(book)
    skills = book[SKILLS_SHEET]
    writes, appended = plan_skills(skills)
    return writes + plan_appended(skills, appended) + plan_minions(book[MINIONS_SHEET])


def snapshot(book) -> dict[tuple[str, int, int], object]:
    """Every cell holding something, by sheet, row and column."""
    return {(sheet.title, cell.row, cell.column): cell.value
            for sheet in book.worksheets for row in sheet.iter_rows() for cell in row
            if cell.value not in (None, "")}


def apply(path: pathlib.Path = WORKBOOK) -> list[Write]:
    """Write the cells into the workbook at `path`, read it back, and answer what was written."""
    book = openpyxl.load_workbook(path)
    writes = plan(book)
    if not writes:
        return []
    before = snapshot(book)
    sheets_before = list(book.sheetnames)
    for write in writes:
        book[write.sheet].cell(write.row, write.column).value = write.after
    book.save(path)

    # READ BACK FROM DISK: the same sheets in the same order, the edit in place, and no other cell changed.
    after_book = openpyxl.load_workbook(path)
    if after_book.sheetnames != sheets_before:
        raise ApplyError(f"the sheets changed: {sheets_before} became {after_book.sheetnames}")
    after = snapshot(after_book)
    changed = {key for key in set(before) | set(after) if before.get(key) != after.get(key)}
    intended = {(write.sheet, write.row, write.column) for write in writes}
    if changed != intended:
        raise ApplyError(f"the save changed {sorted(changed - intended)} and missed {sorted(intended - changed)}; "
                         "inspect the workbook")
    if plan(after_book):
        raise ApplyError("read back, the workbook still has cells to write; inspect it")
    return writes


def check(path: pathlib.Path = WORKBOOK) -> bool:
    """Whether the workbook at `path` already holds every cell this writes."""
    return not plan(openpyxl.load_workbook(path))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true", help="report what would change and write nothing")
    parser.add_argument("--no-generate", action="store_true",
                        help="edit the workbook and do not regenerate the CSVs")
    args = parser.parse_args(argv)

    try:
        if args.check:
            writes = plan(openpyxl.load_workbook(args.workbook))
            for write in writes:
                print(f"to write   {write.describe()}")
            print(f"{len(writes)} cells to write; nothing written")
            return 0
        writes = apply(args.workbook)
    except ApplyError as error:
        print(f"refused: {error}", file=sys.stderr)
        return 1

    for write in writes:
        print(f"wrote      {write.describe()}")
    waiting = [row for row in ROWS if not row.works]
    print(f"wrote {len(writes)} cells, and no other cell changed. {len(ROWS) - len(waiting)} of the {len(ROWS)} Death "
          f"rows have their shape; {len(waiting)} wait on code or a ruling: "
          + "; ".join(f"{row.name} ({', '.join(row.waits_on)})" for row in waiting))
    if args.no_generate:
        return 0
    return subprocess.call([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], cwd=REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main())
