"""Apply the Demonic passive tree rework the project owner approved on 2026-11-24.

    python tools/apply_demonic_tree_rework_2026_11_24.py <tree file or directory> ...
    python tools/apply_demonic_tree_rework_2026_11_24.py --check <tree file or directory> ...

Three audits of the Demonic class trees -- the Masochist, the Ravager and the
Ritualist -- proposed new words and numbers for most of their nodes, and the
project owner chose "Accept all, write them (Recommended)" on 2026-11-24, with
every one of the fifteen questions answered as the audits recommended. This
script writes the node half of that: the names and sentences in a tree file.
The numbers behind each sentence are rows in the design workbook's Passive
Effects sheet, which `tools/reapply_demonic_tree_rework_workbook.py` writes.

IT RUNS AFTER `tools/apply_passive_order_fixes_2026_11_24.py`, NEVER BEFORE, and
refuses a tree the order fixes have not reached. The Masochist audit was written
against the fixed order (Deeper Cuts before Blood Price, Grand Tithe at 2%), and
that is the only tree the order fixes and this rework share.

EVERY CHANGE FINDS ITS NODE BY NAME, NEVER BY IDENTIFIER OR POSITION, for the
reason the order-fix script gives: it applies to the trees in `docs/` and to a
"sigil" layout that keeps each tree's identifiers, contents and edges but moves
every node. The tree is read from `metadata.name` and checked against the file
name, with the order-fix script's own `tree_name`.

NOTHING HERE CHANGES THE GRAPH. No proposal changes a node's identifier, kind,
maximum points or edges, so a saved allocation loads as it is and the approved
sigil layouts stay valid. The one move is the Ritualist's Volatile, whose content
trades places with Spiteful Ash's so that the explosion node leads to Every One
Bursts, the keystone that makes minions explode. Both hold eight points, and the
swap is the order-fix script's own `swap_content`: content moves, colour stays
with the position. Spiteful Ash's place then becomes Legion.

IT REFUSES RATHER THAN GUESSES, and writes nothing when it refuses:
  - the order fixes have not been applied to the tree;
  - a named node or capstone option is missing, or two nodes share the name;
  - a sentence it rewrites is neither the one approved against nor the new one;
  - a renamed node's new name is already taken by another node;
  - after the changes, any node's identifier, kind, position or maximum points,
    or any edge, differs from before (the Volatile swap aside, which moves
    content between two eight-point nodes).
With several files every file is checked before any is written.

IT IS IDEMPOTENT: a node already reading its new sentence is left alone, and a
file with nothing to change is not rewritten. The file keeps its format byte for
byte, as the order-fix script does.
"""

from __future__ import annotations

import argparse
import copy
import dataclasses
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order_fixes  # noqa: E402

Refused = order_fixes.Refused


@dataclasses.dataclass(frozen=True)
class Rework:
    """A node's new sentence, and its new name when the old one no longer fits.

    `node` is the name the node has before the rework; `name` the name after,
    or None when it keeps its name.
    """
    node: str
    before: str
    after: str
    name: str | None = None

    @property
    def new_name(self) -> str:
        return self.name or self.node


@dataclasses.dataclass(frozen=True)
class OptionRework:
    """A capstone option's new sentence, and its new name when it is replaced.

    `option` counts from 1, the way the Passive Effects sheet's Option column
    does, and is checked against `before_name` so a reordered capstone is
    refused rather than having the wrong option rewritten.
    """
    capstone: str
    option: int
    before_name: str
    before: str
    after: str
    name: str | None = None

    @property
    def new_name(self) -> str:
        return self.name or self.before_name


@dataclasses.dataclass(frozen=True)
class SwapToParentOf:
    """Make `node` the parent of `child` by trading content with `partner`."""
    node: str
    child: str
    partner: str


# --------------------------------------------------------------------------
# The Masochist
# --------------------------------------------------------------------------

MASOCHIST = (
    Rework("Pain Tolerance",
           "+1% increased Maximum Health and +0.5% increased Armor per point.",
           "+1% increased Maximum Health per point."),
    Rework("Willing Flesh",
           "+2% increased Health Regeneration per point.",
           "+1% increased Health Regeneration per point for every 10% of your maximum "
           "health missing."),
    Rework("Cataclysmic Resonance",
           "+1% increased damage per point for 5 seconds after you take damage of a "
           "Cataclysm type other than Demonic.",
           "+1% increased damage per point for every full 25 Fervour you hold.",
           name="Gathered Pain"),
    Rework("Brutal Determination",
           "+0.4% Life Leech per point. The Masochist has no Life Leech of its own, so "
           "this is what the tree's other Life Leech nodes increase.",
           "+1% Life Leech for every 2 points. The Masochist has no Life Leech of its "
           "own, so this is what the tree's other Life Leech nodes increase."),
    Rework("Echoes of Agony",
           "Damage taken from damage over time effects is reduced by 1% per point.",
           "Damage taken from damage over time effects is reduced by 2% per point."),
    Rework("Open Wounds",
           "+2% increased Fervour gained from health lost to damage per point.",
           "+5% increased Fervour gained from health lost to damage per point."),
    Rework("Deeper Cuts",
           "Your skills also cost 0.25% of your maximum health per point, in addition "
           "to any other cost. This cost generates Fervour like any other.",
           "Your skills also cost 1% of your current health for every 2 points, in "
           "addition to any other cost. This cost generates Fervour like any other."),
    Rework("Scar Tissue",
           "+2% increased maximum Fervour per point.",
           "+5 maximum Fervour per point."),
    Rework("Hardened Nerves",
           "+2% increased Crowd Control Resistance per point.",
           "+4 Crowd Control Resistance per point."),
    Rework("Retributive Instinct",
           "+3% increased Retaliation per point.",
           "+2% Retaliation per point."),
    Rework("Communion of Pain",
           "While your Fervour is at maximum you deal 20% more damage and take 20% "
           "more damage.",
           "While you have 100 or more Fervour, you deal 20% more damage."),
    Rework("Sanguine Ledger",
           "Health regeneration no longer removes Fervour, but your Health "
           "Regeneration is reduced by 50% (multiplicative).",
           "Healing never takes your Fervour below 50."),
    Rework("Iron Hide",
           "+3% increased Armor per point.",
           "+2% increased melee damage per point for 4 seconds after you are hit.",
           name="Provoked"),
    Rework("Bulk",
           "+1% increased Maximum Health per point.",
           "+1% increased Maximum Health per point. At 6 points: +1% Life Leech."),
    Rework("Reprisal",
           "+3% increased Retaliation per point.",
           "Your Retaliation is increased by 1% per point for each enemy within 4 "
           "metres of you."),
    Rework("Thirst for Pain",
           "While you are Bleeding, +2% increased Attack Speed per point.",
           "+1% increased Attack Speed per point for each stack of Bloodlust you have."),
    Rework("Battle-Scarred",
           "+2% increased Armor per point for each unique debuff on you.",
           "While you are at or below 50% health, +1% Life Leech for every 2 points."),
    Rework("Undying Hunger",
           "+3% increased Life Leech per point.",
           "+5% increased Life Leech per point."),
    Rework("Mutilation Mastery",
           "Your melee critical strikes have a 5% chance per point to apply Bleeding.",
           "+1% increased melee damage per point for every 2% of Life Leech you have.",
           name="Ravenous"),
    Rework("Gorge",
           "+2% increased Fervour gained from health lost to damage per point.",
           "You gain 1 Fervour whenever an enemy dies within 10 metres of you, per "
           "point."),
    Rework("Unflinching",
           "+2% increased Crowd Control Resistance per point.",
           "While you have 50 or more Fervour, +3% increased Attack Speed per point."),
    Rework("Flesh Craver",
           "You gain 30% more Fervour from health lost to damage, and 50% less Fervour "
           "from health spent as an ability cost.",
           "30% more Fervour gained from health lost to damage."),
    Rework("Stigmata",
           "+2% increased damage over time damage per point.",
           "When you set an enemy alight, you are set alight as well for 4 seconds, "
           "taking 1% of your maximum health per second. +3% increased damage over "
           "time per point."),
    Rework("Lingering Hurt",
           "+2% increased damage over time duration per point.",
           "+4% increased damage over time duration per point."),
    Rework("Penitent",
           "+1% increased Maximum Health per point.",
           "+2% increased Health Regeneration per point for each debuff on you."),
    Rework("Endurance in Suffering",
           "For each unique debuff on you, +1% increased damage and +0.5% increased "
           "Damage Reduction per point.",
           "+2% increased damage per point for each debuff on you."),
    Rework("Wound Channeling",
           "Damage from debuffs on you is increased by 1% per point, and you deal 1% "
           "increased damage per point to enemies carrying a debuff you also carry.",
           "You deal 3% increased damage per point to enemies carrying a debuff you "
           "also carry."),
    Rework("Symphony of Pain",
           "Debuffs on you last 2% longer per point, and their effect on you is reduced "
           "by 1% per point.",
           "Debuffs on you last 5% longer per point, and the damage they deal you is "
           "reduced by 2% per point."),
    Rework("Contagious Torment",
           "When a debuff on you deals damage, enemies within 6 metres have a 1% chance "
           "per point to receive a random debuff you carry.",
           "When a debuff on you deals damage, enemies within 6 metres have a 5% chance "
           "per point to receive a random debuff you carry."),
    Rework("Empathic Link",
           "When an enemy dies, its debuffs have a 2% chance per point to pass to a "
           "random enemy within 6 metres.",
           "When an enemy dies, its debuffs have a 10% chance per point to pass to a "
           "random enemy within 6 metres."),
    # THE AURA'S OWN ROW STAYS, AND SO DOES ITS 4% A POINT. The audit's rows keep
    # it ("keep the aura row"), and that row is both what switches the aura on
    # and its duration bonus, so the sentence keeps saying so: a row whose value
    # the sentence does not state is what the wording checks refuse. A
    # judgement, recorded in docs/DECISIONS.md.
    Rework("Beacon of Despair",
           "You radiate an aura that applies a random debuff to enemies within 6 "
           "metres every 3 seconds. The duration of those debuffs is increased by 4% "
           "per point.",
           "You radiate an aura that applies a random debuff to enemies within 6 "
           "metres every 3 seconds, lasting 4% longer per point. You deal 1% increased "
           "damage per point to enemies for each debuff they carry."),
    Rework("Shared Agony",
           "+2% increased Fervour gained from health lost to damage over time per "
           "point.",
           "+5% increased Fervour gained from health lost to damage over time per "
           "point."),
    Rework("Ceaseless",
           "+2% increased maximum Fervour per point.",
           "While you have 50 or more Fervour, +3% increased damage over time per "
           "point."),
    Rework("Vessel of Plagues",
           "Debuffs on you last 50% longer, and debuffs on you deal 50% more damage to "
           "you.",
           "Debuffs on you last 50% longer, and each debuff on you counts twice for "
           "everything that counts the debuffs on you."),
    Rework("Quickened",
           "+1% increased Movement Speed per point.",
           "+1% increased Movement Speed per point for every 10% of your maximum "
           "health missing."),
    Rework("Razor's Edge",
           "+3% increased Critical Strike Multiplier per point.",
           "While you are at or below 50% health, +2% increased damage per point."),
    Rework("Defiance",
           "+2% increased Retaliation per point.",
           "+1% Retaliation per point for every 25% of your maximum health missing."),
    Rework("Last Stand",
           "While at or below 20% health, +3% increased Critical Strike Chance per "
           "point.",
           "While you are at or below 35% health, +1% Damage Reduction per point."),
    Rework("The Catalyst",
           "While at or below 5% health, your skills have a 5% chance per point not to "
           "go on cooldown.",
           "While you are at or below 35% health, your skills have a 4% chance per "
           "point not to go on cooldown."),
    Rework("Desperate Measures",
           "While at or below 50% health, +1% increased Movement Speed per point.",
           "While you are at or below 50% health, +3% increased Attack Speed per "
           "point."),
    Rework("The Breaking Point",
           "Dropping below 50% health converts all damage you take into Bleeding over 5 "
           "seconds. The conversion lasts 3 seconds, increased by 5% per point, and "
           "cannot happen more than once every 10 seconds.",
           "Dropping below 50% health converts all damage you take into Bleeding over 5 "
           "seconds. The conversion lasts 3 seconds, and 1 second longer for every 2 "
           "points, and cannot happen more than once every 10 seconds."),
    Rework("Unstable Aura",
           "While at or below 10% health, you release a nova every 5 seconds dealing "
           "damage equal to 1% of your missing health per point to enemies within 6 "
           "metres.",
           "While you are at or below 50% health, you release a nova every 3 seconds "
           "dealing 10% of your weapon damage per point to enemies within 6 metres."),
    Rework("Bloodletting",
           "+2% increased Fervour gained from health spent as an ability cost per "
           "point.",
           "While you are at or below 50% health, you gain 1 Fervour per second for "
           "every 2 points."),
    Rework("Threadbare",
           "+2% increased maximum Fervour per point.",
           "While you are at or below 50% health, Fervour removed by your health "
           "regeneration is reduced by 10% per point."),
    Rework("Blood Coin",
           "+2% increased Fervour gained from health spent as an ability cost per "
           "point.",
           "Each skill you cast grants 1 Fervour for every 3 points."),
    Rework("Blood Price",
           "+2% increased Fervour gained from health spent as an ability cost per "
           "point.",
           "+5% increased Fervour gained from health spent as an ability cost per "
           "point."),
    Rework("Rolling Debt",
           "Paying a health cost while one is still owed extends the delay on what is "
           "owed by 0.5 seconds per point, to a maximum of 3 seconds.",
           "Paying a health cost while one is still owed extends the delay on what is "
           "owed by 1 second for every 2 points, to a maximum of 3 seconds."),
    Rework("Deep Reserves",
           "+2% increased maximum Fervour per point.",
           "While you have 50 or more Fervour, +3% increased damage per point."),
    Rework("Exsanguinate",
           "Every skill costs an additional 15% of your current health, and every skill "
           "deals 40% more damage. A cost taken from current health cannot reduce it "
           "below 1.",
           "A skill that costs health deals 30% more damage."),
    Rework("Blood Tithe",
           "You gain 30% more Fervour from health spent as an ability cost, and 50% "
           "less Fervour from health lost to damage.",
           "30% more Fervour gained from health spent as an ability cost."),
    OptionRework("The First Vow", 2, "Reprisal Wave",
                 "Your retaliation damage strikes every enemy within 4 metres, not only "
                 "the one that hit you.",
                 "Your retaliation damage strikes every enemy within 4 metres, not only "
                 "the one that hit you, and 1 metre further for every 25 Fervour you "
                 "hold."),
    OptionRework("The Second Vow", 2, "Feeding Wound",
                 "Your life leech applies to your retaliation damage as well as to your "
                 "attacks.",
                 "Your life leech applies to your retaliation damage as well as to your "
                 "attacks, and your Life Leech is increased by 1% for every 2 Fervour "
                 "you hold."),
    OptionRework("The Third Vow", 2, "Glutton",
                 "Your retaliation damage is increased by 1% for every 1% of life leech "
                 "you have.",
                 "You deal 1% more damage for every 10 Fervour you hold."),
    OptionRework("The Third Vow", 3, "Doctrine Made Flesh",
                 "Each debuff on you grants 1% more damage and reduces the damage that "
                 "debuff deals to you by 10%.",
                 "Each debuff on you grants 3% more damage and reduces the damage that "
                 "debuff deals to you by 10%."),
    OptionRework("The Final Vow", 2, "Carnivore",
                 "Every hit you take grants a stack of Carnage. Carnage has no maximum, "
                 "and each stack grants 2% more damage.",
                 "Every hit you take grants a stack of Carnage. Carnage has no maximum, "
                 "each stack grants 2% more damage, and each stack lasts 1 second longer "
                 "for every 10 Fervour you hold."),
)


# --------------------------------------------------------------------------
# The Ravager
# --------------------------------------------------------------------------

RAVAGER = (
    Rework("Thick Hide",
           "+1% increased Armor and +0.5% increased Maximum Health per point.",
           "+1% increased Armor and +1% increased Maximum Health per point."),
    Rework("Set Stance",
           "+1% increased Damage Reduction per point. At 4 points: you cannot be "
           "knocked back while an enemy is within 4 metres.",
           "+1% Damage Reduction per point. At 4 points: you cannot be knocked back "
           "while an enemy is within 4 metres."),
    Rework("Scarred Plate",
           "+1.5% increased Armor per point. At 4 points: +5% increased Crowd Control "
           "Resistance.",
           "+2% increased Armor per point. At 4 points: +10% Crowd Control Resistance."),
    Rework("Stoked",
           "+2% increased maximum Fervour per point.",
           "Your Fervour starts to decay 1 second later per point."),
    Rework("Ironhide",
           "Your Armor cannot be ignored: armor penetration and piercing weapons remove "
           "none of it.",
           "Your Armor cannot be ignored, and Overwhelm takes only half as much from "
           "it."),
    Rework("Plated",
           "+1.5% increased Armor per point.",
           "+1% increased Armor per point, and another +1% per point while three or "
           "more enemies are within 4 metres."),
    Rework("Thick Blood",
           "+1% increased Life Leech per point.",
           "+1% Life Leech per point while three or more enemies are within 4 metres."),
    Rework("Slow to Fall",
           "+2% increased Health Regeneration per point.",
           "+1% Damage Reduction per point while three or more enemies are within 4 "
           "metres."),
    Rework("Braced",
           "+1% increased Damage Reduction per point.",
           "+1% increased Armor per point for every full 20 Fervour you hold."),
    Rework("Set Against It",
           "While you have 50 or more Fervour, you gain +2% increased Damage Reduction "
           "and +3% increased Attack Damage per point.",
           "While you have 50 or more Fervour, you gain +1% Damage Reduction and +3% "
           "increased Attack Damage per point."),
    Rework("Second Wind",
           "+2% increased Health Regeneration per point.",
           "Each time your Fervour becomes full, restore 2% of your maximum health per "
           "point."),
    Rework("Nothing Moves You",
           "Crowd control effects on you last half as long, and one ends entirely when "
           "you kill the enemy that applied it.",
           "Crowd control effects on you last half as long, and one ends entirely when "
           "you kill the enemy that applied it. While you have 50 or more Fervour, no "
           "crowd control can be applied to you."),
    Rework("Rooted",
           "+3% increased Crowd Control Resistance per point.",
           "+3% increased Maximum Health per point."),
    Rework("Stubborn",
           "+3% increased Crowd Control Resistance per point.",
           "+5% Crowd Control Resistance per point."),
    Rework("Deep Wells",
           "+2% increased maximum Fervour per point.",
           "+2% increased Maximum Health and +2% increased Armor per point."),
    Rework("Weight Bearing",
           "Your Maximum Health also grants Armor: 1 Armor for every 10 maximum health "
           "you have.",
           "Your Maximum Health also grants Armor: 1 Armor for every 4 maximum health "
           "you have."),
    Rework("Reach",
           "+2% increased Area of Effect per point.",
           "+1% increased Area of Effect per point for each enemy within 4 metres."),
    Rework("Crushing Weight",
           "+2% increased Attack Damage per point.",
           "Your melee hits ignore 2% of enemy Armor per point."),
    Rework("Swing Through",
           "+2% increased Area of Effect per point.",
           "You gain 1 Fervour per point whenever an enemy dies within 10 metres of "
           "you."),
    Rework("Wind Up",
           "+2% increased Attack Damage per point.",
           "+3% Cooldown Reduction per point for Heavy skills."),
    Rework("Whirl",
           "+2% increased Attack Speed per point.",
           "+3% increased Area of Effect per point for Heavy skills. At 6 points: your "
           "Heavy skills sweep at least a half circle."),
    Rework("Deeper Hurt",
           "+3% increased duration of Cripple and Weaken you apply per point.",
           "When an enemy you have Crippled or Weakened dies, its Cripple and Weaken "
           "pass to the nearest enemy within 1 metre per point."),
    Rework("Dragging Weight",
           "+3% increased magnitude of Cripple you apply per point.",
           "+5% increased magnitude of Cripple you apply per point."),
    Rework("Sapped",
           "+3% increased magnitude of Weaken you apply per point.",
           "+5% increased magnitude of Weaken you apply per point."),
    Rework("Wearing Them Down",
           "+2% increased Damage Reduction per point against enemies you have "
           "Weakened.",
           "+1% Damage Reduction per point against enemies you have Weakened."),
    Rework("Never Still",
           "+1% increased Movement Speed per point.",
           "+1% increased Movement Speed per point, and another +1% per point while out "
           "of combat."),
    Rework("Hunter's Pace",
           "+1% increased Movement Speed and +1% increased Attack Speed per point.",
           "For 4 seconds after you use a Movement skill, +2% increased Attack Speed "
           "per point."),
    Rework("Blood Drinker",
           "+1% increased Life Leech per point.",
           "+1% Life Leech per point."),
    Rework("Onset",
           "+2% increased Attack Damage per point while an enemy is within 4 metres.",
           "Your first hit against each enemy ignores 5% of its Armor per point."),
    Rework("Pressing",
           "+1% increased Movement Speed per point.",
           "While your Fervour is full, +3% increased Movement Speed per point."),
    Rework("Relentless",
           "Your Movement Speed cannot be reduced by any effect.",
           "Your Movement Speed and Attack Speed cannot be reduced by any effect, and "
           "you gain 1% increased Attack Speed for every 2% increased Movement Speed "
           "you have."),
    Rework("Held Ground",
           "+2% increased Fervour gained from enemies near you per point.",
           "+5% increased Fervour gained per point."),
    Rework("Stoked Further",
           "+2% increased maximum Fervour per point.",
           "+5 maximum Fervour per point."),
    Rework("Banked Ruin",
           "+1% increased Damage Reduction per point for every full 25 Fervour you "
           "currently hold.",
           "+1% increased Attack Speed per point for every full 20 Fervour you hold."),
    Rework("Sustained Assault",
           "+1% increased Life Leech per point.",
           "After 5 seconds in combat, +3% increased Attack Speed per point."),
    Rework("No Ground Given",
           "Your Fervour does not decay while an enemy is within 8 metres of you, "
           "rather than 4.",
           "Your Fervour does not decay."),
    # WADE IN AND HEADLONG ARE REORDERED FROM THE AUDIT'S SENTENCES, a judgement:
    # "3% increased Armor and 2% more Attack Damage" puts both magnitude words
    # within one clause, which issue #582's rule refuses because a reader cannot
    # tell which number is which. The same two numbers, the "more" one first, or
    # in a sentence of its own.
    OptionRework("The First Onslaught", 1, "Wade In",
                 "Each enemy within 4 metres of you grants 3% increased Armor and 3% "
                 "increased Attack Damage.",
                 "Each enemy within 4 metres of you grants 2% more Attack Damage and 3% "
                 "increased Armor."),
    OptionRework("The First Onslaught", 2, "Never Lets Go",
                 "Enemies you hit are Crippled for 4 seconds, and your attacks deal 20% "
                 "increased damage to Crippled enemies.",
                 "Enemies you hit are Crippled for 4 seconds, and your attacks deal 20% "
                 "more damage to Crippled enemies."),
    OptionRework("The First Onslaught", 3, "Headlong",
                 "+25% increased Movement Speed, and your first melee attack after "
                 "moving 5 metres deals 50% increased damage.",
                 "+25% increased Movement Speed. Your first melee attack after moving 5 "
                 "metres deals 50% more damage."),
    OptionRework("The Second Onslaught", 3, "Long Hold",
                 "Killing an enemy restores 5% of your maximum health.",
                 "Killing an enemy restores 1% of your maximum health for each enemy "
                 "within 4 metres of you."),
    OptionRework("The Third Onslaught", 2, "Weight Against Them",
                 "Your Armor is also offence: +1% increased Attack Damage for every 2% "
                 "of Damage Reduction you have.",
                 "Your Ultimate costs 5 less Fervour for each enemy within 4 metres of "
                 "you when you use it.",
                 name="Thronged"),
    # "FOR EACH ENEMY WITHIN 8 METRES" RATHER THAN THE AUDIT'S "FOR EACH ENEMY
    # HELD THIS WAY". The same enemies -- the option holds every enemy within 8
    # metres -- in the words the scale's wording check reads. A judgement.
    OptionRework("The Final Onslaught", 1, "Nowhere to Run",
                 "Enemies within 8 metres of you cannot move away from you. They may "
                 "move toward you or around you, but not further away. Your Fervour does "
                 "not decay while any enemy is held this way.",
                 "Enemies within 8 metres of you cannot move away from you. They may "
                 "move toward you or around you, but not further away. Your Fervour does "
                 "not decay while any enemy is held this way, and you deal 2% more damage "
                 "for each enemy within 8 metres of you."),
)


# --------------------------------------------------------------------------
# The Ritualist
# --------------------------------------------------------------------------

RITUALIST_FERVOUR_BEFORE = (
    "Your minions generate Fervour: 1 per second for each minion you have, and 5 when "
    "one of them dies. An enemy that dies carrying a curse you laid on it rises as a "
    "lesser imp that fights for you for 20 seconds and reserves 10 Fervour while it is "
    "out, if you have the Fervour to reserve. While you have this, Fervour does not "
    "decay, so what you hold is held until you spend it or reserve it.")

RITUALIST = (
    # P-S4: the game asks whether the MAXIMUM has room, not what is on hand
    # (`UCataclysmCommand::HasRoomToReserve`).
    Rework("Fervour", RITUALIST_FERVOUR_BEFORE,
           RITUALIST_FERVOUR_BEFORE.replace("if you have the Fervour to reserve",
                                            "if your maximum Fervour has room for it")),
    Rework("Whispered Word",
           "+2% increased Spell Damage per point.",
           "+2% increased Spell Damage per point. At 3 points: curses and burns you lay "
           "last 25% longer."),
    Rework("Binding Sigils",
           "+2% increased Fervour gained from your minions per point.",
           "+1% increased Spell Damage per point for every 25 Fervour you hold."),
    Rework("Conduit",
           "Damage dealt by your minions counts as damage you dealt, for every effect "
           "of yours that asks.",
           "Your minions' hits and kills count as yours: they set off your on-hit and "
           "on-kill effects, and an enemy a minion hits counts as one you have "
           "damaged."),
    Rework("Grip of the Pact",
           "+2% increased Spell Damage per point.",
           "+2% increased Spell Damage per point. At 4 points: enemies carrying a curse "
           "or a burn you laid deal 10% less damage."),
    # NOT THE AUDIT'S SENTENCE, WHICH WOULD NOT BE TRUE. The audit wrote "when you
    # kill an enemy, each curse and burn it carries has a 50% chance to pass", on
    # the understanding that the stat it named does that. It does not:
    # `debuff_spread_on_death_chance` (the Masochist's Empathic Link) rolls on
    # every enemy's death, whoever killed it, and for every debuff it carries,
    # bleeds included (`ACataclysmEnemyCharacter::HandleDeath`). This sentence is
    # what the row does. A passed curse is credited to the killer, so its new
    # carrier still rises as an imp for the Ritualist. A judgement, made rather
    # than a new stat of its own; recorded in docs/DECISIONS.md.
    Rework("Broken Will",
           "+2% increased Spell Damage per point against enemies below half health.",
           "+2% increased Spell Damage per point against enemies below half health. At "
           "4 points: when an enemy dies, each debuff it carries has a 50% chance to "
           "pass to a random enemy within 6 metres."),
    Rework("Subjugator",
           "+2% increased Spell Damage per point.",
           "+2% increased Spell Damage per point. At 6 points: +20% increased Spell "
           "Damage against enemies carrying your curse."),
    # THE THRESHOLD IS A JUDGEMENT. The audit gave Long Servitude its clause and
    # not its N; 4 is half the node's 8, the point Grip of the Pact and Steady
    # Hand use.
    Rework("Long Servitude",
           "+2% increased Minion Damage per point.",
           "+2% increased Minion Damage per point. At 4 points: your minions deal 20% "
           "increased damage to enemies carrying your curse."),
    # P-B1 FIRST, THEN P-B2: the explosion node moves to Every One Bursts' parent,
    # and the place it leaves becomes Legion.
    SwapToParentOf(node="Volatile", child="Every One Bursts", partner="Spiteful Ash"),
    Rework("Spiteful Ash",
           "+3% increased damage over time damage per point.",
           "+1% increased Minion Damage per point for each minion you have.",
           name="Legion"),
    Rework("Short Leash",
           "+3% increased Area of Effect per point.",
           "+3% increased Minion Damage per point against enemies below half health."),
    Rework("The Swarm",
           "Your minions reserve 12% reduced Fervour.",
           "When you or one of your minions kills an enemy, a new minion is summoned "
           "where it fell, if your Fervour has room for it."),
    Rework("Rekindled",
           "When a minion explodes a new minion is summoned where it stood, no more "
           "than once every 5 seconds.",
           "When one of your minions reaches the end of its time, it stays for that "
           "long again, no more than once every 5 seconds."),
    Rework("Distance",
           "+1% increased Movement Speed per point.",
           "+1% increased Spell Damage per point for every 2 metres between you and the "
           "enemy your spell hits."),
    Rework("Unhurried",
           "+2% increased Crowd Control Resistance per point.",
           "+2 Crowd Control Resistance per point."),
    Rework("Steady Hand",
           "+2% increased Spell Damage per point.",
           "+2% increased Spell Damage per point. At 4 points: 2% of the damage your "
           "spells deal is leeched as Energy Shield."),
    Rework("Ritual Focus",
           "Skills you cast while standing still cost no mana.",
           "Skills you cast while standing still cost no mana and have 30% increased "
           "Area of Effect."),
    Rework("Measured Words",
           "+2% increased Spell Damage per point.",
           "+2% increased Spell Damage per point. At 4 points: while you have not moved "
           "for 2 seconds, your minions deal 20% increased damage."),
    OptionRework("The First Pact", 1, "Bound Servants",
                 "Your minions deal 25% increased damage and have 25% increased health.",
                 "Your minions deal 5% increased damage and have 5% increased health for "
                 "each minion you have."),
    OptionRework("The First Pact", 2, "Warded Flesh",
                 "+40% increased Maximum Energy Shield and +20% increased Energy Shield "
                 "Regeneration.",
                 "Each time one of your minions dies, you regain 10% of your Maximum "
                 "Energy Shield."),
    OptionRework("The First Pact", 3, "Open Channel",
                 "+20% increased maximum Fervour and +20% increased Mana Regeneration.",
                 "An enemy that dies carrying your curse raises two imps rather than "
                 "one."),
    # THE NAME IS A JUDGEMENT: the audit replaced the option and named nothing.
    OptionRework("The Second Pact", 2, "Fed by the Fallen",
                 "You gain 10 Fervour whenever an enemy dies within 10 metres of you.",
                 "You take 1% less damage for each minion you have, up to 20% less.",
                 name="Wall of Servants"),
    OptionRework("The Third Pact", 1, "Set the Pack On",
                 "Enemies you have damaged in the last 2 seconds take 25% more damage "
                 "from your minions.",
                 "Enemies you have damaged in the last 2 seconds take 3% more damage from "
                 "your minions for each minion you have."),
)

REWORKS: dict[str, tuple] = {
    "Masochist": MASOCHIST,
    "Ravager": RAVAGER,
    "Ritualist": RITUALIST,
}


# --------------------------------------------------------------------------
# Applying a change
# --------------------------------------------------------------------------

def _named(data: dict, name: str) -> list[dict]:
    return [n for n in data["nodes"] if n.get("data", {}).get("name") == name]


def apply_rework(data: dict, fix: Rework, tree: str) -> list[str]:
    done = _named(data, fix.new_name)
    if len(done) == 1 and done[0]["data"].get("description") == fix.after:
        return []
    node = order_fixes.node_named(data, fix.node, tree)
    now = node["data"].get("description", "")
    if now != fix.before:
        raise Refused(f"{tree}: {fix.node!r} reads {now!r}, which is neither the "
                      f"sentence the rework was approved against nor the new one")
    if fix.name and _named(data, fix.name):
        raise Refused(f"{tree}: {fix.node!r} is to be renamed {fix.name!r}, and a node "
                      f"of that name already exists")
    node["data"]["description"] = fix.after
    if fix.name:
        node["data"]["name"] = fix.name
        return [f"renamed {fix.node!r} to {fix.name!r} and rewrote it ({node['id']})"]
    return [f"rewrote {fix.node!r} ({node['id']})"]


def apply_option_rework(data: dict, fix: OptionRework, tree: str) -> list[str]:
    capstone = order_fixes.node_named(data, fix.capstone, tree)
    if capstone.get("type") != "capstone":
        raise Refused(f"{tree}: {fix.capstone!r} is a {capstone.get('type')}, not a "
                      f"capstone")
    options = capstone["data"].get("options") or []
    if not 1 <= fix.option <= len(options):
        raise Refused(f"{tree}: {fix.capstone!r} has {len(options)} options and the "
                      f"rework names option {fix.option}")
    option = options[fix.option - 1]
    if option.get("name") == fix.new_name and option.get("description") == fix.after:
        return []
    if option.get("name") != fix.before_name or option.get("description") != fix.before:
        raise Refused(f"{tree}: {fix.capstone!r} option {fix.option} is "
                      f"{option.get('name')!r}, {option.get('description')!r}, which is "
                      f"neither the option the rework was approved against nor the new "
                      f"one")
    option["description"] = fix.after
    if fix.name:
        option["name"] = fix.name
        return [f"replaced {fix.capstone!r} option {fix.option}, {fix.before_name!r}, "
                f"with {fix.name!r}"]
    return [f"rewrote {fix.capstone!r} option {fix.option}, {fix.before_name!r}"]


def _parents(data: dict, child_id: str) -> set[str]:
    return {e["source"] for e in data["edges"] if e["target"] == child_id}


def apply_swap_to_parent_of(data: dict, fix: SwapToParentOf, tree: str) -> list[str]:
    child = order_fixes.node_named(data, fix.child, tree)
    parents = _parents(data, child["id"])
    node = order_fixes.node_named(data, fix.node, tree)
    if node["id"] in parents:
        return []
    partner = order_fixes.node_named(data, fix.partner, tree)
    if partner["id"] not in parents:
        raise Refused(f"{tree}: neither {fix.node!r} nor {fix.partner!r} is the parent "
                      f"of {fix.child!r}, so the swap the rework was approved for does "
                      f"not apply")
    if node["data"].get("maxPoints") != partner["data"].get("maxPoints"):
        raise Refused(f"{tree}: {fix.node!r} holds {node['data'].get('maxPoints')} "
                      f"points and {fix.partner!r} {partner['data'].get('maxPoints')}; "
                      f"the swap was approved for two nodes of one size")
    moved = order_fixes.swap_content(partner, node, tree)
    return [f"moved {fix.node!r} to the parent of {fix.child!r}: {moved}"]


def graph_of(data: dict) -> tuple:
    """Everything the rework must leave alone: identifiers, kinds, positions,
    maximum points and edges."""
    nodes = sorted((n["id"], n.get("type"), json.dumps(n.get("position"), sort_keys=True),
                    int(n["data"].get("maxPoints", 0))) for n in data["nodes"])
    edges = sorted(json.dumps(e, sort_keys=True) for e in data["edges"])
    return tuple(nodes), tuple(edges)


def apply_rework_to_tree(data: dict, tree: str,
                         only: frozenset[str] | None = None) -> list[str]:
    """Apply every change for `tree` to `data` in place; return what changed."""
    if only is not None and tree not in only:
        return []
    fixes = REWORKS.get(tree, ())
    if not fixes:
        return []

    # AFTER THE ORDER FIXES, NEVER BEFORE.
    pending = order_fixes.apply_fixes(copy.deepcopy(data), tree)
    if pending:
        raise Refused(f"{tree}: the passive order fixes of 2026-11-24 have not been "
                      f"applied to this tree ({'; '.join(pending)}). Run "
                      f"tools/apply_passive_order_fixes_2026_11_24.py on it first")

    before = graph_of(data)
    changes: list[str] = []
    for fix in fixes:
        if isinstance(fix, Rework):
            changes += apply_rework(data, fix, tree)
        elif isinstance(fix, OptionRework):
            changes += apply_option_rework(data, fix, tree)
        elif isinstance(fix, SwapToParentOf):
            changes += apply_swap_to_parent_of(data, fix, tree)
        else:  # pragma: no cover - a new kind of change without a branch here
            raise TypeError(f"no way to apply {fix!r}")

    if changes:
        if graph_of(data) != before:
            raise Refused(f"{tree}: the rework changed an identifier, a kind, a "
                          f"position, a maximum or an edge, and it was approved as "
                          f"changing none")
        order_fixes.check_structure(data, tree)
    return changes


# --------------------------------------------------------------------------
# Files
# --------------------------------------------------------------------------

@dataclasses.dataclass
class Outcome:
    path: pathlib.Path
    tree: str
    changes: list[str]
    new_bytes: bytes | None


def plan_file(path: pathlib.Path, allow_reformat: bool = False,
              only: frozenset[str] | None = None) -> Outcome:
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise Refused(f"{path.name} is not a JSON tree file: {error}") from error
    if not isinstance(data, dict) or "nodes" not in data or "edges" not in data:
        raise Refused(f"{path.name} has no nodes and edges, so it is not a tree file")

    tree = order_fixes.tree_name(data, path)
    form = order_fixes.format_of(raw, data)
    if form is None and not allow_reformat:
        raise Refused(f"{path.name}: no two-space JSON serialisation reproduces the file "
                      f"byte for byte, so writing it would reformat every line. Pass "
                      f"--allow-reformat to write it anyway")
    form = form or order_fixes.Format("\r\n" if b"\r\n" in raw else "\n",
                                      "\r\n" if raw.endswith(b"\r\n") else
                                      "\n" if raw.endswith(b"\n") else "", True)

    changes = apply_rework_to_tree(data, tree, only)
    new_bytes = form.write(data) if changes else None
    if new_bytes == raw:
        new_bytes = None
    return Outcome(path, tree, changes, new_bytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Apply the Demonic passive tree rework of 2026-11-24 by node name.")
    parser.add_argument("paths", nargs="+",
                        help="tree JSON files, or directories holding *_Class_Tree_*.json")
    parser.add_argument("--check", action="store_true",
                        help="write nothing; exit 1 if any file would change")
    parser.add_argument("--allow-reformat", action="store_true",
                        help="write a file even when its format cannot be reproduced")
    parser.add_argument("--only", action="append", choices=sorted(REWORKS), metavar="TREE",
                        help="apply only this tree's changes; repeat for several. "
                             f"One of: {', '.join(sorted(REWORKS))}")
    args = parser.parse_args(argv)
    only = frozenset(args.only) if args.only else None

    try:
        paths = order_fixes.tree_files(args.paths)
        outcomes = [plan_file(p, args.allow_reformat, only) for p in paths]
    except Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2

    pending = 0
    for outcome in outcomes:
        if outcome.tree not in REWORKS:
            print(f"{outcome.path}: the {outcome.tree} tree has no rework; left alone")
            continue
        if only is not None and outcome.tree not in only:
            print(f"{outcome.path}: the {outcome.tree} tree is not among --only; left alone")
            continue
        if outcome.new_bytes is None:
            print(f"{outcome.path}: the {outcome.tree} tree is already reworked; left alone")
            continue
        pending += 1
        verb = "would change" if args.check else "changed"
        print(f"{outcome.path}: the {outcome.tree} tree {verb}:")
        for line in outcome.changes:
            print(f"  {line}")

    if args.check:
        print(f"{pending} of {len(outcomes)} file(s) would change.")
        return 1 if pending else 0

    for outcome in outcomes:
        if outcome.new_bytes is not None:
            outcome.path.write_bytes(outcome.new_bytes)
    print(f"{pending} of {len(outcomes)} file(s) changed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
